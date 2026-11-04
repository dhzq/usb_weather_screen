#include "weather_data.h"
#include "weather_source.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/*
 * This file pulls raw data from weather_source.c (currently mock, later a
 * real HTTP+JSON client behind the same interface) into the display-ready
 * structs the UI consumes. The source's Chinese condition text is passed
 * straight through for display; cond_from_text() only classifies it into
 * weather_cond_t for icon color/shape selection. Hour labels ("现在"/"HH:00")
 * are computed here from the source's hour_offset plus the current clock.
 */

/*bounds for the fixed-size buffers below - generous enough for the real
 *source's shapes (24h hourly forecast, ~1-2h of minutely precipitation)
 *without needing dynamic allocation*/
#define WEATHER_HOURLY_MAX    24
#define WEATHER_MINUTELY_MAX  24
#define WEATHER_DAILY_MAX     7

static weather_current_t current;
static weather_hour_forecast_t hourly[WEATHER_HOURLY_MAX];
static uint8_t hourly_count;
static weather_minute_precip_t minutely[WEATHER_MINUTELY_MAX];
static uint8_t minutely_count;
static weather_day_forecast_t daily[WEATHER_DAILY_MAX];
static uint8_t daily_count;
static bool is_raining;
static char rain_summary[64];
static bool translated = false;

static const char * const weekday_cn[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

/*longest/most specific substrings are checked first so e.g. "大暴雨" isn't
 *swallowed by the shorter "大雨" check, and compound terms like "雷阵雨" are
 *classified before their shorter components ("阵雨", "雷")*/
static weather_cond_t cond_from_text(const char * text)
{
    if(text == NULL) return WEATHER_COND_CLOUDY;

    if(strstr(text, "特大暴雨") != NULL) return WEATHER_COND_SEVERE_STORM_RAIN;
    if(strstr(text, "大暴雨") != NULL) return WEATHER_COND_HEAVY_STORM_RAIN;
    if(strstr(text, "暴雨") != NULL) return WEATHER_COND_STORM_RAIN;
    if(strstr(text, "阵雨夹雪") != NULL) return WEATHER_COND_SHOWER_SLEET;
    if(strstr(text, "雨夹雪") != NULL) return WEATHER_COND_SLEET;
    if(strstr(text, "雷阵雨") != NULL) return WEATHER_COND_THUNDERSTORM;
    if(strstr(text, "阵雨") != NULL) return WEATHER_COND_SHOWER;
    if(strstr(text, "雷") != NULL) return WEATHER_COND_THUNDERSTORM;
    if(strstr(text, "大雨") != NULL) return WEATHER_COND_HEAVY_RAIN;
    if(strstr(text, "中雨") != NULL) return WEATHER_COND_MODERATE_RAIN;
    if(strstr(text, "小雨") != NULL) return WEATHER_COND_LIGHT_RAIN;
    if(strstr(text, "冻雨") != NULL) return WEATHER_COND_FREEZING_RAIN;

    if(strstr(text, "暴雪") != NULL) return WEATHER_COND_STORM_SNOW;
    if(strstr(text, "大雪") != NULL) return WEATHER_COND_HEAVY_SNOW;
    if(strstr(text, "中雪") != NULL) return WEATHER_COND_MODERATE_SNOW;
    if(strstr(text, "小雪") != NULL) return WEATHER_COND_LIGHT_SNOW;
    if(strstr(text, "阵雪") != NULL) return WEATHER_COND_SNOW_SHOWER;
    if(strstr(text, "雪") != NULL) return WEATHER_COND_LIGHT_SNOW;

    if(strstr(text, "雨") != NULL) return WEATHER_COND_MODERATE_RAIN;

    if(strstr(text, "雾霾") != NULL) return WEATHER_COND_HAZE;
    if(strstr(text, "霾") != NULL) return WEATHER_COND_HAZE;
    if(strstr(text, "雾") != NULL) return WEATHER_COND_FOG;

    if(strstr(text, "沙尘暴") != NULL) return WEATHER_COND_SAND_STORM;
    if(strstr(text, "扬沙") != NULL) return WEATHER_COND_SAND;
    if(strstr(text, "浮尘") != NULL) return WEATHER_COND_DUST;
    if(strstr(text, "台风") != NULL) return WEATHER_COND_TYPHOON;

    if(strstr(text, "晴") != NULL) return WEATHER_COND_SUNNY;
    if(strstr(text, "阴") != NULL) return WEATHER_COND_OVERCAST;
    if(strstr(text, "多云") != NULL) return WEATHER_COND_CLOUDY;

    return WEATHER_COND_CLOUDY;
}

static void ensure_translated(void)
{
    if(translated) return;

    const weather_source_current_t * src_current = weather_source_fetch_current();
    current.cond = cond_from_text(src_current->weather_cn);
    current.cond_text = src_current->weather_cn;
    current.temp_now = src_current->temperature;
    current.temp_high = src_current->temp_max;
    current.temp_low = src_current->temp_min;
    current.humidity = src_current->humidity;

    time_t now = time(NULL);
    struct tm now_tm = *localtime(&now);

    uint8_t src_hourly_count = 0;
    const weather_source_hourly_entry_t * src_hourly = weather_source_fetch_hourly(&src_hourly_count);
    hourly_count = (src_hourly_count > WEATHER_HOURLY_MAX) ? WEATHER_HOURLY_MAX : src_hourly_count;
    for(uint8_t i = 0; i < hourly_count; i++) {
        if(src_hourly[i].hour_offset == 0) {
            strcpy(hourly[i].hour_label, "现在");
        }
        else {
            int hour = (now_tm.tm_hour + src_hourly[i].hour_offset) % 24;
            snprintf(hourly[i].hour_label, sizeof(hourly[i].hour_label), "%02d:00", hour);
        }
        hourly[i].cond = cond_from_text(src_hourly[i].weather_cn);
        hourly[i].temp = src_hourly[i].temperature;
    }

    uint8_t src_minutely_count = 0;
    const weather_source_minute_precip_t * src_minutely = weather_source_fetch_minutely(&src_minutely_count);
    minutely_count = (src_minutely_count > WEATHER_MINUTELY_MAX) ? WEATHER_MINUTELY_MAX : src_minutely_count;

    is_raining = false;
    int16_t rain_start = -1, rain_end = -1;
    for(uint8_t i = 0; i < minutely_count; i++) {
        minutely[i].minute_offset = src_minutely[i].minute_offset;
        minutely[i].precip_x10 = src_minutely[i].precip_x10;
        if(minutely[i].precip_x10 > 0) {
            is_raining = true;
            if(rain_start < 0) rain_start = minutely[i].minute_offset;
            /*interval length = gap to the next sample, falls back to 5 min for the last point*/
            int16_t interval = (i + 1 < minutely_count) ?
                (int16_t)(src_minutely[i + 1].minute_offset - src_minutely[i].minute_offset) : 5;
            rain_end = (int16_t)(minutely[i].minute_offset + interval);
        }
    }

    if(is_raining) {
        if(rain_start == 0) snprintf(rain_summary, sizeof(rain_summary), "现在有雨，预计%d分钟后减弱", rain_end);
        else snprintf(rain_summary, sizeof(rain_summary), "预计%d分钟后有雨，%d分钟后减弱", rain_start, rain_end);
    }
    else {
        rain_summary[0] = '\0';
    }

    uint8_t src_daily_count = 0;
    const weather_source_daily_entry_t * src_daily = weather_source_fetch_daily(&src_daily_count);
    daily_count = (src_daily_count > WEATHER_DAILY_MAX) ? WEATHER_DAILY_MAX : src_daily_count;
    for(uint8_t i = 0; i < daily_count; i++) {
        /*add whole days via time_t arithmetic rather than hand-rolling month
         *rollover - localtime() does the calendar math correctly*/
        time_t day_time = now + (time_t)src_daily[i].day_offset * 86400;
        struct tm day_tm = *localtime(&day_time);
        snprintf(daily[i].day_label, sizeof(daily[i].day_label), "%d月%d日 %s",
                 day_tm.tm_mon + 1, day_tm.tm_mday, weekday_cn[day_tm.tm_wday]);

        daily[i].cond = cond_from_text(src_daily[i].weather_cn);
        daily[i].temp_high = src_daily[i].temp_max;
        daily[i].temp_low = src_daily[i].temp_min;
    }

    translated = true;
}

void weather_data_invalidate(void)
{
    translated = false;
}

const weather_current_t * weather_data_get_current(void)
{
    ensure_translated();
    return &current;
}

const weather_hour_forecast_t * weather_data_get_hourly(uint8_t * count_out)
{
    ensure_translated();
    if(count_out) *count_out = hourly_count;
    return hourly;
}

bool weather_data_is_raining(void)
{
    ensure_translated();
    return is_raining;
}

const weather_minute_precip_t * weather_data_get_minutely(uint8_t * count_out)
{
    ensure_translated();
    if(count_out) *count_out = minutely_count;
    return minutely;
}

const char * weather_data_get_rain_summary(void)
{
    ensure_translated();
    return rain_summary;
}

const weather_day_forecast_t * weather_data_get_daily(uint8_t * count_out)
{
    ensure_translated();
    if(count_out) *count_out = daily_count;
    return daily;
}
