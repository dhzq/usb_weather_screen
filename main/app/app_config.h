#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Which weather API to use: APP_WEATHER_PROVIDER_UPROAPI (uapis.cn, no key
 * needed) or APP_WEATHER_PROVIDER_QWEATHER (和风天气, needs host + key below). */
#define APP_WEATHER_PROVIDER_UPROAPI 1
#define APP_WEATHER_PROVIDER_QWEATHER 2
#define APP_WEATHER_PROVIDER APP_WEATHER_PROVIDER_UPROAPI

/* UPROAPI location: a city name (Chinese or English, e.g. "深圳" / "Tokyo") or
 * a numeric adcode (takes priority). Leave both empty to let the API locate
 * the device by its public IP. */
#define APP_WEATHER_CITY   ""
#define APP_WEATHER_ADCODE ""

/* Optional uapis.cn API key ("uapi-..."), sent as "Authorization: Bearer".
 * The weather endpoint answered anonymous requests when tested; leave empty
 * unless the service starts rejecting them. Don't commit a real key. */
#define APP_UAPIS_API_KEY ""

/* QWeather: the account's own API host (console -> Settings, e.g.
 * "abc1234xyz.def.qweatherapi.com", no "https://"), an API key, and the
 * location as "longitude,latitude". Don't commit a real key. */
#define APP_QWEATHER_HOST     "k56yw38249.re.qweatherapi.com"
#define APP_QWEATHER_API_KEY  "c9b621a80daa41128be887a56d29f8c0"
#define APP_QWEATHER_LOCATION "113.93,22.53"

/* How often to pull fresh weather (a failed fetch is retried after 30 s). */
#define APP_WEATHER_REFRESH_MS (10 * 60 * 1000)

#endif // APP_CONFIG_H
