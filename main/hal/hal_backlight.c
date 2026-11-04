#include "hal_backlight.h"
#include "hal_config.h"

#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>

static const char *TAG = "hal_backlight";

#define BACKLIGHT_LEDC_TIMER    LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_FREQ_HZ  5000
#define BACKLIGHT_LEDC_DUTY_MAX 1023 /* 10-bit resolution */

void hal_backlight_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = BACKLIGHT_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    const ledc_channel_config_t channel_cfg = {
        .gpio_num = HAL_BACKLIGHT_PWM_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = HAL_BACKLIGHT_OUTPUT_INVERT,
        },
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));

    if (HAL_BACKLIGHT_EN_PIN != GPIO_NUM_NC) {
        const gpio_config_t en_io_cfg = {
            .pin_bit_mask = (1ULL << HAL_BACKLIGHT_EN_PIN),
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&en_io_cfg);
        gpio_set_level(HAL_BACKLIGHT_EN_PIN, 0); /* stays dark until the first frame is ready */
    }

    ESP_LOGI(TAG, "backlight ready (pwm_pin=%d, en_pin=%d)", HAL_BACKLIGHT_PWM_PIN, HAL_BACKLIGHT_EN_PIN);
}

void hal_backlight_set_enabled(bool enabled)
{
    if (HAL_BACKLIGHT_EN_PIN == GPIO_NUM_NC) {
        return;
    }
    gpio_set_level(HAL_BACKLIGHT_EN_PIN, enabled ? 1 : 0);
}

void hal_backlight_set_brightness(uint8_t brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    uint32_t duty = (BACKLIGHT_LEDC_DUTY_MAX * brightness_percent) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL);
}
