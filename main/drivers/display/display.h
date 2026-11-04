#ifndef DRIVERS_DISPLAY_H
#define DRIVERS_DISPLAY_H

#include <lvgl.h>

/* Brings up the ST7701 panel and attaches it to LVGL via esp_lvgl_port.
 * Draws nothing itself - pages are shown by lvgl_mgr. */
void display_init(void);

/* The LVGL display created by display_init(), or NULL if it hasn't run
 * (or failed). Other input devices (touch) attach to this. */
lv_display_t *display_get_lv_display(void);

#endif // DRIVERS_DISPLAY_H
