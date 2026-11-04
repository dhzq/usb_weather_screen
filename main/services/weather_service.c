#include "weather_service.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "weather_provider.h"
#include "wifi_service.h"

static const char *TAG = "weather";

#define WEATHER_TASK_STACK    12288 /* TLS handshake + JSON parsing */
#define WEATHER_TASK_PRIORITY 4
#define WEATHER_RETRY_MS      30000
#define TIME_SYNC_TIMEOUT_MS  20000

static const weather_provider_t *s_provider;

static weather_listener_t s_listener;
static void *s_listener_ctx;
static uint32_t s_interval_ms;
static weather_provider_config_t s_cfg;

static void weather_task(void *arg)
{
    wifi_service_wait_connected(UINT32_MAX);

    while (1) {
        /* TLS certificate validation needs a sane clock. */
        if (!wifi_service_wait_time_synced(TIME_SYNC_TIMEOUT_MS)) {
            ESP_LOGW(TAG, "time not synced yet, retrying");
            continue;
        }

        ESP_LOGI(TAG, "waiting for connection");
        uint32_t delay_ms = WEATHER_RETRY_MS;
        weather_snapshot_t *snap = weather_snapshot_alloc();
        if (snap == NULL) {
            ESP_LOGE(TAG, "out of memory for snapshot");
        } else if (s_provider->fetch(&s_cfg, snap)) {
            s_listener(snap, s_listener_ctx);
            delay_ms = s_interval_ms;
        } else {
            ESP_LOGW(TAG, "fetch via %s failed, retry in %d s", s_provider->name, WEATHER_RETRY_MS / 1000);
            weather_snapshot_free(snap);
        }

        /* A dropped link just makes the next fetch fail and retry; no need to
         * track it here. */
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t weather_service_start(weather_listener_t listener, void *ctx, uint32_t interval_ms,
                                const weather_provider_t *provider, const weather_provider_config_t *cfg)
{
    if (listener == NULL || interval_ms == 0 || provider == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_listener != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_listener = listener;
    s_listener_ctx = ctx;
    s_interval_ms = interval_ms;
    s_cfg = *cfg;
    s_provider = provider;

    if (xTaskCreate(weather_task, "weather", WEATHER_TASK_STACK, NULL, WEATHER_TASK_PRIORITY, NULL) != pdPASS) {
        s_listener = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "started (provider=%s, interval=%u s)", s_provider->name, (unsigned)(interval_ms / 1000));
    return ESP_OK;
}
