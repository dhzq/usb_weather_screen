#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

/* Station-mode WiFi with automatic reconnect, plus SNTP time sync (the HTTPS
 * weather requests need a valid wall clock for certificate checks, and the UI
 * clock needs it too). Timezone is fixed to China Standard Time (UTC+8).
 *
 * Credentials are passed in by the caller (loaded from settings_store). When
 * there are none, or the user asked for it, wifi_service_start_provisioning()
 * is used instead: open SoftAP + web page (see wifi_provision.h). The two
 * modes are mutually exclusive per boot. */

/* Initializes NVS/netif/WiFi and starts connecting in the background. */
esp_err_t wifi_service_start(const char *ssid, const char *password);

/* Starts the configuration SoftAP (open, SSID from wifi_service_get_ap_ssid())
 * and the provisioning web server. Submitting the form saves the credentials
 * and reboots into station mode. */
esp_err_t wifi_service_start_provisioning(void);

bool wifi_service_is_provisioning(void);

/* SoftAP SSID; valid after wifi_service_start_provisioning(). */
const char *wifi_service_get_ap_ssid(void);

/* Blocks until an IP address is obtained. Returns true if connected. */
bool wifi_service_wait_connected(uint32_t timeout_ms);

/* Blocks until SNTP has set the system clock. Returns true if synced. */
bool wifi_service_wait_time_synced(uint32_t timeout_ms);

#endif // WIFI_SERVICE_H
