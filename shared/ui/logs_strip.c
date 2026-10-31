/**
 * @file
 * @brief The strip of the LOGS tile (logs_strip.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "logs_strip.h"

#include <stddef.h>

#include "ui_tabs.h"

/** @brief The screens in the strip's order, left to right. */
static const ui_screen_id_t k_screens[] = {
    SCREEN_LOGS, SCREEN_FILES, SCREEN_WIFI,
};

/** @brief How many tabs the strip has. */
#define STRIP_TABS ((int)(sizeof(k_screens) / sizeof(k_screens[0])))

/** @brief The tabs: one row, whichever of the three screens draws it. */
static ui_tabs_t s_tabs;
/** @brief s_tabs holds its labels and its rectangles. */
static bool s_made;

/**
 * @brief The tabs, with @p here's selected.
 * @param here The screen on top.
 * @return The tabs.
 */
static ui_tabs_t *tabs_for(ui_screen_id_t here)
{
    if (!s_made) {
        const char *labels[STRIP_TABS];
        for (int i = 0; i < STRIP_TABS; ++i) {
            labels[i] = ui_router_title(k_screens[i]);
        }
        ui_tabs_init(&s_tabs, labels, STRIP_TABS,
                     gfx_rect_make(LOGS_STRIP_X, LOGS_STRIP_Y, LOGS_STRIP_W,
                                   LOGS_STRIP_H));
        s_made = true;
    }
    for (int i = 0; i < STRIP_TABS; ++i) {
        if (k_screens[i] == here) {
            s_tabs.selected = i;
        }
    }
    return &s_tabs;
}

void logs_strip_render(gfx_canvas_t *c, ui_screen_id_t here)
{
    ui_tabs_render(tabs_for(here), c);
}

bool logs_strip_event(const touch_event_t *e, ui_screen_id_t here)
{
    if (e == NULL) {
        return false;
    }
    ui_tabs_t *t = tabs_for(here);
    const bool held = t->pressed >= 0 && e->point.id == t->press_id;
    if (e->type == TOUCH_EVENT_DOWN && held) {
        t->pressed = -1;    /* that contact has gone, its release with it */
    }
    if (ui_tabs_event(t, e)) {
        ui_router_goto(k_screens[t->selected]);
        return true;
    }
    return (e->type == TOUCH_EVENT_DOWN)
               ? t->pressed >= 0 && t->press_id == e->point.id
               : held;
}

void logs_strip_cancel(void)
{
    ui_tabs_cancel(&s_tabs);
}
