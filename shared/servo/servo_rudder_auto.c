/**
 * @file
 * @brief AUTO end points: the run's steps, its sessions and its guards; see
 *        servo_rudder_auto.h.  The report and the words are
 *        servo_rudder_auto_report.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_rudder_auto_int.h"

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

float rauto_tol_deg(const servo_rauto_t *r)
{
    return (float)r->cfg.tol_ddeg / 10.0f;
}

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
 * @brief The phases that measure: the servo driven to a pulse.
 * @param ph The phase.
 * @return true for MOVE, SETTLE, CHECK and VERIFY.
 */
static bool measuring(servo_rauto_phase_t ph)
{
    return ph == SERVO_RAUTO_PH_MOVE || ph == SERVO_RAUTO_PH_SETTLE
        || ph == SERVO_RAUTO_PH_CHECK || ph == SERVO_RAUTO_PH_VERIFY
        || ph == SERVO_RAUTO_PH_APPROACH || ph == SERVO_RAUTO_PH_SEARCH;
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
        && r->verdict != SERVO_RAUTO_PASS && !r->restore_off;
}

void servo_rauto_servo_seen(servo_rauto_t *r, const servo_rauto_servo_t *sv)
{
    if (!servo_rauto_restore(r) || sv == NULL || !sv->ok) {
        return;
    }
    const bool kst = r->cfg.family == SERVO_RAUTO_FAM_KST;
    if (r->seen_base && sv->value[0] == r->base_value[0]
        && sv->value[1] == r->base_value[1]
        && (!kst || memcmp(sv->pulse_dus, r->base_pulse,
                           sizeof(r->base_pulse)) == 0)) {
        return;
    }
    const bool first = !r->seen_base;
    r->seen_base = true;
    memcpy(r->base_value, sv->value, sizeof(r->base_value));
    memcpy(r->base_pulse, sv->pulse_dus, sizeof(r->base_pulse));
    if (first) {
        return;
    }
    const bool back = sv->value[0] == r->servo.value[0]
                      && sv->value[1] == r->servo.value[1]
                      && (!kst || memcmp(sv->pulse_dus, r->servo.pulse_dus,
                                         sizeof(r->servo.pulse_dus)) == 0);
    const bool other = !r->left_unknown
                       && (sv->value[0] != r->cur[0]
                           || sv->value[1] != r->cur[1]
                           || (kst && memcmp(sv->pulse_dus, r->pulse_now,
                                             sizeof(r->pulse_now)) != 0));
    if (back || other) {
        r->restore_off = true;
    }
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

uint16_t servo_rauto_reg_of(const servo_rauto_t *r, int g, uint16_t travel)
{
    if (r == NULL || r->cfg.family != SERVO_RAUTO_FAM_HITEC_D) {
        return travel;
    }
    const int32_t v = g == 0 ? (int32_t)r->servo.mid - (int32_t)travel
                             : (int32_t)r->servo.mid + (int32_t)travel;
    return (uint16_t)(v < 0 ? 0 : v > 0xFFFF ? 0xFFFF : v);
}

servo_str_t servo_rauto_unit(const servo_rauto_t *r)
{
    return (r != NULL && r->cfg.family == SERVO_RAUTO_FAM_HITEC_D)
               ? SERVO_STR_RA_R_COUNTS : SERVO_STR_RA_R_RAW;
}

bool servo_rauto_in_cap(uint16_t value0, float goal_deg, float first_deg,
                        uint16_t next)
{
    if (!(first_deg > 0.0f)) {
        return false;
    }
    const double est = (double)value0 * (double)goal_deg / (double)first_deg;
    const double room = est * (double)SERVO_RAUTO_CAP_PCT / 100.0;
    return fabs((double)next - est) <= room;
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
        r->programmed   = true;
        r->left_unknown = true;
        r->in_flight    = false;
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

void rauto_end(servo_rauto_t *r, servo_rauto_verdict_t v,
               servo_rauto_why_t why, uint32_t now_ms)
{
    finish(r, v, why, now_ms);
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
 * @brief A session next: the servo to PULSE CENTER, SAFE asked once it had
 *        SERVO_LIMRUN_RETURN_MS to get there (servo_rauto_step()).
 * @param r      Run state.
 * @param now_ms Time, ms.
 */
static void go_safe(servo_rauto_t *r, uint32_t now_ms)
{
    r->phase      = SERVO_RAUTO_PH_SAFE;
    r->phase_ms   = now_ms;
    r->safe_asked = false;
    r->cmd_us     = r->cfg.center_us;
    r->cmd_due    = true;
    r->held_ok    = false;
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
    r->side = side;
    rauto_measure_via(r, check ? SERVO_RAUTO_PH_CHECK : SERVO_RAUTO_PH_MOVE,
                      servo_rauto_side_us(r, side), r->cfg.center_us, now_ms);
}

void rauto_measure_via(servo_rauto_t *r, servo_rauto_phase_t ph, uint16_t us,
                       uint16_t from_us, uint32_t now_ms)
{
    if (r->held_ok && r->held_us == from_us) {
        rauto_command(r, ph, us, now_ms);
        return;
    }
    r->next_ph = ph;
    r->next_us = us;
    rauto_command(r, SERVO_RAUTO_PH_APPROACH, from_us, now_ms);
}

void rauto_command(servo_rauto_t *r, servo_rauto_phase_t ph, uint16_t us,
                   uint32_t now_ms)
{
    if (r->searched
        && (us < r->ends[0].end_us || us > r->ends[1].end_us)) {
        /* The found ends cap every pulse after the search. */
        r->stop_us = us < r->ends[0].end_us ? r->ends[0].end_us
                                            : r->ends[1].end_us;
        finish(r, SERVO_RAUTO_FAIL, SERVO_RAUTO_WHY_STOP, now_ms);
        return;
    }
    r->phase    = ph;
    r->phase_ms = now_ms;
    r->move_ms  = now_ms;
    r->cmd_us   = us;
    r->cmd_due  = true;
    r->held_ok  = false;
    /* The settle window counts from the command. */
    servo_settle_reset(&r->hold);
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
 * @brief The next side to measure; with both settled the verify pass on a
 *        programmed servo, else PASS.
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
    if (r->programmed) {
        rauto_verify_begin(r, 0u, now_ms);
        return;
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
 * @brief The bound on a write: register @p g's new value for side @p s
 *        within SERVO_RAUTO_CAP_PCT of the model's first estimate, else the
 *        run ends as FAIL (SERVO_RAUTO_WHY_CAP).
 * @param r      Run state.
 * @param g      The register.
 * @param s      The side.
 * @param next   The new value.
 * @param now_ms Time, ms.
 * @return true within.
 */
static bool capped_ok(servo_rauto_t *r, int g, uint8_t s, uint16_t next,
                      uint32_t now_ms)
{
    if (r->have_first[s]
        && servo_rauto_in_cap(r->servo.value[g], r->goal_deg[s],
                              r->first_deg[s], next)) {
        return true;
    }
    fail(r, SERVO_RAUTO_WHY_CAP, now_ms);
    return false;
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
        /* The register of the side's name first, the other one with
         * REVERSION on: tried, then found by the measure. */
        g = (int)s ^ (r->servo.reversion ? 1 : 0);
    }
    uint16_t next = 0u;
    bool can = next_value(r, g, ms, r->goal_deg[s], &next);
    if (!can && find) {
        /* That register cannot move toward the goal: the side may be the
         * other one's, which can. */
        g = 1 - g;
        can = next_value(r, g, ms, r->goal_deg[s], &next);
    }
    if (!can) {
        fail(r, SERVO_RAUTO_WHY_LIMIT, now_ms);
        return;
    }
    if (!capped_ok(r, g, s, next, now_ms)) {
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
    } else {
        /* Held to its effect at the side's next measure (judge_effect()). */
        if (r->eff_writes[s] == 0u) {
            r->eff_from[s]   = ms;
            r->eff_expect[s] = 0.0f;
        }
        r->eff_expect[s] += expect(r, g, next, ms, r->goal_deg[s]);
        ++r->eff_writes[s];
        r->eff_due = true;
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
        /* The register's later writes are held to their effect from
         * here. */
        r->eff_writes[s] = 0u;
        return true;
    }
    if (half > 0.0f && d <= -half) {
        fail(r, SERVO_RAUTO_WHY_MAPPING, now_ms);
        return false;
    }
    /* It stayed.  A second time: neither register moved the side. */
    if (r->map_tries >= 2u) {
        fail(r, SERVO_RAUTO_WHY_NO_EFFECT, now_ms);
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
    if (!capped_ok(r, g, s, next, now_ms)) {
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
 * @brief The side measured after a write to its known register: the
 *        writes since the last one that moved it held to the change the
 *        model gives for them (servo_rudder_auto.h, "Each write is held to
 *        its effect").
 * @param r      Run state.
 * @param ms     The measure along the side's direction, °.
 * @param within It lies within the tolerance.
 * @param now_ms Time, ms.
 * @return false when the run ended: the writes did not move the side.
 */
static bool judge_effect(servo_rauto_t *r, float ms, bool within,
                         uint32_t now_ms)
{
    const uint8_t s = r->side;
    const float tol = rauto_tol_deg(r);
    const float e = r->eff_expect[s];
    const float big = fabsf(e);
    const float along = (ms - r->eff_from[s]) * (e < 0.0f ? -1.0f : 1.0f);
    const float need = fminf(big / 2.0f, tol);
    if (within || (big >= tol - RAUTO_EPS_DEG && along >= need)) {
        r->eff_writes[s] = 0u;
        return true;
    }
    if (r->eff_writes[s] >= SERVO_RAUTO_NO_EFFECT_WRITES
        && big >= tol - RAUTO_EPS_DEG) {
        fail(r, SERVO_RAUTO_WHY_NO_EFFECT, now_ms);
        return false;
    }
    return true;
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
    p->reg          = g >= 0 ? (uint8_t)g : 0xFFu;
    p->measured_deg = ms;
    p->goal_deg     = r->goal_deg[s];
    p->within       = within;
}

/**
 * @brief The angle a measure took, judged: the side's direction on its
 *        first measure, the measure along it, the step recorded, both end
 *        pulses on one side of the zero ending the run.
 * @param r      Run state; r->side the side measured.
 * @param deg    The deflection from the zero, signed, °.
 * @param ms     Out: the measure along the side's direction, °.
 * @param within Out: within the tolerance of the goal.
 * @param now_ms Time, ms.
 * @return false when the run ended.
 */
static bool assess(servo_rauto_t *r, float deg, float *ms, bool *within,
                   uint32_t now_ms)
{
    const uint8_t s = r->side;
    const float tol = rauto_tol_deg(r);
    if (r->sign[s] == 0 && fabsf(deg) >= tol) {
        r->sign[s] = deg > 0.0f ? 1 : -1;
    }
    *ms = r->sign[s] != 0 ? deg * (float)r->sign[s] : fabsf(deg);
    *within = fabsf(*ms - r->goal_deg[s]) <= tol + RAUTO_EPS_DEG;
    r->measured_deg[s] = *ms;
    if (!r->have_first[s]) {
        r->have_first[s] = true;
        r->first_deg[s]  = *ms;
    }
    record(r, *ms, *within);
    if (r->sign[s] != 0 && r->sign[s] == r->sign[1u - s]) {
        /* Both end pulses move the surface to the same side. */
        fail(r, SERVO_RAUTO_WHY_MAPPING, now_ms);
        return false;
    }
    return true;
}

/**
 * @brief A side outside the tolerance: its write next, or the end where it
 *        has no direction or lies on the other side of the zero.
 * @param r      Run state.
 * @param s      The side.
 * @param ms     Its measure along its direction, °.
 * @param now_ms Time, ms.
 */
static void outside(servo_rauto_t *r, uint8_t s, float ms, uint32_t now_ms)
{
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
 * @brief The angle a measure took: judged, then done, a write, or the end.
 * @param r      Run state.
 * @param deg    The deflection from the zero, signed, °.
 * @param now_ms Time, ms.
 */
static void measured(servo_rauto_t *r, float deg, uint32_t now_ms)
{
    const uint8_t s = r->side;
    float ms = 0.0f;
    bool within = false;
    if (!assess(r, deg, &ms, &within, now_ms)) {
        return;
    }
    if (r->map_open && !judge_map(r, ms, within, now_ms)) {
        return;
    }
    if (r->eff_due) {
        r->eff_due = false;
        if (!judge_effect(r, ms, within, now_ms)) {
            return;
        }
    }
    if (within) {
        r->verified[s]    = true;
        r->verified_at[s] = r->total;
        next_measure(r, now_ms);
        return;
    }
    outside(r, s, ms, now_ms);
}

void rauto_searched(servo_rauto_t *r, uint32_t now_ms)
{
    float ms[2] = { 0.0f, 0.0f };
    bool within[2] = { false, false };
    for (uint8_t s = 0; s < 2u; ++s) {
        r->side = s;
        if (!assess(r, rauto_at_side(r, s, r->s_deg[s]), &ms[s], &within[s],
                    now_ms)) {
            return;
        }
        r->verified[s]    = within[s];
        r->verified_at[s] = r->total;
    }
    for (uint8_t s = 0; s < 2u; ++s) {
        if (!within[s]) {
            r->side = s;
            outside(r, s, ms[s], now_ms);
            return;
        }
    }
    next_measure(r, now_ms);
}

bool rauto_still(const servo_rauto_t *r, uint32_t now_ms,
                 const servo_rauto_in_t *in)
{
    if (!due(now_ms, r->move_ms, SERVO_RAUTO_STILL_MS)) {
        return false;
    }
    /* On the IMU the reading held within IMU PRECISION over IMU SETTLING
     * TIME too: the sensor's creep after the step (servo_settle.h). */
    const bool held = r->cfg.settle_win_ms == 0u
                      || servo_settle_held(&r->hold, r->cfg.settle_win_ms,
                                           r->cfg.settle_band_deg);
    return in->angle_valid && in->still_ms >= SERVO_RAUTO_STILL_MS && held
           && !r->gap.open;
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
    if (rauto_still(r, now_ms, in)) {
        if (r->phase == SERVO_RAUTO_PH_APPROACH) {
            /* Settled where the measure comes from: its own pulse now. */
            rauto_command(r, r->next_ph, r->next_us, now_ms);
            return;
        }
        /* The servo stands settled here until the next command. */
        r->held_ok = true;
        r->held_us = r->cmd_us;
        if (r->phase == SERVO_RAUTO_PH_VERIFY) {
            /* An end as it stands for its end pulse; the center as read. */
            rauto_verify_take(r, r->vstep < 2u
                                     ? rauto_at_side(r, r->vstep,
                                                     in->angle_deg)
                                     : in->angle_deg, now_ms);
        } else {
            measured(r, rauto_at_side(r, r->side, in->angle_deg), now_ms);
        }
    } else if (due(now_ms, r->move_ms, servo_rauto_wait_ms(r))) {
        fail(r, SERVO_RAUTO_WHY_NOT_STILL, now_ms);
    }
}

uint32_t servo_rauto_wait_ms(const servo_rauto_t *r)
{
    if (r == NULL) {
        return SERVO_RAUTO_MOVE_MS;
    }
    uint32_t ms = SERVO_RAUTO_MOVE_MS + r->settle_ms;
    if (r->cfg.settle_win_ms > 0u) {
        ms += SERVO_RAUTO_CREEP_MS + r->cfg.settle_win_ms;
    }
    return ms;
}

/* ------------------------------------------------------------- inputs */

bool servo_rauto_goals_ok(const float target_deg[2], uint16_t tol_ddeg)
{
    if (target_deg == NULL) {
        return false;
    }
    const float need = 2.0f * (float)tol_ddeg / 10.0f + 2.0f * RAUTO_EPS_DEG;
    for (int s = 0; s < 2; ++s) {
        if (!(target_deg[s] >= need)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief The margin's range from NEUTRAL, exact, ns (0.001us): each side
 *        extends by margin % of its own span from N, with N and the ends
 *        in 0.1us (N - 10000) x p ns below 1000us and (20000 - N) x p ns
 *        above 2000us.
 * @param margin_pct  The margin, %; above SERVO_RAUTO_MARGIN_MAX as that.
 * @param neutral_dus NEUTRAL, 0.1us; 0 as SERVO_RAUTO_NEUTRAL_DUS, else
 *                    held to SERVO_RAUTO_NEUTRAL_MIN_DUS..MAX_DUS.
 * @param ns          Out: the lower and the upper end, ns.
 */
static void margin_ns(uint8_t margin_pct, uint16_t neutral_dus,
                      uint32_t ns[2])
{
    const uint32_t p = margin_pct > SERVO_RAUTO_MARGIN_MAX
                           ? SERVO_RAUTO_MARGIN_MAX : margin_pct;
    uint32_t n = neutral_dus == 0u ? SERVO_RAUTO_NEUTRAL_DUS : neutral_dus;
    n = n < SERVO_RAUTO_NEUTRAL_MIN_DUS ? SERVO_RAUTO_NEUTRAL_MIN_DUS
      : n > SERVO_RAUTO_NEUTRAL_MAX_DUS ? SERVO_RAUTO_NEUTRAL_MAX_DUS : n;
    const uint32_t lo_dus = 10u * k_end_us[0], hi_dus = 10u * k_end_us[1];
    ns[0] = 1000u * k_end_us[0] - (n - lo_dus) * p;
    ns[1] = 1000u * k_end_us[1] + (hi_dus - n) * p;
}

void servo_rauto_kst_pulses(uint8_t margin_pct, uint16_t neutral_dus,
                            uint16_t dus[2])
{
    if (dus == NULL) {
        return;
    }
    uint32_t ns[2];
    margin_ns(margin_pct, neutral_dus, ns);
    const uint32_t k = 100u * SERVO_RAUTO_KST_STEP_DUS;  /* ns */
    /* Toward the center: the servo's range inside the margin's. */
    dus[0] = (uint16_t)((ns[0] + k - 1u) / k * SERVO_RAUTO_KST_STEP_DUS);
    dus[1] = (uint16_t)(ns[1] / k * SERVO_RAUTO_KST_STEP_DUS);
}

void servo_rauto_pulse_range(uint8_t margin_pct, uint16_t neutral_dus,
                             uint16_t us[2])
{
    if (us == NULL) {
        return;
    }
    uint16_t dus[2];
    servo_rauto_kst_pulses(margin_pct, neutral_dus, dus);
    /* Outward: the range holds the servo's, by less than 1us an end. */
    us[0] = (uint16_t)(dus[0] / 10u);
    us[1] = (uint16_t)((dus[1] + 9u) / 10u);
}

bool servo_rauto_widen(const servo_rauto_t *r, uint16_t prof_us[2])
{
    if (r == NULL || !r->pulses_held) {
        return false;
    }
    uint16_t us[2];
    servo_rauto_pulse_range(r->cfg.margin_pct, r->servo.pulse_dus[0], us);
    /* Each side only where the bench's is narrower. */
    const bool wide_lo = us[0] < r->cfg.min_us;
    const bool wide_hi = us[1] > r->cfg.max_us;
    if (prof_us != NULL) {
        prof_us[0] = wide_lo ? us[0] : r->cfg.min_us;
        prof_us[1] = wide_hi ? us[1] : r->cfg.max_us;
    }
    return wide_lo || wide_hi;
}

bool servo_rauto_start(servo_rauto_t *r, const servo_rauto_cfg_t *cfg,
                       uint32_t now_ms)
{
    if (r == NULL || cfg == NULL || servo_rauto_running(r)
        || cfg->margin_pct > SERVO_RAUTO_MARGIN_MAX
        || (cfg->family != (uint8_t)SERVO_RAUTO_FAM_KST
            && cfg->margin_pct > 0u)
        || cfg->min_us > k_end_us[0] || cfg->max_us < k_end_us[1]
        || cfg->tol_ddeg < SERVO_RAUTO_TOL_MIN_DDEG
        || cfg->tol_ddeg > SERVO_RAUTO_TOL_MAX_DDEG
        || cfg->family > (uint8_t)SERVO_RAUTO_FAM_HITEC_C
        || !(cfg->center_us > k_end_us[0] && cfg->center_us < k_end_us[1])
        || cfg->settle_win_ms > SERVO_SETTLE_WIN_MAX_MS
        || (cfg->settle_win_ms > 0u && !(cfg->settle_band_deg > 0.0f))) {
        return false;
    }
    for (int s = 0; s < 2; ++s) {
        /* 0.1° less half a float step, so 0.1 typed is taken. */
        if (!(cfg->target_deg[s] >= 0.0999f && cfg->target_deg[s] <= 90.0f)) {
            return false;
        }
    }
    if (!servo_rauto_goals_ok(cfg->target_deg, cfg->tol_ddeg)) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    r->cfg         = *cfg;
    r->state       = SERVO_RAUTO_RUNNING;
    r->start_ms    = now_ms;
    r->last_win_ms = now_ms;
    r->reg_side[0] = -1;
    r->reg_side[1] = -1;
    /* The goal at 1000us and 2000us is the target: the margin lies in
     * the pulse range (servo_rauto_kst_pulses()). */
    r->goal_deg[0] = cfg->target_deg[0];
    r->goal_deg[1] = cfg->target_deg[1];
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
    /* The verify pass's center and an approach have no goal to bind
     * short of. */
    const bool no_goal = (r->phase == SERVO_RAUTO_PH_VERIFY && r->vstep >= 2u)
                         || r->phase == SERVO_RAUTO_PH_APPROACH
                         || r->phase == SERVO_RAUTO_PH_SEARCH;
    if (measuring(r->phase) && r->angle_valid && !no_goal) {
        const uint8_t s = r->side;
        const float along = r->sign[s] != 0
                                ? r->angle_deg * (float)r->sign[s]
                                : fabsf(r->angle_deg);
        if (along < r->goal_deg[s] - rauto_tol_deg(r)) {
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
            return;
        }
    } else {
        r->elevated = false;
    }
    if (r->phase == SERVO_RAUTO_PH_SEARCH) {
        rauto_search_window(r, (uint16_t)mag(w->mean_ma), at);
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
        || sv->value[1] > sv->hi
        || (r->cfg.family == SERVO_RAUTO_FAM_HITEC_D && sv->mid == 0u)) {
        /* A D series servo without NEUTRAL has no end point positions. */
        fail(r, SERVO_RAUTO_WHY_NO_SERVO, now_ms);
        return;
    }
    if (r->cfg.family == SERVO_RAUTO_FAM_KST
        && (sv->pulse_dus[0] < SERVO_RAUTO_NEUTRAL_MIN_DUS
            || sv->pulse_dus[0] > SERVO_RAUTO_NEUTRAL_MAX_DUS)) {
        /* The margin's range is taken from NEUTRAL, which the KST holds
         * in 1360.0us to 1640.0us; outside it the range would not be the
         * servo's. */
        fail(r, SERVO_RAUTO_WHY_NEUTRAL, now_ms);
        return;
    }
    r->read_once = true;
    r->servo     = *sv;
    if (r->cfg.family == SERVO_RAUTO_FAM_HITEC_D && !sv->reversion) {
        /* Known: LEFT END (0xB2) at 1000us, RIGHT END (0xB0) at 2000us on
         * a D series servo set to CW (servo_rudder_auto.h). */
        r->reg_side[0] = 0;
        r->reg_side[1] = 1;
    }
    r->cur[0]    = sv->value[0];
    r->cur[1]    = sv->value[1];
    memcpy(r->pulse_now, sv->pulse_dus, sizeof(r->pulse_now));
    if (r->cfg.family == SERVO_RAUTO_FAM_KST) {
        uint16_t p[2];
        servo_rauto_kst_pulses(r->cfg.margin_pct, sv->pulse_dus[0], p);
        if (sv->pulse_dus[1] != p[0] || sv->pulse_dus[2] != p[1]) {
            r->pulse_op = true;
            r->phase    = SERVO_RAUTO_PH_WRITE;
            r->phase_ms = now_ms;
            r->pend.pulses       = true;
            r->pend.pulse_dus[0] = p[0];
            r->pend.pulse_dus[1] = p[1];
            return;
        }
        r->pulses_held = true;
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
    r->programmed   = true;
    r->left_unknown = true;
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
    uint16_t p[2];
    servo_rauto_kst_pulses(r->cfg.margin_pct, r->servo.pulse_dus[0], p);
    /* The range is the read NEUTRAL's: a NEUTRAL moved by the write
     * leaves it wrong. */
    if (pulse_dus[0] != r->servo.pulse_dus[0] || pulse_dus[1] != p[0]
        || pulse_dus[2] != p[1]) {
        abort_why(r, SERVO_RAUTO_WHY_WRITE, now_ms);
        return;
    }
    r->pulses_landed = true;
    r->pulses_held   = true;
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
 * @brief The SAFE phase: the hold at PULSE CENTER, SAFE asked, CONNECT once
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
    case SERVO_RAUTO_PH_VERIFY:
    case SERVO_RAUTO_PH_APPROACH:
        if (!in->live) {
            servo_rauto_abort(r, SERVO_TEST_AB_DISARMED, now_ms);
        } else {
            measure_step(r, now_ms, in);
        }
        break;
    case SERVO_RAUTO_PH_SEARCH:
        if (!in->live) {
            servo_rauto_abort(r, SERVO_TEST_AB_DISARMED, now_ms);
        } else {
            rauto_search_step(r, now_ms, in);
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
            if (r->total == 0u && !r->map_open && !r->searched) {
                /* After the read session: the limit search, which takes
                 * the first measures. */
                rauto_search_begin(r, 0u, now_ms);
            } else if (r->total == 0u && !r->map_open) {
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
        if (in->angle_valid && measuring(r->phase)) {
            servo_settle_add(&r->hold, in->angle_deg, now_ms);
        }
        /* The longest: a filter made slower in a run waits longer from
         * then on, one made faster does not cut a wait short. */
        if (in->settle_ms > r->settle_ms) {
            r->settle_ms = in->settle_ms;
        }
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
