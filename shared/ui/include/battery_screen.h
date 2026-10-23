/**
 * @file
 * @brief The pack: what each cell is doing relative to the others.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "ui_screen.h"

/** @brief The most cells a pack is drawn with. */
#define BATTERY_CELLS_MAX 14

/** @brief What the balance lead says of a pack. */
typedef struct {
    int   cells;                          /**< 1 to 14                    */
    float volts[BATTERY_CELLS_MAX];       /**< per cell                   */
    float milliohms[BATTERY_CELLS_MAX];   /**< per cell, under load       */
    float capacity_mah;                   /**< the pack's capacity        */
    float drawn_mah;                      /**< used so far                */
    bool  valid;                          /**< a monitor answered         */
} battery_state_t;

/** Drop the cached chrome, so the next frame repaints it. */
void battery_invalidate(void);

/**
 * @brief The BATTERY screen.
 * @return The screen.
 */
const ui_screen_t *battery_screen(void);

/**
 * @brief What the balance lead says.  NULL, or valid false, means nothing
 *        is on.
 * @param b The pack, copied; NULL clears it.
 */
void battery_screen_set(const battery_state_t *b);

/**
 * @brief The widest gap between any two cells.
 * @return Millivolts; 0 with no pack.
 */
float battery_screen_spread_mv(void);

/**
 * @brief The face of the cells row under the plot for a pack of @p cells:
 *        12 x 24 while a voltage ("3.81") fits its cell's slot, else the
 *        10 x 20 face, else 8 x 16.
 * @param cells The pack's cells; under 1 counts as 1.
 * @return The face.
 */
const gfx_font_t *battery_screen_cells_font(int cells);

/**
 * @brief The lines @p text takes in the right card's column: the verdict's
 *        reason in the 12 x 24 face (the card shows two lines), or a part
 *        of the note in the 10 x 20 face.
 * @param text The text, as the screen shows it.
 * @param note True for the note's face, false for the reason's.
 * @return The lines, 0 for NULL or an empty text.
 */
int battery_screen_text_lines(const char *text, bool note);
