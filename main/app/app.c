#include "app.h"

#include "hal/hal_config.h"
#include "hal/hal_backlight.h"
#include "hal/hal_i2c.h"
#include "drivers/display/display.h"
#include "drivers/ch338x/ch338x.h"
#include "drivers/display/touch/touch.h"
#include "app_config.h"
#include "lvgl_mgr/lvgl_mgr.h"
#include "services/weather_service.h"
#include "services/power_monitor.h"
#include "services/usb_hub_service.h"
#include "services/settings_store.h"
#include "services/wifi_service.h"
#include "ui/usb_switch/usb_switch.h"
#include "ui/weather_dock/weather_dock.h"
#include "ui/theme/ui_theme.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>

static const char *TAG = "app";

/* Runs on the LVGL task (via lvgl_mgr_post). */
static void apply_weather(void *arg)
{
    weather_dock_update((weather_snapshot_t *)arg);
}

/* Runs on the weather task; hands the snapshot over to the LVGL task. */
static void on_weather_snapshot(weather_snapshot_t *snap, void *ctx)
{
    if (lvgl_mgr_post(apply_weather, snap) != ESP_OK) {
        ESP_LOGW(TAG, "UI queue full, dropping weather update");
        weather_snapshot_free(snap);
    }
}

/* Runs on the LVGL task; takes ownership of `arg`. */
static void apply_hub_state(void *arg)
{
    usb_hub_state_t *st = arg;
    for (uint8_t i = 0; i < USB_HUB_PORT_COUNT; i++) {
        usb_switch_set_port(i, st->power_on[i]);
        usb_switch_set_port_status(i, (st->power_on[i] && st->fault[i]) ? USB_PORT_STATUS_FAULT : USB_PORT_STATUS_NORMAL);
    }
    usb_switch_set_speed_mode(st->high_speed ? USB_SPEED_MODE_HIGH : USB_SPEED_MODE_FULL);
    free(st);
}

/* Runs on the hub polling task (or whichever task changed a port). */
static void on_hub_state(const usb_hub_state_t *state, void *ctx)
{
    usb_hub_state_t *copy = malloc(sizeof(*copy));
    if (copy == NULL) {
        return;
    }
    *copy = *state;
    if (lvgl_mgr_post(apply_hub_state, copy) != ESP_OK) {
        free(copy);
    }
}

/* Runs on the LVGL task: a port card was tapped. The card itself only changes
 * once the hub state comes back through on_hub_state(). */
static void on_port_request(uint8_t index, bool on)
{
    if (usb_hub_set_port_power(index, on) != ESP_OK) {
        ESP_LOGW(TAG, "USB%d power %s failed", index + 1, on ? "restore" : "cut");
    }
}

static void start_weather(void)
{
    weather_provider_config_t weather_cfg = { 0 };
    strlcpy(weather_cfg.city, APP_WEATHER_CITY, sizeof(weather_cfg.city));
    strlcpy(weather_cfg.adcode, APP_WEATHER_ADCODE, sizeof(weather_cfg.adcode));
    strlcpy(weather_cfg.api_key, APP_UAPIS_API_KEY, sizeof(weather_cfg.api_key));
    strlcpy(weather_cfg.qweather_host, APP_QWEATHER_HOST, sizeof(weather_cfg.qweather_host));
    strlcpy(weather_cfg.qweather_key, APP_QWEATHER_API_KEY, sizeof(weather_cfg.qweather_key));
    strlcpy(weather_cfg.qweather_location, APP_QWEATHER_LOCATION, sizeof(weather_cfg.qweather_location));
#if APP_WEATHER_PROVIDER == APP_WEATHER_PROVIDER_QWEATHER
    const weather_provider_t *weather_provider = &weather_provider_qweather;
#else
    const weather_provider_t *weather_provider = &weather_provider_uproapi;
#endif
    ESP_ERROR_CHECK(weather_service_start(on_weather_snapshot, NULL, APP_WEATHER_REFRESH_MS,
                                          weather_provider, &weather_cfg));
}

#define BACKLIGHT_ON_DELAY_MS 150 /* a few LVGL refresh periods for the first page to reach the panel */

static void backlight_on_cb(lv_timer_t *timer)
{
    (void)timer;
    hal_backlight_set_enabled(true);
}

/* Runs on the LVGL task right after the first page was loaded. */
static void schedule_backlight_on(void *arg)
{
    (void)arg;
    lv_timer_t *t = lv_timer_create(backlight_on_cb, BACKLIGHT_ON_DELAY_MS, NULL);
    lv_timer_set_repeat_count(t, 1); /* deleted by LVGL after it fires */
}

void app_main_start(void)
{
    settings_store_init();
    hal_i2c_init();
    touch_prepare(); /* before the LCD reset, see touch.h */
    display_init();
    if (touch_init() != ESP_OK) {
        ESP_LOGE(TAG, "touch unavailable, continuing without it");
    }
    hal_backlight_init();
    /* PWM level only; the backlight itself stays off (see hal_backlight_init)
     * until the first page has been drawn, so the LVGL default white screen is
     * never visible. */
    settings_store_set_brightness(settings_store_get_brightness());

    /* One app-wide theme, set before any page is created; switch later with
     * ui_theme_set(). */
    if (lvgl_port_lock(0)) {
        ui_theme_init(settings_store_get_dark_mode() ? UI_THEME_DARK : UI_THEME_LIGHT);
        lvgl_port_unlock();
    }
    ESP_ERROR_CHECK(lvgl_mgr_init(display_get_lv_display()));
    ESP_ERROR_CHECK(lvgl_mgr_switch_root("weather_dock"));
    ESP_ERROR_CHECK(lvgl_mgr_post(schedule_backlight_on, NULL)); /* queued behind the page switch */

    usb_switch_set_port_request_cb(on_port_request);
    if (usb_hub_service_start(on_hub_state, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "USB hub I/O expander unavailable, port control disabled");
    }

    /* Provisioning mode when the device was never configured or the user asked
     * for it in settings; otherwise connect with the saved credentials. */
    char ssid[33] = { 0 };
    char password[64] = { 0 };
    bool has_wifi = settings_store_get_wifi(ssid, sizeof(ssid), password, sizeof(password));
    if (!has_wifi || settings_store_get_config_mode()) {
        if (wifi_service_start_provisioning() != ESP_OK) {
            ESP_LOGE(TAG, "provisioning start failed");
        }
        /* Show the AP name / address right away instead of an empty dock. */
        lvgl_mgr_push("settings");
    } else {
        ESP_ERROR_CHECK(wifi_service_start(ssid, password));
        start_weather();
    }

    /* Probe the hub controller (read-only). */
    ch338x_handle_t hub = ch338x_init(hal_i2c_power_bus(), HAL_CH338X_I2C_ADDRESS);
    uint16_t vid, pid;
    ch338x_speed_t speed;
    if (hub != NULL && ch338x_read_ids(hub, &vid, &pid) == ESP_OK && ch338x_get_speed(hub, &speed) == ESP_OK) {
        ESP_LOGI(TAG, "CH338X vid=%04x pid=%04x speed=%s", vid, pid, speed == CH338X_SPEED_HIGH ? "high" : "full");
    } else {
        ESP_LOGW(TAG, "CH338X not responding");
    }

    if (power_monitor_start() != ESP_OK) {
        ESP_LOGE(TAG, "power monitor unavailable");
    }

    ESP_LOGI(TAG, "app started");
}
