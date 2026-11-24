/**
 * @file
 * @brief The router's own parts in files of their own (ui_router.c).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "ui_screen.h"

/**
 * @brief Draw the alert band across the bottom of the panel
 *        (ui_router_alert.c).
 * @param c        The canvas, the panel's.
 * @param text     The alert, on one row centered or on two from the top.
 * @param closable A tap closes it: the "x" at its right end shows.
 */
void ui_router_alert_draw(gfx_canvas_t *c, const char *text, bool closable);

/**
 * @brief Keep @p text as the one notice that waits behind an alert
 *        (ui_router_alert.c): a later one takes its place and starts its
 *        own UI_NOTICE_WAIT_S.
 * @param text The notice, not NULL.
 */
void ui_router_wait_set(const char *text);

/** @brief Drop the waiting notice: a STOP, a switch to SAFE, a fault bit
 *         or a change of screen. */
void ui_router_wait_drop(void);

/**
 * @brief Hand over the waiting notice to show it; none waits after.
 * @return The notice, valid until the next ui_router_wait_set(), or NULL
 *         when none waits.
 */
const char *ui_router_wait_take(void);

/**
 * @brief Run the waiting notice's time: after UI_NOTICE_WAIT_S it is
 *        dropped.  The frame it arrives in does not count, as for an
 *        alert, so a notice queued in an alert's first frame outlasts
 *        that alert's UI_ALERT_SHOW_S.
 * @param dt_s Seconds since the last tick.
 */
void ui_router_wait_tick(float dt_s);

/**
 * @brief Whether @p opener opens screen @p id through one of its controls,
 *        so the home tag of @p id returns to it (ui_router_screens.c).
 * @param opener The screen that may open it.
 * @param id     The screen opened.
 * @return True when a row of the openers names both.
 */
bool ui_router_opens(ui_screen_id_t opener, ui_screen_id_t id);

/**
 * @brief The table of screen @p id (ui_router_screens.c).
 * @param id The screen; one out of range gives the overview's.
 * @return Its table.
 */
const ui_screen_t *ui_router_screen(ui_screen_id_t id);

/** @brief Every screen's cached chrome repainted into every framebuffer
 *         on its next frame (ui_router_screens.c). */
void ui_router_screens_invalidate(void);
