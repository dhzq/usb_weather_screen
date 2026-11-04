#ifndef DRIVERS_CH338X_H
#define DRIVERS_CH338X_H

#include <stddef.h>
#include <stdint.h>
#include <driver/i2c.h>
#include <esp_err.h>

/* WCH CH338X USB hub controller, configured over I2C. Register access uses
 * SMBus-block-style framing: a write is [reg, count, data...], a read sends
 * [reg] and receives [count, data...]. Uses the legacy driver/i2c.h driver
 * (see hal_i2c.h for why). */

typedef struct ch338x_dev *ch338x_handle_t;

typedef enum {
    CH338X_SPEED_FULL = 0, /* high speed disabled */
    CH338X_SPEED_HIGH = 1,
} ch338x_speed_t;

/* Returns NULL on allocation failure. Does not touch the bus. */
ch338x_handle_t ch338x_init(i2c_port_t bus, uint8_t addr);

/* Datasheet limit: 32 data bytes per block. */
#define CH338X_MAX_BLOCK 32

/* Raw SMBus block access (1..CH338X_MAX_BLOCK data bytes), framed exactly as
 * datasheet figs. 5-7/5-8. On reads the chip reports a 32-byte block count, but only
 * the first `len` bytes are clocked out (NACK on the last one). */
esp_err_t ch338x_read_block(ch338x_handle_t dev, uint8_t reg, uint8_t *data, size_t len);
esp_err_t ch338x_write_block(ch338x_handle_t dev, uint8_t reg, const uint8_t *data, size_t len);

/* Block read/write self-test of `len` bytes (1..CH338X_MAX_BLOCK) starting at a
 * reserved register (datasheet: 0xD4-0xFA reserved, keep reg+len <= 0xFB):
 * saves the bytes, writes two distinct patterns reading each back, then
 * restores them. ESP_OK = write/read path works and every byte held its value;
 * ESP_ERR_INVALID_RESPONSE = bus transfers succeeded but read back differently
 * (details are logged). */
esp_err_t ch338x_selftest_block(ch338x_handle_t dev, uint8_t reg, size_t len);

/* USB vendor/product ID currently configured in the chip. */
esp_err_t ch338x_read_ids(ch338x_handle_t dev, uint16_t *vid, uint16_t *pid);

/* Downstream ports' maximum speed (FUN_CFG1 bit 5). Setting waits 300 ms for
 * the hub to re-enumerate. */
esp_err_t ch338x_get_speed(ch338x_handle_t dev, ch338x_speed_t *speed);
esp_err_t ch338x_set_speed(ch338x_handle_t dev, ch338x_speed_t speed);

/* One-time board configuration: VID/PID 1A86:809E, active-low port-power and
 * over-current polarity, and the fixed physical->logical port remap this
 * board needs (1->1, 2->7, 3->4, 4->6, 5->5, 6->2, 7->3). Register values are
 * the ones from demo/usb_hub, where this call was commented out - check
 * whether the chip keeps them across power cycles before calling it on every
 * boot. */
esp_err_t ch338x_apply_board_config(ch338x_handle_t dev);

#endif // DRIVERS_CH338X_H
