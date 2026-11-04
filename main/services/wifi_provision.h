#ifndef WIFI_PROVISION_H
#define WIFI_PROVISION_H

#include <esp_err.h>

/* Default address of the SoftAP's gateway, i.e. where the setup page lives. */
#define WIFI_PROVISION_AP_IP "192.168.4.1"

/* Starts the provisioning HTTP server (call once the SoftAP is up):
 *   GET  /      setup page
 *   GET  /scan  JSON list of nearby networks
 *   POST /save  ssid=..&password=.. (urlencoded) -> stores the credentials,
 *               clears the config-mode flag and reboots into station mode. */
esp_err_t wifi_provision_server_start(void);

#endif // WIFI_PROVISION_H
