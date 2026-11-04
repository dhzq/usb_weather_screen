#ifndef WEATHER_SNAPSHOT_H
#define WEATHER_SNAPSHOT_H

#include <stdint.h>

/* One complete, self-contained weather reading as delivered by a provider,
 * in the source's own fields and language (Chinese condition text passed
 * through untouched). Display translation happens later, in the UI's
 * weather_data.c.
 *
 * The *_cn pointers point into the snapshot's own text storage below, so a
 * snapshot must never be copied by value - allocate with
 * weather_snapshot_alloc() and hand the pointer around. */

#define WEATHER_SNAPSHOT_HOURLY_MAX   24
#define WEATHER_SNAPSHOT_MINUTELY_MAX 24
#define WEATHER_SNAPSHOT_DAILY_MAX    7
#define WEATHER_SNAPSHOT_TEXT_LEN     32 /* UTF-8 bytes incl. NUL */

typedef struct {
    const char * weather_cn; /*source "weather"*/
    int16_t temperature;
    int16_t temp_max;
    int16_t temp_min;
    uint8_t humidity;
} weather_source_current_t;

typedef struct {
    uint8_t hour_offset;      /*hours from now, 0 = this hour*/
    const char * weather_cn;  /*source "weather"*/
    int16_t temperature;
} weather_source_hourly_entry_t;

typedef struct {
    uint16_t minute_offset; /*minutes from now*/
    uint16_t precip_x10;    /*precipitation amount, in 0.1 mm units*/
} weather_source_minute_precip_t;

typedef struct {
    uint8_t day_offset;      /*days from now, 0 = today*/
    const char * weather_cn; /*source "weather"*/
    int16_t temp_max;
    int16_t temp_min;
} weather_source_daily_entry_t;

typedef struct {
    weather_source_current_t current;
    weather_source_hourly_entry_t hourly[WEATHER_SNAPSHOT_HOURLY_MAX];
    weather_source_minute_precip_t minutely[WEATHER_SNAPSHOT_MINUTELY_MAX];
    weather_source_daily_entry_t daily[WEATHER_SNAPSHOT_DAILY_MAX];
    uint8_t hourly_count;
    uint8_t minutely_count;
    uint8_t daily_count;

    /*backing storage for the weather_cn pointers above*/
    char current_text[WEATHER_SNAPSHOT_TEXT_LEN];
    char hourly_text[WEATHER_SNAPSHOT_HOURLY_MAX][WEATHER_SNAPSHOT_TEXT_LEN];
    char daily_text[WEATHER_SNAPSHOT_DAILY_MAX][WEATHER_SNAPSHOT_TEXT_LEN];
} weather_snapshot_t;

/*zero-initialized snapshot, NULL on out-of-memory*/
weather_snapshot_t * weather_snapshot_alloc(void);
void weather_snapshot_free(weather_snapshot_t * snap);

/*copies text into dst (truncating on a UTF-8 character boundary) and returns dst*/
const char * weather_snapshot_set_text(char * dst, const char * text);

#endif //WEATHER_SNAPSHOT_H
