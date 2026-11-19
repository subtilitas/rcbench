/**
 * @file
 * @brief AUTO end points: the limit search that opens every run and takes
 *        its first measure; see servo_rudder_auto.h, "The limit search".
 *
 * LIMIT RUN's finder (servo_limit.h) steps each side out from PULSE CENTER
 * to the margin's range on the INA3221's windows.  The run records every
 * position the finder judges, holds the servo at the end pulse once the
 * finder judged a position at or past it free, takes the side's first
 * angle there by the measure's rule, and lets the finder go on.  The found
 * ends cap the run's later pulses.  An end inside the end pulse is held for
 * its angle: short of TARGET less TOLERANCE it ends the run, else that
 * side's measures drive the end, their angles scaled to the end pulse.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_rudder_auto_int.h"

#include <math.h>
#include <stdlib.h>

/** @brief The end pulse of each side, us. */
static const uint16_t k_end[2] = { 1000u, 2000u };

/**
 * @brief Tell whether a millisecond span has elapsed.
 * @param now  Current time, ms.
 * @param then Start of the span, ms.
 * @param ms   Span length, ms.
 * @return true when now - then is at least ms, across a counter wrap.
 */
static bool due(uint32_t now, uint32_t then, uint32_t ms)
{
    return (int32_t)(now - then) >= (int32_t)ms;
}

/**
 * @brief The positions a side's search commands: its 10us steps, the last
 *        short one included, and the end pulse where it lies between two
 *        of them.
 * @param travel_us How far from the center it reaches, us.
 * @param end_us    How far from the center the end pulse lies, us.
 * @return the count.
 */
static uint16_t steps_of(uint16_t travel_us, uint16_t end_us)
{
    const bool between = end_us % 10u != 0u && end_us < travel_us;
    return (uint16_t)((travel_us + 9u) / 10u + (between ? 1u : 0u));
}

void rauto_search_begin(servo_rauto_t *r, uint8_t side, uint32_t now_ms)
{
    uint16_t us[2];
    servo_rauto_pulse_range(r->cfg.margin_pct, us);
    const uint16_t c = r->cfg.center_us;
    const uint16_t travel = side == 0u ? (uint16_t)(c - us[0])
                                       : (uint16_t)(us[1] - c);
    servo_limit_cfg_t lc;
    servo_limit_defaults(&lc, c, side == 1u);
    lc.max_travel_us = travel;
    servo_limit_init(&r->lf, &lc);
    if (side == 0u) {
        r->s_plan = (uint16_t)(steps_of(travel, (uint16_t)(c - k_end[0]))
                               + steps_of((uint16_t)(us[1] - c),
                                          (uint16_t)(k_end[1] - c)));
    }
    r->side      = side;
    r->s_hold    = false;
    r->s_from_ms = now_ms;
    rauto_command(r, SERVO_RAUTO_PH_SEARCH, c, now_ms);
}

/**
 * @brief Record the position the finder just judged.
 * @param r  Run state.
 * @param us The position, us.
 */
static void add_row(servo_rauto_t *r, uint16_t us)
{
    if (r->srow_n >= SERVO_RAUTO_SEARCH_MAX) {
        return;
    }
    servo_rauto_srow_t *p = &r->srow[r->srow_n++];
    const double a = r->lf.samples > 0u
                         ? r->lf.sum_a / (double)r->lf.samples : 0.0;
    const double ma = a * 1000.0 + 0.5;
    p->us    = us;
    p->ma    = (uint16_t)(ma > 65535.0 ? 65535.0 : ma < 0.0 ? 0.0 : ma);
    p->deg   = r->angle_deg;
    p->valid = r->angle_valid;
    p->side  = r->side;
}

/**
 * @brief Command the finder's pulse, a new position or the center.
 * @param r      Run state.
 * @param us     The pulse, us.
 * @param now_ms Time, ms.
 */
static void go_to(servo_rauto_t *r, uint16_t us, uint32_t now_ms)
{
    if (us != r->cfg.center_us) {
        ++r->s_steps;
    }
    rauto_command(r, SERVO_RAUTO_PH_SEARCH, us, now_ms);
}

uint16_t servo_rauto_side_us(const servo_rauto_t *r, uint8_t s)
{
    const unsigned k = s == 0u ? 0u : 1u;
    if (r == NULL || !r->ends[k].at_end) {
        return k_end[k];
    }
    return r->ends[k].end_us;
}

float rauto_at_side(const servo_rauto_t *r, uint8_t s, float deg)
{
    const unsigned k = s == 0u ? 0u : 1u;
    const servo_rauto_end_t *e = &r->ends[k];
    if (!e->at_end) {
        return deg;
    }
    const int c = (int)r->cfg.center_us;
    const int full = abs((int)k_end[k] - c);
    const int here = abs((int)e->end_us - c);
    return here > 0 ? deg * (float)full / (float)here : deg;
}

/**
 * @brief A side's search is through: the other side next, or both first
 *        measures judged after the return to the center.
 * @param r      Run state.
 * @param now_ms Time, ms.
 */
static void side_next(servo_rauto_t *r, uint32_t now_ms)
{
    if (r->side == 0u) {
        rauto_search_begin(r, 1u, now_ms);
        return;
    }
    rauto_command(r, SERVO_RAUTO_PH_SEARCH, r->cfg.center_us, now_ms);
    r->searched = true;
    rauto_searched(r, now_ms);
}

/**
 * @brief The angle at a found end inside the end pulse, judged: below
 *        TARGET less TOLERANCE the linkage stops the surface short of the
 *        target and the run ends STOP; else the side is measured at the end
 *        from here on, this angle its first measure.
 * @param r      Run state.
 * @param deg    The angle, signed, °.
 * @param at_us  Where it was taken, us.
 * @param now_ms Time, ms.
 */
static void end_judged(servo_rauto_t *r, float deg, uint16_t at_us,
                       uint32_t now_ms)
{
    const uint8_t s = r->side;
    servo_rauto_end_t *e = &r->ends[s];
    e->deg_taken = true;
    e->deg       = deg;
    e->deg_us    = at_us;
    if (fabsf(deg) + RAUTO_EPS_DEG < r->goal_deg[s] - rauto_tol_deg(r)) {
        /* The surface stops short of the target: that side gets no
         * write. */
        r->stop_us = e->end_us;
        rauto_end(r, SERVO_RAUTO_FAIL, SERVO_RAUTO_WHY_STOP, now_ms);
        return;
    }
    /* The stop lies past the target: the writes shorten the throw, and
     * every measure of the side drives the end, not the end pulse. */
    e->at_end    = true;
    r->s_took[s] = true;
    r->s_deg[s]  = deg;
    side_next(r, now_ms);
}

/**
 * @brief A side's search is over: its end recorded, the found end held for
 *        its angle where it lies inside the end pulse, then the other side
 *        or the first measures judged.
 * @param r      Run state.
 * @param bind   The pulse the finder judged bound, us.
 * @param now_ms Time, ms.
 */
static void side_done(servo_rauto_t *r, uint16_t bind, uint32_t now_ms)
{
    const uint8_t s = r->side;
    if (r->lf.state == SERVO_LIMIT_FAULT
        && r->lf.fault != SERVO_LIMIT_FAULT_NO_LIMIT) {
        /* The finder's own guards, the run's limits: its ceiling, its
         * stall. */
        rauto_end(r, SERVO_RAUTO_ABORTED,
                  r->lf.fault == SERVO_LIMIT_FAULT_OVERCURRENT
                      ? SERVO_RAUTO_WHY_HARD : SERVO_RAUTO_WHY_STALL,
                  now_ms);
        return;
    }
    servo_rauto_end_t *e = &r->ends[s];
    e->done    = true;
    e->bound   = r->lf.state == SERVO_LIMIT_FOUND;
    e->end_us  = r->lf.limit_us;
    e->bind_us = e->bound ? bind : 0u;
    e->free_a  = r->lf.baseline_a;
    e->bound_a = e->bound ? r->lf.knee_a : 0.0f;
    const bool inside = s == 0u ? e->end_us > k_end[0]
                                : e->end_us < k_end[1];
    if (inside) {
        /* The end pulse lies past the stop: the angle at the found end
         * decides, taken by the measure's rule. */
        r->s_hold    = true;
        r->s_endhold = true;
        go_to(r, e->end_us, now_ms);
        return;
    }
    side_next(r, now_ms);
}

/**
 * @brief The angle of the last free step of the side, as the search
 *        recorded it.
 * @param r   Run state.
 * @param deg Out: the angle, °.
 * @return true when that step has an angle.
 */
static bool last_free_deg(const servo_rauto_t *r, float *deg)
{
    for (unsigned k = r->srow_n; k > 0u; --k) {
        const servo_rauto_srow_t *p = &r->srow[k - 1u];
        if (p->side == r->side && p->us == r->lf.last_free_us) {
            *deg = p->deg;
            return p->valid;
        }
    }
    return false;
}

/**
 * @brief Whether the finder's next pulse lies past the side's end pulse,
 *        with its first measure still to take.
 * @param r  Run state.
 * @param us The finder's next pulse, us.
 * @return true when the servo is to hold at the end pulse first.
 */
static bool past_end(const servo_rauto_t *r, uint16_t us)
{
    const uint8_t s = r->side;
    if (r->s_took[s]) {
        return false;
    }
    return s == 0u ? us < k_end[0] : us > k_end[1];
}

void rauto_search_window(servo_rauto_t *r, uint16_t ma, uint32_t at_ms)
{
    if (r->s_hold || r->lf.state != SERVO_LIMIT_RUNNING
        || !due(at_ms, r->s_from_ms, SERVO_LIMRUN_RETURN_MS)) {
        return;
    }
    const uint16_t before = r->lf.cmd_us;
    const uint16_t cmd = servo_limit_step(&r->lf, (float)ma / 1000.0f, at_ms);
    const bool ended = r->lf.state != SERVO_LIMIT_RUNNING;
    if (cmd == before && !ended) {
        return;
    }
    /* A position judged: free, bound, or the end of the travel. */
    add_row(r, before);
    const uint8_t s = r->side;
    if (!r->s_took[s] && r->lf.last_free_us == before
        && before == k_end[s]) {
        /* The end pulse itself judged free: held there for the angle. */
        r->s_hold    = true;
        r->s_next_us = cmd;
        return;
    }
    if (ended) {
        side_done(r, before, at_ms);
        return;
    }
    if (past_end(r, cmd)) {
        /* The next step passes the end pulse: the end pulse first. */
        r->s_hold    = true;
        r->s_next_us = cmd;
        go_to(r, k_end[s], at_ms);
        return;
    }
    go_to(r, cmd, at_ms);
}

void rauto_search_step(servo_rauto_t *r, uint32_t now_ms,
                       const servo_rauto_in_t *in)
{
    if (!r->s_hold) {
        return;
    }
    if (r->s_endhold) {
        if (rauto_still(r, now_ms, in)) {
            r->s_hold = r->s_endhold = false;
            end_judged(r, in->angle_deg, r->cmd_us, now_ms);
            return;
        }
        float deg = 0.0f;
        if (!due(now_ms, r->move_ms, servo_rauto_wait_ms(r))) {
            return;
        }
        r->s_hold = r->s_endhold = false;
        if (!last_free_deg(r, &deg)) {
            rauto_end(r, SERVO_RAUTO_FAIL, SERVO_RAUTO_WHY_NOT_STILL, now_ms);
            return;
        }
        /* No settled angle at the end: the last free step's, named so. */
        r->ends[r->side].fallback = true;
        end_judged(r, deg, r->lf.last_free_us, now_ms);
        return;
    }
    if (rauto_still(r, now_ms, in)) {
        const uint8_t s = r->side;
        r->s_took[s] = true;
        r->s_deg[s]  = in->angle_deg;
        r->s_hold    = false;
        if (r->lf.state == SERVO_LIMIT_RUNNING) {
            /* On from where the finder stood. */
            go_to(r, r->s_next_us, now_ms);
            servo_limit_resume(&r->lf, now_ms);
        } else {
            side_done(r, r->lf.cmd_us, now_ms);
        }
    } else if (due(now_ms, r->move_ms, servo_rauto_wait_ms(r))) {
        rauto_end(r, SERVO_RAUTO_FAIL, SERVO_RAUTO_WHY_NOT_STILL, now_ms);
    }
}
