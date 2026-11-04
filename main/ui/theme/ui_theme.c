#include "theme/ui_theme.h"

lv_style_t ui_theme_style_screen_bg;
lv_style_t ui_theme_style_card_bg;
lv_style_t ui_theme_style_divider_bg;
lv_style_t ui_theme_style_divider_border;
lv_style_t ui_theme_style_muted_bg;
lv_style_t ui_theme_style_text_main;
lv_style_t ui_theme_style_text_muted;

typedef struct {
    lv_color_t screen_bg;
    lv_color_t card_bg;
    lv_color_t divider;
    lv_color_t text_main;
    lv_color_t text_muted;
    lv_color_t muted_bg;
} ui_theme_palette_t;

static ui_theme_mode_t current_mode;
static bool inited = false;

static ui_theme_palette_t palette_for(ui_theme_mode_t mode)
{
    ui_theme_palette_t p;

    if(mode == UI_THEME_LIGHT) {
        p.screen_bg  = lv_color_hex(0xd9dee6);
        p.card_bg    = lv_color_hex(0xeef1f5);
        p.divider    = lv_color_hex(0xc5cbd4);
        p.text_main  = lv_color_hex(0x20242e);
        p.text_muted = lv_color_hex(0x5e6775);
    }
    else {
        p.screen_bg  = lv_color_hex(0x1a1e28);
        p.card_bg    = lv_color_hex(0x2b3242);
        p.divider    = lv_color_hex(0x4a5468);
        p.text_main  = lv_color_white();
        p.text_muted = lv_color_hex(0x8a93a3);
    }

    p.muted_bg = p.text_muted;

    return p;
}

static void apply_palette(const ui_theme_palette_t * p)
{
    lv_style_set_bg_color(&ui_theme_style_screen_bg, p->screen_bg);
    lv_style_set_bg_color(&ui_theme_style_card_bg, p->card_bg);
    lv_style_set_bg_color(&ui_theme_style_divider_bg, p->divider);
    lv_style_set_border_color(&ui_theme_style_divider_border, p->divider);
    lv_style_set_line_color(&ui_theme_style_divider_border, p->divider);
    lv_style_set_bg_color(&ui_theme_style_muted_bg, p->muted_bg);
    lv_style_set_text_color(&ui_theme_style_text_main, p->text_main);
    lv_style_set_text_color(&ui_theme_style_text_muted, p->text_muted);
}

void ui_theme_init(ui_theme_mode_t mode)
{
    if(inited) {
        ui_theme_set(mode);
        return;
    }

    lv_style_init(&ui_theme_style_screen_bg);
    lv_style_set_bg_opa(&ui_theme_style_screen_bg, LV_OPA_COVER);

    lv_style_init(&ui_theme_style_card_bg);
    lv_style_set_bg_opa(&ui_theme_style_card_bg, LV_OPA_COVER);

    lv_style_init(&ui_theme_style_divider_bg);
    lv_style_set_bg_opa(&ui_theme_style_divider_bg, LV_OPA_COVER);

    lv_style_init(&ui_theme_style_divider_border);

    lv_style_init(&ui_theme_style_muted_bg);
    lv_style_set_bg_opa(&ui_theme_style_muted_bg, LV_OPA_COVER);

    lv_style_init(&ui_theme_style_text_main);
    lv_style_init(&ui_theme_style_text_muted);

    inited = true;

    current_mode = mode;
    ui_theme_palette_t p = palette_for(mode);
    apply_palette(&p);
}

void ui_theme_set(ui_theme_mode_t mode)
{
    if(!inited) {
        ui_theme_init(mode);
        return;
    }
    if(mode == current_mode) return;

    current_mode = mode;
    ui_theme_palette_t p = palette_for(mode);
    apply_palette(&p);

    lv_obj_report_style_change(&ui_theme_style_screen_bg);
    lv_obj_report_style_change(&ui_theme_style_card_bg);
    lv_obj_report_style_change(&ui_theme_style_divider_bg);
    lv_obj_report_style_change(&ui_theme_style_divider_border);
    lv_obj_report_style_change(&ui_theme_style_muted_bg);
    lv_obj_report_style_change(&ui_theme_style_text_main);
    lv_obj_report_style_change(&ui_theme_style_text_muted);
}

ui_theme_mode_t ui_theme_get(void)
{
    return current_mode;
}
