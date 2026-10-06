/*
 * The SUPPLY link page at the coprocessor: a PD mini on a PIO UART, its
 * wiring, what it is to do and what it last said.  The registers are in
 * link_pages.h (LINK_SP_*).
 *
 * The wiring is refused while the output is on, and on pins an output or
 * the board already holds; the pins it takes are reserved from the outputs
 * for as long as it holds them (supply_page_pins()).  The output switches
 * on only with a live heartbeat from the panel and goes off when that
 * stops, so a panel that dies leaves no supply running.
 *
 * Host-tested; the coprocessor is wiring: it runs the PIO UART, feeds the
 * driver and calls supply_page_step() every pass.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "outputs.h"
#include "pdmini.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The module's UART Baudrate settings, 0 to 6. */
#define SUPPLY_BAUD_COUNT 7u
uint32_t supply_page_baud(uint16_t setting);

typedef struct {
    uint16_t regs[LINK_SP_COUNT];
} supply_page_t;

void supply_page_init(supply_page_t *p);

/**
 * A write, validated whole before any of it is stored.  Refused: off the
 * page (BAD_RANGE); a read-only register (READ_ONLY); a wiring change while
 * the output is asked on or may be on, a pin past the bank, reserved, bound to an output or
 * the other pin, a baud setting past 6, an ON without the supply enabled
 * or without its set points in the same frame,
 * a set point above the module's range (BAD_VALUE); an ON without a live
 * heartbeat (NOT_ARMED).
 */
uint8_t supply_page_write(supply_page_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in, const outputs_t *o,
                          bool beat_alive);

/** LINK_NACK_BAD_VALUE for an OUTPUTS page (@p slots, LINK_OS_COUNT
 *  registers) that binds a slot to a pin the supply holds; 0 otherwise.
 *  Checked before the page is stored, so no binding is kept that the bank
 *  would leave unbound -- and that a restart, with the supply not yet
 *  wired, would drive on the module's pin. */
uint8_t supply_page_slots_check(const supply_page_t *p, const uint16_t *slots);

void supply_page_read(const supply_page_t *p, uint8_t off, uint8_t n,
                      uint16_t *out);

/** Whether the page drives a supply, and on which pins. */
bool    supply_page_enabled(const supply_page_t *p);
uint8_t supply_page_tx(const supply_page_t *p);
uint8_t supply_page_rx(const supply_page_t *p);

/** The pins it holds, as a reservation mask; 0 while not enabled. */
uint64_t supply_page_pins(const supply_page_t *p);

/**
 * One pass: the output off without a heartbeat, what the driver is to do,
 * and what it last said into the read-only registers.  @p drv may be NULL
 * while no supply is wired.
 */
void supply_page_step(supply_page_t *p, bool beat_alive, pdmini_t *drv);

#ifdef __cplusplus
}
#endif
