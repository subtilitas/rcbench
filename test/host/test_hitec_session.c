/*
 * The Hitec session (protocols/hitec/hitec_session.c): the steps it hands
 * out and what it does with their results, on the rig of
 * hitec_session_rig.h.  The write plans, their read-back and leave are in
 * test_hitec_session_write.c.
 *
 * Under test:
 *  - the classic entry: the servo's `+` at 0.23ms and at 1999ms, none in
 *    2s, a byte after the `+`, `-` instead of `+`, `-` to the first command
 *    (the reply to a bad sum) and the first command's own sum;
 *  - the D series entry: the 1000ms wait after the supply comes on, across
 *    the wrap of both clocks, and the entry on a supply left as it is;
 *  - read tries: an answer on the first, on the third, none on 3;
 *  - every hitec_rx_t code 1 to 14 on each of 16 kinds of step;
 *  - an abort at every kind of step and in a gap, a cut and a step that
 *    did not run;
 *  - every refusal of an operation; a classic full read and a failed one.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_session_rig.h"

#include "hitec_pre.h"
#include "link_pages.h"

/* The step's decoder is the HITEC page's; the POWER LISTEN of a D series
 * entry fits a PRECHARGE line; the off times fit the page's. */
_Static_assert((int)HITEC_DEC_NONE == (int)LINK_HITEC_DEC_NONE, "decoder none");
_Static_assert((int)HITEC_DEC_C == (int)LINK_HITEC_DEC_C, "decoder classic");
_Static_assert((int)HITEC_DEC_D == (int)LINK_HITEC_DEC_D, "decoder D series");
_Static_assert((int)HITEC_DEC_LISTEN == (int)LINK_HITEC_DEC_LISTEN, "decoder listen");
_Static_assert(HITEC_SES_POWER_WINDOW_US <= HITEC_PRE_WINDOW_MAX_US,
               "a D series POWER runs on a PRECHARGE line");
_Static_assert(HITEC_SES_OFF_MS_MAX == LINK_HITEC_OFF_MS_MAX, "off time");
_Static_assert(HITEC_RX_ERR_SUM == 14, "decoder codes 1 to 14");

static rig_t g;

/* ---------------------------------------------------------------- picks */

static bool p_c_power(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    return s->in_mode == 0u && st->kind == HITEC_SES_STEP_POWER;
}
static bool p_listen(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    (void)s;
    return st->kind == HITEC_SES_STEP_XFER
           && st->xfer.kind == HITEC_XFER_LISTEN;
}
static bool p_first(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    (void)s;
    return st->xfer.kind == HITEC_XFER_SEND && st->xfer.tx[0] == 0x67;
}
static bool p_c_read(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    return st->xfer.tx[0] == 0x61 && s->plan.n == 0u;
}
static bool p_c_cell(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    (void)s;
    return st->xfer.tx[0] == 0x62 && st->xfer.tx[1] != 0x1C;
}
static bool p_c_sum(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    (void)s;
    return st->xfer.tx[0] == 0x62 && st->xfer.tx[1] == 0x1C;
}
static bool p_back(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    return st->kind == HITEC_SES_STEP_XFER && st->decoder != HITEC_DEC_NONE
           && s->plan.n != 0u && s->step_index == s->plan.n;
}
static bool p_c_leave(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    return s->in_mode == 0u && st->kind == HITEC_SES_STEP_POWER
           && st->decoder == HITEC_DEC_NONE;
}
static bool p_d_power(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    (void)s;
    return st->kind == HITEC_SES_STEP_POWER;
}
static bool p_d_model(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    return s->in_mode == 0u && st->decoder == HITEC_DEC_D;
}
static bool p_d_read(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    return s->in_mode != 0u && s->plan.n == 0u
           && st->decoder == HITEC_DEC_D;
}
static bool p_d_write(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    (void)s;
    return st->xfer.n_tx == HITEC_D_WRITE_LEN && st->xfer.tx[2] == 0x54;
}
static bool p_d_save(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    (void)s;
    return st->xfer.n_tx == HITEC_D_WRITE_LEN && st->xfer.tx[2] == 0x70;
}
static bool p_d_reboot(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    return s->in_mode != 0u && st->xfer.n_tx == HITEC_D_WRITE_LEN
           && st->xfer.tx[2] == 0x46;
}
static bool p_d_leave(const hitec_session_t *s, const hitec_ses_step_t *st)
{
    return s->in_mode == 0u && st->xfer.n_tx == HITEC_D_WRITE_LEN
           && st->xfer.tx[2] == 0x46;
}

enum { DEC_ENTRY, DEC_TRIES, NO_DEC };
static const struct {
    const char *name;
    uint8_t series;
    pick_t pick;
    unsigned nth;
    uint8_t how;
    uint8_t err;      /* the error after the tries */
} k_steps[] = {
    { "c power", HITEC_CLASSIC, p_c_power, 1, DEC_ENTRY, HITEC_SES_ERR_ENTRY },
    { "c next", HITEC_CLASSIC, p_listen, 1, DEC_ENTRY, HITEC_SES_ERR_ENTRY },
    { "c first", HITEC_CLASSIC, p_first, 1, DEC_TRIES, HITEC_SES_ERR_ENTRY },
    { "c read", HITEC_CLASSIC, p_c_read, 7, DEC_TRIES, HITEC_SES_ERR_READ },
    { "c cell", HITEC_CLASSIC, p_c_cell, 1, DEC_TRIES, HITEC_SES_ERR_WRITE },
    { "c sum", HITEC_CLASSIC, p_c_sum, 1, DEC_TRIES, HITEC_SES_ERR_WRITE },
    { "c back", HITEC_CLASSIC, p_back, 36, DEC_TRIES, HITEC_SES_ERR_READ },
    { "c leave", HITEC_CLASSIC, p_c_leave, 1, NO_DEC, HITEC_SES_OK },
    { "d power", HITEC_D, p_d_power, 1, NO_DEC, HITEC_SES_OK },
    { "d model", HITEC_D, p_d_model, 1, DEC_TRIES, HITEC_SES_ERR_ENTRY },
    { "d read", HITEC_D, p_d_read, 21, DEC_TRIES, HITEC_SES_ERR_READ },
    { "d write", HITEC_D, p_d_write, 1, NO_DEC, HITEC_SES_OK },
    { "d save", HITEC_D, p_d_save, 1, NO_DEC, HITEC_SES_OK },
    { "d reboot", HITEC_D, p_d_reboot, 1, NO_DEC, HITEC_SES_OK },
    { "d back", HITEC_D, p_back, 1, DEC_TRIES, HITEC_SES_ERR_READ },
    { "d leave", HITEC_D, p_d_leave, 1, NO_DEC, HITEC_SES_OK },
};
#define N_STEPS (sizeof(k_steps) / sizeof(k_steps[0]))

/* Bring a rig to step @p i of k_steps, out. */
static bool reach(rig_t *r, unsigned i, uint8_t clock, uint32_t t0)
{
    rig_init(r, k_steps[i].series, clock, t0);
    if (!rig_until(r, k_steps[i].series == HITEC_CLASSIC ? k_c_script
                                                         : k_d_script,
                   k_steps[i].pick, k_steps[i].nth)) {
        T_FAIL("%s not reached", k_steps[i].name);
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------- cases */
TEST_CASE(a_classic_entry_cycles_the_supply_answers_and_sends_0x67)
{
    rig_t *r = &g;
    static const uint8_t script[] = { SC_ENTER, SC_END };
    const hitec_ses_step_t *st;

    rig_init(r, HITEC_CLASSIC, HITEC_SES_CLOCK_US, 1000u);
    CHECK_EQ(rig_run(r, script), HITEC_SES_OK);
    CHECK_EQ(r->n, 3);
    st = &r->log[0].st;
    CHECK_EQ(st->kind, HITEC_SES_STEP_POWER);
    CHECK_EQ(st->off_ms, 500);
    CHECK_EQ(st->xfer.kind, HITEC_XFER_LISTEN);
    CHECK_EQ(st->xfer.family, HITEC_CLASSIC);
    CHECK_EQ(st->xfer.window_us, 2000000u);
    CHECK_EQ(st->xfer.max_rx, 1);
    CHECK_EQ(st->xfer.answer, 1);
    CHECK_EQ(st->xfer.answer_byte, 0x2B);
    CHECK_EQ(st->decoder, HITEC_DEC_LISTEN);
    CHECK_EQ(st->dec_arg, 0x2B);
    /* The servo's next `+`: at once after the POWER, no answer. */
    st = &r->log[1].st;
    CHECK_EQ(st->kind, HITEC_SES_STEP_XFER);
    CHECK_EQ(st->xfer.kind, HITEC_XFER_LISTEN);
    CHECK_EQ(st->xfer.window_us, 1000000u);
    CHECK_EQ(st->xfer.answer, 0);
    CHECK_EQ(st->decoder, HITEC_DEC_LISTEN);
    CHECK_EQ(r->log[1].at, r->log[0].at + r->cost);
    /* 67 00 00 99, which sums to 0, 2ms after the `+`. */
    st = &r->log[2].st;
    CHECK_EQ(st->xfer.n_tx, 4);
    CHECK_EQ(st->xfer.tx[0], 0x67);
    CHECK_EQ(st->xfer.tx[1], 0x00);
    CHECK_EQ(st->xfer.tx[2], 0x00);
    CHECK_EQ(st->xfer.tx[3], 0x99);
    CHECK(hitec_c_frame_ok(st->xfer.tx));
    CHECK_EQ(st->decoder, HITEC_DEC_C);
    CHECK_EQ(st->dec_arg, 0x67);
    CHECK(r->log[2].at - (r->log[1].at + r->cost) > 2000u);
    CHECK_EQ(r->s.in_mode, 1);
    CHECK_EQ(r->s.image.has_version, 1);
    CHECK_EQ(r->s.image.version, 0x16);
    CHECK_EQ(r->s.off_ms, 500);
}

TEST_CASE(the_classic_entry_takes_a_plus_from_0_23ms_to_1999ms_only)
{
    hitec_session_t s;
    hitec_ses_step_t st;
    hitec_capture_t c;
    static const uint32_t ok_ns[] = { 230000u, 1999000000u };

    for (unsigned i = 0; i < 2u; ++i) {
        CHECK(hitec_session_init(&s, HITEC_CLASSIC, HITEC_SES_CLOCK_MS));
        CHECK_EQ(hitec_session_enter(&s, true, 500), HITEC_SES_BUSY);
        CHECK_EQ(hitec_session_next(&s, 0, &st), HITEC_SES_STEP_POWER);
        CHECK(ok_ns[i] < st.xfer.window_us * 1000u);
        memset(&c, 0, sizeof(c));
        c.family = HITEC_CLASSIC;
        cap_byte(&c, ok_ns[i], HITEC_C_ACK);
        c.answered = 1;
        CHECK_EQ(give_cap(&s, 2000u, &c), HITEC_SES_BUSY);
        CHECK_EQ(give_cap(&s, 2300u, &c), HITEC_SES_BUSY);
        memset(&c, 0, sizeof(c));
        c.family = HITEC_CLASSIC;
        c_reply(&c, 0x26, HITEC_C_ACK);
        CHECK_EQ(hitec_session_next(&s, 2303u, &st), HITEC_SES_STEP_XFER);
        CHECK_EQ(give_cap(&s, 2310u, &c), HITEC_SES_OK);
        CHECK_EQ(s.image.version, 0x26);
    }
}

TEST_CASE(a_classic_entry_without_two_plus_bytes_fails_and_is_not_tried_again)
{
    hitec_session_t s;
    hitec_ses_step_t st;
    hitec_capture_t c;

    for (unsigned listen = 0; listen < 2u; ++listen) {
        for (unsigned v = 0; v < 3u; ++v) {
            CHECK(hitec_session_init(&s, HITEC_CLASSIC, HITEC_SES_CLOCK_US));
            CHECK_EQ(hitec_session_enter(&s, true, 0), HITEC_SES_BUSY);
            if (listen == 1u) {
                CHECK_EQ(hitec_session_next(&s, 0, &st),
                         HITEC_SES_STEP_POWER);
                CHECK_EQ(give_ok(&s, 0, HITEC_C_ACK), HITEC_SES_BUSY);
            }
            memset(&c, 0, sizeof(c));
            c.family = HITEC_CLASSIC;
            if (v == 0u) {
                /* None in the window. */
                CHECK_EQ(give_cap(&s, 9u, &c), HITEC_SES_ERR_NO_SERVO);
                CHECK_EQ(s.last_rx, HITEC_RX_ERR_SILENT);
            } else if (v == 1u) {
                /* A byte after the `+`. */
                cap_byte(&c, 230000u, HITEC_C_ACK);
                cap_byte(&c, 750000u, HITEC_C_ACK);
                CHECK_EQ(give_cap(&s, 9u, &c), HITEC_SES_ERR_ENTRY);
                CHECK_EQ(s.last_rx, HITEC_RX_ERR_LENGTH);
            } else {
                /* `-` where the `+` belongs. */
                cap_byte(&c, 230000u, HITEC_C_NAK);
                CHECK_EQ(give_cap(&s, 9u, &c), HITEC_SES_ERR_ENTRY);
                CHECK_EQ(s.last_rx, HITEC_RX_ERR_ACK);
            }
            CHECK_EQ(s.in_mode, 0);
            CHECK_EQ(s.tries, 1);
            CHECK_EQ(hitec_session_next(&s, 99999u, &st),
                     HITEC_SES_STEP_DONE);
        }
    }
}

TEST_CASE(a_minus_to_the_first_command_is_a_bad_sum_and_tried_3_times)
{
    hitec_session_t s;
    hitec_ses_step_t st;
    hitec_capture_t c;
    uint32_t t = 0;

    CHECK(hitec_session_init(&s, HITEC_CLASSIC, HITEC_SES_CLOCK_MS));
    CHECK_EQ(hitec_session_enter(&s, true, 500), HITEC_SES_BUSY);
    (void)hitec_session_next(&s, t, &st);
    CHECK_EQ(give_ok(&s, t, HITEC_C_ACK), HITEC_SES_BUSY);
    (void)hitec_session_next(&s, t, &st);
    CHECK_EQ(give_ok(&s, t, HITEC_C_ACK), HITEC_SES_BUSY);
    memset(&c, 0, sizeof(c));
    c.family = HITEC_CLASSIC;
    cap_byte(&c, 513000u - C_ARM, HITEC_C_NAK);
    for (unsigned i = 1; i <= 3u; ++i) {
        t += 3u;
        CHECK_EQ(hitec_session_next(&s, t, &st), HITEC_SES_STEP_XFER);
        CHECK_EQ(st.xfer.tx[0], 0x67);
        CHECK(hitec_c_frame_ok(st.xfer.tx));
        CHECK_EQ(give_cap(&s, t, &c),
                 i < 3u ? HITEC_SES_BUSY : HITEC_SES_ERR_ENTRY);
        CHECK_EQ(s.last_rx, HITEC_RX_ERR_NAK);
        CHECK_EQ(s.tries, i);
    }
    CHECK_EQ(s.in_mode, 0);
    CHECK_EQ(s.image.has_version, 0);
}

TEST_CASE(a_d_entry_waits_1000ms_after_the_supply_comes_on)
{
    static const uint8_t clocks[] = { HITEC_SES_CLOCK_US, HITEC_SES_CLOCK_MS };

    for (unsigned k = 0; k < 2u; ++k) {
        hitec_session_t s;
        hitec_ses_step_t st;
        const uint32_t ms = clocks[k] == HITEC_SES_CLOCK_US ? 1000u : 1u;
        const uint32_t t = 0xFFFFFFFFu - 500u * ms;

        CHECK(hitec_session_init(&s, HITEC_D, clocks[k]));
        CHECK_EQ(hitec_session_enter(&s, true, 0), HITEC_SES_BUSY);
        CHECK_EQ(hitec_session_next(&s, t, &st), HITEC_SES_STEP_POWER);
        CHECK_EQ(st.off_ms, 0);
        CHECK_EQ(st.xfer.kind, HITEC_XFER_LISTEN);
        CHECK_EQ(st.xfer.family, HITEC_D);
        CHECK_EQ(st.xfer.window_us, 1000u);
        CHECK_EQ(st.xfer.answer, 0);
        CHECK_EQ(st.decoder, HITEC_DEC_NONE);
        CHECK_EQ(give(&s, t, HITEC_SES_RAN, HITEC_RX_ERR_SILENT, 0),
                 HITEC_SES_BUSY);
        CHECK_EQ(hitec_session_next(&s, t, &st), HITEC_SES_STEP_WAIT);
        CHECK_EQ(st.wait_ms, 1001);
        check_gap(&s, t, 1000u * ms);
        CHECK_EQ(hitec_session_next(&s, t + 1000u * ms + 1u, &st),
                 HITEC_SES_STEP_XFER);
        CHECK_EQ(st.decoder, HITEC_DEC_D);
        CHECK_EQ(st.dec_arg, 0x00);
        CHECK_EQ(st.xfer.tx[2], 0x00);
        CHECK_EQ(give_ok(&s, t + 1001u * ms, 34485u), HITEC_SES_OK);
        CHECK_EQ(s.in_mode, 1);
        CHECK_EQ(hitec_image_reg(&s.image, 0x00), 34485u);
    }
}

TEST_CASE(a_d_entry_on_the_supply_as_it_is_reads_the_model_at_once)
{
    rig_t *r = &g;
    static const uint8_t script[] = { SC_ENTER_ON, SC_READ_ALL, SC_END };
    hitec_model_t m;

    rig_init(r, HITEC_D, HITEC_SES_CLOCK_MS, 77u);
    CHECK_EQ(rig_run(r, script), HITEC_SES_OK);
    CHECK_EQ(r->log[0].at, 77u);
    CHECK_EQ(r->log[0].st.decoder, HITEC_DEC_D);
    CHECK_EQ(r->n, 22);
    CHECK(hitec_identify(&r->s.image, &m));
    CHECK_EQ(m.model, HITEC_MODEL_D485HW);
    CHECK_EQ(r->s.done, 21);
    CHECK_EQ(r->s.total, 21);
    /* Ascending, 20ms apart. */
    for (unsigned i = 2; i < r->n; ++i) {
        CHECK(r->log[i].st.xfer.tx[2] > r->log[i - 1u].st.xfer.tx[2]);
        CHECK(r->log[i].at - r->log[i - 1u].at - r->cost > 20u);
    }
}

TEST_CASE(a_read_answers_on_try_1_or_3_and_fails_after_3)
{
    static const uint8_t series[] = { HITEC_CLASSIC, HITEC_D };

    for (unsigned k = 0; k < 2u; ++k) {
        for (unsigned fails = 0; fails <= 3u; ++fails) {
            rig_t *r = &g;
            static const uint8_t addr[] = { 0x32 };
            const uint8_t silent = fails == 3u ? 1u : 0u;

            rig_init(r, series[k], HITEC_SES_CLOCK_US, 0);
            if (series[k] == HITEC_CLASSIC) {
                enter_classic(&r->s, HITEC_SES_CLOCK_US, &r->now, 500);
            } else {
                static const uint8_t script[] = { SC_ENTER_ON, SC_END };
                CHECK_EQ(rig_run(r, script), HITEC_SES_OK);
            }
            r->n = 0;
            for (unsigned f = 0; f < fails; ++f) {
                rig_force(r, f, HITEC_SES_RAN,
                          silent != 0u ? HITEC_RX_ERR_SILENT
                                       : HITEC_RX_ERR_FRAMING);
            }
            r->mem[0x32] = 0x07;
            CHECK_EQ(hitec_session_read_set(&r->s, addr, 1), HITEC_SES_BUSY);
            while (rig_step(r) != HITEC_SES_STEP_DONE) {
            }
            if (fails < 3u) {
                CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_OK);
                CHECK_EQ(r->n, fails + 1u);
                CHECK_EQ(hitec_image_cell(&r->s.image, 0x32), 0x07);
            } else {
                CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_ERR_NO_SERVO);
                CHECK_EQ(r->n, 3);
                CHECK_EQ(r->s.tries, 3);
                CHECK_EQ(r->s.fail_addr, 0x32);
                CHECK(!hitec_image_known(&r->s.image, 0x32));
            }
            /* A try again goes after the gap, the same frame. */
            for (unsigned i = 1; i < r->n; ++i) {
                CHECK_EQ(r->log[i].st.xfer.tx[1], r->log[0].st.xfer.tx[1]);
                CHECK_EQ(r->log[i].st.xfer.tx[2], r->log[0].st.xfer.tx[2]);
                CHECK(r->log[i].at - r->log[i - 1u].at - r->cost
                      > (series[k] == HITEC_D ? 20000u : 2000u));
            }
        }
    }
}

TEST_CASE(every_rx_code_on_every_step)
{
    rig_t *r = &g;

    for (unsigned i = 0; i < N_STEPS; ++i) {
        for (uint8_t code = 1; code <= HITEC_RX_ERR_SUM; ++code) {
            hitec_ses_step_t first;
            hitec_ses_t res;

            if (!reach(r, i, HITEC_SES_CLOCK_US, 0)) {
                continue;
            }
            (void)hitec_session_next(&r->s, r->now, &first);
            if (k_steps[i].how == NO_DEC) {
                hitec_capture_t c;

                servo(r, &first, &c);  /* the step ran on the wire */
            }
            res = give(&r->s, r->now, HITEC_SES_RAN, code, 0);
            if (k_steps[i].how == NO_DEC) {
                /* No decoder: the code is not read. */
                CHECK(res == HITEC_SES_BUSY || res == HITEC_SES_OK);
                if (res == HITEC_SES_BUSY) {
                    while (rig_step(r) != HITEC_SES_STEP_DONE) {
                    }
                }
                CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_OK);
                continue;
            }
            CHECK_EQ(r->s.last_rx, code);
            if (code == HITEC_RX_ERR_ARG) {
                CHECK_EQ(res, HITEC_SES_ERR_STEP);
            } else if (code == HITEC_RX_ERR_ABORTED) {
                CHECK_EQ(res, HITEC_SES_ERR_CUT);
            } else if (k_steps[i].how == DEC_ENTRY) {
                CHECK_EQ(res, code == HITEC_RX_ERR_SILENT
                                  ? HITEC_SES_ERR_NO_SERVO
                                  : HITEC_SES_ERR_ENTRY);
            } else {
                hitec_ses_step_t again;

                /* Tried again, the same transaction; two more fail. */
                CHECK_EQ(res, HITEC_SES_BUSY);
                while (hitec_session_next(&r->s, r->now, &again)
                       == HITEC_SES_STEP_WAIT) {
                    r->now += again.wait_ms * 1000u;
                }
                CHECK(memcmp(&again.xfer, &first.xfer, sizeof(first.xfer))
                      == 0);
                CHECK_EQ(give(&r->s, r->now, HITEC_SES_RAN, code, 0),
                         HITEC_SES_BUSY);
                r->now += 30000u;
                (void)hitec_session_next(&r->s, r->now, &again);
                res = give(&r->s, r->now, HITEC_SES_RAN, code, 0);
                CHECK_EQ(res, code == HITEC_RX_ERR_SILENT
                                  ? HITEC_SES_ERR_NO_SERVO
                                  : k_steps[i].err);
                CHECK_EQ(r->s.tries, 3);
            }
            if (hitec_session_result(&r->s) == HITEC_SES_BUSY) {
                T_FAIL("%s code %u: still running", k_steps[i].name, code);
            }
        }
    }
}

TEST_CASE(an_abort_ends_every_step)
{
    rig_t *r = &g;

    for (unsigned i = 0; i < N_STEPS; ++i) {
        hitec_ses_step_t a;
        hitec_ses_step_t b;

        if (!reach(r, i, HITEC_SES_CLOCK_MS, 0xFFFFFFF0u)) {
            continue;
        }
        (void)hitec_session_next(&r->s, r->now, &a);
        CHECK(hitec_session_abort(&r->s));
        CHECK_EQ(hitec_session_next(&r->s, r->now + 5000u, &b), a.kind);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_BUSY);
        CHECK_EQ(give_ok(&r->s, r->now, 0), HITEC_SES_ERR_ABORTED);
        CHECK_EQ(hitec_session_next(&r->s, r->now, NULL),
                 HITEC_SES_STEP_DONE);
        /* Cut, or not run, it ends as aborted too. */
        CHECK(reach(r, i, HITEC_SES_CLOCK_MS, 0));
        CHECK(hitec_session_abort(&r->s));
        CHECK_EQ(give(&r->s, r->now, i % 2u == 0u ? HITEC_SES_CUT
                                                   : HITEC_SES_FAILED,
                      0, 0),
                 HITEC_SES_ERR_ABORTED);
        /* A failed try then the abort: aborted, not tried again. */
        CHECK(reach(r, i, HITEC_SES_CLOCK_MS, 0));
        CHECK(hitec_session_abort(&r->s));
        CHECK_EQ(give(&r->s, r->now, HITEC_SES_RAN, HITEC_RX_ERR_SUM, 0),
                 HITEC_SES_ERR_ABORTED);
    }
}

TEST_CASE(an_abort_in_a_gap_ends_at_once_and_keeps_what_was_read)
{
    rig_t *r = &g;
    hitec_ses_step_t st;
    static const uint8_t script[] = { SC_ENTER, SC_END };

    /* The D series entry's 1000ms wait. */
    rig_init(r, HITEC_D, HITEC_SES_CLOCK_US, 0);
    CHECK(rig_until(r, script, p_d_power, 1));
    (void)rig_step(r);
    CHECK_EQ(hitec_session_next(&r->s, r->now, &st), HITEC_SES_STEP_WAIT);
    CHECK(!hitec_session_abort(&r->s));
    CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_ERR_ABORTED);
    CHECK_EQ(hitec_session_next(&r->s, r->now, &st), HITEC_SES_STEP_DONE);
    CHECK(!hitec_session_abort(&r->s));
    CHECK(!hitec_session_abort(NULL));
    CHECK_EQ(r->s.in_mode, 0);
    /* A read whose answer comes with the abort keeps the value. */
    rig_init(r, HITEC_D, HITEC_SES_CLOCK_US, 0);
    CHECK(rig_until(r, k_d_script, p_d_read, 3));
    (void)hitec_session_next(&r->s, r->now, &st);
    CHECK(hitec_session_abort(&r->s));
    CHECK_EQ(give_ok(&r->s, r->now, 0x1234), HITEC_SES_ERR_ABORTED);
    CHECK_EQ(hitec_image_reg(&r->s.image, st.xfer.tx[2]), 0x1234);
    CHECK_EQ(r->s.done, 3);
    /* A write that took counts. */
    rig_init(r, HITEC_CLASSIC, HITEC_SES_CLOCK_US, 0);
    CHECK(rig_until(r, k_c_script, p_c_cell, 1));
    CHECK(hitec_session_abort(&r->s));
    CHECK_EQ(give_ok(&r->s, r->now, 0), HITEC_SES_ERR_ABORTED);
    CHECK_EQ(r->s.step_index, 1);
    /* The classic first command, answered with the abort: not in mode. */
    rig_init(r, HITEC_CLASSIC, HITEC_SES_CLOCK_US, 0);
    CHECK(rig_until(r, k_c_script, p_first, 1));
    CHECK(hitec_session_abort(&r->s));
    CHECK_EQ(give_ok(&r->s, r->now, 0x16), HITEC_SES_ERR_ABORTED);
    CHECK_EQ(r->s.in_mode, 0);
    CHECK_EQ(r->s.image.version, 0x16);
}

TEST_CASE(a_cut_or_a_step_that_did_not_run_ends_the_operation)
{
    rig_t *r = &g;

    for (unsigned i = 0; i < N_STEPS; ++i) {
        CHECK(reach(r, i, HITEC_SES_CLOCK_US, 0));
        CHECK_EQ(give(&r->s, r->now, HITEC_SES_CUT, 0, 0), HITEC_SES_ERR_CUT);
        CHECK(reach(r, i, HITEC_SES_CLOCK_US, 0));
        CHECK_EQ(give(&r->s, r->now, HITEC_SES_FAILED, 0, 0),
                 HITEC_SES_ERR_LINK);
        CHECK(reach(r, i, HITEC_SES_CLOCK_US, 0));
        CHECK_EQ(give(&r->s, r->now, 7u, 0, 0), HITEC_SES_ERR_LINK);
        CHECK_EQ(hitec_session_next(&r->s, r->now, NULL),
                 HITEC_SES_STEP_DONE);
    }
}

TEST_CASE(every_refusal_of_an_operation)
{
    hitec_session_t s;
    hitec_image_t img;
    hitec_ses_step_t st;
    static const uint8_t odd[] = { 0x00, 0x33 };
    static const uint8_t dup[] = { 0x04, 0x02, 0x04 };
    uint8_t all[256] = { 0 };
    uint32_t t = 0;

    CHECK(!hitec_session_init(NULL, HITEC_D, HITEC_SES_CLOCK_US));
    CHECK(!hitec_session_init(&s, 2, HITEC_SES_CLOCK_US));
    CHECK(!hitec_session_init(&s, HITEC_D, 2));
    CHECK(hitec_session_init(&s, HITEC_D, HITEC_SES_CLOCK_MS));
    CHECK_EQ(s.off_ms, HITEC_SES_OFF_MS);
    CHECK_EQ(hitec_session_result(&s), HITEC_SES_OK);
    CHECK_EQ(hitec_session_result(NULL), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_next(NULL, 0, &st), HITEC_SES_STEP_DONE);
    CHECK_EQ(st.kind, HITEC_SES_STEP_DONE);
    CHECK_EQ(hitec_session_next(&s, 0, &st), HITEC_SES_STEP_DONE);
    CHECK_EQ(give_ok(&s, 0, 0), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_feed(&s, 0, NULL), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_feed(NULL, 0, NULL), HITEC_SES_ERR_ARG);
    /* NULL session. */
    hitec_image_init(&img, HITEC_D);
    CHECK_EQ(hitec_session_enter(NULL, true, 0), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_read_all(NULL), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_read_set(NULL, odd, 1), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_write(NULL, &img), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_run_plan(NULL, NULL, NULL), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_leave(NULL), HITEC_SES_ERR_ARG);
    /* Not in mode. */
    CHECK_EQ(hitec_session_read_all(&s), HITEC_SES_ERR_NOT_IN_MODE);
    CHECK_EQ(hitec_session_read_set(&s, odd, 1), HITEC_SES_ERR_NOT_IN_MODE);
    CHECK_EQ(hitec_session_write(&s, &img), HITEC_SES_ERR_NOT_IN_MODE);
    CHECK_EQ(hitec_session_run_plan(&s, &s.plan, NULL),
             HITEC_SES_ERR_NOT_IN_MODE);
    /* Off time. */
    CHECK_EQ(hitec_session_enter(&s, true, 10001), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_enter(&s, true, 10000), HITEC_SES_BUSY);
    /* Busy. */
    CHECK_EQ(hitec_session_enter(&s, true, 0), HITEC_SES_ERR_BUSY);
    CHECK_EQ(hitec_session_read_all(&s), HITEC_SES_ERR_BUSY);
    CHECK_EQ(hitec_session_read_set(&s, odd, 1), HITEC_SES_ERR_BUSY);
    CHECK_EQ(hitec_session_write(&s, &img), HITEC_SES_ERR_BUSY);
    CHECK_EQ(hitec_session_run_plan(&s, NULL, NULL), HITEC_SES_ERR_BUSY);
    CHECK_EQ(hitec_session_leave(&s), HITEC_SES_ERR_BUSY);
    CHECK_EQ(hitec_session_result(&s), HITEC_SES_BUSY);
    CHECK_EQ(hitec_session_next(&s, t, &st), HITEC_SES_STEP_POWER);
    CHECK_EQ(st.off_ms, 10000);
    CHECK_EQ(give_ok(&s, t, 0), HITEC_SES_BUSY);
    CHECK_EQ(give_ok(&s, t, 0), HITEC_SES_ERR_ARG);
    t += 1001u;
    CHECK_EQ(hitec_session_next(&s, t, NULL), HITEC_SES_STEP_XFER);
    CHECK_EQ(give_ok(&s, t, 34485u), HITEC_SES_OK);
    /* Arguments of a read and a write. */
    CHECK_EQ(hitec_session_read_set(&s, NULL, 1), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_read_set(&s, odd, 0), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_read_set(&s, all, 257), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_read_set(&s, odd, 2), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_write(&s, NULL), HITEC_SES_ERR_ARG);
    hitec_image_init(&img, HITEC_CLASSIC);
    CHECK_EQ(hitec_session_write(&s, &img), HITEC_SES_ERR_ARG);
    /* A target the plan cannot reach from what was read. */
    d485(&img);
    hitec_image_put_reg(&img, 0x54, 6);
    CHECK_EQ(hitec_session_write(&s, &img), HITEC_SES_ERR_PLAN);
    CHECK_EQ(s.plan_result, HITEC_PLAN_ERR_UNKNOWN);
    /* The same image: an empty plan, nothing sent. */
    img = s.image;
    CHECK_EQ(hitec_session_write(&s, &img), HITEC_SES_OK);
    CHECK_EQ(s.plan.n, 0);
    CHECK_EQ(hitec_session_next(&s, t, NULL), HITEC_SES_STEP_DONE);
    /* A set: ascending, each address once; the image keeps the rest. */
    CHECK_EQ(hitec_session_read_set(&s, dup, 3), HITEC_SES_BUSY);
    CHECK_EQ(s.total, 2);
    t += 21u;
    CHECK_EQ(hitec_session_next(&s, t, &st), HITEC_SES_STEP_XFER);
    CHECK_EQ(st.dec_arg, 0x02);
    CHECK_EQ(give_ok(&s, t, 2), HITEC_SES_BUSY);
    t += 21u;
    CHECK_EQ(hitec_session_next(&s, t, &st), HITEC_SES_STEP_XFER);
    CHECK_EQ(st.dec_arg, 0x04);
    CHECK_EQ(give_ok(&s, t, 4), HITEC_SES_OK);
    CHECK_EQ(hitec_image_reg(&s.image, 0x00), 34485u);
    for (unsigned i = 0; i < 256u; ++i) {
        all[i] = (uint8_t)(i & 0xFEu);
    }
    CHECK_EQ(hitec_session_read_set(&s, all, 256), HITEC_SES_BUSY);
    CHECK_EQ(s.total, 128);
}

TEST_CASE(a_classic_full_read_keeps_the_version_and_takes_100_cells)
{
    rig_t *r = &g;
    hitec_model_t m;
    static const uint8_t script[] = { SC_ENTER, SC_READ_ALL, SC_END };

    rig_init(r, HITEC_CLASSIC, HITEC_SES_CLOCK_MS, 0);
    CHECK_EQ(rig_run(r, script), HITEC_SES_OK);
    CHECK_EQ(r->n, 3u + 100u);
    CHECK_EQ(r->s.image.version, 0x16);
    CHECK_EQ(r->s.image.has_version, 1);
    CHECK(hitec_identify(&r->s.image, &m));
    CHECK_EQ(m.model, HITEC_MODEL_HS5645MG);
    CHECK_EQ(hitec_c_eeprom_sum(&r->s.image), 0);
    /* A failed cell ends the read; the image keeps the cells before it. */
    rig_init(r, HITEC_CLASSIC, HITEC_SES_CLOCK_MS, 0);
    for (unsigned i = 0; i < 3u; ++i) {
        rig_force(r, 3u + 40u + i, HITEC_SES_RAN, HITEC_RX_ERR_FRAMING);
    }
    CHECK_EQ(rig_run(r, script), HITEC_SES_ERR_READ);
    CHECK_EQ(r->s.fail_addr, 0x34);
    CHECK_EQ(r->s.done, 40);
    CHECK(hitec_image_known(&r->s.image, 0x33));
    CHECK(!hitec_image_known(&r->s.image, 0x34));
}

int main(void)
{
    RUN(a_classic_entry_cycles_the_supply_answers_and_sends_0x67);
    RUN(the_classic_entry_takes_a_plus_from_0_23ms_to_1999ms_only);
    RUN(a_classic_entry_without_two_plus_bytes_fails_and_is_not_tried_again);
    RUN(a_minus_to_the_first_command_is_a_bad_sum_and_tried_3_times);
    RUN(a_d_entry_waits_1000ms_after_the_supply_comes_on);
    RUN(a_d_entry_on_the_supply_as_it_is_reads_the_model_at_once);
    RUN(a_read_answers_on_try_1_or_3_and_fails_after_3);
    RUN(every_rx_code_on_every_step);
    RUN(an_abort_ends_every_step);
    RUN(an_abort_in_a_gap_ends_at_once_and_keeps_what_was_read);
    RUN(a_cut_or_a_step_that_did_not_run_ends_the_operation);
    RUN(every_refusal_of_an_operation);
    RUN(a_classic_full_read_keeps_the_version_and_takes_100_cells);
    return test_summary("hitec_session");
}
