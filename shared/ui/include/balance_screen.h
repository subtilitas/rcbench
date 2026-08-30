/*
 * Balancing: where to put the two sensors, and later what they say.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ui_screen.h"

typedef enum {
    BALANCE_PANE_MEASURE = 0,
    BALANCE_PANE_RIG,
    BALANCE_PANE_AIRCRAFT,
    BALANCE_PANE_COUNT,
} balance_pane_t;

typedef enum { ROTOR_PROP = 0, ROTOR_EDF, ROTOR_COUNT } balance_rotor_t;

const ui_screen_t *balance_screen(void);

/** How many blades the rotor has, 2 to 6. */
int  balance_screen_blades(void);
/** Propeller or ducted fan. */
int  balance_screen_rotor(void);
/** Where the correction goes, in degrees from the index mark. */
float balance_screen_angle(void);
