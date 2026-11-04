#include "lvgl_mgr.h"

#include <stdlib.h>
#include <string.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

static const char *TAG = "lvgl_mgr";

#define LVGL_MGR_MAX_PAGES      16
#define LVGL_MGR_STACK_DEPTH    8
#define LVGL_MGR_QUEUE_LEN      16
#define LVGL_MGR_PUMP_PERIOD_MS 20
#define LVGL_MGR_ANIM_MS        200
#define LVGL_MGR_NAME_MAX       24

/* Bounds of the registry section, emitted by the SURROUND() in main/linker.lf. */
extern const lvgl_page_t _lvgl_pages_start[];
extern const lvgl_page_t _lvgl_pages_end[];

typedef struct {
    const lvgl_page_t *def;
    lv_obj_t *screen; /* NULL until first shown */
} page_rt_t;

typedef enum {
    CMD_SWITCH_ROOT,
    CMD_PUSH,
    CMD_POP,
    CMD_CALL,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    lvgl_mgr_dir_t dir;
    char name[LVGL_MGR_NAME_MAX];
    void (*fn)(void *arg);
    void *arg;
} cmd_t;

static page_rt_t s_pages[LVGL_MGR_MAX_PAGES];
static int s_page_count;
static int s_stack[LVGL_MGR_STACK_DEPTH]; /* indices into s_pages */
static lvgl_mgr_dir_t s_stack_dir[LVGL_MGR_STACK_DEPTH]; /* how each entry was pushed */
static int s_depth;
static QueueHandle_t s_queue;

static int find_page(const char *name)
{
    for (int i = 0; i < s_page_count; i++) {
        if (strcmp(s_pages[i].def->name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void collect_pages(void)
{
    for (const lvgl_page_t *p = _lvgl_pages_start; p < _lvgl_pages_end; p++) {
        if (p->name == NULL || p->create == NULL || p->get_screen == NULL) {
            ESP_LOGE(TAG, "skipping malformed page entry %p", p);
            continue;
        }
        if (strlen(p->name) >= LVGL_MGR_NAME_MAX) {
            ESP_LOGE(TAG, "page name '%s' too long (max %d), skipped", p->name, LVGL_MGR_NAME_MAX - 1);
            continue;
        }
        if (find_page(p->name) >= 0) {
            ESP_LOGE(TAG, "duplicate page '%s', skipped", p->name);
            continue;
        }
        if (s_page_count >= LVGL_MGR_MAX_PAGES) {
            ESP_LOGE(TAG, "too many pages (max %d), '%s' skipped", LVGL_MGR_MAX_PAGES, p->name);
            continue;
        }
        s_pages[s_page_count++] = (page_rt_t){ .def = p, .screen = NULL };
        ESP_LOGI(TAG, "registered page '%s'", p->name);
    }
}

/* Builds the page's screen on first use. Returns NULL on failure. */
static lv_obj_t *ensure_screen(page_rt_t *page)
{
    if (page->screen == NULL) {
        page->def->create();
        page->screen = page->def->get_screen();
        if (page->screen == NULL) {
            ESP_LOGE(TAG, "page '%s' created no screen", page->def->name);
        }
    }
    return page->screen;
}

/* Makes s_pages[to] the active screen. `from` is the page being left, or -1. */
static bool show_page(int from, int to, lv_screen_load_anim_t anim)
{
    page_rt_t *next = &s_pages[to];
    lv_obj_t *scr = ensure_screen(next);
    if (scr == NULL) {
        return false;
    }
    if (from >= 0 && s_pages[from].def->on_hide) {
        s_pages[from].def->on_hide();
    }
    lv_screen_load_anim(scr, anim, anim == LV_SCREEN_LOAD_ANIM_NONE ? 0 : LVGL_MGR_ANIM_MS, 0, false);
    if (next->def->on_show) {
        next->def->on_show();
    }
    return true;
}

static void do_switch_root(const char *name)
{
    int idx = find_page(name);
    if (idx < 0) {
        ESP_LOGE(TAG, "switch_root: unknown page '%s'", name);
        return;
    }
    int from = s_depth > 0 ? s_stack[s_depth - 1] : -1;
    if (from == idx && s_depth == 1) {
        return;
    }
    if (show_page(from == idx ? -1 : from, idx, LV_SCREEN_LOAD_ANIM_NONE)) {
        s_stack[0] = idx;
        s_depth = 1;
    }
}

static void do_push(const char *name, lvgl_mgr_dir_t dir)
{
    int idx = find_page(name);
    if (idx < 0) {
        ESP_LOGE(TAG, "push: unknown page '%s'", name);
        return;
    }
    if (s_depth >= LVGL_MGR_STACK_DEPTH) {
        ESP_LOGE(TAG, "push '%s': stack full", name);
        return;
    }
    int from = s_depth > 0 ? s_stack[s_depth - 1] : -1;
    if (from == idx) {
        return;
    }
    lv_screen_load_anim_t anim = dir == LVGL_MGR_DIR_RIGHT ? LV_SCREEN_LOAD_ANIM_MOVE_RIGHT
                                                           : LV_SCREEN_LOAD_ANIM_MOVE_LEFT;
    if (show_page(from, idx, anim)) {
        s_stack[s_depth] = idx;
        s_stack_dir[s_depth] = dir;
        s_depth++;
    }
}

static void do_pop(void)
{
    if (s_depth <= 1) {
        return;
    }
    /* Slide back the way the top page came in. */
    lv_screen_load_anim_t anim = s_stack_dir[s_depth - 1] == LVGL_MGR_DIR_RIGHT ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT
                                                                                 : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT;
    if (show_page(s_stack[s_depth - 1], s_stack[s_depth - 2], anim)) {
        s_depth--;
    }
}

/* Runs on the LVGL task (lv_timer), so no lvgl_port_lock() needed. */
static void pump_cb(lv_timer_t *timer)
{
    (void)timer;
    cmd_t cmd;
    while (xQueueReceive(s_queue, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
        case CMD_SWITCH_ROOT: do_switch_root(cmd.name); break;
        case CMD_PUSH:        do_push(cmd.name, cmd.dir); break;
        case CMD_POP:         do_pop(); break;
        case CMD_CALL:        cmd.fn(cmd.arg); break;
        }
    }
}

static esp_err_t send_cmd(cmd_type_t type, lvgl_mgr_dir_t dir, const char *name, void (*fn)(void *), void *arg)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    cmd_t cmd = { .type = type, .dir = dir, .fn = fn, .arg = arg };
    if (name != NULL) {
        if (strlen(name) >= LVGL_MGR_NAME_MAX) {
            return ESP_ERR_INVALID_ARG;
        }
        strcpy(cmd.name, name);
    }
    return xQueueSend(s_queue, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t lvgl_mgr_init(lv_display_t *disp)
{
    if (disp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    collect_pages();
    ESP_LOGI(TAG, "%d page(s) registered", s_page_count);

    s_queue = xQueueCreate(LVGL_MGR_QUEUE_LEN, sizeof(cmd_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }
    lv_timer_create(pump_cb, LVGL_MGR_PUMP_PERIOD_MS, NULL);
    lvgl_port_unlock();
    return ESP_OK;
}

int lvgl_mgr_page_count(void)
{
    return s_page_count;
}

esp_err_t lvgl_mgr_switch_root(const char *name)
{
    return name ? send_cmd(CMD_SWITCH_ROOT, LVGL_MGR_DIR_LEFT, name, NULL, NULL) : ESP_ERR_INVALID_ARG;
}

esp_err_t lvgl_mgr_push(const char *name)
{
    return lvgl_mgr_push_dir(name, LVGL_MGR_DIR_LEFT);
}

esp_err_t lvgl_mgr_push_dir(const char *name, lvgl_mgr_dir_t dir)
{
    return name ? send_cmd(CMD_PUSH, dir, name, NULL, NULL) : ESP_ERR_INVALID_ARG;
}

esp_err_t lvgl_mgr_pop(void)
{
    return send_cmd(CMD_POP, LVGL_MGR_DIR_LEFT, NULL, NULL, NULL);
}

esp_err_t lvgl_mgr_post(void (*fn)(void *arg), void *arg)
{
    return fn ? send_cmd(CMD_CALL, LVGL_MGR_DIR_LEFT, NULL, fn, arg) : ESP_ERR_INVALID_ARG;
}

typedef struct {
    const char *on_left;
    const char *on_right;
} swipe_targets_t;

static void swipe_go(const char *target, lvgl_mgr_dir_t dir)
{
    if (target == NULL) {
        return;
    }
    if (strcmp(target, LVGL_MGR_BACK) == 0) {
        lvgl_mgr_pop();
    } else {
        lvgl_mgr_push_dir(target, dir);
    }
}

static void swipe_event_cb(lv_event_t *e)
{
    const swipe_targets_t *t = lv_event_get_user_data(e);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_GESTURE:
        switch (lv_indev_get_gesture_dir(lv_indev_active())) {
        case LV_DIR_LEFT:  swipe_go(t->on_left, LVGL_MGR_DIR_LEFT); break;
        case LV_DIR_RIGHT: swipe_go(t->on_right, LVGL_MGR_DIR_RIGHT); break;
        default: break;
        }
        break;
    case LV_EVENT_DELETE:
        free((void *)t);
        break;
    default:
        break;
    }
}

void lvgl_mgr_set_swipe(lv_obj_t *screen, const char *on_left, const char *on_right)
{
    swipe_targets_t *t = malloc(sizeof(*t));
    if (screen == NULL || t == NULL) {
        free(t);
        return;
    }
    t->on_left = on_left;
    t->on_right = on_right;

    /* LVGL sends the gesture to the first object up the parent chain that has
     * GESTURE_BUBBLE cleared (and drops it if the chain runs out), so the
     * screen has to be that object. */
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(screen, swipe_event_cb, LV_EVENT_GESTURE, t);
    lv_obj_add_event_cb(screen, swipe_event_cb, LV_EVENT_DELETE, t);
}
