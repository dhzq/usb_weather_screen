#include "touch.h"
#include "../display.h"
#include "hal/hal_config.h"
#include "hal/hal_i2c.h"

#include <esp_log.h>
#include <esp_check.h>
#include <esp_lcd_io_i2c.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if CONFIG_TOUCH_CONTROLLER_GT911
#include <esp_lcd_touch_gt911.h>
#else
#include <esp_lcd_touch_ft5x06.h>
#endif

static const char *TAG = "touch";

void touch_prepare(void)
{
#if CONFIG_TOUCH_CONTROLLER_GT911
    /* The GT911's RST is wired to the LCD reset (HAL_LCD_RESET_PIN), so the
     * panel bring-up in display_init() is also the GT911's hardware reset. The
     * chip latches its I2C address from INT while RST is released (low =
     * 0x5D) and only loads its own stored config when it comes up that way:
     * with INT floating it came up on 0x14 with a blank config and never
     * scanned. Hold INT low from here until touch_init() releases it. */
    const gpio_config_t int_out = {
        .pin_bit_mask = 1ULL << HAL_TOUCH_INT_PIN,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&int_out);
    gpio_set_level(HAL_TOUCH_INT_PIN, 0);
#endif
}

esp_err_t touch_init(void)
{
    lv_display_t *disp = display_get_lv_display();
    ESP_RETURN_ON_FALSE(disp != NULL, ESP_ERR_INVALID_STATE, TAG, "display not initialized");

    /* Both I2C buses stay on the legacy driver/i2c.h driver (see hal_i2c.h),
     * so the panel IO has to be the _v1 (port number) variant, not the
     * i2c_master_bus_handle_t one. v1 uses the bus's own clock config and
     * rejects a non-zero scl_speed_hz with ESP_ERR_INVALID_ARG. */
#if CONFIG_TOUCH_CONTROLLER_GT911
    /* Address 0x5D: INT was held low through the reset (see touch_prepare()).
     * The LCD reset finished long ago, so let the chip drive INT again. */
    const gpio_config_t int_in = {
        .pin_bit_mask = 1ULL << HAL_TOUCH_INT_PIN,
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&int_in);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
#else
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
#endif
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
#if CONFIG_TOUCH_CONTROLLER_GT911
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(io, &tp_cfg, &tp), TAG, "GT911 init failed");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(io, &tp_cfg, &tp), TAG, "FT5x06 init failed");
#endif

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
    };
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg) != NULL, ESP_FAIL, TAG,
                        "add touch to LVGL failed");

    ESP_LOGI(TAG, "touch ready (%s, addr 0x%02X)",
#if CONFIG_TOUCH_CONTROLLER_GT911
             "GT911",
#else
             "FT6336U",
#endif
             (unsigned)io_cfg.dev_addr);
    return ESP_OK;
}
