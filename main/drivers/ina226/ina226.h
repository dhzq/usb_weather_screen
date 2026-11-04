#ifndef DRIVERS_INA226_H
#define DRIVERS_INA226_H

#include <stdint.h>
#include <driver/i2c.h>

typedef struct ina226_dev *ina226_handle_t;

typedef enum {
    INA226_AVG_1    = 0, /* 1 sample */
    INA226_AVG_4    = 1,
    INA226_AVG_16   = 2,
    INA226_AVG_64   = 3,
    INA226_AVG_128  = 4,
    INA226_AVG_256  = 5,
    INA226_AVG_512  = 6,
    INA226_AVG_1024 = 7,
} ina226_avg_mode_t;

typedef enum {
    INA226_CT_140US  = 0,
    INA226_CT_204US  = 1,
    INA226_CT_332US  = 2,
    INA226_CT_588US  = 3,
    INA226_CT_1100US = 4,
    INA226_CT_2116US = 5,
    INA226_CT_4156US = 6,
    INA226_CT_8244US = 7,
} ina226_conversion_time_t;

typedef enum {
    INA226_MODE_POWER_DOWN            = 0,
    INA226_MODE_SHUNT_TRIGGERED       = 1,
    INA226_MODE_BUS_TRIGGERED         = 2,
    INA226_MODE_SHUNT_BUS_TRIGGERED   = 3,
    INA226_MODE_SHUNT_CONTINUOUS      = 5,
    INA226_MODE_BUS_CONTINUOUS        = 6,
    INA226_MODE_SHUNT_AND_BUS_CONTINUOUS = 7,
} ina226_mode_t;

/* Adds the INA226 as an I2C device at `i2c_addr` on legacy i2c_driver port
 * `bus`. The bus-wide clock speed is set once on `bus` at hal_i2c_init()
 * time, not per device. */
ina226_handle_t ina226_init(i2c_port_t bus, uint8_t i2c_addr);

/* Writes the Calibration register for the given shunt resistor and max
 * expected current, per the standard TI INA226 formula
 * (current_lsb = max_current_a / 2^15, cal = 0.00512 / (current_lsb * shunt_ohm)).
 * Must be called before current/power reads are meaningful. */
void ina226_calibrate(ina226_handle_t dev, float shunt_ohm, float max_current_a);

/* Writes the Configuration register in one go. */
void ina226_configure(ina226_handle_t dev, ina226_avg_mode_t avg,
                       ina226_conversion_time_t bus_conv_time,
                       ina226_conversion_time_t shunt_conv_time,
                       ina226_mode_t mode);

/* These read the latest register value over I2C and convert it. On a
 * transient I2C bus error they log a warning, recover the bus (see
 * hal_i2c_bus_recover()) and return the previous successfully read value
 * instead of aborting the caller. */
float ina226_read_bus_voltage_v(ina226_handle_t dev);
float ina226_read_shunt_voltage_v(ina226_handle_t dev);
float ina226_read_current_a(ina226_handle_t dev);
float ina226_read_power_w(ina226_handle_t dev);

#endif // DRIVERS_INA226_H
