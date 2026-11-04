#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>

#define SETTINGS_BRIGHTNESS_MIN 5   /* floor so the screen can't be dimmed to black and become unrecoverable */
#define SETTINGS_BRIGHTNESS_DEFAULT 100

/* Initializes NVS (idempotent) and loads the persisted settings into RAM.
 * Missing/unreadable values fall back to defaults. Call once at boot,
 * before anything reads the getters. */
void settings_store_init(void);

/* Brightness percent, SETTINGS_BRIGHTNESS_MIN~100. */
uint8_t settings_store_get_brightness(void);

/* Applies the brightness to the backlight immediately and caches it, but does
 * NOT write flash - safe to call on every slider tick. */
void settings_store_set_brightness(uint8_t percent);

/* Writes the cached brightness to NVS (no-op if unchanged since the last
 * save). Call when the user finishes adjusting. */
void settings_store_save_brightness(void);

/* Dark/light theme. Default is dark. */
bool settings_store_get_dark_mode(void);

/* Caches the theme choice and writes it to NVS right away (toggled rarely,
 * so no need to defer the write). Does not recolor the UI itself. */
void settings_store_set_dark_mode(bool dark);

/* Saved WiFi credentials. Returns false when no SSID has been saved yet (i.e.
 * the device was never provisioned). */
bool settings_store_get_wifi(char *ssid, size_t ssid_len, char *password, size_t password_len);

/* Persists WiFi credentials (writes NVS immediately). */
esp_err_t settings_store_set_wifi(const char *ssid, const char *password);

/* "Enter provisioning mode on next boot" flag. Persisted; the provisioning
 * page clears it after saving credentials. */
bool settings_store_get_config_mode(void);
void settings_store_set_config_mode(bool on);

#endif // SETTINGS_STORE_H
