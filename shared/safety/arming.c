/*
 * SPDX-License-Identifier: MIT
 */

#include "arming.h"

#include <string.h>

void arming_init(arming_t *a, uint32_t now_ms, uint32_t settle_ms)
{
    if (a == NULL) {
        return;
    }
    memset(a, 0, sizeof(*a));
    a->settle_ms     = settle_ms;
    a->last_touch_ms = now_ms;
}

void arming_touch_seen(arming_t *a, uint32_t now_ms)
{
    if (a != NULL) {
        a->last_touch_ms = now_ms;
    }
}

bool arming_touch_dead(const arming_t *a, uint32_t now_ms)
{
    if (a == NULL) {
        return true;
    }
    return (uint32_t)(now_ms - a->last_touch_ms) >= ARMING_TOUCH_DEAD_MS;
}

void arming_stop(arming_t *a)
{
    if (a == NULL) {
        return;
    }
    a->stopped = true;
    /* A stop during the settle wins: the arm is abandoned, not deferred to
     * whenever the line happens to become trustworthy. */
    a->arming  = false;
}

void arming_request_arm(arming_t *a, uint32_t now_ms)
{
    if (a == NULL || arming_touch_dead(a, now_ms)) {
        return;
    }
    /*
     * The latch clears here, before the write, so the heartbeat resumes and
     * the coprocessor can come to trust it.  Clearing it only after a
     * successful write deadlocks: the write needs the line, the line needs
     * the latch cleared.
     */
    a->stopped         = false;
    a->arming          = true;
    a->settle_until_ms = now_ms + a->settle_ms;
}

void arming_request_disarm(arming_t *a)
{
    if (a == NULL) {
        return;
    }
    a->arming = false;
    a->armed  = false;
}

void arming_refused(arming_t *a)
{
    if (a != NULL) {
        a->arming = false;
        a->armed  = false;
    }
}

void arming_stop_from_far_end(arming_t *a)
{
    if (a != NULL) {
        a->stopped = true;
        a->arming  = false;
        a->armed   = false;
    }
}

arming_action_t arming_step(arming_t *a, uint32_t now_ms)
{
    if (a == NULL) {
        return ARMING_ACT_NONE;
    }

    arming_action_t act = ARMING_ACT_NONE;

    /* Touch that has stopped answering, or a latched stop, disarms. */
    if ((arming_touch_dead(a, now_ms) || a->stopped) && a->armed) {
        a->armed  = false;
        a->arming = false;
        act = ARMING_ACT_DISARM;
    }

    if (a->arming && (int32_t)(a->settle_until_ms - now_ms) <= 0) {
        a->arming = false;
        if (!a->stopped && !arming_touch_dead(a, now_ms)) {
            a->armed = true;
            act = ARMING_ACT_ARM;
        }
    }

    /* The clock times the run, not the panel: it starts when the bench arms
     * and holds the last run's length once it stops. */
    if (a->armed) {
        if (a->run_start_ms == 0u) {
            a->run_start_ms = now_ms;
        }
        a->run_seconds = (uint32_t)(now_ms - a->run_start_ms) / 1000u;
    } else {
        a->run_start_ms = 0u;
    }

    return act;
}

bool arming_heartbeat(const arming_t *a, uint32_t now_ms)
{
    if (a == NULL) {
        return false;
    }
    return !a->stopped && !arming_touch_dead(a, now_ms);
}

uint32_t arming_run_seconds(const arming_t *a)
{
    return (a != NULL) ? a->run_seconds : 0u;
}
