/*
 * SPDX-License-Identifier: MIT
 */

#include "ui_workbench.h"

#include <string.h>

#include "ui_text.h"

/* Each task's label: LIVE is the strip's own word; TEST, PROGRAM and
 * SETTINGS are the words SERVO's pages, the programmer and SUPPLY use. */
static const ui_text_id_t k_label[UI_WB_TASKS] = {
    TX_WB_LIVE, TX_SV_PG_TEST, TX_KP_PROGRAM, TX_SUP_SETTINGS,
};

void ui_wb_strip_init(ui_wb_strip_t *w, unsigned tasks, int16_t x, int16_t y)
{
    if (w == NULL) {
        return;
    }
    memset(w, 0, sizeof(*w));
    tasks |= UI_WB_HAS(UI_WB_LIVE);
    ui_text_id_t ids[UI_WB_TASKS] = { TX_WB_LIVE };
    int n = 0;
    for (int t = 0; t < (int)UI_WB_TASKS; ++t) {
        if ((tasks & UI_WB_HAS(t)) != 0u) {
            ids[n]    = k_label[t];
            w->task[n] = (uint8_t)t;
            ++n;
        }
    }
    const int16_t width = (int16_t)(n * UI_WB_TAB_W + (n - 1) * UI_WB_TAB_GAP);
    ui_tabs_init_text(&w->tabs, ids, n,
                      (gfx_rect_t){ x, y, width, UI_WB_STRIP_H });
}

ui_wb_task_t ui_wb_strip_task(const ui_wb_strip_t *w)
{
    if (w == NULL || w->tabs.selected < 0 || w->tabs.selected >= w->tabs.count) {
        return UI_WB_LIVE;
    }
    return (ui_wb_task_t)w->task[w->tabs.selected];
}

/* The tab that shows @p task, or -1. */
static int tab_of(const ui_wb_strip_t *w, ui_wb_task_t task)
{
    for (int i = 0; i < w->tabs.count; ++i) {
        if (w->task[i] == (uint8_t)task) {
            return i;
        }
    }
    return -1;
}

bool ui_wb_strip_has(const ui_wb_strip_t *w, ui_wb_task_t task)
{
    return w != NULL && tab_of(w, task) >= 0;
}

bool ui_wb_strip_select(ui_wb_strip_t *w, ui_wb_task_t task)
{
    if (w == NULL) {
        return false;
    }
    const int i = tab_of(w, task);
    if (i < 0) {
        return false;
    }
    ui_tabs_cancel(&w->tabs);
    if (w->tabs.selected == i) {
        return false;
    }
    w->tabs.selected = i;
    return true;
}

void ui_wb_strip_open(ui_wb_strip_t *w)
{
    (void)ui_wb_strip_select(w, UI_WB_LIVE);
    if (w != NULL) {
        ui_tabs_cancel(&w->tabs);
    }
}

bool ui_wb_strip_event(ui_wb_strip_t *w, const touch_event_t *evt)
{
    return w != NULL && ui_tabs_event(&w->tabs, evt);
}

bool ui_wb_strip_pressed(const ui_wb_strip_t *w)
{
    return w != NULL && w->tabs.pressed >= 0;
}

void ui_wb_strip_cancel(ui_wb_strip_t *w)
{
    if (w != NULL) {
        ui_tabs_cancel(&w->tabs);
    }
}

gfx_rect_t ui_wb_strip_rect(const ui_wb_strip_t *w)
{
    if (w == NULL || w->tabs.count <= 0) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    const gfx_rect_t a = w->tabs.rect[0];
    const gfx_rect_t z = w->tabs.rect[w->tabs.count - 1];
    return (gfx_rect_t){ a.x, a.y, (int16_t)(z.x + z.w - a.x), a.h };
}

gfx_rect_t ui_wb_strip_tab_rect(const ui_wb_strip_t *w, ui_wb_task_t task)
{
    const int i = (w != NULL) ? tab_of(w, task) : -1;
    if (i < 0) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return w->tabs.rect[i];
}

void ui_wb_strip_render(const ui_wb_strip_t *w, gfx_canvas_t *c)
{
    if (w != NULL && w->tabs.count > 0) {
        ui_tabs_render(&w->tabs, c);
    }
}
