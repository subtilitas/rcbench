/**
 * @file prog_link.h
 * @brief The panel's side of the programming ports' page (0x33).
 *
 * The SERVO screen's tester drives one of two things: the standard servo
 * outputs, or a programming port.  Which one is a register of the
 * coprocessor (LINK_PG_TARGET), and whether the port renders a pulse is
 * another (its bit in LINK_PG_PWM).  This module holds what the page last
 * read, what the screen wants, and names the one write that brings the page
 * to it.
 *
 * Rules:
 *
 *   - Nothing is written before the page has been read, and a write that is
 *     not acknowledged makes the page unknown until the next read.
 *   - The target changes only while the bench is quiet: the bank disarmed
 *     and no surface holding a position this panel wrote.  The pulse is
 *     switched on under the same condition and switched off at any time.
 *   - A port the page does not have is not a target: the standard outputs
 *     stay the target.
 *   - The pulse bit of a port is set only while that port is the target.
 *
 * A coprocessor older than protocol 4.13 has no page: nothing is read or
 * written and the target is the standard outputs.
 *
 * Pure logic, no I/O.  Nothing here has run on hardware.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PROG_LINK_H
#define RCBENCH_PROG_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The page is read this often, in ms, and at once while it is unknown. */
#define PROG_LINK_POLL_MS 500u
/** What prog_link_pin() returns for a port without a pin. */
#define PROG_LINK_NO_PIN 0xFFu
/** The registers prog_link_cfg() fills: SLEW, MIN_US, MAX_US of a port. */
#define PROG_LINK_CFG_REGS 3u

typedef struct {
    bool     page;                 /**< the coprocessor has the page       */
    bool     known;                /**< regs holds a read of it            */
    bool     read_once;            /**< read_ms is a time                  */
    uint32_t read_ms;
    uint16_t regs[LINK_PG_REGS];
    uint8_t  want_target;          /**< 0, or 1 + n for port n             */
    bool     want_pwm;
} prog_link_t;

/** One register to write. */
typedef struct {
    uint8_t  off;
    uint16_t value;
} prog_link_write_t;

/** No page, nothing wanted. */
void prog_link_init(prog_link_t *p);

/** A coprocessor answered with protocol minor @p minor.  What is wanted
 *  stays; the page is unknown. */
void prog_link_came_up(prog_link_t *p, uint16_t minor);

/** The link is down: no page. */
void prog_link_lost(prog_link_t *p);

/** What the screen wants: @p target 0 for the standard outputs or 1 + n for
 *  port n, and whether that port renders its pulse.  A target above
 *  LINK_PG_PORTS_MAX is taken as 0. */
void prog_link_want(prog_link_t *p, uint8_t target, bool pwm);

/** Whether the page is to be read now: it is unknown, or
 *  PROG_LINK_POLL_MS have passed. */
bool prog_link_read_due(const prog_link_t *p, uint32_t now_ms);

/** The page as read, LINK_PG_REGS registers, or NULL for a read that was
 *  not answered: the page is unknown then. */
void prog_link_read(prog_link_t *p, const uint16_t *regs, uint32_t now_ms);

/** The page is unknown until it is read again. */
void prog_link_stale(prog_link_t *p);

/**
 * The write that is due, into @p w.  @p quiet says the bank is disarmed and
 * no surface holds a position this panel wrote.  False for none: no page,
 * the page unknown, nothing differs, or what differs waits for @p quiet.
 * The pulse is switched off before the target changes and on after.
 */
bool prog_link_next(const prog_link_t *p, bool quiet, prog_link_write_t *w);

/** What became of @p w: acknowledged, or not (refused or unanswered), which
 *  makes the page unknown. */
void prog_link_written(prog_link_t *p, const prog_link_write_t *w, bool ack);

/** The target the screen can have: what is wanted, or 0 when the page has
 *  no such port or the port has no bound pin -- the coprocessor refuses
 *  such a target.  0 while the page is unknown or absent. */
uint8_t prog_link_wanted(const prog_link_t *p);

/** Whether prog_link_target() is the coprocessor's: there is no page, or
 *  the page is read.  A release goes to the target that holds the pulse,
 *  so it waits for this and not for prog_link_settled(). */
bool prog_link_ready(const prog_link_t *p);

/** The target as the coprocessor holds it; 0 without a page or a read. */
uint8_t prog_link_target(const prog_link_t *p);

/** Whether a target change waits: the page is known and its target is not
 *  the one prog_link_wanted() names. */
bool prog_link_pending(const prog_link_t *p);

/** Whether the tester may command: there is no page, or the page is known
 *  and its target is the one wanted.  The pulse bit is not part of it. */
bool prog_link_settled(const prog_link_t *p);

/** Whether the target port renders its pulse, as the page holds it.  False
 *  with the standard outputs as target. */
bool prog_link_pwm(const prog_link_t *p);

/** The GPIO port @p port is bound to, or PROG_LINK_NO_PIN: no page, no
 *  read, no such port, no pin, or a pin the coprocessor refused. */
uint8_t prog_link_pin(const prog_link_t *p, uint8_t port);

/** A channel's configuration as the CHAN_CFG page takes it
 *  (LINK_CC_STRIDE registers) as a port takes it from LINK_PG_SLEW on:
 *  SLEW, MIN_US, MAX_US. */
void prog_link_cfg(const uint16_t *cc, uint16_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PROG_LINK_H */
