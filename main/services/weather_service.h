#ifndef WEATHER_SERVICE_H
#define WEATHER_SERVICE_H

#include <stdint.h>
#include <esp_err.h>
#include "weather_provider.h"

/* Called on the weather task each time a fresh snapshot is ready. Ownership
 * of `snap` passes to the listener, which must eventually
 * weather_snapshot_free() it. */
typedef void (*weather_listener_t)(weather_snapshot_t *snap, void *ctx);

/* `provider` must stay valid (use one of the weather_provider_* tables);
 * `cfg` is copied. With no city/adcode set, the API locates the device by its
 * public IP.
 *
 * Starts the background task: waits for WiFi + time sync, fetches from the
 * configured provider every `interval_ms` (retrying sooner after a failure)
 * and delivers each result to `listener`. */
esp_err_t weather_service_start(weather_listener_t listener, void *ctx, uint32_t interval_ms,
                                const weather_provider_t *provider, const weather_provider_config_t *cfg);

#endif // WEATHER_SERVICE_H
