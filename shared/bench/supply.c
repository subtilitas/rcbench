/*
 * A programmable supply and its model.  See supply.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "supply.h"

#include <math.h>
#include <stddef.h>

#include "bench_state.h"

float supply_snap(float value, float min, float max, float step)
{
    if (isnan(value)) {           /* the bottom of the range */
        value = min;
    }
    if (value < min) {
        value = min;
    }
    if (value > max) {
        value = max;
    }
    if (step > 0.0f) {
        const float n = floorf((value - min) / step + 0.5f);
        value = min + n * step;
        if (value > max) {
            value = max;
        }
    }
    return value;
}

void supply_reset_peaks(supply_state_t *s)
{
    if (s == NULL) {
        return;
    }
    /* Only what arrived; a reading that did not is left over from before
     * and would hold the run's maxima above anything this run delivers.
     * Current and power do not go below 0, so 0 is no maximum. */
    const uint8_t both = (uint8_t)(SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    s->v_min = s->v;
    s->i_max = ((s->ok & SUPPLY_OK_CURRENT) != 0u) ? s->i : 0.0f;
    s->p_max = ((s->ok & both) == both) ? s->p : 0.0f;
    s->sag_seeded = s->output && (s->ok & SUPPLY_OK_VOLTAGE) != 0u;
}

void supply_track_peaks(supply_state_t *s)
{
    if (s == NULL) {
        return;
    }
    if ((s->ok & SUPPLY_OK_VOLTAGE) != 0u) {
        if (!s->sag_seeded || s->v < s->v_min) {
            s->v_min = s->v;
            s->sag_seeded = true;
        }
    }
    if ((s->ok & SUPPLY_OK_CURRENT) != 0u && s->i > s->i_max) {
        s->i_max = s->i;
    }
    const uint8_t both = (uint8_t)(SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    if ((s->ok & both) == both && s->p > s->p_max) {
        s->p_max = s->p;
    }
}

void supply_reset_totals(supply_state_t *s)
{
    if (s == NULL) {
        return;
    }
    s->charge_mah = 0.0f;
    s->energy_wh  = 0.0f;
    s->counted    = 0u;
}

void supply_count_totals(supply_state_t *s, float dt_s)
{
    if (s == NULL || !s->output) {
        return;
    }
    float dt = dt_s;
    if (!(dt > 0.0f)) {
        dt = 0.0f;
    } else if (dt > BENCH_TOTALS_MAX_STEP_S) {
        dt = BENCH_TOTALS_MAX_STEP_S;
    }
    if ((s->ok & SUPPLY_OK_CURRENT) != 0u) {
        s->charge_mah += s->i * dt * (1000.0f / 3600.0f);
        s->counted |= BENCH_COUNTED_CHARGE;
    }
    const uint8_t both = (uint8_t)(SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    if ((s->ok & both) == both) {
        s->energy_wh += s->p * dt * (1.0f / 3600.0f);
        s->counted |= BENCH_COUNTED_ENERGY;
    }
}

/* ----------------------------------------------------------------- limits */

supply_caps_t supply_caps_limited(const supply_caps_t *caps,
                                  const supply_limits_t *lim)
{
    const supply_caps_t pps = SUPPLY_CAPS_PPS_DEFAULT;
    supply_caps_t out = (caps != NULL) ? *caps : pps;
    if (lim != NULL) {
        if (lim->v_max < out.v_max) {
            out.v_max = lim->v_max;
        }
        if (lim->i_max < out.i_max) {
            out.i_max = lim->i_max;
        }
    }
    if (!(out.v_max >= out.v_min)) {
        out.v_max = out.v_min;
    }
    if (!(out.i_max >= out.i_min)) {
        out.i_max = out.i_min;
    }
    return out;
}

void supply_trip_reset(supply_trip_t *t)
{
    if (t != NULL) {
        t->over_i_s = 0.0f;
        t->over_v_s = 0.0f;
        t->i_over   = false;
        t->v_over   = false;
        t->i_at     = 0.0f;
        t->v_at     = 0.0f;
    }
}

/*
 * One reading against its threshold: true once it has been over for
 * @p hold_s, counted from the first reading seen over -- not from the one
 * before it, which was not over, so the first interval is not credited.  A
 * threshold of 0 or less is off.
 */
static bool over_for(float *over_s, bool *over, float *at, bool arrived,
                     float value, float threshold, float hold_s, float dt)
{
    /* A count is for the threshold it was made against: one changed while
     * the output is on starts again from the next reading over it. */
    if (*over && *at != threshold) {
        *over   = false;
        *over_s = 0.0f;
    }
    if (!(threshold > 0.0f)) {
        /* Off: what was counted is forgotten, so turning it back on starts
         * the count from nothing. */
        *over_s = 0.0f;
        *over   = false;
        return false;
    }
    if (!arrived) {
        return false;
    }
    if (value > threshold) {
        if (*over) {
            *over_s += dt;
        } else {
            *over   = true;
            *over_s = 0.0f;
            *at     = threshold;
        }
        return *over_s >= hold_s;
    }
    *over_s = 0.0f;
    *over   = false;
    return false;
}

supply_trip_kind_t supply_trip_step(supply_trip_t *t,
                                    const supply_limits_t *lim,
                                    const supply_state_t *s, float dt_s)
{
    if (t == NULL || lim == NULL || s == NULL) {
        return SUPPLY_TRIP_NONE;
    }
    if (!s->output) {
        supply_trip_reset(t);
        return SUPPLY_TRIP_NONE;
    }
    float dt = dt_s;
    if (!(dt > 0.0f)) {
        dt = 0.0f;
    } else if (dt > BENCH_TOTALS_MAX_STEP_S) {
        dt = BENCH_TOTALS_MAX_STEP_S;
    }
    const bool i_trip = over_for(&t->over_i_s, &t->i_over, &t->i_at,
                                 (s->ok & SUPPLY_OK_CURRENT) != 0u, s->i,
                                 lim->trip_i, lim->trip_s, dt);
    const bool v_trip = over_for(&t->over_v_s, &t->v_over, &t->v_at,
                                 (s->ok & SUPPLY_OK_VOLTAGE) != 0u, s->v,
                                 lim->trip_v, lim->trip_s, dt);
    if (!i_trip && !v_trip) {
        return SUPPLY_TRIP_NONE;
    }
    supply_trip_reset(t);
    return i_trip ? SUPPLY_TRIP_CURRENT : SUPPLY_TRIP_VOLTAGE;
}

/* ------------------------------------------------------------------ model */

void supply_sim_init(supply_sim_t *m)
{
    if (m == NULL) {
        return;
    }
    const supply_caps_t caps = SUPPLY_CAPS_PPS_DEFAULT;
    m->caps   = caps;
    m->t      = 0.0f;
    m->output = false;
    /* A servo rail's voltage and a limit a standard servo stays under at
     * rest, so the model starts somewhere a servo test would. */
    m->set_v  = supply_snap(6.0f, caps.v_min, caps.v_max, caps.v_step);
    m->set_i  = supply_snap(2.0f, caps.i_min, caps.i_max, caps.i_step);
}

void supply_sim_set(supply_sim_t *m, float v, float i)
{
    if (m == NULL) {
        return;
    }
    m->set_v = supply_snap(v, m->caps.v_min, m->caps.v_max, m->caps.v_step);
    m->set_i = supply_snap(i, m->caps.i_min, m->caps.i_max, m->caps.i_step);
}

void supply_sim_output(supply_sim_t *m, bool on)
{
    if (m == NULL) {
        return;
    }
    if (on && !m->output) {
        m->t = 0.0f;   /* the load's bursts are counted from the switch-on */
    }
    m->output = on;
}

void supply_sim_step(supply_sim_t *m, float dt_s, supply_state_t *out)
{
    if (m == NULL || out == NULL) {
        return;
    }
    if (dt_s > 0.0f) {
        m->t += dt_s;
    }
    out->set_v  = m->set_v;
    out->set_i  = m->set_i;
    out->output = m->output;
    out->online = true;
    ++out->samples;
    out->ok     = (uint8_t)(SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    if (!m->output) {
        out->v = 0.0f;
        out->i = 0.0f;
        out->p = 0.0f;
        out->mode = SUPPLY_MODE_OFF;
        return;
    }
    /* The load: a resistance, and a burst at the start of every period. */
    const float phase = fmodf(m->t, SUPPLY_SIM_PERIOD_S);
    const float burst = (phase < SUPPLY_SIM_BURST_S) ? SUPPLY_SIM_BURST_A
                                                     : 0.0f;
    /* What it would draw at the set voltage behind the source resistance. */
    const float r = SUPPLY_SIM_LOAD_OHMS;
    float v = m->set_v * r / (r + SUPPLY_SIM_SOURCE_OHMS);
    float i = v / r + burst;
    supply_mode_t mode = SUPPLY_MODE_CV;
    if (i > m->set_i) {
        /* The limit holds and the voltage falls to what the load takes at
         * it: the burst is a current sink, the rest is the resistance. */
        i = m->set_i;
        const float resistive = i - burst;
        v = (resistive > 0.0f) ? resistive * r : 0.0f;
        mode = SUPPLY_MODE_CC;
    } else {
        v -= burst * SUPPLY_SIM_SOURCE_OHMS;
    }
    out->v = v;
    out->i = i;
    out->p = v * i;
    out->mode = mode;
}
