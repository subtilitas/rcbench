/*
 * The Hitec session's writes (protocols/hitec/hitec_session.c), on the rig
 * of hitec_session_rig.h: the plans of both families, their read-back and
 * leave.  Entry, reads, the decoder codes, aborts and refusals are in
 * test_hitec_session.c.
 *
 * Under test:
 *  - the D series plan's order and waits: write, 20ms, save, 100ms,
 *    reboot, 1000ms, read-back, every gap across the wrap of a
 *    microsecond and a millisecond clock;
 *  - the classic cell and balance pairs in the HPP-21 Plus's order and
 *    their read-back of cells 0x00 to 0x23;
 *  - a classic write without its `+ +`: tried again, 3 tries;
 *  - a write the servo did not take, found by the read-back;
 *  - a plan the caller built, with and without an image to compare, and
 *    every refusal of one;
 *  - leave: the classic supply cycle and the D series reboot;
 *  - the classic 2ms gap across both clock wraps;
 *  - the longest wait a plan step can ask, 65535ms, reported whole.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_session_rig.h"

static rig_t g;

/* ---------------------------------------------------------------- cases */

TEST_CASE(a_d_write_plan_writes_saves_waits_reboots_waits_and_reads_back)
{
    static const uint8_t clocks[] = { HITEC_SES_CLOCK_US, HITEC_SES_CLOCK_MS };

    for (unsigned k = 0; k < 2u; ++k) {
        rig_t *r = &g;
        static const uint8_t script[] = { SC_ENTER_ON, SC_READ_ALL, SC_END };
        const uint32_t ms = clocks[k] == HITEC_SES_CLOCK_US ? 1000u : 1u;
        static const struct { uint8_t addr; uint16_t value; uint32_t gap; }
            want[] = { { 0x54, 6, 20 }, { 0x70, 0xFFFF, 100 },
                       { 0x46, 1, 1000 } };
        hitec_ses_step_t st;
        uint32_t t;

        rig_init(r, HITEC_D, clocks[k], 0);
        CHECK_EQ(rig_run(r, script), HITEC_SES_OK);
        r->target = r->s.image;
        CHECK_EQ(hitec_param_set(&r->target, HITEC_LAYOUT_D,
                                 HITEC_P_D_SPEED, 30), HITEC_SET_OK);
        CHECK_EQ(hitec_session_write(&r->s, &r->target), HITEC_SES_BUSY);
        CHECK_EQ(r->s.plan.n, 3);
        /* Every gap across the wrap of the clock. */
        t = 0xFFFFFFFFu - 1500u * ms;
        r->now = t;
        for (unsigned i = 0; i < 3u; ++i) {
            uint8_t f[HITEC_D_WRITE_LEN];

            CHECK_EQ(hitec_session_next(&r->s, t, &st), HITEC_SES_STEP_XFER);
            hitec_d_write_frame(0, want[i].addr, want[i].value, f);
            CHECK(memcmp(st.xfer.tx, f, sizeof(f)) == 0);
            CHECK_EQ(st.xfer.n_tx, 7);
            CHECK_EQ(st.xfer.window_us, 0u);
            CHECK_EQ(st.decoder, HITEC_DEC_NONE);
            CHECK_EQ(give_ok(&r->s, t, 0), HITEC_SES_BUSY);
            CHECK_EQ(r->s.step_index, i + 1u);
            check_gap(&r->s, t, want[i].gap * ms);
            t += want[i].gap * ms + 1u;
        }
        /* The read-back: the full set again, then compared. */
        r->now = t;
        r->n = 0;
        r->mem[0x54] = 6;
        r->mem[0x55] = 0;
        while (rig_step(r) != HITEC_SES_STEP_DONE) {
        }
        CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_OK);
        CHECK_EQ(r->n, 21);
        CHECK_EQ(r->log[0].at, t);
        CHECK_EQ(r->log[0].st.decoder, HITEC_DEC_D);
        CHECK_EQ(hitec_image_reg(&r->s.image, 0x54), 6);
        CHECK(!hitec_image_known(&r->s.image, 0x70));
    }
}

TEST_CASE(a_classic_write_plan_writes_each_cell_then_its_balance)
{
    rig_t *r = &g;
    hitec_image_t to;
    unsigned cells = 0;

    rig_init(r, HITEC_CLASSIC, HITEC_SES_CLOCK_US, 0xFFFFFFFFu - 1000000u);
    CHECK_EQ(rig_run(r, (const uint8_t[]){ SC_ENTER, SC_READ_BLOCK, SC_END }),
             HITEC_SES_OK);
    /* Speed 50% and dead band 5: 0x06, 0x1C, 0x02, 0x1C in the HPP-21
     * Plus's order. */
    to = r->s.image;
    CHECK_EQ(hitec_param_set(&to, HITEC_LAYOUT_HS5, HITEC_P_C_SPEED, 50),
             HITEC_SET_OK);
    CHECK_EQ(hitec_param_set(&to, HITEC_LAYOUT_HS5, HITEC_P_C_DEAD_BAND, 5),
             HITEC_SET_OK);
    r->n = 0;
    CHECK_EQ(hitec_session_write(&r->s, &to), HITEC_SES_BUSY);
    CHECK_EQ(r->s.plan.n, 4);
    while (rig_step(r) != HITEC_SES_STEP_DONE) {
    }
    CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_OK);
    CHECK_EQ(r->n, 4u + 36u);
    for (unsigned i = 0; i < 4u; ++i) {
        const hitec_xfer_t *x = &r->log[i].st.xfer;
        static const uint8_t addr[] = { 0x02, 0x1C, 0x06, 0x1C };

        CHECK_EQ(x->tx[0], 0x62);
        CHECK_EQ(x->tx[1], addr[i]);
        CHECK_EQ(x->tx[2], r->s.plan.step[i].value);
        CHECK(hitec_c_frame_ok(x->tx));
        CHECK_EQ(r->log[i].st.decoder, HITEC_DEC_C);
        CHECK_EQ(r->log[i].st.dec_arg, 0x62);
        if (i > 0u) {
            CHECK(r->log[i].at - r->log[i - 1u].at - r->cost > 2000u);
        }
    }
    /* After each pair the cells balance. */
    CHECK_EQ(hitec_c_eeprom_sum(&r->s.image), 0);
    for (unsigned i = 4; i < r->n; ++i) {
        CHECK_EQ(r->log[i].st.xfer.tx[0], 0x61);
        CHECK_EQ(r->log[i].st.xfer.tx[1], i - 4u);
        ++cells;
    }
    CHECK_EQ(cells, 36);
    CHECK_EQ(r->mem[0x06], 0x1C);
    CHECK_EQ(r->s.step_index, 4);
}

TEST_CASE(a_classic_write_without_its_ack_is_tried_3_times)
{
    rig_t *r = &g;

    rig_init(r, HITEC_CLASSIC, HITEC_SES_CLOCK_MS, 0);
    /* 3 entry steps and 36 reads; the cell takes on its third try (step
     * 41), the balance on none of its 3 (steps 42 to 44). */
    rig_force(r, 39, HITEC_SES_RAN, HITEC_RX_ERR_LATE);
    rig_force(r, 40, HITEC_SES_RAN, HITEC_RX_ERR_ACK);
    rig_force(r, 42, HITEC_SES_RAN, HITEC_RX_ERR_SILENT);
    rig_force(r, 43, HITEC_SES_RAN, HITEC_RX_ERR_GAP);
    rig_force(r, 44, HITEC_SES_RAN, HITEC_RX_ERR_NAK);
    CHECK_EQ(rig_run(r, k_c_script), HITEC_SES_ERR_WRITE);
    CHECK_EQ(r->n, 45);
    CHECK_EQ(r->s.step_index, 1);
    CHECK_EQ(r->s.fail_addr, 0x1C);
    CHECK_EQ(r->s.last_rx, HITEC_RX_ERR_NAK);
    CHECK_EQ(r->log[39].st.xfer.tx[1], 0x06);
    CHECK_EQ(r->log[41].st.xfer.tx[1], 0x06);
    CHECK_EQ(r->log[44].st.xfer.tx[1], 0x1C);
    /* The cell took: the servo's checksum is off, the image says so. */
    CHECK(hitec_c_eeprom_sum(&r->s.image) != 0);
}

TEST_CASE(a_write_the_servo_did_not_take_fails_the_read_back)
{
    static const uint8_t series[] = { HITEC_CLASSIC, HITEC_D };

    for (unsigned k = 0; k < 2u; ++k) {
        rig_t *r = &g;

        rig_init(r, series[k], HITEC_SES_CLOCK_US, 0);
        r->ignore = 1;
        r->ign = series[k] == HITEC_CLASSIC ? 0x06 : 0x54;
        CHECK_EQ(rig_run(r, series[k] == HITEC_CLASSIC ? k_c_script
                                                       : k_d_script),
                 HITEC_SES_ERR_VERIFY);
        CHECK(hitec_ses_in_set(r->s.diff, r->ign));
        CHECK(!hitec_ses_in_set(r->s.diff, 0x32));
        CHECK(!hitec_ses_in_set(NULL, 0x00));
        CHECK_EQ(r->s.step_index, r->s.plan.n);
    }
}

TEST_CASE(a_caller_plan_runs_and_reads_back_without_a_compare)
{
    rig_t *r = &g;
    hitec_plan_t p;
    hitec_image_t other;
    static const uint8_t script[] = { SC_ENTER_ON, SC_READ_ALL, SC_END };

    rig_init(r, HITEC_D, HITEC_SES_CLOCK_MS, 5u);
    CHECK_EQ(rig_run(r, script), HITEC_SES_OK);
    hitec_d_factory_plan(&p);
    r->n = 0;
    CHECK_EQ(hitec_session_run_plan(&r->s, &p, NULL), HITEC_SES_BUSY);
    while (rig_step(r) != HITEC_SES_STEP_DONE) {
    }
    CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_OK);
    CHECK_EQ(r->n, 3u + 21u);
    CHECK_EQ(r->log[0].st.xfer.tx[2], 0x6E);
    /* A D series write has no answer: the image holds 0x6E only once
     * read. */
    CHECK(!hitec_image_known(&r->s.image, 0x6E));
    /* With an image to hold: compared. */
    other = r->s.image;
    hitec_image_put_reg(&other, 0x4E, 9);
    CHECK_EQ(hitec_session_run_plan(&r->s, &p, &other), HITEC_SES_BUSY);
    while (rig_step(r) != HITEC_SES_STEP_DONE) {
    }
    CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_ERR_VERIFY);
    CHECK(hitec_ses_in_set(r->s.diff, 0x4E));
    /* Refused: other series, too long, a step of the other series, a
     * classic value above 255, an odd register, a balance elsewhere. */
    hitec_image_init(&other, HITEC_CLASSIC);
    CHECK_EQ(hitec_session_run_plan(&r->s, &p, &other), HITEC_SES_ERR_ARG);
    CHECK_EQ(hitec_session_run_plan(&r->s, NULL, NULL), HITEC_SES_ERR_ARG);
    p.n = HITEC_PLAN_MAX + 1u;
    CHECK_EQ(hitec_session_run_plan(&r->s, &p, NULL), HITEC_SES_ERR_PLAN);
    hitec_d_factory_plan(&p);
    p.step[1].kind = HITEC_STEP_C_CELL;
    CHECK_EQ(hitec_session_run_plan(&r->s, &p, NULL), HITEC_SES_ERR_PLAN);
    hitec_d_factory_plan(&p);
    p.step[0].addr = 0x6F;
    CHECK_EQ(hitec_session_run_plan(&r->s, &p, NULL), HITEC_SES_ERR_PLAN);
    rig_init(r, HITEC_CLASSIC, HITEC_SES_CLOCK_MS, 0);
    enter_classic(&r->s, HITEC_SES_CLOCK_MS, &r->now, 500);
    memset(&p, 0, sizeof(p));
    p.n = 1;
    p.step[0].kind = HITEC_STEP_C_CELL;
    p.step[0].value = 0x100;
    CHECK_EQ(hitec_session_run_plan(&r->s, &p, NULL), HITEC_SES_ERR_PLAN);
    p.step[0].kind = HITEC_STEP_C_SUM;
    p.step[0].value = 1;
    CHECK_EQ(hitec_session_run_plan(&r->s, &p, NULL), HITEC_SES_ERR_PLAN);
    p.step[0].kind = HITEC_STEP_D_SAVE;
    CHECK_EQ(hitec_session_run_plan(&r->s, &p, NULL), HITEC_SES_ERR_PLAN);
    /* An empty plan sends nothing. */
    p.n = 0;
    CHECK_EQ(hitec_session_run_plan(&r->s, &p, NULL), HITEC_SES_OK);
    CHECK_EQ(hitec_session_next(&r->s, 0, NULL), HITEC_SES_STEP_DONE);
}

TEST_CASE(a_leave_cycles_a_classic_supply_and_reboots_a_d_servo)
{
    rig_t *r = &g;
    static const uint8_t leave[] = { SC_LEAVE, SC_END };

    rig_init(r, HITEC_CLASSIC, HITEC_SES_CLOCK_US, 0);
    enter_classic(&r->s, HITEC_SES_CLOCK_US, &r->now, 1200);
    r->n = 0;
    CHECK_EQ(rig_run(r, leave), HITEC_SES_OK);
    CHECK_EQ(r->n, 1);
    CHECK_EQ(r->log[0].st.kind, HITEC_SES_STEP_POWER);
    CHECK_EQ(r->log[0].st.off_ms, 1200);
    CHECK_EQ(r->log[0].st.xfer.window_us, 1000u);
    CHECK_EQ(r->log[0].st.xfer.answer, 0);
    CHECK_EQ(r->log[0].st.decoder, HITEC_DEC_NONE);
    CHECK_EQ(r->s.in_mode, 0);
    /* Out of mode it is taken too. */
    CHECK_EQ(rig_run(r, leave), HITEC_SES_OK);

    rig_init(r, HITEC_D, HITEC_SES_CLOCK_US, 0);
    CHECK_EQ(rig_run(r, (const uint8_t[]){ SC_ENTER_ON, SC_LEAVE, SC_END }),
             HITEC_SES_OK);
    CHECK_EQ(r->n, 2);
    CHECK_EQ(r->log[1].st.kind, HITEC_SES_STEP_XFER);
    CHECK_EQ(r->log[1].st.xfer.tx[2], 0x46);
    CHECK_EQ(r->log[1].st.xfer.tx[4], 1);
    CHECK_EQ(r->log[1].st.xfer.window_us, 0u);
    CHECK_EQ(r->s.in_mode, 0);
    CHECK_EQ(hitec_session_read_all(&r->s), HITEC_SES_ERR_NOT_IN_MODE);
}

TEST_CASE(the_classic_gap_holds_across_both_clock_wraps)
{
    static const uint8_t clocks[] = { HITEC_SES_CLOCK_US, HITEC_SES_CLOCK_MS };

    for (unsigned k = 0; k < 2u; ++k) {
        hitec_session_t s;
        hitec_ses_step_t st;
        static const uint8_t addr[] = { 0x00, 0x01 };
        const uint32_t ms = clocks[k] == HITEC_SES_CLOCK_US ? 1000u : 1u;
        uint32_t t = 0xFFFFFFFFu - 10u * ms;

        enter_classic(&s, clocks[k], &t, 500);
        t = 0xFFFFFFFFu - 1u * ms;
        CHECK_EQ(hitec_session_read_set(&s, addr, 2), HITEC_SES_BUSY);
        CHECK_EQ(hitec_session_next(&s, t, &st), HITEC_SES_STEP_XFER);
        CHECK_EQ(give_ok(&s, t, 0x78), HITEC_SES_BUSY);
        CHECK_EQ(hitec_session_next(&s, t, &st), HITEC_SES_STEP_WAIT);
        CHECK_EQ(st.wait_ms, 3);
        check_gap(&s, t, 2u * ms);
        CHECK_EQ(hitec_session_next(&s, t + 2u * ms + 1u, &st),
                 HITEC_SES_STEP_XFER);
        CHECK_EQ(st.xfer.tx[1], 0x01);
        CHECK_EQ(give_ok(&s, t + 2u * ms + 1u, 0xB4), HITEC_SES_OK);
        CHECK_EQ(s.done, 2);
        CHECK_EQ(s.total, 2);
    }
}

TEST_CASE(the_longest_plan_wait_is_reported_whole)
{
    static const uint8_t clocks[] = { HITEC_SES_CLOCK_US, HITEC_SES_CLOCK_MS };

    for (unsigned k = 0; k < 2u; ++k) {
        rig_t *r = &g;
        static const uint8_t script[] = { SC_ENTER_ON, SC_END };
        const uint32_t ms = clocks[k] == HITEC_SES_CLOCK_US ? 1000u : 1u;
        hitec_plan_t p;
        hitec_ses_step_t st;
        const uint32_t t = 0xFFFFFFFFu - 100u * ms;

        rig_init(r, HITEC_D, clocks[k], 0);
        CHECK_EQ(rig_run(r, script), HITEC_SES_OK);
        memset(&p, 0, sizeof(p));
        p.n = 1;
        p.step[0].kind = HITEC_STEP_D_WRITE;
        p.step[0].addr = 0x32;
        p.step[0].value = 2;
        p.step[0].wait_ms = UINT16_MAX;
        CHECK_EQ(hitec_session_run_plan(&r->s, &p, NULL), HITEC_SES_BUSY);
        CHECK_EQ(hitec_session_next(&r->s, t, &st), HITEC_SES_STEP_XFER);
        CHECK_EQ(give_ok(&r->s, t, 0), HITEC_SES_BUSY);
        /* 65535ms and one tick are left: the report stops at 65535. */
        CHECK_EQ(hitec_session_next(&r->s, t, &st), HITEC_SES_STEP_WAIT);
        CHECK_EQ(st.wait_ms, UINT16_MAX);
        CHECK_EQ(hitec_session_next(&r->s, t + 1u, &st), HITEC_SES_STEP_WAIT);
        CHECK_EQ(st.wait_ms, UINT16_MAX);
        check_gap(&r->s, t, 65535u * ms);
    }
}

TEST_CASE(a_d_leave_waits_1000ms_for_the_reboot)
{
    static const uint8_t clocks[] = { HITEC_SES_CLOCK_US, HITEC_SES_CLOCK_MS };

    for (unsigned k = 0; k < 2u; ++k) {
        rig_t *r = &g;
        static const uint8_t script[] = { SC_ENTER_ON, SC_LEAVE, SC_END };
        const uint32_t ms = clocks[k] == HITEC_SES_CLOCK_US ? 1000u : 1u;

        rig_init(r, HITEC_D, clocks[k], 0xFFFFFFFFu - 500u * ms);
        CHECK_EQ(rig_run(r, script), HITEC_SES_OK);
        /* The reboot was fed at r->now: an entry on the supply as it is
         * reads the model only after the servo's 1000ms. */
        CHECK_EQ(hitec_session_enter(&r->s, false, 0), HITEC_SES_BUSY);
        check_gap(&r->s, r->now, 1000u * ms);
    }
}

TEST_CASE(a_d_write_whose_read_back_fails_keeps_the_old_value)
{
    rig_t *r = &g;
    static const uint8_t script[] = { SC_ENTER_ON, SC_READ_ALL, SC_END };
    const uint16_t old = 4095u;

    rig_init(r, HITEC_D, HITEC_SES_CLOCK_MS, 0);
    CHECK_EQ(rig_run(r, script), HITEC_SES_OK);
    CHECK_EQ(hitec_image_reg(&r->s.image, 0x54), old);
    r->target = r->s.image;
    CHECK_EQ(hitec_param_set(&r->target, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 30),
             HITEC_SET_OK);
    /* 0x54, save, reboot, then the read-back's 0x00 silent 3 times. */
    r->n = 0;
    for (unsigned i = 3; i < 6u; ++i) {
        rig_force(r, i, HITEC_SES_RAN, HITEC_RX_ERR_SILENT);
    }
    CHECK_EQ(hitec_session_write(&r->s, &r->target), HITEC_SES_BUSY);
    while (rig_step(r) != HITEC_SES_STEP_DONE) {
    }
    CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_ERR_NO_SERVO);
    CHECK_EQ(r->s.step_index, 3);
    CHECK_EQ(hitec_image_reg(&r->s.image, 0x54), old);
    /* The servo is not confirmed at 6: the same draft plans the write
     * again and the read-back confirms it. */
    CHECK_EQ(hitec_session_write(&r->s, &r->target), HITEC_SES_BUSY);
    CHECK_EQ(r->s.plan.n, 3);
    while (rig_step(r) != HITEC_SES_STEP_DONE) {
    }
    CHECK_EQ(hitec_session_result(&r->s), HITEC_SES_OK);
    CHECK_EQ(hitec_image_reg(&r->s.image, 0x54), 6);
}

int main(void)
{
    RUN(a_d_write_plan_writes_saves_waits_reboots_waits_and_reads_back);
    RUN(a_classic_write_plan_writes_each_cell_then_its_balance);
    RUN(a_classic_write_without_its_ack_is_tried_3_times);
    RUN(a_write_the_servo_did_not_take_fails_the_read_back);
    RUN(a_caller_plan_runs_and_reads_back_without_a_compare);
    RUN(a_leave_cycles_a_classic_supply_and_reboots_a_d_servo);
    RUN(the_classic_gap_holds_across_both_clock_wraps);
    RUN(the_longest_plan_wait_is_reported_whole);
    RUN(a_d_leave_waits_1000ms_for_the_reboot);
    RUN(a_d_write_whose_read_back_fails_keeps_the_old_value);
    return test_summary("hitec_session_write");
}
