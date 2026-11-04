#include "usb_switch.h"
#include "lvgl_mgr/lvgl_mgr.h"
#include "theme/ui_theme.h"
#include "fonts/ui_fonts.h"
#include "services/power_monitor.h"
#include <stdlib.h>

#define USB_PORT_COUNT 4

/*which port has power-metering hardware behind it - just the one, like a
 *real multi-port charger where only one port is the "smart" one*/
#define USB_POWER_MONITOR_INDEX 3

/*how often the power-monitoring port's wattage reading refreshes while on*/
#define USB_POWER_READING_PERIOD_MS 1500

/*fixed accent colors - deliberately not theme-driven, same idea as the rain
 *chart's accent color, so they read the same in both themes*/
#define USB_ON_BG_COLOR     lv_color_hex(0x2ecc71)   /*on state*/
#define USB_ON_TEXT_COLOR   lv_color_white()
#define USB_POWER_BADGE_COLOR lv_color_hex(0x3a7bd5) /*power-monitor badge, same blue as the rain chart accent*/
#define USB_STATUS_NORMAL_COLOR lv_color_hex(0x2ecc71)
#define USB_STATUS_FAULT_COLOR  lv_color_hex(0xe74c3c)

typedef struct {
    uint8_t index;
    lv_obj_t * card;
    lv_obj_t * icon_wrap;
    lv_obj_t * icon_label;
    lv_obj_t * name_label;
    lv_obj_t * status_dot;
    lv_obj_t * status_label;
    lv_obj_t * watt_label;   /*NULL on every port except USB_POWER_MONITOR_INDEX*/
    bool on;
    usb_port_status_t status;
} usb_port_ctx_t;

static lv_obj_t * s_screen;
static usb_port_ctx_t s_ports[USB_PORT_COUNT];
static const char * const s_port_names[USB_PORT_COUNT] = {"USB1", "USB2", "USB3", "USB4"};

/*global, not per-port - simulated, so just a label to reflect it on screen*/
static usb_speed_mode_t s_speed_mode = USB_SPEED_MODE_HIGH;
static lv_obj_t * s_speed_label;

/*the power-monitored port's lightning badge object, read by
 *usb_switch_get_power_badge() for pages that visualize this port's data*/
static lv_obj_t * s_power_badge;

static usb_switch_port_request_cb_t s_port_request_cb;

void usb_switch_set_port_request_cb(usb_switch_port_request_cb_t cb)
{
    s_port_request_cb = cb;
}

static void apply_port_visual(usb_port_ctx_t * ctx)
{
    /*state can be pushed (e.g. from the hub hardware) before the page was ever
     *shown and built; it is kept in ctx and applied by build_port_card()*/
    if(ctx->card == NULL) return;

    /*port status is independent of the on/off power state, so it's always
     *refreshed regardless of which branch below runs*/
    if(ctx->status == USB_PORT_STATUS_FAULT) {
        lv_obj_set_style_bg_color(ctx->status_dot, USB_STATUS_FAULT_COLOR, 0);
        lv_label_set_text(ctx->status_label, "异常");
    }
    else {
        lv_obj_set_style_bg_color(ctx->status_dot, USB_STATUS_NORMAL_COLOR, 0);
        lv_label_set_text(ctx->status_label, "正常");
    }

    if(ctx->on) {
        lv_obj_set_style_bg_color(ctx->card, USB_ON_BG_COLOR, 0);
        lv_obj_set_style_bg_opa(ctx->card, LV_OPA_COVER, 0);
        /*frosted-glass look for the icon chip against the now-green card*/
        lv_obj_set_style_bg_color(ctx->icon_wrap, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(ctx->icon_wrap, LV_OPA_30, 0);
        lv_obj_set_style_text_color(ctx->icon_label, USB_ON_TEXT_COLOR, 0);
        lv_obj_set_style_text_color(ctx->name_label, USB_ON_TEXT_COLOR, 0);
        lv_obj_set_style_text_color(ctx->status_label, USB_ON_TEXT_COLOR, 0);
        if(ctx->watt_label) lv_obj_set_style_text_color(ctx->watt_label, USB_ON_TEXT_COLOR, 0);
    }
    else {
        /*fall back to whatever the attached shared theme styles provide,
         *instead of hardcoding an "off" palette here*/
        lv_obj_remove_local_style_prop(ctx->card, LV_STYLE_BG_COLOR, 0);
        lv_obj_remove_local_style_prop(ctx->card, LV_STYLE_BG_OPA, 0);
        lv_obj_remove_local_style_prop(ctx->icon_wrap, LV_STYLE_BG_COLOR, 0);
        lv_obj_remove_local_style_prop(ctx->icon_wrap, LV_STYLE_BG_OPA, 0);
        lv_obj_remove_local_style_prop(ctx->icon_label, LV_STYLE_TEXT_COLOR, 0);
        lv_obj_remove_local_style_prop(ctx->name_label, LV_STYLE_TEXT_COLOR, 0);
        lv_obj_remove_local_style_prop(ctx->status_label, LV_STYLE_TEXT_COLOR, 0);
        if(ctx->watt_label) {
            lv_obj_remove_local_style_prop(ctx->watt_label, LV_STYLE_TEXT_COLOR, 0);
            lv_label_set_text(ctx->watt_label, "-- W");
        }
    }
}

void usb_switch_set_port(uint8_t index, bool on)
{
    if(index >= USB_PORT_COUNT) return;

    s_ports[index].on = on;
    /*no current flows while off, so it can't be stuck in an over-current
     *fault - clear back to normal*/
    if(!on) s_ports[index].status = USB_PORT_STATUS_NORMAL;
    apply_port_visual(&s_ports[index]);
}

bool usb_switch_get_port(uint8_t index)
{
    if(index >= USB_PORT_COUNT) return false;

    return s_ports[index].on;
}

void usb_switch_set_power_monitor_port(bool on)
{
    /*same path as tapping the card: with a request callback installed the
     *hardware is switched and the card follows the reported state*/
    if(s_port_request_cb) s_port_request_cb(USB_POWER_MONITOR_INDEX, on);
    else usb_switch_set_port(USB_POWER_MONITOR_INDEX, on);
}

void usb_switch_set_speed_mode(usb_speed_mode_t mode)
{
    s_speed_mode = mode;
    if(s_speed_label) {
        lv_label_set_text(s_speed_label, mode == USB_SPEED_MODE_HIGH ? "高速" : "全速");
    }
}

usb_speed_mode_t usb_switch_get_speed_mode(void)
{
    return s_speed_mode;
}

void usb_switch_set_port_status(uint8_t index, usb_port_status_t status)
{
    if(index >= USB_PORT_COUNT) return;

    s_ports[index].status = status;
    apply_port_visual(&s_ports[index]);
}

usb_port_status_t usb_switch_get_port_status(uint8_t index)
{
    if(index >= USB_PORT_COUNT) return USB_PORT_STATUS_NORMAL;

    return s_ports[index].status;
}

usb_power_reading_t usb_switch_get_power_reading(void)
{
    usb_port_ctx_t * ctx = &s_ports[USB_POWER_MONITOR_INDEX];
    power_sample_t sample = {0};
    if(ctx->on) power_monitor_get_latest(&sample);
    usb_power_reading_t reading = {
        .on = ctx->on,
        .status = ctx->status,
        .volts = sample.volts,
        .amps = sample.amps,
    };
    return reading;
}

lv_obj_t * usb_switch_get_power_badge(void)
{
    return s_power_badge;
}

static void port_click_cb(lv_event_t * e)
{
    /*a press-drag-release that ends in a swipe gesture still fires CLICKED on
     *release here, because this card is clickable but not scrollable (LVGL
     *only suppresses CLICKED when a scroll_obj was found). The gesture dir
     *stays set on the indev for the whole press-release cycle, so use it to
     *tell "this release ended a swipe" apart from a real tap*/
    if(lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_NONE) return;

    uint8_t index = (uint8_t)(uintptr_t) lv_event_get_user_data(e);
    if(s_port_request_cb) s_port_request_cb(index, !s_ports[index].on);
    else usb_switch_set_port(index, !s_ports[index].on);
}

/*same swipe-vs-tap race as port_click_cb above, guarding the badge instead
 *of the card - this callback doesn't navigate anywhere itself (this module
 *stays unaware of any oscilloscope page), it only exists so a caller's own
 *CLICKED handler on this same object (added via usb_switch_get_power_badge())
 *isn't fooled by a swipe gesture that happens to release over the badge*/
static void power_badge_click_cb(lv_event_t * e)
{
    if(lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_NONE) {
        lv_event_stop_processing(e);
    }
}

/*tap on the lightning badge drills into the oscilloscope page. Registered
 *after power_badge_click_cb, which swallows the CLICKED that ends a swipe*/
static void power_badge_open_scope_cb(lv_event_t * e)
{
    lvgl_mgr_push("power_scope");
}

/*shows the power-monitored port's wattage from the INA226 sampling service
 *(default slow rate; the oscilloscope page switches it to fast while open)*/
static void power_reading_timer_cb(lv_timer_t * timer)
{
    usb_port_ctx_t * ctx = (usb_port_ctx_t *) lv_timer_get_user_data(timer);
    power_sample_t sample;
    if(!ctx->on || !power_monitor_get_latest(&sample)) return;

    float watts = sample.watts > 0 ? sample.watts : 0;
    int32_t tenths = (int32_t)(watts * 10 + 0.5f);
    lv_label_set_text_fmt(ctx->watt_label, "%d.%d W", (int)(tenths / 10), (int)(tenths % 10));
}

static void build_port_card(lv_obj_t * parent, uint8_t index)
{
    usb_port_ctx_t * ctx = &s_ports[index];
    bool is_power_monitor = (index == USB_POWER_MONITOR_INDEX);

    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 180, 180);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(card, &ui_theme_style_card_bg, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_add_event_cb(card, port_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t) index);

    /*icon chip: a soft circular backing behind the symbol instead of a bare
     *glyph floating on the card, so it reads as a proper icon button*/
    lv_obj_t * icon_wrap = lv_obj_create(card);
    lv_obj_remove_style_all(icon_wrap);
    lv_obj_set_size(icon_wrap, 72, 72);
    lv_obj_set_style_radius(icon_wrap, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(icon_wrap, &ui_theme_style_divider_bg, 0);
    lv_obj_clear_flag(icon_wrap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(icon_wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * icon_label = lv_label_create(icon_wrap);
    lv_label_set_text(icon_label, LV_SYMBOL_USB);
    lv_obj_add_style(icon_label, &ui_theme_style_text_main, 0);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_40, 0);
    lv_obj_center(icon_label);

    lv_obj_t * name_label = lv_label_create(card);
    lv_label_set_text(name_label, s_port_names[index]);
    lv_obj_add_style(name_label, &ui_theme_style_text_main, 0);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);

    /*status row: a small colored dot plus a 正常/异常 label, so the state
     *reads at a glance even for a colorblind viewer who only gets the text*/
    lv_obj_t * status_row = lv_obj_create(card);
    lv_obj_remove_style_all(status_row);
    lv_obj_set_size(status_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(status_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(status_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status_row, 6, 0);

    /*neutral chip backing behind the dot, same idea as icon_wrap above - a
     *fixed theme-gray circle that never matches the colored dot or the
     *card's on-state green, so the dot always reads against it regardless
     *of the card's own background*/
    lv_obj_t * status_dot_wrap = lv_obj_create(status_row);
    lv_obj_remove_style_all(status_dot_wrap);
    lv_obj_set_size(status_dot_wrap, 12, 12);
    lv_obj_set_style_radius(status_dot_wrap, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(status_dot_wrap, &ui_theme_style_divider_bg, 0);
    lv_obj_clear_flag(status_dot_wrap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(status_dot_wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * status_dot = lv_obj_create(status_dot_wrap);
    lv_obj_remove_style_all(status_dot);
    lv_obj_set_size(status_dot, 8, 8);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(status_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(status_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(status_dot);

    lv_obj_t * status_label = lv_label_create(status_row);
    lv_obj_add_style(status_label, &ui_theme_style_text_muted, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_cjk_12, 0);

    lv_obj_t * watt_label = NULL;
    if(is_power_monitor) {
        watt_label = lv_label_create(card);
        lv_obj_add_style(watt_label, &ui_theme_style_text_muted, 0);
        lv_obj_set_style_text_font(watt_label, &lv_font_montserrat_12, 0);

        /*small corner badge marks this port as power-metering capable -
         *always on, independent of the port's own on/off state*/
        lv_obj_t * badge = lv_obj_create(card);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 36, 36);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(badge, USB_POWER_BADGE_COLOR, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        /*clickable - this is the tap target a caller wires up (via
         *usb_switch_get_power_badge()) to drill into a page that visualizes
         *this port's reading; the card underneath keeps its own separate
         *on/off CLICKED handler, unaffected since this child doesn't bubble*/
        lv_obj_add_flag(badge, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(badge, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -8, 8);
        lv_obj_add_event_cb(badge, power_badge_click_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(badge, power_badge_open_scope_cb, LV_EVENT_CLICKED, NULL);
        s_power_badge = badge;

        lv_obj_t * badge_icon = lv_label_create(badge);
        lv_label_set_text(badge_icon, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_color(badge_icon, lv_color_white(), 0);
        lv_obj_set_style_text_font(badge_icon, &lv_font_montserrat_16, 0);
        lv_obj_center(badge_icon);

        lv_timer_create(power_reading_timer_cb, USB_POWER_READING_PERIOD_MS, ctx);
    }

    ctx->index = index;
    ctx->card = card;
    ctx->icon_wrap = icon_wrap;
    ctx->icon_label = icon_label;
    ctx->name_label = name_label;
    ctx->status_dot = status_dot;
    ctx->status_label = status_label;
    ctx->watt_label = watt_label;
    apply_port_visual(ctx); /*ctx->on/status keep whatever was set before the page was built*/
}

void usb_switch_ui_init(void)
{

    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_theme_style_screen_bg, 0);
    lv_obj_set_style_pad_all(scr, 16, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 16, 0);
    s_screen = scr;
    lvgl_mgr_set_swipe(scr, LVGL_MGR_BACK, NULL); /*swipe left -> back to the dock*/

    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "USB端口");
    lv_obj_add_style(title, &ui_theme_style_text_main, 0);
    lv_obj_set_style_text_font(title, &lv_font_cjk_20, 0);

    // s_speed_label = lv_label_create(scr);
    // lv_obj_add_style(s_speed_label, &ui_theme_style_text_muted, 0);
    // lv_obj_set_style_text_font(s_speed_label, &lv_font_cjk_12, 0);
    // lv_label_set_text(s_speed_label, s_speed_mode == USB_SPEED_MODE_HIGH ? "高速" : "全速");

    lv_obj_t * grid = lv_obj_create(scr);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 16, 0);
    lv_obj_set_style_pad_column(grid, 16, 0);
    lv_obj_set_width(grid, 180 * 2 + 16);

    for(uint8_t i = 0; i < USB_PORT_COUNT; i++) {
        build_port_card(grid, i);
    }
}

lv_obj_t * usb_switch_get_screen(void)
{
    return s_screen;
}

LVGL_REGISTER_PAGE(usb_switch,
    .create = usb_switch_ui_init,
    .get_screen = usb_switch_get_screen);
