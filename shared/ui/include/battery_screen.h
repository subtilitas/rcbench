/*
 * The pack: what each cell is doing relative to the others.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ui_screen.h"

#define BATTERY_CELLS_MAX 14

typedef struct {
    int   cells;                          /**< 1 to 14                    */
    float volts[BATTERY_CELLS_MAX];       /**< per cell                   */
    float milliohms[BATTERY_CELLS_MAX];   /**< per cell, under load       */
    float capacity_mah;
    float drawn_mah;
    bool  valid;                          /**< a monitor answered         */
} battery_state_t;

const ui_screen_t *battery_screen(void);

/** What the balance lead says.  NULL, or valid false, means nothing is on. */
void battery_screen_set(const battery_state_t *b);

/** The widest gap between any two cells, in millivolts. */
float battery_screen_spread_mv(void);
