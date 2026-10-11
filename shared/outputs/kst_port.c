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

#define NO_REG 0xFFu
#define FRAMES_MAX 0x0FFFu

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

/* Back to PWM: the driver closed, the slot bound. */
static void go_pwm(kst_port_t *p)
{
    const bool had_pin = p->state != (uint8_t)LINK_KST_STOPPING;

    p->state = (uint8_t)LINK_KST_PWM;
    p->framed = 0;
    p->must_read = 0;
    p->leaving = 0;
    if (had_pin) {
        p->hw.give(p->hw.ctx);
    }
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
        go_pwm(p);
        op_end(p, KST_SES_ERR_ABORTED);
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

/* The slot that renders @p ch as PWM, or -1. */
static int pwm_slot(const outputs_t *o, uint8_t ch)
{
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        const out_slot_t *s = &o->slot[i];

        if (s->driver == OUT_DRIVER_PWM && ch >= s->first_channel
            && ch < (unsigned)s->first_channel + s->channels) {
            return (int)i;
        }
    }
    return -1;
}

static link_kst_refusal_t enter_from_pwm(kst_port_t *p, uint8_t ch,
                                         const outputs_t *o, uint32_t now_ms)
{
    const int slot = o != NULL ? pwm_slot(o, ch) : -1;

    if (slot < 0 || !p->hw.bound(p->hw.ctx, (uint8_t)slot)) {
        return LINK_KST_REF_CHANNEL;
    }
    if (o->channel[ch].role == OUT_ROLE_THROTTLE) {
        return LINK_KST_REF_THROTTLE;
    }
    if (!p->hw.reply_path(p->hw.ctx, o->slot[slot].pin)) {
        return LINK_KST_REF_NO_REPLY_PATH;
    }
    p->slot = (uint8_t)slot;
    p->channel = ch;
    p->pin = o->slot[slot].pin;
    p->state = (uint8_t)LINK_KST_STOPPING;
    p->t0_ms = now_ms;
    op_begin(p, LINK_KST_OP_ENTER);
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
                                   uint8_t ch, const uint16_t *in,
                                   const outputs_t *o, uint32_t now_ms)
{
    const link_kst_state_t state = (link_kst_state_t)p->state;

    if (op == LINK_KST_OP_ABORT) {
        if (state == LINK_KST_STOPPING || state == LINK_KST_LOW) {
            go_pwm(p);
            op_end(p, KST_SES_ERR_ABORTED);
        } else if (state == LINK_KST_PROGRAMMING && p->running != 0u) {
            cut(p);
            if (p->framed != 0u) {
                p->must_read = 1;
            }
        }
        return LINK_KST_REF_NONE;
    }
    /* Before every other answer: a write without the enable is refused as
     * that, whatever else is wrong with it. */
    if (writes_servo(op) && in[LINK_KS_W_KEY] != LINK_KST_WRITE_KEY) {
        return LINK_KST_REF_NO_ENABLE;
    }
    if (op == LINK_KST_OP_POWER_CYCLED) {
        if (state == LINK_KST_PWM) {
            return LINK_KST_REF_NOT_PROGRAMMING;
        }
        if (state != LINK_KST_PROGRAMMING || p->leaving != 0u) {
            return LINK_KST_REF_BUSY;
        }
        if (ch != p->channel) {
            return LINK_KST_REF_CHANNEL;
        }
        kst_session_power_cycled(&p->session);
        {
            const uint8_t was_running = p->running;

            op_begin(p, LINK_KST_OP_POWER_CYCLED);
            p->running = was_running;
        }
        p->leaving = 1;
        return LINK_KST_REF_NONE;
    }
    if (p->safe == 0u) {
        return LINK_KST_REF_UNSAFE;
    }
    if (state == LINK_KST_PWM) {
        return op == LINK_KST_OP_ENTER ? enter_from_pwm(p, ch, o, now_ms)
                                       : LINK_KST_REF_NOT_PROGRAMMING;
    }
    if (state != LINK_KST_PROGRAMMING || p->busy != 0u) {
        return LINK_KST_REF_BUSY;
    }
    if (ch != p->channel) {
        return LINK_KST_REF_CHANNEL;
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

static uint8_t command(kst_port_t *p, const uint16_t *in, const outputs_t *o,
                       uint32_t now_ms)
{
    const uint16_t cmd = in[LINK_KS_W_CMD];
    const uint8_t op = (uint8_t)(cmd & 0xFu);
    const uint8_t ch = (uint8_t)((cmd >> 8) & 0xFu);

    if ((cmd & 0xF070u) != 0u || op == (uint8_t)LINK_KST_OP_NONE
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
    p->refusal = (uint8_t)dispatch(p, (link_kst_op_t)op, ch, in, o, now_ms);
    return 0u;
}

uint8_t kst_port_write(kst_port_t *p, uint8_t off, uint8_t n,
                       const uint16_t *in, const outputs_t *o,
                       uint32_t now_ms)
{
    if (p == NULL || in == NULL || n == 0u
        || (unsigned)off + n > LINK_KS_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (off < LINK_KS_W_CMD_FRAME) {
        if (off != 0u || n != LINK_KS_W_CMD_FRAME) {
            return LINK_NACK_BAD_RANGE;
        }
        return command(p, in, o, now_ms);
    }
    if ((unsigned)off + n > LINK_KS_W_COUNT) {
        return LINK_NACK_READ_ONLY;
    }
    if ((unsigned)off + n > LINK_KS_W_VIEW && in[LINK_KS_W_VIEW - off] > 1u) {
        return LINK_NACK_BAD_VALUE;
    }
    for (unsigned i = 0; i < n; ++i) {
        const unsigned at = off + i;

        if (at == LINK_KS_W_VIEW) {
            p->view = (uint8_t)in[i];
        } else {
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
    uint16_t r = (uint16_t)(p->state | ((uint16_t)p->channel << 12));

    r |= p->busy != 0u ? LINK_KS_F_BUSY : 0u;
    r |= p->framed != 0u ? LINK_KS_F_IN_MODE : 0u;
    r |= p->must_read != 0u ? LINK_KS_F_MUST_READ : 0u;
    r |= s->has_image != 0u ? LINK_KS_F_IMAGE : 0u;
    r |= s->has_backup != 0u ? LINK_KS_F_BACKUP : 0u;
    r |= s->fingerprint_ok != 0u ? LINK_KS_F_FP_OK : 0u;
    r |= s->locked != 0u ? LINK_KS_F_LOCKED : 0u;
    r |= p->hw.rail != NULL ? LINK_KS_F_RAIL : 0u;
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

static void step_programming(kst_port_t *p, uint32_t now_ms)
{
    if (p->running != 0u) {
        const kst_ses_t r = kst_session_step(&p->session);

        if (r == KST_SES_BUSY) {
            p->seen_step = p->session.step_index;
            return;
        }
        if (p->leaving != 0u) {
            p->running = 0;
        } else {
            session_done(p, r);
        }
    }
    if (p->leaving != 0u) {
        /* The servo was without power, or is about to be. */
        p->leaving = 0;
        if (p->hw.rail != NULL) {
            p->hw.rail(p->hw.ctx, false);
            p->state = (uint8_t)LINK_KST_RAIL_OFF;
            p->t0_ms = now_ms;
        } else {
            go_pwm(p);
            op_end(p, KST_SES_OK);
        }
    } else if (p->framed == 0u) {
        /* Nothing was said to the servo and nothing runs. */
        go_pwm(p);
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
            if (!p->hw.take(p->hw.ctx, p->slot, p->pin)) {
                go_pwm(p);
                op_end(p, KST_SES_ERR_DRIVER);
            }
        }
        break;
    case LINK_KST_LOW:
        if ((uint32_t)(now_ms - p->t0_ms) >= KST_PORT_LOW_MS) {
            p->state = (uint8_t)LINK_KST_PROGRAMMING;
            session_fresh(p);
            session_started(p, kst_session_enter(&p->session));
        }
        break;
    case LINK_KST_PROGRAMMING:
        step_programming(p, now_ms);
        break;
    case LINK_KST_RAIL_OFF:
        if ((uint32_t)(now_ms - p->t0_ms) >= KST_PORT_RAIL_OFF_MS) {
            p->hw.rail(p->hw.ctx, true);
            p->state = (uint8_t)LINK_KST_RAIL_WAIT;
            p->t0_ms = now_ms;
        }
        break;
    case LINK_KST_RAIL_WAIT:
        if ((uint32_t)(now_ms - p->t0_ms) >= KST_PORT_RAIL_WAIT_MS) {
            go_pwm(p);
            op_end(p, KST_SES_OK);
        }
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
    if (hw == NULL || hw->bound == NULL || hw->reply_path == NULL
        || hw->take == NULL || hw->give == NULL || hw->driver.start == NULL
        || hw->driver.poll == NULL || hw->driver.now_us == NULL) {
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

uint8_t kst_port_hold_mask(const kst_port_t *p)
{
    return (uint8_t)(p != NULL && p->state != (uint8_t)LINK_KST_PWM
                         ? 1u << p->slot : 0u);
}

uint8_t kst_port_pin_mask(const kst_port_t *p)
{
    return (uint8_t)(p != NULL && p->state > (uint8_t)LINK_KST_STOPPING
                         ? 1u << p->slot : 0u);
}
