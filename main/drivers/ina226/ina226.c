#include "ina226.h"
#include "hal/hal_i2c.h"

#include <stdlib.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "ina226";

#define INA226_REG_CONFIG      0x00
#define INA226_REG_SHUNT_V     0x01
#define INA226_REG_BUS_V       0x02
#define INA226_REG_POWER       0x03
#define INA226_REG_CURRENT     0x04
#define INA226_REG_CALIBRATION 0x05

/* Bits 14-12 of the config register are reserved and must always be
 * written as 100b (TI datasheet default 0x4127). */
#define INA226_CONFIG_RESERVED 0x4000

#define INA226_BUS_VOLTAGE_LSB_V   1.25e-3f
#define INA226_SHUNT_VOLTAGE_LSB_V 2.5e-6f

#define I2C_XFER_TIMEOUT_MS 1000

struct ina226_dev {
    i2c_port_t bus;
    uint8_t addr;
    float current_lsb;
    float power_lsb;
    float last_bus_voltage_v;
    float last_shunt_voltage_v;
    float last_current_a;
    float last_power_w;
};

static void recover_bus(ina226_handle_t dev, esp_err_t err)
{
    ESP_LOGW(TAG, "recovering bus after error: %s", esp_err_to_name(err));
    hal_i2c_bus_recover(dev->bus);
}

static esp_err_t write_reg16(ina226_handle_t dev, uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = { reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    esp_err_t err = i2c_master_write_to_device(dev->bus, dev->addr, buf, sizeof(buf), pdMS_TO_TICKS(I2C_XFER_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "write reg 0x%02x failed: %s", reg, esp_err_to_name(err));
        recover_bus(dev, err);
    }
    return err;
}

static esp_err_t read_reg16(ina226_handle_t dev, uint8_t reg, uint16_t *out_value)
{
    uint8_t rx[2] = { 0 };
    esp_err_t err = i2c_master_write_read_device(dev->bus, dev->addr, &reg, 1, rx, sizeof(rx), pdMS_TO_TICKS(I2C_XFER_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read reg 0x%02x failed: %s", reg, esp_err_to_name(err));
        recover_bus(dev, err);
        return err;
    }
    *out_value = (uint16_t)((rx[0] << 8) | rx[1]);
    return ESP_OK;
}

ina226_handle_t ina226_init(i2c_port_t bus, uint8_t i2c_addr)
{
    ina226_handle_t dev = calloc(1, sizeof(struct ina226_dev));
    if (dev == NULL) {
        ESP_LOGE(TAG, "out of memory");
        return NULL;
    }

    dev->bus = bus;
    dev->addr = i2c_addr;

    ESP_LOGI(TAG, "device ready at 0x%02x", i2c_addr);
    return dev;
}

void ina226_calibrate(ina226_handle_t dev, float shunt_ohm, float max_current_a)
{
    dev->current_lsb = max_current_a / 32768.0f;
    dev->power_lsb = 25.0f * dev->current_lsb;

    uint16_t cal = (uint16_t)(0.00512f / (dev->current_lsb * shunt_ohm));
    ESP_ERROR_CHECK(write_reg16(dev, INA226_REG_CALIBRATION, cal));

    ESP_LOGI(TAG, "calibrated: shunt=%.4f ohm, max_current=%.3f A, cal=0x%04x",
             shunt_ohm, max_current_a, cal);
}

void ina226_configure(ina226_handle_t dev, ina226_avg_mode_t avg,
                       ina226_conversion_time_t bus_conv_time,
                       ina226_conversion_time_t shunt_conv_time,
                       ina226_mode_t mode)
{
    uint16_t config = INA226_CONFIG_RESERVED
                     | ((uint16_t)avg << 9)
                     | ((uint16_t)bus_conv_time << 6)
                     | ((uint16_t)shunt_conv_time << 3)
                     | (uint16_t)mode;
    ESP_ERROR_CHECK(write_reg16(dev, INA226_REG_CONFIG, config));
}

float ina226_read_bus_voltage_v(ina226_handle_t dev)
{
    uint16_t raw;
    if (read_reg16(dev, INA226_REG_BUS_V, &raw) != ESP_OK) {
        return dev->last_bus_voltage_v;
    }
    dev->last_bus_voltage_v = raw * INA226_BUS_VOLTAGE_LSB_V;
    return dev->last_bus_voltage_v;
}

float ina226_read_shunt_voltage_v(ina226_handle_t dev)
{
    uint16_t raw;
    if (read_reg16(dev, INA226_REG_SHUNT_V, &raw) != ESP_OK) {
        return dev->last_shunt_voltage_v;
    }
    dev->last_shunt_voltage_v = (int16_t)raw * INA226_SHUNT_VOLTAGE_LSB_V;
    return dev->last_shunt_voltage_v;
}

float ina226_read_current_a(ina226_handle_t dev)
{
    uint16_t raw;
    if (read_reg16(dev, INA226_REG_CURRENT, &raw) != ESP_OK) {
        return dev->last_current_a;
    }
    dev->last_current_a = (int16_t)raw * dev->current_lsb;
    return dev->last_current_a;
}

float ina226_read_power_w(ina226_handle_t dev)
{
    uint16_t raw;
    if (read_reg16(dev, INA226_REG_POWER, &raw) != ESP_OK) {
        return dev->last_power_w;
    }
    dev->last_power_w = raw * dev->power_lsb;
    return dev->last_power_w;
}
