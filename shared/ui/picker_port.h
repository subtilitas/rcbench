/**
 * @file
 * @brief The board view of OUTPUTS on a programming port: what PROG PORT n in
 * the protocol list makes of the buttons.  picker_screen.c holds the screen;
 * this holds the rules for a port's pin, as the panel can judge them before the IOmcu
 * does (prog_port.h), and the asks a tap makes.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "out_bind.h"
#include "picker_screen.h"

/**
 * @brief Catalogue entry @p index as a button with port @p port picked: TICKED
 * for the pin the port names; HELD for another port's pin, a protocol's,
 * one SENSORS or SUPPLY's PD MINI holds, and for a SERVO PWM port one on the PWM
 * slice of a bound PWM output or on the compare register of another
 * SERVO PWM port; LOCKED while @p locked or the ports are not read; FREE
 * otherwise.  A reserved pin is the caller's.  @p holder as
 * picker_screen_pin() fills it.
 * @param b The binding.
 * @param index The catalogue entry.
 * @param port The port picked.
 * @param locked Whether the port's pin takes no change now.
 * @param holder Takes who holds the pin.
 * @param cap The size of @p holder in bytes.
 * @return The button's state.
 */
picker_pin_t picker_port_pin(const outbind_t *b, uint8_t index, uint8_t port,
                             bool locked, char *holder, size_t cap);

/**
 * @brief A tap on catalogue entry @p index drawn @p st: a FREE pin asks for it,
 *  the TICKED pin asks for none.  Whether an ask went out.
 *
 * @param b the binding
 * @param index the catalogue entry
 * @param port the port picked
 * @param st how the pin is drawn
 * @return true when an ask went out
 */
bool picker_port_tap(const outbind_t *b, uint8_t index, uint8_t port,
                     picker_pin_t st);

/**
 * @brief Whether a tap on @p index was refused for a PWM rule, which the reason
 *  line then names.
 *
 * @param b the binding
 * @param index the catalogue entry
 * @param port the port picked
 * @return true when it was
 */
bool picker_port_clashes(const outbind_t *b, uint8_t index, uint8_t port);

/**
 * @brief The amber line with port @p port picked: the ports not read, the last
 * ask for the port (port_view_ask_line()), or why the PWM rule refused
 * the pin at @p refused (an index, -1 for none).  False with none.
 *
 * @param b the binding
 * @param port the port picked
 * @param refused the pin refused, an index; -1 for none
 * @param buf receives the line
 * @param cap its size, bytes
 * @return true when there is a line
 */
bool picker_port_reason(const outbind_t *b, uint8_t port, int refused,
                        char *buf, size_t cap);

/**
 * @brief Row @p k of the PINS table with a port picked: the port's one pin, the
 *  TICKED button, with no channel (-1).  False past it.
 *
 * @param b the binding
 * @param k the row, from 0
 * @param out receives the row
 * @return true when there is one
 */
bool picker_port_row(const outbind_t *b, uint8_t k, picker_row_t *out);

/**
 * @brief Whether catalogue entry @p index is the pin of port @p port (-1 for
 *  none picked): listed under PINS, not IN USE.
 *
 * @param b the binding
 * @param index the catalogue entry
 * @param port the port picked; -1 for none
 * @return true when it is
 */
bool picker_port_own_pin(const outbind_t *b, uint8_t index, int port);

