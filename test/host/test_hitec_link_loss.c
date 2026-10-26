/*
 * The panel's half of the HITEC page (shared/bench/hitec_link.c) when the
 * link fails under it: the link lost at 5 points of a request, the port
 * held over a silence and released by CLOSE, a port another writer opened
 * or commanded, another writer's command at the SEQ a lost try left free
 * (a step, CLOSE and CLEAR), an IOmcu that starts again (idle, under a request, under
 * a request and a command at SEQ 0), a command at SEQ 0 refused with its
 * answers lost, SEQ across 65535, the 25,000ms limit and the
 * poll periods, both across the wrap of the millisecond clock.  The rig is
 * hitec_link_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_link_rig.h"

/* ===================================================================== cases */

/* Where the request is when the link goes. */
static bool lost_here(unsigned i)
{
    switch (i) {
    case 0:  /* 2 of the 5 staged windows out */
        return g_l.stage_at == 2u;
    case 1:  /* the command went, its answer lost, owed again */
        return g_l.cmd_live != 0u && g_l.cmd_tries == 1u
               && g_l.pending == 0u && g_l.tries == 1u;
    case 2:  /* the command taken, its verdict owed */
        return g_l.verdict != 0u;
    case 3:
        return g_l.phase == (uint8_t)HITEC_LINK_RUNNING;
    default:
        return g_l.phase == (uint8_t)HITEC_LINK_FETCHING;
    }
}

TEST_CASE(a_lost_link_ends_the_request_at_each_phase)
{
    const hitec_ses_step_t st = plus_listen(200000u);
    const hitec_ses_step_t rd = c_read_step(0x12u);
    hitec_link_outcome_t out;

    for (unsigned i = 0; i < 5u; ++i) {
        opened(HITEC_CLASSIC);
        g_lose_cmd_reply = i == 1u ? 1u : 0u;
        CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, ++g_id),
                 HITEC_LINK_START_OK);
        for (unsigned k = 0; k < 2000u && !lost_here(i); ++k) {
            tick_as(true, false);
            one_exchange();
        }
        CHECK(lost_here(i));
        /* A STOP just before, once the page has the command: its ABORT
         * goes with the link. */
        if (i >= 2u) {
            hitec_link_abort(&g_l);
        }
        if (i == 3u) {
            /* And the ABORT handed out, with no answer yet. */
            hitec_link_xfer_t x;

            CHECK(hitec_link_next(&g_l, g_ms, &x));
            CHECK(x.write && x.off == LINK_HT_W_CMD);
            CHECK_EQ(x.regs[LINK_HT_W_CMD] & 0xFu, LINK_HITEC_OP_ABORT);
        }
        memset(g_op_cmds, 0, sizeof(g_op_cmds));
        hitec_link_lost(&g_l);
        CHECK(hitec_link_outcome(&g_l, &out));
        CHECK_EQ(out.kind, HITEC_LINK_OUT_NO_ANSWER);
        CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
        CHECK_EQ(out.taken, i >= 2u);
        CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_DOWN);
        CHECK(!readout().known);
        CHECK_EQ(readout().minor, 0u);
        g_sent = 0;
        run_for(500u);
        CHECK_EQ(g_sent, 0u);
        /* Back: the port is held, and no longer this module's. */
        link_up();
        CHECK(readout().held);
        CHECK(!readout().ours);
        CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_OPEN);
        CHECK_EQ(g_drv.starts, i >= 1u ? 1u : 0u);
        run_for(300u);
        CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 0u);
        /* The next request goes once and runs. */
        CHECK_EQ(run_step(&rd).ses.run, HITEC_SES_RAN);
        CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], 1u);
    }
}

TEST_CASE(after_a_silence_the_held_port_is_released_by_close)
{
    hitec_link_outcome_t out;

    opened(HITEC_D);
    hitec_link_lost(&g_l);
    /* The IOmcu finds the link silent: every pass a stop. */
    g_safe = false;
    run_for(300u);
    link_up();
    CHECK(readout().held);
    CHECK(readout().stopped);
    CHECK(!readout().ours);
    /* The failsafe holds: CLEAR is refused, CLOSE is taken. */
    CHECK_EQ(run_bare(LINK_HITEC_OP_CLEAR, CH).refusal,
             LINK_HITEC_REF_UNSAFE);
    out = run_bare(LINK_HITEC_OP_CLOSE, CH);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
    CHECK(!readout().held);
    g_safe = true;
}

TEST_CASE(a_port_another_writer_opened_is_not_ours_and_closes)
{
    hitec_link_outcome_t out;

    up();
    g_port.staged[0] = LINK_HT_LINE_OF(HITEC_CLASSIC, 0u);
    CHECK_EQ(other_cmd(LINK_HITEC_OP_OPEN, CH, 0u), 0u);
    run_for(HITEC_LINK_IDLE_MS + 100u);
    CHECK(readout().held);
    CHECK_EQ(readout().state, LINK_HITEC_OPEN);
    CHECK(!readout().ours);
    CHECK(!readout().have_line);
    out = run_bare(LINK_HITEC_OP_CLOSE, CH);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
    /* Another writer's command on this module's port: not ours any more. */
    opened(HITEC_CLASSIC);
    CHECK(readout().ours);
    CHECK_EQ(other_cmd(LINK_HITEC_OP_CLEAR, CH, 0u), 0u);
    run_for(HITEC_LINK_HELD_MS + 10u);
    CHECK(readout().held);
    CHECK(!readout().ours);
}

TEST_CASE(a_restarted_iomcu_ends_the_request_and_frees_the_port)
{
    const hitec_ses_step_t st = plus_listen(200000u);
    hitec_link_outcome_t out;
    hitec_port_hw_t hw;

    opened(HITEC_CLASSIC);
    hw = g_port.hw;
    start_until(&st, HITEC_LINK_RUNNING);
    CHECK(hitec_port_init(&g_port, &hw));
    out = outcome();
    CHECK_EQ(out.kind, HITEC_LINK_OUT_RESTARTED);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
    CHECK(!readout().held);
    CHECK(!readout().ours);
    CHECK_EQ(readout().seq, 0u);
    /* Idle, the same. */
    opened(HITEC_CLASSIC);
    hw = g_port.hw;
    CHECK(hitec_port_init(&g_port, &hw));
    run_for(HITEC_LINK_HELD_MS + 10u);
    CHECK(!readout().held);
    CHECK(!readout().ours);
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_READY);
}

/* A try lost, another writer's command at the SEQ it left free, the next
 * try refused: the SEQ matches, the page's state does not.  After an OPEN
 * and after a read of the same kind. */
TEST_CASE(another_writers_command_at_the_seq_of_a_lost_try_is_not_this_one)
{
    const hitec_ses_step_t st = c_read_step(0x12u);

    for (unsigned k = 0; k < 2u; ++k) {
        hitec_link_outcome_t out;
        unsigned sends;

        opened(HITEC_CLASSIC);
        if (k == 1u) {
            CHECK_EQ(run_step(&st).ses.run, HITEC_SES_RAN);
        }
        sends = g_op_cmds[LINK_HITEC_OP_SEND];
        g_other_cmd = 1u;
        g_lose_cmd = 1u;
        out = run_step(&st);
        CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND] - sends, 2u);
        CHECK_EQ(out.kind, HITEC_LINK_OUT_RESTARTED);
        CHECK(!out.taken);
        CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
        CHECK_EQ(g_drv.starts, k);
        CHECK_EQ(run_step(&st).ses.run, HITEC_SES_RAN);
    }
}

/* The same for CLOSE, the port still held, and for CLEAR, the port still
 * stopped after another writer's ABORT. */
TEST_CASE(another_writers_command_is_no_close_or_clear_of_this_one)
{
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    g_other_cmd = 1u;
    g_lose_cmd = 1u;
    out = run_bare(LINK_HITEC_OP_CLOSE, CH);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_CLOSE], 2u);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_RESTARTED);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_OPEN);
    CHECK_EQ(run_bare(LINK_HITEC_OP_CLOSE, CH).kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);

    opened(HITEC_CLASSIC);
    stop_pulse();
    g_other_op = LINK_HITEC_OP_ABORT;
    g_other_cmd = 1u;
    g_lose_cmd = 1u;
    out = run_bare(LINK_HITEC_OP_CLEAR, CH);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_CLEAR], 2u);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_RESTARTED);
    CHECK(g_port.stopped != 0u);
    CHECK_EQ(run_bare(LINK_HITEC_OP_CLEAR, CH).kind, HITEC_LINK_OUT_DONE);
    CHECK(g_port.stopped == 0u);
}

/* SEQ from 65533: OPEN at 65534, steps at 65535, 0 and 1. */
TEST_CASE(the_seq_wraps_under_the_module)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;

    bench_at(0u, SUPPLY_ON, 1u);
    g_port.seq = 65533u;
    link_up();
    CHECK_EQ(readout().seq, 65533u);
    CHECK_EQ(open_as(CH, HITEC_CLASSIC, 0u).result, LINK_HITEC_RESULT_DONE);
    CHECK_EQ(g_last_cmd[LINK_HT_W_SEQ], 65534u);
    CHECK_EQ(run_step(&st).ses.run, HITEC_SES_RAN);
    CHECK_EQ(g_last_cmd[LINK_HT_W_SEQ], 65535u);
    /* At SEQ 0 with its answers lost: the page took it. */
    g_lose_cmd_reply = HITEC_LINK_RETRIES;
    out = run_step(&st);
    CHECK_EQ(g_last_cmd[LINK_HT_W_SEQ], 0u);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    CHECK_EQ(run_step(&st).ses.run, HITEC_SES_RAN);
    CHECK_EQ(g_last_cmd[LINK_HT_W_SEQ], 1u);
    CHECK_EQ(readout().seq, 1u);
    CHECK_EQ(g_drv.starts, 3u);
}

/* A restart under a command at SEQ 0 whose answers were lost: a fresh page
 * at SEQ 0, operation NONE, is no page that took it. */
TEST_CASE(a_restart_under_a_command_at_seq_0_is_no_taken_command)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;
    hitec_port_hw_t hw;

    bench_at(0u, SUPPLY_ON, 1u);
    g_port.seq = 65534u;
    link_up();
    CHECK_EQ(open_as(CH, HITEC_CLASSIC, 0u).result, LINK_HITEC_RESULT_DONE);
    CHECK_EQ(g_l.seq, 65535u);
    /* The IOmcu starts again; the next command, SEQ 0, is lost thrice. */
    hw = g_port.hw;
    CHECK(hitec_port_init(&g_port, &hw));
    g_lose_cmd = HITEC_LINK_RETRIES;
    out = run_step(&st);
    CHECK_EQ(g_last_cmd[LINK_HT_W_SEQ], 0u);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_RESTARTED);
    CHECK(!out.taken);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
    CHECK_EQ(g_drv.starts, 0u);
    CHECK(!readout().held);
}

/* A command at SEQ 0 that the page refuses, its answers lost, on a page
 * that has started no operation: the refusal says it was taken. */
TEST_CASE(a_command_at_seq_0_refused_with_its_answers_lost_ends_refused)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;

    bench_at(0u, SUPPLY_ON, 1u);
    g_port.seq = 65535u;
    link_up();
    g_lose_cmd_reply = HITEC_LINK_RETRIES;
    out = run_step(&st);
    CHECK_EQ(g_last_cmd[LINK_HT_W_SEQ], 0u);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_REFUSED);
    CHECK_EQ(out.refusal, LINK_HITEC_REF_NOT_OPEN);
    CHECK(out.taken);
}

/* A restart under a request at SEQ 0: the page at SEQ 0 again, its
 * operation back at NONE. */
TEST_CASE(a_restart_under_a_request_at_seq_0_ends_it_restarted)
{
    const hitec_ses_step_t st = plus_listen(200000u);
    hitec_link_outcome_t out;
    hitec_port_hw_t hw;

    bench_at(0u, SUPPLY_ON, 1u);
    g_port.seq = 65534u;
    link_up();
    CHECK_EQ(open_as(CH, HITEC_CLASSIC, 0u).result, LINK_HITEC_RESULT_DONE);
    start_until(&st, HITEC_LINK_RUNNING);
    CHECK_EQ(g_l.seq, 0u);
    hw = g_port.hw;
    CHECK(hitec_port_init(&g_port, &hw));
    out = outcome();
    CHECK_EQ(out.kind, HITEC_LINK_OUT_RESTARTED);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
    CHECK(!readout().ours);
}

/* A page that says BUSY for ever: the ABORT goes 25,000ms after the
 * command was taken, not before. */
static void a_step_running_25s_is_aborted_at(uint32_t t0)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;
    uint32_t taken;

    up_at(t0, SUPPLY_ON);
    CHECK_EQ(open_as(CH, HITEC_CLASSIC, 0u).result, LINK_HITEC_RESULT_DONE);
    g_fake_busy = true;
    start_until(&st, HITEC_LINK_RUNNING);
    taken = g_l.taken_ms;
    while ((uint32_t)(g_ms - taken) < HITEC_LINK_RUN_MAX_MS - 1u) {
        ms();
    }
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 0u);
    /* An exchange asked 1ms short of the limit is no ABORT. */
    {
        hitec_link_xfer_t x;

        if (hitec_link_next(&g_l, taken + HITEC_LINK_RUN_MAX_MS - 1u, &x)) {
            uint16_t regs[LINK_HT_COUNT] = { 0 };
            const int r = xchg(NULL, &x, x.write ? NULL : regs);

            hitec_link_done(&g_l, r, x.write ? NULL : regs);
        }
        CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 0u);
    }
    /* At the first pass from 25,000ms on. */
    while (g_op_cmds[LINK_HITEC_OP_ABORT] == 0u
           && (uint32_t)(g_ms - taken) < HITEC_LINK_RUN_MAX_MS + 100u) {
        ms();
    }
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 1u);
    CHECK((uint32_t)(g_ms - taken) < HITEC_LINK_RUN_MAX_MS
                                         + HITEC_LINK_POLL_MS);
    g_fake_busy = false;
    out = outcome();
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    run_for(1000u);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 1u);
}

TEST_CASE(a_step_still_running_after_25_s_is_aborted)
{
    at_tick_0_and_before_the_wrap(a_step_running_25s_is_aborted_at, 10000u);
}

/* The page is read every 500ms while it holds no port, every 100ms while
 * it holds one, every 5ms while a request runs. */
static void the_polls_keep_their_periods_at(uint32_t t0)
{
    const hitec_ses_step_t st = listen_step(HITEC_CLASSIC, 300000u, 1u,
                                            false);
    unsigned sent;

    up_at(t0, SUPPLY_ON);
    sent = g_sent;
    run_for(10u * HITEC_LINK_IDLE_MS);
    CHECK(g_sent - sent >= 9u && g_sent - sent <= 11u);
    CHECK_EQ(open_as(CH, HITEC_CLASSIC, 0u).result, LINK_HITEC_RESULT_DONE);
    sent = g_sent;
    run_for(10u * HITEC_LINK_HELD_MS);
    CHECK(g_sent - sent >= 9u && g_sent - sent <= 11u);
    start_until(&st, HITEC_LINK_RUNNING);
    sent = g_sent;
    run_for(20u * HITEC_LINK_POLL_MS);
    CHECK(g_sent - sent >= 19u && g_sent - sent <= 21u);
    CHECK_EQ(outcome().ses.run, HITEC_SES_RAN);
}

TEST_CASE(the_polls_keep_their_periods_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(the_polls_keep_their_periods_at, 1200u);
}

int main(void)
{
    RUN(a_lost_link_ends_the_request_at_each_phase);
    RUN(after_a_silence_the_held_port_is_released_by_close);
    RUN(a_port_another_writer_opened_is_not_ours_and_closes);
    RUN(a_restarted_iomcu_ends_the_request_and_frees_the_port);
    RUN(another_writers_command_at_the_seq_of_a_lost_try_is_not_this_one);
    RUN(another_writers_command_is_no_close_or_clear_of_this_one);
    RUN(the_seq_wraps_under_the_module);
    RUN(a_restart_under_a_command_at_seq_0_is_no_taken_command);
    RUN(a_command_at_seq_0_refused_with_its_answers_lost_ends_refused);
    RUN(a_restart_under_a_request_at_seq_0_ends_it_restarted);
    RUN(a_step_still_running_after_25_s_is_aborted);
    RUN(the_polls_keep_their_periods_across_the_tick_wrap);
    return test_summary("hitec_link_loss");
}
