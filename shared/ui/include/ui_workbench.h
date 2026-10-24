/**
 * @file
 * @brief A workbench's task strip: one device, its tasks as tabs under the
 *        band.
 *
 * A workbench is a home tile that drives an output (SERVO today; MOTOR &
 * ESC and SUPPLY reuse it).  Its tasks are LIVE, TEST, PROGRAM and
 * SETTINGS, always in that order; a task the device lacks is not drawn.
 * The strip is the existing ui_tabs control, one row across the screen's
 * full width, so the output card on the right stays where it is on every
 * task.
 *
 * Rules the strip carries:
 *   - A tile opens LIVE (ui_wb_strip_open()).
 *   - A tab switch is no navigation level: the home tag never climbs from
 *     one task to another, and the screen keeps each task's own scroll and
 *     selection for the session.
 *   - Leaving the screen is the screen's: a workbench switches to SAFE in
 *     its leave(), whichever task is shown.
 *
 * Pure layout and touch logic over ui_tabs; no state outside the struct.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "touch_types.h"
#include "ui_tabs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A workbench's tasks, in the order the strip draws them. */
typedef enum {
    UI_WB_LIVE = 0,
    UI_WB_TEST,
    UI_WB_PROGRAM,
    UI_WB_SETTINGS,
    UI_WB_TASKS
} ui_wb_task_t;

/** The task's bit in the mask ui_wb_strip_init() takes. */
#define UI_WB_HAS(task) (1u << (unsigned)(task))

/*
 * Geometry in the screen's own coordinates (the body under the band).  A
 * tab is 112px: PROGRAMMIEREN, the longest label, is 13 cells of 8px.
 * Four tabs take 460px, which the 488px page area of a workbench holds.
 * The page area and the output card start at UI_WB_PAGE_Y.
 */
#define UI_WB_STRIP_X 9
#define UI_WB_STRIP_Y 6
#define UI_WB_STRIP_H 24
#define UI_WB_TAB_W   112
#define UI_WB_TAB_GAP 4
#define UI_WB_PAGE_Y  38

typedef struct {
    ui_tabs_t tabs;
    /** The task each tab shows, tab by tab. */
    uint8_t   task[UI_WB_TASKS];
} ui_wb_strip_t;

/**
 * The strip for the tasks in @p tasks (UI_WB_HAS() bits), from (@p x,
 * @p y), LIVE selected.  LIVE is always there: a mask without it gets it.
 * Bits above UI_WB_SETTINGS are ignored.
 */
void ui_wb_strip_init(ui_wb_strip_t *w, unsigned tasks, int16_t x, int16_t y);

/** The task shown now. */
ui_wb_task_t ui_wb_strip_task(const ui_wb_strip_t *w);

/** Whether the strip has @p task. */
bool ui_wb_strip_has(const ui_wb_strip_t *w, ui_wb_task_t task);

/**
 * Show @p task.  Returns whether the task shown changed; a task the strip
 * lacks changes nothing and returns false.  A press under way is dropped.
 */
bool ui_wb_strip_select(ui_wb_strip_t *w, ui_wb_task_t task);

/** What a tile does: LIVE, and no press under way. */
void ui_wb_strip_open(ui_wb_strip_t *w);

/**
 * A touch event.  Returns true when it switched the task: the release of a
 * press that began on another tab, as a tap (touch_event_is_tap_up()), on
 * the same tab.
 */
bool ui_wb_strip_event(ui_wb_strip_t *w, const touch_event_t *evt);

/** Whether a press on a tab is under way (the strip owns that contact). */
bool ui_wb_strip_pressed(const ui_wb_strip_t *w);

/** Drop a press under way (ui_tabs_cancel()). */
void ui_wb_strip_cancel(ui_wb_strip_t *w);

/**
 * @brief The strip's row.
 * @param w the strip
 * @return the tabs, without the trough's 3px around them
 */
gfx_rect_t ui_wb_strip_rect(const ui_wb_strip_t *w);

/** The tab of @p task, or an empty rectangle when the strip lacks it. */
gfx_rect_t ui_wb_strip_tab_rect(const ui_wb_strip_t *w, ui_wb_task_t task);

void ui_wb_strip_render(const ui_wb_strip_t *w, gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
