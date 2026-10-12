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

/** The longest an OFF waits for a programming port's frame to end, in ms.
 *  A frame of the port and its answer are shorter; how long a servo needs
 *  after a frame to store a register is not measured. */
#define SUPPLY_PAGE_OFF_WAIT_MS 12u

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
    bool     held;        /**< a programming port holds the command   */
    bool     frame_out;   /**< the port has a frame on the wire        */
    bool     was_on;      /**< OUTPUT was 1 at the last step           */
    bool     off_wait;    /**< an OFF waits for the frame's end        */
    uint32_t now_ms;      /**< the clock of supply_page_port_frame()   */
    uint32_t off_t0;      /**< when the waiting OFF was asked          */
} supply_page_t;

/** The supply as a programming port sees it (kst_port.h). */
typedef struct {
    bool     online;    /**< wired, a PD mini answers, no wiring change
                             waits */
    bool     asked_on;  /**< OUTPUT is 1 */
    bool     on;        /**< asked on and read on, at a voltage no higher
                             than asked, with nothing stuck */
    bool     off;       /**< read off, and not maybe on */
    uint16_t set_mv;    /**< SET_MV and SET_MA as asked */
    uint16_t set_ma;
    uint16_t vin_mv;    /**< the PD mini's input, 0 while not read */
} supply_port_view_t;

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

/** The page as of the last supply_page_step(), for a programming port. */
void supply_page_port_view(const supply_page_t *p, supply_port_view_t *out);

/**
 * A programming port's request, in place of a link write of OUTPUT to
 * SET_MA.  Off is always taken and leaves the set points.  On is refused
 * (false, nothing changed) without a live heartbeat, while the supply is
 * not wired or a wiring change waits, for set points outside the module's
 * range, and for a voltage above SET_MV while OUTPUT is 1: the voltage of
 * an output that is on is not raised here.
 */
bool supply_page_port_set(supply_page_t *p, bool on, uint16_t mv,
                          uint16_t ma, bool beat_alive);

/**
 * Whether a programming port holds the command registers (FLAGS bit 11,
 * LINK_SP_PORT).  While it does, supply_page_write() takes an OFF, which
 * leaves SET_MV and SET_MA as they are whatever the write names, and
 * refuses every other write of OUTPUT to SET_MA that changes one of the
 * three with LINK_NACK_BAD_VALUE: the panel can switch the supply off, and
 * cannot switch it on or move a set point under the servo.
 */
void supply_page_hold(supply_page_t *p, bool held);

/**
 * Whether a programming port has a frame on the wire, and the clock, once
 * a pass before supply_page_step().  An OFF taken while one is -- written
 * by the panel or forced by a lost heartbeat -- reaches the driver when
 * the frame has ended, SUPPLY_PAGE_OFF_WAIT_MS after it was asked at the
 * latest.  OUTPUT reads 0 from the moment it is asked.
 */
void supply_page_port_frame(supply_page_t *p, bool frame_out,
                            uint32_t now_ms);

#ifdef __cplusplus
}
#endif
