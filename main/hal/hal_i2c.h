#ifndef HAL_I2C_H
#define HAL_I2C_H

#include <driver/i2c.h>

/* Brings up both I2C buses on the legacy driver/i2c.h i2c_driver: I2C_NUM_0
 * (touch + TCA9555 I/O expander) and I2C_NUM_1 (INA226 power monitor,
 * CH338X USB hub controller). Every I2C peripheral in this project has to
 * stay on this one driver generation - ESP-IDF's check_i2c_driver_conflict()
 * link-time constructor aborts at boot if the legacy driver and the newer
 * i2c_master driver are both linked into the same firmware, regardless of
 * which port each is bound to. Safe to call once at boot; subsequent calls
 * are no-ops. */
void hal_i2c_init(void);

/* Port for the I/O bus (I2C_NUM_0: touch, TCA9555). Valid after hal_i2c_init(). */
i2c_port_t hal_i2c_io_bus(void);

/* Port for the power-monitor bus (I2C_NUM_1: INA226, CH338X). Valid after
 * hal_i2c_init(). */
i2c_port_t hal_i2c_power_bus(void);

/* Deletes and reinstalls `port`'s i2c_driver with the config it was brought
 * up with. The legacy driver has no equivalent of i2c_master_bus_reset();
 * this is the closest matching recovery for a wedged bus. */
void hal_i2c_bus_recover(i2c_port_t port);

#endif // HAL_I2C_H
