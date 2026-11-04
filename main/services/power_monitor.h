#ifndef SERVICES_POWER_MONITOR_H
#define SERVICES_POWER_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <esp_err.h>

/* Samples the INA226 on the power bus (it measures USB4's VBUS and current).
 * Two rates: the default (slow, averaged) one feeds the USB page's wattage
 * label; fast mode (20 ms, unaveraged) is for the oscilloscope page and only
 * runs while that page is open. */

typedef struct {
    float volts;
    float amps;
    float watts;
} power_sample_t;

/* Initializes and calibrates the INA226 and starts the sampling task
 * (hal_i2c_init() first). */
esp_err_t power_monitor_start(void);

/* Fast mode on: samples every 20 ms into a queue read with
 * power_monitor_drain(), starting empty. Off: back to the default rate. */
void power_monitor_set_fast(bool fast);

/* Most recent sample at either rate; false until the first one arrived. */
bool power_monitor_get_latest(power_sample_t *out);

/* Moves up to `max` queued fast-mode samples (oldest first) into `out` and
 * returns how many. If the reader falls behind, the oldest samples are dropped. */
size_t power_monitor_drain(power_sample_t *out, size_t max);

#endif // SERVICES_POWER_MONITOR_H
