/*
 * The Hitec session's move (hitec_session_move()), on the rig of
 * hitec_session_rig.h.
 *
 * Under test:
 *  - D series: the write of 0x1E, the 20ms gap, the read of 0x0C, and a
 *    move without the read; classic: `66 high low` answered `+ +`, the 2ms
 *    gap, `65` answered high, low;
 *  - the target limits: D 0 and 6000 taken, 6001 refused; classic 3599
 *    and 8401 refused, 3600 and 8400 taken; every other refusal;
 *  - tries: the position read on try 1 and 3, none on 3, silent on 3; a
 *    classic move with no `+ +`;
 *  - an abort with the write out, in the gap, with the read out; a cut
 *    and a step that did not run;
 *  - the image untouched, no step marked more;
 *  - 50 passes 20ms apart across the wrap of both clocks.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_session_rig.h"

static rig_t g;

static const uint8_t k_d_on[] = { SC_ENTER_ON, SC_END };

/* A D series session in mode on the rig, its clock at @p t0. */
static void d_in_mode(rig_t *r, uint8_t clock, uint32_t t0)
{
    rig_init(r, HITEC_D, clock, t0);
    CHECK_EQ(rig_run(r, k_d_on), HITEC_SES_OK);
    CHECK_EQ(r->s.in_mode, 1u);
}

/* Run the operation that runs to its end on the rig. */
static hitec_ses_t rig_finish(rig_t *r)
{
    for (unsigned i = 0; i < 1000u; ++i) {
        if (rig_step(r) == HITEC_SES_STEP_DONE) {
            return hitec_session_result(&r->s);
        }
    }
    T_FAIL("the operation did not end");
    return HITEC_SES_BUSY;
}

/* Hand out the step that is due at the rig's clock, past any gap. */
static hitec_ses_kind_t due(rig_t *r, hitec_ses_step_t *st)
{
    hitec_ses_kind_t k = hitec_session_next(&r->s, r->now, st);

    while (k == HITEC_SES_STEP_WAIT) {
        r->now += st->wait_ms * tick_ms(r);
        k = hitec_session_next(&r->s, r->now, st);
    }
    return k;
}

TEST_CASE(a_d_move_writes_0x1e_waits_20ms_and_reads_0x0c)
{
    rig_t *r = &g;
    hitec_ses_step_t st;
    hitec_image_t before;
    uint8_t f[HITEC_D_WRITE_LEN];

    d_in_mode(r, HITEC_SES_CLOCK_MS, 1000u);
    before = r->s.image;
    r->mem[HITEC_D_REG_POSITION] = (uint8_t)(8733u & 0xFFu);
    r->mem[HITEC_D_REG_POSITION + 1u] = (uint8_t)(8733u >> 8);
    CHECK_EQ(hitec_session_move(&r->s, 3300u, true), HITEC_SES_BUSY);
    CHECK_EQ(r->s.has_position, 0u);

    CHECK_EQ(due(r, &st), HITEC_SES_STEP_XFER);
    hitec_d_write_frame(0u, HITEC_D_REG_TARGET, 3300u, f);
    CHECK_EQ(st.xfer.n_tx, HITEC_D_WRITE_LEN);
    CHECK(memcmp(st.xfer.tx, f, sizeof(f)) == 0);
    CHECK_EQ(st.decoder, HITEC_DEC_NONE);
    CHECK_EQ(st.more, 0u);
    (void)rig_step(r);
    CHECK_EQ(r->mem[HITEC_D_REG_TARGET] | r->mem[HITEC_D_REG_TARGET + 1u] << 8,
             3300);
    /* The read goes 20ms after the write's result. */
    check_gap(&r->s, r->now, 20u);

    r->now += 21u;
    CHECK_EQ(hitec_session_next(&r->s, r->now, &st), HITEC_SES_STEP_XFER);
    CHECK_EQ(st.xfer.n_tx, HITEC_D_READ_LEN);
    CHECK_EQ(st.xfer.tx[2], HITEC_D_REG_POSITION);
    CHECK_EQ(st.decoder, HITEC_DEC_D);
    CHECK_EQ(st.dec_arg, HITEC_D_REG_POSITION);
    CHECK_EQ(st.more, 0u);
    CHECK_EQ(rig_finish(r), HITEC_SES_OK);
    CHECK_EQ(r->s.position, 8733u);
    CHECK_EQ(r->s.has_position, 1u);
    CHECK(memcmp(&r->s.image, &before, sizeof(before)) == 0);
    /* The next operation waits the 20ms after the read. */
    CHECK_EQ(hitec_session_move(&r->s, 3000u, false), HITEC_SES_BUSY);
    check_gap(&r->s, r->now, 20u);
}

TEST_CASE(a_d_move_without_the_read_ends_after_the_write)
{
    rig_t *r = &g;

    d_in_mode(r, HITEC_SES_CLOCK_MS, 0u);
    CHECK_EQ(hitec_session_move(&r->s, 0u, false), HITEC_SES_BUSY);
    CHECK_EQ(rig_finish(r), HITEC_SES_OK);
    CHECK_EQ(r->s.has_position, 0u);
    CHECK_EQ(r->n, 2u);  /* the entry's read of 0x00, the write */
    CHECK_EQ(r->log[1].st.xfer.tx[2], HITEC_D_REG_TARGET);
    CHECK_EQ(r->log[1].st.xfer.tx[4], 0u);
    CHECK_EQ(r->log[1].st.xfer.tx[5], 0u);
}

TEST_CASE(a_classic_move_sends_0x66_then_reads_0x65)
{
    rig_t *r = &g;
    hitec_ses_step_t st;
    uint8_t f[HITEC_C_FRAME_LEN];
    uint32_t t = 500u;

    rig_init(r, HITEC_CLASSIC, HITEC_SES_CLOCK_MS, 0u);
    enter_classic(&r->s, HITEC_SES_CLOCK_MS, &t, 500u);
    r->now = t;
    r->c_pos = 726u;
    CHECK_EQ(hitec_session_move(&r->s, 0x1770u, true), HITEC_SES_BUSY);

    CHECK_EQ(due(r, &st), HITEC_SES_STEP_XFER);
    hitec_c_frame(HITEC_C_MOVE, 0x17u, 0x70u, f);
    CHECK(memcmp(st.xfer.tx, f, sizeof(f)) == 0);
    CHECK_EQ(st.decoder, HITEC_DEC_C);
    CHECK_EQ(st.dec_arg, HITEC_C_MOVE);
    CHECK_EQ(st.more, 0u);
    (void)rig_step(r);
    CHECK_EQ(r->c_moved, 0x1770u);
    check_gap(&r->s, r->now, 2u);

    r->now += 3u;
    CHECK_EQ(hitec_session_next(&r->s, r->now, &st), HITEC_SES_STEP_XFER);
    hitec_c_frame(HITEC_C_READ_POSITION, 0u, 0u, f);
    CHECK(memcmp(st.xfer.tx, f, sizeof(f)) == 0);
    CHECK_EQ(st.decoder, HITEC_DEC_C);
    CHECK_EQ(st.dec_arg, HITEC_C_READ_POSITION);
    CHECK_EQ(rig_finish(r), HITEC_SES_OK);
    CHECK_EQ(r->s.position, 726u);
    CHECK_EQ(r->s.has_position, 1u);
}

TEST_CASE(every_refusal_of_a_move)
{
    rig_t *r = &g;
    hitec_session_t c;
    uint32_t t = 0u;
    static const uint8_t addrs[] = { 0x00u };

    CHECK_EQ(hitec_session_move(NULL, 3000u, true), HITEC_SES_ERR_ARG);

    rig_init(r, HITEC_D, HITEC_SES_CLOCK_MS, 0u);
    CHECK_EQ(hitec_session_move(&r->s, 3000u, true),
             HITEC_SES_ERR_NOT_IN_MODE);
    d_in_mode(r, HITEC_SES_CLOCK_MS, 0u);
    CHECK_EQ(hitec_session_move(&r->s, 6001u, true), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_move(&r->s, 65535u, true), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_OK);
    CHECK_EQ(hitec_session_move(&r->s, 6000u, true), HITEC_SES_BUSY);
    CHECK_EQ(hitec_session_move(&r->s, 3000u, true), HITEC_SES_ERR_BUSY);
    CHECK_EQ(hitec_session_read_set(&r->s, addrs, 1u), HITEC_SES_ERR_BUSY);
    CHECK_EQ(rig_finish(r), HITEC_SES_OK);
    CHECK_EQ(hitec_session_move(&r->s, 0u, true), HITEC_SES_BUSY);
    CHECK_EQ(rig_finish(r), HITEC_SES_OK);
    /* A leave ends the mode. */
    CHECK_EQ(hitec_session_leave(&r->s), HITEC_SES_BUSY);
    CHECK_EQ(rig_finish(r), HITEC_SES_OK);
    CHECK_EQ(hitec_session_move(&r->s, 3000u, true),
             HITEC_SES_ERR_NOT_IN_MODE);

    enter_classic(&c, HITEC_SES_CLOCK_MS, &t, 500u);
    CHECK_EQ(hitec_session_move(&c, 3599u, true), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_move(&c, 8401u, true), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_move(&c, 0u, true), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_move(&c, 3600u, true), HITEC_SES_BUSY);
    CHECK(hitec_session_abort(&c) == false);
    CHECK_EQ(hitec_session_move(&c, 8400u, true), HITEC_SES_BUSY);
}

TEST_CASE(the_position_read_has_3_tries)
{
    static const struct {
        uint8_t rx[3];
        unsigned n;
        hitec_ses_t res;
    } k[] = {
        { { HITEC_RX_OK, 0, 0 }, 1, HITEC_SES_OK },
        { { HITEC_RX_ERR_SUM, HITEC_RX_ERR_LATE, HITEC_RX_OK }, 3,
          HITEC_SES_OK },
        { { HITEC_RX_ERR_SUM, HITEC_RX_ERR_SUM, HITEC_RX_ERR_SUM }, 3,
          HITEC_SES_ERR_READ },
        { { HITEC_RX_ERR_SILENT, HITEC_RX_ERR_SILENT, HITEC_RX_ERR_SILENT }, 3,
          HITEC_SES_ERR_NO_SERVO },
    };
    rig_t *r = &g;
    hitec_ses_step_t st;

    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        hitec_ses_t res = HITEC_SES_BUSY;

        d_in_mode(r, HITEC_SES_CLOCK_MS, 0u);
        CHECK_EQ(hitec_session_move(&r->s, 3000u, true), HITEC_SES_BUSY);
        CHECK_EQ(due(r, &st), HITEC_SES_STEP_XFER);
        CHECK_EQ(give_ok(&r->s, r->now, 0u), HITEC_SES_BUSY);
        for (unsigned n = 0; n < k[i].n; ++n) {
            CHECK_EQ(due(r, &st), HITEC_SES_STEP_XFER);
            CHECK_EQ(st.xfer.tx[2], HITEC_D_REG_POSITION);
            res = give(&r->s, r->now, HITEC_SES_RAN, k[i].rx[n], 4840u);
        }
        CHECK_EQ(res, k[i].res);
        CHECK_EQ(r->s.tries, k[i].n);
        CHECK_EQ(r->s.has_position, k[i].res == HITEC_SES_OK);
        if (k[i].res == HITEC_SES_OK) {
            CHECK_EQ(r->s.position, 4840u);
        } else {
            CHECK_EQ(r->s.fail_addr, HITEC_D_REG_POSITION);
        }
    }
}

TEST_CASE(a_classic_move_with_no_plus_plus_fails_after_3_tries)
{
    rig_t *r = &g;
    hitec_ses_step_t st;
    uint32_t t = 0u;
    hitec_ses_t res = HITEC_SES_BUSY;

    rig_init(r, HITEC_CLASSIC, HITEC_SES_CLOCK_MS, 0u);
    enter_classic(&r->s, HITEC_SES_CLOCK_MS, &t, 500u);
    r->now = t;
    CHECK_EQ(hitec_session_move(&r->s, 6000u, true), HITEC_SES_BUSY);
    for (unsigned n = 0; n < 3u; ++n) {
        CHECK_EQ(due(r, &st), HITEC_SES_STEP_XFER);
        CHECK_EQ(st.xfer.tx[0], HITEC_C_MOVE);
        CHECK_EQ(st.more, 0u);
        res = give(&r->s, r->now, HITEC_SES_RAN, HITEC_RX_ERR_ACK, 0u);
    }
    CHECK_EQ(res, HITEC_SES_ERR_WRITE);
    CHECK_EQ(r->s.fail_addr, HITEC_C_MOVE);
    CHECK_EQ(r->s.has_position, 0u);
}

TEST_CASE(an_abort_ends_a_move_at_every_step_and_in_its_gap)
{
    rig_t *r = &g;
    hitec_ses_step_t st;
    hitec_image_t before;

    /* The write out: it ends at its result, before the read. */
    d_in_mode(r, HITEC_SES_CLOCK_MS, 0u);
    before = r->s.image;
    CHECK_EQ(hitec_session_move(&r->s, 5600u, true), HITEC_SES_BUSY);
    CHECK_EQ(due(r, &st), HITEC_SES_STEP_XFER);
    CHECK(hitec_session_abort(&r->s));
    CHECK_EQ(give(&r->s, r->now, HITEC_SES_CUT, 0u, 0u),
             HITEC_SES_ERR_ABORTED);
    CHECK_EQ(r->s.has_position, 0u);
    CHECK(memcmp(&r->s.image, &before, sizeof(before)) == 0);

    /* The write ran, then an abort in the gap ends it at once. */
    d_in_mode(r, HITEC_SES_CLOCK_MS, 0u);
    CHECK_EQ(hitec_session_move(&r->s, 5600u, true), HITEC_SES_BUSY);
    CHECK_EQ(due(r, &st), HITEC_SES_STEP_XFER);
    CHECK_EQ(give_ok(&r->s, r->now, 0u), HITEC_SES_BUSY);
    CHECK(hitec_session_abort(&r->s) == false);
    CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_ERR_ABORTED);
    CHECK_EQ(hitec_session_next(&r->s, r->now, &st), HITEC_SES_STEP_DONE);

    /* An abort with the write out that ran: ABORTED, no read. */
    d_in_mode(r, HITEC_SES_CLOCK_MS, 0u);
    CHECK_EQ(hitec_session_move(&r->s, 5600u, true), HITEC_SES_BUSY);
    CHECK_EQ(due(r, &st), HITEC_SES_STEP_XFER);
    CHECK(hitec_session_abort(&r->s));
    CHECK_EQ(give_ok(&r->s, r->now, 0u), HITEC_SES_ERR_ABORTED);
    CHECK_EQ(hitec_session_next(&r->s, r->now, &st), HITEC_SES_STEP_DONE);
    CHECK_EQ(r->s.has_position, 0u);

    /* The read out: it ends with the read stored. */
    d_in_mode(r, HITEC_SES_CLOCK_MS, 0u);
    CHECK_EQ(hitec_session_move(&r->s, 5600u, true), HITEC_SES_BUSY);
    CHECK_EQ(due(r, &st), HITEC_SES_STEP_XFER);
    CHECK_EQ(give_ok(&r->s, r->now, 0u), HITEC_SES_BUSY);
    CHECK_EQ(due(r, &st), HITEC_SES_STEP_XFER);
    CHECK(hitec_session_abort(&r->s));
    CHECK_EQ(give_ok(&r->s, r->now, 13003u), HITEC_SES_ERR_ABORTED);
    CHECK_EQ(r->s.has_position, 1u);
    CHECK_EQ(r->s.position, 13003u);
    /* The abort keeps the 20ms after the read for the next operation. */
    CHECK_EQ(hitec_session_move(&r->s, 3000u, true), HITEC_SES_BUSY);
    check_gap(&r->s, r->now, 20u);
}

TEST_CASE(a_cut_or_lost_step_ends_a_move)
{
    static const struct {
        uint8_t at;   /* 0 the write, 1 the read */
        uint8_t run;
        hitec_ses_t res;
    } k[] = {
        { 0, HITEC_SES_CUT, HITEC_SES_ERR_CUT },
        { 0, HITEC_SES_FAILED, HITEC_SES_ERR_LINK },
        { 1, HITEC_SES_CUT, HITEC_SES_ERR_CUT },
        { 1, HITEC_SES_FAILED, HITEC_SES_ERR_LINK },
    };
    rig_t *r = &g;
    hitec_ses_step_t st;

    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        hitec_image_t before;

        d_in_mode(r, HITEC_SES_CLOCK_MS, 0u);
        before = r->s.image;
        CHECK_EQ(hitec_session_move(&r->s, 400u, true), HITEC_SES_BUSY);
        CHECK_EQ(due(r, &st), HITEC_SES_STEP_XFER);
        if (k[i].at == 1u) {
            CHECK_EQ(give_ok(&r->s, r->now, 0u), HITEC_SES_BUSY);
            CHECK_EQ(due(r, &st), HITEC_SES_STEP_XFER);
        }
        CHECK_EQ(give(&r->s, r->now, k[i].run, 0u, 0u), k[i].res);
        CHECK_EQ(r->s.has_position, 0u);
        /* A move forgets nothing: it writes no saved register. */
        CHECK(memcmp(&r->s.image, &before, sizeof(before)) == 0);
        /* The step may have reached the servo: its 20ms stand. */
        CHECK_EQ(hitec_session_move(&r->s, 3000u, true), HITEC_SES_BUSY);
        check_gap(&r->s, r->now, 20u);
    }
}

TEST_CASE(fifty_passes_go_20ms_apart_across_the_wrap_of_both_clocks)
{
    static const struct {
        uint8_t clock;
        uint32_t t0;
    } k[] = {
        { HITEC_SES_CLOCK_MS, 0xFFFFFF00u },
        { HITEC_SES_CLOCK_US, 0xFFF00000u },
    };
    rig_t *r = &g;

    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        const uint32_t ms = k[i].clock == HITEC_SES_CLOCK_US ? 1000u : 1u;
        uint32_t last = 0;
        bool wrapped = false;

        d_in_mode(r, k[i].clock, k[i].t0);
        r->n = 0;
        for (unsigned pass = 0; pass < 50u; ++pass) {
            const uint16_t t = (uint16_t)(3000u + pass * 10u);

            CHECK_EQ(hitec_session_move(&r->s, t, true), HITEC_SES_BUSY);
            CHECK_EQ(rig_finish(r), HITEC_SES_OK);
            CHECK_EQ(r->mem[HITEC_D_REG_TARGET]
                         | r->mem[HITEC_D_REG_TARGET + 1u] << 8,
                     t);
        }
        CHECK_EQ(r->n, 100u);
        for (unsigned n = 1; n < 100u; ++n) {
            /* Each step goes once 20ms have passed since the result of
             * the one before, which came r->cost after it went out: the
             * rig waits whole ms, 21 of them. */
            CHECK_EQ(r->log[n].at - r->log[n - 1u].at, r->cost + 21u * ms);
            wrapped = wrapped || r->log[n].at < last;
            last = r->log[n].at;
        }
        CHECK(wrapped);
    }
}

int main(void)
{
    RUN(a_d_move_writes_0x1e_waits_20ms_and_reads_0x0c);
    RUN(a_d_move_without_the_read_ends_after_the_write);
    RUN(a_classic_move_sends_0x66_then_reads_0x65);
    RUN(every_refusal_of_a_move);
    RUN(the_position_read_has_3_tries);
    RUN(a_classic_move_with_no_plus_plus_fails_after_3_tries);
    RUN(an_abort_ends_a_move_at_every_step_and_in_its_gap);
    RUN(a_cut_or_lost_step_ends_a_move);
    RUN(fifty_passes_go_20ms_apart_across_the_wrap_of_both_clocks);
    return test_summary("hitec_session_move");
}
