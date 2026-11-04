#ifndef LVGL_WEATHER_SOURCE_H
#define LVGL_WEATHER_SOURCE_H

#include <stdint.h>
#include "services/weather_snapshot.h"

/*
 * Data acquisition layer, sitting above weather_data.c.
 *
 * Everything here is intentionally "dumb": it just hands back whatever the
 * weather source provides, in the source's own fields and language (Chinese,
 * matching the real API's JSON). No translation/interpretation happens here
 * - that stays weather_data.c's job.
 *
 * The data comes from the latest snapshot delivered by the weather service
 * (services/weather_service.h); until the first one arrives, placeholder
 * values ("--", no forecast entries) are returned. Like all UI code, these
 * functions must only be called on the LVGL task.
 */

const weather_source_current_t * weather_source_fetch_current(void);
const weather_source_hourly_entry_t * weather_source_fetch_hourly(uint8_t * count_out);
const weather_source_minute_precip_t * weather_source_fetch_minutely(uint8_t * count_out);
const weather_source_daily_entry_t * weather_source_fetch_daily(uint8_t * count_out);

/*installs `snap` as the data source (taking ownership) and returns the
 *previously installed one, or NULL. The caller frees the old snapshot after
 *everything derived from it has been refreshed (see weather_dock_update())*/
weather_snapshot_t * weather_source_replace(weather_snapshot_t * snap);

#endif //LVGL_WEATHER_SOURCE_H
