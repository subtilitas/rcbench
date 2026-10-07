/*
 * The sweep.  See servo_sweep.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_sweep.h"

#include <math.h>
#include <stddef.h>

bool sweep_cfg_valid(const sweep_cfg_t *cfg)
{
    return cfg != NULL
           && cfg->kind > SWEEP_OFF && cfg->kind < SWEEP_KIND_COUNT
           && cfg->mhz >= SWEEP_MHZ_MIN && cfg->mhz <= SWEEP_MHZ_MAX
           && cfg->amplitude <= SWEEP_AMPLITUDE_MAX
           && cfg->dwell_ms <= SWEEP_DWELL_MAX_MS;
}

bool sweep_start(sweep_t *w, const sweep_cfg_t *cfg, uint32_t now_ms)
{
    if (w == NULL) {
        return false;
    }
    w->paused = false;
    if (!sweep_cfg_valid(cfg)) {
        w->running = false;
        return false;
    }
    w->cfg      = *cfg;
    w->start_ms = now_ms;
    w->running  = true;
    return true;
}

void sweep_stop(sweep_t *w)
{
    if (w != NULL) {
        w->running = false;
        w->paused  = false;
    }
}

/*
 * The phase is the time into the sweep, kept as milliseconds: everything
 * the curve is -- its point, the dwell it is in, the ends it has reached --
 * follows from that time, so starting the clock that far back again is the
 * whole of a resume.
 */
void sweep_pause(sweep_t *w, uint32_t now_ms)
{
    if (w == NULL || !w->running) {
        return;
    }
    w->paused_ms = now_ms - w->start_ms;
    w->running   = false;
    w->paused    = true;
}

bool sweep_resume(sweep_t *w, uint32_t now_ms)
{
    if (w == NULL || !w->paused) {
        return false;
    }
    w->start_ms = now_ms - w->paused_ms;
    w->running  = true;
    w->paused   = false;
    return true;
}

/*
 * One cycle in microseconds: the motion's period, a quarter of it, the dwell
 * and the whole cycle with both dwells.  Integers, because a sweep can run
 * for hours and a float of milliseconds stops counting single ones after
 * four and a half.
 */
typedef struct {
    uint32_t period, quarter, dwell, cycle;
} cycle_t;

static cycle_t cycle_of(const sweep_cfg_t *c)
{
    cycle_t y;
    y.period  = 1000000000u / (uint32_t)c->mhz;
    y.quarter = y.period / 4u;
    y.dwell   = (uint32_t)c->dwell_ms * 1000u;
    y.cycle   = y.period + 2u * y.dwell;
    return y;
}

static uint64_t elapsed_us(const sweep_t *w, uint32_t now_ms)
{
    return (uint64_t)(uint32_t)(now_ms - w->start_ms) * 1000u;
}

/* Ends reached by @p t microseconds into the sweep, with no limit. */
static uint64_t ends_by(const cycle_t *y, uint64_t t)
{
    const uint64_t k = t / y->cycle;
    const uint64_t r = t % y->cycle;
    return 2u * k + (r >= y->quarter ? 1u : 0u)
           + (r >= 3u * (uint64_t)y->quarter + y->dwell ? 1u : 0u);
}

/* When the dwell at the @p n-th end is over, @p n at least 1. */
static uint64_t done_at(const cycle_t *y, uint32_t n)
{
    const uint64_t k = (uint64_t)(n - 1u) / 2u;
    return (n % 2u == 1u)
               ? k * y->cycle + y->quarter + y->dwell
               : k * y->cycle + 3u * (uint64_t)y->quarter
                     + 2u * (uint64_t)y->dwell;
}

uint32_t sweep_moves(const sweep_t *w, uint32_t now_ms)
{
    if (w == NULL || !sweep_cfg_valid(&w->cfg)) {
        return 0u;
    }
    const cycle_t y = cycle_of(&w->cfg);
    const uint64_t n = ends_by(&y, elapsed_us(w, now_ms));
    if (w->cfg.moves != 0u && n > w->cfg.moves) {
        return w->cfg.moves;
    }
    return (n > UINT32_MAX) ? UINT32_MAX : (uint32_t)n;
}

bool sweep_slew_limited(const sweep_cfg_t *cfg, uint16_t slew_per_s)
{
    if (!sweep_cfg_valid(cfg) || slew_per_s == 0u || cfg->amplitude == 0u) {
        return false;
    }
    /* Both sides in thousandths of a unit a second.  The triangle's in
     * integers: 4 * 5000 * 500 is 10^7. */
    const uint32_t slew_milli = (uint32_t)slew_per_s * 1000u;
    switch (cfg->kind) {
    case SWEEP_TRIANGLE:
        return 4u * (uint32_t)cfg->mhz * (uint32_t)cfg->amplitude > slew_milli;
    case SWEEP_SINE:
        return 6.28318531f * (float)cfg->mhz * (float)cfg->amplitude
               > (float)slew_milli;
    case SWEEP_SQUARE:
    case SWEEP_OFF:
    case SWEEP_KIND_COUNT:
    default:
        return true;    /* a jump; the other kinds were refused above */
    }
}

/* The curve's position, -1..1, a fraction @p x of the way through its
 * motion. */
static float shape(sweep_kind_t kind, float x)
{
    switch (kind) {
    case SWEEP_SQUARE:
        return (x < 0.5f) ? 1.0f : -1.0f;
    case SWEEP_TRIANGLE:
        if (x < 0.25f) {
            return 4.0f * x;
        }
        if (x < 0.75f) {
            return 2.0f - 4.0f * x;
        }
        return 4.0f * x - 4.0f;
    case SWEEP_SINE:
        return sinf(6.28318531f * x);
    case SWEEP_OFF:
    case SWEEP_KIND_COUNT:
    default:
        return 0.0f;
    }
}

bool sweep_step(sweep_t *w, uint32_t now_ms, uint16_t *command)
{
    uint16_t out = (uint16_t)SWEEP_CENTRE;
    bool running = false;
    if (w != NULL && w->running && sweep_cfg_valid(&w->cfg)) {
        const cycle_t y = cycle_of(&w->cfg);
        const uint64_t t = elapsed_us(w, now_ms);
        if (w->cfg.moves != 0u && t >= done_at(&y, w->cfg.moves)) {
            w->running = false;
        } else {
            /*
             * Where the motion is, the dwells taken out: the curve stands
             * still a quarter and three quarters of the way through while
             * an end is held.
             */
            const uint32_t r = (uint32_t)(t % y.cycle);
            const uint32_t q = y.quarter, d = y.dwell;
            uint32_t m;
            if (r < q) {
                m = r;
            } else if (r < q + d) {
                m = q;
            } else if (r < 3u * q + d) {
                m = r - d;
            } else if (r < 3u * q + 2u * d) {
                m = 3u * q;
            } else {
                m = r - 2u * d;
            }
            const float x = (float)m / (float)y.period;
            const float p = shape(w->cfg.kind, x);
            float c = (float)SWEEP_CENTRE + p * (float)w->cfg.amplitude;
            if (c < 0.0f) {
                c = 0.0f;
            }
            if (c > 2.0f * (float)SWEEP_CENTRE) {
                c = 2.0f * (float)SWEEP_CENTRE;
            }
            out = (uint16_t)(c + 0.5f);
            running = true;
        }
    }
    if (command != NULL) {
        *command = out;
    }
    return running;
}
