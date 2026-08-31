/*
 * The pane selector: one bench, several views of it.
 *
 * Numbers and controls stay put while the large area switches, so a change of
 * view never moves the thing you were about to press.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_TABS_MAX 6

typedef struct {
    const char *label[UI_TABS_MAX];
    gfx_rect_t  rect[UI_TABS_MAX];
    int         count;
    int         selected;
    int         pressed;    /**< index, or -1 */
    uint8_t     press_id;
} ui_tabs_t;

void ui_tabs_init(ui_tabs_t *t, const char *const *labels, int count,
                  gfx_rect_t row);
/** Returns true when the selection changed. */
bool ui_tabs_event(ui_tabs_t *t, const touch_event_t *evt);
void ui_tabs_render(const ui_tabs_t *t, gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
