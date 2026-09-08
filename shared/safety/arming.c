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
    ++a->stops;
    /* A stop during the settle wins: the arm is abandoned, not deferred to
     * whenever the line happens to become trustworthy. */
    a->arming  = false;
}

bool arming_stopped(const arming_t *a)
{
    return a != NULL && a->stopped;
}

uint32_t arming_stop_count(const arming_t *a)
{
    return (a != NULL) ? a->stops : 0u;
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
        ++a->stops;
        a->arming  = false;
        a->armed   = false;
    }
}

void arming_touch_poll(arming_t *a, uint32_t now_ms)
{
    if (a == NULL) {
        return;
    }
    /*
     * Touch that stops answering counts as a stop, once, on the edge.
     *
     * It is not a latch -- it clears by itself when the controller answers
     * again -- but it invalidates a gesture the same way, and for the same
     * reason: a hold advances on frames rather than on touch events, so one
     * left standing when the controller died goes on counting and would arm
     * the bench on recovery without anybody having touched anything since.
     * Counting it means every caller that watches the count abandons the
     * gesture and drops an arm made before it.
     *
     * Sampled here rather than only in arming_step() because the caller that
     * judges touch is not the caller that steps the policy: a link exchange
     * can wait a second, pumping touch the whole time, and a controller that
     * died and recovered inside that wait would leave no trace at all by the
     * time the policy ran again.
     */
    const bool dead = arming_touch_dead(a, now_ms);
    if (dead && !a->touch_was_dead) {
        ++a->stops;
        /* And what is armed comes down, even if the controller answers
         * again before the policy next runs. */
        if (a->armed) {
            a->disarm_pending = true;
        }
        /*
         * And a settle in progress is abandoned, exactly as a stop abandons
         * one.  It is not gated on being armed: an arm that is still settling
         * has nothing to disarm, and the deadline would otherwise be reached
         * with the controller healthy again and arm from a gesture nobody has
         * re-made.  A screen cancelling its hold cannot retract a request the
         * policy already holds.
         */
        a->arming = false;
    }
    a->touch_was_dead = dead;
}

arming_action_t arming_step(arming_t *a, uint32_t now_ms)
{
    if (a == NULL) {
        return ARMING_ACT_NONE;
    }

    arming_action_t act = ARMING_ACT_NONE;

    arming_touch_poll(a, now_ms);
    const bool dead = arming_touch_dead(a, now_ms);

    /* An outage the poll saw and this step cannot: see disarm_pending. */
    if (a->disarm_pending) {
        a->disarm_pending = false;
        if (a->armed) {
            a->armed  = false;
            a->arming = false;
            return ARMING_ACT_DISARM;
        }
    }

    /* Touch that has stopped answering, or a latched stop, disarms. */
    if ((dead || a->stopped) && a->armed) {
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
