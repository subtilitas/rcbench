/**
 * @file
 * @brief The board the IOmcu that answered last reports (IDENTITY register
 *        5), for the screens that show a board with soldered pins
 *        differently.
 *
 * Written by the task that polls the link, read by the screens: one 16-bit
 * value, held atomically.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_UI_BOARD_H
#define RCBENCH_UI_BOARD_H

#include <stdint.h>

#include "out_bind.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Record the board an IOmcu reported.
 *
 * @param board  An outbind_board_id_t value, OUTBIND_BOARD_UNKNOWN for none.
 */
void ui_board_set(uint16_t board);

/**
 * @brief The board last recorded, OUTBIND_BOARD_UNKNOWN before any.
 *
 * @return The board's identity.
 */
uint16_t ui_board(void);

/**
 * @brief The roles of the board last recorded: its soldered pins.
 *
 * @return outbind_roles() of ui_board(): NULL for a board whose pins are
 *         chosen and for an unknown one.
 */
const outbind_roles_t *ui_board_roles(void);

/**
 * @brief The pin that switches the programming ports' own fixed supply on
 *        the recorded board (the rbmod carrier's 5V buck: GPIO 21).  The
 *        KST and Hitec programmers show its 5V as fixed there and offer no
 *        voltage or current.
 *
 * @return The GPIO; -1 on a board without it.
 */
int ui_board_prog_5v_pin(void);

/**
 * @brief The rail the servo tester's outputs are on, on a board that
 *        solders an INA3221 on its servo rails (the rbmod carrier):
 *        settings_sensors_set_tester_rail()'s argument.
 *
 * @param pins  The GPIOs bound as SERVO PWM in the binding the tester
 *              drives, bit g for GPIO g.
 * @return LINK_SM_WATCH_RAIL_A or LINK_SM_WATCH_RAIL_B: the rail of the
 *         channel that feeds the lowest of @p pins; LINK_SM_WATCH_NONE on
 *         another board, with no pin, or with that pin on no rail.
 */
uint16_t ui_board_tester_rail(uint64_t pins);

/** @brief Called for each setting whose value ui_board_apply_pins() moved. */
typedef void (*ui_board_changed_fn)(setting_id_t id);

/**
 * @brief Hold the interface pin settings at the pins the recorded board
 *        solders, or let them go on a board whose pins are chosen.
 *
 * Sensor SDA and SCL, Tap pin, PD mini TX and RX: settings_fix() at the
 * roles' pins on a board with roles, settings_unfix() on any other.  On a
 * board that solders an INA3221, SETUP > SENSORS' FITTED row of its part
 * at ON and its three WATCHES rows at the board's rails; every FITTED and
 * WATCHES row let go on any other board.  Does
 * nothing while the board is the one applied last.  Once a frame, on the
 * task that owns the settings.
 *
 * @param changed  Called for each setting whose value moved, or NULL.
 * @return true when the board differed from the one applied last.
 */
bool ui_board_apply_pins(ui_board_changed_fn changed);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_UI_BOARD_H */
