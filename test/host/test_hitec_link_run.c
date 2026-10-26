/*
 * A session with a servo end to end: hitec_session (protocols/hitec) hands
 * out the steps, the panel's link half (shared/bench/hitec_link.c) runs
 * each on the HITEC page (shared/outputs/hitec_port.c) through the link
 * dispatch, and the servo model of hitec_sim.h answers.  The D series
 * entry with its supply cycle and 1000ms wait, a read of one register and
 * of the full set, the leave; the classic entry with the servo's two `+`
 * and the version, a read of one cell, the leave; a classic servo that
 * does not answer; an abort, a stop and a lost link in the middle of a
 * read.  The rig is hitec_link_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_link_rig.h"

/* What a case does when the session hands out its step number n. */
typedef enum {
    AT_NOTHING = 0,
    AT_ABORT,   /* the operator's cancel: the session and the link */
    AT_STOP,    /* a switch to SAFE at the IOmcu */
    AT_LOSE,    /* the link goes, and comes back */
} at_t;

static unsigned g_steps;
static unsigned g_at_step;
static at_t g_at;

/* What a case does at its step, once the step is on its way. */
static void at_step(hitec_session_t *s)
{
    switch (g_at) {
    case AT_ABORT:
        (void)hitec_session_abort(s);
        hitec_link_abort(&g_l);
        break;
    case AT_STOP:
        run_for(2u * HITEC_LINK_POLL_MS);
        stop_pulse();
        break;
    case AT_LOSE:
        hitec_link_lost(&g_l);
        break;
    case AT_NOTHING:
    default:
        break;
    }
    g_at = AT_NOTHING;
}

/* The session's operation run through the link to its end, @p max_ms at
 * most: each step to hitec_link_step(), each outcome to
 * hitec_session_feed(), a WAIT waited out. */
static hitec_ses_t drive(hitec_session_t *s, unsigned max_ms)
{
    bool out = false;

    for (unsigned i = 0; i < max_ms; ++i) {
        hitec_link_outcome_t o;

        if (out && hitec_link_outcome(&g_l, &o)) {
            out = false;
            (void)hitec_session_feed(s, g_ms, &o.ses);
        }
        if (!out) {
            hitec_ses_step_t st;
            const hitec_ses_kind_t k = hitec_session_next(s, g_ms, &st);

            if (k == HITEC_SES_STEP_DONE) {
                return hitec_session_result(s);
            }
            if (k != HITEC_SES_STEP_WAIT) {
                CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, ++g_id),
                         HITEC_LINK_START_OK);
                out = true;
                if (++g_steps == g_at_step) {
                    at_step(s);
                }
            }
        }
        ms();
    }
    CHECK(false);
    return HITEC_SES_BUSY;
}

/* Up, port CH open for @p series, the session ready. */
static void session(hitec_session_t *s, uint8_t series)
{
    opened(series);
    CHECK(hitec_session_init(s, series, HITEC_SES_CLOCK_MS));
    g_steps = 0;
    g_at_step = 0;
    g_at = AT_NOTHING;
}

/* A D series servo, a D485HW: model 34485, ID 0. */
static void d485hw(void)
{
    g_drv.dreg[HITEC_D_REG_MODEL] = 34485u;
    g_drv.dreg[0x32] = 0u;
}

/* The classic servo's entry: its `+` 59ms after the ON, as measured
 * through the IOmcu, and its version 0x16. */
static void hs5645mg(void)
{
    g_drv.c_entry = true;
    g_drv.boot_byte = HITEC_C_ACK;
    g_drv.boot_ns = 59000000u;
    g_drv.version = 0x16u;
}

/* ===================================================================== cases */

TEST_CASE(a_d_series_session_enters_reads_and_leaves_through_the_link)
{
    static const uint8_t one[1] = { 0x32u };
    uint8_t frame[HITEC_D_WRITE_LEN];
    hitec_session_t s;
    size_t n = 0;
    const uint8_t *list = hitec_d_read_list(&n);
    uint32_t t0;

    session(&s, HITEC_D);
    d485hw();
    t0 = g_ms;
    CHECK_EQ(hitec_session_enter(&s, true, 0u), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 5000u), HITEC_SES_OK);
    CHECK(s.in_mode);
    CHECK_EQ(hitec_image_reg(&s.image, HITEC_D_REG_MODEL), 34485u);
    /* POWER, the 1000ms wait, the model's read. */
    CHECK_EQ(g_steps, 2u);
    CHECK_EQ(g_hw.ons, 1u);
    CHECK(g_ms - t0 >= HITEC_SES_D_ENTRY_WAIT_MS);
    CHECK(g_ms - t0 <= HITEC_SES_D_ENTRY_WAIT_MS + 200u);
    /* One register. */
    CHECK_EQ(hitec_session_read_set(&s, one, 1u), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 1000u), HITEC_SES_OK);
    CHECK_EQ(hitec_image_reg(&s.image, 0x32u), 0u);
    /* The full set: 21 registers, 20ms apart, in 1s. */
    t0 = g_ms;
    g_steps = 0;
    CHECK_EQ(hitec_session_read_all(&s), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 5000u), HITEC_SES_OK);
    CHECK_EQ(g_steps, n);
    for (size_t i = 0; i < n; ++i) {
        CHECK(hitec_image_known(&s.image, list[i]));
        CHECK_EQ(hitec_image_reg(&s.image, list[i]), g_drv.dreg[list[i]]);
    }
    CHECK(g_ms - t0 >= 800u && g_ms - t0 <= 880u);
    CHECK_EQ(readout().count, 1u + 1u + 1u + n);
    /* The leave: the reboot write, no answer. */
    g_steps = 0;
    CHECK_EQ(hitec_session_leave(&s), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 2000u), HITEC_SES_OK);
    CHECK(!s.in_mode);
    CHECK_EQ(g_steps, 1u);
    CHECK_EQ(g_drv.x.n_tx, HITEC_D_WRITE_LEN);
    hitec_d_write_frame(0u, HITEC_D_REG_REBOOT, 1u, frame);
    CHECK_EQ(memcmp(g_drv.x.tx, frame, HITEC_D_WRITE_LEN), 0);
    CHECK_EQ(run_bare(LINK_HITEC_OP_CLOSE, CH).kind, HITEC_LINK_OUT_DONE);
    CHECK(!readout().held);
}

TEST_CASE(a_classic_session_enters_reads_and_leaves_through_the_link)
{
    static const uint8_t one[1] = { 0x12u };
    hitec_session_t s;
    uint32_t t0;

    session(&s, HITEC_CLASSIC);
    hs5645mg();
    t0 = g_ms;
    CHECK_EQ(hitec_session_enter(&s, false, HITEC_SES_OFF_MS),
             HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 5000u), HITEC_SES_OK);
    CHECK(s.in_mode);
    CHECK(s.image.has_version);
    CHECK_EQ(s.image.version, 0x16u);
    /* POWER with the answer, the LISTEN for the next `+`, 0x67. */
    CHECK_EQ(g_steps, 3u);
    CHECK_EQ(g_hw.ons, 1u);
    /* 500ms off, the `+` at 59ms, the next 250ms after it. */
    CHECK(g_ms - t0 >= HITEC_SES_OFF_MS + 59u + HITEC_SIM_NEXT_US / 1000u);
    CHECK(g_ms - t0 <= HITEC_SES_OFF_MS + 59u + HITEC_SIM_NEXT_US / 1000u
                           + 200u);
    CHECK_EQ(hitec_session_read_set(&s, one, 1u), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 1000u), HITEC_SES_OK);
    CHECK(hitec_image_known(&s.image, 0x12u));
    CHECK_EQ(s.image.mem[0x12], g_drv.eeprom[0x12]);
    /* The leave: a supply cycle with no answer. */
    CHECK_EQ(hitec_session_leave(&s), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 2000u), HITEC_SES_OK);
    CHECK(!s.in_mode);
    CHECK_EQ(g_hw.ons, 2u);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_OPEN);
}

TEST_CASE(a_classic_servo_that_does_not_answer_ends_the_entry_no_servo)
{
    hitec_session_t s;

    session(&s, HITEC_CLASSIC);
    hs5645mg();
    /* Its `+` after the 2s window. */
    g_drv.boot_ns = 2500000000u;
    CHECK_EQ(hitec_session_enter(&s, false, HITEC_SES_OFF_MS),
             HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 5000u), HITEC_SES_ERR_NO_SERVO);
    CHECK(!s.in_mode);
    CHECK_EQ(g_steps, 1u);
    CHECK_EQ(s.last_rx, HITEC_RX_ERR_SILENT);
}

TEST_CASE(an_abort_in_the_middle_of_a_read_ends_it_aborted)
{
    hitec_session_t s;

    session(&s, HITEC_D);
    d485hw();
    CHECK_EQ(hitec_session_enter(&s, false, 0u), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 3000u), HITEC_SES_OK);
    g_steps = 0;
    g_at_step = 5u;
    g_at = AT_ABORT;
    CHECK_EQ(hitec_session_read_all(&s), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 3000u), HITEC_SES_ERR_ABORTED);
    CHECK_EQ(g_steps, 5u);
    run_for(20u);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 1u);
    /* The port stays open: the next read runs. */
    CHECK_EQ(hitec_session_read_all(&s), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 3000u), HITEC_SES_OK);
}

TEST_CASE(a_stop_in_the_middle_of_a_read_ends_it_cut_until_clear)
{
    hitec_session_t s;

    session(&s, HITEC_D);
    d485hw();
    CHECK_EQ(hitec_session_enter(&s, false, 0u), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 3000u), HITEC_SES_OK);
    g_steps = 0;
    g_at_step = 3u;
    g_at = AT_STOP;
    CHECK_EQ(hitec_session_read_all(&s), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 3000u), HITEC_SES_ERR_CUT);
    CHECK(readout().stopped);
    /* Refused until CLEAR. */
    CHECK_EQ(hitec_session_read_all(&s), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 3000u), HITEC_SES_ERR_LINK);
    CHECK_EQ(run_bare(LINK_HITEC_OP_CLEAR, CH).kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(hitec_session_read_all(&s), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 3000u), HITEC_SES_OK);
}

TEST_CASE(a_lost_link_in_the_middle_of_a_read_ends_it_err_link)
{
    hitec_session_t s;

    session(&s, HITEC_D);
    d485hw();
    CHECK_EQ(hitec_session_enter(&s, false, 0u), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 3000u), HITEC_SES_OK);
    g_steps = 0;
    g_at_step = 4u;
    g_at = AT_LOSE;
    CHECK_EQ(hitec_session_read_all(&s), HITEC_SES_BUSY);
    CHECK_EQ(drive(&s, 3000u), HITEC_SES_ERR_LINK);
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_DOWN);
    /* Back: the port is held by no session of this module. */
    link_up();
    CHECK(readout().held);
    CHECK(!readout().ours);
}

int main(void)
{
    RUN(a_d_series_session_enters_reads_and_leaves_through_the_link);
    RUN(a_classic_session_enters_reads_and_leaves_through_the_link);
    RUN(a_classic_servo_that_does_not_answer_ends_the_entry_no_servo);
    RUN(an_abort_in_the_middle_of_a_read_ends_it_aborted);
    RUN(a_stop_in_the_middle_of_a_read_ends_it_cut_until_clear);
    RUN(a_lost_link_in_the_middle_of_a_read_ends_it_err_link);
    return test_summary("hitec_link_run");
}
