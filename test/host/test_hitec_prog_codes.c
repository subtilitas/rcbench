/*
 * Every code the Hitec programmer's model (shared/bench/hitec_prog.c) turns
 * into a message, and its edges, by feeds written by hand: each refusal of
 * the page, each outcome kind and result, each answer of the link's task
 * that is not OK, a request without an outcome past HITEC_PROG_RUN_MS, an
 * ABORT before the command is taken, the command ids across 2^32 and the
 * operations refused in each state; a step its decoder refuses, an ABORT
 * of a step not handed out and of an OPEN on its way, a RELEASE the page
 * refuses, a link back after a loss, READ ALL on a stopped port and a
 * CLEAR given up; a command unanswered for 2s with no gate and with its
 * pass claimed, the last port and pin and one past them, an ABORT with
 * nothing out.  On the loopback of hitec_prog_rig.h:
 * the session's failures (no D series servo, no classic servo, a wrong
 * entry, a read with a wrong sum on every try), an OPEN cut by a stop, a CLEAR refused UNSAFE and
 * a CLEAR that enters again after the supply went off, the port closed or
 * taken by another writer under the model, and LEAVE of a servo that never
 * entered.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_rig.h"

/* --------------------------------------------------------- by hand */

static hitec_prog_t     m;
static hitec_prog_feed_t f;

/* A model fed a page that holds no port, the tester on port 1 at GPIO 13,
 * a PD mini answering. */
static void hand(void)
{
    hitec_prog_init(&m);
    memset(&f, 0, sizeof(f));
    f.now_ms = 1000u;
    f.rd.phase = HITEC_LINK_READY;
    f.rd.known = true;
    f.rd.flags = LINK_HT_F_RAIL;
    f.target = 1u;
    f.port_pin[0] = 13u;
    f.port_pin_known = 1u;
    f.rx_mark = (uint8_t)HITEC_RX_LINK_HELD;
    hitec_prog_feed(&m, &f);
}

/* The command the model hands out now. */
static hitec_prog_cmd_t taken(void)
{
    hitec_prog_cmd_t c;

    memset(&c, 0, sizeof(c));
    CHECK(hitec_prog_take(&m, &c));
    return c;
}

/* The link's task answers command @p c with @p answer. */
static void answer(const hitec_prog_cmd_t *c, hitec_link_start_t answer_)
{
    f.answer_id = c->id;
    f.answer = (uint8_t)answer_;
    f.now_ms += 5u;
    hitec_prog_feed(&m, &f);
}

/* And then hands out outcome @p o for it. */
static void outcome_of(const hitec_prog_cmd_t *c, hitec_link_outcome_t o)
{
    o.id = c->id;
    f.outcome = o;
    f.outcomes++;
    f.now_ms += 5u;
    hitec_prog_feed(&m, &f);
}

/* CONNECT, its OPEN taken and answered OK. */
static hitec_prog_cmd_t opening(void)
{
    hitec_prog_cmd_t c;

    hand();
    CHECK_EQ(hitec_prog_connect(&m), HITEC_PROG_MSG_NONE);
    c = taken();
    CHECK_EQ(c.kind, HITEC_PROG_CMD_OPEN);
    answer(&c, HITEC_LINK_START_OK);
    return c;
}

TEST_CASE(every_refusal_of_the_page_has_its_message)
{
    static const struct {
        link_hitec_refusal_t ref;
        unsigned msg;
    } k[] = {
        { LINK_HITEC_REF_BUSY, HITEC_PROG_MSG_BUSY },
        { LINK_HITEC_REF_PORT, HITEC_PROG_MSG_HELD_OTHER },
        { LINK_HITEC_REF_HELD, HITEC_PROG_MSG_HELD_KST },
        { LINK_HITEC_REF_ARMED, HITEC_PROG_MSG_NEED_DISARM },
        { LINK_HITEC_REF_UNSAFE, HITEC_PROG_MSG_UNSAFE },
        { LINK_HITEC_REF_NOT_OPEN, HITEC_PROG_MSG_SESSION_ENDED },
        { LINK_HITEC_REF_STOPPED, HITEC_PROG_MSG_STOPPED },
        { LINK_HITEC_REF_NO_PIN, HITEC_PROG_MSG_NO_PIN },
        { LINK_HITEC_REF_NO_SUPPLY, HITEC_PROG_MSG_NEED_PD_C },
        { LINK_HITEC_REF_NO_KEY, HITEC_PROG_MSG_REFUSED },
        { LINK_HITEC_REF_RANGE, HITEC_PROG_MSG_REFUSED },
    };

    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        const hitec_prog_cmd_t c = opening();
        hitec_link_outcome_t o;

        memset(&o, 0, sizeof(o));
        o.kind = HITEC_LINK_OUT_REFUSED;
        o.refusal = k[i].ref;
        outcome_of(&c, o);
        CHECK_EQ(hitec_prog_message(&m)->id, k[i].msg);
        if (k[i].msg == HITEC_PROG_MSG_REFUSED) {
            CHECK_EQ(hitec_prog_message(&m)->arg, (unsigned)k[i].ref);
        }
        CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_PWM);
        CHECK(!hitec_prog_holds_port(&m));
    }
}

TEST_CASE(every_outcome_kind_and_result_has_its_message)
{
    static const struct {
        hitec_link_outcome_kind_t kind;
        uint8_t result;
        unsigned msg;
    } k[] = {
        { HITEC_LINK_OUT_DONE, LINK_HITEC_RESULT_SUPPLY,
          HITEC_PROG_MSG_SUPPLY },
        { HITEC_LINK_OUT_DONE, LINK_HITEC_RESULT_NO_LINE,
          HITEC_PROG_MSG_NO_LINE },
        { HITEC_LINK_OUT_DONE, LINK_HITEC_RESULT_START,
          HITEC_PROG_MSG_UNKNOWN },
        { HITEC_LINK_OUT_NACK, 0u, HITEC_PROG_MSG_NACK },
        { HITEC_LINK_OUT_RESTARTED, 0u, HITEC_PROG_MSG_RESTARTED },
        { HITEC_LINK_OUT_DROPPED, 0u, HITEC_PROG_MSG_ABORTED },
        { HITEC_LINK_OUT_NO_ANSWER, 0u, HITEC_PROG_MSG_NO_ANSWER },
    };

    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        const hitec_prog_cmd_t c = opening();
        hitec_link_outcome_t o;

        memset(&o, 0, sizeof(o));
        o.kind = k[i].kind;
        o.result = k[i].result;
        o.nack = 7u;
        outcome_of(&c, o);
        CHECK_EQ(hitec_prog_message(&m)->id, k[i].msg);
        if (k[i].msg == HITEC_PROG_MSG_NACK) {
            CHECK_EQ(hitec_prog_message(&m)->arg, 7u);
        }
        if (k[i].msg == HITEC_PROG_MSG_UNKNOWN) {
            CHECK_EQ(hitec_prog_message(&m)->arg, LINK_HITEC_RESULT_START);
        }
    }
}

TEST_CASE(an_open_cut_by_a_stop_holds_the_port_stopped)
{
    const hitec_prog_cmd_t c = opening();
    hitec_link_outcome_t o;

    memset(&o, 0, sizeof(o));
    o.kind = HITEC_LINK_OUT_DONE;
    o.result = LINK_HITEC_RESULT_CUT;
    f.stopped = true;
    f.rd.held = true;
    f.rd.ours = true;
    f.rd.stopped = true;
    f.rd.flags |= LINK_HT_F_STOPPED;
    outcome_of(&c, o);
    CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_STOPPED);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_HELD);
    CHECK(hitec_prog_stopped(&m));
    CHECK(!hitec_prog_in_mode(&m));
    /* Not stopped at the panel: ABORT. */
    {
        const hitec_prog_cmd_t c2 = opening();

        f.rd.held = true;
        f.rd.ours = true;
        o.kind = HITEC_LINK_OUT_DONE;
        o.result = LINK_HITEC_RESULT_CUT;
        outcome_of(&c2, o);
        CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_ABORTED);
    }
}

TEST_CASE(every_answer_of_the_link_task_that_is_not_ok_has_its_message)
{
    static const struct {
        hitec_link_start_t answer;
        unsigned msg;
    } k[] = {
        { HITEC_LINK_START_DOWN, HITEC_PROG_MSG_DOWN },
        { HITEC_LINK_START_NO_PAGE, HITEC_PROG_MSG_NO_PAGE },
        { HITEC_LINK_START_BUSY, HITEC_PROG_MSG_BUSY },
        { HITEC_LINK_START_ARG, HITEC_PROG_MSG_UNKNOWN },
    };

    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        hitec_prog_cmd_t c;

        hand();
        CHECK_EQ(hitec_prog_connect(&m), HITEC_PROG_MSG_NONE);
        c = taken();
        answer(&c, k[i].answer);
        CHECK_EQ(hitec_prog_message(&m)->id, k[i].msg);
        CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_PWM);
    }
}

TEST_CASE(a_request_without_an_outcome_is_aborted_after_its_limit)
{
    hitec_prog_cmd_t c = opening();
    hitec_prog_cmd_t a;

    f.now_ms += HITEC_PROG_RUN_MS - 20u;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_OPENING);
    f.now_ms += 20u;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_TIMEOUT);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_PWM);
    a = taken();
    CHECK_EQ(a.kind, HITEC_PROG_CMD_ABORT);
    CHECK_EQ(a.id, 0u);
    /* The outcome that comes late is no request of it. */
    {
        hitec_link_outcome_t o;

        memset(&o, 0, sizeof(o));
        o.kind = HITEC_LINK_OUT_DONE;
        o.result = LINK_HITEC_RESULT_DONE;
        outcome_of(&c, o);
        CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_PWM);
        CHECK(!hitec_prog_holds_port(&m));
    }
}

TEST_CASE(an_abort_before_the_command_is_taken_sends_nothing)
{
    hitec_prog_cmd_t c;

    hand();
    CHECK_EQ(hitec_prog_connect(&m), HITEC_PROG_MSG_NONE);
    hitec_prog_abort(&m);
    CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_ABORTED);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_PWM);
    CHECK(!hitec_prog_take(&m, &c));
}

TEST_CASE(command_ids_run_on_across_2_to_the_32)
{
    hitec_prog_cmd_t c;

    hitec_prog_init(&m);
    memset(&f, 0, sizeof(f));
    f.now_ms = 1000u;
    f.rd.phase = HITEC_LINK_READY;
    f.rd.known = true;
    f.rd.flags = LINK_HT_F_RAIL;
    f.target = 1u;
    f.port_pin[0] = 13u;
    f.port_pin_known = 1u;
    /* The control task's last answer was to id 2^32 - 1. */
    f.answer_id = 0xFFFFFFFFu;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_connect(&m), HITEC_PROG_MSG_NONE);
    c = taken();
    CHECK_EQ(c.id, 1u);
    /* An answer to the old id is not this command's. */
    f.answer = (uint8_t)HITEC_LINK_START_BUSY;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_OPENING);
}

TEST_CASE(operations_are_refused_in_states_that_do_not_take_them)
{
    hand();
    /* PWM: nothing held. */
    CHECK_EQ(hitec_prog_read(&m), HITEC_PROG_MSG_NOT_IN_MODE);
    CHECK_EQ(hitec_prog_leave(&m), HITEC_PROG_MSG_NOT_IN_MODE);
    CHECK_EQ(hitec_prog_clear(&m), HITEC_PROG_MSG_NOT_IN_MODE);
    CHECK_EQ(hitec_prog_release(&m), HITEC_PROG_MSG_BUSY);
    /* While OPEN is out. */
    CHECK_EQ(hitec_prog_connect(&m), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_connect(&m), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(hitec_prog_read(&m), HITEC_PROG_MSG_BUSY);
    CHECK(!hitec_prog_series_set(&m, HITEC_CLASSIC));
    /* No IOmcu. */
    hand();
    f.rd.phase = HITEC_LINK_DOWN;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_read(&m), HITEC_PROG_MSG_DOWN);
    CHECK_EQ(hitec_prog_leave(&m), HITEC_PROG_MSG_DOWN);
    CHECK(hitec_prog_series_set(&m, HITEC_CLASSIC));
    /* An IOmcu without the page. */
    f.rd.phase = HITEC_LINK_NO_PAGE;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_read(&m), HITEC_PROG_MSG_NO_PAGE);
    CHECK_EQ(hitec_prog_clear(&m), HITEC_PROG_MSG_NO_PAGE);
    CHECK(!hitec_prog_port_taken(&f.rd));
    /* The page not read yet. */
    f.rd.phase = HITEC_LINK_SYNC;
    f.rd.known = false;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_SYNC);
    CHECK_EQ(hitec_prog_connect(&m), HITEC_PROG_MSG_BUSY);
    CHECK(hitec_prog_port_taken(&f.rd));
    /* Not fed at all. */
    hitec_prog_init(&m);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_DOWN);
    CHECK(!hitec_prog_on_port(&m));
    CHECK_EQ(hitec_prog_pin(&m), HITEC_PROG_NO_PIN);
    CHECK(hitec_prog_servo(&m) == NULL);
    CHECK(hitec_prog_model(&m) == NULL);
}

/* The page holds port 1 for this model. */
static void page_holds(bool stopped)
{
    f.rd.held = true;
    f.rd.ours = true;
    f.rd.port_no = 0u;
    f.rd.stopped = stopped;
    if (stopped) {
        f.rd.flags |= LINK_HT_F_STOPPED;
    }
}

/* CONNECT, its OPEN run: held, the entry's first step not handed out. */
static void entering(void)
{
    const hitec_prog_cmd_t c = opening();
    hitec_link_outcome_t o;

    memset(&o, 0, sizeof(o));
    o.kind = HITEC_LINK_OUT_DONE;
    o.result = LINK_HITEC_RESULT_DONE;
    page_holds(false);
    outcome_of(&c, o);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_ENTERING);
}

/* CONNECT, its OPEN cut by a stop at the bench: held and stopped. */
static void held_stopped(void)
{
    const hitec_prog_cmd_t c = opening();
    hitec_link_outcome_t o;

    memset(&o, 0, sizeof(o));
    o.kind = HITEC_LINK_OUT_DONE;
    o.result = LINK_HITEC_RESULT_CUT;
    page_holds(true);
    outcome_of(&c, o);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_HELD);
}

/* The next step the session hands out, the clock run on until it does. */
static hitec_prog_cmd_t next_step(void)
{
    hitec_prog_cmd_t c;

    memset(&c, 0, sizeof(c));
    for (unsigned i = 0; i < 400u && !hitec_prog_take(&m, &c); ++i) {
        f.now_ms += 10u;
        hitec_prog_feed(&m, &f);
    }
    CHECK_EQ(c.kind, HITEC_PROG_CMD_STEP);
    return c;
}

TEST_CASE(a_step_the_decoder_refuses_ends_the_entry_unknown)
{
    hitec_prog_cmd_t s;
    hitec_link_outcome_t o;

    entering();
    /* The POWER step runs. */
    s = next_step();
    answer(&s, HITEC_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = HITEC_LINK_OUT_DONE;
    o.result = LINK_HITEC_RESULT_DONE;
    o.ses.run = (uint8_t)HITEC_SES_RAN;
    outcome_of(&s, o);
    /* The model register's read: its decoder refuses the step. */
    s = next_step();
    answer(&s, HITEC_LINK_START_OK);
    o.ses.rx = (uint8_t)HITEC_RX_ERR_ARG;
    outcome_of(&s, o);
    CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_UNKNOWN);
    CHECK_EQ(hitec_prog_message(&m)->arg, (unsigned)HITEC_SES_ERR_STEP);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_HELD);
    CHECK(!hitec_prog_in_mode(&m));
}

TEST_CASE(an_abort_ends_a_step_not_handed_out_and_an_open_on_its_way)
{
    hitec_prog_cmd_t c;

    /* The entry's first step is ready, not taken: nothing of it ran. */
    entering();
    hitec_prog_abort(&m);
    CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_ABORTED);
    f.now_ms += 10u;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_HELD);
    CHECK(!hitec_prog_in_mode(&m));
    CHECK(!hitec_prog_take(&m, &c));
    /* OPEN taken and answered, its outcome awaited: ABORT goes out. */
    (void)opening();
    hitec_prog_abort(&m);
    c = taken();
    CHECK_EQ(c.kind, HITEC_PROG_CMD_ABORT);
}

TEST_CASE(a_release_the_page_refuses_says_why_and_keeps_the_port_held)
{
    hitec_prog_cmd_t c;
    hitec_link_outcome_t o;

    hand();
    f.rd.held = true;
    f.rd.ours = false;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_OTHER);
    CHECK_EQ(hitec_prog_release(&m), HITEC_PROG_MSG_NONE);
    c = taken();
    CHECK_EQ(c.kind, HITEC_PROG_CMD_CLOSE);
    answer(&c, HITEC_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = HITEC_LINK_OUT_NACK;
    o.nack = 3u;
    outcome_of(&c, o);
    CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_NACK);
    CHECK_EQ(hitec_prog_message(&m)->arg, 3u);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_OTHER);
}

TEST_CASE(a_link_back_after_a_loss_says_the_session_ended)
{
    held_stopped();
    f.rd.phase = HITEC_LINK_DOWN;
    f.now_ms += 10u;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_DOWN);
    CHECK(!hitec_prog_holds_port(&m));
    /* Back, and the IOmcu gave the port back to the tester. */
    f.rd.phase = HITEC_LINK_READY;
    f.rd.held = false;
    f.rd.ours = false;
    f.now_ms += 10u;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_SESSION_ENDED);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_PWM);
}

/* A session the link or the page takes: a line about what it held goes at once,
 * in both phases, and the panel redraws on the sequence number. */
TEST_CASE(a_session_lost_takes_the_lines_of_what_it_held)
{
    static const hitec_prog_msg_id_t k_goes[] = {
        HITEC_PROG_MSG_READ, HITEC_PROG_MSG_WRITE_OFF, HITEC_PROG_MSG_NOT_READ,
        HITEC_PROG_MSG_MUST_READ, HITEC_PROG_MSG_NO_CHANGES,
        HITEC_PROG_MSG_EDITS_PENDING, HITEC_PROG_MSG_EDIT_REFUSED,
        HITEC_PROG_MSG_REC_SAME, HITEC_PROG_MSG_REC_DONE,
        HITEC_PROG_MSG_RECORD_HELD, HITEC_PROG_MSG_SUM_OK,
        HITEC_PROG_MSG_LOAD_SAME, HITEC_PROG_MSG_LOAD_CHECK,
        HITEC_PROG_MSG_LOADED, HITEC_PROG_MSG_LOADED_RAW,
        HITEC_PROG_MSG_OTHER_SERVO, HITEC_PROG_MSG_READS_DIFFER,
    };
    static const hitec_link_phase_t k_phase[] = { HITEC_LINK_DOWN,
                                                  HITEC_LINK_NO_PAGE };
    for (size_t i = 0u; i < sizeof(k_goes) / sizeof(k_goes[0]); ++i) {
        for (size_t k = 0u; k < 2u; ++k) {
            held_stopped();
            m.msg.id = (uint8_t)k_goes[i];
            const uint32_t seq = m.msg.seq;
            f.rd.phase = k_phase[k];
            f.now_ms += 10u;
            hitec_prog_feed(&m, &f);
            CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_NONE);
            CHECK(hitec_prog_message(&m)->seq != seq);
        }
    }
}

/* What happened stays true: a write, a saved file, a refused file, a danger
 * line.  The link back then says what the page holds. */
TEST_CASE(a_session_lost_leaves_what_happened)
{
    static const hitec_prog_msg_id_t k_stays[] = {
        HITEC_PROG_MSG_WRITTEN, HITEC_PROG_MSG_RESTORED, HITEC_PROG_MSG_SAVED,
        HITEC_PROG_MSG_LOAD_LAYOUT, HITEC_PROG_MSG_STOPPED,
        HITEC_PROG_MSG_WRITE_CUT, HITEC_PROG_MSG_VERIFY_FAIL,
        HITEC_PROG_MSG_REC_MIX,
    };
    for (size_t i = 0u; i < sizeof(k_stays) / sizeof(k_stays[0]); ++i) {
        held_stopped();
        m.msg.id = (uint8_t)k_stays[i];
        f.rd.phase = HITEC_LINK_DOWN;
        f.now_ms += 10u;
        hitec_prog_feed(&m, &f);
        CHECK_EQ(hitec_prog_message(&m)->id, k_stays[i]);
    }
    held_stopped();
    m.msg.id = HITEC_PROG_MSG_READ;
    f.rd.phase = HITEC_LINK_DOWN;
    f.now_ms += 10u;
    hitec_prog_feed(&m, &f);
    f.rd.phase = HITEC_LINK_READY;
    f.rd.held = false;
    f.rd.ours = false;
    f.now_ms += 10u;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_SESSION_ENDED);
}

TEST_CASE(read_all_on_a_stopped_port_is_refused_and_clear_given_up_stays)
{
    hitec_prog_cmd_t c;

    held_stopped();
    CHECK_EQ(hitec_prog_read(&m), HITEC_PROG_MSG_STOPPED);
    /* CLEAR out, its outcome never comes: given up, its ABORT pending, the
     * port still held and stopped. */
    CHECK_EQ(hitec_prog_clear(&m), HITEC_PROG_MSG_NONE);
    c = taken();
    CHECK_EQ(c.kind, HITEC_PROG_CMD_CLEAR);
    answer(&c, HITEC_LINK_START_OK);
    f.now_ms += HITEC_PROG_RUN_MS;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_TIMEOUT);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_HELD);
    c = taken();
    CHECK_EQ(c.kind, HITEC_PROG_CMD_ABORT);
    CHECK_EQ(hitec_prog_read(&m), HITEC_PROG_MSG_STOPPED);
}

TEST_CASE(a_command_unanswered_past_its_limit_is_given_up_or_kept)
{
    hitec_prog_gate_t gate;
    hitec_prog_cmd_t c;
    hitec_link_outcome_t o;

    /* No gate: an OPEN taken and not answered in 2s may have run, so an
     * ABORT follows it. */
    hand();
    CHECK_EQ(hitec_prog_connect(&m), HITEC_PROG_MSG_NONE);
    c = taken();
    CHECK_EQ(c.kind, HITEC_PROG_CMD_OPEN);
    f.now_ms += HITEC_PROG_START_MS - 1u;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_OPENING);
    f.now_ms += 1u;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_TIMEOUT);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_PWM);
    c = taken();
    CHECK_EQ(c.kind, HITEC_PROG_CMD_ABORT);
    /* A pass the link's task claimed: the command runs, and its answer and
     * outcome are taken after the 2s. */
    hand();
    hitec_prog_gate_init(&gate);
    hitec_prog_gate_set(&m, &gate);
    CHECK_EQ(hitec_prog_connect(&m), HITEC_PROG_MSG_NONE);
    c = taken();
    CHECK(hitec_prog_gate_claim(&gate, c.id));
    f.now_ms += HITEC_PROG_START_MS + 100u;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_OPENING);
    CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_NONE);
    answer(&c, HITEC_LINK_START_OK);
    memset(&o, 0, sizeof(o));
    o.kind = HITEC_LINK_OUT_DONE;
    o.result = LINK_HITEC_RESULT_DONE;
    page_holds(false);
    outcome_of(&c, o);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_ENTERING);
}

TEST_CASE(connect_takes_the_last_port_and_pin_and_refuses_one_past)
{
    hitec_prog_cmd_t c;

    hand();
    f.target = (uint8_t)(LINK_PG_PORTS_MAX + 1u);
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_connect(&m), HITEC_PROG_MSG_NEED_TARGET);
    f.target = 1u;
    f.port_pin[0] = (uint8_t)(HITEC_PROG_PIN_LAST + 1u);
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_connect(&m), HITEC_PROG_MSG_NO_PIN);
    CHECK(!hitec_prog_take(&m, &c));
    f.port_pin[0] = (uint8_t)HITEC_PROG_PIN_LAST;
    hitec_prog_feed(&m, &f);
    CHECK_EQ(hitec_prog_connect(&m), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_pin(&m), HITEC_PROG_PIN_LAST);
}

TEST_CASE(an_abort_with_nothing_out_does_nothing)
{
    hitec_prog_cmd_t c;

    hand();
    hitec_prog_abort(&m);
    CHECK(!hitec_prog_take(&m, &c));
    CHECK_EQ(hitec_prog_message(&m)->id, HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_state(&m), HITEC_PROG_PWM);
}

/* ---------------------------------------------------- on the loopback */

TEST_CASE(a_d_series_line_with_no_servo_says_so)
{
    /* A PD mini offline: no POWER, and the servo has no supply. */
    rig_at(SUPPLY_OFFLINE, false);
    servo_d485();
    connected();
    CHECK_EQ(msg(), HITEC_PROG_MSG_NO_D);
    CHECK(!hitec_prog_in_mode(model()));
    /* LEAVE of a servo that never entered: CLOSE at once. */
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK_EQ(msg(), HITEC_PROG_MSG_LEFT);
}

TEST_CASE(a_classic_servo_that_does_not_answer_or_answers_wrong_says_so)
{
    /* No `+` within the window. */
    rig();
    servo_hs5645();
    g_drv.boot_ns = 2500000000u;
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    connected();
    CHECK_EQ(msg(), HITEC_PROG_MSG_NO_C);
    /* A byte that is not `+`. */
    rig();
    servo_hs5645();
    g_drv.boot_byte = 0x5Au;
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    connected();
    CHECK_EQ(msg(), HITEC_PROG_MSG_ENTRY_BAD);
    CHECK(!hitec_prog_in_mode(model()));
}

TEST_CASE(a_read_that_fails_on_every_try_names_the_address)
{
    rig();
    servo_d485();
    connected();
    /* Register 0x32's answer comes with a wrong sum, on every try. */
    g_drv.d_bad_sum_plus1 = 0x32u + 1u;
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ_FAIL);
    CHECK_EQ(hitec_prog_message(model())->arg, 0x32u);
    /* The servo of the last full read stays shown. */
    CHECK(hitec_prog_servo(model()) != NULL);
    g_drv.d_bad_sum_plus1 = 0u;
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
}

TEST_CASE(a_clear_refused_unsafe_and_a_clear_that_enters_again)
{
    /* Stopped at the bench, then the heartbeat untrusted: CLEAR refused. */
    rig();
    servo_d485();
    connected();
    disarm();
    prun(200u);
    g_safe = false;
    CHECK_EQ(hitec_prog_clear(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_UNSAFE);
    CHECK(hitec_prog_stopped(model()));
    /* Trusted again, and the supply went off meanwhile: CLEAR enters
     * again before it reads. */
    g_safe = true;
    g_hw.v.on = false;
    g_hw.v.asked_on = false;
    prun(50u);
    CHECK_EQ(hitec_prog_clear(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_ENTERING);
    CHECK_EQ(msg(), HITEC_PROG_MSG_CLEARED);
    settled();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
}

TEST_CASE(a_port_closed_or_taken_under_the_model_ends_its_session)
{
    hitec_link_outcome_t out;

    /* Closed by the link module's own CLOSE, not the model's. */
    rig();
    servo_d485();
    connected();
    out = run_bare(LINK_HITEC_OP_CLOSE, CH);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    until(HITEC_PROG_PWM);
    CHECK_EQ(msg(), HITEC_PROG_MSG_SESSION_ENDED);
    CHECK(!hitec_prog_in_mode(model()));
    /* Taken by another writer's command: held, and not this model's. */
    rig();
    servo_d485();
    connected();
    CHECK_EQ(other_cmd(LINK_HITEC_OP_CLEAR, CH, 0u), 0u);
    until(HITEC_PROG_OTHER);
    CHECK_EQ(msg(), HITEC_PROG_MSG_HELD_OTHER);
}

int main(void)
{
    RUN(every_refusal_of_the_page_has_its_message);
    RUN(every_outcome_kind_and_result_has_its_message);
    RUN(an_open_cut_by_a_stop_holds_the_port_stopped);
    RUN(every_answer_of_the_link_task_that_is_not_ok_has_its_message);
    RUN(a_request_without_an_outcome_is_aborted_after_its_limit);
    RUN(an_abort_before_the_command_is_taken_sends_nothing);
    RUN(command_ids_run_on_across_2_to_the_32);
    RUN(operations_are_refused_in_states_that_do_not_take_them);
    RUN(a_step_the_decoder_refuses_ends_the_entry_unknown);
    RUN(an_abort_ends_a_step_not_handed_out_and_an_open_on_its_way);
    RUN(a_release_the_page_refuses_says_why_and_keeps_the_port_held);
    RUN(a_link_back_after_a_loss_says_the_session_ended);
    RUN(a_session_lost_takes_the_lines_of_what_it_held);
    RUN(a_session_lost_leaves_what_happened);
    RUN(read_all_on_a_stopped_port_is_refused_and_clear_given_up_stays);
    RUN(a_command_unanswered_past_its_limit_is_given_up_or_kept);
    RUN(connect_takes_the_last_port_and_pin_and_refuses_one_past);
    RUN(an_abort_with_nothing_out_does_nothing);
    RUN(a_d_series_line_with_no_servo_says_so);
    RUN(a_classic_servo_that_does_not_answer_or_answers_wrong_says_so);
    RUN(a_read_that_fails_on_every_try_names_the_address);
    RUN(a_clear_refused_unsafe_and_a_clear_that_enters_again);
    RUN(a_port_closed_or_taken_under_the_model_ends_its_session);
    return test_summary("hitec_prog_codes");
}
