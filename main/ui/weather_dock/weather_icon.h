#ifndef LVGL_WEATHER_ICON_H
#define LVGL_WEATHER_ICON_H

#include <lvgl.h>
#include "weather_data.h"

/**
 * Single entry point for weather condition icons. size >= 48 renders the
 * big artwork (day/night variant picked from the current clock), smaller
 * sizes render the small artwork; either is scaled to fit size if it
 * doesn't match the baked resolution.
 */
void weather_icon_create(lv_obj_t * parent, weather_cond_t cond, int32_t size);

#endif //LVGL_WEATHER_ICON_H
