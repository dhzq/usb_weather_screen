#include "weather_source.h"
#include <stdbool.h>

static weather_snapshot_t * s_snap;

/*shown until the first real snapshot arrives*/
static weather_snapshot_t s_placeholder;
static bool s_placeholder_ready;

static const weather_snapshot_t * current_snapshot(void)
{
    if(s_snap) return s_snap;
    if(!s_placeholder_ready) {
        s_placeholder.current.weather_cn = weather_snapshot_set_text(s_placeholder.current_text, "--");
        s_placeholder_ready = true;
    }
    return &s_placeholder;
}

weather_snapshot_t * weather_source_replace(weather_snapshot_t * snap)
{
    weather_snapshot_t * old = s_snap;
    s_snap = snap;
    return old;
}

const weather_source_current_t * weather_source_fetch_current(void)
{
    return &current_snapshot()->current;
}

const weather_source_hourly_entry_t * weather_source_fetch_hourly(uint8_t * count_out)
{
    const weather_snapshot_t * snap = current_snapshot();
    if(count_out) *count_out = snap->hourly_count;
    return snap->hourly;
}

const weather_source_minute_precip_t * weather_source_fetch_minutely(uint8_t * count_out)
{
    const weather_snapshot_t * snap = current_snapshot();
    if(count_out) *count_out = snap->minutely_count;
    return snap->minutely;
}

const weather_source_daily_entry_t * weather_source_fetch_daily(uint8_t * count_out)
{
    const weather_snapshot_t * snap = current_snapshot();
    if(count_out) *count_out = snap->daily_count;
    return snap->daily;
}
