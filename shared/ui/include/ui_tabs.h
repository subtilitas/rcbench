/*
 * The pane selector: one bench, several views of it.
 *
 * Readings and controls stay in place while the pane switches, so a change of
 * view never moves a control that is about to be pressed.
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
/**
 * Abandon a press in progress.  A screen calls it when told its record of
 * the glass is stale: a latched press owns a track id the controller reuses,
 * so a later contact that began elsewhere and lifts over the tab would
 * otherwise be taken for the missing release and switch the pane.
 */
void ui_tabs_cancel(ui_tabs_t *t);
void ui_tabs_render(const ui_tabs_t *t, gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
