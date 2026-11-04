#include "ch338x.h"
#include "ch338x_regs.h"
#include "hal/hal_i2c.h"

#include <stdlib.h>
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "ch338x";

#define XFER_TIMEOUT_MS 1000
#define MAX_BLOCK       CH338X_MAX_BLOCK

struct ch338x_dev {
    i2c_port_t bus;
    uint8_t addr;
};

static esp_err_t run_cmd(ch338x_handle_t dev, i2c_cmd_handle_t cmd)
{
    esp_err_t err = i2c_master_cmd_begin(dev->bus, cmd, pdMS_TO_TICKS(XFER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return err;
}

ch338x_handle_t ch338x_init(i2c_port_t bus, uint8_t addr)
{
    ch338x_handle_t dev = calloc(1, sizeof(*dev));
    if (dev != NULL) {
        dev->bus = bus;
        dev->addr = addr;
    }
    return dev;
}

esp_err_t ch338x_write_block(ch338x_handle_t dev, uint8_t reg, const uint8_t *data, size_t len)
{
    if (dev == NULL || data == NULL || len == 0 || len > MAX_BLOCK) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Fig. 5-8: S | addr+W | A | reg | A | count | A | data1 | A ... dataN | A | P */
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)(dev->addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, (uint8_t)len, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);
    esp_err_t err = run_cmd(dev, cmd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "write reg 0x%02x failed: %s", reg, esp_err_to_name(err));
        hal_i2c_bus_recover(dev->bus);
    }
    return err;
}

esp_err_t ch338x_read_block(ch338x_handle_t dev, uint8_t reg, uint8_t *data, size_t len)
{
    if (dev == NULL || data == NULL || len == 0 || len > MAX_BLOCK) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Fig. 5-7 framing: S | addr+W | A | reg | A | Sr | addr+R | A | count | A | data1 .. dataN.
     * The datasheet figure shows the master ACKing the last byte, but the chip
     * only behaves with the usual I2C ending: NACK on the final byte (this is
     * what demo/usb_hub's i2c_master_transmit_receive() does and it works on the
     * real hardware). ACKing the last byte or stopping after an ACK leaves the
     * chip streaming and holding SDA low. Only count + len bytes are read. */
    uint8_t rx[1 + MAX_BLOCK];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)(dev->addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd); /* repeated start */
    i2c_master_write_byte(cmd, (uint8_t)(dev->addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, rx, len + 1, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = run_cmd(dev, cmd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read reg 0x%02x failed: %s", reg, esp_err_to_name(err));
        hal_i2c_bus_recover(dev->bus);
        return err;
    }
    if (rx[0] < len) {
        ESP_LOGW(TAG, "read reg 0x%02x: byte count %u < requested %u", reg, rx[0], (unsigned)len);
        return ESP_ERR_INVALID_RESPONSE;
    }
    memcpy(data, rx + 1, len);
    return ESP_OK;
}

static esp_err_t update_bits(ch338x_handle_t dev, uint8_t reg, uint8_t clear, uint8_t set)
{
    uint8_t v;
    esp_err_t err = ch338x_read_block(dev, reg, &v, 1);
    if (err != ESP_OK) {
        return err;
    }
    v = (uint8_t)((v & ~clear) | set);
    return ch338x_write_block(dev, reg, &v, 1);
}

esp_err_t ch338x_selftest_block(ch338x_handle_t dev, uint8_t reg, size_t len)
{
    if (dev == NULL || len == 0 || len > MAX_BLOCK || (unsigned)reg + len > 0xFB) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t orig[MAX_BLOCK], tx[MAX_BLOCK], rx[MAX_BLOCK];
    esp_err_t err = ch338x_read_block(dev, reg, orig, len);
    if (err != ESP_OK) {
        return err;
    }
    esp_err_t result = ESP_OK;
    static const uint8_t seeds[] = { 0xA5, 0x3C };
    for (size_t p = 0; p < sizeof(seeds) && result == ESP_OK; p++) {
        for (size_t i = 0; i < len; i++) {
            tx[i] = (uint8_t)(seeds[p] + i * 7); /* distinct per byte, catches shifts */
        }
        if ((result = ch338x_write_block(dev, reg, tx, len)) != ESP_OK ||
            (result = ch338x_read_block(dev, reg, rx, len)) != ESP_OK) {
            break;
        }
        if (memcmp(tx, rx, len) != 0) {
            ESP_LOGW(TAG, "selftest reg 0x%02x len %u: mismatch", reg, (unsigned)len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, tx, len, ESP_LOG_WARN);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx, len, ESP_LOG_WARN);
            result = ESP_ERR_INVALID_RESPONSE;
        }
    }
    err = ch338x_write_block(dev, reg, orig, len); /* always restore */
    return result != ESP_OK ? result : err;
}

esp_err_t ch338x_read_ids(ch338x_handle_t dev, uint16_t *vid, uint16_t *pid)
{
    uint8_t b[2];
    esp_err_t err = ch338x_read_block(dev, CH338X_REG_VID, b, 2);
    if (err != ESP_OK) {
        return err;
    }
    *vid = (uint16_t)(b[0] | (b[1] << 8));
    err = ch338x_read_block(dev, CH338X_REG_PID, b, 2);
    if (err != ESP_OK) {
        return err;
    }
    *pid = (uint16_t)(b[0] | (b[1] << 8));
    return ESP_OK;
}

esp_err_t ch338x_get_speed(ch338x_handle_t dev, ch338x_speed_t *speed)
{
    uint8_t cfg1;
    esp_err_t err = ch338x_read_block(dev, CH338X_REG_FUN_CFG1, &cfg1, 1);
    if (err == ESP_OK) {
        *speed = (cfg1 & CH338X_HIGH_SPEED_DISABLE) ? CH338X_SPEED_FULL : CH338X_SPEED_HIGH;
    }
    return err;
}

esp_err_t ch338x_set_speed(ch338x_handle_t dev, ch338x_speed_t speed)
{
    esp_err_t err = speed == CH338X_SPEED_FULL
        ? update_bits(dev, CH338X_REG_FUN_CFG1, 0, CH338X_HIGH_SPEED_DISABLE)
        : update_bits(dev, CH338X_REG_FUN_CFG1, CH338X_HIGH_SPEED_DISABLE, 0);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(300)); /* let the hub re-enumerate */
    }
    return err;
}

esp_err_t ch338x_apply_board_config(ch338x_handle_t dev)
{
    esp_err_t err;
    const uint8_t vid[2] = { 0x86, 0x1A }; /* 0x1A86 little-endian */
    const uint8_t pid[2] = { 0x9E, 0x80 }; /* 0x809E */
    if ((err = ch338x_write_block(dev, CH338X_REG_VID, vid, 2)) != ESP_OK ||
        (err = ch338x_write_block(dev, CH338X_REG_PID, pid, 2)) != ESP_OK) {
        return err;
    }

    /* Port power and over-current both active low. */
    err = update_bits(dev, CH338X_REG_FUN_CFG5, CH338X_PORT_POWER_POLARITY | CH338X_OCP_POLARITY, 0);
    if (err != ESP_OK) {
        return err;
    }

    /* Physical -> logical port remap, values as validated in demo/usb_hub. */
    err = update_bits(dev, CH338X_REG_FUN_CFG3, 0, CH338X_PORT_REMAP_ENABLE);
    if (err != ESP_OK) {
        return err;
    }
    static const struct { uint8_t reg, val; } remap[] = {
        { CH338X_REG_REMAP12, 0x60 },
        { CH338X_REG_REMAP34, 0x37 },
        { CH338X_REG_REMAP56, 0x40 },
        { CH338X_REG_REMAP7,  0x02 },
    };
    for (size_t i = 0; i < sizeof(remap) / sizeof(remap[0]); i++) {
        err = ch338x_write_block(dev, remap[i].reg, &remap[i].val, 1);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
