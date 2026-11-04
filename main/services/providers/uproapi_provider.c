#include "services/weather_provider.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "protocols/https_request.h"
#include "provider_json.h"

static const char *TAG = "uproapi";

/* Full API address from the docs. Only the modules the UI shows are switched
 * on (extended/indices are off by default and not needed). */
#define UPROAPI_BASE_URL   "https://uapis.cn/api/v1/misc/weather"
#define UPROAPI_QUERY      "forecast=true&hourly=true&minutely=true&lang=zh"
#define UPROAPI_TIMEOUT_MS 15000
#define UPROAPI_URL_MAX    384

/* The UI charts the next ~2 h in 5-minute steps; the API reports every 2 min. */
#define MINUTELY_STEP_MIN 5

/* "2026-10-06T18:31:11+08:00" -> minute of day, or -1. */
static int minute_of_day(const char *iso)
{
    if (iso == NULL || strlen(iso) < 16 || iso[10] != 'T') {
        return -1;
    }
    return atoi(iso + 11) * 60 + atoi(iso + 14);
}

static void parse_current(const cJSON *root, weather_snapshot_t *out)
{
    out->current.weather_cn = weather_snapshot_set_text(out->current_text, json_str(root, "weather"));
    out->current.temperature = round_i16(json_num(root, "temperature", 0));
    out->current.temp_max = round_i16(json_num(root, "temp_max", out->current.temperature));
    out->current.temp_min = round_i16(json_num(root, "temp_min", out->current.temperature));
    out->current.humidity = (uint8_t)json_num(root, "humidity", 0);
}

static void parse_hourly(const cJSON *root, weather_snapshot_t *out)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "hourly_forecast");
    const cJSON *item;
    int first_hour = -1;
    cJSON_ArrayForEach(item, arr) {
        uint8_t i = out->hourly_count;
        if (i >= WEATHER_SNAPSHOT_HOURLY_MAX) {
            break;
        }
        /* Hours from the first entry; the docs don't promise the list starts
         * at the current hour, so don't assume offset == index. `time` is
         * either ISO8601 or "YYYY-MM-DD HH:MM", hour sits at index 11 in both. */
        const char *t = json_str(item, "time");
        int hour = (t != NULL && strlen(t) >= 13) ? atoi(t + 11) : -1;
        if (first_hour < 0) {
            first_hour = hour;
        }
        out->hourly[i].hour_offset = (hour >= 0 && first_hour >= 0) ? (uint8_t)((hour - first_hour + 24) % 24) : i;
        out->hourly[i].weather_cn = weather_snapshot_set_text(out->hourly_text[i], json_str(item, "weather"));
        out->hourly[i].temperature = round_i16(json_num(item, "temperature", 0));
        out->hourly_count++;
    }
}

static void parse_minutely(const cJSON *root, weather_snapshot_t *out)
{
    const cJSON *block = cJSON_GetObjectItemCaseSensitive(root, "minutely_precip");
    if (!cJSON_IsObject(block)) {
        block = cJSON_GetObjectItemCaseSensitive(root, "minutely_forecast");
    }
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(block, "data");
    if (!cJSON_IsArray(data) || cJSON_GetArraySize(data) == 0) {
        return;
    }

    int start = minute_of_day(json_str(cJSON_GetArrayItem(data, 0), "time"));
    if (start < 0) {
        return;
    }

    /* Resample: for each 5-minute mark take the first report at or after it. */
    const cJSON *item = NULL;
    int next_target = 0;
    cJSON_ArrayForEach(item, data) {
        int minute = minute_of_day(json_str(item, "time"));
        if (minute < 0) {
            continue;
        }
        int offset = (minute - start + 1440) % 1440;
        while (offset >= next_target && out->minutely_count < WEATHER_SNAPSHOT_MINUTELY_MAX) {
            uint8_t i = out->minutely_count++;
            out->minutely[i].minute_offset = (uint16_t)next_target;
            double precip = json_num(item, "precip", 0);
            out->minutely[i].precip_x10 = precip > 0 ? (uint16_t)lround(precip * 10) : 0;
            next_target += MINUTELY_STEP_MIN;
        }
    }
}

static void parse_daily(const cJSON *root, weather_snapshot_t *out)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "forecast");
    const cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        uint8_t i = out->daily_count;
        if (i >= WEATHER_SNAPSHOT_DAILY_MAX) {
            break;
        }
        out->daily[i].day_offset = i;
        out->daily[i].weather_cn = weather_snapshot_set_text(out->daily_text[i], json_str(item, "weather_day"));
        out->daily[i].temp_max = round_i16(json_num(item, "temp_max", 0));
        out->daily[i].temp_min = round_i16(json_num(item, "temp_min", 0));
        out->daily_count++;
    }
}

/* Appends `src` percent-encoded (UTF-8 bytes, RFC 3986 unreserved kept).
 * Returns false if it doesn't fit. */
static bool append_encoded(char *dst, size_t cap, size_t *len, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        bool plain = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                     *p == '-' || *p == '_' || *p == '.' || *p == '~';
        size_t need = plain ? 1 : 3;
        if (*len + need + 1 > cap) {
            return false;
        }
        if (plain) {
            dst[(*len)++] = (char)*p;
        } else {
            dst[(*len)++] = '%';
            dst[(*len)++] = hex[*p >> 4];
            dst[(*len)++] = hex[*p & 0x0F];
        }
    }
    dst[*len] = '\0';
    return true;
}

/* Location: adcode wins over city (per the docs); neither -> the API locates
 * the client by IP. Invalid values are dropped rather than sent. */
static bool build_url(const weather_provider_config_t *cfg, char *url, size_t cap)
{
    int n = snprintf(url, cap, "%s?%s", UPROAPI_BASE_URL, UPROAPI_QUERY);
    if (n < 0 || (size_t)n >= cap) {
        return false;
    }
    size_t len = (size_t)n;

    bool adcode_ok = cfg->adcode[0] != '\0';
    for (const char *p = cfg->adcode; adcode_ok && *p; p++) {
        adcode_ok = (*p >= '0' && *p <= '9');
    }
    if (cfg->adcode[0] != '\0' && !adcode_ok) {
        ESP_LOGW(TAG, "adcode '%s' is not numeric, ignoring", cfg->adcode);
    }

    if (adcode_ok) {
        return append_encoded(url, cap, &len, "&adcode=") && append_encoded(url, cap, &len, cfg->adcode);
    }
    if (cfg->city[0] != '\0') {
        return append_encoded(url, cap, &len, "&city=") && append_encoded(url, cap, &len, cfg->city);
    }
    return true;
}

static bool uproapi_fetch(const weather_provider_config_t *cfg, weather_snapshot_t *out)
{
    char url[UPROAPI_URL_MAX];
    if (!build_url(cfg, url, sizeof(url))) {
        ESP_LOGE(TAG, "request URL too long");
        return false;
    }
    if (cfg->api_key[0] != '\0' && strncmp(cfg->api_key, "uapi-", 5) != 0) {
        ESP_LOGW(TAG, "API key doesn't start with 'uapi-', sending anyway");
    }

    https_response_t resp;
    char bearer[sizeof(cfg->api_key) + 8];
    snprintf(bearer, sizeof(bearer), "Bearer %s", cfg->api_key);
    bool has_key = cfg->api_key[0] != '\0';
    esp_err_t err = https_get(url, UPROAPI_TIMEOUT_MS, has_key ? "Authorization" : NULL, bearer, &resp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "request failed: %s", esp_err_to_name(err));
        https_response_free(&resp);
        return false;
    }
    if (resp.status != 200 || resp.body == NULL) {
        /* Documented error bodies: {"code": "...", "message": "..."} for
         * 400 INVALID_PARAMETER / 404 NOT_FOUND / 500 INTERNAL_SERVER_ERROR /
         * 503 SERVICE_UNAVAILABLE. Other statuses (e.g. rate limiting) aren't
         * documented; they're reported the same way. */
        const char *hint = resp.status == 400 ? " (check city/adcode/lang)"
                         : resp.status == 404 ? " (city not found, check APP_WEATHER_CITY)"
                         : resp.status == 429 ? " (rate limited)"
                         : (resp.status == 401 || resp.status == 403) ? " (check API key)" : "";
        ESP_LOGE(TAG, "HTTP %d%s: %.200s", resp.status, hint, resp.body ? resp.body : "");
        https_response_free(&resp);
        return false;
    }

    provider_json_use_psram(true);
    cJSON *root = cJSON_Parse(resp.body);
    https_response_free(&resp);

    bool ok = false;
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON parse failed");
    } else if (json_str(root, "weather") == NULL) {
        ESP_LOGE(TAG, "response has no 'weather' (code=%s message=%s)",
                 json_str(root, "code") ? json_str(root, "code") : "?",
                 json_str(root, "message") ? json_str(root, "message") : "?");
    } else {
        parse_current(root, out);
        parse_hourly(root, out);
        parse_minutely(root, out);
        parse_daily(root, out);
        ESP_LOGI(TAG, "%s %dC (%d~%d), hourly=%u minutely=%u daily=%u",
                 out->current.weather_cn, out->current.temperature, out->current.temp_min,
                 out->current.temp_max, out->hourly_count, out->minutely_count, out->daily_count);
        ok = true;
    }
    cJSON_Delete(root);
    provider_json_use_psram(false);
    return ok;
}

const weather_provider_t weather_provider_uproapi = {
    .name = "uproapi",
    .fetch = uproapi_fetch,
};
