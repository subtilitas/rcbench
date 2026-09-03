/*
 * When the bench is allowed to be armed, and what the panel must tell the
 * coprocessor about it.
 *
 * The policy only: no touch driver, no link, no task.  The panel's control
 * loop feeds it what happened and acts on what it returns, and the host suite
 * can then hold the rules that decide whether something spins.
 *
 * The rules, each of which has cost this project a defect:
 *
 *   - A stop latches.  Only an explicit arm clears it.
 *   - The heartbeat is suppressed while a stop is latched, and the
 *     coprocessor refuses to arm while the heartbeat is not trusted.  An arm
 *     therefore clears the latch FIRST and waits for the line to settle
 *     before the write, or the two conditions deadlock each other.
 *   - A stop during that wait wins.  The arm is abandoned, not queued.
 *   - Touch that has stopped answering disarms and refuses to arm: the panel
 *     is the only place a STOP button exists.
 *   - The run clock times a run, not the panel.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How long the touch controller may go without answering before the bench is
 * disarmed and refused an arm.
 */
#define ARMING_TOUCH_DEAD_MS 500u

/** What the caller must put on the link, if anything, after a step. */
typedef enum {
    ARMING_ACT_NONE = 0,
    ARMING_ACT_DISARM,   /**< write ARM = 0                          */
    ARMING_ACT_ARM,      /**< write CLEAR, then ARM = 1              */
} arming_action_t;

typedef struct {
    bool     armed;
    bool     stopped;         /**< the latch                              */
    bool     arming;          /**< an arm is waiting for the line         */
    uint32_t settle_ms;       /**< how long the line is given             */
    uint32_t settle_until_ms;
    uint32_t last_touch_ms;
    uint32_t run_start_ms;    /**< 0 when not in a run                    */
    uint32_t run_seconds;     /**< held after the run ends                */
} arming_t;

/**
 * @p settle_ms is how long the heartbeat is given to become trustworthy
 * before an arm is written: HEARTBEAT_GOOD_RUN intervals, plus one.
 */
void arming_init(arming_t *a, uint32_t now_ms, uint32_t settle_ms);

/** The touch controller answered. */
void arming_touch_seen(arming_t *a, uint32_t now_ms);

/** True once touch has been silent for ARMING_TOUCH_DEAD_MS. */
bool arming_touch_dead(const arming_t *a, uint32_t now_ms);

/** STOP. Latches; abandons an arm that is waiting for the line. */
void arming_stop(arming_t *a);

/** The operator asked to arm. Ignored while touch is dead. */
void arming_request_arm(arming_t *a, uint32_t now_ms);

/** The operator asked to disarm. */
void arming_request_disarm(arming_t *a);

/**
 * The coprocessor refused the arm. The latch stays clear: a heartbeat that
 * is running is the truth about a loop that is running, and the operator can
 * ask again.
 */
void arming_refused(arming_t *a);

/** The coprocessor disarmed us: a NACK on a control write, or a failsafe. */
void arming_stop_from_far_end(arming_t *a);

/** Advance, and say what the link owes the coprocessor. */
arming_action_t arming_step(arming_t *a, uint32_t now_ms);

/** Whether the heartbeat may edge. */
bool arming_heartbeat(const arming_t *a, uint32_t now_ms);

/** Seconds of the current run, or of the last one once it has ended. */
uint32_t arming_run_seconds(const arming_t *a);

#ifdef __cplusplus
}
#endif
