/*
 * How a session of programming for YGE ESCs ends, as the model
 * (shared/bench/yge_prog.c) says it: each way the link ends a request
 * (refused, NACK, no answer, restarted, dropped), each result of the page
 * (no line, a session error, BY HAND run out, the pin not back), a stop or an
 * ARM between two steps of CONNECT, a command that never reaches the link's
 * task, READ and CLOSE on their own and their refusals, the link's state in
 * place of a good word, and NULL taken everywhere.
 *
 * SPDX-License-Identifier: MIT
 */
#include "yge_prog_rig.h"

/* Every way the link ends a request has its own word, and ends the flow. */
TEST_CASE(each_way_a_request_ends_has_its_word)
{
    uint16_t detail = 0u;
    static yge_prog_cmd_t c;

    rig_up();
    model_up();
    /* REFUSED: the page takes OPEN, then refuses ENTER with no PD mini. */
    g.v.online = false;
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_NONE);
    model_run(500u);
    CHECK_EQ(yge_prog_message(&g_m, &detail), YGE_PROG_MSG_REFUSED);
    CHECK_EQ(detail, LINK_YGE_REF_NO_SUPPLY);
    CHECK_EQ(refusal(), LINK_YGE_REF_NO_SUPPLY);
    CHECK(!yge_prog_busy(&g_m));
    CHECK_EQ(commands_of(LINK_YGE_OP_READ), 0);
    CHECK(!g.v.asked_on);
    g.v.online = true;
    CHECK_EQ(yge_prog_enter_again(&g_m, &k_conn), YGE_PROG_MSG_NONE);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_READ_DONE);
    /* NACK: the command frame refused. */
    g_nack_cmd = 1;
    CHECK_EQ(yge_prog_read(&g_m), YGE_PROG_MSG_NONE);
    model_run(100u);
    CHECK_EQ(yge_prog_message(&g_m, &detail), YGE_PROG_MSG_NACK);
    CHECK_EQ(detail, LINK_NACK_BUSY);
    CHECK(!yge_prog_busy(&g_m));
    /* RESTARTED: another writer moves the SEQ under a running read. */
    CHECK_EQ(yge_prog_read(&g_m), YGE_PROG_MSG_NONE);
    model_run(20u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_RUN);
    (void)cmd(LINK_YGE_OP_READ);
    model_run(100u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_RESTARTED);
    CHECK(!yge_prog_busy(&g_m));
    model_run(30000u);
    /* NO_ANSWER: 10 failed reads over a hold; an error, and the STOP the
     * link sends for it, which does not replace the word. */
    const uint32_t faults = yge_link_faults(&g_l);

    CHECK_EQ(yge_prog_read(&g_m), YGE_PROG_MSG_NONE);
    model_run(10u);
    g_drop_status = 10;
    model_run(100u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_NO_ANSWER);
    CHECK_EQ(yge_link_faults(&g_l), faults + 1u);
    model_run(100u);
    CHECK(flag(LINK_YP_F_STOPPED));
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_NO_ANSWER);
    CHECK_ALL_OFF();
    CHECK(yge_prog_holds_port(&g_m));
    /* DROPPED: a stop before the OPEN went; the page stays free. */
    CHECK_EQ(yge_prog_leave(&g_m), YGE_PROG_MSG_NONE);
    model_run(500u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    const int opens = commands_of(LINK_YGE_OP_OPEN);

    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_NONE);
    CHECK(yge_prog_next_cmd(&g_m, &c));
    carry_out(&c);
    yge_link_stop(&g_l);
    model_run(600u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_DROPPED);
    CHECK(!yge_prog_busy(&g_m));
    CHECK_EQ(commands_of(LINK_YGE_OP_OPEN), opens);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
}

/* Each result of the page that is not DONE has its own word. */
TEST_CASE(each_result_of_the_page_has_its_word)
{
    uint16_t detail = 0u;
    yge_prog_connect_t hand = k_conn;

    rig_up();
    model_up();
    /* NO_LINE: the line gets no resources, the pin goes back. */
    g.take_ok = false;
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_NONE);
    model_run(500u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_NO_LINE);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK_EQ(commands_of(LINK_YGE_OP_ENTER), 0);
    CHECK(!yge_prog_holds_port(&g_m));
    g.take_ok = true;
    /* SESSION: an ESC that never answers; the supply off. */
    sim.silent = true;
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_NONE);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, &detail), YGE_PROG_MSG_SESSION);
    CHECK_EQ(detail, OY_SILENT);
    CHECK_EQ(commands_of(LINK_YGE_OP_READ), 0);
    CHECK_ALL_OFF();
    sim.silent = false;
    /* NOT_BACK: the port does not take the pin back at the CLOSE. */
    g.give_refused = true;
    CHECK_EQ(yge_prog_leave(&g_m), YGE_PROG_MSG_NONE);
    model_run(500u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_NOT_BACK);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_NOT_BACK);
    CHECK(yge_prog_holds_port(&g_m));
    g.give_refused = false;
    model_run(1500u);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    /* TIMEOUT: BY HAND with no battery; the page's fault says it, with no
     * request running. */
    hand.power.power = LINK_YGE_BY_HAND;
    const int ons = g.ons;

    CHECK_EQ(yge_prog_connect(&g_m, &hand), YGE_PROG_MSG_NONE);
    model_run(500u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_HAND_WAIT);
    model_run(60000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_TIMEOUT);
    CHECK(flag(LINK_YP_F_FAULT));
    CHECK_EQ(g.ons, ons);
}

/* A stop or an ARM between OPEN and ENTER ends CONNECT: no ENTER goes. */
TEST_CASE(a_stop_or_an_arm_between_two_steps_ends_connect)
{
    for (int arm = 0; arm < 2; ++arm) {
        rig_up();
        model_up();
        CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_NONE);
        model_run(5u);
        CHECK_EQ(commands_of(LINK_YGE_OP_OPEN), 1);
        g_stopped = arm == 0;
        g_armed = arm != 0;
        model_run(500u);
        CHECK_EQ(yge_prog_message(&g_m, NULL),
                 arm != 0 ? YGE_PROG_MSG_ARMED : YGE_PROG_MSG_STOPPED);
        CHECK(!yge_prog_busy(&g_m));
        CHECK_EQ(commands_of(LINK_YGE_OP_ENTER), 0);
        CHECK_EQ(g.ons, 0);
        CHECK_EQ(yge_port_state(&g_p), LINK_YGE_OPEN);
        /* The pin goes back under the stop. */
        CHECK_EQ(yge_prog_leave(&g_m), YGE_PROG_MSG_NONE);
        model_run(500u);
        CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    }
}

/* A command the link's task never took: still waited for at 1999ms, given
 * up at 2000ms with its pass taken back, so it is never carried out. */
TEST_CASE(a_command_that_never_reaches_the_link_is_taken_back)
{
    static yge_prog_cmd_t c;
    yge_prog_feed_t f;

    rig_up();
    model_up();
    connected();
    CHECK_EQ(yge_prog_read(&g_m), YGE_PROG_MSG_NONE);
    CHECK(yge_prog_next_cmd(&g_m, &c));
    f = g_f;
    f.now_ms = g_m.wait_ms + YGE_PROG_CMD_WAIT_MS - 1u;
    yge_prog_feed(&g_m, &f);
    CHECK(yge_prog_busy(&g_m));
    f.now_ms = g_m.wait_ms + YGE_PROG_CMD_WAIT_MS;
    yge_prog_feed(&g_m, &f);
    CHECK(!yge_prog_busy(&g_m));
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_NO_ANSWER);
    CHECK(!yge_prog_gate_claim(&g_gate, c.id));
    const int before = g_commands;

    carry_out(&c);
    model_run(100u);
    CHECK_EQ(g_commands, before);
}

/* READ and CLOSE on their own; LEAVE and CLOSE wait for a flow. */
TEST_CASE(read_and_close_on_their_own)
{
    rig_up();
    model_up();
    connected();
    CHECK_EQ(yge_prog_read(&g_m), YGE_PROG_MSG_NONE);
    CHECK_EQ(yge_prog_leave(&g_m), YGE_PROG_MSG_BUSY);
    CHECK_EQ(yge_prog_close(&g_m), YGE_PROG_MSG_BUSY);
    CHECK_EQ(yge_prog_enter_again(&g_m, &k_conn), YGE_PROG_MSG_BUSY);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_READ_DONE);
    CHECK_EQ(commands_of(LINK_YGE_OP_READ), 2);
    /* ENTER again only from OPEN. */
    CHECK_EQ(yge_prog_enter_again(&g_m, &k_conn), YGE_PROG_MSG_NOT_READY);
    CHECK_EQ(yge_prog_close(&g_m), YGE_PROG_MSG_NONE);
    model_run(500u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_LEFT);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK_EQ(commands_of(LINK_YGE_OP_LEAVE), 0);
    CHECK(!g.v.asked_on);
    CHECK(yge_prog_idle(&g_m));
}

/* A write on an ESC that answered and was not read: nothing goes. */
TEST_CASE(a_write_before_any_read_sends_nothing)
{
    rig_up();
    model_up();
    /* The bench console's session, taken over after it entered. */
    ready_on(0u);
    model_run(600u);
    CHECK_EQ(g_f.rd.state, LINK_YGE_READY);
    CHECK_EQ(yge_prog_write(&g_m, sim.table, 32u), YGE_PROG_MSG_NOT_READ);
    model_run(100u);
    CHECK_EQ(g_commands, 0);
}

/* The link's state stands in for a good word while the link is gone, and
 * goes when it is back. */
TEST_CASE(the_link_state_stands_in_for_a_good_word)
{
    rig_up();
    model_up();
    connected();
    yge_link_lost(&g_l);
    model_run(5u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_NO_LINK);
    yge_link_came_up(&g_l, YGE_LINK_MINOR);
    model_run(100u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_NONE);
    yge_link_lost(&g_l);
    yge_link_came_up(&g_l, YGE_LINK_MINOR - 1u);
    model_run(5u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_NO_PAGE);
}

TEST_CASE(every_call_takes_null)
{
    yge_prog_cmd_t c;

    yge_prog_init(NULL);
    yge_prog_gate_set(NULL, NULL);
    yge_prog_feed(NULL, NULL);
    yge_prog_limits(NULL, 0u, 0u);
    yge_prog_abort(NULL);
    yge_prog_backup_follow(NULL);
    yge_prog_card_done(NULL, 1u, 0u);
    CHECK(!yge_prog_next_cmd(NULL, &c));
    CHECK_EQ(yge_prog_hand_done(NULL), YGE_PROG_MSG_BUSY);
    CHECK_EQ(yge_prog_enter_again(NULL, &k_conn), YGE_PROG_MSG_BUSY);
    CHECK_EQ(yge_prog_read(NULL), YGE_PROG_MSG_BUSY);
    CHECK_EQ(yge_prog_leave(NULL), YGE_PROG_MSG_BUSY);
    CHECK_EQ(yge_prog_clear(NULL), YGE_PROG_MSG_BUSY);
    CHECK_EQ(yge_prog_close(NULL), YGE_PROG_MSG_BUSY);
    CHECK_EQ(yge_prog_message(NULL, NULL), YGE_PROG_MSG_NONE);
    CHECK_EQ(yge_prog_bad(NULL), 0u);
    CHECK(!yge_prog_busy(NULL));
    CHECK(!yge_prog_idle(NULL));
    CHECK(!yge_prog_holds_port(NULL));
    CHECK(!yge_prog_port_taken(NULL));
    CHECK_EQ(yge_prog_taken_port(NULL), -1);
    CHECK_EQ(yge_prog_backup_where(NULL), YGE_PROG_BACKUP_NONE);
    CHECK_EQ(yge_prog_backup_text(NULL, NULL, 0u), 0u);
    CHECK(yge_prog_file_draft(NULL, NULL) == NULL);
    CHECK(!yge_prog_guard_of(NULL, NULL));
}

int main(void)
{
    RUN(each_way_a_request_ends_has_its_word);
    RUN(each_result_of_the_page_has_its_word);
    RUN(a_stop_or_an_arm_between_two_steps_ends_connect);
    RUN(a_command_that_never_reaches_the_link_is_taken_back);
    RUN(read_and_close_on_their_own);
    RUN(a_write_before_any_read_sends_nothing);
    RUN(the_link_state_stands_in_for_a_good_word);
    RUN(every_call_takes_null);
    return test_summary("yge_prog_ends");
}
