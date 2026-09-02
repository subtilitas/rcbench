/*
 * SPDX-License-Identifier: MIT
 */

#include "servo_limit.h"

#include <string.h>

/*
 * How many positions feed the free-running baseline before the knee test is
 * allowed to fire.
 *
 * Three, more than one: the first position is the centre, where a surface
 * with any preload draws more than it does a few degrees out, and a baseline
 * from that single reading sits high enough to hide a real knee.  Three
 * positions average that away and are over within three steps of travel.
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
    /* A pulse width is unsigned; this guards a configuration with centre
     * below max_travel. */
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
     * 10 us is about 1 degree on a standard servo (1000 us of pulse width
     * over about 90 degrees of travel).  The step has to be small enough that
     * one step cannot carry the horn from free to hard against the stop; the
     * method depends on meeting the knee rather than arriving past it.
     */
    cfg->step_us       = positive ? 10 : -10;
    cfg->max_travel_us = 600;    /* 900..2100 us from a 1500 us centre */
    cfg->settle_ms     = 120;    /* a slow digital servo arrives within this */
    cfg->measure_ms    = 80;
    cfg->knee_ratio    = 1.8f;
    cfg->knee_margin_a = 0.15f;
    cfg->hard_limit_a  = 3.0f;
    cfg->stall_above_a = 1.0f;   /* between free current and the hard limit */
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

/* Finish the search with the surface returned to centre. */
static void finish(servo_limit_t *lf, servo_limit_state_t state,
                   servo_limit_fault_t fault)
{
    lf->state  = state;
    lf->fault  = fault;
    lf->cmd_us = lf->cfg.centre_us;
}

/*
 * Move one step further out.  Past the permitted travel the surface is free
 * over the whole range asked about, and that is reported as NO_LIMIT rather
 * than as a mechanical limit.
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

/* One position's averaged current has arrived; decide what it means. */
static void judge(servo_limit_t *lf, float avg_a)
{
    if (lf->baseline_steps < BASELINE_STEPS) {
        lf->baseline_sum += (double)avg_a;
        ++lf->baseline_steps;
        lf->baseline_a = (float)(lf->baseline_sum / (double)lf->baseline_steps);
        lf->last_free_us = lf->cmd_us;
        /* Advance, so the baseline covers BASELINE_STEPS positions rather
         * than that many readings of one.  A preloaded surface draws most at
         * centre, and a baseline of centre readings alone sits high enough to
         * hide a real knee. */
        (void)advance(lf);
        return;
    }

    const bool bound = (avg_a > lf->baseline_a * lf->cfg.knee_ratio)
                       && (avg_a > lf->baseline_a + lf->cfg.knee_margin_a);
    if (bound) {
        /*
         * The first rise ends the search.  Confirming it by pushing further
         * would cost another settle-and-measure cycle (200 ms at the
         * defaults) of a servo pushing against a stop already found.
         *
         * The endpoint is the last free position, backed off by backoff_us.
         * Both steps: the bound position is already too far, and the last
         * free one is only one step short of binding.
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
     * The two protections that do not wait for a position to finish, checked
     * on every sample before anything else.  They act inside the settle
     * window: a servo that hits its stop while still travelling would
     * otherwise draw stall current for the whole settle time.
     */
    if (current_a >= lf->cfg.hard_limit_a) {
        finish(lf, SERVO_LIMIT_FAULT, SERVO_LIMIT_FAULT_OVERCURRENT);
        return lf->cmd_us;
    }

    /*
     * Measured continuously across positions, not within one.  A search that
     * steps past a bind because the knee test is set too high stays elevated
     * the whole way, and the duration is what this catches.  A timer reset
     * at each position never fires while settle_ms plus measure_ms is
     * shorter than stall_ms.
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
