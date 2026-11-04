#ifndef WEATHER_PROVIDER_H
#define WEATHER_PROVIDER_H

#include <stdbool.h>
#include "weather_snapshot.h"

/* Per-deployment settings handed to the provider. Empty string = unset. */
typedef struct {
    char city[48];     /* city name (Chinese or English); ignored if adcode is set */
    char adcode[16];   /* 6-digit administrative code; takes priority over city */
    char api_key[128]; /* optional "uapi-..." key, sent as a Bearer token */

    /* QWeather: all three are required */
    char qweather_host[64];      /* account API host, e.g. "abc.def.qweatherapi.com" */
    char qweather_key[128];      /* API key, sent as X-QW-Api-Key */
    char qweather_location[32];  /* "longitude,latitude", e.g. "113.93,22.53" */
} weather_provider_config_t;

/* A weather data source. Providers only know how to talk to one API and map
 * its JSON into a weather_snapshot_t; scheduling and delivery live in
 * weather_service.c. Add another API by adding a table + implementation and
 * selecting it in weather_service.c. */
typedef struct {
    const char *name;
    /* Blocking: performs the network request(s) and fills `out` (already
     * zeroed). Returns false on any failure. */
    bool (*fetch)(const weather_provider_config_t *cfg, weather_snapshot_t *out);
} weather_provider_t;

/* uapis.cn "misc/weather" - one request returns current conditions,
 * 7-day, 24-hour and minutely-precipitation data. */
extern const weather_provider_t weather_provider_uproapi;

/* QWeather v7: now + 24h + 7d + minutely/5m, four keyed requests per refresh. */
extern const weather_provider_t weather_provider_qweather;

#endif // WEATHER_PROVIDER_H
