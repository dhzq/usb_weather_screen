#ifndef LVGL_POWER_SCOPE_H
#define LVGL_POWER_SCOPE_H

#include <lvgl.h>

/*builds this page's UI as an independent screen (not auto-loaded)*/
void power_scope_ui_init(void);

/*the screen object this page lives on, for the caller to switch to (e.g. via
 *lv_screen_load_anim())*/
lv_obj_t * power_scope_get_screen(void);

#endif //LVGL_POWER_SCOPE_H
