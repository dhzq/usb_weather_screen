#ifndef UI_FONTS_H
#define UI_FONTS_H

#include <lvgl.h>

/*
 * CJK bitmap fonts for this app's Chinese text (weather conditions, weekday
 * names, rain/humidity labels). Each only embeds the glyphs actually used
 * here plus basic ASCII, generated with lv_font_conv - see
 * weather_dock/fonts_HOWTO.md for the exact command to regenerate them if
 * new Chinese vocabulary is added.
 */
LV_FONT_DECLARE(lv_font_cjk_12)
LV_FONT_DECLARE(lv_font_cjk_14)
LV_FONT_DECLARE(lv_font_cjk_16)
LV_FONT_DECLARE(lv_font_cjk_20)

#endif //UI_FONTS_H
