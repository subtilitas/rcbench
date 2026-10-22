/**
 * @file
 * @brief The balance screen: sensor placement guides and the correction readout.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ui_keypad.h"
#include "ui_screen.h"

typedef enum {
    BALANCE_PANE_MEASURE = 0,
    BALANCE_PANE_RIG,
    BALANCE_PANE_AIRCRAFT,
    BALANCE_PANE_COUNT,
} balance_pane_t;

typedef enum { ROTOR_PROP = 0, ROTOR_EDF, ROTOR_COUNT } balance_rotor_t;

/** Drop the cached chrome, so the next frame repaints it. */
void balance_invalidate(void);

const ui_screen_t *balance_screen(void);

/** How many blades the rotor has, 2 to 6. */
int  balance_screen_blades(void);
/** Propeller or ducted fan. */
int  balance_screen_rotor(void);

/**
 * @brief The keypad a tap on the blade count opens over both cards, for
 *        tests and the renderer.
 * @return The keypad, open or closed.
 */
const ui_keypad_t *balance_screen_keypad(void);

/**
 * @brief Where the blade count is drawn and tapped, in the screen's
 *        coordinates: between BLADES and the "-" key.
 * @return The value's rectangle.
 */
gfx_rect_t balance_screen_blades_rect(void);

/** The MODELLED mark the BALANCE pane shows beside its readings, or NULL on
 *  the other panes: the angle and both masses are fixed values. */
const char *balance_screen_model_mark(void);
/** The words beside the mark ("Not measured."), or NULL where it is not
 *  shown. */
const char *balance_screen_model_note(void);

/**
 * The two blades a correction at @p angle (degrees from the index mark) falls
 * between, on a rotor of @p blades blades.  Both are 1..blades and wrap, so a
 * correction past the last blade reads "between N and 1".
 */
void balance_nearest_blades(float angle, int blades, int *lo, int *hi);
