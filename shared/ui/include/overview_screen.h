/**
 * @file
 * @brief The feature menu: eight tiles in 4 x 2, each in a fixed place by kind.
 *
 * Row 1: the workbenches MOTOR & ESC (electronic speed controller), SERVO
 * and SUPPLY, which drive outputs and can ARM.  Row 2: the instruments
 * RECEIVER, BATTERY and BALANCE, which only read.  The last column: LOGS
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

/** @brief Marks the whole screen to be drawn again. */
void overview_invalidate(void);
/**
 * @brief Gives the screen's table.
 *
 * @return the table
 */
const ui_screen_t *overview_screen(void);

/**
 * @brief Whether SUPPLY drives the PD mini; the tile is marked MODELLED while it
 *  does not.
 *
 * @param real true when SUPPLY drives the PD mini
 */
void overview_screen_set_supply_real(bool real);

/**
 * @brief The grid's columns: 4.
 *
 * @return the columns
 */
int overview_screen_columns(void);

/**
 * @brief The screen the tile in slot @p slot opens, row by row from the top
 *  left, or SCREEN_COUNT for an empty slot or one past the grid.
 *
 * @param slot the slot
 * @return the screen
 */
ui_screen_id_t overview_screen_tile(int slot);

/**
 * @brief The tile's name and its line in the language in force, NULL for an
 *  empty slot.
 *
 * @param slot the slot
 * @return the name
 */
const char *overview_screen_tile_name(int slot);
/**
 * @brief Gives the line under a tile's name in the language in force.
 *
 * @param slot the slot
 * @return the line; NULL for an empty slot
 */
const char *overview_screen_tile_line(int slot);

/**
 * @brief The slot's rectangle in the screen's own coordinates.
 *
 * @param slot the slot
 * @return the rectangle
 */
gfx_rect_t overview_screen_tile_rect(int slot);

#ifdef __cplusplus
}
#endif
