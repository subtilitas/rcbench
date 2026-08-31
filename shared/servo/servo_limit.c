/*
 * SPDX-License-Identifier: MIT
 */

#include "servo_limit.h"

#include <string.h>

/*
 * How many positions feed the free-running baseline before the knee test is
 * allowed to fire.
 *
 * Three, and it matters that it is more than one.  The first position is the
 * centre, where a surface with any preload at all draws more than it does a
 * few degrees out -- taking the baseline from that single reading would set it
 * high and hide a genuine knee.  Three positions average that away while
 * still being over before the search has gone anywhere.
 */
#define BASELINE_STEPS 3u

static inline bool elapsed(uint32_t now, uint32_t then, uint32_t ms)
{
    return (uint32_t)(now - then) >= ms;
}

static uint16_t clamp_travel(const servo_limit_cfg_t *cfg, int32_t us)
{
    const int32_t centre = (int32_t)cfg->centre_us;
    const int32_t span   = (int32_t)cfg->max_travel_us;
    if (us > centre + span) {
        us = centre + span;
    }
    if (us < centre - span) {
        us = centre - span;
    }
    /* A pulse width is unsigned and a servo has no opinion about zero; this
     * only guards a configuration that put centre below max_travel. */
    return (uint16_t)(us < 0 ? 0 : us);
}

void servo_limit_defaults(servo_limit_cfg_t *cfg, uint16_t centre_us,
                          bool positive)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->centre_us     = centre_us;
    /*
     * Ten microseconds is roughly a quarter of a degree on a standard servo.
     * The step has to be small enough that one of them cannot carry the horn
     * from free to hard against the stop -- the whole method depends on
     * meeting the knee rather than arriving past it.
     */
    cfg->step_us       = positive ? 10 : -10;
    cfg->max_travel_us = 600;    /* 900..2100 us from a 1500 us centre */
    cfg->settle_ms     = 120;    /* a slow digital servo is there well inside this */
    cfg->measure_ms    = 80;
    cfg->knee_ratio    = 1.8f;
    cfg->knee_margin_a = 0.15f;
    cfg->hard_limit_a  = 3.0f;
    cfg->stall_above_a = 1.0f;   /* between any servo's free current and that */
    cfg->stall_ms      = 400;
    cfg->backoff_us    = 25;     /* two and a half steps clear of the bind */
}

void servo_limit_init(servo_limit_t *lf, const servo_limit_cfg_t *cfg)
{
    if (lf == NULL || cfg == NULL) {
        return;
    }
    memset(lf, 0, sizeof(*lf));
    lf->cfg          = *cfg;
    lf->state        = SERVO_LIMIT_RUNNING;
    lf->cmd_us       = cfg->centre_us;
    lf->last_free_us = cfg->centre_us;
    lf->limit_us     = cfg->centre_us;
}

/* Finish the search, one way or the other, with the surface brought back. */
static void finish(servo_limit_t *lf, servo_limit_state_t state,
                   servo_limit_fault_t fault)
{
    lf->state  = state;
    lf->fault  = fault;
    lf->cmd_us = lf->cfg.centre_us;
}

/*
 * Move one step further out, unless that would leave the permitted travel --
 * in which case the surface is free over the whole range asked about, and
 * that is reported rather than dressed up as a mechanical limit.
 *
 * Returns false if the search ended here.
 */
static bool advance(servo_limit_t *lf)
{
    const int32_t next  = (int32_t)lf->cmd_us + lf->cfg.step_us;
    const int32_t reach = (lf->cfg.step_us > 0)
                              ? (int32_t)lf->cfg.centre_us
                                    + (int32_t)lf->cfg.max_travel_us
                              : (int32_t)lf->cfg.centre_us
                                    - (int32_t)lf->cfg.max_travel_us;
    const bool past = (lf->cfg.step_us > 0) ? (next > reach) : (next < reach);
    if (past) {
        lf->limit_us = lf->last_free_us;
        finish(lf, SERVO_LIMIT_FAULT, SERVO_LIMIT_FAULT_NO_LIMIT);
        return false;
    }
    lf->cmd_us = clamp_travel(&lf->cfg, next);
    return true;
}

/* One position's worth of averaged current has arrived; decide what it means. */
static void judge(servo_limit_t *lf, float avg_a)
{
    if (lf->baseline_steps < BASELINE_STEPS) {
        lf->baseline_sum += (double)avg_a;
        ++lf->baseline_steps;
        lf->baseline_a = (float)(lf->baseline_sum / (double)lf->baseline_steps);
        lf->last_free_us = lf->cmd_us;
        /* Advance, so the baseline really is BASELINE_STEPS *positions* and
         * not that many readings of one.  Taking them all at the centre would
         * defeat the whole reason for averaging more than one: a preloaded
         * surface draws most at centre, and a baseline that is only ever the
         * centre reading sits high enough to hide a real knee. */
        (void)advance(lf);
        return;
    }

    const bool bound = (avg_a > lf->baseline_a * lf->cfg.knee_ratio)
                       && (avg_a > lf->baseline_a + lf->cfg.knee_margin_a);
    if (bound) {
        /*
         * The first rise, and the search stops here rather than confirming it
         * by pushing further.  Confirmation would cost exactly what this is
         * meant to avoid: another few hundred milliseconds of a servo pushing
         * against a stop it has already found.
         *
         * The endpoint is measured from the last position that was *free*,
         * not from the one that bound, and then backed off again.  Both, not
         * either: the bound position is already too far by definition, and the
         * last free one is only one step short of binding.
         */
        lf->knee_a = avg_a;
        int32_t endpoint = (int32_t)lf->last_free_us;
        endpoint -= (lf->cfg.step_us > 0) ? (int32_t)lf->cfg.backoff_us
                                          : -(int32_t)lf->cfg.backoff_us;
        lf->limit_us = clamp_travel(&lf->cfg, endpoint);
        finish(lf, SERVO_LIMIT_FOUND, SERVO_LIMIT_FAULT_NONE);
        return;
    }

    lf->last_free_us = lf->cmd_us;
    (void)advance(lf);
}

uint16_t servo_limit_step(servo_limit_t *lf, float current_a, uint32_t now_ms)
{
    if (lf == NULL) {
        return 0;
    }
    if (lf->state != SERVO_LIMIT_RUNNING) {
        return lf->cfg.centre_us;
    }

    if (!lf->started) {
        lf->started        = true;
        lf->phase_start_ms = now_ms;
        lf->measuring      = false;
    }

    /*
     * The two protections that do not wait for a position to finish.  They are
     * checked on every sample, before anything else, because the point of them
     * is to act inside the settle window rather than after it -- a servo that
     * hits its stop while still travelling is drawing stall current for the
     * whole of the settle time otherwise.
     */
    if (current_a >= lf->cfg.hard_limit_a) {
        finish(lf, SERVO_LIMIT_FAULT, SERVO_LIMIT_FAULT_OVERCURRENT);
        return lf->cmd_us;
    }

    /*
     * Measured continuously, across positions rather than within one.  That
     * is the whole point: a search that steps on past a bind -- because the
     * knee test was set too high to see it -- stays elevated the entire way,
     * and it is the *duration* that this catches, not any single position.
     * Resetting the timer at each position, as an earlier version did, made
     * it unreachable under every configuration in which the search still
     * worked.
     */
    const bool elevated_now = current_a > lf->cfg.stall_above_a;
    if (elevated_now) {
        if (!lf->elevated) {
            lf->elevated          = true;
            lf->elevated_since_ms = now_ms;
        } else if (elapsed(now_ms, lf->elevated_since_ms, lf->cfg.stall_ms)) {
            finish(lf, SERVO_LIMIT_FAULT, SERVO_LIMIT_FAULT_STALL);
            return lf->cmd_us;
        }
    } else {
        lf->elevated = false;
    }

    if (!lf->measuring) {
        if (elapsed(now_ms, lf->phase_start_ms, lf->cfg.settle_ms)) {
            lf->measuring      = true;
            lf->phase_start_ms = now_ms;
            lf->sum_a          = 0.0;
            lf->samples        = 0;
        }
        return lf->cmd_us;
    }

    lf->sum_a += (double)current_a;
    ++lf->samples;

    if (elapsed(now_ms, lf->phase_start_ms, lf->cfg.measure_ms)
        && lf->samples > 0) {
        const float avg = (float)(lf->sum_a / (double)lf->samples);
        judge(lf, avg);
        lf->measuring      = false;
        lf->phase_start_ms = now_ms;
    }
    return lf->cmd_us;
}

float servo_limit_progress(const servo_limit_t *lf)
{
    if (lf == NULL || lf->cfg.max_travel_us == 0) {
        return 0.0f;
    }
    if (lf->state != SERVO_LIMIT_RUNNING) {
        return 1.0f;
    }
    int32_t gone = (int32_t)lf->cmd_us - (int32_t)lf->cfg.centre_us;
    if (gone < 0) {
        gone = -gone;
    }
    const float p = (float)gone / (float)lf->cfg.max_travel_us;
    return p > 1.0f ? 1.0f : p;
}
