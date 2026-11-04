#ifndef LVGL_UI_THEME_H
#define LVGL_UI_THEME_H

#include <lvgl.h>

typedef enum {
    UI_THEME_DARK,
    UI_THEME_LIGHT,
} ui_theme_mode_t;

/*Single, app-wide theme. Pages never pick a mode themselves: they only attach
 *the shared styles below, so one ui_theme_set() call recolors every page.*/

/*call once at startup, before any page is created (see app.c)*/
void ui_theme_init(ui_theme_mode_t mode);

/*switches the whole UI at once - recolors every object on every screen that
 *was styled via the shared styles below (including pages that are cached but
 *not currently shown), no UI rebuild needed. Must run in LVGL context (an
 *LVGL event callback / timer, or under lvgl_port_lock()); from another task
 *use lvgl_mgr_post()*/
void ui_theme_set(ui_theme_mode_t mode);
ui_theme_mode_t ui_theme_get(void);

/*shared styles - attach with lv_obj_add_style() instead of hardcoding a
 *color via lv_obj_set_style_xxx(), so ui_theme_set() can recolor
 *every attached object at once via lv_obj_report_style_change()*/
extern lv_style_t ui_theme_style_screen_bg;
extern lv_style_t ui_theme_style_card_bg;
extern lv_style_t ui_theme_style_divider_bg;
extern lv_style_t ui_theme_style_divider_border; /*border_color + line_color, e.g. for a chart's grid lines*/
extern lv_style_t ui_theme_style_muted_bg;   /*弱化色当背景用，如降雨图表的刻度点*/
extern lv_style_t ui_theme_style_text_main;
extern lv_style_t ui_theme_style_text_muted;

#endif //LVGL_UI_THEME_H
