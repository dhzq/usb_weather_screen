#include "settings.h"
#include "lvgl_mgr/lvgl_mgr.h"
#include "theme/ui_theme.h"
#include "fonts/ui_fonts.h"
#include "usb_switch/usb_switch.h"
#include "services/settings_store.h"
#include "services/wifi_service.h"

#include <esp_system.h>

static lv_obj_t * s_screen;

static lv_obj_t * s_network_state_label;

static lv_obj_t * build_row(lv_obj_t * parent, bool is_last)
{
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(row, 12, 0);
    lv_obj_set_style_pad_bottom(row, 12, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if(!is_last) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_add_style(row, &ui_theme_style_divider_border, 0);
    }
    return row;
}

/*plain non-interactive wrapper for stacking a title + sub label on the left
 *side of a row, so the right side can still be a single switch/slider*/
static lv_obj_t * build_label_stack(lv_obj_t * row)
{
    lv_obj_t * stack = lv_obj_create(row);
    lv_obj_remove_style_all(stack);
    lv_obj_set_size(stack, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(stack, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(stack, 2, 0);
    return stack;
}

static lv_obj_t * build_title_label(lv_obj_t * parent, const char * text)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_add_style(label, &ui_theme_style_text_main, 0);
    lv_obj_set_style_text_font(label, &lv_font_cjk_16, 0);
    return label;
}

static lv_obj_t * build_sub_label(lv_obj_t * parent, const char * text)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_add_style(label, &ui_theme_style_text_muted, 0);
    lv_obj_set_style_text_font(label, &lv_font_cjk_12, 0);
    return label;
}

static void dark_mode_switch_cb(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target(e);
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ui_theme_set(checked ? UI_THEME_DARK : UI_THEME_LIGHT);
    settings_store_set_dark_mode(checked);
}

static void brightness_slider_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e);
    lv_obj_t * pct_label = (lv_obj_t *) lv_event_get_user_data(e);

    /*applied live while dragging; flash is only written on release (below)*/
    settings_store_set_brightness((uint8_t) lv_slider_get_value(slider));
    lv_label_set_text_fmt(pct_label, "%d%%", (int) settings_store_get_brightness());
}

static void brightness_slider_release_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    settings_store_save_brightness();
}

static void restart_timer_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    esp_restart();
}

/*Switching mode reboots: the WiFi stack runs as either station or provisioning
 *SoftAP for a whole boot, so the flag is stored and the device restarts*/
static void network_config_switch_cb(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target(e);
    bool want_config = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if(!want_config) {
        char ssid[33], password[64];
        if(!settings_store_get_wifi(ssid, sizeof(ssid), password, sizeof(password))) {
            /*nothing to fall back to: staying in config mode is the only option*/
            lv_obj_add_state(sw, LV_STATE_CHECKED);
            return; /*label already shows the AP details*/
        }
    }

    settings_store_set_config_mode(want_config);
    lv_obj_add_state(sw, LV_STATE_DISABLED);
    lv_label_set_text(s_network_state_label, "Restarting...");
    lv_timer_t * timer = lv_timer_create(restart_timer_cb, 500, NULL);
    lv_timer_set_repeat_count(timer, 1);
}

static void usb_speed_switch_cb(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target(e);
    lv_obj_t * sub_label = (lv_obj_t *) lv_event_get_user_data(e);
    bool high_speed = lv_obj_has_state(sw, LV_STATE_CHECKED);

    usb_switch_set_speed_mode(high_speed ? USB_SPEED_MODE_HIGH : USB_SPEED_MODE_FULL);
    lv_label_set_text(sub_label, high_speed ? "高速" : "全速");
}

static void build_dark_mode_row(lv_obj_t * section, bool is_last)
{
    lv_obj_t * row = build_row(section, is_last);
    build_title_label(row, "深色模式");

    lv_obj_t * sw = lv_switch_create(row);
    if(ui_theme_get() == UI_THEME_DARK) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, dark_mode_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void build_brightness_row(lv_obj_t * section, bool is_last)
{
    lv_obj_t * row = build_row(section, is_last);
    build_title_label(row, "亮度");

    lv_obj_t * right = lv_obj_create(row);
    lv_obj_remove_style_all(right);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 10, 0);

    lv_obj_t * slider = lv_slider_create(right);
    lv_obj_set_width(slider, 140);
    lv_slider_set_range(slider, SETTINGS_BRIGHTNESS_MIN, 100);
    lv_slider_set_value(slider, settings_store_get_brightness(), LV_ANIM_OFF);
    /*horizontal drags belong to the slider, not to the page's swipe navigation*/
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t * pct_label = build_sub_label(right, "");
    lv_label_set_text_fmt(pct_label, "%d%%", (int) settings_store_get_brightness());
    lv_obj_set_width(pct_label, 40);

    lv_obj_add_event_cb(slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, pct_label);
    lv_obj_add_event_cb(slider, brightness_slider_release_cb, LV_EVENT_RELEASED, NULL);
}

static void build_network_config_row(lv_obj_t * section, bool is_last)
{
    lv_obj_t * row = build_row(section, is_last);
    bool config_mode = wifi_service_is_provisioning();

    lv_obj_t * stack = build_label_stack(row);
    build_title_label(stack, "配网模式");
    s_network_state_label = build_sub_label(stack, "正常");
    if(config_mode) {
        /*the CJK font subsets (ui/fonts) lack glyphs for a longer hint, so stick to ASCII + known characters*/
        lv_label_set_text_fmt(s_network_state_label, "配置模式 WiFi:%s  http://192.168.4.1",
                              wifi_service_get_ap_ssid());
        lv_obj_set_width(s_network_state_label, 330);
    }

    lv_obj_t * sw = lv_switch_create(row);
    if(config_mode) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, network_config_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void build_usb_speed_row(lv_obj_t * section, bool is_last)
{
    lv_obj_t * row = build_row(section, is_last);
    bool high_speed = (usb_switch_get_speed_mode() == USB_SPEED_MODE_HIGH);

    lv_obj_t * stack = build_label_stack(row);
    build_title_label(stack, "USB速度模式");
    lv_obj_t * sub_label = build_sub_label(stack, high_speed ? "高速" : "全速");

    lv_obj_t * sw = lv_switch_create(row);
    if(high_speed) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, usb_speed_switch_cb, LV_EVENT_VALUE_CHANGED, sub_label);
}

void settings_ui_init(void)
{
    /*full-screen list, same shell as weather_dock's screen: the screen itself
     *doesn't scroll, a full-width content column underneath does*/

    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_theme_style_screen_bg, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    s_screen = scr;
    lvgl_mgr_set_swipe(scr, NULL, LVGL_MGR_BACK); /*swipe right -> back to the dock*/

    lv_obj_t * content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 16, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * title = lv_label_create(content);
    lv_label_set_text(title, "设置");
    lv_obj_add_style(title, &ui_theme_style_text_main, 0);
    lv_obj_set_style_text_font(title, &lv_font_cjk_20, 0);

    lv_obj_t * section = lv_obj_create(content);
    lv_obj_remove_style_all(section);
    lv_obj_set_size(section, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_style(section, &ui_theme_style_card_bg, 0);
    lv_obj_set_style_radius(section, 16, 0);
    lv_obj_set_style_pad_hor(section, 16, 0);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);

    build_dark_mode_row(section, false);
    build_brightness_row(section, false);
    build_network_config_row(section, false);
    build_usb_speed_row(section, true);
}

lv_obj_t * settings_get_screen(void)
{
    return s_screen;
}

LVGL_REGISTER_PAGE(settings,
    .create = settings_ui_init,
    .get_screen = settings_get_screen);
