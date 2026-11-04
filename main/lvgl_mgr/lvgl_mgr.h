#ifndef LVGL_MGR_H
#define LVGL_MGR_H

#include <stdbool.h>
#include <esp_err.h>
#include "lvgl_page.h"

/* Page manager: walks every LVGL_REGISTER_PAGE() entry, keeps a navigation
 * stack, and loads screens.
 *
 * Thread model: every navigation call below is safe from any task. They only
 * enqueue a command; a short LVGL timer (running on the esp_lvgl_port task)
 * drains the queue and touches LVGL, so page code never races with rendering.
 * Pages are created lazily on first show and then kept cached. */

/* Collects registered pages and starts the command pump. `disp` is the LVGL
 * display from display_init(). Call once, after display_init(). */
esp_err_t lvgl_mgr_init(lv_display_t *disp);

/* Number of pages found in the registry (valid after lvgl_mgr_init()). */
int lvgl_mgr_page_count(void);

/* Clears the stack and makes `name` the sole (root) page, no animation. */
esp_err_t lvgl_mgr_switch_root(const char *name);

/* Direction of the finger swipe that drives a transition: LEFT slides the new
 * page in from the right edge, RIGHT from the left edge. */
typedef enum {
    LVGL_MGR_DIR_LEFT,
    LVGL_MGR_DIR_RIGHT,
} lvgl_mgr_dir_t;

/* Pushes `name` on top of the stack, sliding in as for a left swipe. No-op if
 * it is already on top. */
esp_err_t lvgl_mgr_push(const char *name);

/* Same as lvgl_mgr_push() with an explicit slide direction. */
esp_err_t lvgl_mgr_push_dir(const char *name, lvgl_mgr_dir_t dir);

/* Pops the top page, sliding back the way it came in. No-op at the root page. */
esp_err_t lvgl_mgr_pop(void);

/* Swipe target meaning "go back" (pop) for lvgl_mgr_set_swipe(). */
#define LVGL_MGR_BACK "<back>"

/* Makes a swipe on `screen` navigate: swiping left goes to page `on_left`,
 * swiping right to `on_right`; either may be NULL (ignored) or LVGL_MGR_BACK.
 * Target names must be string literals. Call from the page's create(). Widgets
 * that use horizontal drags themselves (sliders) should clear their
 * LV_OBJ_FLAG_GESTURE_BUBBLE so they don't also trigger navigation. */
void lvgl_mgr_set_swipe(lv_obj_t *screen, const char *on_left, const char *on_right);

/* Runs fn(arg) on the LVGL task, for UI updates posted from other tasks. */
esp_err_t lvgl_mgr_post(void (*fn)(void *arg), void *arg);

#endif // LVGL_MGR_H
