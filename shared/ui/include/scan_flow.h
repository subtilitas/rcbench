/**
 * @file
 * @brief The scan flow: a scanned line made into the active servo, the
 *        alert band's notice for it, and the band's servo chip.
 *
 * A line reaches the flow three ways.  scan_flow_post() takes one from any
 * task (the debug console's `scan <text>`) into a mailbox of one line;
 * scan_flow_pull() fills the mailbox on the frame task from a queue of its
 * caller's (the scanner's, panel_scan_take_line()), only while the queue
 * holds a line and the mailbox is empty, so a line the mailbox cannot take stays in that queue; and
 * scan_flow_tick() on the frame task runs the line that waits.
 * scan_flow_line() runs a line at once on the frame task; the host suite
 * and the renderer inject lines through it.
 *
 * While TEST runs on SETUP > SENSORS > SCANNER (scanner_pane_testing()) a
 * line goes to that page alone and changes nothing else.  Every other line
 * runs, and the page keeps it with what it did (scanner_pane_line()).
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
 * DUT NAME stays.  The panel's hook (panel_servo.c) answers
 * SCAN_CARD_UNKNOWN: the notice names the id, and the card's answer, no
 * card among them, comes frames later from its logger task through
 * scan_flow_answer(), which raises the full notice.
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
#include "servo_look.h"
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
 * @brief Frame task: the card's answer for the last id the hook answered
 *        SCAN_CARD_UNKNOWN.  It raises that id's notice, a known servo's
 *        name into DUT NAME, as the hook's answer would have.
 *
 * Dropped, and false, when @p id is not that id, when another line set an
 * id since, when the id is no longer active, when a run is active (DUT
 * NAME is not changed under a run), or for SCAN_CARD_UNKNOWN.  Each id's
 * answer is taken once.  A DUT NAME changed since the scan (typed by hand)
 * stays: the notice then names the id alone.
 *
 * @param id   The id asked.
 * @param card What the card knows.
 * @param name The newest run's DUT NAME; "" or NULL for none.
 * @return true when it raised the notice.
 */
bool scan_flow_answer(const char *id, scan_card_t card, const char *name);

/**
 * @brief What the card knows, as servo_look() found it: no folder and no
 *        run are new on this card, runs are known, a folder that does not
 *        read is unknown.
 * @param look servo_look()'s result.
 * @return The card for scan_flow_answer().
 */
scan_card_t scan_flow_card_of(servo_look_t look);

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
 * @brief Take one line from the caller's queue.
 * @param line     Receives the line without its CR LF, NUL ended.
 * @param n        Its size, SERVO_SCAN_LINE_MAX + 1.
 * @param too_long Receives true for a line the scanner cut at
 *                 SERVO_SCAN_LINE_MAX.
 * @return false when the queue is empty.
 */
typedef bool (*scan_flow_take_fn)(char *line, size_t n, bool *too_long);

/**
 * @brief Whether the caller's queue holds a line, without taking it.
 * @return true when one waits.
 */
typedef bool (*scan_flow_has_fn)(void);

/**
 * @brief Frame task, before scan_flow_tick(): fill an empty mailbox with
 *        the oldest line of @p take.  An empty queue (@p has false) leaves
 *        the mailbox untouched, so a console line is never refused for it.
 *        A full mailbox (a console line waits) leaves the queue alone: its
 *        line is taken a frame later.
 * @param has  Whether the queue holds a line; NULL does nothing.
 * @param take The queue; NULL does nothing.
 * @return true when a line went into the mailbox.
 */
bool scan_flow_pull(scan_flow_has_fn has, scan_flow_take_fn take);

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
 * @brief Whether a run is active, as the scan and NONE see it:
 *        servo_screen_run_active() (a run, its start sequence, its files
 *        still draining) or the bench ARMED with UI_ARMED_SERVO (a SERVO
 *        run is logged).
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
 * @brief Forget the mailbox, the hook, the pending answer and the last
 *        result; the host suite's start of a case.
 */
void scan_flow_reset(void);

#ifdef __cplusplus
}
#endif
