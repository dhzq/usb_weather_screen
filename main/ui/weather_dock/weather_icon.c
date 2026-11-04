#include "weather_icon.h"
#include "weather_icon_assets.h"
#include <time.h>

#define ICON_COUNT (WEATHER_COND_TYPHOON + 1)
#define BIG_SIZE   64
#define SMALL_SIZE 32

static const lv_img_dsc_t * const big_day_table[ICON_COUNT] = {
    [WEATHER_COND_SUNNY]             = &weather_icon_sunny_big_day,
    [WEATHER_COND_CLOUDY]            = &weather_icon_cloudy_big_day,
    [WEATHER_COND_OVERCAST]          = &weather_icon_overcast_big_day,
    [WEATHER_COND_SHOWER]            = &weather_icon_shower_big_day,
    [WEATHER_COND_THUNDERSTORM]      = &weather_icon_thunderstorm_big_day,
    [WEATHER_COND_SHOWER_SLEET]      = &weather_icon_shower_sleet_big_day,
    [WEATHER_COND_SLEET]             = &weather_icon_sleet_big_day,
    [WEATHER_COND_LIGHT_RAIN]        = &weather_icon_light_rain_big_day,
    [WEATHER_COND_MODERATE_RAIN]     = &weather_icon_moderate_rain_big_day,
    [WEATHER_COND_HEAVY_RAIN]        = &weather_icon_heavy_rain_big_day,
    [WEATHER_COND_STORM_RAIN]        = &weather_icon_storm_rain_big_day,
    [WEATHER_COND_HEAVY_STORM_RAIN]  = &weather_icon_heavy_storm_rain_big_day,
    [WEATHER_COND_SEVERE_STORM_RAIN] = &weather_icon_severe_storm_rain_big_day,
    [WEATHER_COND_SNOW_SHOWER]       = &weather_icon_snow_shower_big_day,
    [WEATHER_COND_LIGHT_SNOW]        = &weather_icon_light_snow_big_day,
    [WEATHER_COND_MODERATE_SNOW]     = &weather_icon_moderate_snow_big_day,
    [WEATHER_COND_HEAVY_SNOW]        = &weather_icon_heavy_snow_big_day,
    [WEATHER_COND_STORM_SNOW]        = &weather_icon_storm_snow_big_day,
    [WEATHER_COND_FOG]               = &weather_icon_fog_big_day,
    [WEATHER_COND_FREEZING_RAIN]     = &weather_icon_freezing_rain_big_day,
    [WEATHER_COND_SAND_STORM]        = &weather_icon_sand_storm_big_day,
    [WEATHER_COND_DUST]              = &weather_icon_dust_big_day,
    [WEATHER_COND_SAND]              = &weather_icon_sand_big_day,
    [WEATHER_COND_HAZE]              = &weather_icon_haze_big_day,
    [WEATHER_COND_TYPHOON]           = &weather_icon_typhoon_big_day,
};

static const lv_img_dsc_t * const big_night_table[ICON_COUNT] = {
    [WEATHER_COND_SUNNY]             = &weather_icon_sunny_big_night,
    [WEATHER_COND_CLOUDY]            = &weather_icon_cloudy_big_night,
    [WEATHER_COND_OVERCAST]          = &weather_icon_overcast_big_day,
    [WEATHER_COND_SHOWER]            = &weather_icon_shower_big_night,
    [WEATHER_COND_THUNDERSTORM]      = &weather_icon_thunderstorm_big_day,
    [WEATHER_COND_SHOWER_SLEET]      = &weather_icon_shower_sleet_big_day,
    [WEATHER_COND_SLEET]             = &weather_icon_sleet_big_day,
    [WEATHER_COND_LIGHT_RAIN]        = &weather_icon_light_rain_big_day,
    [WEATHER_COND_MODERATE_RAIN]     = &weather_icon_moderate_rain_big_day,
    [WEATHER_COND_HEAVY_RAIN]        = &weather_icon_heavy_rain_big_day,
    [WEATHER_COND_STORM_RAIN]        = &weather_icon_storm_rain_big_day,
    [WEATHER_COND_HEAVY_STORM_RAIN]  = &weather_icon_heavy_storm_rain_big_day,
    [WEATHER_COND_SEVERE_STORM_RAIN] = &weather_icon_severe_storm_rain_big_day,
    [WEATHER_COND_SNOW_SHOWER]       = &weather_icon_snow_shower_big_night,
    [WEATHER_COND_LIGHT_SNOW]        = &weather_icon_light_snow_big_day,
    [WEATHER_COND_MODERATE_SNOW]     = &weather_icon_moderate_snow_big_day,
    [WEATHER_COND_HEAVY_SNOW]        = &weather_icon_heavy_snow_big_day,
    [WEATHER_COND_STORM_SNOW]        = &weather_icon_storm_snow_big_day,
    [WEATHER_COND_FOG]               = &weather_icon_fog_big_night,
    [WEATHER_COND_FREEZING_RAIN]     = &weather_icon_freezing_rain_big_day,
    [WEATHER_COND_SAND_STORM]        = &weather_icon_sand_storm_big_day,
    [WEATHER_COND_DUST]              = &weather_icon_dust_big_day,
    [WEATHER_COND_SAND]              = &weather_icon_sand_big_day,
    [WEATHER_COND_HAZE]              = &weather_icon_haze_big_day,
    [WEATHER_COND_TYPHOON]           = &weather_icon_typhoon_big_day,
};

static const lv_img_dsc_t * const small_table[ICON_COUNT] = {
    [WEATHER_COND_SUNNY]             = &weather_icon_sunny_small,
    [WEATHER_COND_CLOUDY]            = &weather_icon_cloudy_small,
    [WEATHER_COND_OVERCAST]          = &weather_icon_overcast_small,
    [WEATHER_COND_SHOWER]            = &weather_icon_shower_small,
    [WEATHER_COND_THUNDERSTORM]      = &weather_icon_thunderstorm_small,
    [WEATHER_COND_SHOWER_SLEET]      = &weather_icon_shower_sleet_small,
    [WEATHER_COND_SLEET]             = &weather_icon_sleet_small,
    [WEATHER_COND_LIGHT_RAIN]        = &weather_icon_light_rain_small,
    [WEATHER_COND_MODERATE_RAIN]     = &weather_icon_moderate_rain_small,
    [WEATHER_COND_HEAVY_RAIN]        = &weather_icon_heavy_rain_small,
    [WEATHER_COND_STORM_RAIN]        = &weather_icon_storm_rain_small,
    [WEATHER_COND_HEAVY_STORM_RAIN]  = &weather_icon_heavy_storm_rain_small,
    [WEATHER_COND_SEVERE_STORM_RAIN] = &weather_icon_severe_storm_rain_small,
    [WEATHER_COND_SNOW_SHOWER]       = &weather_icon_snow_shower_small,
    [WEATHER_COND_LIGHT_SNOW]        = &weather_icon_light_snow_small,
    [WEATHER_COND_MODERATE_SNOW]     = &weather_icon_moderate_snow_small,
    [WEATHER_COND_HEAVY_SNOW]        = &weather_icon_heavy_snow_small,
    [WEATHER_COND_STORM_SNOW]        = &weather_icon_storm_snow_small,
    [WEATHER_COND_FOG]               = &weather_icon_fog_small,
    [WEATHER_COND_FREEZING_RAIN]     = &weather_icon_freezing_rain_small,
    [WEATHER_COND_SAND_STORM]        = &weather_icon_sand_storm_small,
    [WEATHER_COND_DUST]              = &weather_icon_dust_small,
    [WEATHER_COND_SAND]              = &weather_icon_sand_small,
    [WEATHER_COND_HAZE]              = &weather_icon_haze_small,
    [WEATHER_COND_TYPHOON]           = &weather_icon_typhoon_small,
};

static bool is_daytime_now(void)
{
    time_t now = time(NULL);
    struct tm now_tm = *localtime(&now);
    return now_tm.tm_hour >= 6 && now_tm.tm_hour < 18;
}

void weather_icon_create(lv_obj_t * parent, weather_cond_t cond, int32_t size)
{
    if(cond >= ICON_COUNT) cond = WEATHER_COND_CLOUDY;

    bool big = size >= 48;
    const lv_img_dsc_t * dsc;
    int32_t base_size;

    if(big) {
        dsc = is_daytime_now() ? big_day_table[cond] : big_night_table[cond];
        base_size = BIG_SIZE;
    }
    else {
        dsc = small_table[cond];
        base_size = SMALL_SIZE;
    }

    lv_obj_t * img = lv_image_create(parent);
    lv_image_set_src(img, dsc);
    if(size != base_size) {
        lv_image_set_scale(img, (uint32_t)(size * 256 / base_size));
    }
}
