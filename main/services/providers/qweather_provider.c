#include "services/weather_provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>
#include <esp_log.h>

#include "protocols/https_request.h"
#include "provider_json.h"

/* QWeather (和风天气) v7 REST API. Needs the account's own API host and an API
 * key (console -> settings); `location` is "longitude,latitude" because the
 * minutely endpoint only accepts coordinates. Four requests per refresh:
 *   /v7/weather/now   current conditions
 *   /v7/weather/24h   hourly forecast
 *   /v7/weather/7d    daily forecast (also gives today's high/low)
 *   /v7/minutely/5m   2 h precipitation in 5-minute steps (mainland China only) */

static const char *TAG = "qweather";

#define QWEATHER_TIMEOUT_MS 15000
#define QWEATHER_URL_MAX    256
#define MINUTELY_STEP_MIN   5

/* Fetches `path`, checks HTTP status and the body's "code", returns the parsed
 * JSON (caller cJSON_Delete()s) or NULL. */
static cJSON *qw_get(const weather_provider_config_t *cfg, const char *path)
{
    char url[QWEATHER_URL_MAX];
    int n = snprintf(url, sizeof(url), "https://%s%s?location=%s", cfg->qweather_host, path, cfg->qweather_location);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        ESP_LOGE(TAG, "request URL too long");
        return NULL;
    }

    https_response_t resp;
    esp_err_t err = https_get(url, QWEATHER_TIMEOUT_MS, "X-QW-Api-Key", cfg->qweather_key, &resp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: request failed: %s", path, esp_err_to_name(err));
        https_response_free(&resp);
        return NULL;
    }
    if (resp.status != 200 || resp.body == NULL) {
        const char *hint = (resp.status == 401 || resp.status == 403) ? " (check API host / API key)"
                         : resp.status == 429 ? " (rate limited)" : "";
        ESP_LOGE(TAG, "%s: HTTP %d%s: %.200s", path, resp.status, hint, resp.body ? resp.body : "");
        https_response_free(&resp);
        return NULL;
    }

    cJSON *root = cJSON_Parse(resp.body);
    https_response_free(&resp);
    if (root == NULL) {
        ESP_LOGE(TAG, "%s: JSON parse failed", path);
        return NULL;
    }
    const char *code = json_str(root, "code");
    if (code == NULL || strcmp(code, "200") != 0) {
        /* e.g. "204" = no data for this location, "400"/"401"/"402"/"403"/"404"... */
        ESP_LOGE(TAG, "%s: API code=%s", path, code ? code : "?");
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

/* "2020-06-30T22:00+08:00" / "2020-06-30" -> hour 0-23, or -1 if no time part. */
static int hour_of(const char *iso)
{
    return (iso != NULL && strlen(iso) >= 13 && iso[10] == 'T') ? atoi(iso + 11) : -1;
}

static bool parse_now(const cJSON *root, weather_snapshot_t *out, int *obs_hour)
{
    const cJSON *now = cJSON_GetObjectItemCaseSensitive(root, "now");
    if (!cJSON_IsObject(now) || json_str(now, "text") == NULL) {
        ESP_LOGE(TAG, "now: missing fields");
        return false;
    }
    out->current.weather_cn = weather_snapshot_set_text(out->current_text, json_str(now, "text"));
    out->current.temperature = round_i16(json_num(now, "temp", 0));
    out->current.humidity = (uint8_t)json_num(now, "humidity", 0);
    *obs_hour = hour_of(json_str(now, "obsTime"));
    return true;
}

static bool parse_daily(const cJSON *root, weather_snapshot_t *out)
{
    const cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "daily")) {
        uint8_t i = out->daily_count;
        if (i >= WEATHER_SNAPSHOT_DAILY_MAX) {
            break;
        }
        out->daily[i].day_offset = i;
        out->daily[i].weather_cn = weather_snapshot_set_text(out->daily_text[i], json_str(item, "textDay"));
        out->daily[i].temp_max = round_i16(json_num(item, "tempMax", 0));
        out->daily[i].temp_min = round_i16(json_num(item, "tempMin", 0));
        out->daily_count++;
    }
    if (out->daily_count == 0) {
        ESP_LOGE(TAG, "7d: no entries");
        return false;
    }
    /* /now has no high/low; today's come from the forecast. */
    out->current.temp_max = out->daily[0].temp_max;
    out->current.temp_min = out->daily[0].temp_min;
    return true;
}

static bool parse_hourly(const cJSON *root, weather_snapshot_t *out, int obs_hour)
{
    /* Entry 0 is the live observation (shown as "now"); the 24h forecast then
     * continues from the next full hour. Offsets come from fxTime so a gap or
     * an entry that duplicates the current hour can't skew the labels. */
    out->hourly[0].hour_offset = 0;
    out->hourly[0].temperature = out->current.temperature;
    out->hourly[0].weather_cn = weather_snapshot_set_text(out->hourly_text[0], out->current_text);
    out->hourly_count = 1;

    const cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "hourly")) {
        uint8_t i = out->hourly_count;
        if (i >= WEATHER_SNAPSHOT_HOURLY_MAX) {
            break;
        }
        int hour = hour_of(json_str(item, "fxTime"));
        if (hour < 0 || obs_hour < 0) {
            return out->hourly_count > 1;
        }
        int offset = (hour - obs_hour + 24) % 24;
        if (offset == 0) {
            continue;
        }
        out->hourly[i].hour_offset = (uint8_t)offset;
        out->hourly[i].weather_cn = weather_snapshot_set_text(out->hourly_text[i], json_str(item, "text"));
        out->hourly[i].temperature = round_i16(json_num(item, "temp", 0));
        out->hourly_count++;
    }
    return out->hourly_count > 1;
}

static void parse_minutely(const cJSON *root, weather_snapshot_t *out)
{
    const cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "minutely")) {
        uint8_t i = out->minutely_count;
        if (i >= WEATHER_SNAPSHOT_MINUTELY_MAX) {
            break;
        }
        double precip = json_num(item, "precip", 0);
        out->minutely[i].minute_offset = (uint16_t)(i * MINUTELY_STEP_MIN);
        out->minutely[i].precip_x10 = precip > 0 ? (uint16_t)lround(precip * 10) : 0;
        out->minutely_count++;
    }
}

static bool qweather_fetch(const weather_provider_config_t *cfg, weather_snapshot_t *out)
{
    if (cfg->qweather_host[0] == '\0' || cfg->qweather_key[0] == '\0' || cfg->qweather_location[0] == '\0') {
        ESP_LOGE(TAG, "APP_QWEATHER_HOST / APP_QWEATHER_API_KEY / APP_QWEATHER_LOCATION must all be set");
        return false;
    }

    bool ok = false;
    int obs_hour = -1;
    cJSON *now = NULL, *hourly = NULL, *daily = NULL, *minutely = NULL;

    provider_json_use_psram(true);
    now = qw_get(cfg, "/v7/weather/now");
    if (now == NULL || !parse_now(now, out, &obs_hour)) {
        goto done;
    }
    daily = qw_get(cfg, "/v7/weather/7d");
    if (daily == NULL || !parse_daily(daily, out)) {
        goto done;
    }
    hourly = qw_get(cfg, "/v7/weather/24h");
    if (hourly == NULL || !parse_hourly(hourly, out, obs_hour)) {
        goto done;
    }
    /* Optional: only covers mainland China, failure just hides the rain chart. */
    minutely = qw_get(cfg, "/v7/minutely/5m");
    ESP_LOGI(TAG, "%s", cJSON_Print(minutely));
    if (minutely != NULL) {
        parse_minutely(minutely, out);
    }

    ESP_LOGI(TAG, "%s %dC (%d~%d), hourly=%u minutely=%u daily=%u",
             out->current.weather_cn, out->current.temperature, out->current.temp_min,
             out->current.temp_max, out->hourly_count, out->minutely_count, out->daily_count);
    ok = true;

done:
    cJSON_Delete(now);
    cJSON_Delete(daily);
    cJSON_Delete(hourly);
    cJSON_Delete(minutely);
    provider_json_use_psram(false);
    return ok;
}

const weather_provider_t weather_provider_qweather = {
    .name = "qweather",
    .fetch = qweather_fetch,
};
