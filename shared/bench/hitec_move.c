/**
 * @file
 * @brief MOVE SERVO on the programmer for servos by Hitec: the loop's
 *        gate, its passes, its end and its hard end.  See hitec_move.h.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_move.h"

#include <string.h>

#include "hitec_live.h"
#include "hitec_prog_int.h"

/** @brief The registers the start reads besides the full read's: the
 *         position, and the ones of the loop's set the full read lacks. */
static const uint8_t k_start[] = { 0x0Cu, 0x1Eu, 0x50u, 0x52u, 0x56u };

/** @brief The loop's set the full read lacks, read again at the end. */
static const uint8_t k_extra[] = { 0x1Eu, 0x50u, 0x52u, 0x56u };

/** @brief The loop's set: the registers its end holds against its start
 *         besides every one a plan writes. */
static const uint8_t k_set[] = { 0x1Eu, 0x50u, 0x52u, 0x54u,
                                 0x56u, 0xB0u, 0xB2u, 0xC2u };

/**
 * @brief Whether @p id is a position the loop follows.
 *
 * @param id hitec_param_t.
 * @return true for NEUTRAL, LEFT END and RIGHT END of the D series.
 */
static bool position_id(uint8_t id)
{
    return id == (uint8_t)HITEC_P_D_NEUTRAL
           || id == (uint8_t)HITEC_P_D_LEFT_END
           || id == (uint8_t)HITEC_P_D_RIGHT_END;
}

/**
 * @brief Count a session fault at the gate: the panel's error path turns
 *        every output off (error_stop.h).
 *
 * @param p The programmer.
 */
static void cut(hitec_prog_t *p)
{
    if (p->gate != NULL) {
        atomic_fetch_add(&p->gate->faults, 1u);
    }
}

/**
 * @brief The session's next operation is the loop's.
 *
 * @param p The programmer.
 * @param r What the session said to it.
 * @return true when it runs.
 */
static bool runs(hitec_prog_t *p, hitec_ses_t r)
{
    if (r != HITEC_SES_BUSY) {
        return false;
    }
    p->st = ST_SES;
    p->op = OP_MOVE;
    return true;
}

/**
 * @brief The loop ends with no end sequence: the hard end.
 *
 * The switch goes off and READ ALL is owed.  A stop, a cut or lost step,
 * the supply off and an AS5600 reading gone count a fault: every output
 * off.  A session fault is counted by ses_done() already, an ABORT is no
 * error.
 *
 * @param p The programmer.
 * @param r The operation's result.
 */
static void hard_end(hitec_prog_t *p, hitec_ses_t r)
{
    hitec_move_t *m = &p->mv;
    const uint8_t why = m->end_why;

    m->phase = (uint8_t)HITEC_MOVE_OFF;
    m->end_why = (uint8_t)HITEC_PROG_MSG_NONE;
    p->must_read = true;
    m->owed = true;
    hpw_off(p);
    if (why != (uint8_t)HITEC_PROG_MSG_NONE || r == HITEC_SES_ERR_CUT
        || r == HITEC_SES_ERR_LINK) {
        cut(p);
    }
    if (why != (uint8_t)HITEC_PROG_MSG_NONE) {
        (void)hpg_say2(p, (hitec_prog_msg_id_t)why, 0u, 0u);
    } else {
        hpg_ses_say(p, r);
    }
}

/**
 * @brief The end sequence starts: 0x1E = 3000.
 *
 * @param p The programmer.
 */
static void end_begin(hitec_prog_t *p)
{
    p->mv.phase = (uint8_t)HITEC_MOVE_CENTRE;
    (void)runs(p, hitec_session_move(&p->ses, HITEC_LIVE_D_T_MID, false));
}

/**
 * @brief One pass: the command toward the followed value, its move and
 *        its position read.
 *
 * @param p The programmer.
 */
static void pass(hitec_prog_t *p)
{
    hitec_move_t *m = &p->mv;
    hitec_value_t v;
    uint16_t want = m->last_p;

    if (hitec_param_get(&p->draft, hitec_prog_layout(p), m->follow, &v)
            == HITEC_GET_OK
        && v.value >= 0 && v.value <= (int32_t)HITEC_D_POS_MAX) {
        want = (uint16_t)v.value;
    }
    /* last_p is the last command, inside the reach: a command is made. */
    (void)hitec_live_d_command(&m->ends, m->last_p, want, &m->cmd);
    (void)runs(p, hitec_session_move(&p->ses, m->cmd.target, true));
}

/**
 * @brief The start's read ended: the judge, and the first pass.
 *
 * @param p The programmer.
 */
static void start_done(hitec_prog_t *p)
{
    hitec_move_t *m = &p->mv;
    const uint16_t pos = hitec_image_reg(&p->ses.image, 0x0Cu);
    uint16_t lo = 0;
    uint16_t hi = 0;

    m->start = p->ses.image;
    if (m->off) {
        /* Nothing moved: nothing to end. */
        m->phase = (uint8_t)HITEC_MOVE_OFF;
        return;
    }
    hitec_live_d_reach(&m->ends, &lo, &hi);
    if ((uint32_t)pos + HITEC_LIVE_LAG_FREE < lo
        || pos > (uint32_t)hi + HITEC_LIVE_LAG_FREE) {
        /* No target puts the horn back there within one slew. */
        m->phase = (uint8_t)HITEC_MOVE_OFF;
        (void)hpg_say2(p, HITEC_PROG_MSG_MOVE_OUTSIDE, pos, 0u);
        return;
    }
    hitec_live_judge_init(&m->j, m->ends.mid, pos, p->ma_open);
    m->pos = pos;
    m->has_pos = true;
    m->last_p = pos;
    m->enc_start = p->f.has_angle;
    m->phase = (uint8_t)HITEC_MOVE_RUN;
    pass(p);
}

/**
 * @brief A pass ended: its judge, then the next pass, the move back or the
 *        end.
 *
 * @param p The programmer.
 */
static void run_done(hitec_prog_t *p)
{
    hitec_move_t *m = &p->mv;
    hitec_live_obs_t o;
    hitec_live_verdict_t v;
    uint16_t back;

    memset(&o, 0, sizeof(o));
    o.now_ms = p->f.now_ms;
    o.p = m->cmd.p;
    o.pos = p->ses.position;
    o.ma = p->f.out_ma;
    o.has_ma = p->f.supply_read ? 1u : 0u;
    o.angle = p->f.angle;
    o.has_angle = p->f.has_angle ? 1u : 0u;
    m->pos = p->ses.position;
    m->has_pos = p->ses.has_position != 0u;
    m->last_p = m->cmd.p;
    v = hitec_live_judge(&m->j, &o);
    if (v == HITEC_LIVE_FREE || v == HITEC_LIVE_FOLLOWING) {
        if (m->off) {
            end_begin(p);
        } else {
            pass(p);
        }
        return;
    }
    back = hitec_live_back(&m->j);
    if (v == HITEC_LIVE_BOUND_LAG) {
        (void)hpg_say2(p, HITEC_PROG_MSG_MOVE_BLOCKED, o.pos, back);
    } else if (v == HITEC_LIVE_BOUND_CURRENT) {
        (void)hpg_say2(p, HITEC_PROG_MSG_MOVE_CURRENT, m->j.stall_ma, back);
    } else {
        (void)hpg_say2(p, HITEC_PROG_MSG_MOVE_ENC, 0u, back);
    }
    m->bound = true;
    m->last_p = back;
    m->phase = (uint8_t)HITEC_MOVE_BACK;
    (void)runs(p, hitec_session_move(
                      &p->ses, hitec_live_d_target(&m->ends, back), true));
}

/**
 * @brief The end's reboot: 0x46 = 1, its wait, and the full read.
 *
 * @param p The programmer.
 */
static void reboot(hitec_prog_t *p)
{
    hitec_plan_t plan;

    memset(&plan, 0, sizeof(plan));
    plan.n = 1u;
    plan.step[0].kind = (uint8_t)HITEC_STEP_D_REBOOT;
    plan.step[0].addr = HITEC_D_REG_REBOOT;
    plan.step[0].value = 1u;
    plan.step[0].wait_ms = HITEC_D_REBOOT_WAIT_MS;
    p->mv.phase = (uint8_t)HITEC_MOVE_REBOOT;
    (void)runs(p, hitec_session_run_plan(&p->ses, &plan, NULL));
}

/**
 * @brief Whether register @p a of the end differs from the start's, or
 *        either does not know it.
 *
 * @param m The loop.
 * @param img The session's image at the end.
 * @param a The register.
 * @return true when it does.
 */
static bool differs(const hitec_move_t *m, const hitec_image_t *img,
                    uint8_t a)
{
    return !hitec_image_known(&m->start, a) || !hitec_image_known(img, a)
           || hitec_image_reg(&m->start, a) != hitec_image_reg(img, a);
}

/**
 * @brief The end's read ended: the loop's set and every register a plan
 *        writes held against the start.
 *
 * @param p The programmer.
 */
static void check_done(hitec_prog_t *p)
{
    hitec_move_t *m = &p->mv;
    bool changed = false;

    for (unsigned i = 0; i < sizeof(k_set); ++i) {
        changed = changed || differs(m, &p->ses.image, k_set[i]);
    }
    for (unsigned a = 0; a < HITEC_MEM_SIZE; a += 2u) {
        if (hitec_writable((uint8_t)HITEC_D, (uint8_t)a)) {
            changed = changed || differs(m, &p->ses.image, (uint8_t)a);
        }
    }
    m->phase = (uint8_t)HITEC_MOVE_OFF;
    if (changed) {
        p->must_read = true;
        m->owed = true;
        hpw_off(p);
        (void)hpg_say2(p, HITEC_PROG_MSG_MOVE_CHANGED, 0u, 0u);
    } else if (!m->bound) {
        (void)hpg_say2(p, HITEC_PROG_MSG_MOVE_DONE, 0u, 0u);
    }
}

void hpm_done(hitec_prog_t *p, hitec_ses_t r)
{
    hitec_move_t *m = &p->mv;

    if (r == HITEC_SES_ERR_ABORTED
        && m->end_why == (uint8_t)HITEC_PROG_MSG_NONE
        && m->phase <= (uint8_t)HITEC_MOVE_BACK) {
        /* The operator's ABORT before the end: the end runs, as MOVE OFF's
         * does; before the first pass nothing moved. */
        hpg_ses_say(p, r);
        m->bound = true;
        if (m->phase == (uint8_t)HITEC_MOVE_START) {
            m->phase = (uint8_t)HITEC_MOVE_OFF;
        } else {
            end_begin(p);
        }
        return;
    }
    if (r != HITEC_SES_OK || m->end_why != (uint8_t)HITEC_PROG_MSG_NONE) {
        hard_end(p, r);
        return;
    }
    switch (m->phase) {
    case HITEC_MOVE_START:
        start_done(p);
        break;
    case HITEC_MOVE_RUN:
        run_done(p);
        break;
    case HITEC_MOVE_BACK:
        end_begin(p);
        break;
    case HITEC_MOVE_CENTRE:
        reboot(p);
        break;
    case HITEC_MOVE_REBOOT:
        m->phase = (uint8_t)HITEC_MOVE_CHECK;
        (void)runs(p, hitec_session_read_set(&p->ses, k_extra,
                                             (uint16_t)sizeof(k_extra)));
        break;
    default: /* HITEC_MOVE_CHECK */
        check_done(p);
        break;
    }
}

void hpm_tick(hitec_prog_t *p)
{
    hitec_move_t *m = &p->mv;
    hitec_prog_msg_id_t why = HITEC_PROG_MSG_NONE;

    if (!p->must_read) {
        /* READ ALL took it, or nothing asks for it. */
        m->owed = false;
    }
    if (m->phase == (uint8_t)HITEC_MOVE_OFF
        || m->end_why != (uint8_t)HITEC_PROG_MSG_NONE) {
        return;
    }
    if (p->f.stopped || hitec_prog_stopped(p)) {
        why = HITEC_PROG_MSG_STOPPED;
    } else if (!p->f.supply_read) {
        /* A reading that feeds a protection: no gap allowed. */
        why = HITEC_PROG_MSG_MOVE_SUPPLY;
    } else if (m->enc_start && !p->f.has_angle) {
        why = HITEC_PROG_MSG_MOVE_ENC_LOST;
    }
    if (why != HITEC_PROG_MSG_NONE) {
        m->end_why = (uint8_t)why;
        hitec_prog_abort(p);
    }
}

void hpm_gone(hitec_prog_t *p)
{
    p->mv.phase = (uint8_t)HITEC_MOVE_OFF;
    p->mv.end_why = (uint8_t)HITEC_PROG_MSG_NONE;
}

bool hpm_edit_ok(const hitec_prog_t *p, uint8_t id)
{
    return hitec_prog_move_live(p, id);
}

/* ------------------------------------------------------------- interface */

hitec_prog_msg_id_t hitec_prog_move_gate(const hitec_prog_t *p)
{
    hitec_live_ends_t ends;
    hitec_prog_msg_id_t m = hpg_held_idle(p, true);

    if (m != HITEC_PROG_MSG_NONE) {
        return m;
    }
    if (!hitec_prog_in_mode(p)) {
        return HITEC_PROG_MSG_NOT_IN_MODE;
    }
    if (!p->have_servo) {
        return HITEC_PROG_MSG_NOT_READ;
    }
    if (p->run || hitec_prog_card_busy(p)) {
        return HITEC_PROG_MSG_BUSY;
    }
    if (p->must_read) {
        return HITEC_PROG_MSG_MUST_READ;
    }
    if (p->rec) {
        return HITEC_PROG_MSG_RECORD_HELD;
    }
    if (p->servo.series != (uint8_t)HITEC_D) {
        return HITEC_PROG_MSG_MOVE_NO_MAP;
    }
    if (!hitec_image_known(&p->servo, 0x5Eu)
        || hitec_image_reg(&p->servo, 0x5Eu) != (uint16_t)HITEC_CW
        || hitec_image_reg(&p->draft, 0x5Eu) != (uint16_t)HITEC_CW) {
        return HITEC_PROG_MSG_MOVE_NEED_DIR;
    }
    if (!hitec_live_d_ends(&p->servo, &ends)) {
        return HITEC_PROG_MSG_MOVE_NO_MAP;
    }
    if (!hitec_prog_rail(p) || !p->f.supply_read) {
        return HITEC_PROG_MSG_MOVE_NEED_PD;
    }
    return HITEC_PROG_MSG_NONE;
}

hitec_prog_msg_id_t hitec_prog_move_start(hitec_prog_t *p)
{
    hitec_move_t *m = &p->mv;
    const hitec_prog_msg_id_t g = hitec_prog_move_gate(p);

    if (g != HITEC_PROG_MSG_NONE) {
        return hpg_say2(p, g, 0u, 0u);
    }
    memset(m, 0, sizeof(*m));
    (void)hitec_live_d_ends(&p->servo, &m->ends);
    m->follow = (uint8_t)HITEC_P_D_NEUTRAL;
    m->phase = (uint8_t)HITEC_MOVE_START;
    /* The gate holds the session idle and in mode: the read starts. */
    (void)runs(p, hitec_session_read_set(&p->ses, k_start,
                                         (uint16_t)sizeof(k_start)));
    hpg_pump(p);
    return HITEC_PROG_MSG_NONE;
}

void hitec_prog_move_stop(hitec_prog_t *p)
{
    if (hitec_prog_move_on(p)) {
        p->mv.off = true;
    }
}

hitec_move_phase_t hitec_prog_move_phase(const hitec_prog_t *p)
{
    return (hitec_move_phase_t)p->mv.phase;
}

bool hitec_prog_move_on(const hitec_prog_t *p)
{
    return (p->mv.phase == (uint8_t)HITEC_MOVE_START
            || p->mv.phase == (uint8_t)HITEC_MOVE_RUN)
           && !p->mv.off && p->mv.end_why == (uint8_t)HITEC_PROG_MSG_NONE;
}

bool hitec_prog_move_live(const hitec_prog_t *p, uint8_t id)
{
    return hitec_prog_move_on(p) && position_id(id);
}

hitec_prog_edit_t hitec_prog_move_edit(hitec_prog_t *p, uint8_t id,
                                       int32_t value)
{
    const uint16_t consent = (uint16_t)(HITEC_RULE_BIT(HITEC_RULE_END)
                                        | HITEC_RULE_BIT(HITEC_RULE_CENTRE));
    hitec_prog_edit_t e;

    if (!hitec_prog_move_live(p, id)) {
        (void)hpg_say2(p, HITEC_PROG_MSG_BUSY, 0u, 0u);
        return HITEC_PROG_EDIT_REFUSED;
    }
    e = hitec_prog_edit(p, id, value);
    if (e == HITEC_PROG_EDIT_CHECK) {
        /* A position breaks no rule but these two: the hold took them. */
        if ((hitec_prog_pending(p, NULL, NULL) & (uint16_t)~consent) == 0u) {
            e = hitec_prog_confirm(p);
        } else {
            hitec_prog_cancel(p);
            (void)hpg_say2(p, HITEC_PROG_MSG_EDIT_REFUSED,
                           (unsigned)HITEC_SET_ERR_RO, 0u);
            e = HITEC_PROG_EDIT_REFUSED;
        }
    }
    if (e == HITEC_PROG_EDIT_OK) {
        p->mv.follow = id;
    }
    return e;
}

bool hitec_prog_move_view(const hitec_prog_t *p, hitec_move_view_t *out)
{
    const hitec_move_t *m = &p->mv;
    hitec_value_t v;

    memset(out, 0, sizeof(*out));
    out->phase = m->phase;
    if (m->phase == (uint8_t)HITEC_MOVE_OFF) {
        return false;
    }
    out->follow = m->follow;
    out->has_pos = m->has_pos;
    out->pos = m->pos;
    out->has_ma = p->f.supply_read;
    out->ma = p->f.out_ma;
    out->has_angle = p->f.has_angle;
    out->angle = p->f.angle;
    hitec_live_d_reach(&m->ends, &out->lo, &out->hi);
    if (hitec_param_get(&p->draft, hitec_prog_layout(p), m->follow, &v)
        == HITEC_GET_OK) {
        out->clamped = v.value < (int32_t)out->lo || v.value > (int32_t)out->hi;
    }
    return true;
}

uint16_t hitec_prog_move_stall_ma(const hitec_prog_t *p)
{
    return hitec_live_stall_ma(p->owned ? p->ma_open : hitec_prog_ma(p));
}
