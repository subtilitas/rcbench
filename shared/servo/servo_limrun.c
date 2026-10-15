/*
 * The limit run: see servo_limrun.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_limrun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What the next session of the programmer is for. */
typedef enum {
    PLAN_PROBE = 0,   /* a bisection value, then a search of its side   */
    PLAN_FINAL,       /* the final values, then both sides searched     */
    PLAN_FAIL_CAP,    /* no room for a value a side still needs         */
} plan_t;

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

uint16_t servo_limrun_final(uint16_t v, uint16_t lo)
{
    unsigned m = (unsigned)v * SERVO_LIMRUN_MARGIN_PCT / 100u;
    if (m < SERVO_LIMRUN_MARGIN_MIN) {
        m = SERVO_LIMRUN_MARGIN_MIN;
    }
    if ((unsigned)v < (unsigned)lo + m) {
        return lo < v ? lo : v;
    }
    return (uint16_t)(v - m);
}

/* ------------------------------------------------------------ the end */

/*
 * The run is over.  It lets the servo go; aborted, it also asks the
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

/* ------------------------------------------------------- the searches */

static void begin_search(servo_limrun_t *r, uint8_t side,
                         servo_limrun_phase_t ph, uint32_t now_ms)
{
    servo_limit_cfg_t c;
    servo_limit_defaults(&c, r->cfg.centre_us, side == SERVO_LIMRUN_HI);
    c.max_travel_us = (side == SERVO_LIMRUN_HI)
                          ? (uint16_t)(r->cfg.max_us - r->cfg.centre_us)
                          : (uint16_t)(r->cfg.centre_us - r->cfg.min_us);
    servo_limit_init(&r->lf, &c);
    r->side      = side;
    r->phase     = ph;
    r->phase_ms  = now_ms;
    r->search_ms = now_ms;
    r->cmd_us    = r->cfg.centre_us;
    r->cmd_due   = true;
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
    s.baseline_a = r->lf.baseline_a;
    s.knee_a     = r->lf.knee_a;
    return s;
}

/* How far @p now lies outward of @p was on @p side, us: positive further
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

/* The bisection's next value for @p side: the middle of what is left, or
 * the smallest value itself once nothing between is left untried. */
static uint16_t candidate(const servo_limrun_t *r, uint8_t side)
{
    const uint16_t lo = r->lo[side], hi = r->hi[side];
    if (hi - lo <= (int)SERVO_LIMRUN_RES_RAW) {
        return lo;
    }
    return (uint16_t)(lo + (hi - lo) / 2u);
}

static unsigned changes(const servo_limrun_t *r, const uint16_t *want)
{
    return (unsigned)(want[0] != r->cur[0]) + (unsigned)(want[1] != r->cur[1]);
}

static void settle_side(servo_limrun_t *r, uint8_t side)
{
    r->settled[side] = true;
    r->final_v[side] = servo_limrun_final(r->lo[side], r->servo.lo_raw);
}

/*
 * The next session's values.  Before the mapping is known: the register
 * chosen for it lowered (r->probe_value), the other as it is.  After: each
 * register of a side that never bound back at the servo's own value, each
 * settled side at its final value, and the first side still bisected at
 * its next value.  A bisection value that would leave less than
 * SERVO_LIMRUN_WRITES_KEEP writes ends the bisection: a side with a free
 * value settles on it, and one without fails the run.
 */
static plan_t plan(servo_limrun_t *r, uint16_t *want, uint8_t *side)
{
    want[0] = r->cur[0];
    want[1] = r->cur[1];
    if (!r->mapped) {
        /* The mapping: the register tried at its smallest value; the
         * second try keeps the first one there too. */
        want[r->map_reg] = r->servo.lo_raw;
        *side = r->probe_side;
        return PLAN_PROBE;
    }
    for (int g = 0; g < 2; ++g) {
        const int8_t s = r->reg_side[g];
        if (s < 0) {
            continue;
        }
        if (!r->need[s]) {
            want[g] = r->servo.angle[g];
        } else if (r->settled[s]) {
            want[g] = r->final_v[s];
        }
    }
    for (uint8_t s = 0; s < 2u; ++s) {
        if (!r->need[s] || r->settled[s]) {
            continue;
        }
        uint16_t trial[2] = { want[0], want[1] };
        trial[reg_of(r, s)] = candidate(r, s);
        if (r->writes + changes(r, trial) + SERVO_LIMRUN_WRITES_KEEP
            <= SERVO_LIMRUN_WRITES_MAX) {
            want[0] = trial[0];
            want[1] = trial[1];
            *side   = s;
            return PLAN_PROBE;
        }
        /* No room: every side still bisected takes its free value. */
        for (uint8_t k = 0; k < 2u; ++k) {
            if (r->need[k] && !r->settled[k]) {
                if (!r->lo_free[k]) {
                    return PLAN_FAIL_CAP;
                }
                settle_side(r, k);
                want[reg_of(r, k)] = r->final_v[k];
            }
        }
        break;
    }
    if (r->writes + changes(r, want) > SERVO_LIMRUN_WRITES_MAX) {
        return PLAN_FAIL_CAP;
    }
    return PLAN_FINAL;
}

/* A session next: the servo back to PULSE CENTRE first, SAFE asked once it
 * had SERVO_LIMRUN_RETURN_MS to get there (servo_limrun_step()). */
static void go_safe(servo_limrun_t *r, uint32_t now_ms)
{
    r->phase      = SERVO_LIMRUN_PH_SAFE;
    r->phase_ms   = now_ms;
    r->safe_asked = false;
    r->cmd_us     = r->cfg.centre_us;
    r->cmd_due    = true;
}

static void begin_check(servo_limrun_t *r, uint32_t now_ms)
{
    memset(r->after, 0, sizeof(r->after));
    begin_search(r, SERVO_LIMRUN_LO, SERVO_LIMRUN_PH_CHECK, now_ms);
}

/* After a search of the bound side with a register lowered: what that says
 * of the bisection, of the mapping, and what comes next. */
static void apply(servo_limrun_t *r, uint8_t side, uint16_t value,
                  bool free_now, uint32_t now_ms)
{
    if (free_now) {
        r->lo[side]      = value;
        r->lo_free[side] = true;
    } else if (value <= r->lo[side]) {
        /* The smallest value the protocol takes, and still bound. */
        fail(r, SERVO_LIMRUN_WHY_SMALLEST, now_ms);
        return;
    } else {
        r->hi[side] = value;
    }
    if (r->lo_free[side] && r->hi[side] - r->lo[side]
                                <= (int)SERVO_LIMRUN_RES_RAW) {
        settle_side(r, side);
    }
}

/* The mapping is known: each side bisects from the smallest value up to
 * the value its register held. */
static void map_done(servo_limrun_t *r)
{
    r->mapped = true;
    for (uint8_t s = 0; s < 2u; ++s) {
        r->lo[s] = r->servo.lo_raw;
        r->hi[s] = r->servo.angle[reg_of(r, s)];
    }
}

static void next_after_probe(servo_limrun_t *r, uint32_t now_ms)
{
    const servo_limrun_side_t *res = &r->probe;
    const uint8_t side = r->probe_side;

    if (!r->mapped) {
        const int32_t out = outward(side, res, &r->first[side]);
        const bool moved = !res->bound || out >= (int32_t)SERVO_LIMRUN_MOVED_US;
        const bool inward = res->bound && out <= -(int32_t)SERVO_LIMRUN_MOVED_US;
        const int  other_reg = 1 - (int)r->map_reg;
        if (inward) {
            fail(r, SERVO_LIMRUN_WHY_MAPPING, now_ms);
            return;
        }
        if (!moved) {
            if (r->map_both || r->cur[other_reg] <= r->servo.lo_raw) {
                /* Both registers at their smallest value, and the end
                 * binds where it did. */
                fail(r, SERVO_LIMRUN_WHY_SMALLEST, now_ms);
                return;
            }
            /* The other register to its smallest value as well. */
            r->map_both = true;
            r->map_reg  = (uint8_t)other_reg;
            go_safe(r, now_ms);
            return;
        }
        r->reg_side[r->map_reg] = (int8_t)side;
        r->reg_side[other_reg]  = (int8_t)(1 - side);
        map_done(r);
        apply(r, side, r->servo.lo_raw, !res->bound, now_ms);
        if (!servo_limrun_running(r)) {
            return;
        }
    } else {
        apply(r, side, r->probe_value, !res->bound, now_ms);
        if (!servo_limrun_running(r)) {
            return;
        }
    }

    uint16_t want[2];
    uint8_t s = 0u;
    const plan_t p = plan(r, want, &s);
    if (p == PLAN_FAIL_CAP) {
        fail(r, SERVO_LIMRUN_WHY_WRITE_CAP, now_ms);
    } else if (p == PLAN_FINAL && changes(r, want) == 0u) {
        begin_check(r, now_ms);
    } else {
        go_safe(r, now_ms);
    }
}

static void after_first_search(servo_limrun_t *r, uint32_t now_ms)
{
    r->need[SERVO_LIMRUN_LO] = r->first[SERVO_LIMRUN_LO].bound;
    r->need[SERVO_LIMRUN_HI] = r->first[SERVO_LIMRUN_HI].bound;
    if (!r->need[SERVO_LIMRUN_LO] && !r->need[SERVO_LIMRUN_HI]) {
        finish(r, SERVO_LIMRUN_FREE, SERVO_LIMRUN_WHY_NONE, now_ms);
    } else if (!r->cfg.program) {
        finish(r, SERVO_LIMRUN_FOUND, SERVO_LIMRUN_WHY_NONE, now_ms);
    } else {
        go_safe(r, now_ms);
    }
}

/* The search under way has ended. */
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
    /* Back to the centre from the end it reached, whatever comes next. */
    r->cmd_us  = r->cfg.centre_us;
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
        } else if (r->after[0].bound || r->after[1].bound) {
            fail(r, SERVO_LIMRUN_WHY_STILL_BINDS, now_ms);
        } else {
            finish(r, SERVO_LIMRUN_PASS, SERVO_LIMRUN_WHY_NONE, now_ms);
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

/* ------------------------------------------------------------- inputs */

bool servo_limrun_start(servo_limrun_t *r, const servo_limrun_cfg_t *cfg,
                        uint32_t now_ms)
{
    if (r == NULL || cfg == NULL || servo_limrun_running(r)
        || !(cfg->min_us < cfg->centre_us && cfg->centre_us < cfg->max_us)) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    r->cfg         = *cfg;
    r->state       = SERVO_LIMRUN_RUNNING;
    r->start_ms    = now_ms;
    r->last_win_ms = now_ms;
    r->reg_side[0] = -1;
    r->reg_side[1] = -1;
    begin_search(r, SERVO_LIMRUN_LO, SERVO_LIMRUN_PH_SEARCH, now_ms);
    return true;
}

static int16_t mag(int16_t ma)
{
    return (int16_t)(ma < 0 ? -(int32_t)ma : ma);
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
        /* A supply that answers at the start is watched from here on. */
        if (s->online && s->output) {
            r->sup_watch = true;
            r->sup_v     = s->set_v;
            r->sup_i     = s->set_i;
        }
        return;
    }
    if (!s->online) {
        servo_limrun_abort(r, SERVO_TEST_AB_SUPPLY_LOST, now_ms);
    } else if (s->trip != 0u) {
        servo_limrun_abort(r, SERVO_TEST_AB_TRIPPED, now_ms);
    } else if (!s->output) {
        servo_limrun_abort(r, SERVO_TEST_AB_SUPPLY_OFF, now_ms);
    } else if (s->set_v > r->sup_v + 0.005f || s->set_v < r->sup_v - 0.005f
               || s->set_i > r->sup_i + 0.0005f
               || s->set_i < r->sup_i - 0.0005f) {
        abort_why(r, SERVO_LIMRUN_WHY_SUPPLY_CHANGED, now_ms);
    }
}

void servo_limrun_backup(servo_limrun_t *r, const char *name)
{
    if (r != NULL && r->read_once && name != NULL && name[0] != '\0') {
        snprintf(r->servo.backup, sizeof(r->servo.backup), "%s", name);
    }
}

void servo_limrun_connected(servo_limrun_t *r, const servo_limrun_servo_t *sv,
                       uint32_t now_ms)
{
    if (!servo_limrun_running(r) || r->phase != SERVO_LIMRUN_PH_CONNECT
        || sv == NULL) {
        return;
    }
    ++r->sessions;
    if (!sv->ok) {
        if (!r->read_once) {
            fail(r, SERVO_LIMRUN_WHY_NOT_KST, now_ms);
        } else {
            abort_why(r, SERVO_LIMRUN_WHY_PROG, now_ms);
        }
        return;
    }
    if (!r->read_once) {
        r->read_once = true;
        r->servo     = *sv;
        r->cur[0]    = sv->angle[0];
        r->cur[1]    = sv->angle[1];
        /* The register the mapping lowers: LEFT ANGLE, or RIGHT ANGLE
         * where LEFT is at the smallest value already. */
        if (sv->lo_raw > sv->hi_raw
            || sv->angle[0] < sv->lo_raw || sv->angle[1] < sv->lo_raw) {
            /* Values the hard rules do not take: not one to lower. */
            fail(r, SERVO_LIMRUN_WHY_NOT_KST, now_ms);
            return;
        }
        if (sv->angle[SERVO_LIMRUN_LEFT] > sv->lo_raw) {
            r->map_reg = SERVO_LIMRUN_LEFT;
        } else if (sv->angle[SERVO_LIMRUN_RIGHT] > sv->lo_raw) {
            r->map_reg = SERVO_LIMRUN_RIGHT;
            r->map_both = true;     /* LEFT ANGLE is at its smallest */
        } else {
            fail(r, SERVO_LIMRUN_WHY_SMALLEST, now_ms);
            return;
        }
        r->probe_value = sv->lo_raw;
        r->probe_side  = r->need[SERVO_LIMRUN_LO] ? SERVO_LIMRUN_LO
                                                  : SERVO_LIMRUN_HI;
        /* The first session reads and gives the port back: a LEAVE that
         * fails on this link or supply ends the run before a value
         * changes. */
        r->dry      = true;
        r->phase    = SERVO_LIMRUN_PH_LEAVE;
        r->phase_ms = now_ms;
        r->pend.leave = true;
        return;
    } else if (sv->angle[0] != r->cur[0] || sv->angle[1] != r->cur[1]
               || sv->type != r->servo.type
               || sv->reversion != r->servo.reversion
               || sv->lo_raw != r->servo.lo_raw
               || sv->hi_raw != r->servo.hi_raw) {
        /* Another servo, or one set otherwise: the side each register
         * limits and the values held before the run are not its. */
        abort_why(r, SERVO_LIMRUN_WHY_OTHER_VALUES, now_ms);
        return;
    }
    uint16_t want[2];
    uint8_t side = 0u;
    const plan_t p = plan(r, want, &side);
    if (p == PLAN_FAIL_CAP) {
        fail(r, SERVO_LIMRUN_WHY_WRITE_CAP, now_ms);
        return;
    }
    r->final_plan = p == PLAN_FINAL;
    if (p == PLAN_PROBE) {
        r->probe_side = side;
        if (r->mapped) {
            r->probe_value = want[reg_of(r, side)];
        }
    }
    if (changes(r, want) == 0u) {
        r->phase    = SERVO_LIMRUN_PH_LEAVE;
        r->phase_ms = now_ms;
        r->pend.leave = true;
        return;
    }
    r->want[0]  = want[0];
    r->want[1]  = want[1];
    r->phase    = SERVO_LIMRUN_PH_WRITE;
    r->phase_ms = now_ms;
    r->pend.write    = true;
    r->pend.angle[0] = want[0];
    r->pend.angle[1] = want[1];
}

void servo_limrun_written(servo_limrun_t *r, bool ok, const uint16_t *angle,
                          uint32_t now_ms)
{
    if (!servo_limrun_running(r) || r->phase != SERVO_LIMRUN_PH_WRITE) {
        return;
    }
    if (!ok || angle == NULL) {
        abort_why(r, SERVO_LIMRUN_WHY_WRITE, now_ms);
        return;
    }
    const unsigned n = changes(r, r->want);
    r->writes    += n;
    r->write_ops += 1u;
    r->cur[0] = angle[0];
    r->cur[1] = angle[1];
    r->programmed = r->cur[0] != r->servo.angle[0]
                    || r->cur[1] != r->servo.angle[1];
    if (angle[0] != r->want[0] || angle[1] != r->want[1]) {
        abort_why(r, SERVO_LIMRUN_WHY_OTHER_VALUES, now_ms);
        return;
    }
    r->phase    = SERVO_LIMRUN_PH_LEAVE;
    r->phase_ms = now_ms;
    r->pend.leave = true;
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
    if (r->dry) {
        /* The read-only session is over: the first writing one. */
        r->dry      = false;
        r->phase    = SERVO_LIMRUN_PH_CONNECT;
        r->phase_ms = now_ms;
        r->pend.connect = true;
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
                if (r->final_plan) {
                    begin_check(r, now_ms);
                } else {
                    begin_search(r, r->probe_side, SERVO_LIMRUN_PH_PROBE,
                                 now_ms);
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
        /* A search's position, or PULSE CENTRE after one. */
        out->command = true;
        out->cmd_us  = r->cmd_us;
        r->cmd_due   = false;
    }
}

/* ------------------------------------------------------------ the outbox */

servo_test_out_t servo_limrun_peek(servo_limrun_t *r, const char **text)
{
    if (text != NULL) {
        *text = "";
    }
    if (r == NULL || r->state != SERVO_LIMRUN_DONE || r->out_done) {
        return SERVO_TEST_OUT_NONE;
    }
    if (!r->out_open) {
        return SERVO_TEST_OUT_OPEN_TXT;
    }
    if (!r->out_end) {
        if (servo_limrun_report_line(r, r->out_line, r->scratch,
                                     sizeof(r->scratch))) {
            if (text != NULL) {
                *text = r->scratch;
            }
            return SERVO_TEST_OUT_TXT;
        }
        r->out_end = true;
    }
    return SERVO_TEST_OUT_END;
}

void servo_limrun_pop(servo_limrun_t *r)
{
    if (r == NULL || r->state != SERVO_LIMRUN_DONE || r->out_done) {
        return;
    }
    if (!r->out_open) {
        r->out_open = true;
    } else if (!r->out_end) {
        ++r->out_line;
    } else {
        r->out_done = true;
    }
}

bool servo_limrun_drained(const servo_limrun_t *r)
{
    return r == NULL || r->state == SERVO_LIMRUN_IDLE
        || (r->state == SERVO_LIMRUN_DONE && r->out_done);
}

/* ------------------------------------------------------------ the report */

/*
 * One line at a time, found by counting, as the automatic test's report is
 * (servo_report.c): a cursor walks the lines in order and prints the one
 * asked for.
 */
typedef struct {
    unsigned want, at;
    char    *buf;
    size_t   n;
    const char *const *text;
} cur_t;

#define LS(id) servo_str_in(c->text, SERVO_STR_##id)

static bool here(cur_t *c)
{
    return c->want == c->at++;
}

static bool blank(cur_t *c)
{
    if (here(c)) {
        c->buf[0] = '\0';
        return true;
    }
    return false;
}

static bool text_line(cur_t *c, servo_str_t id)
{
    if (here(c)) {
        snprintf(c->buf, c->n, "%s", servo_str_in(c->text, id));
        return true;
    }
    return false;
}

static bool end_lines(const servo_limrun_t *r, cur_t *c,
                      const servo_limrun_side_t *e, uint8_t side)
{
    const char *label = side == SERVO_LIMRUN_LO ? LS(L_R_LOW) : LS(L_R_HIGH);
    if (here(c)) {
        if (!e->done) {
            snprintf(c->buf, c->n, LS(L_R_END_NOT_RUN), label);
        } else if (e->bound) {
            snprintf(c->buf, c->n, LS(L_R_END_BOUND), label,
                     (unsigned)e->bind_us, (unsigned)e->limit_us,
                     (double)e->baseline_a, (double)e->knee_a);
        } else {
            snprintf(c->buf, c->n, LS(L_R_END_FREE), label,
                     (unsigned)e->limit_us, (double)e->baseline_a);
        }
        return true;
    }
    if (r->cfg.enc_on && e->done && here(c)) {
        if (e->enc) {
            snprintf(c->buf, c->n, LS(L_R_ANGLE), (double)e->enc_deg);
        } else {
            snprintf(c->buf, c->n, "%s", LS(L_R_ANGLE_NONE));
        }
        return true;
    }
    return false;
}

static bool head_lines(const servo_limrun_t *r, cur_t *c)
{
    const servo_limrun_cfg_t *g = &r->cfg;
    if (text_line(c, SERVO_STR_L_R_TITLE)) {
        return true;
    }
    if (here(c)) {
        const char *v = servo_str_in(c->text,
                                     servo_limrun_verdict_str(r->verdict));
        if (r->why != SERVO_LIMRUN_WHY_NONE) {
            snprintf(c->buf, c->n, LS(R_RESULT_WHY), v,
                     servo_str_in(c->text, servo_limrun_why_str(r)));
        } else {
            snprintf(c->buf, c->n, LS(R_RESULT), v);
        }
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(R_DEVICE), g->dut);
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(R_FIRMWARE), g->firmware);
        return true;
    }
    if (text_line(c, g->program ? SERVO_STR_L_R_MODE_PROGRAM
                                : SERVO_STR_L_R_MODE_SEARCH)) {
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_OUTPUT), g->output,
                 (unsigned)g->frame_hz);
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_PULSE), (unsigned)g->centre_us,
                 (unsigned)g->min_us, (unsigned)g->max_us);
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_METER),
                 (double)g->shunt_dmohm / 10.0);
        return true;
    }
    if (here(c)) {
        servo_limit_cfg_t d;
        servo_limit_defaults(&d, g->centre_us, true);
        snprintf(c->buf, c->n, LS(L_R_RULE), (double)d.knee_ratio,
                 (double)d.knee_margin_a);
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_PROTECT),
                 (double)SERVO_LIMRUN_HARD_MA / 1000.0,
                 (double)SERVO_LIMRUN_STALL_MA / 1000.0,
                 (unsigned)SERVO_LIMRUN_STALL_MS,
                 (unsigned)SERVO_LIMRUN_WIN_STALE_MS,
                 (unsigned)(SERVO_LIMRUN_SEARCH_MS / 1000u));
        return true;
    }
    if (here(c)) {
        const uint32_t ms = r->end_ms - r->start_ms;
        snprintf(c->buf, c->n, LS(R_DURATION), (unsigned long)(ms / 1000u),
                 (unsigned long)((ms % 1000u) / 100u));
        return true;
    }
    if (!g->enc_on && text_line(c, SERVO_STR_L_R_ENC_OFF)) {
        return true;
    }
    if (blank(c) || text_line(c, SERVO_STR_L_R_ENDS_HEAD)) {
        return true;
    }
    return end_lines(r, c, &r->first[SERVO_LIMRUN_LO], SERVO_LIMRUN_LO)
        || end_lines(r, c, &r->first[SERVO_LIMRUN_HI], SERVO_LIMRUN_HI);
}

static servo_str_t side_word(const servo_limrun_t *r, int g)
{
    switch (r->reg_side[g]) {
    case SERVO_LIMRUN_LO: return SERVO_STR_L_R_SIDE_LO;
    case SERVO_LIMRUN_HI: return SERVO_STR_L_R_SIDE_HI;
    default:              return SERVO_STR_L_R_SIDE_UNKNOWN;
    }
}

static bool reg_lines(const servo_limrun_t *r, cur_t *c, int g)
{
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_REG),
                 g == SERVO_LIMRUN_LEFT ? LS(L_R_LEFT) : LS(L_R_RIGHT),
                 (unsigned)r->servo.angle[g], (unsigned)r->cur[g],
                 servo_str_in(c->text, side_word(r, g)));
        return true;
    }
    const int8_t s = r->reg_side[g];
    if (s >= 0 && r->need[s] && r->lo_free[s] && here(c)) {
        snprintf(c->buf, c->n, LS(L_R_REG_BIS), (unsigned)r->lo[s],
                 (unsigned)r->hi[s]);
        return true;
    }
    return false;
}

static bool prog_lines(const servo_limrun_t *r, cur_t *c)
{
    if (blank(c) || text_line(c, SERVO_STR_L_R_PROG_HEAD)) {
        return true;
    }
    if (!r->cfg.program) {
        return text_line(c, SERVO_STR_L_R_PROG_OFF);
    }
    if (!r->read_once) {
        return text_line(c, r->verdict == SERVO_LIMRUN_FREE
                                ? SERVO_STR_L_R_PROG_FREE
                                : SERVO_STR_L_R_PROG_NO_SERVO);
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_SERVO), r->servo.type,
                 r->servo.reversion ? LS(R_ON) : LS(R_OFF));
        return true;
    }
    if (reg_lines(r, c, SERVO_LIMRUN_LEFT)
        || reg_lines(r, c, SERVO_LIMRUN_RIGHT)) {
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_MARGIN),
                 (unsigned)SERVO_LIMRUN_MARGIN_PCT,
                 (unsigned)SERVO_LIMRUN_MARGIN_MIN);
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_WRITES), r->writes, r->write_ops,
                 (unsigned)SERVO_LIMRUN_WRITES_MAX, r->sessions);
        return true;
    }
    if (here(c)) {
        if (r->servo.backup[0] != '\0') {
            snprintf(c->buf, c->n, LS(L_R_BACKUP), r->servo.backup);
        } else {
            snprintf(c->buf, c->n, "%s", LS(L_R_BACKUP_MEM));
        }
        return true;
    }
    if (r->programmed && r->verdict != SERVO_LIMRUN_PASS
        && text_line(c, SERVO_STR_L_R_RESTORE)) {
        return true;
    }
    if (blank(c) || text_line(c, SERVO_STR_L_R_CHECK_HEAD)) {
        return true;
    }
    return end_lines(r, c, &r->after[SERVO_LIMRUN_LO], SERVO_LIMRUN_LO)
        || end_lines(r, c, &r->after[SERVO_LIMRUN_HI], SERVO_LIMRUN_HI);
}

static bool tail_lines(const servo_limrun_t *r, cur_t *c)
{
    if (blank(c)) {
        return true;
    }
    if (r->cfg.program && text_line(c, SERVO_STR_L_R_UNIT)) {
        return true;
    }
    return text_line(c, SERVO_STR_L_R_BENCH);
}

bool servo_limrun_report_line(const servo_limrun_t *r, unsigned idx,
                              char *buf, size_t n)
{
    if (r == NULL || buf == NULL || n == 0u
        || r->state != SERVO_LIMRUN_DONE) {
        return false;
    }
    cur_t c = { idx, 0u, buf, n, r->cfg.text };
    return head_lines(r, &c) || prog_lines(r, &c) || tail_lines(r, &c);
}

/* -------------------------------------------------------------- the words */

servo_str_t servo_limrun_verdict_str(servo_limrun_verdict_t v)
{
    switch (v) {
    case SERVO_LIMRUN_FOUND:   return SERVO_STR_L_FOUND;
    case SERVO_LIMRUN_FREE:    return SERVO_STR_L_FREE;
    case SERVO_LIMRUN_PASS:    return SERVO_STR_PASS;
    case SERVO_LIMRUN_FAIL:    return SERVO_STR_FAIL;
    default:                   return SERVO_STR_ABORTED;
    }
}

servo_str_t servo_limrun_why_str(const servo_limrun_t *r)
{
    if (r == NULL) {
        return SERVO_STR_AB_NONE;
    }
    switch (r->why) {
    case SERVO_LIMRUN_WHY_TEST:           return servo_test_abort_str(r->test_why);
    case SERVO_LIMRUN_WHY_HARD:           return SERVO_STR_L_WHY_HARD;
    case SERVO_LIMRUN_WHY_CLIPPED:        return SERVO_STR_L_WHY_CLIPPED;
    case SERVO_LIMRUN_WHY_STALL:          return SERVO_STR_L_WHY_STALL;
    case SERVO_LIMRUN_WHY_SEARCH_LONG:    return SERVO_STR_L_WHY_SEARCH_LONG;
    case SERVO_LIMRUN_WHY_SUPPLY_CHANGED: return SERVO_STR_L_WHY_SUPPLY;
    case SERVO_LIMRUN_WHY_BENCH:          return SERVO_STR_L_WHY_BENCH;
    case SERVO_LIMRUN_WHY_NOT_KST:        return SERVO_STR_L_WHY_NOT_KST;
    case SERVO_LIMRUN_WHY_PROG:           return SERVO_STR_L_WHY_PROG;
    case SERVO_LIMRUN_WHY_WRITE:          return SERVO_STR_L_WHY_WRITE;
    case SERVO_LIMRUN_WHY_OTHER_VALUES:   return SERVO_STR_L_WHY_OTHER;
    case SERVO_LIMRUN_WHY_MAPPING:        return SERVO_STR_L_WHY_MAPPING;
    case SERVO_LIMRUN_WHY_SMALLEST:       return SERVO_STR_L_WHY_SMALLEST;
    case SERVO_LIMRUN_WHY_WRITE_CAP:      return SERVO_STR_L_WHY_CAP;
    case SERVO_LIMRUN_WHY_STILL_BINDS:    return SERVO_STR_L_WHY_STILL;
    case SERVO_LIMRUN_WHY_NO_VOLTAGE:     return SERVO_STR_L_WHY_NO_VOLTAGE;
    default:                              return SERVO_STR_AB_NONE;
    }
}

servo_str_t servo_limrun_phase_str(servo_limrun_phase_t ph)
{
    switch (ph) {
    case SERVO_LIMRUN_PH_SEARCH:  return SERVO_STR_L_PH_SEARCH;
    case SERVO_LIMRUN_PH_SAFE:    return SERVO_STR_L_PH_SAFE;
    case SERVO_LIMRUN_PH_CONNECT: return SERVO_STR_L_PH_CONNECT;
    case SERVO_LIMRUN_PH_WRITE:   return SERVO_STR_L_PH_WRITE;
    case SERVO_LIMRUN_PH_LEAVE:   return SERVO_STR_L_PH_LEAVE;
    case SERVO_LIMRUN_PH_ARM:     return SERVO_STR_L_PH_ARM;
    case SERVO_LIMRUN_PH_PROBE:   return SERVO_STR_L_PH_PROBE;
    case SERVO_LIMRUN_PH_CHECK:   return SERVO_STR_L_PH_CHECK;
    default:                      return SERVO_STR_PHASE_NONE;
    }
}
