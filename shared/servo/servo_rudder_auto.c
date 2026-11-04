/**
 * @file
 * @brief AUTO end points: the run's steps, its sessions and its guards; see
 *        servo_rudder_auto.h.  The report and the words are
 *        servo_rudder_auto_report.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_rudder_auto.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/** @brief The end pulse of each side, us. */
static const uint16_t k_end_us[2] = { 1000u, 2000u };

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
 * @brief The tolerance, °.
 * @param r Run state.
 * @return cfg.tol_ddeg / 10.
 */
static float tol_deg(const servo_rauto_t *r)
{
    return (float)r->cfg.tol_ddeg / 10.0f;
}

/** @brief What a float difference of two angles may lie past the tolerance
 *         and still count as within, °: 27.6 - 27.5 in float is 0.1000004. */
#define RAUTO_EPS_DEG 0.0005f

void servo_rauto_init(servo_rauto_t *r)
{
    if (r != NULL) {
        memset(r, 0, sizeof(*r));
    }
}

bool servo_rauto_running(const servo_rauto_t *r)
{
    return r != NULL && r->state == SERVO_RAUTO_RUNNING;
}

/**
 * @brief The phases that hold the programmer.
 * @param ph The phase.
 * @return true for READ, CONNECT, WRITE and LEAVE.
 */
static bool prog_phase(servo_rauto_phase_t ph)
{
    return ph == SERVO_RAUTO_PH_READ || ph == SERVO_RAUTO_PH_CONNECT
        || ph == SERVO_RAUTO_PH_WRITE || ph == SERVO_RAUTO_PH_LEAVE;
}

/**
 * @brief The phases that measure: the servo driven to an end pulse.
 * @param ph The phase.
 * @return true for MOVE, SETTLE and CHECK.
 */
static bool measuring(servo_rauto_phase_t ph)
{
    return ph == SERVO_RAUTO_PH_MOVE || ph == SERVO_RAUTO_PH_SETTLE
        || ph == SERVO_RAUTO_PH_CHECK;
}

bool servo_rauto_switching(const servo_rauto_t *r)
{
    return servo_rauto_running(r)
        && ((r->phase == SERVO_RAUTO_PH_SAFE && r->safe_asked)
            || prog_phase(r->phase) || r->phase == SERVO_RAUTO_PH_ARM);
}

bool servo_rauto_programming(const servo_rauto_t *r)
{
    return servo_rauto_running(r) && prog_phase(r->phase);
}

bool servo_rauto_wrote(const servo_rauto_t *r)
{
    return r != NULL && r->write_ops > 0u;
}

bool servo_rauto_restore(const servo_rauto_t *r)
{
    return r != NULL && r->state == SERVO_RAUTO_DONE && r->programmed
        && r->verdict != SERVO_RAUTO_PASS;
}

uint16_t servo_rauto_scaled(uint16_t value, float goal_deg, float measured_deg,
                            uint16_t lo, uint16_t hi)
{
    if (!(measured_deg > 0.0f)) {
        return lo;
    }
    const double v = (double)value * (double)goal_deg / (double)measured_deg;
    if (!(v >= (double)lo)) {
        return lo;
    }
    if (v >= (double)hi) {
        return hi < lo ? lo : hi;
    }
    /* Under hi, so its rounding is at most hi. */
    return (uint16_t)lround(v);
}

/* ------------------------------------------------------------- the end */

/**
 * @brief The run is over.  It lets the servo go and asks SAFE; a FAIL in
 *        the read session's CONNECT asks LEAVE, every other end the output
 *        off.
 * @param r      Run state.
 * @param v      Verdict.
 * @param why    Reason.
 * @param now_ms Time, ms.
 */
static void finish(servo_rauto_t *r, servo_rauto_verdict_t v,
                   servo_rauto_why_t why, uint32_t now_ms)
{
    if (r->state != SERVO_RAUTO_RUNNING) {
        return;
    }
    /* An end that is no abort holds the port only in the read session's
     * CONNECT: a read that is no servo.  Every later step of the
     * programmer that ends the run is an abort. */
    const bool in_session = r->phase == SERVO_RAUTO_PH_READ;
    if (r->in_flight) {
        /* A write on its way when the run ended may have landed: RESTORE
         * is offered for it. */
        r->programmed = true;
        r->in_flight  = false;
    }
    r->state   = SERVO_RAUTO_DONE;
    r->verdict = v;
    r->why     = why;
    r->end_ms  = now_ms;
    memset(&r->pend, 0, sizeof(r->pend));
    r->pend.release = true;
    r->pend.disarm  = true;
    if (v != SERVO_RAUTO_ABORTED && in_session) {
        r->pend.leave = true;
    } else {
        r->pend.off = true;
    }
    r->cmd_due = false;
}

/**
 * @brief End the run as FAIL.
 * @param r      Run state.
 * @param why    Reason.
 * @param now_ms Time, ms.
 */
static void fail(servo_rauto_t *r, servo_rauto_why_t why, uint32_t now_ms)
{
    finish(r, SERVO_RAUTO_FAIL, why, now_ms);
}

/**
 * @brief End the run as ABORTED.
 * @param r      Run state.
 * @param why    Reason.
 * @param now_ms Time, ms.
 */
static void abort_why(servo_rauto_t *r, servo_rauto_why_t why,
                      uint32_t now_ms)
{
    finish(r, SERVO_RAUTO_ABORTED, why, now_ms);
}

void servo_rauto_abort(servo_rauto_t *r, servo_test_abort_t why,
                       uint32_t now_ms)
{
    if (!servo_rauto_running(r)) {
        return;
    }
    r->test_why = why;
    abort_why(r, SERVO_RAUTO_WHY_TEST, now_ms);
}

void servo_rauto_supply_changed(servo_rauto_t *r, uint32_t now_ms)
{
    if (!servo_rauto_running(r)) {
        return;
    }
    abort_why(r, SERVO_RAUTO_WHY_SUPPLY, now_ms);
}

/* ------------------------------------------------------------ the steps */

/**
 * @brief A session next: the servo to PULSE CENTRE, SAFE asked once it had
 *        SERVO_LIMRUN_RETURN_MS to get there (servo_rauto_step()).
 * @param r      Run state.
 * @param now_ms Time, ms.
 */
static void go_safe(servo_rauto_t *r, uint32_t now_ms)
{
    r->phase      = SERVO_RAUTO_PH_SAFE;
    r->phase_ms   = now_ms;
    r->safe_asked = false;
    r->cmd_us     = r->cfg.centre_us;
    r->cmd_due    = true;
}

/**
 * @brief Drive a side to its end pulse and measure it.
 * @param r      Run state.
 * @param side   0 for 1000us, 1 for 2000us.
 * @param check  A side that was within before: phase CHECK.
 * @param now_ms Time, ms.
 */
static void begin_measure(servo_rauto_t *r, uint8_t side, bool check,
                          uint32_t now_ms)
{
    r->side     = side;
    r->phase    = check ? SERVO_RAUTO_PH_CHECK : SERVO_RAUTO_PH_MOVE;
    r->phase_ms = now_ms;
    r->move_ms  = now_ms;
    r->cmd_us   = k_end_us[side];
    r->cmd_due  = true;
}

/**
 * @brief Whether a side is within its tolerance, measured after the last
 *        write.
 * @param r    Run state.
 * @param side The side.
 * @return true when it needs no further measure.
 */
static bool side_settled(const servo_rauto_t *r, uint8_t side)
{
    return r->verified[side] && r->verified_at[side] == r->total;
}

/**
 * @brief The next side to measure, or PASS when both are settled.
 * @param r      Run state.
 * @param now_ms Time, ms.
 */
static void next_measure(servo_rauto_t *r, uint32_t now_ms)
{
    for (uint8_t s = 0; s < 2u; ++s) {
        if (!side_settled(r, s)) {
            begin_measure(r, s, r->verified[s], now_ms);
            return;
        }
    }
    finish(r, SERVO_RAUTO_PASS, SERVO_RAUTO_WHY_NONE, now_ms);
}

/**
 * @brief The register that moves a side, once found.
 * @param r    Run state.
 * @param side The side.
 * @return the register, or -1 while not found.
 */
static int reg_of(const servo_rauto_t *r, uint8_t side)
{
    for (int g = 0; g < 2; ++g) {
        if (r->reg_side[g] == (int8_t)side) {
            return g;
        }
    }
    return -1;
}

/**
 * @brief A register's next value for a side measured at @p ms: the scaled
 *        value, or one count toward the goal where it rounds to the one in
 *        force.
 * @param r     Run state.
 * @param g     The register.
 * @param ms    The side's measure, along its direction, °.
 * @param goal  The side's goal, °.
 * @param next  Out: the value.
 * @return false where no value moves the side toward the goal within lo..hi.
 */
static bool next_value(const servo_rauto_t *r, int g, float ms, float goal,
                       uint16_t *next)
{
    const uint16_t v  = r->cur[g];
    const uint16_t lo = r->servo.lo, hi = r->servo.hi;
    uint16_t k = servo_rauto_scaled(v, goal, ms, lo, hi);
    if (k == v) {
        /* Rounded to the value in force, or held at a bound already. */
        const bool more = goal > ms;
        if (more && v < hi) {
            k = (uint16_t)(v + 1u);
        } else if (!more && v > lo) {
            k = (uint16_t)(v - 1u);
        } else {
            return false;
        }
    }
    *next = k;
    return true;
}

/**
 * @brief The change of a side's measure the model gives for a register's
 *        new value: ms x (next / value - 1); the way to the goal for a
 *        value of 0.
 * @param r    Run state.
 * @param g    The register.
 * @param next Its new value.
 * @param ms   The side's measure, °.
 * @param goal The side's goal, °.
 * @return the change, °.
 */
static float expect(const servo_rauto_t *r, int g, uint16_t next, float ms,
                    float goal)
{
    if (r->cur[g] == 0u) {
        return goal - ms;
    }
    return ms * ((float)next / (float)r->cur[g] - 1.0f);
}

/**
 * @brief A write session for side @p s next: its value worked out, the
 *        register tried while none is found.
 * @param r      Run state.
 * @param s      The side.
 * @param ms     Its measure along its direction, °.
 * @param now_ms Time, ms.
 */
static void plan_write(servo_rauto_t *r, uint8_t s, float ms, uint32_t now_ms)
{
    if (r->writes[s] >= SERVO_RAUTO_WRITES_MAX) {
        fail(r, SERVO_RAUTO_WHY_WRITE_CAP, now_ms);
        return;
    }
    int g = reg_of(r, s);
    const bool find = g < 0;
    if (find) {
        g = 0;                  /* the first register tried first */
    }
    uint16_t next = 0u;
    bool can = next_value(r, g, ms, r->goal_deg[s], &next);
    if (!can && find) {
        /* The first register cannot move toward the goal: the side may be
         * the second one's, which can. */
        g = 1;
        can = next_value(r, g, ms, r->goal_deg[s], &next);
    }
    if (!can) {
        fail(r, SERVO_RAUTO_WHY_LIMIT, now_ms);
        return;
    }
    r->want[g]     = next;
    r->want[1 - g] = r->cur[1 - g];
    if (find) {
        r->map_open   = true;
        r->map_reg    = (uint8_t)g;
        r->map_tries  = 1u;
        r->map_before = ms;
        r->map_expect = expect(r, g, next, ms, r->goal_deg[s]);
    }
    r->side = s;
    go_safe(r, now_ms);
}

/**
 * @brief The side measured after a write to the register tried: found,
 *        tried again on the other register, or not found.
 * @param r      Run state.
 * @param ms     The measure along the side's direction, °.
 * @param within It lies within the tolerance.
 * @param now_ms Time, ms.
 * @return true when the register is found and the run goes on as usual.
 */
static bool judge_map(servo_rauto_t *r, float ms, bool within,
                      uint32_t now_ms)
{
    const uint8_t s = r->side;
    const float e = r->map_expect;
    const float d = (ms - r->map_before) * (e < 0.0f ? -1.0f : 1.0f);
    const float half = fabsf(e) / 2.0f;
    if (within || (half > 0.0f && d >= half)) {
        r->map_open = false;
        r->reg_side[r->map_reg]      = (int8_t)s;
        r->reg_side[1u - r->map_reg] = (int8_t)(1u - s);
        return true;
    }
    if (half > 0.0f && d <= -half) {
        fail(r, SERVO_RAUTO_WHY_MAPPING, now_ms);
        return false;
    }
    /* It stayed. */
    if (r->map_tries >= 2u) {
        fail(r, SERVO_RAUTO_WHY_MAPPING, now_ms);
        return false;
    }
    /* The register is found on the run's first writes of a side: no cap
     * to reach here. */
    const int g = 1 - (int)r->map_reg;
    uint16_t next = 0u;
    if (!next_value(r, g, ms, r->goal_deg[s], &next)) {
        fail(r, SERVO_RAUTO_WHY_LIMIT, now_ms);
        return false;
    }
    /* The register tried back to its value, the side's value to the
     * other one. */
    r->want[r->map_reg] = r->servo.value[r->map_reg];
    r->want[g]          = next;
    r->map_reg    = (uint8_t)g;
    r->map_tries  = 2u;
    r->map_before = ms;
    r->map_expect = expect(r, g, next, ms, r->goal_deg[s]);
    go_safe(r, now_ms);
    return false;
}

/**
 * @brief Record a measure in the step list.
 * @param r      Run state.
 * @param ms     The measure along the side's direction, °.
 * @param within Within the tolerance.
 */
static void record(servo_rauto_t *r, float ms, bool within)
{
    if (r->step_n >= sizeof(r->steps) / sizeof(r->steps[0])) {
        return;
    }
    const uint8_t s = r->side;
    int g = reg_of(r, s);
    if (g < 0 && r->map_open) {
        g = r->map_reg;
    }
    servo_rauto_step_t *p = &r->steps[r->step_n++];
    p->side         = s;
    p->n            = r->writes[s];
    p->value        = g >= 0 ? r->cur[g] : 0u;
    p->measured_deg = ms;
    p->goal_deg     = r->goal_deg[s];
    p->within       = within;
}

/**
 * @brief The angle a measure took: the side's direction on its first
 *        measure, the step recorded, then done, a write, or the end.
 * @param r      Run state.
 * @param deg    The deflection from the zero, signed, °.
 * @param now_ms Time, ms.
 */
static void measured(servo_rauto_t *r, float deg, uint32_t now_ms)
{
    const uint8_t s = r->side;
    const float goal = r->goal_deg[s];
    const float tol = tol_deg(r);
    if (r->sign[s] == 0 && fabsf(deg) >= tol) {
        r->sign[s] = deg > 0.0f ? 1 : -1;
    }
    const float ms = r->sign[s] != 0 ? deg * (float)r->sign[s] : fabsf(deg);
    const bool within = fabsf(ms - goal) <= tol + RAUTO_EPS_DEG;
    r->measured_deg[s] = ms;
    record(r, ms, within);
    if (r->sign[s] != 0 && r->sign[s] == r->sign[1u - s]) {
        /* Both end pulses move the surface to the same side. */
        fail(r, SERVO_RAUTO_WHY_MAPPING, now_ms);
        return;
    }
    if (r->map_open && !judge_map(r, ms, within, now_ms)) {
        return;
    }
    if (within) {
        r->verified[s]    = true;
        r->verified_at[s] = r->total;
        next_measure(r, now_ms);
        return;
    }
    r->verified[s] = false;
    if (r->sign[s] == 0 || !(ms > 0.0f)) {
        /* Too close to the zero to give the side a direction, or on the
         * other side of it. */
        fail(r, SERVO_RAUTO_WHY_MAPPING, now_ms);
        return;
    }
    plan_write(r, s, ms, now_ms);
}

/**
 * @brief A measure's step: the angle taken once still, the run ended when
 *        it is not still in time.
 * @param r      Run state.
 * @param now_ms Time, ms.
 * @param in     What the caller knows.
 */
static void measure_step(servo_rauto_t *r, uint32_t now_ms,
                         const servo_rauto_in_t *in)
{
    if (!due(now_ms, r->move_ms, SERVO_RAUTO_STILL_MS)) {
        return;
    }
    if (r->phase == SERVO_RAUTO_PH_MOVE) {
        r->phase    = SERVO_RAUTO_PH_SETTLE;
        r->phase_ms = now_ms;
    }
    if (in->angle_valid && in->still_ms >= SERVO_RAUTO_STILL_MS
        && r->gap.open == false) {
        measured(r, in->angle_deg, now_ms);
    } else if (due(now_ms, r->move_ms, SERVO_RAUTO_MOVE_MS)) {
        fail(r, SERVO_RAUTO_WHY_NOT_STILL, now_ms);
    }
}

/* ------------------------------------------------------------- inputs */

bool servo_rauto_start(servo_rauto_t *r, const servo_rauto_cfg_t *cfg,
                       uint32_t now_ms)
{
    if (r == NULL || cfg == NULL || servo_rauto_running(r)
        || cfg->margin_pct > SERVO_RAUTO_MARGIN_MAX
        || cfg->tol_ddeg < SERVO_RAUTO_TOL_MIN_DDEG
        || cfg->tol_ddeg > SERVO_RAUTO_TOL_MAX_DDEG
        || cfg->family > (uint8_t)SERVO_RAUTO_FAM_HITEC_C
        || !(cfg->centre_us > k_end_us[0] && cfg->centre_us < k_end_us[1])) {
        return false;
    }
    for (int s = 0; s < 2; ++s) {
        /* 0.1° less half a float step, so 0.1 typed is taken. */
        if (!(cfg->target_deg[s] >= 0.0999f && cfg->target_deg[s] <= 90.0f)) {
            return false;
        }
    }
    memset(r, 0, sizeof(*r));
    r->cfg         = *cfg;
    r->state       = SERVO_RAUTO_RUNNING;
    r->start_ms    = now_ms;
    r->last_win_ms = now_ms;
    r->reg_side[0] = -1;
    r->reg_side[1] = -1;
    for (int s = 0; s < 2; ++s) {
        r->goal_deg[s] = cfg->target_deg[s]
                         * (1.0f + (float)cfg->margin_pct / 100.0f);
    }
    go_safe(r, now_ms);
    return true;
}

/**
 * @brief A current's magnitude, mA, wider than the reading.
 * @param ma Current, mA, signed.
 * @return its magnitude.
 */
static int32_t mag(int16_t ma)
{
    return ma < 0 ? -(int32_t)ma : (int32_t)ma;
}

/**
 * @brief A stall: SURFACE BINDS AT the angle while a measure drives the
 *        servo short of its goal, else the limit run's stall.
 * @param r      Run state.
 * @param now_ms Time, ms.
 */
static void stalled(servo_rauto_t *r, uint32_t now_ms)
{
    if (measuring(r->phase) && r->angle_valid) {
        const uint8_t s = r->side;
        const float along = r->sign[s] != 0
                                ? r->angle_deg * (float)r->sign[s]
                                : fabsf(r->angle_deg);
        if (along < r->goal_deg[s] - tol_deg(r)) {
            r->bind_deg = r->angle_deg;
            finish(r, SERVO_RAUTO_FAIL, SERVO_RAUTO_WHY_BINDS, now_ms);
            return;
        }
    }
    abort_why(r, SERVO_RAUTO_WHY_STALL, now_ms);
}

void servo_rauto_window(servo_rauto_t *r, const servo_test_win_t *w)
{
    if (!servo_rauto_running(r) || w == NULL || !w->current) {
        return;
    }
    const uint32_t at = w->taken_ms;
    r->have_win    = true;
    r->last_win_ms = at;
    if (w->clipped != 0u || mag(w->max_ma) >= SERVO_LIMRUN_HARD_MA
        || mag(w->min_ma) >= SERVO_LIMRUN_HARD_MA
        || mag(w->mean_ma) >= SERVO_LIMRUN_HARD_MA) {
        /* A clipped sample's true value is not known: as one past it. */
        abort_why(r, SERVO_RAUTO_WHY_HARD, at);
        return;
    }
    if (mag(w->mean_ma) > SERVO_LIMRUN_STALL_MA) {
        if (!r->elevated) {
            r->elevated    = true;
            r->elevated_ms = at - SERVO_TEST_WIN_MS;
        }
        if (due(at, r->elevated_ms, SERVO_LIMRUN_STALL_MS)) {
            stalled(r, at);
        }
    } else {
        r->elevated = false;
    }
}

void servo_rauto_angle(servo_rauto_t *r, const servo_angle_in_t *a)
{
    if (!servo_rauto_running(r) || a == NULL) {
        return;
    }
    if (!a->valid) {
        if (servo_gap_lost(&r->gap, a->taken_ms) == SERVO_GAP_TOO_SOON) {
            abort_why(r, SERVO_RAUTO_WHY_ANGLE, a->taken_ms);
        }
    } else if (servo_gap_back(&r->gap, a->taken_ms) == SERVO_GAP_TOO_LONG) {
        abort_why(r, SERVO_RAUTO_WHY_ANGLE, a->taken_ms);
    }
}

void servo_rauto_backup(servo_rauto_t *r, const char *name)
{
    if (r != NULL && r->read_once && name != NULL && name[0] != '\0') {
        snprintf(r->servo.backup, sizeof(r->servo.backup), "%s", name);
    }
}

/**
 * @brief Ask the programmer for LEAVE.
 * @param r      Run state.
 * @param now_ms Time, ms.
 */
static void ask_leave(servo_rauto_t *r, uint32_t now_ms)
{
    r->phase      = SERVO_RAUTO_PH_LEAVE;
    r->phase_ms   = now_ms;
    r->pend.leave = true;
}

/**
 * @brief Whether any value differs from the servo's own.
 * @param r Run state.
 */
static void mark_programmed(servo_rauto_t *r)
{
    r->programmed = r->programmed
                    || r->cur[0] != r->servo.value[0]
                    || r->cur[1] != r->servo.value[1]
                    || memcmp(r->pulse_now, r->servo.pulse_dus,
                              sizeof(r->pulse_now)) != 0;
}

/**
 * @brief The read session's CONNECT: the servo as it is, then the pulse
 *        write (KST) or LEAVE.
 * @param r      Run state.
 * @param sv     The servo read.
 * @param now_ms Time, ms.
 */
static void first_read(servo_rauto_t *r, const servo_rauto_servo_t *sv,
                       uint32_t now_ms)
{
    if (!sv->ok || sv->lo > sv->hi || sv->value[0] < sv->lo
        || sv->value[1] < sv->lo || sv->value[0] > sv->hi
        || sv->value[1] > sv->hi) {
        fail(r, SERVO_RAUTO_WHY_NO_SERVO, now_ms);
        return;
    }
    r->read_once = true;
    r->servo     = *sv;
    r->cur[0]    = sv->value[0];
    r->cur[1]    = sv->value[1];
    memcpy(r->pulse_now, sv->pulse_dus, sizeof(r->pulse_now));
    if (r->cfg.family == SERVO_RAUTO_FAM_KST
        && (sv->pulse_dus[1] != SERVO_LIMRUN_STD_LOWER_DUS
            || sv->pulse_dus[2] != SERVO_LIMRUN_STD_UPPER_DUS)) {
        r->pulse_op = true;
        r->phase    = SERVO_RAUTO_PH_WRITE;
        r->phase_ms = now_ms;
        r->pend.pulses       = true;
        r->pend.pulse_dus[0] = SERVO_LIMRUN_STD_LOWER_DUS;
        r->pend.pulse_dus[1] = SERVO_LIMRUN_STD_UPPER_DUS;
        return;
    }
    ask_leave(r, now_ms);
}

void servo_rauto_connected(servo_rauto_t *r, const servo_rauto_servo_t *sv,
                           uint32_t now_ms)
{
    if (!servo_rauto_running(r) || sv == NULL
        || (r->phase != SERVO_RAUTO_PH_READ
            && r->phase != SERVO_RAUTO_PH_CONNECT)) {
        return;
    }
    ++r->sessions;
    if (r->phase == SERVO_RAUTO_PH_READ) {
        first_read(r, sv, now_ms);
        return;
    }
    if (!sv->ok || sv->value[0] != r->cur[0] || sv->value[1] != r->cur[1]
        || sv->lo != r->servo.lo || sv->hi != r->servo.hi
        || sv->reversion != r->servo.reversion
        || memcmp(sv->pulse_dus, r->pulse_now, sizeof(r->pulse_now)) != 0) {
        /* Another servo, or one set otherwise since the run left it. */
        abort_why(r, SERVO_RAUTO_WHY_PROG, now_ms);
        return;
    }
    r->pulse_op = false;
    r->phase    = SERVO_RAUTO_PH_WRITE;
    r->phase_ms = now_ms;
    r->pend.write    = true;
    r->pend.value[0] = r->want[0];
    r->pend.value[1] = r->want[1];
}

/**
 * @brief A write that failed once sent: part of it may have landed.
 * @param r      Run state.
 * @param now_ms Time, ms.
 */
static void sent_failed(servo_rauto_t *r, uint32_t now_ms)
{
    r->in_flight = false;
    ++r->write_ops;
    r->programmed = true;
    abort_why(r, SERVO_RAUTO_WHY_WRITE, now_ms);
}

void servo_rauto_written(servo_rauto_t *r, bool ok, const uint16_t *value,
                         uint32_t now_ms)
{
    if (!servo_rauto_running(r) || r->phase != SERVO_RAUTO_PH_WRITE
        || r->pulse_op) {
        return;
    }
    if (!ok || value == NULL) {
        sent_failed(r, now_ms);
        return;
    }
    r->in_flight = false;
    ++r->write_ops;
    r->cur[0] = value[0];
    r->cur[1] = value[1];
    mark_programmed(r);
    if (value[0] != r->want[0] || value[1] != r->want[1]) {
        abort_why(r, SERVO_RAUTO_WHY_WRITE, now_ms);
        return;
    }
    ++r->writes[r->side];
    ++r->total;
    ask_leave(r, now_ms);
}

void servo_rauto_pulsed(servo_rauto_t *r, bool ok, const uint16_t *pulse_dus,
                        uint32_t now_ms)
{
    if (!servo_rauto_running(r) || r->phase != SERVO_RAUTO_PH_WRITE
        || !r->pulse_op) {
        return;
    }
    if (!ok || pulse_dus == NULL) {
        sent_failed(r, now_ms);
        return;
    }
    r->in_flight = false;
    ++r->write_ops;
    memcpy(r->pulse_now, pulse_dus, sizeof(r->pulse_now));
    mark_programmed(r);
    r->pulse_op = false;
    if (pulse_dus[1] != SERVO_LIMRUN_STD_LOWER_DUS
        || pulse_dus[2] != SERVO_LIMRUN_STD_UPPER_DUS) {
        abort_why(r, SERVO_RAUTO_WHY_WRITE, now_ms);
        return;
    }
    r->pulses_landed = true;
    ask_leave(r, now_ms);
}

void servo_rauto_left(servo_rauto_t *r, bool ok, uint32_t now_ms)
{
    if (!servo_rauto_running(r) || r->phase != SERVO_RAUTO_PH_LEAVE) {
        return;
    }
    if (!ok) {
        abort_why(r, SERVO_RAUTO_WHY_PROG, now_ms);
        return;
    }
    r->phase    = SERVO_RAUTO_PH_ARM;
    r->phase_ms = now_ms;
    r->pend.arm = true;
}

void servo_rauto_refused(servo_rauto_t *r, uint32_t now_ms)
{
    if (!servo_rauto_running(r)) {
        return;
    }
    r->in_flight = false;           /* refused before it was sent */
    if (r->phase == SERVO_RAUTO_PH_WRITE) {
        abort_why(r, SERVO_RAUTO_WHY_WRITE, now_ms);
    } else if (prog_phase(r->phase)) {
        abort_why(r, SERVO_RAUTO_WHY_PROG, now_ms);
    }
}

void servo_rauto_no_servo(servo_rauto_t *r, uint32_t now_ms)
{
    if (!servo_rauto_running(r)) {
        return;
    }
    if (r->phase == SERVO_RAUTO_PH_READ) {
        fail(r, SERVO_RAUTO_WHY_NO_SERVO, now_ms);
    } else if (r->phase == SERVO_RAUTO_PH_CONNECT) {
        servo_rauto_refused(r, now_ms);
    }
}

/**
 * @brief The SAFE phase: the hold at PULSE CENTRE, SAFE asked, CONNECT once
 *        the bench is SAFE.
 * @param r      Run state.
 * @param now_ms Time, ms.
 * @param in     What the caller knows.
 */
static void safe_step(servo_rauto_t *r, uint32_t now_ms,
                      const servo_rauto_in_t *in)
{
    if (!r->safe_asked) {
        if (!in->live) {
            servo_rauto_abort(r, SERVO_TEST_AB_DISARMED, now_ms);
        } else if (due(now_ms, r->phase_ms, SERVO_LIMRUN_RETURN_MS)) {
            r->safe_asked  = true;
            r->phase_ms    = now_ms;
            r->pend.disarm = true;
        }
    } else if (in->safe) {
        r->phase    = r->read_once ? SERVO_RAUTO_PH_CONNECT
                                   : SERVO_RAUTO_PH_READ;
        r->phase_ms = now_ms;
        r->pend.connect = true;
    } else if (due(now_ms, r->phase_ms, SERVO_LIMRUN_BENCH_MS)) {
        abort_why(r, SERVO_RAUTO_WHY_BENCH, now_ms);
    }
}

/**
 * @brief The phase's own step.
 * @param r      Run state.
 * @param now_ms Time, ms.
 * @param in     What the caller knows.
 */
static void phase_step(servo_rauto_t *r, uint32_t now_ms,
                       const servo_rauto_in_t *in)
{
    switch (r->phase) {
    case SERVO_RAUTO_PH_MOVE:
    case SERVO_RAUTO_PH_SETTLE:
    case SERVO_RAUTO_PH_CHECK:
        if (!in->live) {
            servo_rauto_abort(r, SERVO_TEST_AB_DISARMED, now_ms);
        } else {
            measure_step(r, now_ms, in);
        }
        break;
    case SERVO_RAUTO_PH_SAFE:
        safe_step(r, now_ms, in);
        break;
    case SERVO_RAUTO_PH_READ:
    case SERVO_RAUTO_PH_CONNECT:
    case SERVO_RAUTO_PH_WRITE:
    case SERVO_RAUTO_PH_LEAVE:
        if (due(now_ms, r->phase_ms, SERVO_LIMRUN_PROG_MS)) {
            abort_why(r, SERVO_RAUTO_WHY_PROG, now_ms);
        }
        break;
    case SERVO_RAUTO_PH_ARM:
        if (in->live) {
            /* After the read session the first side; after a write the
             * side written. */
            if (r->total == 0u && !r->map_open) {
                next_measure(r, now_ms);
            } else {
                begin_measure(r, r->side, false, now_ms);
            }
        } else if (due(now_ms, r->phase_ms, SERVO_LIMRUN_BENCH_MS)) {
            abort_why(r, SERVO_RAUTO_WHY_BENCH, now_ms);
        }
        break;
    default:
        break;
    }
}

void servo_rauto_step(servo_rauto_t *r, uint32_t now_ms,
                      const servo_rauto_in_t *in, servo_rauto_do_t *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (r == NULL) {
        return;
    }
    if (servo_rauto_running(r) && in != NULL) {
        r->angle_valid = in->angle_valid;
        r->angle_deg   = in->angle_deg;
        if (due(now_ms, r->last_win_ms, SERVO_LIMRUN_WIN_STALE_MS)) {
            servo_rauto_abort(r, SERVO_TEST_AB_WIN_STALE, now_ms);
        } else if (servo_gap_check(&r->gap, now_ms) == SERVO_GAP_TOO_LONG) {
            abort_why(r, SERVO_RAUTO_WHY_ANGLE, now_ms);
        } else {
            phase_step(r, now_ms, in);
        }
    }
    if (out == NULL) {
        return;
    }
    *out = r->pend;
    memset(&r->pend, 0, sizeof(r->pend));
    if (servo_rauto_running(r) && (out->write || out->pulses)) {
        r->in_flight = true;
    }
    if (servo_rauto_running(r) && r->cmd_due) {
        out->command = true;
        out->cmd_us  = r->cmd_us;
        r->cmd_due   = false;
    }
}
