/*
 * Balancing: where to put the two sensors, and later what they say.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ui_screen.h"

typedef enum {
    BALANCE_PANE_RIG = 0,
    BALANCE_PANE_AIRCRAFT,
    BALANCE_PANE_COUNT,
} balance_pane_t;

const ui_screen_t *balance_screen(void);
