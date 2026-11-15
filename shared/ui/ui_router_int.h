/**
 * @file
 * @brief The router's own parts in files of their own (ui_router.c).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"

/**
 * @brief Draw the alert band across the bottom of the panel
 *        (ui_router_alert.c).
 * @param c        The canvas, the panel's.
 * @param text     The alert, on one row centered or on two from the top.
 * @param closable A tap closes it: the "x" at its right end shows.
 */
void ui_router_alert_draw(gfx_canvas_t *c, const char *text, bool closable);
