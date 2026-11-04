#ifndef LVGL_WEATHER_DATA_H
#define LVGL_WEATHER_DATA_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    WEATHER_COND_SUNNY,              /*晴*/
    WEATHER_COND_CLOUDY,              /*多云*/
    WEATHER_COND_OVERCAST,            /*阴*/
    WEATHER_COND_SHOWER,              /*阵雨*/
    WEATHER_COND_THUNDERSTORM,        /*雷阵雨*/
    WEATHER_COND_SHOWER_SLEET,        /*阵雨夹雪*/
    WEATHER_COND_SLEET,               /*雨夹雪*/
    WEATHER_COND_LIGHT_RAIN,          /*小雨*/
    WEATHER_COND_MODERATE_RAIN,       /*中雨*/
    WEATHER_COND_HEAVY_RAIN,          /*大雨*/
    WEATHER_COND_STORM_RAIN,          /*暴雨*/
    WEATHER_COND_HEAVY_STORM_RAIN,    /*大暴雨*/
    WEATHER_COND_SEVERE_STORM_RAIN,   /*特大暴雨*/
    WEATHER_COND_SNOW_SHOWER,         /*阵雪*/
    WEATHER_COND_LIGHT_SNOW,          /*小雪*/
    WEATHER_COND_MODERATE_SNOW,       /*中雪*/
    WEATHER_COND_HEAVY_SNOW,          /*大雪*/
    WEATHER_COND_STORM_SNOW,          /*暴雪*/
    WEATHER_COND_FOG,                 /*雾*/
    WEATHER_COND_FREEZING_RAIN,       /*冻雨*/
    WEATHER_COND_SAND_STORM,          /*沙尘暴*/
    WEATHER_COND_DUST,                /*浮尘*/
    WEATHER_COND_SAND,                /*扬沙*/
    WEATHER_COND_HAZE,                /*雾霾*/
    WEATHER_COND_TYPHOON,             /*台风*/
} weather_cond_t;

typedef struct {
    weather_cond_t cond;
    const char * cond_text; /*source's own Chinese text, e.g. "阴"/"雷阵雨", shown as-is*/
    int16_t temp_now;
    int16_t temp_high;
    int16_t temp_low;
    uint8_t humidity; /*0-100*/
} weather_current_t;

typedef struct {
    char hour_label[8]; /*"现在" for this hour, otherwise a computed "HH:00"*/
    weather_cond_t cond;
    int16_t temp;
} weather_hour_forecast_t;

typedef struct {
    uint16_t minute_offset; /*minutes from now*/
    uint16_t precip_x10;    /*precipitation amount, in 0.1 mm units*/
} weather_minute_precip_t;

typedef struct {
    char day_label[20]; /*computed "M月D日 周X", e.g. "10月3日 周五"*/
    weather_cond_t cond;
    int16_t temp_high;
    int16_t temp_low;
} weather_day_forecast_t;

/*drops the translated cache so the next get re-reads weather_source.c; call
 *after installing a new snapshot*/
void weather_data_invalidate(void);

const weather_current_t * weather_data_get_current(void);
const weather_hour_forecast_t * weather_data_get_hourly(uint8_t * count_out);
const weather_day_forecast_t * weather_data_get_daily(uint8_t * count_out);

/*true if the minutely forecast has any precipitation coming up*/
bool weather_data_is_raining(void);
const weather_minute_precip_t * weather_data_get_minutely(uint8_t * count_out);

/*short Chinese summary of the minutely rain window, e.g. "预计10分钟后有雨，
 *40分钟后减弱". Only valid (non-empty) when weather_data_is_raining().*/
const char * weather_data_get_rain_summary(void);

#endif //LVGL_WEATHER_DATA_H
