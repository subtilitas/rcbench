/*
 * The feature menu: eight tiles in 4 x 2, each in a fixed place by kind.
 * Row 1: the workbenches MOTOR & ESC (electronic speed controller), SERVO
 * and SUPPLY, which drive outputs and can ARM.  Row 2: the instruments
 * ANALYSER, BATTERY and BALANCE, which only read.  The last column: LOGS
 * over SETUP.  A tile carries a MODELLED badge when its hardware is not
 * fitted.  Programming is a task of a workbench (MOTOR & ESC > PROGRAM,
 * SERVO > PROGRAM), not a tile.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

void overview_invalidate(void);
const ui_screen_t *overview_screen(void);

/** Whether SUPPLY drives the PD mini; the tile is marked MODELLED while it
 *  does not. */
void overview_screen_set_supply_real(bool real);

/** The grid's columns: 4. */
int overview_screen_columns(void);

/** The screen the tile in slot @p slot opens, row by row from the top
 *  left, or SCREEN_COUNT for an empty slot or one past the grid. */
ui_screen_id_t overview_screen_tile(int slot);

/** The tile's name and its line in the language in force, NULL for an
 *  empty slot. */
const char *overview_screen_tile_name(int slot);
const char *overview_screen_tile_line(int slot);

/** The slot's rectangle in the screen's own coordinates. */
gfx_rect_t overview_screen_tile_rect(int slot);

#ifdef __cplusplus
}
#endif
