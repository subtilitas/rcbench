/*
 * The panel's half of the YGE_PROG pages, shared/bench/yge_link.c: what it
 * sends to whom, and when.
 *
 * Under test: an IOmcu below 4.42 is sent nothing; the CLOSE at link-up after
 * a panel restart and only then, with its retries every 1000ms on WRITING and
 * NOT_BACK; a link that came back closes nothing; the key goes with ENTER and
 * WRITE only; a stop is the STOP operation, drops what has not gone, and cuts
 * a write; 9 failed reads of the page keep a request, the 10th ends it, and
 * over a hold that is an error and the STOP goes; a FAULT counted on its edge
 * and followed to every output off; a command unanswered goes again as it
 * was and the page takes it once.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>

#include "error_sites.h"
#include "yge_link_rig.h"

TEST_CASE(an_older_iomcu_is_sent_nothing)
{
    yge_link_xfer_t x;
    yge_link_outcome_t o;
    yge_link_readout_t r;
    uint8_t port = 0u;

    rig_up();
    yge_link_init(&g_l);
    CHECK(!yge_link_next(&g_l, 0u, &x));
    CHECK_EQ(yge_link_open(&g_l, 0u, 1u), YGE_LINK_START_DOWN);
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_DOWN);
    yge_link_came_up(&g_l, YGE_LINK_MINOR - 1u);
    CHECK(!yge_link_has_page(&g_l));
    CHECK(!yge_link_next(&g_l, 0u, &x));
    CHECK_EQ(yge_link_open(&g_l, 0u, 1u), YGE_LINK_START_NO_PAGE);
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_NO_PAGE);
    yge_link_stop(&g_l);
    CHECK(!yge_link_next(&g_l, 0u, &x));
    /* NULLs. */
    yge_link_init(NULL);
    yge_link_lost(NULL);
    yge_link_came_up(NULL, 42u);
    yge_link_stop(NULL);
    yge_link_stop_follow(NULL, 1u);
    yge_link_abort(NULL);
    yge_link_done(NULL, 0, NULL);
    yge_link_readout(NULL, &r);
    CHECK(!yge_link_has_page(NULL));
    CHECK(!yge_link_next(NULL, 0u, &x));
    CHECK(!yge_link_outcome(NULL, &o));
    CHECK(!yge_link_not_back(NULL, &port));
    CHECK(!yge_link_not_back(&g_l, NULL));
    CHECK_EQ(yge_link_phase(NULL), YGE_LINK_DOWN);
    CHECK_EQ(yge_link_open(NULL, 0u, 1u), YGE_LINK_START_ARG);
    CHECK_EQ(yge_link_enter(&g_l, NULL, 1u), YGE_LINK_START_ARG);
    CHECK_EQ(yge_link_write(&g_l, NULL, 1u, 0u, 0u, 1u), YGE_LINK_START_ARG);
    CHECK(yge_link_image(NULL) == NULL);
    CHECK_EQ(yge_link_faults(NULL), 0u);
    CHECK_EQ(yge_link_service(&g_l, 0u, NULL, NULL), 0u);
}

TEST_CASE(a_start_on_a_free_page_sends_no_command)
{
    yge_link_readout_t r;

    rig_up();
    link_up();
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_READY);
    CHECK_EQ(g_commands, 0);
    CHECK_EQ(reg(LINK_YP_SEQ), 0u);
    yge_link_readout(&g_l, &r);
    CHECK(r.known);
    CHECK(!r.held);
    CHECK_EQ(r.port, 0xFFu);
    CHECK_EQ(r.gpio, 0xFFu);
    /* A free page is read every 500ms, a held one every 100ms. */
    int before = g_exchanges;

    link_run(1000u);
    CHECK_EQ(g_exchanges - before, 2);
    link_ready(0u);
    before = g_exchanges;
    link_run(1000u);
    CHECK_EQ(g_exchanges - before, 10);
    yge_link_readout(&g_l, &r);
    CHECK(r.held);
    CHECK(r.ours);
    CHECK_EQ(r.port, 0u);
    CHECK_EQ(r.gpio, 13u);
    CHECK_EQ(r.line, LINK_YGE_LINE_LISTEN);
    CHECK_EQ(r.state, LINK_YGE_READY);
}

/* The page held by another writer (the bench console) before the panel
 * started: the first read is followed by CLOSE, and nothing else is taken
 * until the page is free. */
TEST_CASE(close_at_link_up_after_a_panel_restart)
{
    yge_link_outcome_t o;

    rig_up();
    ready_on(0u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    CHECK(g.v.asked_on);
    yge_link_init(&g_l);
    yge_link_came_up(&g_l, YGE_LINK_MINOR);
    link_counts_zero();
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_SYNC);
    CHECK_EQ(yge_link_read(&g_l, 1u), YGE_LINK_START_BUSY);
    link_run(100u);
    CHECK_EQ(commands_of(LINK_YGE_OP_CLOSE), 1);
    CHECK_EQ(g_commands, 1);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK(!g.v.asked_on);
    CHECK(!g.listen);
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_READY);
    /* The CLOSE is the module's own: no outcome, and no error. */
    CHECK(!yge_link_outcome(&g_l, &o));
    CHECK_EQ(yge_link_faults(&g_l), 0u);
    /* Once only: a later hold is the caller's. */
    link_ready(0u);
    link_run(2000u);
    CHECK_EQ(commands_of(LINK_YGE_OP_CLOSE), 1);
}

TEST_CASE(a_link_that_came_back_closes_nothing)
{
    yge_link_readout_t r;

    rig_up();
    link_up();
    link_ready(0u);
    yge_link_lost(&g_l);
    yge_link_readout(&g_l, &r);
    CHECK(!r.known);
    CHECK(!r.held);
    yge_link_came_up(&g_l, YGE_LINK_MINOR);
    const int before = g_commands;

    link_run(2000u);
    CHECK_EQ(g_commands, before);
    yge_link_readout(&g_l, &r);
    CHECK(r.held);
    CHECK(!r.ours);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
}

/* A write runs on the page as the panel starts: CLOSE is refused with
 * WRITING, goes again 1000ms later and not before, and frees the page once
 * the write has ended. */
TEST_CASE(the_close_after_a_restart_waits_out_a_write)
{
    rig_up();
    ready_on(0u);
    read_now();
    sim.wlate_n = 100u;
    sim.wlate_us = 300u * MS;
    draft_of(21u, 13u);
    CHECK_EQ(write_table(g_draft), 0u);
    for (int i = 0; i < 5000 && !yge_port_writing(&g_p); ++i) {
        pass();
    }
    CHECK(yge_port_writing(&g_p));
    yge_link_init(&g_l);
    yge_link_came_up(&g_l, YGE_LINK_MINOR);
    link_counts_zero();
    link_run(10u);
    CHECK_EQ(commands_of(LINK_YGE_OP_CLOSE), 1);
    CHECK_EQ(refusal(), LINK_YGE_REF_WRITING);
    link_run(980u);
    CHECK_EQ(commands_of(LINK_YGE_OP_CLOSE), 1);
    link_run(30u);
    CHECK_EQ(commands_of(LINK_YGE_OP_CLOSE), 2);
    for (int i = 0; i < 60 && yge_port_state(&g_p) != LINK_YGE_FREE; ++i) {
        link_run(1000u);
    }
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_READY);
}

/* A port that does not take its pin back: an error once, the port handed
 * out once, CLOSE again every 1000ms until it does. */
TEST_CASE(not_back_is_an_error_and_close_goes_again)
{
    yge_link_outcome_t o;
    uint8_t port = 0xEEu;

    rig_up();
    link_up();
    link_ready(2u);
    g.give_refused = true;
    CHECK_EQ(yge_link_close(&g_l, 3u), YGE_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, YGE_LINK_OUT_DONE);
    CHECK_EQ(o.result, LINK_YGE_RESULT_NOT_BACK);
    CHECK_EQ(o.state, LINK_YGE_NOT_BACK);
    CHECK_EQ(yge_link_faults(&g_l), 1u);
    CHECK(yge_link_not_back(&g_l, &port));
    CHECK_EQ(port, 2u);
    CHECK(!yge_link_not_back(&g_l, &port));
    const int before = commands_of(LINK_YGE_OP_CLOSE);

    link_run(990u);
    CHECK_EQ(commands_of(LINK_YGE_OP_CLOSE), before);
    link_run(20u);
    CHECK_EQ(commands_of(LINK_YGE_OP_CLOSE), before + 1);
    CHECK_EQ(yge_link_faults(&g_l), 1u);
    g.give_refused = false;
    link_run(1100u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_READY);
    CHECK_EQ(yge_link_faults(&g_l), 1u);
}

/* Only ENTER and WRITE carry the key, and only when asked. */
TEST_CASE(the_key_goes_with_enter_and_write_only)
{
    yge_link_outcome_t o;
    const yge_link_image_t *im = yge_link_image(&g_l);
    uint16_t d[YGE_LINK_VALUES_MAX];

    rig_up();
    link_up();
    link_ready(0u);
    link_read(3u);
    CHECK(im->gen != 0u);
    memcpy(d, im->value, sizeof(d));
    d[21] = 13u;
    CHECK_EQ(yge_link_write(&g_l, d, im->entries, im->reads, im->read_crc, 4u),
             YGE_LINK_START_OK);
    CHECK(link_wait(&o, 60000u));
    CHECK_EQ(o.kind, YGE_LINK_OUT_DONE);
    CHECK_EQ(o.result, LINK_YGE_RESULT_DONE);
    CHECK_EQ(sim.table[21], 13u);
    CHECK_EQ(yge_link_leave(&g_l, 5u), YGE_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(yge_link_close(&g_l, 6u), YGE_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(g_commands, 6);
    for (int i = 0; i < g_commands; ++i) {
        const bool keyed = g_cmd_op[i] == LINK_YGE_OP_ENTER
                           || g_cmd_op[i] == LINK_YGE_OP_WRITE;

        CHECK_EQ(g_cmd_key[i], keyed ? LINK_YGE_KEY : 0u);
    }
    /* A draft of no length that fits is refused here. */
    CHECK_EQ(yge_link_write(&g_l, d, 0u, 0u, 0u, 7u), YGE_LINK_START_ARG);
    CHECK_EQ(yge_link_write(&g_l, d, YGE_LINK_VALUES_MAX + 1u, 0u, 0u, 7u),
             YGE_LINK_START_ARG);
}

/* A stop before the command went drops it; a stop in a write cuts it, the
 * line parked and the supply off. */
TEST_CASE(a_stop_drops_what_has_not_gone_and_cuts_a_write)
{
    yge_link_outcome_t o;
    const yge_link_image_t *im = yge_link_image(&g_l);
    uint16_t d[YGE_LINK_VALUES_MAX];

    rig_up();
    link_up();
    link_ready(0u);
    link_read(3u);
    CHECK_EQ(yge_link_read(&g_l, 4u), YGE_LINK_START_OK);
    yge_link_stop(&g_l);
    CHECK(yge_link_outcome(&g_l, &o));
    CHECK_EQ(o.kind, YGE_LINK_OUT_DROPPED);
    CHECK_EQ(o.id, 4u);
    CHECK(!o.taken);
    link_run(50u);
    CHECK_EQ(commands_of(LINK_YGE_OP_READ), 1);
    CHECK_EQ(commands_of(LINK_YGE_OP_STOP), 1);
    CHECK(flag(LINK_YP_F_STOPPED));
    CHECK_ALL_OFF();
    /* CLEAR, ENTER again, read, and a write that a stop cuts. */
    CHECK_EQ(yge_link_clear(&g_l, 5u), YGE_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(yge_link_enter(&g_l, &k_pw, 6u), YGE_LINK_START_OK);
    CHECK(link_wait(&o, 30000u));
    CHECK_EQ(o.state, LINK_YGE_READY);
    link_read(7u);
    sim.wlate_n = 100u;
    sim.wlate_us = 300u * MS;
    memcpy(d, im->value, sizeof(d));
    d[21] = 13u;
    CHECK_EQ(yge_link_write(&g_l, d, im->entries, im->reads, im->read_crc, 8u),
             YGE_LINK_START_OK);
    for (int i = 0; i < 400 && !yge_port_writing(&g_p); ++i) {
        link_run(5u);
    }
    CHECK(yge_port_writing(&g_p));
    yge_link_stop_follow(&g_l, 1u);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, YGE_LINK_OUT_DONE);
    CHECK_EQ(o.result, LINK_YGE_RESULT_CUT);
    CHECK(flag(LINK_YP_F_CUT_WRITE));
    CHECK_ALL_OFF();
    /* The same count again is no new stop. */
    const int stops = commands_of(LINK_YGE_OP_STOP);

    yge_link_stop_follow(&g_l, 1u);
    link_run(100u);
    CHECK_EQ(commands_of(LINK_YGE_OP_STOP), stops);
}

/* 9 failed reads of the page keep a request, the 10th ends it: over a hold
 * it is an error and the STOP goes; over a free page neither. */
TEST_CASE(ten_failed_reads_end_a_request)
{
    yge_link_outcome_t o;

    rig_up();
    link_up();
    link_ready(0u);
    CHECK_EQ(yge_link_read(&g_l, 3u), YGE_LINK_START_OK);
    link_run(10u);
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_RUNNING);
    /* One read every 5ms pass: 9 fail, the request stays. */
    g_drop_status = 9;
    link_run(45u);
    CHECK_EQ(g_drop_status, 0);
    CHECK(!yge_link_outcome(&g_l, &o));
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_RUNNING);
    /* One answered read counts from 0 again; then the 10th in a row ends
     * it. */
    link_run(5u);
    g_drop_status = 10;
    link_run(45u);
    CHECK(!yge_link_outcome(&g_l, &o));
    CHECK_EQ(yge_link_faults(&g_l), 0u);
    link_run(5u);
    CHECK(yge_link_outcome(&g_l, &o));
    CHECK_EQ(o.kind, YGE_LINK_OUT_NO_ANSWER);
    CHECK_EQ(o.id, 3u);
    CHECK_EQ(yge_link_faults(&g_l), 1u);
    link_run(50u);
    CHECK_EQ(commands_of(LINK_YGE_OP_STOP), 1);
    CHECK(flag(LINK_YP_F_STOPPED));
    CHECK_ALL_OFF();
    /* Over a free page: the request ends, no error, no STOP. */
    CHECK_EQ(yge_link_close(&g_l, 4u), YGE_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    link_run(600u);
    CHECK_EQ(yge_link_close(&g_l, 5u), YGE_LINK_START_OK);
    g_drop_status = 10;
    link_run(50u);
    CHECK(yge_link_outcome(&g_l, &o));
    CHECK_EQ(o.kind, YGE_LINK_OUT_NO_ANSWER);
    CHECK_EQ(o.id, 5u);
    link_run(100u);
    CHECK_EQ(yge_link_faults(&g_l), 1u);
    CHECK_EQ(commands_of(LINK_YGE_OP_STOP), 1);
}

/* A FAULT counts once on its edge, not when it stands at the first read after
 * the link came up, and the panel's act on it switches every output off. */
TEST_CASE(a_fault_is_followed_to_all_outputs_off)
{
    yge_link_outcome_t o;
    error_watch_t watch = { 0 };
    const error_bench_t armed = {
        .live = true, .supply_on = true, .port_holds = true,
        .supply_real = true, .supply_page = true, .far_minor = YGE_LINK_MINOR,
        .prog_supply = true,
    };

    rig_up();
    link_up();
    link_ready(0u);
    CHECK_EQ(yge_link_faults(&g_l), 0u);
    error_act_t act = error_sites_follow(&watch, yge_link_faults(&g_l), &armed);

    CHECK(!act.disarm);
    /* The supply goes off under the session: an error of the page. */
    g.dropped = true;
    g.v.on = false;
    g.v.off = true;
    link_run(200u);
    CHECK(flag(LINK_YP_F_FAULT));
    CHECK_EQ(yge_link_faults(&g_l), 1u);
    CHECK_ALL_OFF();
    act = error_sites_follow(&watch, yge_link_faults(&g_l), &armed);
    CHECK(act.disarm);
    CHECK(act.supply_off);
    CHECK(act.cut);
    CHECK(act.prog_off);
    /* Once: the next look acts on nothing. */
    act = error_sites_follow(&watch, yge_link_faults(&g_l), &armed);
    CHECK(!act.disarm);
    link_run(1000u);
    CHECK_EQ(yge_link_faults(&g_l), 1u);
    /* Standing at the first read after the link came up: not counted. */
    yge_link_lost(&g_l);
    yge_link_came_up(&g_l, YGE_LINK_MINOR);
    link_run(500u);
    CHECK_EQ(yge_link_faults(&g_l), 1u);
    CHECK(!yge_link_outcome(&g_l, &o) || o.kind == YGE_LINK_OUT_NO_ANSWER);
}

/* A command with no answer goes again as it was, 3 times; the page takes it
 * once. */
TEST_CASE(a_command_unanswered_goes_again_as_it_was)
{
    yge_link_outcome_t o;

    rig_up();
    link_up();
    CHECK_EQ(yge_link_open(&g_l, 0u, 1u), YGE_LINK_START_OK);
    /* The two staged frames go; the command's first two tries are lost. */
    link_run(5u);
    g_drop = 2;
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, YGE_LINK_OUT_DONE);
    CHECK_EQ(o.state, LINK_YGE_OPEN);
    CHECK_EQ(reg(LINK_YP_SEQ), 1u);
    /* 3 lost in a row: a read says whether it was taken. */
    CHECK_EQ(yge_link_close(&g_l, 2u), YGE_LINK_START_OK);
    g_drop = 3;
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, YGE_LINK_OUT_NO_ANSWER);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_OPEN);
    /* A staged frame lost 3 times ends the request. */
    CHECK_EQ(yge_link_enter(&g_l, &k_pw, 3u), YGE_LINK_START_OK);
    g_drop = 3;
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, YGE_LINK_OUT_NO_ANSWER);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_OPEN);
}

int main(void)
{
    RUN(an_older_iomcu_is_sent_nothing);
    RUN(a_start_on_a_free_page_sends_no_command);
    RUN(close_at_link_up_after_a_panel_restart);
    RUN(a_link_that_came_back_closes_nothing);
    RUN(the_close_after_a_restart_waits_out_a_write);
    RUN(not_back_is_an_error_and_close_goes_again);
    RUN(the_key_goes_with_enter_and_write_only);
    RUN(a_stop_drops_what_has_not_gone_and_cuts_a_write);
    RUN(ten_failed_reads_end_a_request);
    RUN(a_fault_is_followed_to_all_outputs_off);
    RUN(a_command_unanswered_goes_again_as_it_was);
    return test_summary("yge_link");
}
