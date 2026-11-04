#include "wifi_service.h"
#include "wifi_provision.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>

static const char *TAG = "wifi";

#define GOT_IP_BIT BIT0

static EventGroupHandle_t s_events;
static bool s_sntp_started;
static volatile bool s_time_synced;
static bool s_provisioning;
static char s_ap_ssid[33];

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (s_provisioning) {
        return; /* the STA half is only used for scanning */
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, GOT_IP_BIT);
        ESP_LOGW(TAG, "disconnected, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_events, GOT_IP_BIT);
    }
}

/* Shared by both modes: NVS, netif, event loop, WiFi driver, event handlers.
 * Only one mode is ever started per boot (switching modes reboots). */
static esp_err_t wifi_base_init(bool with_ap)
{
    if (s_events != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { /* already created elsewhere */
        return err;
    }
    esp_netif_create_default_wifi_sta();
    if (with_ap) {
        esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
    return ESP_OK;
}

esp_err_t wifi_service_start(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = wifi_base_init(false);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    if (password != NULL) {
        strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    }
    cfg.sta.threshold.authmode = cfg.sta.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    /* China Standard Time, no DST. */
    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "pool.ntp.org"));
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_cfg));
    s_sntp_started = true;

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "connecting to '%s'", ssid);
    return ESP_OK;
}

esp_err_t wifi_service_start_provisioning(void)
{
    s_provisioning = true; /* before the driver starts, so the handler never auto-connects */
    esp_err_t err = wifi_base_init(true);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "USB-Weather-%02X%02X", mac[4], mac[5]);

    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_ap_ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA)); /* STA half is needed for scanning */
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "provisioning AP '%s' up, open http://" WIFI_PROVISION_AP_IP, s_ap_ssid);
    return wifi_provision_server_start();
}

bool wifi_service_is_provisioning(void)
{
    return s_provisioning;
}

const char *wifi_service_get_ap_ssid(void)
{
    return s_ap_ssid;
}

bool wifi_service_wait_connected(uint32_t timeout_ms)
{
    if (s_events == NULL) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP_BIT, pdFALSE, pdTRUE,
                                           timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms));
    return (bits & GOT_IP_BIT) != 0;
}

bool wifi_service_wait_time_synced(uint32_t timeout_ms)
{
    if (!s_sntp_started) {
        return false;
    }
    /* esp_netif_sntp_sync_wait() only reports a *new* sync event, so after the
     * first success it would block until the next poll (~1 h). Remember it. */
    if (s_time_synced) {
        return true;
    }
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
        return false;
    }
    s_time_synced = true;
    return true;
}
