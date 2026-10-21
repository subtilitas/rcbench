/**
 * @file
 * @brief The limit run: see servo_limrun.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_limrun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "servo_limrun_int.h"

static bool due(uint32_t now, uint32_t then, uint32_t ms)
{
    return (int32_t)(now - then) >= (int32_t)ms;
}

void servo_limrun_init(servo_limrun_t *r)
{
    if (r != NULL) {
        memset(r, 0, sizeof(*r));
    }
}

bool servo_limrun_running(const servo_limrun_t *r)
{
    return r != NULL && r->state == SERVO_LIMRUN_RUNNING;
}

bool servo_limrun_switching(const servo_limrun_t *r)
{
    return servo_limrun_running(r)
        && ((r->phase == SERVO_LIMRUN_PH_SAFE && r->safe_asked)
            || r->phase == SERVO_LIMRUN_PH_CONNECT
            || r->phase == SERVO_LIMRUN_PH_WRITE
            || r->phase == SERVO_LIMRUN_PH_LEAVE
            || r->phase == SERVO_LIMRUN_PH_ARM);
}

bool servo_limrun_programming(const servo_limrun_t *r)
{
    return servo_limrun_running(r)
        && (r->phase == SERVO_LIMRUN_PH_CONNECT
            || r->phase == SERVO_LIMRUN_PH_WRITE
            || r->phase == SERVO_LIMRUN_PH_LEAVE);
}

bool servo_limrun_wrote(const servo_limrun_t *r)
{
    return r != NULL && r->writes > 0u;
}

bool servo_limrun_restore(const servo_limrun_t *r)
{
    return limrun_restore(r);
}

bool servo_limrun_original(const servo_limrun_t *r, uint16_t *left,
                           uint16_t *right)
{
    if (r == NULL || !r->read_once) {
        return false;
    }
    if (left != NULL) {
        *left = r->servo.angle[SERVO_LIMRUN_LEFT];
    }
    if (right != NULL) {
        *right = r->servo.angle[SERVO_LIMRUN_RIGHT];
    }
    return true;
}

int32_t servo_limrun_dist(const servo_limrun_servo_t *sv, uint8_t side,
                          uint16_t p_us)
{
    const int32_t p = (int32_t)p_us * 10;
    const int32_t n = (int32_t)sv->pulse_dus[SERVO_LIMRUN_P_NEUTRAL];
    return side == SERVO_LIMRUN_HI ? p - n : n - p;
}

int32_t servo_limrun_span(const servo_limrun_servo_t *sv, uint8_t side)
{
    return limrun_span(sv, side);
}

/** @brief The share's terms from the pulses in force: after SERVO PULSES at
 * 1000-2000 those written, not those read. */
static int32_t dist_now(const servo_limrun_t *r, uint8_t side, uint16_t p_us)
{
    servo_limrun_servo_t sv;
    memset(&sv, 0, sizeof(sv));
    memcpy(sv.pulse_dus, r->pulse_now, sizeof(sv.pulse_dus));
    return servo_limrun_dist(&sv, side, p_us);
}

static uint16_t clamp_raw(uint64_t v, uint16_t lo, uint16_t hi)
{
    if (v < lo) {
        return lo;
    }
    return v > hi ? hi : (uint16_t)v;
}

uint16_t servo_limrun_overset(uint16_t v0, int32_t d, int32_t span,
                              uint16_t lo_raw)
{
    if (d <= 0 || span <= 0) {
        return lo_raw > v0 ? v0 : lo_raw;
    }
    const uint64_t num = (uint64_t)v0 * (uint64_t)d
                         * (100u + SERVO_LIMRUN_OVERSET_PCT);
    const uint64_t den = (uint64_t)span * 100u;
    return clamp_raw((num + den - 1u) / den, lo_raw > v0 ? v0 : lo_raw, v0);
}

uint16_t servo_limrun_scaled(uint16_t v, int32_t d, int32_t span,
                             uint16_t lo_raw)
{
    if (d <= 0 || span <= 0) {
        return lo_raw > v ? v : lo_raw;
    }
    return clamp_raw((uint64_t)v * (uint64_t)d / (uint64_t)span,
                     lo_raw > v ? v : lo_raw, v);
}

/**
 * @brief ------------------------------------------------------------ the end
 */

/**
 * @brief The run is over.  It lets the servo go; aborted, it also asks the
 * supply's output off.  A run that ends between CONNECT and LEAVE with
 * nothing running gives the port back, unless it was aborted: the
 * programmer then holds the port for RESTORE, and a STOP or the supply
 * switched off ends its session by itself.  Aborted while it waits for
 * ARMED, it asks SAFE: the ARM it asked may not be taken yet, and a
 * release does not replace one (servo_screen.c, post()).
 */
static void finish(servo_limrun_t *r, servo_limrun_verdict_t v,
                   servo_limrun_why_t why, uint32_t now_ms)
{
    if (r->state != SERVO_LIMRUN_RUNNING) {
        return;
    }
    const bool in_session = r->phase == SERVO_LIMRUN_PH_CONNECT
                            || r->phase == SERVO_LIMRUN_PH_WRITE;
    r->state   = SERVO_LIMRUN_DONE;
    r->verdict = v;
    r->why     = why;
    r->end_ms  = now_ms;
    memset(&r->pend, 0, sizeof(r->pend));
    r->pend.release = true;
    if (v == SERVO_LIMRUN_ABORTED) {
        r->pend.off = true;
        r->pend.disarm = r->phase == SERVO_LIMRUN_PH_ARM;
    } else if (in_session) {
        r->pend.leave = true;
    }
    r->cmd_due = false;
}

static void fail(servo_limrun_t *r, servo_limrun_why_t why, uint32_t now_ms)
{
    finish(r, SERVO_LIMRUN_FAIL, why, now_ms);
}

static void abort_why(servo_limrun_t *r, servo_limrun_why_t why,
                      uint32_t now_ms)
{
    finish(r, SERVO_LIMRUN_ABORTED, why, now_ms);
}

void servo_limrun_abort(servo_limrun_t *r, servo_test_abort_t why,
                        uint32_t now_ms)
{
    if (!servo_limrun_running(r)) {
        return;
    }
    r->test_why = why;
    abort_why(r, SERVO_LIMRUN_WHY_TEST, now_ms);
}

/**
 * @brief ------------------------------------------------------- the searches
 */

static void begin_search(servo_limrun_t *r, uint8_t side,
                         servo_limrun_phase_t ph, uint32_t now_ms)
{
    servo_limit_cfg_t c;
    servo_limit_defaults(&c, r->ctr_us, side == SERVO_LIMRUN_HI);
    c.max_travel_us = (side == SERVO_LIMRUN_HI)
                          ? (uint16_t)(r->end_us[SERVO_LIMRUN_HI] - r->ctr_us)
                          : (uint16_t)(r->ctr_us - r->end_us[SERVO_LIMRUN_LO]);
    servo_limit_init(&r->lf, &c);
    r->side      = side;
    r->phase     = ph;
    r->phase_ms  = now_ms;
    r->search_ms = now_ms;
    r->cmd_us    = r->ctr_us;
    r->cmd_due   = true;
    r->searched  = true;
    memset(&r->probe, 0, sizeof(r->probe));
}

static servo_limrun_side_t side_result(const servo_limrun_t *r,
                                       uint16_t bind_us)
{
    servo_limrun_side_t s = r->probe;
    s.done       = true;
    s.bound      = r->lf.state == SERVO_LIMIT_FOUND;
    s.limit_us   = r->lf.limit_us;
    s.bind_us    = s.bound ? bind_us : 0u;
    s.free_us    = r->lf.last_free_us;
    s.baseline_a = r->lf.baseline_a;
    s.knee_a     = r->lf.knee_a;
    return s;
}

/**
 * @brief How far @p now lies outward of @p was on @p side, us: positive further
 * from the centre. */
static int32_t outward(uint8_t side, const servo_limrun_side_t *now,
                       const servo_limrun_side_t *was)
{
    const int32_t d = (int32_t)now->limit_us - (int32_t)was->limit_us;
    return side == SERVO_LIMRUN_HI ? d : -d;
}

static unsigned reg_of(const servo_limrun_t *r, uint8_t side)
{
    /* Called once the mapping is known, where the two registers limit the
     * two sides: the one that is not LEFT ANGLE's is RIGHT ANGLE's. */
    return r->reg_side[SERVO_LIMRUN_LEFT] == (int8_t)side
               ? (unsigned)SERVO_LIMRUN_LEFT : (unsigned)SERVO_LIMRUN_RIGHT;
}

static unsigned changes(const servo_limrun_t *r, const uint16_t *want)
{
    return (unsigned)(want[0] != r->cur[0]) + (unsigned)(want[1] != r->cur[1]);
}

/**
 * @brief The search centre and ends: the servo's NEUTRAL, PULSE LOWER and PULSE
 * UPPER once read, rounded inward to whole microseconds so no command
 * passes a pulse limit; the bench's PULSE CENTRE, MIN and MAX before. */
static void search_span(servo_limrun_t *r)
{
    if (!r->read_once) {
        r->ctr_us = r->cfg.centre_us;
        r->end_us[SERVO_LIMRUN_LO] = r->cfg.min_us;
        r->end_us[SERVO_LIMRUN_HI] = r->cfg.max_us;
        return;
    }
    r->ctr_us = (uint16_t)((r->pulse_now[SERVO_LIMRUN_P_NEUTRAL] + 5u) / 10u);
    r->end_us[SERVO_LIMRUN_LO] =
        (uint16_t)((r->pulse_now[SERVO_LIMRUN_P_LOWER] + 9u) / 10u);
    r->end_us[SERVO_LIMRUN_HI] =
        (uint16_t)(r->pulse_now[SERVO_LIMRUN_P_UPPER] / 10u);
}

/**
 * @brief A bound side's values worked out for register @p reg: the overset, or,
 * where it is left out, the final value at once (servo_limrun.h).  False
 * where the register holds the smallest value already: nothing lowers it.
 */
static bool prepare_side(servo_limrun_t *r, uint8_t side, unsigned reg)
{
    const uint16_t v0 = r->servo.angle[reg];
    const uint16_t lo = r->servo.lo_raw;
    const int32_t  d0 = r->d0_dus[side];
    const int32_t  span = span_now(r, side);
    if (v0 <= lo) {
        return false;
    }
    /* The overset from the first bound pulse: at or past the stop, so the
     * stop the overset moves out stays inside the pulse limit by the 3 %. */
    const uint16_t v1 = servo_limrun_overset(v0, r->db_dus[side], span, lo);
    /* The move the model gives the stop, 0.1 us: d0 x (v0 / v1 - 1), the
     * last free pulse the stop's nearest estimate. */
    const bool skip = v1 >= v0
                      || (int64_t)d0 * (int64_t)(v0 - v1)
                             < (int64_t)SERVO_LIMRUN_MOVED_US * 10
                                   * (int64_t)v1;
    r->v0[side] = v0;
    r->d1_dus[side] = 0;
    if (skip) {
        r->v1[side] = 0u;
        r->v2[side] = servo_limrun_scaled(v0, d0, span, lo);
        r->stage[side] = ST_FINAL;
    } else {
        r->v1[side] = v1;
        r->v2[side] = 0u;
        r->stage[side] = ST_OVERSET;
    }
    return true;
}

/** @brief The value the side's stage puts in its register. */
static uint16_t stage_value(const servo_limrun_t *r, uint8_t side,
                            unsigned reg)
{
    switch (r->stage[side]) {
    case ST_OVERSET:
    case ST_OVERSET_SEARCH:
        return r->v1[side];
    case ST_FINAL:
    case ST_DONE:
        return r->v2[side];
    default:
        return r->servo.angle[reg];
    }
}

/**
 * @brief The next session's values and the side searched after it (-1: both
 * ends, the final check).  Before the mapping is known: the first bound
 * side's value in the register tried, the other register at its own value.
 * After: each register at its side's stage.  Pure: called again at the
 * session's CONNECT it gives the same.
 */
static void plan(const servo_limrun_t *r, uint16_t *want, int8_t *search)
{
    want[0] = r->cur[0];
    want[1] = r->cur[1];
    *search = -1;
    if (!r->mapped) {
        const unsigned g = r->map_reg;
        want[g] = stage_value(r, r->map_side, g);
        want[1u - g] = r->servo.angle[1u - g];
        *search = (int8_t)r->map_side;
        return;
    }
    for (uint8_t s = 0; s < 2u; ++s) {
        const unsigned g = reg_of(r, s);
        want[g] = stage_value(r, s, g);
        if (r->stage[s] == ST_OVERSET) {
            *search = (int8_t)s;
        }
    }
}

/**
 * @brief A session next: the servo back to the centre first, SAFE asked once it
 * had SERVO_LIMRUN_RETURN_MS to get there (servo_limrun_step()). */
static void go_safe(servo_limrun_t *r, uint32_t now_ms)
{
    r->phase      = SERVO_LIMRUN_PH_SAFE;
    r->phase_ms   = now_ms;
    r->safe_asked = false;
    r->cmd_us     = r->ctr_us;
    r->cmd_due    = true;
}

static void begin_check(servo_limrun_t *r, uint32_t now_ms)
{
    memset(r->after, 0, sizeof(r->after));
    begin_search(r, SERVO_LIMRUN_LO, SERVO_LIMRUN_PH_CHECK, now_ms);
}

/** @brief A session if the plan changes a value, the final check if not. */
static void next_step(servo_limrun_t *r, uint32_t now_ms)
{
    uint16_t want[2];
    int8_t search = -1;
    plan(r, want, &search);
    if (changes(r, want) == 0u) {
        begin_check(r, now_ms);
    } else {
        go_safe(r, now_ms);
    }
}

/** @brief The first bound side's search after its first write: which register
 * limits it.  False where the run ended or tries the other register. */
static bool map_side(servo_limrun_t *r, uint32_t now_ms)
{
    const servo_limrun_side_t *res = &r->probe;
    const uint8_t side = r->map_side;
    const int32_t out = outward(side, res, &r->first[side]);
    const bool moved = !res->bound || out >= (int32_t)SERVO_LIMRUN_MOVED_US;
    const bool inward = res->bound && out <= -(int32_t)SERVO_LIMRUN_MOVED_US;
    const unsigned other = 1u - r->map_reg;
    if (inward) {
        fail(r, SERVO_LIMRUN_WHY_MAPPING, now_ms);
        return false;
    }
    if (!moved) {
        if (r->map_tries >= 2u) {
            fail(r, SERVO_LIMRUN_WHY_MAPPING, now_ms);
        } else if (!prepare_side(r, side, other)) {
            /** @brief The other register holds the smallest value already. */
            fail(r, SERVO_LIMRUN_WHY_SMALLEST, now_ms);
        } else {
            /** @brief That register back to its value, the other one tried. */
            r->map_reg = (uint8_t)other;
            ++r->map_tries;
            go_safe(r, now_ms);
        }
        return false;
    }
    r->mapped = true;
    r->reg_side[r->map_reg] = (int8_t)side;
    r->reg_side[other]      = (int8_t)(1 - side);
    if (r->need[1 - side] && !prepare_side(r, (uint8_t)(1 - side), other)) {
        fail(r, SERVO_LIMRUN_WHY_SMALLEST, now_ms);
        return false;
    }
    return true;
}

static void next_after_probe(servo_limrun_t *r, uint32_t now_ms)
{
    const servo_limrun_side_t *res = &r->probe;
    const uint8_t side = r->side;

    if (!r->mapped && !map_side(r, now_ms)) {
        return;
    }
    if (r->stage[side] == ST_OVERSET_SEARCH) {
        if (!res->bound) {
            /**
             * @brief The stop the overset was to keep in reach is not there.
             */
            fail(r, SERVO_LIMRUN_WHY_NO_STOP, now_ms);
            return;
        }
        const int32_t d1 = dist_now(r, side, res->free_us);
        if (d1 <= 0) {
            fail(r, SERVO_LIMRUN_WHY_BEFORE_NEUTRAL, now_ms);
            return;
        }
        r->d1_dus[side] = d1;
        r->v2[side] = servo_limrun_scaled(r->v1[side], d1,
                                          span_now(r, side),
                                          r->servo.lo_raw);
        r->stage[side] = ST_FINAL;
    }
    next_step(r, now_ms);
}

/** @brief The first search of both sides is over. */
static void after_first_search(servo_limrun_t *r, uint32_t now_ms)
{
    r->need[SERVO_LIMRUN_LO] = r->first[SERVO_LIMRUN_LO].bound;
    r->need[SERVO_LIMRUN_HI] = r->first[SERVO_LIMRUN_HI].bound;
    if (!r->need[SERVO_LIMRUN_LO] && !r->need[SERVO_LIMRUN_HI]) {
        finish(r, SERVO_LIMRUN_FREE, SERVO_LIMRUN_WHY_NONE, now_ms);
        return;
    }
    if (!r->cfg.program) {
        finish(r, SERVO_LIMRUN_FOUND, SERVO_LIMRUN_WHY_NONE, now_ms);
        return;
    }
    for (uint8_t s = 0; s < 2u; ++s) {
        if (!r->need[s]) {
            continue;
        }
        r->d0_dus[s] = dist_now(r, s, r->first[s].free_us);
        r->db_dus[s] = dist_now(r, s, r->first[s].bind_us);
        if (r->d0_dus[s] <= 0) {
            fail(r, SERVO_LIMRUN_WHY_BEFORE_NEUTRAL, now_ms);
            return;
        }
    }
    /* The first bound side's first write finds its register: LEFT ANGLE,
     * or RIGHT ANGLE where LEFT holds the smallest value already. */
    r->map_side  = r->need[SERVO_LIMRUN_LO] ? SERVO_LIMRUN_LO
                                            : SERVO_LIMRUN_HI;
    r->map_tries = 1u;
    if (prepare_side(r, r->map_side, SERVO_LIMRUN_LEFT)) {
        r->map_reg = SERVO_LIMRUN_LEFT;
    } else if (prepare_side(r, r->map_side, SERVO_LIMRUN_RIGHT)) {
        r->map_reg   = SERVO_LIMRUN_RIGHT;
        r->map_tries = 2u;          /* LEFT ANGLE cannot be lowered */
    } else {
        fail(r, SERVO_LIMRUN_WHY_SMALLEST, now_ms);
        return;
    }
    go_safe(r, now_ms);
}

/** @brief The final check is over: PASS with both ends free.  An end that binds
 * with its register at the smallest value fails as that. */
static void after_check(servo_limrun_t *r, uint32_t now_ms)
{
    if (!r->after[0].bound && !r->after[1].bound) {
        finish(r, SERVO_LIMRUN_PASS, SERVO_LIMRUN_WHY_NONE, now_ms);
        return;
    }
    for (uint8_t s = 0; s < 2u; ++s) {
        if (r->after[s].bound && r->mapped
            && r->cur[reg_of(r, s)] <= r->servo.lo_raw) {
            fail(r, SERVO_LIMRUN_WHY_SMALLEST, now_ms);
            return;
        }
    }
    fail(r, SERVO_LIMRUN_WHY_STILL_BINDS, now_ms);
}

/** @brief The search under way has ended. */
static void search_done(servo_limrun_t *r, uint16_t bind_us, uint32_t now_ms)
{
    if (r->lf.state == SERVO_LIMIT_FAULT
        && r->lf.fault != SERVO_LIMIT_FAULT_NO_LIMIT) {
        abort_why(r, r->lf.fault == SERVO_LIMIT_FAULT_STALL
                         ? SERVO_LIMRUN_WHY_STALL : SERVO_LIMRUN_WHY_HARD,
                  now_ms);
        return;
    }
    r->probe = side_result(r, bind_us);
    /**
     * @brief Back to the centre from the end it reached, whatever comes next.
     */
    r->cmd_us  = r->ctr_us;
    r->cmd_due = true;
    switch (r->phase) {
    case SERVO_LIMRUN_PH_SEARCH:
        r->first[r->side] = r->probe;
        if (r->side == SERVO_LIMRUN_LO) {
            begin_search(r, SERVO_LIMRUN_HI, SERVO_LIMRUN_PH_SEARCH, now_ms);
        } else {
            after_first_search(r, now_ms);
        }
        break;
    case SERVO_LIMRUN_PH_PROBE:
        next_after_probe(r, now_ms);
        break;
    case SERVO_LIMRUN_PH_CHECK:
        r->after[r->side] = r->probe;
        if (r->side == SERVO_LIMRUN_LO) {
            begin_search(r, SERVO_LIMRUN_HI, SERVO_LIMRUN_PH_CHECK, now_ms);
        } else {
            after_check(r, now_ms);
        }
        break;
    default:
        break;
    }
}

static bool searching(const servo_limrun_t *r)
{
    return r->phase == SERVO_LIMRUN_PH_SEARCH
        || r->phase == SERVO_LIMRUN_PH_PROBE
        || r->phase == SERVO_LIMRUN_PH_CHECK;
}

/**
 * @brief ------------------------------------------------------------- inputs
 */

bool servo_limrun_start(servo_limrun_t *r, const servo_limrun_cfg_t *cfg,
                        uint32_t now_ms)
{
    if (r == NULL || cfg == NULL || servo_limrun_running(r)
        || !(cfg->min_us < cfg->centre_us && cfg->centre_us < cfg->max_us)
        || ((cfg->program || cfg->set_pulses) && !cfg->read)) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    r->cfg         = *cfg;
    r->state       = SERVO_LIMRUN_RUNNING;
    r->start_ms    = now_ms;
    r->last_win_ms = now_ms;
    r->reg_side[0] = -1;
    r->reg_side[1] = -1;
    r->probe_side  = -1;
    search_span(r);
    if (cfg->read) {
        /** @brief The servo read first: its pulses are the search's ends. */
        go_safe(r, now_ms);
    } else {
        begin_search(r, SERVO_LIMRUN_LO, SERVO_LIMRUN_PH_SEARCH, now_ms);
    }
    return true;
}

/** @brief A current's magnitude, mA.  Wider than the reading: -32768 mA is
 * 32768 mA, past every ceiling, not a negative value under them. */
static int32_t mag(int16_t ma)
{
    return ma < 0 ? -(int32_t)ma : (int32_t)ma;
}

void servo_limrun_window(servo_limrun_t *r, const servo_test_win_t *w)
{
    if (!servo_limrun_running(r) || w == NULL) {
        return;
    }
    /* A window without a current reading is no window: the protections
     * cannot read it, and the meter's silence runs on through it. */
    if (!w->current) {
        return;
    }
    const uint32_t at = w->taken_ms;
    ++r->windows;
    r->have_win    = true;
    r->last_win_ms = at;
    /* Every 1 ms sample against the ceiling, and a sample at the meter's
     * end as one past it: its true value is not known. */
    if (w->clipped != 0u) {
        abort_why(r, SERVO_LIMRUN_WHY_CLIPPED, at);
        return;
    }
    if (mag(w->max_ma) >= SERVO_LIMRUN_HARD_MA
        || mag(w->min_ma) >= SERVO_LIMRUN_HARD_MA
        || mag(w->mean_ma) >= SERVO_LIMRUN_HARD_MA) {
        abort_why(r, SERVO_LIMRUN_WHY_HARD, at);
        return;
    }
    /* Window means above the working level, timed from the window's start
     * across every phase. */
    if (mag(w->mean_ma) > SERVO_LIMRUN_STALL_MA) {
        const uint32_t from = at - SERVO_TEST_WIN_MS;
        if (!r->elevated) {
            r->elevated    = true;
            r->elevated_ms = from;
        }
        if (due(at, r->elevated_ms, SERVO_LIMRUN_STALL_MS)) {
            abort_why(r, SERVO_LIMRUN_WHY_STALL, at);
            return;
        }
    } else {
        r->elevated = false;
    }
    /* While a search drives the servo every window holds the rail too: the
     * rail band's reference is a window's, and a window without the rail
     * ends the run before it moves the search. */
    if (searching(r) && !w->voltage) {
        abort_why(r, SERVO_LIMRUN_WHY_NO_VOLTAGE, at);
        return;
    }
    /* Every search window holds the rail the start was let through on: a
     * rail that went off after START, its first window included, reads
     * free ends on a servo that never moved. */
    if (searching(r) && w->mean_mv < SERVO_LIMRUN_RAIL_MIN_MV) {
        abort_why(r, SERVO_LIMRUN_WHY_SUPPLY_CHANGED, at);
        return;
    }
    if (searching(r)) {
        if (!r->have_rail) {
            r->have_rail = true;
            r->rail_mv   = w->mean_mv;
        } else if (abs((int)w->mean_mv - (int)r->rail_mv)
                   > SERVO_LIMRUN_RAIL_DV_MV) {
            abort_why(r, SERVO_LIMRUN_WHY_SUPPLY_CHANGED, at);
            return;
        }
    }
    if (!searching(r) || r->lf.state != SERVO_LIMIT_RUNNING
        || !due(at, r->search_ms, SERVO_LIMRUN_RETURN_MS)) {
        return;
    }
    const uint16_t before = r->lf.cmd_us;
    const uint16_t free_before = r->lf.last_free_us;
    /* The magnitude, as the protections read it: a shunt fitted the other
     * way round reads a bound servo as a more negative current. */
    const uint16_t cmd = servo_limit_step(&r->lf,
                                          (float)mag(w->mean_ma) / 1000.0f,
                                          at);
    if (r->enc_valid && r->lf.last_free_us != free_before) {
        /* The angle at the furthest free position so far: the servo is
         * still there. */
        r->probe.enc     = true;
        r->probe.enc_deg = r->enc_deg;
    }
    if (cmd != r->cmd_us) {
        r->cmd_us  = cmd;
        r->cmd_due = true;
    }
    if (r->lf.state != SERVO_LIMIT_RUNNING) {
        search_done(r, before, at);
    }
}

void servo_limrun_supply(servo_limrun_t *r, const servo_test_reading_t *s,
                         uint32_t now_ms)
{
    if (!servo_limrun_running(r) || s == NULL) {
        return;
    }
    if (servo_limrun_programming(r)) {
        /* The programmer's session, CONNECT to LEAVE: the supply is its
         * (servo_limrun.h).  Before and after it, in SAFE and on the way
         * back to ARMED, the supply is the run's again. */
        return;
    }
    if (!r->sup_watch) {
        /* The supply is watched from its first reading on: answering or
         * not, its set points, and its output on or off.  An output off
         * stays off: a rail fed from elsewhere gets no second supply
         * switched on beside it. */
        r->sup_watch  = true;
        r->sup_online = s->online;
        r->sup_out    = s->online && s->output;
        r->sup_v      = s->set_v;
        r->sup_i      = s->set_i;
        return;
    }
    if (!r->sup_online) {
        if (s->online) {
            /* A supply that did not answer at the start answers now, its
             * output on or off: a later CONNECT would hand it the rail
             * beside the feed the run started on. */
            abort_why(r, SERVO_LIMRUN_WHY_SUPPLY_CHANGED, now_ms);
        }
        return;
    }
    if (!s->online) {
        servo_limrun_abort(r, SERVO_TEST_AB_SUPPLY_LOST, now_ms);
    } else if (r->sup_out && s->trip != 0u) {
        servo_limrun_abort(r, SERVO_TEST_AB_TRIPPED, now_ms);
    } else if (r->sup_out && !s->output) {
        servo_limrun_abort(r, SERVO_TEST_AB_SUPPLY_OFF, now_ms);
    } else if (s->output != r->sup_out
               || s->set_v > r->sup_v + 0.005f || s->set_v < r->sup_v - 0.005f
               || s->set_i > r->sup_i + 0.0005f
               || s->set_i < r->sup_i - 0.0005f) {
        /** @brief Switched on from off, or its set points moved. */
        abort_why(r, SERVO_LIMRUN_WHY_SUPPLY_CHANGED, now_ms);
    }
}

void servo_limrun_supply_refused(servo_limrun_t *r, bool output_on,
                                 uint32_t now_ms)
{
    if (!servo_limrun_running(r)) {
        return;
    }
    if (r->sup_online && !output_on) {
        /* The supply the run started with, its output gone off: as a
         * reading outside a session reports it. */
        servo_limrun_abort(r, SERVO_TEST_AB_SUPPLY_OFF, now_ms);
    } else {
        abort_why(r, SERVO_LIMRUN_WHY_SUPPLY_CHANGED, now_ms);
    }
}

void servo_limrun_backup(servo_limrun_t *r, const char *name)
{
    if (r != NULL && r->read_once && name != NULL && name[0] != '\0') {
        snprintf(r->servo.backup, sizeof(r->servo.backup), "%s", name);
    }
}

static void ask_leave(servo_limrun_t *r, uint32_t now_ms)
{
    r->phase    = SERVO_LIMRUN_PH_LEAVE;
    r->phase_ms = now_ms;
    r->pend.leave = true;
}

/** @brief NEUTRAL between PULSE LOWER and PULSE UPPER. */
static bool pulses_ordered(const uint16_t *p)
{
    return p[SERVO_LIMRUN_P_LOWER] < p[SERVO_LIMRUN_P_NEUTRAL]
        && p[SERVO_LIMRUN_P_NEUTRAL] < p[SERVO_LIMRUN_P_UPPER];
}

/**
 * @brief The read session's pulses stand: the search's ends, within the bench's
 * PULSE MIN and PULSE MAX, and the port given back.  The bench stays SAFE
 * where they are not. */
static void read_done(servo_limrun_t *r, uint32_t now_ms)
{
    const uint16_t *p = r->pulse_now;
    if (!pulses_ordered(p)) {
        fail(r, SERVO_LIMRUN_WHY_NEUTRAL, now_ms);
        return;
    }
    if ((uint32_t)p[SERVO_LIMRUN_P_LOWER] < (uint32_t)r->cfg.min_us * 10u
        || (uint32_t)p[SERVO_LIMRUN_P_UPPER] > (uint32_t)r->cfg.max_us * 10u) {
        fail(r, SERVO_LIMRUN_WHY_RANGE, now_ms);
        return;
    }
    search_span(r);
    ask_leave(r, now_ms);
}

/** @brief The first CONNECT: the servo as it is. */
static void first_read(servo_limrun_t *r, const servo_limrun_servo_t *sv,
                       uint32_t now_ms)
{
    if (!sv->ok || sv->lo_raw > sv->hi_raw
        || sv->angle[0] < sv->lo_raw || sv->angle[1] < sv->lo_raw) {
        /** @brief No servo, or values the hard rules do not take. */
        fail(r, SERVO_LIMRUN_WHY_NOT_KST, now_ms);
        return;
    }
    r->read_once = true;
    r->servo     = *sv;
    r->cur[0]    = sv->angle[0];
    r->cur[1]    = sv->angle[1];
    memcpy(r->pulse_now, sv->pulse_dus, sizeof(r->pulse_now));
    if (!pulses_ordered(r->pulse_now)) {
        fail(r, SERVO_LIMRUN_WHY_NEUTRAL, now_ms);
        return;
    }
    if (r->cfg.set_pulses
        && (r->pulse_now[SERVO_LIMRUN_P_LOWER] != SERVO_LIMRUN_STD_LOWER_DUS
            || r->pulse_now[SERVO_LIMRUN_P_UPPER]
                   != SERVO_LIMRUN_STD_UPPER_DUS)) {
        /**
         * @brief SERVO PULSES 1000-2000: the pulse write before anything moves.
         */
        r->pulse_op = true;
        r->phase    = SERVO_LIMRUN_PH_WRITE;
        r->phase_ms = now_ms;
        r->pend.pulses = true;
        r->pend.pulse_dus[0] = SERVO_LIMRUN_STD_LOWER_DUS;
        r->pend.pulse_dus[1] = SERVO_LIMRUN_STD_UPPER_DUS;
        return;
    }
    read_done(r, now_ms);
}

void servo_limrun_connected(servo_limrun_t *r, const servo_limrun_servo_t *sv,
                       uint32_t now_ms)
{
    if (!servo_limrun_running(r) || r->phase != SERVO_LIMRUN_PH_CONNECT
        || sv == NULL) {
        return;
    }
    ++r->sessions;
    if (!r->read_once) {
        first_read(r, sv, now_ms);
        return;
    }
    if (!sv->ok) {
        abort_why(r, SERVO_LIMRUN_WHY_PROG, now_ms);
        return;
    }
    if (sv->angle[0] != r->cur[0] || sv->angle[1] != r->cur[1]
        || sv->type != r->servo.type || sv->reversion != r->servo.reversion
        || sv->lo_raw != r->servo.lo_raw || sv->hi_raw != r->servo.hi_raw
        || memcmp(sv->pulse_dus, r->pulse_now, sizeof(r->pulse_now)) != 0) {
        /* Another servo, or one set otherwise: the side each register
         * limits and the values held before the run are not its. */
        abort_why(r, SERVO_LIMRUN_WHY_OTHER_VALUES, now_ms);
        return;
    }
    uint16_t want[2];
    int8_t search = -1;
    plan(r, want, &search);
    r->probe_side = search;
    if (changes(r, want) == 0u) {
        ask_leave(r, now_ms);
        return;
    }
    if (r->writes + changes(r, want) > SERVO_LIMRUN_WRITES_MAX) {
        fail(r, SERVO_LIMRUN_WHY_WRITE_CAP, now_ms);
        return;
    }
    r->want[0]  = want[0];
    r->want[1]  = want[1];
    r->pulse_op = false;
    r->phase    = SERVO_LIMRUN_PH_WRITE;
    r->phase_ms = now_ms;
    r->pend.write    = true;
    r->pend.angle[0] = want[0];
    r->pend.angle[1] = want[1];
}

static void mark_programmed(servo_limrun_t *r)
{
    r->programmed = r->cur[0] != r->servo.angle[0]
                    || r->cur[1] != r->servo.angle[1]
                    || memcmp(r->pulse_now, r->servo.pulse_dus,
                              sizeof(r->pulse_now)) != 0;
}

/**
 * @brief A write that failed once sent -- no answer, or one the programmer did
 *        not
 * take: part of it may have landed, so the servo's values count as changed
 * and RESTORE is offered.  A write refused before it was sent is
 * servo_limrun_refused()'s. */
static void sent_failed(servo_limrun_t *r, uint32_t now_ms)
{
    r->write_ops += 1u;
    r->programmed = true;
    abort_why(r, SERVO_LIMRUN_WHY_WRITE, now_ms);
}

void servo_limrun_written(servo_limrun_t *r, bool ok, const uint16_t *angle,
                          uint32_t now_ms)
{
    if (!servo_limrun_running(r) || r->phase != SERVO_LIMRUN_PH_WRITE
        || r->pulse_op) {
        return;
    }
    if (!ok || angle == NULL) {
        sent_failed(r, now_ms);
        return;
    }
    const unsigned n = changes(r, r->want);
    r->writes    += n;
    r->write_ops += 1u;
    r->cur[0] = angle[0];
    r->cur[1] = angle[1];
    mark_programmed(r);
    if (angle[0] != r->want[0] || angle[1] != r->want[1]) {
        abort_why(r, SERVO_LIMRUN_WHY_OTHER_VALUES, now_ms);
        return;
    }
    /** @brief The values are in: each side's stage moves on. */
    for (uint8_t s = 0; s < 2u; ++s) {
        if (!r->mapped && s != r->map_side) {
            continue;
        }
        if (r->stage[s] == ST_OVERSET) {
            r->stage[s] = ST_OVERSET_SEARCH;
        } else if (r->stage[s] == ST_FINAL) {
            r->stage[s] = ST_DONE;
        }
    }
    ask_leave(r, now_ms);
}

void servo_limrun_pulsed(servo_limrun_t *r, bool ok,
                         const uint16_t *pulse_dus, uint32_t now_ms)
{
    if (!servo_limrun_running(r) || r->phase != SERVO_LIMRUN_PH_WRITE
        || !r->pulse_op) {
        return;
    }
    if (!ok || pulse_dus == NULL) {
        sent_failed(r, now_ms);
        return;
    }
    r->writes += (unsigned)(pulse_dus[SERVO_LIMRUN_P_LOWER]
                            != r->pulse_now[SERVO_LIMRUN_P_LOWER])
                 + (unsigned)(pulse_dus[SERVO_LIMRUN_P_UPPER]
                              != r->pulse_now[SERVO_LIMRUN_P_UPPER]);
    r->write_ops += 1u;
    memcpy(r->pulse_now, pulse_dus, sizeof(r->pulse_now));
    mark_programmed(r);
    r->pulse_op = false;
    if (pulse_dus[SERVO_LIMRUN_P_LOWER] != SERVO_LIMRUN_STD_LOWER_DUS
        || pulse_dus[SERVO_LIMRUN_P_UPPER] != SERVO_LIMRUN_STD_UPPER_DUS) {
        abort_why(r, SERVO_LIMRUN_WHY_OTHER_VALUES, now_ms);
        return;
    }
    read_done(r, now_ms);
}

void servo_limrun_left(servo_limrun_t *r, bool ok, uint32_t now_ms)
{
    if (!servo_limrun_running(r) || r->phase != SERVO_LIMRUN_PH_LEAVE) {
        return;
    }
    if (!ok) {
        abort_why(r, SERVO_LIMRUN_WHY_PROG, now_ms);
        return;
    }
    r->phase    = SERVO_LIMRUN_PH_ARM;
    r->phase_ms = now_ms;
    r->pend.arm = true;
}

void servo_limrun_refused(servo_limrun_t *r, uint32_t now_ms)
{
    if (!servo_limrun_running(r)) {
        return;
    }
    if (r->phase == SERVO_LIMRUN_PH_WRITE) {
        abort_why(r, SERVO_LIMRUN_WHY_WRITE, now_ms);
    } else if (r->phase == SERVO_LIMRUN_PH_CONNECT
               || r->phase == SERVO_LIMRUN_PH_LEAVE) {
        abort_why(r, SERVO_LIMRUN_WHY_PROG, now_ms);
    }
}

void servo_limrun_step(servo_limrun_t *r, uint32_t now_ms,
                       const servo_limrun_in_t *in, servo_limrun_do_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (r == NULL) {
        return;
    }
    if (servo_limrun_running(r) && in != NULL) {
        r->enc_valid = in->enc_valid;
        r->enc_deg   = in->enc_deg;
        /* The meter's silence: at SERVO_LIMRUN_WIN_STALE_MS without a
         * window, not after it. */
        if (due(now_ms, r->last_win_ms, SERVO_LIMRUN_WIN_STALE_MS)) {
            servo_limrun_abort(r, SERVO_TEST_AB_WIN_STALE, now_ms);
        }
    }
    if (servo_limrun_running(r) && in != NULL) {
        switch (r->phase) {
        case SERVO_LIMRUN_PH_SEARCH:
        case SERVO_LIMRUN_PH_PROBE:
        case SERVO_LIMRUN_PH_CHECK:
            if (!in->live) {
                servo_limrun_abort(r, SERVO_TEST_AB_DISARMED, now_ms);
            } else if (due(now_ms, r->search_ms, SERVO_LIMRUN_SEARCH_MS)) {
                abort_why(r, SERVO_LIMRUN_WHY_SEARCH_LONG, now_ms);
            }
            break;
        case SERVO_LIMRUN_PH_SAFE:
            if (!r->safe_asked) {
                if (!in->live) {
                    servo_limrun_abort(r, SERVO_TEST_AB_DISARMED, now_ms);
                } else if (due(now_ms, r->phase_ms,
                               SERVO_LIMRUN_RETURN_MS)) {
                    r->safe_asked  = true;
                    r->phase_ms    = now_ms;
                    r->pend.disarm = true;
                }
            } else if (in->safe) {
                r->phase    = SERVO_LIMRUN_PH_CONNECT;
                r->phase_ms = now_ms;
                r->pend.connect = true;
            } else if (due(now_ms, r->phase_ms, SERVO_LIMRUN_BENCH_MS)) {
                abort_why(r, SERVO_LIMRUN_WHY_BENCH, now_ms);
            }
            break;
        case SERVO_LIMRUN_PH_CONNECT:
        case SERVO_LIMRUN_PH_WRITE:
        case SERVO_LIMRUN_PH_LEAVE:
            if (due(now_ms, r->phase_ms, SERVO_LIMRUN_PROG_MS)) {
                abort_why(r, SERVO_LIMRUN_WHY_PROG, now_ms);
            }
            break;
        case SERVO_LIMRUN_PH_ARM:
            if (in->live) {
                if (!r->searched) {
                    /** @brief The read session is over: the first search. */
                    begin_search(r, SERVO_LIMRUN_LO, SERVO_LIMRUN_PH_SEARCH,
                                 now_ms);
                } else if (r->probe_side < 0) {
                    begin_check(r, now_ms);
                } else {
                    begin_search(r, (uint8_t)r->probe_side,
                                 SERVO_LIMRUN_PH_PROBE, now_ms);
                }
            } else if (due(now_ms, r->phase_ms, SERVO_LIMRUN_BENCH_MS)) {
                abort_why(r, SERVO_LIMRUN_WHY_BENCH, now_ms);
            }
            break;
        default:
            break;
        }
    }
    if (out == NULL) {
        return;
    }
    *out = r->pend;
    memset(&r->pend, 0, sizeof(r->pend));
    if (servo_limrun_running(r) && r->cmd_due) {
        /** @brief A search's position, or PULSE CENTRE after one. */
        out->command = true;
        out->cmd_us  = r->cmd_us;
        r->cmd_due   = false;
    }
}
