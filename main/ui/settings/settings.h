#ifndef LVGL_SETTINGS_H
#define LVGL_SETTINGS_H

#include <lvgl.h>

/*builds this page's UI as an independent screen (not auto-loaded); colors come
 *from the shared theme (theme/ui_theme.h), which must already be initialized*/
void settings_ui_init(void);

/*the screen object this page lives on, for the caller to switch to (e.g. via
 *lv_screen_load_anim())*/
lv_obj_t * settings_get_screen(void);

#endif //LVGL_SETTINGS_H
