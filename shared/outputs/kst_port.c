/*
 * SPDX-License-Identifier: MIT
 */

#include "kst_port.h"

#include <stddef.h>
#include <string.h>

#include "kst_plan.h"

/* The page carries the session's result as the core numbers it. */
_Static_assert(KST_SES_OK == 0 && KST_SES_BUSY == 1, "kst_ses_t on the page");
_Static_assert(KST_SES_ERR_NO_SERVO == 4 && KST_SES_ERR_NOT_IN_MODE == 5
               && KST_SES_ERR_READ == 6 && KST_SES_ERR_NO_BACKUP == 7
               && KST_SES_ERR_FINGERPRINT == 8 && KST_SES_ERR_LOCKED == 9
               && KST_SES_ERR_BAD_PLAN == 10 && KST_SES_ERR_UNCONFIRMED == 11
               && KST_SES_ERR_CHANGED == 12 && KST_SES_ERR_WRITE_FAILED == 13
               && KST_SES_ERR_ROLLED_BACK == 14 && KST_SES_ERR_TORN == 15
               && KST_SES_ERR_UNINTENDED == 16 && KST_SES_ERR_VERIFY == 17
               && KST_SES_ERR_DRIVER == 18 && KST_SES_ERR_ABORTED == 19,
               "kst_ses_t on the page");
_Static_assert(KST_F_COUNT <= 64, "the unlock set is 4 registers");
_Static_assert(KST_REG_COUNT == 2u * LINK_KS_IMAGE_REGS, "image registers");
_Static_assert(LINK_KS_W_IMAGE + LINK_KS_W_STAGED == LINK_KS_W_VIEW,
               "staged registers");

_Static_assert(LINK_KST_RESULT_SUPPLY > KST_SES_ERR_ABORTED,
               "the supply's result is no kst_ses_t");
_Static_assert(LINK_KS_W_SUPPLY_MV == LINK_KS_W_VIEW + 1
               && LINK_KS_W_SUPPLY_MA == LINK_KS_W_VIEW + 2
               && LINK_KS_W_COUNT == LINK_KS_W_VIEW + 3,
               "the registers after the staged ones");

#define NO_REG 0xFFu
#define FRAMES_MAX 0x0FFFu

/* Where a wait for the supply is. */
enum {
    SUP_NONE = 0, /* no supply in this step */
    SUP_OFF,      /* waits for the read that shows it off */
    SUP_ON,       /* waits for the read that shows it on */
    SUP_TIMED,    /* read; the time after it runs from t1_ms */
};

/* Why PROGRAMMING is left once the session has ended. */
enum {
    LEAVE_NONE = 0,
    LEAVE_ASKED,  /* POWER_CYCLED or REBOOT */
    LEAVE_FORCED, /* the supply went off under the port */
};

/* --- the driver the session sees --------------------------------------------- */

/* A frame starts only on a channel in PROGRAMMING and while the bench is
 * safe.  The first one marks the servo as possibly in programming mode. */
static bool drv_start(void *ctx, const kst_frame_t *frame, uint32_t window_ns)
{
    kst_port_t *p = (kst_port_t *)ctx;

    if (p->state != (uint8_t)LINK_KST_PROGRAMMING || p->safe == 0u
        || !p->hw.driver.start(p->hw.driver.ctx, frame, window_ns)) {
        return false;
    }
    p->framed = 1;
    if (p->frames < FRAMES_MAX) {
        p->frames++;
    }
    return true;
}

static bool drv_poll(void *ctx, kst_capture_t *out)
{
    kst_port_t *p = (kst_port_t *)ctx;

    return p->hw.driver.poll(p->hw.driver.ctx, out);
}

static uint32_t drv_now(void *ctx)
{
    kst_port_t *p = (kst_port_t *)ctx;

    return p->hw.driver.now_us(p->hw.driver.ctx);
}

static void session_fresh(kst_port_t *p)
{
    kst_driver_t d;

    d.ctx = p;
    d.start = drv_start;
    d.poll = drv_poll;
    d.now_us = drv_now;
    (void)kst_session_init(&p->session, &d);
}

/* --- images on the page ------------------------------------------------------- */

static void image_to_regs(const kst_image_t *img, uint16_t *regs)
{
    for (size_t k = 0; k < LINK_KS_IMAGE_REGS; ++k) {
        regs[k] = (uint16_t)(img->r[2u * k]
                             | ((uint16_t)img->r[2u * k + 1u] << 8));
    }
}

static void image_from_regs(const uint16_t *regs, kst_image_t *img)
{
    for (size_t k = 0; k < LINK_KS_IMAGE_REGS; ++k) {
        img->r[2u * k] = (uint8_t)(regs[k] & 0xFFu);
        img->r[2u * k + 1u] = (uint8_t)(regs[k] >> 8);
    }
}

static uint8_t lowest_bit(uint32_t mask)
{
    for (uint8_t i = 0; i < KST_REG_COUNT; ++i) {
        if ((mask & ((uint32_t)1 << i)) != 0u) {
            return i;
        }
    }
    return NO_REG;
}

/* --- the supply --------------------------------------------------------------- */

/* What the supply says; false with no PD mini answering. */
static bool supply_view(const kst_port_t *p, supply_port_view_t *v)
{
    memset(v, 0, sizeof(*v));
    if (p->hw.supply == NULL) {
        return false;
    }
    p->hw.supply(p->hw.ctx, v);
    return v->online;
}

static void supply_off(kst_port_t *p)
{
    if (p->hw.supply_set != NULL) {
        (void)p->hw.supply_set(p->hw.ctx, false, 0u, 0u);
    }
}

/* Whether the PD mini gives a servo in programming mode @p mv and @p ma. */
static bool supply_range_ok(const supply_port_view_t *v, uint16_t mv,
                            uint16_t ma)
{
    if (mv < PDMINI_V_MIN_MV || mv > KST_PORT_SUPPLY_MAX_MV
        || ma < PDMINI_I_MIN_MA || ma > PDMINI_I_MAX_MA) {
        return false;
    }
    return v->vin_mv == 0u
           || (uint32_t)mv + PDMINI_HEADROOM_MV <= v->vin_mv;
}

/* --- operations --------------------------------------------------------------- */

static void op_begin(kst_port_t *p, link_kst_op_t op)
{
    p->op = (uint8_t)op;
    p->result = (uint8_t)KST_SES_BUSY;
    p->frames = 0;
    p->fail_reg = NO_REG;
    p->steps_done = 0;
    p->seen_step = 0;
    p->busy = 1;
    p->running = 0;
}

static void op_end(kst_port_t *p, kst_ses_t result)
{
    p->result = (uint8_t)result;
    p->busy = 0;
    p->running = 0;
}

/* The session's operation ended with @p r. */
static void session_done(kst_port_t *p, kst_ses_t r)
{
    const kst_session_t *s = &p->session;

    p->steps_done = s->step_index;
    switch (r) {
    case KST_SES_ERR_WRITE_FAILED:
    case KST_SES_ERR_ROLLED_BACK:
    case KST_SES_ERR_TORN:
        p->fail_reg = p->seen_step < p->plan_n ? p->plan_reg[p->seen_step]
                                               : NO_REG;
        break;
    case KST_SES_ERR_READ:
        p->fail_reg = lowest_bit(s->bad_regs);
        break;
    case KST_SES_ERR_VERIFY:
    case KST_SES_ERR_CHANGED:
    case KST_SES_ERR_UNINTENDED:
        p->fail_reg = lowest_bit(s->diff_regs);
        break;
    default:
        p->fail_reg = NO_REG;
        break;
    }
    if (r == KST_SES_OK && p->op == (uint8_t)LINK_KST_OP_READ_ALL) {
        p->must_read = 0;
    }
    op_end(p, r);
}

/* What a session function returned when asked to start. */
static void session_started(kst_port_t *p, kst_ses_t r)
{
    if (r == KST_SES_BUSY) {
        p->running = 1;
    } else {
        session_done(p, r);
    }
}

/* Back to PWM: the driver closed, the pin the generator's. */
static void go_pwm(kst_port_t *p)
{
    const bool had_pin = p->has_pin != 0u;

    p->state = (uint8_t)LINK_KST_PWM;
    p->framed = 0;
    p->must_read = 0;
    p->leaving = LEAVE_NONE;
    p->has_pin = 0;
    p->sup = SUP_NONE;
    p->on_here = 0;
    p->watch = 0;
    p->from_pwm = 0;
    if (had_pin) {
        p->hw.give(p->hw.ctx);
    }
}

/* The way in ended before a frame went out: the supply as it was found. */
static void back_out(kst_port_t *p, uint8_t result)
{
    if (p->on_here != 0u) {
        supply_off(p);
    }
    go_pwm(p);
    p->result = result;
    p->busy = 0;
    p->running = 0;
}

/* End the running operation.  A frame on the wire is finished first; the
 * session ends at one of the next steps. */
static void cut(kst_port_t *p)
{
    if (p->running != 0u) {
        kst_session_abort(&p->session);
    }
}

static void on_stop(kst_port_t *p)
{
    switch ((link_kst_state_t)p->state) {
    case LINK_KST_STOPPING:
    case LINK_KST_LOW:
        back_out(p, (uint8_t)KST_SES_ERR_ABORTED);
        break;
    case LINK_KST_PROGRAMMING:
        cut(p);
        if (p->framed != 0u) {
            p->must_read = 1;
        }
        break;
    case LINK_KST_PWM:
    case LINK_KST_RAIL_OFF:
    case LINK_KST_RAIL_WAIT:
    default:
        break;
    }
}

static link_kst_refusal_t enter_from_pwm(kst_port_t *p, uint8_t port,
                                         uint32_t now_ms)
{
    const uint8_t pin = p->hw.pin(p->hw.ctx, port);
    supply_port_view_t v;

    if (pin > OUT_MAX_PIN) {
        return LINK_KST_REF_NO_PIN;
    }
    if (supply_view(p, &v) && !supply_range_ok(&v, p->supply_mv,
                                               p->supply_ma)) {
        return LINK_KST_REF_SUPPLY_RANGE;
    }
    p->port = port;
    p->pin = pin;
    p->want_mv = p->supply_mv;
    p->want_ma = p->supply_ma;
    p->state = (uint8_t)LINK_KST_STOPPING;
    p->t0_ms = now_ms;
    op_begin(p, LINK_KST_OP_ENTER);
    return LINK_KST_REF_NONE;
}

/* The supply switched off for RAIL_OFF; @p again: and on after it, at what
 * it was set to. */
static void rail_off(kst_port_t *p, const supply_port_view_t *v, bool again,
                     uint32_t now_ms)
{
    p->again = again ? 1u : 0u;
    p->back_mv = v->set_mv;
    p->back_ma = v->set_ma;
    supply_off(p);
    p->state = (uint8_t)LINK_KST_RAIL_OFF;
    p->sup = SUP_OFF;
    p->t0_ms = now_ms;
}

static link_kst_refusal_t reboot(kst_port_t *p, uint8_t port, uint32_t now_ms)
{
    const link_kst_state_t state = (link_kst_state_t)p->state;
    supply_port_view_t v;

    if (p->safe == 0u) {
        return LINK_KST_REF_UNSAFE;
    }
    if ((state != LINK_KST_PWM && state != LINK_KST_PROGRAMMING)
        || p->leaving != LEAVE_NONE) {
        return LINK_KST_REF_BUSY;
    }
    if (state == LINK_KST_PROGRAMMING && port != p->port) {
        return LINK_KST_REF_PORT;
    }
    if (!supply_view(p, &v) || !v.asked_on) {
        return LINK_KST_REF_NO_SUPPLY;
    }
    if (state == LINK_KST_PWM) {
        p->port = port;
        p->from_pwm = 1;
        op_begin(p, LINK_KST_OP_REBOOT);
        rail_off(p, &v, true, now_ms);
        return LINK_KST_REF_NONE;
    }
    /* The frame on the wire ends first.  The session keeps the mode until
     * the supply is read off. */
    cut(p);
    {
        const uint8_t was_running = p->running;

        op_begin(p, LINK_KST_OP_REBOOT);
        p->running = was_running;
    }
    p->leaving = LEAVE_ASKED;
    return LINK_KST_REF_NONE;
}

static link_kst_refusal_t plan_refusal(kst_plan_result_t r)
{
    switch (r) {
    case KST_PLAN_OK:          return LINK_KST_REF_NONE;
    case KST_PLAN_ERR_R00:     return LINK_KST_REF_PLAN_R00;
    case KST_PLAN_ERR_PAIRING: return LINK_KST_REF_PLAN_PAIRING;
    case KST_PLAN_ERR_NO_PATH: return LINK_KST_REF_PLAN_NO_PATH;
    case KST_PLAN_ERR_ARG:
    case KST_PLAN_ERR_RULES:
    default:                   return LINK_KST_REF_PLAN_RULES;
    }
}

/* A write to the servo: the plan from the image read to what is staged. */
static link_kst_refusal_t start_plan(kst_port_t *p, link_kst_op_t op,
                                     bool confirm)
{
    kst_session_t *s = &p->session;
    kst_plan_t *plan = &p->plan;
    kst_image_t *staged = &p->work;
    kst_plan_result_t r;

    if (s->has_image == 0u) {
        return LINK_KST_REF_NO_IMAGE;
    }
    image_from_regs(&p->staged[0], staged);
    if (op == LINK_KST_OP_RESTORE) {
        r = kst_plan_restore(&s->image, staged, plan);
    } else {
        uint16_t regs[LINK_KS_IMAGE_REGS];
        const uint16_t *u = &p->staged[LINK_KS_W_UNLOCK - LINK_KS_W_IMAGE];
        const kst_field_set_t unlocked =
            (kst_field_set_t)u[0] | ((kst_field_set_t)u[1] << 16)
            | ((kst_field_set_t)u[2] << 32) | ((kst_field_set_t)u[3] << 48);

        /* The panel planned from the image this session holds. */
        image_to_regs(&s->image, regs);
        if (link_kst_crc(regs, LINK_KS_IMAGE_REGS)
            != p->staged[LINK_KS_W_START_CRC - LINK_KS_W_IMAGE]) {
            return LINK_KST_REF_START;
        }
        r = op == LINK_KST_OP_WRITE
                ? kst_plan_edit(&s->image, staged, unlocked, plan, NULL)
                : kst_plan_release_pairing(&s->image, plan);
    }
    if (r != KST_PLAN_OK) {
        return plan_refusal(r);
    }
    op_begin(p, op);
    p->plan_n = plan->n;
    for (unsigned i = 0; i < plan->n; ++i) {
        p->plan_reg[i] = plan->step[i].reg;
    }
    session_started(p, kst_session_write(s, plan, confirm));
    return LINK_KST_REF_NONE;
}

static bool writes_servo(link_kst_op_t op)
{
    return op == LINK_KST_OP_WRITE || op == LINK_KST_OP_RESTORE
           || op == LINK_KST_OP_RELEASE;
}

static link_kst_refusal_t dispatch(kst_port_t *p, link_kst_op_t op,
                                   uint8_t port, const uint16_t *in,
                                   uint32_t now_ms)
{
    const link_kst_state_t state = (link_kst_state_t)p->state;

    if (op == LINK_KST_OP_ABORT) {
        if (state == LINK_KST_STOPPING || state == LINK_KST_LOW) {
            back_out(p, (uint8_t)KST_SES_ERR_ABORTED);
        } else if (state == LINK_KST_PROGRAMMING && p->running != 0u) {
            cut(p);
            if (p->framed != 0u) {
                p->must_read = 1;
            }
        } else if (state == LINK_KST_RAIL_OFF) {
            /* The cycle ends off: nothing is switched on after an abort. */
            p->again = 0;
        } else if (state == LINK_KST_RAIL_WAIT) {
            supply_off(p);
            go_pwm(p);
            op_end(p, KST_SES_ERR_ABORTED);
        }
        return LINK_KST_REF_NONE;
    }
    /* A command that names no programming port names an output channel,
     * as a panel before 4.13 does: none of those is a programming port. */
    if ((in[LINK_KS_W_CMD] & LINK_KS_W_CMD_PORT) == 0u
        || port >= p->hw.ports) {
        return LINK_KST_REF_PORT;
    }
    /* Before every other answer: a write without the enable is refused as
     * that, whatever else is wrong with it. */
    if (writes_servo(op) && in[LINK_KS_W_KEY] != LINK_KST_WRITE_KEY) {
        return LINK_KST_REF_NO_ENABLE;
    }
    if (op == LINK_KST_OP_REBOOT) {
        return reboot(p, port, now_ms);
    }
    if (op == LINK_KST_OP_POWER_CYCLED) {
        if (state == LINK_KST_PWM) {
            return LINK_KST_REF_NOT_PROGRAMMING;
        }
        if (state != LINK_KST_PROGRAMMING || p->leaving != LEAVE_NONE) {
            return LINK_KST_REF_BUSY;
        }
        if (port != p->port) {
            return LINK_KST_REF_PORT;
        }
        kst_session_power_cycled(&p->session);
        {
            const uint8_t was_running = p->running;

            op_begin(p, LINK_KST_OP_POWER_CYCLED);
            p->running = was_running;
        }
        p->leaving = LEAVE_ASKED;
        return LINK_KST_REF_NONE;
    }
    if (p->safe == 0u) {
        return LINK_KST_REF_UNSAFE;
    }
    if (state == LINK_KST_PWM) {
        return op == LINK_KST_OP_ENTER ? enter_from_pwm(p, port, now_ms)
                                       : LINK_KST_REF_NOT_PROGRAMMING;
    }
    if (state != LINK_KST_PROGRAMMING || p->busy != 0u
        || p->leaving != LEAVE_NONE) {
        return LINK_KST_REF_BUSY;
    }
    if (port != p->port) {
        return LINK_KST_REF_PORT;
    }
    if (p->must_read != 0u && op != LINK_KST_OP_ENTER
        && op != LINK_KST_OP_READ_ALL) {
        return LINK_KST_REF_MUST_READ;
    }
    if (op == LINK_KST_OP_ENTER) {
        op_begin(p, op);
        session_started(p, kst_session_enter(&p->session));
        return LINK_KST_REF_NONE;
    }
    if (op == LINK_KST_OP_READ_ALL) {
        op_begin(p, op);
        session_started(p, kst_session_read_all(&p->session));
        return LINK_KST_REF_NONE;
    }
    if (link_kst_crc(p->staged, LINK_KS_W_STAGED) != in[LINK_KS_W_CRC]) {
        return LINK_KST_REF_STAGED;
    }
    if (op == LINK_KST_OP_VERIFY) {
        image_from_regs(&p->staged[0], &p->work);
        op_begin(p, op);
        session_started(p, kst_session_verify(&p->session, &p->work));
        return LINK_KST_REF_NONE;
    }
    return start_plan(p, op, (in[LINK_KS_W_CMD] & LINK_KS_W_CMD_CONFIRM) != 0u);
}

static uint8_t command(kst_port_t *p, const uint16_t *in, uint32_t now_ms)
{
    const uint16_t cmd = in[LINK_KS_W_CMD];
    const uint8_t op = (uint8_t)(cmd & 0xFu);
    const uint8_t port = (uint8_t)((cmd >> 8) & 0xFu);

    if ((cmd & 0xF030u) != 0u || op == (uint8_t)LINK_KST_OP_NONE
        || op >= (uint8_t)LINK_KST_OP_COUNT
        || ((cmd & LINK_KS_W_CMD_CONFIRM) != 0u
            && op != (uint8_t)LINK_KST_OP_RESTORE)
        || (in[LINK_KS_W_KEY] != 0u
            && in[LINK_KS_W_KEY] != LINK_KST_WRITE_KEY)) {
        return LINK_NACK_BAD_VALUE;
    }
    /* The frame just taken, sent again: its answer was lost. */
    if (memcmp(in, p->last_cmd, sizeof(p->last_cmd)) == 0) {
        return 0u;
    }
    if (in[LINK_KS_W_SEQ] != (uint16_t)(p->seq + 1u)) {
        return LINK_NACK_BAD_VALUE;
    }
    p->seq = in[LINK_KS_W_SEQ];
    memcpy(p->last_cmd, in, sizeof(p->last_cmd));
    p->refusal = (uint8_t)dispatch(p, (link_kst_op_t)op, port, in, now_ms);
    return 0u;
}

uint8_t kst_port_write(kst_port_t *p, uint8_t off, uint8_t n,
                       const uint16_t *in, uint32_t now_ms)
{
    if (p == NULL || in == NULL || n == 0u
        || (unsigned)off + n > LINK_KS_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (off < LINK_KS_W_CMD_FRAME) {
        if (off != 0u || n != LINK_KS_W_CMD_FRAME) {
            return LINK_NACK_BAD_RANGE;
        }
        return command(p, in, now_ms);
    }
    if ((unsigned)off + n > LINK_KS_W_COUNT) {
        return LINK_NACK_READ_ONLY;
    }
    if (off <= LINK_KS_W_VIEW && (unsigned)off + n > LINK_KS_W_VIEW
        && in[LINK_KS_W_VIEW - off] > 1u) {
        return LINK_NACK_BAD_VALUE;
    }
    for (unsigned i = 0; i < n; ++i) {
        const unsigned at = off + i;

        if (at == LINK_KS_W_VIEW) {
            p->view = (uint8_t)in[i];
        } else if (at == LINK_KS_W_SUPPLY_MV) {
            p->supply_mv = in[i];
        } else if (at == LINK_KS_W_SUPPLY_MA) {
            p->supply_ma = in[i];
        } else if (at - LINK_KS_W_IMAGE < LINK_KS_W_STAGED) {
            p->staged[at - LINK_KS_W_IMAGE] = in[i];
        }
    }
    return 0u;
}

/* --- the page ----------------------------------------------------------------- */

static uint16_t clamp16(uint32_t v)
{
    return v > 0xFFFFu ? 0xFFFFu : (uint16_t)v;
}

static uint16_t state_reg(const kst_port_t *p)
{
    const kst_session_t *s = &p->session;
    uint16_t r = (uint16_t)(p->state | ((uint16_t)p->port << 12));
    supply_port_view_t v;

    r |= p->busy != 0u ? LINK_KS_F_BUSY : 0u;
    r |= p->framed != 0u ? LINK_KS_F_IN_MODE : 0u;
    r |= p->must_read != 0u ? LINK_KS_F_MUST_READ : 0u;
    r |= s->has_image != 0u ? LINK_KS_F_IMAGE : 0u;
    r |= s->has_backup != 0u ? LINK_KS_F_BACKUP : 0u;
    r |= s->fingerprint_ok != 0u ? LINK_KS_F_FP_OK : 0u;
    r |= s->locked != 0u ? LINK_KS_F_LOCKED : 0u;
    r |= supply_view(p, &v) ? LINK_KS_F_RAIL : 0u;
    r |= p->view != 0u ? LINK_KS_F_VIEW_BACKUP : 0u;
    return r;
}

void kst_port_read(const kst_port_t *p, uint8_t off, uint8_t n,
                   uint16_t *out)
{
    uint16_t all[LINK_KS_COUNT];
    const kst_session_t *s;
    bool replied;

    if (p == NULL || out == NULL || (unsigned)off + n > LINK_KS_COUNT) {
        return;
    }
    s = &p->session;
    replied = s->stats.replies != 0u;
    memset(all, 0, sizeof(all));
    all[LINK_KS_STATE] = state_reg(p);
    all[LINK_KS_SEQ] = p->seq;
    all[LINK_KS_OP] = (uint16_t)(((uint16_t)p->op << 12) | p->frames);
    all[LINK_KS_RESULT] = (uint16_t)(((uint16_t)p->refusal << 8) | p->result);
    all[LINK_KS_FAIL] = (uint16_t)(((uint16_t)p->steps_done << 8)
                                   | p->fail_reg);
    all[LINK_KS_DIFF_LO] = (uint16_t)(s->diff_regs & 0xFFFFu);
    all[LINK_KS_DIFF_HI] = (uint16_t)(s->diff_regs >> 16);
    all[LINK_KS_BAD_LO] = (uint16_t)(s->bad_regs & 0xFFFFu);
    all[LINK_KS_BAD_HI] = (uint16_t)(s->bad_regs >> 16);
    if (s->has_image != 0u) {
        all[LINK_KS_FP_RULES] = (uint16_t)s->fingerprint.rules;
        all[LINK_KS_FP_REGS_LO] = (uint16_t)(s->fingerprint.regs & 0xFFFFu);
        all[LINK_KS_FP_REGS_HI] = (uint16_t)(s->fingerprint.regs >> 16);
    }
    if (replied) {
        all[LINK_KS_HALF_MIN_NS] = clamp16(s->stats.half_min_ns);
        all[LINK_KS_HALF_MAX_NS] = clamp16(s->stats.half_max_ns);
        all[LINK_KS_DELAY_MIN_US] = clamp16(s->stats.delay_min_ns / 1000u);
        all[LINK_KS_DELAY_MAX_US] = clamp16(s->stats.delay_max_ns / 1000u);
    }
    if (p->view != 0u && s->has_backup != 0u) {
        image_to_regs(&s->backup, &all[LINK_KS_IMAGE]);
    } else if (p->view == 0u && s->has_image != 0u) {
        image_to_regs(&s->image, &all[LINK_KS_IMAGE]);
    }
    for (unsigned i = 0; i < n; ++i) {
        out[i] = all[off + i];
    }
}

/* --- the pass ----------------------------------------------------------------- */

/* The servo is asked to enter programming mode. */
static void start_session(kst_port_t *p)
{
    p->state = (uint8_t)LINK_KST_PROGRAMMING;
    p->watch = p->sup != SUP_NONE ? 1u : 0u;
    p->sup = SUP_NONE;
    session_fresh(p);
    session_started(p, kst_session_enter(&p->session));
}

/* The pin is the driver's: what the supply has to do before the servo is
 * spoken to.  The voltage of an output that is on is lowered or left, never
 * raised. */
static void low_begin(kst_port_t *p)
{
    supply_port_view_t v;

    p->sup = SUP_NONE;
    p->on_here = 0;
    if (!supply_view(p, &v)) {
        return;                 /* no PD mini: the servo's supply is not ours */
    }
    if (!supply_range_ok(&v, p->want_mv, p->want_ma)) {
        back_out(p, (uint8_t)LINK_KST_RESULT_SUPPLY);
        return;
    }
    if (v.asked_on) {
        if (v.set_mv > p->want_mv
            && !p->hw.supply_set(p->hw.ctx, true, p->want_mv, v.set_ma)) {
            back_out(p, (uint8_t)LINK_KST_RESULT_SUPPLY);
            return;
        }
        p->sup = SUP_ON;
    } else {
        /* Asked off.  One that is on by itself is seen off first. */
        p->sup = SUP_OFF;
    }
}

static void step_low(kst_port_t *p, uint32_t now_ms)
{
    const uint32_t waited = (uint32_t)(now_ms - p->t0_ms);
    supply_port_view_t v;

    if (p->sup == SUP_NONE) {
        if (waited >= KST_PORT_LOW_MS) {
            start_session(p);
        }
        return;
    }
    (void)supply_view(p, &v);
    if (p->sup == SUP_OFF && v.online && v.off) {
        if (!p->hw.supply_set(p->hw.ctx, true, p->want_mv, p->want_ma)) {
            back_out(p, (uint8_t)LINK_KST_RESULT_SUPPLY);
            return;
        }
        p->on_here = 1;
        p->sup = SUP_ON;
    } else if (p->sup == SUP_ON && v.online && v.on) {
        p->sup = SUP_TIMED;
        p->t1_ms = now_ms;
    }
    if (p->sup != SUP_TIMED) {
        if (waited >= KST_PORT_SUPPLY_MS) {
            back_out(p, (uint8_t)LINK_KST_RESULT_SUPPLY);
        }
        return;
    }
    /* A servo switched on here has the time to start that one switched on
     * again after RAIL_OFF has. */
    if (waited >= KST_PORT_LOW_MS
        && (p->on_here == 0u
            || (uint32_t)(now_ms - p->t1_ms) >= KST_PORT_RAIL_WAIT_MS)) {
        start_session(p);
    }
}

static void step_programming(kst_port_t *p, uint32_t now_ms)
{
    supply_port_view_t v;

    /* The supply this session started under is off, or asked off: by the
     * panel, by the heartbeat lost, by the PD mini's own protection.  The
     * servo is without power and in no mode. */
    if (p->watch != 0u && p->leaving != LEAVE_FORCED && supply_view(p, &v)
        && (!v.asked_on || v.off)) {
        cut(p);
        p->leaving = LEAVE_FORCED;
    }
    if (p->running != 0u) {
        const kst_ses_t r = kst_session_step(&p->session);

        if (r == KST_SES_BUSY) {
            p->seen_step = p->session.step_index;
            return;
        }
        if (p->leaving != LEAVE_NONE) {
            p->running = 0;
        } else {
            session_done(p, r);
        }
    }
    if (p->leaving != LEAVE_NONE) {
        const bool forced = p->leaving == LEAVE_FORCED;
        const bool have = supply_view(p, &v);

        p->leaving = LEAVE_NONE;
        if (forced) {
            rail_off(p, &v, false, now_ms);
        } else if (have && v.asked_on) {
            rail_off(p, &v, true, now_ms);
        } else if (p->op == (uint8_t)LINK_KST_OP_REBOOT) {
            /* The PD mini went between the command and here. */
            p->must_read = p->framed;
            p->result = (uint8_t)LINK_KST_RESULT_SUPPLY;
            p->busy = 0;
        } else {
            /* The user's word that the servo was without power. */
            go_pwm(p);
            op_end(p, KST_SES_OK);
        }
    } else if (p->framed == 0u) {
        /* Nothing was said to the servo and nothing runs. */
        if (p->on_here != 0u) {
            supply_off(p);
        }
        go_pwm(p);
    }
}

static void step_rail_off(kst_port_t *p, uint32_t now_ms)
{
    supply_port_view_t v;

    (void)supply_view(p, &v);
    if (p->sup != SUP_TIMED) {
        if (v.online && v.off) {
            p->sup = SUP_TIMED;
            p->t0_ms = now_ms;
            kst_session_power_cycled(&p->session);
        } else if ((uint32_t)(now_ms - p->t0_ms) >= KST_PORT_SUPPLY_MS) {
            /* Not seen off: the servo may have power still. */
            if (p->from_pwm != 0u) {
                go_pwm(p);
            } else {
                p->state = (uint8_t)LINK_KST_PROGRAMMING;
                p->sup = SUP_NONE;
                p->watch = 0;
                p->must_read = p->framed;
            }
            p->result = (uint8_t)LINK_KST_RESULT_SUPPLY;
            p->busy = 0;
            p->running = 0;
        }
        return;
    }
    if ((uint32_t)(now_ms - p->t0_ms) < KST_PORT_RAIL_OFF_MS) {
        return;
    }
    /* On again only for the command that asked, and only while a panel is
     * there to switch it off. */
    if (p->again == 0u || p->safe == 0u) {
        go_pwm(p);
        op_end(p, KST_SES_ERR_ABORTED);
    } else if (!p->hw.supply_set(p->hw.ctx, true, p->back_mv, p->back_ma)) {
        go_pwm(p);
        p->result = (uint8_t)LINK_KST_RESULT_SUPPLY;
        p->busy = 0;
        p->running = 0;
    } else {
        p->state = (uint8_t)LINK_KST_RAIL_WAIT;
        p->sup = SUP_ON;
        p->t0_ms = now_ms;
    }
}

static void step_rail_wait(kst_port_t *p, uint32_t now_ms)
{
    supply_port_view_t v;

    (void)supply_view(p, &v);
    if (v.online && !v.asked_on) {
        /* Switched off again: by the panel or by the heartbeat lost. */
        go_pwm(p);
        op_end(p, KST_SES_ERR_ABORTED);
        return;
    }
    if (p->sup != SUP_TIMED) {
        if (v.online && v.on) {
            p->sup = SUP_TIMED;
            p->t0_ms = now_ms;
        } else if ((uint32_t)(now_ms - p->t0_ms) >= KST_PORT_SUPPLY_MS) {
            supply_off(p);
            go_pwm(p);
            p->result = (uint8_t)LINK_KST_RESULT_SUPPLY;
            p->busy = 0;
            p->running = 0;
        }
        return;
    }
    if ((uint32_t)(now_ms - p->t0_ms) >= KST_PORT_RAIL_WAIT_MS) {
        go_pwm(p);
        op_end(p, KST_SES_OK);
    }
}

void kst_port_step(kst_port_t *p, bool safe, bool stop, uint32_t now_ms)
{
    if (p == NULL) {
        return;
    }
    p->safe = safe ? 1u : 0u;
    if (stop || !safe) {
        on_stop(p);
    }
    switch ((link_kst_state_t)p->state) {
    case LINK_KST_STOPPING:
        if ((uint32_t)(now_ms - p->t0_ms) >= KST_PORT_STOP_MS) {
            p->state = (uint8_t)LINK_KST_LOW;
            p->t0_ms = now_ms;
            p->has_pin = 1;
            if (!p->hw.take(p->hw.ctx, p->port, p->pin)) {
                back_out(p, (uint8_t)KST_SES_ERR_DRIVER);
            } else {
                low_begin(p);
            }
        }
        break;
    case LINK_KST_LOW:
        step_low(p, now_ms);
        break;
    case LINK_KST_PROGRAMMING:
        step_programming(p, now_ms);
        break;
    case LINK_KST_RAIL_OFF:
        step_rail_off(p, now_ms);
        break;
    case LINK_KST_RAIL_WAIT:
        step_rail_wait(p, now_ms);
        break;
    case LINK_KST_PWM:
    default:
        break;
    }
}

bool kst_port_init(kst_port_t *p, const kst_port_hw_t *hw)
{
    if (p == NULL) {
        return false;
    }
    memset(p, 0, sizeof(*p));
    p->fail_reg = NO_REG;
    if (hw == NULL || hw->ports == 0u || hw->ports > LINK_PG_PORTS_MAX
        || hw->pin == NULL || hw->take == NULL || hw->give == NULL
        || (hw->supply == NULL) != (hw->supply_set == NULL)
        || hw->driver.start == NULL || hw->driver.poll == NULL
        || hw->driver.now_us == NULL) {
        return false;
    }
    p->hw = *hw;
    session_fresh(p);
    return true;
}

link_kst_state_t kst_port_state(const kst_port_t *p)
{
    return p != NULL ? (link_kst_state_t)p->state : LINK_KST_PWM;
}

uint8_t kst_port_quiet_mask(const kst_port_t *p)
{
    return (uint8_t)(p != NULL && p->state != (uint8_t)LINK_KST_PWM
                         ? 1u << p->port : 0u);
}

uint8_t kst_port_pin_mask(const kst_port_t *p)
{
    return (uint8_t)(p != NULL && p->has_pin != 0u ? 1u << p->port : 0u);
}
