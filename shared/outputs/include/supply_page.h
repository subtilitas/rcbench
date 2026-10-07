/*
 * The SUPPLY link page at the coprocessor: a PD mini on a PIO UART, its
 * wiring, what it is to do and what it last said.  The registers are in
 * link_pages.h (LINK_SP_*).
 *
 * The wiring is refused while the output is on, and on pins an output or
 * the board already holds; the pins it takes are reserved from the outputs
 * for as long as it holds them (supply_page_pins()).  A change while a
 * module has answered waits for a state read sent after it
 * (pdmini_check_off()): taken if that read shows the output off, refused
 * if it shows it on or fails.  FLAGS bit 8 says it waits, bit 9 that the
 * last one was refused.  The output switches
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

/** The module's UART Baudrate settings, 0 to 6, and 7 to find it. */
#define SUPPLY_BAUD_COUNT 7u
#define SUPPLY_BAUD_AUTO  7u
uint32_t supply_page_baud(uint16_t setting);

/** Where a wiring change that waits for a state read is. */
typedef enum {
    SUPPLY_WIRE_IDLE = 0,  /**< none waits                              */
    SUPPLY_WIRE_WAIT,      /**< waits for the driver's state read        */
    SUPPLY_WIRE_READY,     /**< the read showed off: to be written now   */
} supply_wire_t;

typedef struct {
    uint16_t regs[LINK_SP_COUNT];
    bool     commanded;   /**< set points written since init          */
    bool     answered;    /**< the driver's module has answered once  */
    uint8_t  wire;        /**< supply_wire_t                          */
    bool     wire_ask;    /**< the state read not yet asked of the driver */
    bool     wire_refused; /**< the last change waited and was refused */
    uint16_t wire_next[4]; /**< the change waiting, ENABLE to BAUD    */
    bool     reset_owed;  /**< RESET written, not yet passed on       */
    uint8_t  scan;        /**< the rate being tried under AUTO, 0..6   */
    uint16_t scan_seen;   /**< the driver's who_failed when last looked */
} supply_page_t;

void supply_page_init(supply_page_t *p);

/**
 * The rate the UART is to run at now, 0 to 6.  The setting itself, or under
 * AUTO the one being tried: the next after every WHO_AM_I @p drv reports
 * without a valid answer, until a module has answered once, from when the
 * rate holds.  Publishes it in BAUD_FOUND -- 7 while AUTO has found none.
 * @p drv NULL leaves the scan where it is.
 */
uint8_t supply_page_rate(supply_page_t *p, const pdmini_t *drv);

/** The baud the UART is to be opened at now. */
uint32_t supply_page_uart_baud(const supply_page_t *p);

/**
 * A write, validated whole before any of it is stored.  Refused: off the
 * page (BAD_RANGE); a read-only register (READ_ONLY); a wiring change while
 * the output is asked on or may be on, a pin past the bank, reserved, bound to an output or
 * the other pin, a baud setting past 7 (AUTO), an ON without the supply enabled
 * or without its set points in the same frame,
 * a set point above the module's range, an ON while a wiring change
 * waits, a wiring change that waits in the same frame as OUTPUT to SET_MA
 * (BAD_VALUE); an ON without a live heartbeat (NOT_ARMED).
 *
 * A wiring change that passes, while the driver's module has answered
 * (supply_page_follow()), is acknowledged and not yet taken: it waits
 * (SUPPLY_WIRE_WAIT, FLAGS bit 8) for a state read the next
 * supply_page_step() asks of the driver.  Another change replaces it and
 * is read for again; a write of the wiring in force drops it.
 */
uint8_t supply_page_write(supply_page_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in, const outputs_t *o,
                          bool beat_alive);

/**
 * What the driver says now, straight before a write is judged: FLAGS bit 6
 * (the output on or maybe on) and whether its module has answered.  @p drv
 * NULL for no driver: nothing answered, nothing on.  supply_page_step()
 * does the same every pass.
 */
void supply_page_follow(supply_page_t *p, const pdmini_t *drv);

/**
 * The wiring change whose state read showed the output off, into @p regs
 * (ENABLE to BAUD): true while one is SUPPLY_WIRE_READY.  The caller writes
 * it with supply_page_wire_write() in the same pass.
 */
bool supply_page_wire_ready(const supply_page_t *p, uint16_t *regs);

/**
 * The ready wiring written: judged whole as a link write is, against the
 * driver's state as supply_page_follow() last took it, without waiting
 * again.  0, or the NACK reason, which refuses it (FLAGS bit 9).
 */
uint8_t supply_page_wire_write(supply_page_t *p, const outputs_t *o);

/** The ready wiring could not be put in force after it was written (no
 *  UART for its pins): refused, FLAGS bit 9, as a refused read is. */
void supply_page_wire_refuse(supply_page_t *p);

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
 * while no supply is wired.  A wiring change waiting asks the driver for
 * its state read and follows the answer: off makes it SUPPLY_WIRE_READY,
 * anything else refuses it (FLAGS bit 9).  With no driver it is ready:
 * nothing is attached to the module to switch it off.
 */
void supply_page_step(supply_page_t *p, bool beat_alive, pdmini_t *drv);

#ifdef __cplusplus
}
#endif
