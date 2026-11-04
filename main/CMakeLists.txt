file(GLOB UI_SRCS "ui/*/*.c")

idf_component_register(SRCS
                    "main.c"
                    "app/app.c"
                    "lvgl_mgr/lvgl_mgr.c"
                    "protocols/https_request.c"
                    "services/wifi_service.c"
                    "services/wifi_provision.c"
                    "services/weather_service.c"
                    "services/weather_snapshot.c"
                    "services/providers/uproapi_provider.c"
                    "services/providers/qweather_provider.c"
                    ${UI_SRCS}
                    "drivers/display/display.c"
                    "drivers/display/panel/panel.c"
                    "drivers/display/touch/touch.c"
                    "drivers/ina226/ina226.c"
                    "drivers/ch338x/ch338x.c"
                    "services/usb_hub_service.c"
                    "services/power_monitor.c"
                    "services/settings_store.c"
                    "hal/hal_i2c.c"
                    "hal/hal_backlight.c"
                    INCLUDE_DIRS "." "ui"
                    LDFRAGMENTS "linker.lf"
                    PRIV_REQUIRES
                        driver
                        esp_driver_gpio
                        esp_driver_i2c
                        esp_driver_ledc
                        esp_lcd
                        esp_wifi
                        esp_netif
                        esp_event
                        nvs_flash
                        esp_http_client
                        esp_http_server
                        esp-tls
                        mbedtls
                        json
                    # Page .c files are only reached through the .lvgl_pages
                    # linker section, never by symbol - without this the
                    # archive linker would drop any page nothing references.
                    WHOLE_ARCHIVE)
