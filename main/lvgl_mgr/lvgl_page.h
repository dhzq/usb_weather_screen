#ifndef LVGL_PAGE_H
#define LVGL_PAGE_H

#include <lvgl.h>

/* One UI page. Each page's own .c file declares one of these via
 * LVGL_REGISTER_PAGE(); the manager discovers them at runtime by walking the
 * linker section they all land in (see main/linker.lf) - there is no central
 * page list to edit when adding a page.
 *
 * All callbacks run on the LVGL task (never call them from elsewhere). */
typedef struct {
    const char *name;              /* unique id, used for navigation */
    void (*create)(void);          /* builds the page's screen; called lazily on first show */
    lv_obj_t *(*get_screen)(void); /* the screen built by create() */
    void (*on_show)(void);         /* optional: page just became the active screen */
    void (*on_hide)(void);         /* optional: page is about to stop being active */
} lvgl_page_t;

/* Name of the linker input section every registered page lives in. Must match
 * main/linker.lf. */
#define LVGL_PAGE_SECTION ".lvgl_pages"

/* Registers a page from its own .c file, at build time (no runtime call):
 *
 *   LVGL_REGISTER_PAGE(settings,
 *       .create = settings_ui_init,
 *       .get_screen = settings_get_screen);
 *
 * `id` is a bare identifier; it becomes the page name string and keeps the
 * emitted symbol unique. Optional fields (.on_show/.on_hide) go in the
 * trailing designated initializers. */
#define LVGL_REGISTER_PAGE(id, ...)                                              \
    static const lvgl_page_t s_lvgl_page_##id                                    \
        __attribute__((used, section(LVGL_PAGE_SECTION))) = {                    \
        .name = #id,                                                             \
        __VA_ARGS__                                                              \
    }

#endif // LVGL_PAGE_H
