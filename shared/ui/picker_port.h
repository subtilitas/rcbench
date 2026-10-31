/*
 * PICK A PIN on a programming port: what PROG PORT n in the protocol list
 * makes of the buttons.  picker_screen.c holds the screen; this holds the
 * rules for a port's pin, as the panel can judge them before the IOmcu
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
 * Catalogue entry @p index as a button with port @p port picked: TICKED
 * for the pin the port names; HELD for another port's pin, a protocol's,
 * one SENSORS or SUPPLY's PD MINI holds, and for a SERVO PWM port one on the PWM
 * slice of a bound PWM output or on the compare register of another
 * SERVO PWM port; LOCKED while @p locked or the ports are not read; FREE
 * otherwise.  A reserved pin is the caller's.  @p holder as
 * picker_screen_pin() fills it.
 */
picker_pin_t picker_port_pin(const outbind_t *b, uint8_t index, uint8_t port,
                             bool locked, char *holder, size_t cap);

/** A tap on catalogue entry @p index drawn @p st: a FREE pin asks for it,
 *  the TICKED pin asks for none.  Whether an ask went out. */
bool picker_port_tap(const outbind_t *b, uint8_t index, uint8_t port,
                     picker_pin_t st);

/** Whether a tap on @p index was refused for a PWM rule, which the reason
 *  line then names. */
bool picker_port_clashes(const outbind_t *b, uint8_t index, uint8_t port);

/**
 * The amber line with port @p port picked: the ports not read, the last
 * ask for the port (port_view_ask_line()), or why the PWM rule refused
 * the pin at @p refused (an index, -1 for none).  False with none.
 */
bool picker_port_reason(const outbind_t *b, uint8_t port, int refused,
                        char *buf, size_t cap);

/** Row @p k of the PINS table with a port picked: the port's one pin, the
 *  TICKED button, with no channel (-1).  False past it. */
bool picker_port_row(const outbind_t *b, uint8_t k, picker_row_t *out);

/** Whether catalogue entry @p index is the pin of port @p port (-1 for
 *  none picked): listed under PINS, not IN USE. */
bool picker_port_own_pin(const outbind_t *b, uint8_t index, int port);

/** The count line's text with port @p port picked: its function. */
void picker_port_count(uint8_t port, char *buf, size_t cap);
