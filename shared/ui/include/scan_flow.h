/**
 * @file
 * @brief The scan flow: a scanned line made into the active servo, the
 *        alert band's notice for it, and the band's servo chip.
 *
 * A line reaches the flow two ways.  scan_flow_post() takes one from any
 * task (the debug console's `scan <text>`, the scanner's UART task) into a
 * mailbox of one line; scan_flow_tick() on the frame task runs it.
 * scan_flow_line() runs a line at once on the frame task; the host suite
 * and the renderer inject lines through it.
 *
 * A line runs through servo_scan_decide() (servo_active.h) with the run
 * state of the screens (scan_flow_run_active()) and raises the notice of
 * PLAN-B 3.3 (ui_router_set_notice()).  A scan starts no run, arms
 * nothing, switches no output and changes no screen.  On the splash and
 * on the two update screens a line is not taken.
 *
 * What the card knows of an id comes through a look-up hook
 * (scan_flow_set_look()): no card, new on this card, or known with the
 * DUT NAME of its newest run.  A known servo's name becomes DUT NAME
 * (SET_TEXT_DUT_NAME).  Without a hook the notice names the id alone and
 * DUT NAME stays: the card walk that answers the hook belongs to the
 * filing (panel_servo.c), which registers it.
 *
 * State of the frame task, but the mailbox, which any number of tasks
 * may post to.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "servo_active.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What the card knows of a scanned id. */
typedef enum {
    SCAN_CARD_UNKNOWN = 0,  /**< not asked, or no answer: the id alone   */
    SCAN_CARD_NONE,         /**< no card: runs are not filed             */
    SCAN_CARD_NEW,          /**< no folder of the id on this card        */
    SCAN_CARD_KNOWN,        /**< a folder with runs; its newest's name   */
} scan_card_t;

/**
 * @brief The look-up of an id on the card.
 * @param id   The id, upper case.
 * @param name Receives the `dut` value of the id's newest run; "" for
 *             none.  Only read for SCAN_CARD_KNOWN.
 * @param n    Its size, SETTINGS_TEXT_MAX.
 * @return What the card knows.
 */
typedef scan_card_t (*scan_flow_look_fn)(const char *id, char *name,
                                         size_t n);

/**
 * @brief Register the card's look-up; NULL for none.
 * @param fn The look-up.
 */
void scan_flow_set_look(scan_flow_look_fn fn);

/**
 * @brief Hand a line to the frame task, from any task.
 *
 * The mailbox holds one line.  A line past SERVO_SCAN_LINE_MAX bytes goes
 * as too long.
 *
 * @param line The line without its CR LF; NULL does nothing.
 * @return false when NULL, or when a line still waits: this one is dropped.
 */
bool scan_flow_post(const char *line);

/**
 * @brief Frame task, once a frame: run the line that waits, if any.
 *
 * The arm is read from @p st, the status this frame acts on, not from the
 * band's status of the frame before: a line in the frame that first sees
 * an arm on SERVO is refused.
 *
 * @param now_ms The millisecond clock.
 * @param st     This frame's bench status; NULL reads the band's.
 */
void scan_flow_tick(uint32_t now_ms, const ui_bench_status_t *st);

/**
 * @brief Frame task: run one line now.
 * @param line     The line without its CR LF; NULL counts as "".
 * @param too_long The scanner cut it at SERVO_SCAN_LINE_MAX.
 * @param now_ms   The millisecond clock.
 * @return false when the screen on top takes no line (the splash, an
 *         update); else true, with scan_flow_last() saying what it did.
 */
bool scan_flow_line(const char *line, bool too_long, uint32_t now_ms);

/**
 * @brief What the last line taken did.
 * @return SERVO_SCAN_REPEAT before the first.
 */
servo_scan_t scan_flow_last(void);

/**
 * @brief Whether a run is active, as the scan and NONE see it: a servo
 *        test, a limit run or RUDDER AUTO under way, or the bench ARMED
 *        with UI_ARMED_SERVO (a SERVO run is logged).
 * @return true while one is.
 */
bool scan_flow_run_active(void);

/**
 * @brief Put the active servo into @p st for the band: its id, how it was
 *        set and whether a run holds it, the arm read from @p st itself.
 * @param st The status the band shows; NULL does nothing.
 */
void scan_flow_status(ui_bench_status_t *st);

/**
 * @brief Forget the mailbox, the hook and the last result; the host
 *        suite's start of a case.
 */
void scan_flow_reset(void);

#ifdef __cplusplus
}
#endif
