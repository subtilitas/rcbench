/*
 * The rig the Hitec session suites run on (test_hitec_session.c and
 * test_hitec_session_write.c): a servo model that answers each step the
 * session hands out with a capture, decoded by the decoder the step names
 * as the HITEC link page decodes it, scripts of operations, forced
 * results and the helpers that step a session by hand.  Only static
 * inline helpers; every suite includes the header whole.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "hitec_reg_fix.h"

#include "hitec_session.h"

static const uint32_t C_ARM = 468750u;  /* 9 classic bit times, ns */
static const uint32_t D_BIT = 8681u;    /* ns */

/* ------------------------------------------------------------- the rig */

/* Ops of a script. */
enum { SC_END = 0, SC_ENTER, SC_ENTER_ON, SC_READ_ALL, SC_READ_BLOCK,
       SC_WRITE, SC_LEAVE };

typedef struct {
    hitec_ses_step_t st;
    uint32_t at;          /* the clock when it was handed out */
} rec_t;

typedef struct {
    hitec_session_t s;
    uint8_t mem[HITEC_MEM_SIZE];  /* the servo: cells or registers */
    uint8_t version;
    uint8_t ignore;       /* 1: the servo does not take a write of ign */
    uint8_t ign;
    uint32_t plus_ns;     /* the servo's `+` from the start of a listen */
    uint16_t c_pos;       /* classic: the potentiometer 0x65 answers */
    uint16_t c_moved;     /* classic: the last target 0x66 took */
    uint32_t now;
    uint32_t cost;        /* ticks a step takes */
    unsigned n;           /* steps run */
    rec_t log[400];
    unsigned force_at[8]; /* step numbers whose result is forced */
    hitec_ses_result_t force[8];
    unsigned n_force;
    const uint8_t *script;
    unsigned sp;
    hitec_image_t target;
} rig_t;


/* Ticks of one ms. */
static inline uint32_t tick_ms(const rig_t *r)
{
    return r->s.clock == HITEC_SES_CLOCK_US ? 1000u : 1u;
}

static inline void rig_init(rig_t *r, uint8_t series, uint8_t clock, uint32_t t0)
{
    hitec_image_t img;

    memset(r, 0, sizeof(*r));
    CHECK(hitec_session_init(&r->s, series, clock));
    r->now = t0;
    r->cost = clock == HITEC_SES_CLOCK_US ? 3000u : 3u;
    r->plus_ns = 160000000u;
    if (series == HITEC_CLASSIC) {
        hs5645(&img);
        r->version = img.version;
    } else {
        d485(&img);
    }
    memcpy(r->mem, img.mem, sizeof(r->mem));
}

static inline void cap_byte(hitec_capture_t *c, uint32_t at_ns, uint8_t v)
{
    c->byte[c->n].at_ns = at_ns;
    c->byte[c->n].value = v;
    c->n++;
}

/* A classic reply 513us after the start of the master's last byte. */
static inline void c_reply(hitec_capture_t *c, uint8_t a, uint8_t b)
{
    cap_byte(c, 513000u - C_ARM, a);
    cap_byte(c, 513000u - C_ARM + 520000u, b);
}

/* What the servo puts on the wire for @p st. */
static inline void servo(rig_t *r, const hitec_ses_step_t *st, hitec_capture_t *c)
{
    const hitec_xfer_t *x = &st->xfer;

    memset(c, 0, sizeof(*c));
    c->family = x->family;
    c->window_ns = x->window_us * 1000u;
    if (x->kind == HITEC_XFER_LISTEN) {
        cap_byte(c, r->plus_ns, HITEC_C_ACK);
        c->answered = x->answer;
    } else if (x->family == HITEC_CLASSIC) {
        const uint8_t a = x->tx[1];

        if (x->tx[0] == HITEC_C_FIRST) {
            c_reply(c, r->version, HITEC_C_ACK);
        } else if (x->tx[0] == HITEC_C_READ_EEPROM) {
            c_reply(c, r->mem[a], HITEC_C_ACK);
        } else if (x->tx[0] == HITEC_C_READ_POSITION) {
            c_reply(c, (uint8_t)(r->c_pos >> 8), (uint8_t)r->c_pos);
        } else if (x->tx[0] == HITEC_C_MOVE) {
            r->c_moved = (uint16_t)(a << 8 | x->tx[2]);
            c_reply(c, HITEC_C_ACK, HITEC_C_ACK);
        } else {
            if (!(r->ignore != 0u && a == r->ign)) {
                r->mem[a] = x->tx[2];
            }
            c_reply(c, HITEC_C_ACK, HITEC_C_ACK);
        }
    } else if (x->n_tx == HITEC_D_READ_LEN) {
        const uint8_t a = x->tx[2];
        uint8_t f[HITEC_D_WRITE_LEN];

        hitec_d_write_frame(0, a,
                            (uint16_t)(r->mem[a] | (r->mem[a + 1u] << 8)), f);
        f[0] = HITEC_D_HEADER_ANSWER;
        for (unsigned i = 0; i < HITEC_D_ANSWER_LEN; ++i) {
            cap_byte(c, 6000000u - D_BIT + i * 86806u, f[i]);
        }
        cap_byte(c, 6000000u - D_BIT + 7u * 86806u, 0x00);
    } else if (!(r->ignore != 0u && x->tx[2] == r->ign)) {
        r->mem[x->tx[2]] = x->tx[4];
        r->mem[x->tx[2] + 1u] = x->tx[5];
    }
}

/* The page's decode of @p c by the step's decoder. */
static inline hitec_ses_result_t decode(const hitec_ses_step_t *st,
                                 const hitec_capture_t *c)
{
    hitec_ses_result_t res = { HITEC_SES_RAN, HITEC_RX_OK, 0 };
    hitec_reply_t rep;
    hitec_c_expect_t e = HITEC_C_EXPECT_VALUE;

    memset(&rep, 0, sizeof(rep));
    if (st->decoder == HITEC_DEC_C) {
        CHECK(hitec_c_expect_of(st->dec_arg, &e));
        res.rx = (uint8_t)hitec_c_decode(c, e, &rep);
    } else if (st->decoder == HITEC_DEC_D) {
        res.rx = (uint8_t)hitec_d_decode(c, st->dec_arg, &rep);
    } else if (st->decoder == HITEC_DEC_LISTEN) {
        res.rx = (uint8_t)hitec_listen_decode(c, st->dec_arg, &rep);
    }
    res.value = rep.value;
    return res;
}

/* Start the script's next op; false at its end. */
static inline bool rig_start(rig_t *r)
{
    static const uint8_t block[HITEC_C_BLOCK_LEN] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
        18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
    };
    hitec_session_t *s = &r->s;
    hitec_ses_t res = HITEC_SES_ERR_ARG;

    switch (r->script[r->sp++]) {
    case SC_ENTER:
        res = hitec_session_enter(s, true, s->series == HITEC_D ? 0 : 500);
        break;
    case SC_ENTER_ON:
        res = hitec_session_enter(s, false, 0);
        break;
    case SC_READ_ALL:
        res = hitec_session_read_all(s);
        break;
    case SC_READ_BLOCK:
        res = hitec_session_read_set(s, block, HITEC_C_BLOCK_LEN);
        break;
    case SC_WRITE:
        r->target = s->image;
        if (s->series == HITEC_CLASSIC) {
            CHECK_EQ(hitec_param_set(&r->target, HITEC_LAYOUT_HS5,
                                     HITEC_P_C_SPEED, 50), HITEC_SET_OK);
        } else {
            CHECK_EQ(hitec_param_set(&r->target, HITEC_LAYOUT_D,
                                     HITEC_P_D_SPEED, 30), HITEC_SET_OK);
        }
        res = hitec_session_write(s, &r->target);
        break;
    case SC_LEAVE:
        res = hitec_session_leave(s);
        break;
    default:
        return false;
    }
    CHECK_EQ(res, HITEC_SES_BUSY);
    return res == HITEC_SES_BUSY;
}

/* Run one step: wait, or the servo's answer and its result. */
static inline hitec_ses_kind_t rig_step(rig_t *r)
{
    hitec_ses_step_t st;
    hitec_capture_t c;
    hitec_ses_result_t res;
    const hitec_ses_kind_t k = hitec_session_next(&r->s, r->now, &st);

    if (k == HITEC_SES_STEP_WAIT) {
        r->now += st.wait_ms * tick_ms(r);
        return k;
    }
    if (k == HITEC_SES_STEP_DONE) {
        return k;
    }
    if (r->n < sizeof(r->log) / sizeof(r->log[0])) {
        r->log[r->n].st = st;
        r->log[r->n].at = r->now;
    }
    servo(r, &st, &c);
    res = decode(&st, &c);
    for (unsigned i = 0; i < r->n_force; ++i) {
        if (r->force_at[i] == r->n) {
            res = r->force[i];
        }
    }
    r->n++;
    r->now += r->cost;
    (void)hitec_session_feed(&r->s, r->now, &res);
    return k;
}

/* Run the script to its end, or to the first op that ends with an error. */
static inline hitec_ses_t rig_run(rig_t *r, const uint8_t *script)
{
    r->script = script;
    r->sp = 0;
    if (!rig_start(r)) {
        return HITEC_SES_ERR_ARG;
    }
    for (unsigned i = 0; i < 20000u; ++i) {
        if (rig_step(r) == HITEC_SES_STEP_DONE) {
            const hitec_ses_t res = hitec_session_result(&r->s);

            if (res != HITEC_SES_OK || r->script[r->sp] == SC_END
                || !rig_start(r)) {
                return res;
            }
        }
    }
    T_FAIL("the script did not end");
    return HITEC_SES_BUSY;
}

/* Force the result of step @p at. */
static inline void rig_force(rig_t *r, unsigned at, uint8_t run, uint8_t rx)
{
    r->force_at[r->n_force] = at;
    r->force[r->n_force].run = run;
    r->force[r->n_force].rx = rx;
    r->force[r->n_force].value = 0;
    r->n_force++;
}

/* A step that matches. */
typedef bool (*pick_t)(const hitec_session_t *s, const hitec_ses_step_t *st);

/* Run the script until the @p nth step @p pick takes is out. */
static inline bool rig_until(rig_t *r, const uint8_t *script, pick_t pick,
                      unsigned nth)
{
    unsigned seen = 0;

    r->script = script;
    r->sp = 0;
    if (!rig_start(r)) {
        return false;
    }
    for (unsigned i = 0; i < 20000u; ++i) {
        hitec_ses_step_t st;
        const hitec_ses_kind_t k = hitec_session_next(&r->s, r->now, &st);

        if (k == HITEC_SES_STEP_DONE) {
            if (hitec_session_result(&r->s) != HITEC_SES_OK
                || !rig_start(r)) {
                return false;
            }
        } else if (k != HITEC_SES_STEP_WAIT && pick(&r->s, &st)
                   && ++seen == nth) {
            return true;
        } else {
            (void)rig_step(r);
        }
    }
    return false;
}

/* Manual stepping. */
static inline hitec_ses_t give(hitec_session_t *s, uint32_t now, uint8_t run,
                        uint8_t rx, uint16_t value)
{
    const hitec_ses_result_t res = { run, rx, value };
    return hitec_session_feed(s, now, &res);
}

static inline hitec_ses_t give_ok(hitec_session_t *s, uint32_t now, uint16_t v)
{
    return give(s, now, HITEC_SES_RAN, HITEC_RX_OK, v);
}

/* Feed the capture @p c of the step that is out, decoded by its decoder. */
static inline hitec_ses_t give_cap(hitec_session_t *s, uint32_t now,
                            const hitec_capture_t *c)
{
    hitec_ses_step_t st;
    hitec_ses_result_t res;

    (void)hitec_session_next(s, now, &st);
    res = decode(&st, c);
    return hitec_session_feed(s, now, &res);
}

/* The gap from a result at @p t: WAIT at t + gap ticks, due one later. */
static inline void check_gap(hitec_session_t *s, uint32_t t, uint32_t gap)
{
    hitec_ses_step_t st;

    CHECK_EQ(hitec_session_next(s, t, &st), HITEC_SES_STEP_WAIT);
    CHECK_EQ(hitec_session_next(s, t + gap, &st), HITEC_SES_STEP_WAIT);
    CHECK_EQ(st.wait_ms, 1);
    CHECK(hitec_session_next(s, t + gap + 1u, &st) != HITEC_SES_STEP_WAIT);
}

/* A classic session in mode, by hand, its clock at @p *t. */
static inline void enter_classic(hitec_session_t *s, uint8_t clock, uint32_t *t,
                          uint16_t off_ms)
{
    hitec_ses_step_t st;
    const uint32_t ms = clock == HITEC_SES_CLOCK_US ? 1000u : 1u;

    CHECK(hitec_session_init(s, HITEC_CLASSIC, clock));
    CHECK_EQ(hitec_session_enter(s, false, off_ms), HITEC_SES_BUSY);
    CHECK_EQ(hitec_session_next(s, *t, &st), HITEC_SES_STEP_POWER);
    CHECK_EQ(give_ok(s, *t, HITEC_C_ACK), HITEC_SES_BUSY);
    CHECK_EQ(hitec_session_next(s, *t, &st), HITEC_SES_STEP_XFER);
    CHECK_EQ(give_ok(s, *t, HITEC_C_ACK), HITEC_SES_BUSY);
    *t += 3u * ms;
    CHECK_EQ(hitec_session_next(s, *t, &st), HITEC_SES_STEP_XFER);
    CHECK_EQ(give_ok(s, *t, 0x16), HITEC_SES_OK);
    *t += 3u * ms;
}

static const uint8_t k_c_script[] = { SC_ENTER, SC_READ_BLOCK, SC_WRITE,
                                      SC_LEAVE, SC_END };
static const uint8_t k_d_script[] = { SC_ENTER, SC_READ_ALL, SC_WRITE,
                                      SC_LEAVE, SC_END };

/* Every kind of step, and how a failed try of it ends. */
