#ifndef HAL_BACKLIGHT_H
#define HAL_BACKLIGHT_H

#include <stdbool.h>
#include <stdint.h>

/* Sets up the LEDC PWM channel driving the backlight, plus the backlight
 * enable GPIO. Safe to call once at boot. */
void hal_backlight_init(void);

/* Drives the backlight enable pin (separate from the PWM duty cycle). */
void hal_backlight_set_enabled(bool enabled);

/* 0~100, clamped. Immediate (no fade). */
void hal_backlight_set_brightness(uint8_t brightness_percent);

#endif // HAL_BACKLIGHT_H
