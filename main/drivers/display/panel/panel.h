#ifndef DRIVERS_DISPLAY_PANEL_H
#define DRIVERS_DISPLAY_PANEL_H

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
} display_panel_handles_t;

/* Brings up the ST7701 480x480 RGB panel (3-wire SPI control interface
 * multiplexed onto the RGB timing pins + 16-bit parallel RGB data bus).
 * Runs the vendor init command sequence and calls esp_lcd_panel_init().
 * The returned handles are ready for esp_lvgl_port. */
display_panel_handles_t display_panel_init(void);

#endif // DRIVERS_DISPLAY_PANEL_H
