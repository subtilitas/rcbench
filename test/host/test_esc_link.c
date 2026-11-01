/*
 * The panel's half of the ESC_WIRE pages: what it sends to whom, and when.
 *
 * Under test: an IOmcu below 4.34 is sent nothing; a CLOSE after a panel restart
 * and only then, with its retries and with NOT_BACK; a link that came back closes
 * nothing; a stop is the STOP operation and drops what has not gone; a command
 * unanswered goes again as it was and the page takes it once; errors counted on
 * their edge.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_link_rig.h"

TEST_CASE(an_older_iomcu_is_sent_nothing)
{
    esc_link_xfer_t x;
    esc_link_outcome_t o;
    esc_link_readout_t r;

    up_am_rig();
    esc_link_init(&g_l);
    CHECK(!esc_link_next(&g_l, 0u, &x));
    CHECK_EQ(esc_link_open(&g_l, 2u, 1u), ESC_LINK_START_DOWN);
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_DOWN);
    esc_link_came_up(&g_l, ESC_LINK_MINOR - 1u);
    CHECK(!esc_link_has_page(&g_l));
    CHECK(!esc_link_next(&g_l, 0u, &x));
    CHECK_EQ(esc_link_open(&g_l, 2u, 1u), ESC_LINK_START_NO_PAGE);
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_NO_PAGE);
    /* NULLs. */
    esc_link_init(NULL);
    esc_link_lost(NULL);
    esc_link_came_up(NULL, 34u);
    esc_link_stop(NULL);
    esc_link_abort(NULL);
    esc_link_done(NULL, 0, NULL);
    esc_link_readout(NULL, &r);
    CHECK(!esc_link_has_page(NULL));
    CHECK(!esc_link_next(NULL, 0u, &x));
    CHECK(!esc_link_outcome(NULL, &o));
    CHECK_EQ(esc_link_phase(NULL), ESC_LINK_DOWN);
    CHECK_EQ(esc_link_open(NULL, 2u, 1u), ESC_LINK_START_ARG);
    CHECK_EQ(esc_link_enter(&g_l, NULL, 1u), ESC_LINK_START_ARG);
    CHECK_EQ(esc_link_write(&g_l, NULL, 1u, 0u, 0u, 1u), ESC_LINK_START_ARG);
    CHECK_EQ(esc_link_restore(&g_l, esc.am, 1u, NULL, 0u, 0u, 1u),
             ESC_LINK_START_ARG);
    CHECK(esc_link_image(NULL) == NULL);
    CHECK_EQ(esc_link_faults(NULL), 0u);
    CHECK_EQ(esc_link_service(&g_l, 0u, NULL, NULL), 0u);
}

TEST_CASE(a_start_on_a_free_page_sends_no_command)
{
    up_am_rig();
    link_up();
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_READY);
    CHECK_EQ(g_commands, 0);
    CHECK_EQ(reg(LINK_EW_SEQ), 0u);
    /* A free page is read every 500ms, a held one every 100ms. */
    const int before = g_exchanges;

    link_run(1000u);
    CHECK_EQ(g_exchanges - before, 2);
    link_ready(2u);
    const int held = g_exchanges;

    link_run(1000u);
    CHECK_EQ(g_exchanges - held, 10);
}

TEST_CASE(a_restart_closes_what_the_first_read_finds)
{
    esc_link_outcome_t o;
    esc_link_readout_t r;

    up_am_rig();
    open_target(2u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_OPEN);
    CHECK_EQ(g.gives, 0);
    /* The panel started: a request waits for the CLOSE. */
    esc_link_init(&g_l);
    esc_link_came_up(&g_l, ESC_LINK_MINOR);
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_SYNC);
    CHECK_EQ(esc_link_open(&g_l, 2u, 1u), ESC_LINK_START_BUSY);
    link_run(20u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    CHECK_EQ(g.gives, 1);
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_CLOSE);
    /* Its own: no outcome for a model that asked nothing. */
    CHECK(!esc_link_outcome(&g_l, &o));
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_READY);
    esc_link_readout(&g_l, &r);
    CHECK(!r.held);
    /* Once. */
    const int cmds = g_commands;

    link_run(500u);
    CHECK_EQ(g_commands, cmds);
}

TEST_CASE(a_restart_with_a_pin_that_does_not_come_back)
{
    uint16_t target = 0u;

    up_am_rig();
    open_target(2u);
    g.give_refused = true;
    esc_link_init(&g_l);
    esc_link_came_up(&g_l, ESC_LINK_MINOR);
    g_commands = 0;
    link_run(20u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_NOT_BACK);
    CHECK(esc_link_not_back(&g_l, &target));
    CHECK_EQ(target, 2u);
    CHECK(!esc_link_not_back(&g_l, &target));
    CHECK_EQ(esc_link_faults(&g_l), 1u);
    CHECK_EQ(g_commands, 1);
    /* Again, 1000ms on. */
    link_run(900u);
    CHECK_EQ(g_commands, 1);
    link_run(200u);
    CHECK_EQ(g_commands, 2);
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_SYNC);
    /* And the output takes it. */
    g.give_refused = false;
    link_run(1100u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_READY);
    /* NOT_BACK named once per arrival, and its count stands. */
    CHECK(!esc_link_not_back(&g_l, &target));
    CHECK_EQ(esc_link_faults(&g_l), 1u);
    CHECK(!esc_link_not_back(NULL, &target));
    CHECK(!esc_link_not_back(&g_l, NULL));
}

TEST_CASE(a_port_that_does_not_come_back_names_the_port)
{
    uint16_t target = 0u;

    up_am_rig();
    open_target(LINK_EW_TARGET_PORT);
    g.give_refused = true;
    esc_link_init(&g_l);
    esc_link_came_up(&g_l, ESC_LINK_MINOR);
    link_run(20u);
    CHECK(esc_link_not_back(&g_l, &target));
    CHECK_EQ(target, LINK_EW_TARGET_PORT);
}

TEST_CASE(a_link_that_came_back_closes_nothing)
{
    esc_link_readout_t r;

    up_am_rig();
    link_up();
    link_ready(2u);
    esc_link_readout(&g_l, &r);
    CHECK(r.ours);
    esc_link_lost(&g_l);
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_DOWN);
    esc_link_came_up(&g_l, ESC_LINK_MINOR);
    const int cmds = g_commands;

    link_run(100u);
    CHECK_EQ(g_commands, cmds);
    CHECK_EQ(g.gives, 0);
    esc_link_readout(&g_l, &r);
    CHECK(r.held);
    CHECK(!r.ours);
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_READY);
}

TEST_CASE(a_stop_is_the_stop_operation)
{
    esc_link_outcome_t o;

    up_am_rig();
    link_up();
    link_ready(2u);
    const unsigned seq = reg(LINK_EW_SEQ);

    esc_link_stop(&g_l);
    link_run(5u);
    CHECK_EQ(reg(LINK_EW_SEQ), seq + 1u);
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_STOP);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_OPEN);
    CHECK(flag(LINK_EW_F_STOPPED));
    /* No fault, and no outcome: no request ran. */
    CHECK_EQ(esc_link_faults(&g_l), 0u);
    CHECK(!esc_link_outcome(&g_l, &o));
    /* On a free page nothing goes. */
    CHECK_EQ(esc_link_close(&g_l, 9u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 100u));
    const int cmds = g_commands;

    esc_link_stop(&g_l);
    link_run(600u);
    CHECK_EQ(g_commands, cmds);
    /* ABORT stands over a STOP owed. */
    link_ready(2u);
    esc_link_stop(&g_l);
    esc_link_abort(&g_l);
    link_run(5u);
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_ABORT);
}

TEST_CASE(a_stop_drops_a_request_that_has_not_gone)
{
    esc_link_outcome_t o;

    up_am_rig();
    link_up();
    CHECK_EQ(esc_link_open(&g_l, 2u, 7u), ESC_LINK_START_OK);
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_SENDING);
    esc_link_stop(&g_l);
    CHECK(esc_link_outcome(&g_l, &o));
    CHECK_EQ(o.kind, ESC_LINK_OUT_DROPPED);
    CHECK_EQ(o.id, 7u);
    CHECK(!o.taken);
    link_run(20u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    CHECK_EQ(reg(LINK_EW_SEQ), 0u);
}

TEST_CASE(a_stop_after_another_writers_seq_goes_again)
{
    up_am_rig();
    link_up();
    link_ready(2u);
    esc_link_stop(&g_l);
    /* The console takes a SEQ between the panel's read and its command. */
    (void)cmd(LINK_ESC_OP_READ);
    run_while(LINK_ESC_RUN, 20000u);
    link_run(10u);
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_STOP);
    CHECK(flag(LINK_EW_F_STOPPED));
}

TEST_CASE(an_answer_lost_is_a_command_taken_once)
{
    esc_link_outcome_t o;

    up_am_rig();
    link_up();
    /* The answer lost, twice: the same frame, and the page's SEQ moves once. */
    g_drop_cmd = 2;
    CHECK_EQ(esc_link_open(&g_l, 2u, 3u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_DONE);
    CHECK(o.taken);
    CHECK_EQ(reg(LINK_EW_SEQ), 1u);
    CHECK_EQ(g_commands, 3);
    /* All 3 answers lost: a read says it was taken. */
    g_drop_cmd = 3;
    g_commands = 0;
    CHECK_EQ(esc_link_close(&g_l, 4u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_DONE);
    CHECK_EQ(g_commands, 3);
    CHECK_EQ(reg(LINK_EW_SEQ), 2u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
}

TEST_CASE(no_answer_at_all_is_said_as_that)
{
    esc_link_outcome_t o;

    up_am_rig();
    link_up();
    /* In a staged frame. */
    g_drop = 3;
    CHECK_EQ(esc_link_open(&g_l, 2u, 3u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_NO_ANSWER);
    CHECK(!o.taken);
    /* In the command: 3 unanswered, and a read shows the page has not moved. */
    g_drop = 0;
    CHECK_EQ(esc_link_clear(&g_l, 4u), ESC_LINK_START_OK);
    g_drop = 3;
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_NO_ANSWER);
    CHECK(!o.taken);
    CHECK_EQ(reg(LINK_EW_SEQ), 0u);
    /* The link gone with a request on its way. */
    CHECK_EQ(esc_link_read(&g_l, 5u), ESC_LINK_START_OK);
    esc_link_lost(&g_l);
    CHECK(esc_link_outcome(&g_l, &o));
    CHECK_EQ(o.kind, ESC_LINK_OUT_NO_ANSWER);
    CHECK_EQ(o.id, 5u);
}

TEST_CASE(a_frame_refused_is_said_with_its_reason)
{
    esc_link_outcome_t o;
    esc_link_xfer_t x;

    up_am_rig();
    link_up();
    /* The page answers BAD_VALUE: another writer's SEQ. */
    CHECK_EQ(esc_link_clear(&g_l, 4u), ESC_LINK_START_OK);
    CHECK(esc_link_next(&g_l, 1000u, &x));
    CHECK_EQ(x.off, 0u);
    esc_link_done(&g_l, LINK_NACK_BAD_VALUE, NULL);
    CHECK(esc_link_outcome(&g_l, &o));
    CHECK_EQ(o.kind, ESC_LINK_OUT_NACK);
    CHECK_EQ(o.nack, LINK_NACK_BAD_VALUE);
    /* In a staged frame. */
    link_run(5u);
    CHECK_EQ(esc_link_open(&g_l, 2u, 6u), ESC_LINK_START_OK);
    CHECK(esc_link_next(&g_l, 2000u, &x));
    CHECK_EQ(x.off, LINK_EW_W_TARGET);
    esc_link_done(&g_l, LINK_NACK_BUSY, NULL);
    CHECK(esc_link_outcome(&g_l, &o));
    CHECK_EQ(o.kind, ESC_LINK_OUT_NACK);
    CHECK_EQ(o.nack, LINK_NACK_BUSY);
    CHECK(!esc_link_outcome(&g_l, &o));
}

TEST_CASE(a_page_that_moved_under_a_request_is_said)
{
    esc_link_outcome_t o;

    up_am_rig();
    link_up();
    link_ready(2u);
    CHECK_EQ(esc_link_read(&g_l, 5u), ESC_LINK_START_OK);
    link_run(5u);
    /* An IOmcu that started again: its SEQ is 0. */
    rig_up();
    CHECK(link_wait(&o, 100u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_RESTARTED);
    CHECK(o.taken);
}

TEST_CASE(a_refusal_is_said_as_the_page_says_it)
{
    esc_link_outcome_t o;

    up_am_rig();
    link_up();
    CHECK_EQ(esc_link_read(&g_l, 5u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 100u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_REFUSED);
    CHECK_EQ(o.refusal, LINK_ESC_REF_NOT_OPEN);
    CHECK_EQ(o.id, 5u);
    CHECK(o.taken);
}

TEST_CASE(errors_count_on_their_edge)
{
    esc_link_outcome_t o;

    up_am_rig();
    link_up();
    CHECK_EQ(esc_link_open(&g_l, 2u, 1u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(esc_link_faults(&g_l), 0u);
    sim_script(&esc, F_DEAD, 0, 1, 0u);
    CHECK_EQ(esc_link_enter(&g_l, &k_pw, 2u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 30000u));
    CHECK_EQ(o.result, LINK_ESC_RESULT_SESSION);
    CHECK_EQ(esc_link_faults(&g_l), 1u);
    link_run(500u);
    CHECK_EQ(esc_link_faults(&g_l), 1u);
    /* One that stands over a link that came back is no new error. */
    esc_link_lost(&g_l);
    esc_link_came_up(&g_l, ESC_LINK_MINOR);
    link_run(100u);
    CHECK_EQ(esc_link_faults(&g_l), 1u);
    CHECK(flag(LINK_EW_F_FAULT));
    /* Cleared, and a new one counts. */
    CHECK_EQ(esc_link_clear(&g_l, 3u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 100u));
    CHECK(!flag(LINK_EW_F_FAULT));
    sim_script(&esc, F_DEAD, 0, 1, 0u);
    CHECK_EQ(esc_link_enter(&g_l, &k_pw, 4u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 30000u));
    CHECK_EQ(esc_link_faults(&g_l), 2u);
    /* One that stands at a panel's start: no new error, and the CLOSE frees it. */
    esc_link_init(&g_l);
    esc_link_came_up(&g_l, ESC_LINK_MINOR);
    link_run(20u);
    CHECK_EQ(esc_link_faults(&g_l), 0u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
}

/* A STOP while a command waits for its answer, the page free in the last read: not
 * dropped, and the command not sent again.  A read says whether it was taken. */
TEST_CASE(a_stop_with_a_command_unanswered)
{
    esc_link_outcome_t o;
    esc_link_xfer_t x;

    /* Not taken: nothing ran, and nothing goes. */
    up_am_rig();
    link_up();
    CHECK_EQ(esc_link_open(&g_l, 2u, 1u), ESC_LINK_START_OK);
    for (int i = 0; i < 8; ++i) {
        const uint32_t t = esc.now / 1000u;

        CHECK(esc_link_next(&g_l, t, &x));
        if (x.off == 0u) {
            esc_link_done(&g_l, ESC_LINK_NO_ANSWER, NULL);
            break;
        }
        esc_link_done(&g_l, link_xchg(NULL, &x, NULL), NULL);
    }
    const int cmds = g_commands;

    esc_link_stop(&g_l);
    CHECK(link_wait(&o, 100u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_DROPPED);
    CHECK_EQ(g_commands, cmds);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    /* Taken, the answer lost: the STOP goes to what it took. */
    g_drop_cmd = 1;
    CHECK_EQ(esc_link_open(&g_l, 2u, 2u), ESC_LINK_START_OK);
    link_run(5u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_STOPPING);
    esc_link_stop(&g_l);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_STOP);
    CHECK(o.taken);
    CHECK(esc_port_state(&g_p) != LINK_ESC_OPEN || flag(LINK_EW_F_STOPPED));
}

/* @p n reads of the page answered with @p result, 5ms apart. */
static void reads_fail(unsigned n, int result)
{
    esc_link_xfer_t x;

    for (unsigned i = 0u; i < n; ++i) {
        esc.now += 5000u;
        CHECK(esc_link_next(&g_l, esc.now / 1000u, &x));
        CHECK(!x.write);
        esc_link_done(&g_l, result, NULL);
    }
}

/* Reads of the page that fail, other pages answering, a session held: an error, and
 * the STOP.  It does not run on to READY with the supply on. */
TEST_CASE(failed_reads_over_a_hold_are_an_error_and_a_stop)
{
    esc_link_outcome_t o;

    up_am_rig();
    link_up();
    link_ready(2u);
    CHECK_EQ(esc_link_read(&g_l, 5u), ESC_LINK_START_OK);
    link_run(5u);
    CHECK_EQ(esc_link_faults(&g_l), 0u);
    reads_fail(ESC_LINK_STATUS_FAILS - 1u, LINK_NACK_BUSY);
    CHECK(!esc_link_outcome(&g_l, &o));
    reads_fail(1u, LINK_NACK_BUSY);
    CHECK(esc_link_outcome(&g_l, &o));
    CHECK_EQ(o.kind, ESC_LINK_OUT_NO_ANSWER);
    CHECK_EQ(o.nack, 0u);
    CHECK_EQ(esc_link_faults(&g_l), 1u);
    /* A second request waits for the STOP. */
    CHECK_EQ(esc_link_read(&g_l, 6u), ESC_LINK_START_BUSY);
    link_run(10u);
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_STOP);
    CHECK(flag(LINK_EW_F_STOPPED));
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_OPEN);
    CHECK(!g.v.asked_on);
}

/* A write gets no exception, and the page takes the STOP at its read-back. */
TEST_CASE(failed_reads_over_a_write_are_an_error_and_a_stop)
{
    esc_link_outcome_t o;
    static uint8_t pg[ESC_LINK_PAGE_MAX];
    const esc_link_image_t *im = esc_link_image(&g_l);

    up_am_rig();
    link_up();
    link_ready(2u);
    CHECK_EQ(esc_link_read(&g_l, 5u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 20000u));
    link_run(100u);
    memcpy(pg, im->page, im->len);
    pg[0x18] = 48u;
    const int cmds = g_commands;

    CHECK_EQ(esc_link_write(&g_l, pg, im->len, im->reads, im->read_crc, 6u),
             ESC_LINK_START_OK);
    for (int i = 0; i < 400 && g_commands == cmds; ++i) {
        (void)esc_link_service(&g_l, esc.now / 1000u, link_xchg, NULL);
    }
    CHECK_EQ(g_commands, cmds + 1);
    link_run(5u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_RUN);
    g_drop = 1000000;
    bool got = false;

    for (int ms = 0; ms < 5000 && !got; ms += 5) {
        link_run(5u);
        got = esc_link_outcome(&g_l, &o);
    }
    CHECK(got);
    CHECK_EQ(o.kind, ESC_LINK_OUT_NO_ANSWER);
    CHECK_EQ(o.op, LINK_ESC_OP_WRITE);
    CHECK_EQ(esc_link_faults(&g_l), 1u);
    g_drop = 0;
    link_run(10000u);
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_STOP);
    CHECK(flag(LINK_EW_F_STOPPED));
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_OPEN);
}

/* With no hold in the last read: the request ends, and no error is said. */
TEST_CASE(failed_reads_on_a_free_page_end_the_request)
{
    esc_link_outcome_t o;
    esc_link_xfer_t x;

    up_am_rig();
    link_up();
    /* A CLEAR on a free page, its answer in: the reads fail. */
    CHECK_EQ(esc_link_clear(&g_l, 4u), ESC_LINK_START_OK);
    CHECK(esc_link_next(&g_l, esc.now / 1000u, &x));
    esc_link_done(&g_l, ESC_LINK_ACK, NULL);
    reads_fail(ESC_LINK_STATUS_FAILS, LINK_NACK_BUSY);
    CHECK(esc_link_outcome(&g_l, &o));
    CHECK_EQ(o.kind, ESC_LINK_OUT_NACK);
    CHECK_EQ(o.nack, LINK_NACK_BUSY);
    CHECK_EQ(esc_link_faults(&g_l), 0u);
    link_run(500u);
    /* One read short of the limit, then a good one: the page answers the request. */
    CHECK_EQ(esc_link_clear(&g_l, 5u), ESC_LINK_START_OK);
    CHECK(esc_link_next(&g_l, esc.now / 1000u, &x));
    esc_link_done(&g_l, link_xchg(NULL, &x, NULL), NULL);
    reads_fail(ESC_LINK_STATUS_FAILS - 1u, ESC_LINK_NO_ANSWER);
    CHECK(!esc_link_outcome(&g_l, &o));
    link_run(100u);
    CHECK(esc_link_outcome(&g_l, &o));
    CHECK_EQ(o.kind, ESC_LINK_OUT_REFUSED);
    CHECK_EQ(g_l.status_fails, 0u);
}

/* OPEN taken with its answer lost, then reads that fail, and a stop: FREE in the old
 * read does not decide it.  The page's stop latch stands. */
TEST_CASE(a_stop_is_decided_by_a_read_since_the_ask)
{
    esc_link_outcome_t o;
    esc_link_xfer_t x;

    up_am_rig();
    link_up();
    CHECK_EQ(esc_link_open(&g_l, 2u, 1u), ESC_LINK_START_OK);
    g_drop_cmd = 3;
    for (int i = 0; i < 40 && g_drop_cmd > 0; ++i) {
        (void)esc_link_service(&g_l, esc.now / 1000u, link_xchg, NULL);
    }
    CHECK_EQ(g_drop_cmd, 0);
    CHECK(esc_link_next(&g_l, esc.now / 1000u, &x));
    CHECK(!x.write);
    esc_link_done(&g_l, ESC_LINK_NO_ANSWER, NULL);
    reads_fail(ESC_LINK_STATUS_FAILS - 1u, ESC_LINK_NO_ANSWER);
    CHECK(esc_link_outcome(&g_l, &o));
    CHECK_EQ(o.kind, ESC_LINK_OUT_NO_ANSWER);
    /* The last read said FREE: no error; the page runs its OPEN. */
    CHECK_EQ(esc_link_faults(&g_l), 0u);
    run_ms(200u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_OPEN);
    /* The stop, with reads still failing: asked, kept. */
    esc_link_stop(&g_l);
    reads_fail(4u, ESC_LINK_NO_ANSWER);
    CHECK_EQ(g_l.urgent, LINK_ESC_OP_STOP);
    CHECK(!flag(LINK_EW_F_STOPPED));
    /* The first read answered decides. */
    link_run(10u);
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_STOP);
    CHECK(flag(LINK_EW_F_STOPPED));
    CHECK_EQ(g_l.urgent, 0u);
    /* On a page that is free indeed it goes unsent. */
    up_am_rig();
    link_up();
    const int cmds = g_commands;

    esc_link_stop(&g_l);
    reads_fail(2u, ESC_LINK_NO_ANSWER);
    CHECK_EQ(g_l.urgent, LINK_ESC_OP_STOP);
    link_run(5u);
    CHECK_EQ(g_l.urgent, 0u);
    CHECK_EQ(g_commands, cmds);
}

/* A stop in a gap of the link, under the IOmcu's 200ms: it reaches the page after. */
TEST_CASE(a_stop_in_a_gap_of_the_link_reaches_the_page)
{
    up_am_rig();
    link_up();
    link_ready(2u);
    esc_link_stop_follow(&g_l, 0u);
    run_ms(1u);
    CHECK_EQ(g_l.urgent, 0u);
    esc_link_lost(&g_l);
    esc_link_stop_follow(&g_l, 1u);
    CHECK_EQ(g_l.urgent, 0u);
    CHECK_EQ(g_l.stops_seen, 0u);
    run_ms(100u);
    CHECK(!flag(LINK_EW_F_STOPPED));
    esc_link_came_up(&g_l, ESC_LINK_MINOR);
    esc_link_stop_follow(&g_l, 1u);
    CHECK_EQ(g_l.stops_seen, 1u);
    link_run(10u);
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_STOP);
    CHECK(flag(LINK_EW_F_STOPPED));
    /* Once. */
    const int cmds = g_commands;

    esc_link_stop_follow(&g_l, 1u);
    link_run(200u);
    CHECK_EQ(g_commands, cmds);
    /* One owed as the link went stays: the first read after decides. */
    up_am_rig();
    link_up();
    link_ready(2u);
    esc_link_stop(&g_l);
    esc_link_lost(&g_l);
    esc_link_came_up(&g_l, ESC_LINK_MINOR);
    link_run(10u);
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_STOP);
    /* To an IOmcu with no page it does not stand. */
    esc_link_stop(&g_l);
    esc_link_lost(&g_l);
    esc_link_came_up(&g_l, ESC_LINK_MINOR - 1u);
    CHECK_EQ(g_l.urgent, 0u);
    esc_link_stop_follow(NULL, 1u);
}

/* NOT_BACK from any CLOSE, no restart: the text says CLOSE tries again. */
TEST_CASE(not_back_tries_again_without_a_restart)
{
    esc_link_outcome_t o;
    uint16_t target = 0u;

    up_am_rig();
    link_up();
    CHECK_EQ(esc_link_open(&g_l, 2u, 1u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    g.give_refused = true;
    CHECK_EQ(esc_link_close(&g_l, 2u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 100u));
    CHECK_EQ(o.result, LINK_ESC_RESULT_NOT_BACK);
    CHECK(esc_link_not_back(&g_l, &target));
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_SYNC);
    const int cmds = g_commands;

    link_run(900u);
    CHECK_EQ(g_commands, cmds);
    link_run(200u);
    CHECK_EQ(g_commands, cmds + 1);
    g.give_refused = false;
    link_run(1100u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_READY);
    CHECK(!esc_link_outcome(&g_l, &o));
}

/* A copy is its session's: gone when the page has no read, so an ESC with equal
 * bytes brings its own name. */
TEST_CASE(the_read_copy_goes_with_its_session)
{
    esc_link_outcome_t o;
    const esc_link_image_t *im = esc_link_image(&g_l);

    up_am_rig();
    link_up();
    link_ready(2u);
    CHECK_EQ(esc_link_read(&g_l, 3u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 20000u));
    link_run(100u);
    CHECK(im->gen != 0u);
    CHECK_EQ(esc_link_leave(&g_l, 4u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 5000u));
    CHECK_EQ(esc_link_close(&g_l, 5u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 100u));
    link_run(600u);
    CHECK_EQ(im->gen, 0u);
}

int main(void)
{
    RUN(an_older_iomcu_is_sent_nothing);
    RUN(a_start_on_a_free_page_sends_no_command);
    RUN(a_restart_closes_what_the_first_read_finds);
    RUN(a_restart_with_a_pin_that_does_not_come_back);
    RUN(a_port_that_does_not_come_back_names_the_port);
    RUN(a_link_that_came_back_closes_nothing);
    RUN(a_stop_is_the_stop_operation);
    RUN(a_stop_drops_a_request_that_has_not_gone);
    RUN(a_stop_after_another_writers_seq_goes_again);
    RUN(an_answer_lost_is_a_command_taken_once);
    RUN(no_answer_at_all_is_said_as_that);
    RUN(a_frame_refused_is_said_with_its_reason);
    RUN(a_page_that_moved_under_a_request_is_said);
    RUN(a_refusal_is_said_as_the_page_says_it);
    RUN(errors_count_on_their_edge);
    RUN(a_stop_with_a_command_unanswered);
    RUN(failed_reads_over_a_hold_are_an_error_and_a_stop);
    RUN(failed_reads_over_a_write_are_an_error_and_a_stop);
    RUN(failed_reads_on_a_free_page_end_the_request);
    RUN(a_stop_is_decided_by_a_read_since_the_ask);
    RUN(a_stop_in_a_gap_of_the_link_reaches_the_page);
    RUN(not_back_tries_again_without_a_restart);
    RUN(the_read_copy_goes_with_its_session);
    return test_summary("esc_link");
}
