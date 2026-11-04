#include "display.h"
#include "panel/panel.h"
#include "hal/hal_config.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>

static const char *TAG = "display";

static lv_display_t *s_lv_display = NULL;

void display_init(void)
{
    display_panel_handles_t panel = display_panel_init();

    /* Paint white immediately so the panel isn't left showing garbage
     * while LVGL spins up. */
    uint16_t line_buf[HAL_DISPLAY_WIDTH];
    for (int i = 0; i < HAL_DISPLAY_WIDTH; i++) {
        line_buf[i] = 0xFFFF;
    }
    for (int y = 0; y < HAL_DISPLAY_HEIGHT; y++) {
        esp_lcd_panel_draw_bitmap(panel.panel, 0, y, HAL_DISPLAY_WIDTH, y + 1, line_buf);
    }

    ESP_LOGI(TAG, "initializing LVGL");
    lv_init();

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel.io,
        .panel_handle = panel.panel,
        .buffer_size = (uint32_t)(HAL_DISPLAY_WIDTH * 20),
        .double_buffer = true,
        .hres = HAL_DISPLAY_WIDTH,
        .vres = HAL_DISPLAY_HEIGHT,
        .rotation = {
            .swap_xy = HAL_DISPLAY_SWAP_XY,
            .mirror_x = HAL_DISPLAY_MIRROR_X,
            .mirror_y = HAL_DISPLAY_MIRROR_Y,
        },
        .flags = {
            .buff_dma = 1,
            .swap_bytes = 0,
            .full_refresh = 1,
            .direct_mode = 1,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };

    s_lv_display = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    if (s_lv_display == NULL) {
        ESP_LOGE(TAG, "failed to add RGB display to LVGL");
        return;
    }

    if (HAL_DISPLAY_OFFSET_X != 0 || HAL_DISPLAY_OFFSET_Y != 0) {
        lv_display_set_offset(s_lv_display, HAL_DISPLAY_OFFSET_X, HAL_DISPLAY_OFFSET_Y);
    }

    ESP_LOGI(TAG, "display ready");
}

lv_display_t *display_get_lv_display(void)
{
    return s_lv_display;
}
