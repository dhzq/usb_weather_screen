#ifndef DRIVERS_DISPLAY_TOUCH_H
#define DRIVERS_DISPLAY_TOUCH_H

#include <esp_err.h>

/* Brings up the touch controller (FT5x06-family register protocol, via the
 * esp_lcd_touch_ft5x06 component) on the legacy-driver I2C_NUM_0 bus and
 * registers it with LVGL as a pointer input device on the display created by
 * display_init(). Requires hal_i2c_init() and display_init() to have run. */
esp_err_t touch_init(void);

#endif // DRIVERS_DISPLAY_TOUCH_H
