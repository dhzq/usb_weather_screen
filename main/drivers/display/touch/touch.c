#include "touch.h"
#include "../display.h"
#include "hal/hal_config.h"
#include "hal/hal_i2c.h"

#include <esp_log.h>
#include <esp_check.h>
#include <esp_lcd_io_i2c.h>
#include <esp_lcd_touch_ft5x06.h>
#include <esp_lvgl_port.h>

static const char *TAG = "touch";

esp_err_t touch_init(void)
{
    lv_display_t *disp = display_get_lv_display();
    ESP_RETURN_ON_FALSE(disp != NULL, ESP_ERR_INVALID_STATE, TAG, "display not initialized");

    /* Both I2C buses stay on the legacy driver/i2c.h driver (see hal_i2c.h),
     * so the panel IO has to be the _v1 (port number) variant, not the
     * i2c_master_bus_handle_t one. v1 uses the bus's own clock config and
     * rejects a non-zero scl_speed_hz with ESP_ERR_INVALID_ARG. */
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_cfg.scl_speed_hz = 0; // use the bus's own clock config
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v1(hal_i2c_io_bus(), &io_cfg, &io), TAG,
                        "create touch panel IO failed");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = HAL_DISPLAY_WIDTH,
        .y_max = HAL_DISPLAY_HEIGHT,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = HAL_DISPLAY_SWAP_XY,
            .mirror_x = HAL_DISPLAY_MIRROR_X,
            .mirror_y = HAL_DISPLAY_MIRROR_Y,
        },
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(io, &tp_cfg, &tp), TAG, "FT5x06 init failed");

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
    };
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg) != NULL, ESP_FAIL, TAG,
                        "add touch to LVGL failed");

    ESP_LOGI(TAG, "touch ready (addr 0x%02X)", HAL_TOUCH_I2C_ADDRESS);
    return ESP_OK;
}
