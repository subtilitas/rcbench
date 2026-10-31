/**
 * @file
 * @brief The strip of the LOGS tile: one tile, three screens.
 *
 * LOGS, FILES and WI-FI stay screens of their own and each draws this strip
 * at the top of its first level.  A tab tapped is a change of screen
 * (ui_router_goto()): the screen left runs its leave(), so leaving WI-FI by
 * a tab switches the radio off and drops a waiting image, as its home tag
 * does.  The radio is up only while WI-FI shows; the strip has no way to
 * keep it on.  A level under the first one of a screen (a file's views, a
 * keyboard, a box, a question, the text viewer and its editor, the network
 * list) does not draw the strip and takes no tab, so such a level is left by
 * its own controls and the home tag first.
 *
 * The labels are the screens' titles, in every language.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "touch_types.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The strip's row in the body: 1002 x 40px at (11, 5), three tabs of
 *        331px, the trough 3px round it, y 2 to 48.  It spans the screen, so
 *        it does not read as one row with LOGS' folder tabs of 110px under it.
 */
#define LOGS_STRIP_X 11
#define LOGS_STRIP_Y 5    /**< Its top. */
#define LOGS_STRIP_W 1002 /**< Its width. */
#define LOGS_STRIP_H 40   /**< A tab's height. */
/** @brief The trough's foot: a screen's first row starts below it. */
#define LOGS_STRIP_FOOT (LOGS_STRIP_Y + LOGS_STRIP_H + 3)

/**
 * @brief Draw the strip with the tab of @p here selected.
 * @param c    The canvas of the screen's body.
 * @param here SCREEN_LOGS, SCREEN_FILES or SCREEN_WIFI.
 */
void logs_strip_render(gfx_canvas_t *c, ui_screen_id_t here);

/**
 * @brief One touch event of the screen @p here, on its first level.  A tap on
 *        another tab than @p here's opens that screen.
 *
 * A DOWN elsewhere is not the strip's; the event the strip takes is not the
 * screen's.  While a contact holds a tab, a DOWN of any other contact presses
 * nothing, on the strip or on the screen.  The caller draws again after a
 * true: the tab's look changed.
 *
 * @param e    The event, in the body's coordinates.
 * @param here SCREEN_LOGS, SCREEN_FILES or SCREEN_WIFI.
 * @return True when the event belongs to a press on the strip.
 */
bool logs_strip_event(const touch_event_t *e, ui_screen_id_t here);

/**
 * @brief Drop a press on the strip: the touch stream lost an event, the
 *        screen changed, or the level with the strip went away under it.
 */
void logs_strip_cancel(void);

#ifdef __cplusplus
}
#endif
