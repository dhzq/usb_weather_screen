#include "weather_dock.h"
#include <lvgl.h>
#include "lvgl_mgr/lvgl_mgr.h"
#include "weather_data.h"
#include "weather_source.h"
#include "weather_icon.h"
#include "fonts/ui_fonts.h"
#include "theme/ui_theme.h"
#include "quote_data.h"
#include <time.h>
#include <string.h>

/*rain chart: how many bars apart (at the data's 5-minute cadence) a bar gets
 *a time tick (dot + label) under it - at 24 bars this lands on 0/6/12/18,
 *plus the last bar is always ticked too (see the tick condition below) so
 *the chart's right edge always shows a time, even when it doesn't fall on
 *an exact multiple of the step*/
#define RAIN_TIME_TICK_STEP 6

/*fixed pixel width of each rain bar (and its background track) - kept small
 *and constant regardless of bar count, so the track's width doesn't depend
 *on its column's own (flex-grown, content-sized) width*/
#define RAIN_BAR_WIDTH 10

/*fixed section heights - the content area scrolls as a whole now, so every
 *section just sizes to its own content instead of fighting for leftover
 *space via flex_grow. current section is a left/right split (clock | weather)
 *so it doesn't need to be as tall as the old all-centered-column version*/
#define CURRENT_SECTION_HEIGHT  190
#define HOURLY_SECTION_HEIGHT   132
#define RAIN_CHART_HEIGHT       60

static const char * const current_weekday_cn[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

static lv_obj_t * s_screen;
static lv_obj_t * s_content;      /*scrollable column holding the data sections*/
static lv_obj_t * s_hourly_row;    /*horizontally scrolling 24h strip, NULL if not built*/
static lv_timer_t * s_time_timer; /*updates the clock label inside the current section*/

static void build_current_section(lv_obj_t * parent);
static void build_hourly_forecast_section(lv_obj_t * parent);
static void build_daily_forecast_section(lv_obj_t * parent);
static void build_minute_rain_section(lv_obj_t * parent);
static void build_quote_section(lv_obj_t * parent);
static void build_data_sections(lv_obj_t * content);

void weather_dock_ui_init(void)
{
    /*screen itself doesn't scroll - it's a column with two children: the
     *scrollable content area (grows to fill the remaining space) and the
     *quote bar, which is laid out last but outside the scroll area so it
     *stays pinned to the bottom of the screen no matter how far content
     *is scrolled*/

    /*own screen (not lv_screen_active()) so the page manager can load/swap it*/
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    s_screen = scr;
    lv_obj_add_style(scr, &ui_theme_style_screen_bg, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    /*swipe left -> settings, swipe right -> USB ports*/
    lvgl_mgr_set_swipe(scr, "settings", "usb_switch");

    lv_obj_t * content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_pad_all(content, 12, 0);
    lv_obj_set_style_pad_row(content, 10, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    s_content = content;

    build_data_sections(content);

    // build_quote_section(scr);
}

static void build_data_sections(lv_obj_t * content)
{
    build_current_section(content);
    build_minute_rain_section(content);
    build_hourly_forecast_section(content);
    build_daily_forecast_section(content);
}

void weather_dock_update(weather_snapshot_t * snap)
{
    weather_snapshot_t * old = weather_source_replace(snap);
    weather_data_invalidate();

    /*if the page hasn't been built yet it will read the new data when it is*/
    if(s_content) {
        /*the clock timer points at a label that is about to be deleted*/
        if(s_time_timer) {
            lv_timer_delete(s_time_timer);
            s_time_timer = NULL;
        }
        /*rebuilding resets the scroll position to the top - remember it and put
         *it back so a data refresh doesn't yank the user's view around*/
        int32_t scroll_y = lv_obj_get_scroll_y(s_content);
        int32_t hourly_scroll_x = s_hourly_row ? lv_obj_get_scroll_x(s_hourly_row) : 0;
        s_hourly_row = NULL; /*deleted by the clean below, rebuilt by build_data_sections()*/
        lv_obj_clean(s_content);
        build_data_sections(s_content);
        /*scrolling is clamped to the content size, so lay it out first*/
        lv_obj_update_layout(s_content);
        lv_obj_scroll_to_y(s_content, scroll_y, LV_ANIM_OFF);
        if(s_hourly_row) lv_obj_scroll_to_x(s_hourly_row, hourly_scroll_x, LV_ANIM_OFF);
    }

    /*nothing derived from the old snapshot is referenced any more*/
    weather_snapshot_free(old);
}

lv_obj_t * weather_dock_get_screen(void)
{
    return s_screen;
}

static void time_label_refresh(lv_obj_t * label)
{
    time_t now = time(NULL);
    struct tm * t = localtime(&now);
    lv_label_set_text_fmt(label, "%02d:%02d", t->tm_hour, t->tm_min);
}

static void time_label_timer_cb(lv_timer_t * timer)
{
    time_label_refresh((lv_obj_t *) lv_timer_get_user_data(timer));
}

static void build_current_section(lv_obj_t * parent)
{
    const weather_current_t * cur = weather_data_get_current();

    /*left/right split: clock on the left, weather on the right, each taking
     *half the card - makes the two "equally important" pieces read as two
     *peer blocks side by side instead of a stack where the clock is just one
     *more centered line above the weather*/
    lv_obj_t * section = lv_obj_create(parent);
    lv_obj_remove_style_all(section);
    lv_obj_set_size(section, LV_PCT(100), CURRENT_SECTION_HEIGHT);
    lv_obj_add_style(section, &ui_theme_style_card_bg, 0);
    lv_obj_set_style_radius(section, 16, 0);
    lv_obj_set_style_pad_all(section, 12, 0);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(section, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * clock_col = lv_obj_create(section);
    lv_obj_remove_style_all(clock_col);
    lv_obj_set_size(clock_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(clock_col, 1);
    lv_obj_clear_flag(clock_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(clock_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(clock_col, 4, 0);

    lv_obj_t * time_label = lv_label_create(clock_col);
    lv_obj_add_style(time_label, &ui_theme_style_text_main, 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_40, 0);
    time_label_refresh(time_label);
    s_time_timer = lv_timer_create(time_label_timer_cb, 1000, time_label);

    time_t now = time(NULL);
    struct tm now_tm = *localtime(&now);
    lv_obj_t * date_label = lv_label_create(clock_col);
    lv_label_set_text_fmt(date_label, "%d月%d日 %s", now_tm.tm_mon + 1, now_tm.tm_mday,
                           current_weekday_cn[now_tm.tm_wday]);
    lv_obj_add_style(date_label, &ui_theme_style_text_muted, 0);
    lv_obj_set_style_text_font(date_label, &lv_font_cjk_16, 0);

    /*vertical divider between the two halves, same color as the 7-day list's
     *row dividers so the two "list"/"split" layouts feel visually related*/
    lv_obj_t * divider = lv_obj_create(section);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, 1, LV_PCT(70));
    lv_obj_add_style(divider, &ui_theme_style_divider_bg, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * weather_col = lv_obj_create(section);
    lv_obj_remove_style_all(weather_col);
    lv_obj_set_size(weather_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(weather_col, 1);
    lv_obj_clear_flag(weather_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(weather_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(weather_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(weather_col, 4, 0);

    lv_obj_t * icon_temp_row = lv_obj_create(weather_col);
    lv_obj_remove_style_all(icon_temp_row);
    lv_obj_set_size(icon_temp_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(icon_temp_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(icon_temp_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icon_temp_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(icon_temp_row, 10, 0);

    weather_icon_create(icon_temp_row, cur->cond, 64);

    lv_obj_t * temp_now = lv_label_create(icon_temp_row);
    lv_label_set_text_fmt(temp_now, "%d\xc2\xb0", cur->temp_now);
    lv_obj_add_style(temp_now, &ui_theme_style_text_main, 0);
    lv_obj_set_style_text_font(temp_now, &lv_font_montserrat_40, 0);

    lv_obj_t * cond_text = lv_label_create(weather_col);
    lv_label_set_text_fmt(cond_text, "%s %d\xc2\xb0/%d\xc2\xb0 湿度:%d%%", cur->cond_text, cur->temp_high, cur->temp_low, cur->humidity);
    lv_obj_add_style(cond_text, &ui_theme_style_text_main, 0);
    lv_obj_set_style_text_font(cond_text, &lv_font_cjk_16, 0);
}

static void build_hourly_forecast_section(lv_obj_t * parent)
{
    uint8_t count = 0;
    const weather_hour_forecast_t * hourly = weather_data_get_hourly(&count);

    lv_obj_t * section = lv_obj_create(parent);
    lv_obj_remove_style_all(section);
    lv_obj_set_size(section, LV_PCT(100), HOURLY_SECTION_HEIGHT);
    lv_obj_add_style(section, &ui_theme_style_card_bg, 0);
    lv_obj_set_style_radius(section, 16, 0);
    lv_obj_set_style_pad_all(section, 10, 0);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(section, 4, 0);

    // lv_obj_t * title = lv_label_create(section);
    // lv_label_set_text(title, "24小时预报");
    // lv_obj_set_style_text_color(title, TEXT_MUTED_COLOR, 0);
    // lv_obj_set_style_text_font(title, &lv_font_cjk_16, 0);

    /*plain horizontally-scrolling card strip - must stay LV_OBJ_FLAG_SCROLLABLE
     *(the default) or the horizontal drag gesture is silently ignored*/
    lv_obj_t * row = lv_obj_create(section);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(row, 1);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    s_hourly_row = row;

    for(uint8_t i = 0; i < count; i++) {
        const weather_hour_forecast_t * h = &hourly[i];

        lv_obj_t * card = lv_obj_create(row);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, 56, LV_PCT(100));
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t * hour_label = lv_label_create(card);
        lv_label_set_text(hour_label, h->hour_label);
        lv_obj_add_style(hour_label, &ui_theme_style_text_muted, 0);
        lv_obj_set_style_text_font(hour_label, &lv_font_cjk_14, 0);

        weather_icon_create(card, h->cond, 32);

        lv_obj_t * card_temp = lv_label_create(card);
        lv_label_set_text_fmt(card_temp, "%d\xc2\xb0", h->temp);
        lv_obj_add_style(card_temp, &ui_theme_style_text_main, 0);
        lv_obj_set_style_text_font(card_temp, &lv_font_montserrat_14, 0);
    }
}

static void build_minute_rain_section(lv_obj_t * parent)
{
    /*no rain coming up: skip the section entirely rather than show an empty card*/
    if(!weather_data_is_raining()) return;

    uint8_t count = 0;
    const weather_minute_precip_t * minutely = weather_data_get_minutely(&count);
    if(count == 0) return;

    uint16_t max_x10 = 0;
    for(uint8_t i = 0; i < count; i++) {
        if(minutely[i].precip_x10 > max_x10) max_x10 = minutely[i].precip_x10;
    }

    lv_obj_t * section = lv_obj_create(parent);
    lv_obj_remove_style_all(section);
    lv_obj_set_size(section, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_style(section, &ui_theme_style_card_bg, 0);
    lv_obj_set_style_radius(section, 16, 0);
    lv_obj_set_style_pad_all(section, 10, 0);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(section, 4, 0);

    lv_obj_t * title = lv_label_create(section);
    lv_label_set_text(title, weather_data_get_rain_summary());
    lv_obj_add_style(title, &ui_theme_style_text_main, 0);
    lv_obj_set_style_text_font(title, &lv_font_cjk_16, 0);

    /*bars row: hand-built instead of lv_chart, because a native lv_chart bar
     *series has no per-bar background "track" - without one, a light/no-rain
     *bar is just invisible, giving no reference for how full the scale is.
     *Each point gets its own flex column holding a fixed-width track (dim,
     *always visible, full RAIN_CHART_HEIGHT tall) and - only when there's
     *rain - a same-width "fill" bottom-anchored inside the track, whose
     *height is the actual value's fraction of the scale. The track is a
     *fixed pixel width (not a % of its column) so it doesn't depend on the
     *column's own flex-grown width, which varies with bar count.*/
    lv_obj_t * bars_row = lv_obj_create(section);
    lv_obj_remove_style_all(bars_row);
    lv_obj_set_size(bars_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(bars_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bars_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bars_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    uint16_t scale_max = max_x10 + max_x10 / 4 + 1; /*headroom so the tallest bar doesn't touch the very top*/

    /*dots are tiny (4px) so, unlike the time labels, they're safe to keep as
     *part of each bar's own flex column without unevenly widening it - but we
     *still need a handle to each one after the row's layout settles, to align
     *the (wide, must-stay-outside-the-grid) time labels under them below*/
    lv_obj_t * tick_dots[count];
    memset(tick_dots, 0, sizeof(tick_dots));

    for(uint8_t i = 0; i < count; i++) {
        lv_obj_t * col = lv_obj_create(bars_row);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 4, 0);

        lv_obj_t * track = lv_obj_create(col);
        lv_obj_remove_style_all(track);
        lv_obj_set_size(track, RAIN_BAR_WIDTH, RAIN_CHART_HEIGHT);
        lv_obj_set_style_bg_color(track, lv_color_hex(0x3a7bd5), 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_20, 0);
        lv_obj_set_style_radius(track, 3, 0);
        lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

        lv_coord_t fill_h = (lv_coord_t)((uint32_t) minutely[i].precip_x10 * RAIN_CHART_HEIGHT / scale_max);
        if(fill_h > 0) {
            lv_obj_t * fill = lv_obj_create(track);
            lv_obj_remove_style_all(fill);
            lv_obj_set_size(fill, LV_PCT(100), fill_h);
            lv_obj_set_style_bg_color(fill, lv_color_hex(0x3a7bd5), 0);
            lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(fill, 3, 0);
            lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);
        }

        if(i == 0 || i % RAIN_TIME_TICK_STEP == 0 || i == count - 1) {
            lv_obj_t * dot = lv_obj_create(col);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 4, 4);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_add_style(dot, &ui_theme_style_muted_bg, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            tick_dots[i] = dot;
        }
    }

    /*labels live in their own plain (non-flex) container, not directly in a
     *bar column - a label is wider than a column's fair share, and a column
     *stretching to fit it would break the even bar spacing. Because this
     *container has no flex_flow of its own, its children's positions (set
     *below via align_to) are absolute and left alone by section's layout*/
    lv_obj_t * label_area = lv_obj_create(section);
    lv_obj_remove_style_all(label_area);
    lv_obj_set_size(label_area, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(label_area, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_update_layout(bars_row); /*resolve every column/dot's real position before aligning labels to them*/

    time_t now = time(NULL);
    struct tm now_tm = *localtime(&now);

    for(uint8_t i = 0; i < count; i++) {
        if(tick_dots[i] == NULL) continue;

        lv_obj_t * tick_label = lv_label_create(label_area);
        if(i == 0) {
            lv_label_set_text(tick_label, "现在");
        }
        else {
            int total_min = now_tm.tm_hour * 60 + now_tm.tm_min + minutely[i].minute_offset;
            total_min %= (24 * 60);
            lv_label_set_text_fmt(tick_label, "%02d:%02d", total_min / 60, total_min % 60);
        }
        lv_obj_add_style(tick_label, &ui_theme_style_text_muted, 0);
        lv_obj_set_style_text_font(tick_label, &lv_font_cjk_14, 0);
        lv_label_set_long_mode(tick_label, LV_LABEL_LONG_CLIP); /*single line, never wraps*/
        lv_obj_align_to(tick_label, tick_dots[i], LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

        /*align_to only writes the new position into the style system and marks
         *layout as dirty - it does NOT synchronously refresh tick_label->coords.
         *Without resolving layout here, every lv_obj_get_coords() below would
         *read each label's pre-align (stale) position instead of where it was
         *just aligned to, collapsing every label's edge-correction onto roughly
         *the same wrong spot.*/
        lv_obj_update_layout(tick_label);

        /*the first/last tick's dot sits right at the chart's own left/right
         *edge, so centering the label on it (like every middle tick) would
         *run it half off that edge - nudge it back in by however far it's
         *currently poking past bars_row's bounds, plus a small margin so it
         *lands clear of the edge instead of sitting flush against it*/
        const lv_coord_t edge_margin = 2;
        lv_area_t label_coords, row_coords;
        lv_obj_get_coords(tick_label, &label_coords);
        lv_obj_get_coords(bars_row, &row_coords);
        lv_coord_t shift = 0;
        if(label_coords.x1 < row_coords.x1 + edge_margin) shift = (row_coords.x1 + edge_margin) - label_coords.x1;
        else if(label_coords.x2 > row_coords.x2 - edge_margin) shift = (row_coords.x2 - edge_margin) - label_coords.x2;
        if(shift != 0) lv_obj_set_pos(tick_label, lv_obj_get_x(tick_label) + shift, lv_obj_get_y(tick_label));
    }
}

static void build_quote_section(lv_obj_t * parent)
{
    /*fixed-height footer bar, laid out as a sibling of the scrollable
     *content area (not inside it) - see weather_dock_ui_init()*/
    lv_obj_t * section = lv_obj_create(parent);
    lv_obj_remove_style_all(section);
    lv_obj_set_size(section, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(section, 12, 0);
    lv_obj_set_style_pad_ver(section, 10, 0);
    lv_obj_set_style_border_side(section, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(section, 1, 0);
    lv_obj_add_style(section, &ui_theme_style_divider_border, 0);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * label = lv_label_create(section);
    lv_label_set_text(label, quote_data_get_today());
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label, &ui_theme_style_text_muted, 0);
    lv_obj_set_style_text_font(label, &lv_font_cjk_16, 0);
}

static void build_daily_forecast_section(lv_obj_t * parent)
{
    uint8_t count = 0;
    const weather_day_forecast_t * daily = weather_data_get_daily(&count);

    /*height is content-driven, not fixed - the whole screen scrolls, so this
     *section just grows to fit all 7 rows*/
    lv_obj_t * section = lv_obj_create(parent);
    lv_obj_remove_style_all(section);
    lv_obj_set_size(section, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_style(section, &ui_theme_style_card_bg, 0);
    lv_obj_set_style_radius(section, 16, 0);
    lv_obj_set_style_pad_all(section, 10, 0);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(section, 4, 0);

    // lv_obj_t * title = lv_label_create(section);
    // lv_label_set_text(title, "7天预报");
    // lv_obj_set_style_text_color(title, TEXT_MUTED_COLOR, 0);
    // lv_obj_set_style_text_font(title, &lv_font_cjk_16, 0);

    /*vertical list, one row per day - embedded directly in the section
     *since the whole screen is what scrolls*/
    for(uint8_t i = 0; i < count; i++) {
        const weather_day_forecast_t * d = &daily[i];

        lv_obj_t * row = lv_obj_create(section);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_top(row, 6, 0);
        lv_obj_set_style_pad_bottom(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        if(i < count - 1) {
            lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_width(row, 1, 0);
            lv_obj_add_style(row, &ui_theme_style_divider_border, 0);
        }

        lv_obj_t * day_label = lv_label_create(row);
        lv_label_set_text(day_label, d->day_label);
        lv_obj_set_width(day_label, 110);
        lv_obj_add_style(day_label, &ui_theme_style_text_muted, 0);
        lv_obj_set_style_text_font(day_label, &lv_font_cjk_16, 0);

        weather_icon_create(row, d->cond, 28);

        lv_obj_t * row_temp = lv_label_create(row);
        lv_label_set_text_fmt(row_temp, "%d/%d\xc2\xb0", d->temp_high, d->temp_low);
        lv_obj_add_style(row_temp, &ui_theme_style_text_main, 0);
        lv_obj_set_style_text_font(row_temp, &lv_font_montserrat_16, 0);
    }
}

LVGL_REGISTER_PAGE(weather_dock,
    .create = weather_dock_ui_init,
    .get_screen = weather_dock_get_screen);
