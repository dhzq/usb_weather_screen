#include "hal_i2c.h"
#include "hal_config.h"

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_rom_sys.h>
#include <esp_log.h>

static const char *TAG = "hal_i2c";

static i2c_config_t s_bus_cfg[I2C_NUM_MAX];
static bool s_bus_ready[I2C_NUM_MAX];

/* Frees a bus a slave is holding (SDA stuck low mid-byte): pulse SCL up to 9
 * times until the slave releases SDA, then issue a STOP. Must run while the
 * I2C peripheral is not driving the pins. */
static void bus_clear(const i2c_config_t *cfg)
{
    gpio_num_t sda = cfg->sda_io_num, scl = cfg->scl_io_num;
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    gpio_set_level(sda, 1);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(10);

    int pulses = 0;
    while (gpio_get_level(sda) == 0 && pulses < 9) {
        gpio_set_level(scl, 0);
        esp_rom_delay_us(10);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(10);
        pulses++;
    }
    if (pulses > 0) {
        ESP_LOGW(TAG, "SDA=%d was held low, sent %d SCL pulse(s), now %s", sda, pulses,
                 gpio_get_level(sda) ? "released" : "STILL LOW");
    }
    /* STOP: SDA low -> high while SCL high. */
    gpio_set_level(sda, 0);
    esp_rom_delay_us(10);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(10);
    gpio_set_level(sda, 1);
    esp_rom_delay_us(10);
}

/* Longest SCL-low time the peripheral tolerates before aborting with
 * ESP_ERR_TIMEOUT (2^22 APB cycles, ~52 ms). The CH338X's SMBus slave stretches
 * the clock, so the default is far too short on that bus. */
#define POWER_BUS_TIMEOUT_EXP 22

static void start_bus(i2c_port_t port)
{
    ESP_ERROR_CHECK(i2c_param_config(port, &s_bus_cfg[port]));
    ESP_ERROR_CHECK(i2c_driver_install(port, s_bus_cfg[port].mode, 0, 0, 0));
    if (port == hal_i2c_power_bus()) {
        ESP_ERROR_CHECK(i2c_set_timeout(port, POWER_BUS_TIMEOUT_EXP));
    }
}

static void install_bus(i2c_port_t port, const i2c_config_t *cfg, const char *label)
{
    s_bus_cfg[port] = *cfg;
    bus_clear(cfg);
    start_bus(port);
    s_bus_ready[port] = true;
    ESP_LOGI(TAG, "%s I2C bus ready (I2C_NUM_%d, legacy i2c_driver, SDA=%d SCL=%d)",
             label, port, cfg->sda_io_num, cfg->scl_io_num);
}

void hal_i2c_init(void)
{
    if (s_bus_ready[I2C_NUM_0]) {
        return;
    }

    const i2c_config_t io_bus_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = HAL_TOUCH_I2C_SDA_PIN,
        .scl_io_num = HAL_TOUCH_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100 * 1000,
    };
    install_bus(I2C_NUM_0, &io_bus_cfg, "io");

    const i2c_config_t power_bus_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = HAL_INA226_I2C_SDA_PIN,
        .scl_io_num = HAL_INA226_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100 * 1000, /* SMBus (CH338X) max; INA226 is fine at this too */
    };
    install_bus(I2C_NUM_1, &power_bus_cfg, "power");
}

i2c_port_t hal_i2c_io_bus(void)
{
    return I2C_NUM_0;
}

i2c_port_t hal_i2c_power_bus(void)
{
    return I2C_NUM_1;
}

void hal_i2c_bus_recover(i2c_port_t port)
{
    if (!s_bus_ready[port]) {
        return;
    }

    esp_err_t err = i2c_driver_delete(port);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C_NUM_%d: i2c_driver_delete failed: %s", port, esp_err_to_name(err));
    }
    bus_clear(&s_bus_cfg[port]);
    start_bus(port);
    ESP_LOGW(TAG, "I2C_NUM_%d: legacy i2c_driver reinstalled", port);
}
