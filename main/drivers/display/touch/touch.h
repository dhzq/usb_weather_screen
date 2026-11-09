#ifndef DRIVERS_DISPLAY_TOUCH_H
#define DRIVERS_DISPLAY_TOUCH_H

#include <esp_err.h>

/* Call before display_init(). For the GT911 (whose RST is shared with the LCD
 * reset) this holds INT low across the panel reset so the chip comes up on
 * 0x5D with its own config; touch_init() releases it. No-op for the FT6336U. */
void touch_prepare(void);

/* Brings up the touch controller on the legacy-driver I2C_NUM_0 bus and
 * (chosen by Kconfig TOUCH_CONTROLLER: FT6336U via esp_lcd_touch_ft5x06, or
 * GT911 via esp_lcd_touch_gt911)
 * registers it with LVGL as a pointer input device on the display created by
 * display_init(). Requires hal_i2c_init() and display_init() to have run. */
esp_err_t touch_init(void);

#endif // DRIVERS_DISPLAY_TOUCH_H
