#include "settings_store.h"
#include "hal/hal_backlight.h"

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

static const char *TAG = "settings_store";

#define SETTINGS_NVS_NAMESPACE "settings"
#define SETTINGS_NVS_KEY_BRIGHTNESS "brightness"
#define SETTINGS_NVS_KEY_DARK_MODE "dark_mode"
#define SETTINGS_NVS_KEY_WIFI_SSID "wifi_ssid"
#define SETTINGS_NVS_KEY_WIFI_PASS "wifi_pass"
#define SETTINGS_NVS_KEY_CONFIG_MODE "config_mode"

static uint8_t s_brightness = SETTINGS_BRIGHTNESS_DEFAULT;
static uint8_t s_saved_brightness = SETTINGS_BRIGHTNESS_DEFAULT;
static bool s_dark_mode = true;
static bool s_config_mode;

static uint8_t clamp_brightness(uint8_t percent)
{
    if (percent < SETTINGS_BRIGHTNESS_MIN) return SETTINGS_BRIGHTNESS_MIN;
    if (percent > 100) return 100;
    return percent;
}

void settings_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed (%s), using defaults", esp_err_to_name(err));
        return;
    }

    nvs_handle_t handle;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return; /* namespace not created yet: first boot */
    }
    uint8_t value;
    if (nvs_get_u8(handle, SETTINGS_NVS_KEY_BRIGHTNESS, &value) == ESP_OK) {
        s_brightness = clamp_brightness(value);
        s_saved_brightness = s_brightness;
    }
    if (nvs_get_u8(handle, SETTINGS_NVS_KEY_DARK_MODE, &value) == ESP_OK) {
        s_dark_mode = (value != 0);
    }
    if (nvs_get_u8(handle, SETTINGS_NVS_KEY_CONFIG_MODE, &value) == ESP_OK) {
        s_config_mode = (value != 0);
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "brightness=%d%% dark_mode=%d", s_brightness, s_dark_mode);
}

uint8_t settings_store_get_brightness(void)
{
    return s_brightness;
}

void settings_store_set_brightness(uint8_t percent)
{
    s_brightness = clamp_brightness(percent);
    hal_backlight_set_brightness(s_brightness);
}

void settings_store_save_brightness(void)
{
    if (s_brightness == s_saved_brightness) {
        return;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, SETTINGS_NVS_KEY_BRIGHTNESS, s_brightness);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save brightness failed (%s)", esp_err_to_name(err));
        return;
    }
    s_saved_brightness = s_brightness;
}

bool settings_store_get_dark_mode(void)
{
    return s_dark_mode;
}

void settings_store_set_dark_mode(bool dark)
{
    if (dark == s_dark_mode) {
        return;
    }
    s_dark_mode = dark;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, SETTINGS_NVS_KEY_DARK_MODE, dark ? 1 : 0);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save dark_mode failed (%s)", esp_err_to_name(err));
    }
}

bool settings_store_get_wifi(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    nvs_handle_t handle;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t len = ssid_len;
    bool found = nvs_get_str(handle, SETTINGS_NVS_KEY_WIFI_SSID, ssid, &len) == ESP_OK && ssid[0] != 0;
    if (found) {
        len = password_len;
        if (nvs_get_str(handle, SETTINGS_NVS_KEY_WIFI_PASS, password, &len) != ESP_OK) {
            password[0] = 0; /* open network */
        }
    }
    nvs_close(handle);
    return found;
}

esp_err_t settings_store_set_wifi(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, SETTINGS_NVS_KEY_WIFI_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, SETTINGS_NVS_KEY_WIFI_PASS, password ? password : "");
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save wifi failed (%s)", esp_err_to_name(err));
    }
    return err;
}

bool settings_store_get_config_mode(void)
{
    return s_config_mode;
}

void settings_store_set_config_mode(bool on)
{
    s_config_mode = on;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, SETTINGS_NVS_KEY_CONFIG_MODE, on ? 1 : 0);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save config_mode failed (%s)", esp_err_to_name(err));
    }
}
