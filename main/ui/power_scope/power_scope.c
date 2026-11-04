#include "power_scope.h"
#include "lvgl_mgr/lvgl_mgr.h"
#include "fonts/ui_fonts.h"
#include "theme/ui_theme.h"
#include "usb_switch/usb_switch.h"
#include "services/power_monitor.h"
#include <stdlib.h>

/*the trace/status colors stay fixed across light and dark mode - they're
 *signal identity (which curve is which, what a status means), not UI
 *chrome, so they read the same regardless of theme. Backgrounds and text
 *follow the shared ui_theme_style_* styles like every other page.*/
#define SCOPE_VOLT_COLOR      lv_color_hex(0xf57c00) /*channel 1 - orange: plain yellow nearly
                                                        *disappeared against the light theme's
                                                        *pale gray background*/
#define SCOPE_AMP_COLOR       lv_color_hex(0x00e5ff) /*channel 2 - cyan*/
#define SCOPE_STATUS_OFF_COLOR    lv_color_hex(0x5e6775)
#define SCOPE_STATUS_NORMAL_COLOR lv_color_hex(0x2ecc71)
#define SCOPE_STATUS_FAULT_COLOR  lv_color_hex(0xe74c3c)

/*the power monitor samples every 20ms while this page is open; the chart
 *shows the last SCOPE_POINT_COUNT of them (2.4s). The timer only drains what
 *has queued up and redraws once, so it can run slower than the sampling*/
#define SCOPE_POINT_COUNT 120
#define SCOPE_SAMPLE_PERIOD_MS 40
/*numeric readouts and axis labels refresh slower than the trace so the digits
 *stay readable*/
#define SCOPE_TEXT_PERIOD_MS 100
#define SCOPE_DRAIN_MAX 16

/*number of scale labels shown on each side of the chart - must match the
 *chart's own horizontal div line count (lv_chart_set_div_line_count's
 *hdiv argument below) so each label lines up with a grid line: lv_chart
 *spaces hdiv_cnt lines evenly from the top (index 0) to the bottom
 *(index hdiv_cnt-1) of the plot area*/
#define SCOPE_SCALE_TIER_COUNT 5

/*storage scale factors - lv_chart data points are integers, so volts/amps
 *are multiplied up to keep two decimal digits of precision*/
#define SCOPE_VOLT_SCALE 100   /*e.g. 502 -> 5.02V*/
#define SCOPE_AMP_SCALE  1000000 /*stored in uA, e.g. 1234567 -> 1.234567A*/

/*floor on the auto-ranged axis span, in the same scaled units as above -
 *keeps the range from collapsing to a sliver (or a zero-width range) when
 *the visible window is perfectly flat, e.g. right after the port turns off*/
#define SCOPE_VOLT_MIN_MARGIN 5   /*0.05V*/
#define SCOPE_AMP_MIN_MARGIN  100 /*100uA*/

static lv_obj_t * s_screen;
static lv_obj_t * s_chart;
static lv_chart_series_t * s_volt_series;
static lv_chart_series_t * s_amp_series;
static lv_obj_t * s_volt_value_label;
static lv_obj_t * s_volt_unit_label;
static lv_obj_t * s_amp_value_label;
static lv_obj_t * s_amp_unit_label;
static lv_obj_t * s_volt_tier_labels[SCOPE_SCALE_TIER_COUNT]; /*top -> bottom, i.e. max -> 0*/
static lv_obj_t * s_amp_tier_labels[SCOPE_SCALE_TIER_COUNT];
static lv_obj_t * s_status_dot;
static lv_obj_t * s_status_label;
static lv_obj_t * s_power_switch;
static lv_timer_t * s_timer;
static lv_timer_t * s_text_timer;

/*what the text timer renders: filled by the sampling timer*/
static bool s_port_on;
static bool s_have_last;
static float s_last_volts;
static float s_last_amps;
static int32_t s_volt_axis_max;
static int32_t s_amp_axis_max;

/*number and unit live in separate fixed-width labels inside one row: the
 *number is right-aligned in its own slot and the unit sits at a fixed spot
 *after it, so neither wrapping nor a changing unit ("uA"/"mA"/"A") moves
 *anything. Fixed widths also keep the block's own width (LV_SIZE_CONTENT)
 *stable as the digits jitter - an unstable width would shift readout_row's
 *SPACE_BETWEEN layout and visibly slide the status dot/switch every tick.
 *Widths fit the longest text for montserrat_28: "999.9" / "mA"*/
#define SCOPE_VALUE_SLOT_W 96
#define SCOPE_UNIT_SLOT_W  52

static void build_stat_block(lv_obj_t * parent, const char * name, lv_color_t color,
                             lv_obj_t ** out_value, lv_obj_t ** out_unit)
{
    lv_obj_t * block = lv_obj_create(parent);
    lv_obj_remove_style_all(block);
    lv_obj_set_size(block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(block, 2, 0);

    lv_obj_t * name_label = lv_label_create(block);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_color(name_label, color, 0);
    lv_obj_set_style_text_font(name_label, &lv_font_cjk_14, 0);

    lv_obj_t * row = lv_obj_create(block);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 4, 0);

    lv_obj_t * value_label = lv_label_create(row);
    lv_obj_set_style_text_color(value_label, color, 0);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(value_label, SCOPE_VALUE_SLOT_W);

    lv_obj_t * unit_label = lv_label_create(row);
    lv_obj_set_style_text_color(unit_label, color, 0);
    lv_obj_set_style_text_font(unit_label, &lv_font_montserrat_28, 0);
    lv_label_set_long_mode(unit_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(unit_label, SCOPE_UNIT_SLOT_W);

    *out_value = value_label;
    *out_unit = unit_label;
}

/*scans a chart series' raw backing array (the full point_count window, as
 *returned by lv_chart_get_series_y_array()) for its min/max, skipping
 *unset LV_CHART_POINT_NONE slots (e.g. before the window has filled up
 *once right after the page opens)*/
static void array_min_max(const int32_t * arr, uint16_t len, int32_t * out_min, int32_t * out_max)
{
    int32_t min = LV_CHART_POINT_NONE;
    int32_t max = LV_CHART_POINT_NONE;

    for(uint16_t i = 0; i < len; i++) {
        int32_t v = arr[i];
        if(v == LV_CHART_POINT_NONE) continue;
        if(min == LV_CHART_POINT_NONE || v < min) min = v;
        if(max == LV_CHART_POINT_NONE || v > max) max = v;
    }

    if(min == LV_CHART_POINT_NONE) {
        min = 0;
        max = 0;
    }

    *out_min = min;
    *out_max = max;
}

/*auto-ranges one axis's upper bound to the data currently visible in the
 *chart's sliding window: scans the series' own backing array for its max,
 *pads it by a headroom margin (an eighth of the max, floored at min_margin
 *so a near-zero window doesn't collapse the range to a sliver). The lower
 *bound is always 0 - volts/amps never go negative here, and a fixed zero
 *baseline is what makes the scale readable as an oscilloscope-style axis*/
static int32_t compute_window_max(lv_obj_t * chart, lv_chart_series_t * series, int32_t min_margin)
{
    int32_t * arr = lv_chart_get_series_y_array(chart, series);
    int32_t data_min, data_max;
    array_min_max(arr, SCOPE_POINT_COUNT, &data_min, &data_max);
    LV_UNUSED(data_min);

    int32_t margin = data_max / 8;
    if(margin < min_margin) margin = min_margin;

    return data_max + margin;
}

/*fills the tier labels for one axis, evenly spaced from max_value (top,
 *index 0) down to 0 (bottom, index SCOPE_SCALE_TIER_COUNT-1) - matching
 *where lv_chart draws its horizontal div lines*/
static void set_volt_tier_labels(int32_t max_value)
{
    for(int i = 0; i < SCOPE_SCALE_TIER_COUNT; i++) {
        int32_t value = max_value * (SCOPE_SCALE_TIER_COUNT - 1 - i) / (SCOPE_SCALE_TIER_COUNT - 1);
        lv_label_set_text_fmt(s_volt_tier_labels[i], "%d.%02d", (int)(value / SCOPE_VOLT_SCALE), (int)(value % SCOPE_VOLT_SCALE));
    }
}

/*the unit is picked once for the whole axis from its top value, so every
 *label on the axis uses the same one*/
static void set_amp_tier_labels(int32_t max_ua)
{
    for(int i = 0; i < SCOPE_SCALE_TIER_COUNT; i++) {
        int32_t ua = max_ua * (SCOPE_SCALE_TIER_COUNT - 1 - i) / (SCOPE_SCALE_TIER_COUNT - 1);
        if(max_ua < 1000) lv_label_set_text_fmt(s_amp_tier_labels[i], "%d uA", (int)ua);
        else if(max_ua < 1000000) lv_label_set_text_fmt(s_amp_tier_labels[i], "%d.%02d mA", (int)(ua / 1000), (int)(ua % 1000) / 10);
        else lv_label_set_text_fmt(s_amp_tier_labels[i], "%d.%02d A", (int)(ua / 1000000), (int)(ua % 1000000) / 10000);
    }
}

/*current split into number text and an automatic unit: uA below 1mA, mA
 *below 1A, otherwise A*/
static void format_current(char * num, size_t num_size, const char ** unit, float amps)
{
    int32_t ua = (int32_t)(amps * 1e6f + 0.5f);
    if(ua < 1000) {
        lv_snprintf(num, num_size, "%d", (int)ua);
        *unit = "uA";
    }
    else if(ua < 1000000) {
        lv_snprintf(num, num_size, "%d.%d", (int)(ua / 1000), (int)(ua % 1000) / 100);
        *unit = "mA";
    }
    else {
        lv_snprintf(num, num_size, "%d.%03d", (int)(ua / 1000000), (int)(ua % 1000000) / 1000);
        *unit = "A";
    }
}

/*refreshes the digits and axis labels, slower than the trace*/
static void scope_text_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    if(!s_port_on || !s_have_last) {
        lv_label_set_text(s_volt_value_label, "--");
        lv_label_set_text(s_volt_unit_label, "V");
        lv_label_set_text(s_amp_value_label, "--");
        lv_label_set_text(s_amp_unit_label, "A");
    }
    else {
        char buf[16];
        const char * unit;
        lv_label_set_text_fmt(s_volt_value_label, "%d.%02d", (int) s_last_volts, ((int)(s_last_volts * 100)) % 100);
        lv_label_set_text(s_volt_unit_label, "V");
        format_current(buf, sizeof(buf), &unit, s_last_amps);
        lv_label_set_text(s_amp_value_label, buf);
        lv_label_set_text(s_amp_unit_label, unit);
    }
    set_volt_tier_labels(s_volt_axis_max);
    set_amp_tier_labels(s_amp_axis_max);
}

/*user flips the quick switch on this page - forward it to usb_switch.c as a
 *power request (same path as tapping the port card), so the real hardware is
 *switched. The sampling timer snaps the switch back to the port's actual
 *state if the request fails or hasn't been confirmed yet*/
static void power_switch_cb(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target(e);
    usb_switch_set_power_monitor_port(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void scope_sample_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    usb_power_reading_t reading = usb_switch_get_power_reading();

    /*keep the switch in sync with the port's real state (e.g. toggled from
     *the USB page instead of from here) without re-firing power_switch_cb -
     *lv_obj_add_state()/clear_state() only change state, they don't send
     *LV_EVENT_VALUE_CHANGED the way user interaction does*/
    bool switch_checked = lv_obj_has_state(s_power_switch, LV_STATE_CHECKED);
    if(switch_checked != reading.on) {
        if(reading.on) lv_obj_add_state(s_power_switch, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_power_switch, LV_STATE_CHECKED);
    }

    if(!reading.on) {
        /*port is cut: no sampling data to show, the trace falls to zero*/
        power_sample_t discard[SCOPE_DRAIN_MAX];
        size_t n = power_monitor_drain(discard, SCOPE_DRAIN_MAX);
        for(size_t i = 0; i < (n ? n : 1); i++) {
            lv_chart_set_next_value(s_chart, s_volt_series, 0);
            lv_chart_set_next_value(s_chart, s_amp_series, 0);
        }
        s_port_on = false;
        lv_obj_set_style_bg_color(s_status_dot, SCOPE_STATUS_OFF_COLOR, 0);
        lv_obj_set_style_text_color(s_status_label, SCOPE_STATUS_OFF_COLOR, 0);
        lv_label_set_text(s_status_label, "关闭");
    }
    else {
        power_sample_t samples[SCOPE_DRAIN_MAX];
        size_t n = power_monitor_drain(samples, SCOPE_DRAIN_MAX);
        for(size_t i = 0; i < n; i++) {
            float amps = samples[i].amps < 0 ? 0 : samples[i].amps; /*shunt reads signed*/
            lv_chart_set_next_value(s_chart, s_volt_series, (int32_t)(samples[i].volts * SCOPE_VOLT_SCALE));
            lv_chart_set_next_value(s_chart, s_amp_series, (int32_t)(amps * SCOPE_AMP_SCALE));
        }
        s_port_on = true;
        if(n > 0) {
            s_last_volts = samples[n - 1].volts;
            s_last_amps = samples[n - 1].amps < 0 ? 0 : samples[n - 1].amps;
            s_have_last = true;
        }

        lv_color_t status_color = (reading.status == USB_PORT_STATUS_FAULT) ? SCOPE_STATUS_FAULT_COLOR : SCOPE_STATUS_NORMAL_COLOR;
        lv_obj_set_style_bg_color(s_status_dot, status_color, 0);
        lv_obj_set_style_text_color(s_status_label, status_color, 0);
        lv_label_set_text(s_status_label, reading.status == USB_PORT_STATUS_FAULT ? "异常" : "正常");
        if(n == 0) return; /*nothing new: keep the axes as they are*/
    }

    int32_t volt_max = compute_window_max(s_chart, s_volt_series, SCOPE_VOLT_MIN_MARGIN);
    int32_t amp_max = compute_window_max(s_chart, s_amp_series, SCOPE_AMP_MIN_MARGIN);

    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, volt_max);
    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_SECONDARY_Y, 0, amp_max);

    s_volt_axis_max = volt_max;
    s_amp_axis_max = amp_max;
}

void power_scope_ui_init(void)
{
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_theme_style_screen_bg, 0);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_set_style_pad_row(scr, 10, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    s_screen = scr;
    lvgl_mgr_set_swipe(scr, NULL, LVGL_MGR_BACK); /*swipe right -> back to the USB ports*/

    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "USB 功率示波器");
    lv_obj_add_style(title, &ui_theme_style_text_main, 0);
    lv_obj_set_style_text_font(title, &lv_font_cjk_20, 0);

    lv_obj_t * readout_row = lv_obj_create(scr);
    lv_obj_remove_style_all(readout_row);
    lv_obj_set_size(readout_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(readout_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(readout_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(readout_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(readout_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    build_stat_block(readout_row, "电压", SCOPE_VOLT_COLOR, &s_volt_value_label, &s_volt_unit_label);
    lv_label_set_text(s_volt_value_label, "--");
    lv_label_set_text(s_volt_unit_label, "V");
    build_stat_block(readout_row, "电流", SCOPE_AMP_COLOR, &s_amp_value_label, &s_amp_unit_label);
    lv_label_set_text(s_amp_value_label, "--");
    lv_label_set_text(s_amp_unit_label, "A");

    lv_obj_t * status_block = lv_obj_create(readout_row);
    lv_obj_remove_style_all(status_block);
    lv_obj_set_size(status_block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(status_block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(status_block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(status_block, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status_block, 6, 0);

    s_status_dot = lv_obj_create(status_block);
    lv_obj_remove_style_all(s_status_dot);
    lv_obj_set_size(s_status_dot, 10, 10);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_status_dot, SCOPE_STATUS_OFF_COLOR, 0);
    lv_obj_clear_flag(s_status_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_status_dot, LV_OBJ_FLAG_SCROLLABLE);

    s_status_label = lv_label_create(status_block);
    lv_label_set_text(s_status_label, "关闭");
    lv_obj_set_style_text_color(s_status_label, SCOPE_STATUS_OFF_COLOR, 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_cjk_12, 0);

    s_power_switch = lv_switch_create(status_block);
    if(usb_switch_get_power_reading().on) lv_obj_add_state(s_power_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_power_switch, power_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /*chart_row holds [volt scale column][chart][amp scale column] side by
     *side, all stretched to the same height - that height is what lv_chart
     *itself divides evenly into SCOPE_SCALE_TIER_COUNT horizontal bands, so
     *stacking SCOPE_SCALE_TIER_COUNT labels with SPACE_BETWEEN in each
     *column lines them up with the chart's own grid lines without any
     *pixel math*/
    lv_obj_t * chart_row = lv_obj_create(scr);
    lv_obj_remove_style_all(chart_row);
    lv_obj_set_size(chart_row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(chart_row, 1);
    lv_obj_clear_flag(chart_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(chart_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(chart_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chart_row, 6, 0);

    lv_obj_t * volt_scale_col = lv_obj_create(chart_row);
    lv_obj_remove_style_all(volt_scale_col);
    lv_obj_set_size(volt_scale_col, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_clear_flag(volt_scale_col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(volt_scale_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(volt_scale_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(volt_scale_col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    for(int i = 0; i < SCOPE_SCALE_TIER_COUNT; i++) {
        lv_obj_t * l = lv_label_create(volt_scale_col);
        lv_obj_set_style_text_color(l, SCOPE_VOLT_COLOR, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        s_volt_tier_labels[i] = l;
    }

    lv_obj_t * chart = lv_chart_create(chart_row);
    lv_obj_remove_style_all(chart);
    lv_obj_set_size(chart, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(chart, 1);
    lv_obj_add_style(chart, &ui_theme_style_card_bg, 0);     /*panel bg, follows theme*/
    lv_obj_add_style(chart, &ui_theme_style_divider_border, 0); /*border_color + grid line_color, follows theme*/
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_radius(chart, 8, 0);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN); /*grid div lines*/
    lv_obj_set_style_pad_all(chart, 4, LV_PART_MAIN);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR); /*hide point markers, keep clean trace lines*/
    /*LV_PART_ITEMS (the series lines themselves) has no style applied by
     *default once lv_obj_remove_style_all() strips the theme's own chart
     *styling, and an unstyled line part resolves to line_width 0 - i.e. the
     *line is computed but drawn with zero width, invisible. Must set this
     *explicitly or the curves never appear.*/
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart, true, LV_PART_ITEMS);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, SCOPE_POINT_COUNT);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    /*hdiv must stay equal to SCOPE_SCALE_TIER_COUNT - see that macro's
     *comment for why*/
    lv_chart_set_div_line_count(chart, SCOPE_SCALE_TIER_COUNT, 8);

    s_volt_series = lv_chart_add_series(chart, SCOPE_VOLT_COLOR, LV_CHART_AXIS_PRIMARY_Y);
    s_amp_series = lv_chart_add_series(chart, SCOPE_AMP_COLOR, LV_CHART_AXIS_SECONDARY_Y);
    s_chart = chart;

    lv_obj_t * amp_scale_col = lv_obj_create(chart_row);
    lv_obj_remove_style_all(amp_scale_col);
    lv_obj_set_size(amp_scale_col, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_clear_flag(amp_scale_col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(amp_scale_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(amp_scale_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(amp_scale_col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    for(int i = 0; i < SCOPE_SCALE_TIER_COUNT; i++) {
        lv_obj_t * l = lv_label_create(amp_scale_col);
        lv_obj_set_style_text_color(l, SCOPE_AMP_COLOR, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        s_amp_tier_labels[i] = l;
    }

    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 550);     /*0~5.50V, overwritten every tick once samples arrive*/
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 2000000); /*0~2A, overwritten every tick once samples arrive*/
    s_volt_axis_max = 550;
    s_amp_axis_max = 2000000;
    set_volt_tier_labels(s_volt_axis_max);
    set_amp_tier_labels(s_amp_axis_max);

    s_timer = lv_timer_create(scope_sample_timer_cb, SCOPE_SAMPLE_PERIOD_MS, NULL);
    lv_timer_pause(s_timer); /*both timers run only while the page is shown, see on_show*/
    s_text_timer = lv_timer_create(scope_text_timer_cb, SCOPE_TEXT_PERIOD_MS, NULL);
    lv_timer_pause(s_text_timer);
}

/*sampling only runs while the page is open: fast sampling starts empty on
 *entry and the trace starts from a blank chart*/
static void power_scope_on_show(void)
{
    lv_chart_set_all_value(s_chart, s_volt_series, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(s_chart, s_amp_series, LV_CHART_POINT_NONE);
    s_have_last = false;
    power_monitor_set_fast(true);
    lv_timer_resume(s_timer);
    lv_timer_resume(s_text_timer);
    lv_timer_ready(s_text_timer); /*render the labels now instead of after 200ms*/
}

static void power_scope_on_hide(void)
{
    lv_timer_pause(s_timer);
    lv_timer_pause(s_text_timer);
    power_monitor_set_fast(false);
}

lv_obj_t * power_scope_get_screen(void)
{
    return s_screen;
}

LVGL_REGISTER_PAGE(power_scope,
    .create = power_scope_ui_init,
    .get_screen = power_scope_get_screen,
    .on_show = power_scope_on_show,
    .on_hide = power_scope_on_hide);
