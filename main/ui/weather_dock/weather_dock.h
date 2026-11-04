#ifndef LVGL_WEATHER_DOCK_H
#define LVGL_WEATHER_DOCK_H

#include <lvgl.h>
#include "services/weather_snapshot.h"

void weather_dock_ui_init(void);

/*the screen object this page lives on, for the caller to switch to (e.g. via
 *lv_screen_load_anim())*/
lv_obj_t * weather_dock_get_screen(void);

/*installs a freshly fetched weather snapshot (taking ownership) and redraws
 *the data sections if the page is already built. Must run on the LVGL task,
 *e.g. via lvgl_mgr_post()*/
void weather_dock_update(weather_snapshot_t * snap);

#endif //LVGL_WEATHER_DOCK_H
