/*
 * The ESC programmer's model on the panel's half of the pages: the flows, the
 * gates, the messages, the session's backup, a load and a restore; and the word
 * ARM and the runs read (esc_wire_panel.c).
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_link_rig.h"
#include "esc_prog.h"
#include "esc_wire_panel.h"
#include "ew_file.h"

static esc_prog_t g_m;
static esc_prog_gate_t g_gate;
static esc_prog_feed_t g_f;
static uint32_t g_answer_id;
static uint8_t g_answer;
static uint32_t g_outcomes;
static esc_link_outcome_t g_out;
static bool g_stopped;
static bool g_armed;
static char g_text[EW_FILE_MAX];

static void carry_out(const esc_prog_cmd_t *c)
{
    esc_link_start_t a = ESC_LINK_START_ARG;

    if (!esc_prog_gate_claim(&g_gate, c->id)) {
        return;
    }
    switch (c->kind) {
    case ESC_PROG_CMD_OPEN:
        a = esc_link_open(&g_l, c->target, c->id);
        break;
    case ESC_PROG_CMD_ENTER:
        a = esc_link_enter(&g_l, &c->power, c->id);
        break;
    case ESC_PROG_CMD_POWERED:
        a = esc_link_powered(&g_l, c->id);
        break;
    case ESC_PROG_CMD_READ:
        a = esc_link_read(&g_l, c->id);
        break;
    case ESC_PROG_CMD_WRITE:
        a = esc_link_write(&g_l, c->draft, c->len, c->reads, c->read_crc, c->id);
        break;
    case ESC_PROG_CMD_RESTORE:
        a = esc_link_restore(&g_l, c->draft, c->len, c->identity, c->reads,
                             c->read_crc, c->id);
        break;
    case ESC_PROG_CMD_LEAVE:
        a = esc_link_leave(&g_l, c->id);
        break;
    case ESC_PROG_CMD_CLEAR:
        a = esc_link_clear(&g_l, c->id);
        break;
    case ESC_PROG_CMD_CLOSE:
        a = esc_link_close(&g_l, c->id);
        break;
    default:
        esc_link_abort(&g_l);
        return;
    }
    g_answer = (uint8_t)a;
    g_answer_id = c->id;
}

/* @p ms of the bench: the link's task and the frame every 5ms. */
static void model_run(uint32_t ms)
{
    static esc_prog_cmd_t c;

    for (uint32_t i = 0u; i < ms; i += 5u) {
        if (esc_prog_next_cmd(&g_m, &c)) {
            carry_out(&c);
        }
        link_run(5u);
        esc_link_outcome_t o;

        if (esc_link_outcome(&g_l, &o)) {
            g_out = o;
            ++g_outcomes;
        }
        g_f.now_ms = esc.now / 1000u;
        esc_link_readout(&g_l, &g_f.rd);
        g_f.image = *esc_link_image(&g_l);
        g_f.answer_id = g_answer_id;
        g_f.answer = g_answer;
        g_f.outcomes = g_outcomes;
        g_f.outcome = g_out;
        g_f.stopped = g_stopped;
        g_f.armed = g_armed;
        esc_prog_feed(&g_m, &g_f);
    }
}

static void model_up(void)
{
    link_up();
    esc_prog_init(&g_m);
    esc_prog_gate_init(&g_gate);
    esc_prog_gate_set(&g_m, &g_gate);
    memset(&g_f, 0, sizeof(g_f));
    g_answer_id = 0u;
    g_answer = 0u;
    g_outcomes = 0u;
    g_stopped = false;
    g_armed = false;
    model_run(10u);
}

static const esc_prog_connect_t k_conn = {
    2u, { LINK_ESC_SWITCHED, 7400u, 1000u, 3000u, 1000u }
};

static void connected(void)
{
    CHECK_EQ(esc_prog_connect(&g_m, &k_conn), ESC_PROG_MSG_NONE);
    model_run(30000u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_READ_DONE);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_READY);
    CHECK(g_f.image.gen != 0u);
}

TEST_CASE(connect_is_open_enter_read)
{
    up_am_rig();
    model_up();
    CHECK(!esc_prog_holds_port(&g_m));
    CHECK_EQ(esc_prog_connect(&g_m, &k_conn), ESC_PROG_MSG_NONE);
    /* From the ask on, ARM and the runs wait. */
    CHECK(esc_prog_holds_port(&g_m));
    CHECK_EQ(esc_prog_connect(&g_m, &k_conn), ESC_PROG_MSG_BUSY);
    model_run(30000u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_READ_DONE);
    CHECK_EQ(g.on_mv, 7400u);
    CHECK_EQ(g.on_ma, 1000u);
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_READ);
    CHECK(memcmp(g_f.image.page, esc.am, EW_MAP_LEN_AM) == 0);
    CHECK(esc_prog_holds_port(&g_m));
}

TEST_CASE(what_a_connect_refuses_with_nothing_sent)
{
    esc_prog_connect_t c = k_conn;

    up_am_rig();
    model_up();
    g_armed = true;
    model_run(5u);
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_ARMED);
    g_armed = false;
    g_stopped = true;
    model_run(5u);
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_STOPPED);
    g_stopped = false;
    model_run(5u);
    /* No voltage or limit of its own: a value out of range does not go. */
    c.power.mv = 0u;
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_RANGE);
    c.power.mv = LINK_ESC_SUPPLY_MIN_MV - 1u;
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_RANGE);
    c.power.mv = 20001u;
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_RANGE);
    c.power.mv = 7400u;
    c.power.ma = 49u;
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_RANGE);
    c.power.ma = 3001u;
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_RANGE);
    /* BY HAND switches none. */
    c.power.power = LINK_ESC_BY_HAND;
    c.power.mv = 0u;
    c.power.ma = 0u;
    CHECK_EQ(reg(LINK_EW_SEQ), 0u);
    /* A pin another writer holds. */
    open_target(2u);
    model_run(600u);
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_HELD);
    CHECK(esc_prog_holds_port(&g_m));
    /* The link. */
    esc_link_lost(&g_l);
    model_run(5u);
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_NO_LINK);
    esc_link_came_up(&g_l, ESC_LINK_MINOR - 1u);
    model_run(5u);
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_NO_PAGE);
    /* NULLs. */
    esc_prog_init(NULL);
    esc_prog_gate_set(NULL, NULL);
    esc_prog_feed(NULL, &g_f);
    esc_prog_feed(&g_m, NULL);
    CHECK_EQ(esc_prog_connect(NULL, &c), ESC_PROG_MSG_RANGE);
    CHECK_EQ(esc_prog_connect(&g_m, NULL), ESC_PROG_MSG_RANGE);
    CHECK_EQ(esc_prog_powered(NULL), ESC_PROG_MSG_BUSY);
    CHECK_EQ(esc_prog_read(NULL), ESC_PROG_MSG_BUSY);
    CHECK_EQ(esc_prog_write(NULL, g_f.image.page, 1u), ESC_PROG_MSG_BUSY);
    CHECK_EQ(esc_prog_leave(NULL), ESC_PROG_MSG_BUSY);
    CHECK_EQ(esc_prog_clear(NULL), ESC_PROG_MSG_BUSY);
    CHECK_EQ(esc_prog_close(NULL), ESC_PROG_MSG_BUSY);
    esc_prog_abort(NULL);
    CHECK_EQ(esc_prog_message(NULL, NULL), ESC_PROG_MSG_NONE);
    CHECK(!esc_prog_holds_port(NULL));
    CHECK(!esc_prog_port_taken(NULL));
    CHECK(!esc_prog_next_cmd(NULL, NULL));
}

TEST_CASE(a_failure_on_the_way_says_what_failed)
{
    uint16_t detail = 0xFFFFu;

    up_am_rig();
    model_up();
    /* The output's pin: none there. */
    esc_prog_connect_t c = k_conn;

    c.target = 3u;
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_NONE);
    model_run(100u);
    CHECK_EQ(esc_prog_message(&g_m, &detail), ESC_PROG_MSG_REFUSED);
    CHECK_EQ(detail, LINK_ESC_REF_NO_PIN);
    CHECK(!esc_prog_holds_port(&g_m));
    /* No line. */
    g.take_ok = false;
    CHECK_EQ(esc_prog_connect(&g_m, &k_conn), ESC_PROG_MSG_NONE);
    model_run(200u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_NO_LINE);
    g.take_ok = true;
    /* The supply refuses ON. */
    g.refuse_on = true;
    CHECK_EQ(esc_prog_connect(&g_m, &k_conn), ESC_PROG_MSG_NONE);
    model_run(30000u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_SUPPLY);
    CHECK_EQ(esc_link_faults(&g_l), 1u);
    g.refuse_on = false;
    /* CLEAR moves no result, and comes back well. */
    CHECK_EQ(esc_prog_clear(&g_m), ESC_PROG_MSG_NONE);
    model_run(50u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_CLEARED);
    CHECK(!flag(LINK_EW_F_FAULT));
    /* The session: no answer. */
    sim_script(&esc, F_DEAD, 0, 1, 0u);
    esc_prog_connect_t again = k_conn;

    CHECK_EQ(esc_prog_close(&g_m), ESC_PROG_MSG_NONE);
    model_run(50u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_LEFT);
    CHECK_EQ(esc_prog_connect(&g_m, &again), ESC_PROG_MSG_NONE);
    model_run(30000u);
    CHECK_EQ(esc_prog_message(&g_m, &detail), ESC_PROG_MSG_SESSION);
    CHECK(detail != 0u);
}

TEST_CASE(by_hand_stops_for_the_operator)
{
    esc_prog_connect_t c = k_conn;

    up_am_rig();
    model_up();
    c.power.power = LINK_ESC_BY_HAND;
    c.power.mv = 0u;
    c.power.ma = 0u;
    c.power.hold_ms = 500u;
    CHECK_EQ(esc_prog_powered(&g_m), ESC_PROG_MSG_NOT_IN_MODE);
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_NONE);
    model_run(500u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_HAND_WAIT);
    CHECK_EQ(g.ons, 0);
    CHECK_EQ(esc_prog_powered(&g_m), ESC_PROG_MSG_NONE);
    model_run(20000u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_READ_DONE);
    /* No DONE in time. */
    CHECK_EQ(esc_prog_leave(&g_m), ESC_PROG_MSG_NONE);
    model_run(2000u);
    CHECK_EQ(esc_prog_connect(&g_m, &c), ESC_PROG_MSG_NONE);
    model_run(61000u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_TIMEOUT);
}

TEST_CASE(a_write_needs_a_read_a_change_and_the_backup)
{
    static uint8_t draft[ESC_LINK_PAGE_MAX];

    up_am_rig();
    model_up();
    CHECK_EQ(esc_prog_read(&g_m), ESC_PROG_MSG_NOT_IN_MODE);
    CHECK_EQ(esc_prog_write(&g_m, draft, 1u), ESC_PROG_MSG_NOT_IN_MODE);
    connected();
    memcpy(draft, g_f.image.page, g_f.image.len);
    CHECK_EQ(esc_prog_write(&g_m, draft, g_f.image.len), ESC_PROG_MSG_NO_CHANGE);
    draft[0x18] = 48u;
    /* The backup: captured by the read, and it goes first. */
    CHECK_EQ(esc_prog_backup_state(&g_m), ESC_PROG_BACKUP_OWED);
    CHECK_EQ(esc_prog_write(&g_m, draft, g_f.image.len),
             ESC_PROG_MSG_BACKUP_FIRST);
    CHECK_EQ(esc.programs, 0);
    const size_t n = esc_prog_backup_text(&g_m, g_text, sizeof(g_text));

    CHECK(n != 0u);
    CHECK(strncmp(g_text, "ESC settings backup, format 1", 29u) == 0);
    esc_prog_backup_settled(&g_m);
    CHECK_EQ(esc_prog_backup_state(&g_m), ESC_PROG_BACKUP_SETTLED);
    CHECK_EQ(esc_prog_write(&g_m, draft, (uint16_t)(g_f.image.len - 1u)), ESC_PROG_MSG_STALE);
    g_armed = true;
    model_run(5u);
    CHECK_EQ(esc_prog_write(&g_m, draft, g_f.image.len), ESC_PROG_MSG_ARMED);
    g_armed = false;
    model_run(5u);
    CHECK_EQ(esc_prog_write(&g_m, draft, g_f.image.len), ESC_PROG_MSG_NONE);
    CHECK_EQ(esc_prog_write(&g_m, draft, g_f.image.len), ESC_PROG_MSG_BUSY);
    model_run(20000u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_WRITE_DONE);
    CHECK_EQ(esc.am[0x18], 48u);
    /* The backup stays the first read's, and stays settled in the session. */
    CHECK_EQ(esc_prog_backup_state(&g_m), ESC_PROG_BACKUP_SETTLED);
    CHECK(g_m.file.page[0x18] != 48u);
    /* A read again. */
    CHECK_EQ(esc_prog_read(&g_m), ESC_PROG_MSG_NONE);
    model_run(20000u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_READ_DONE);
    /* Leave: to the application, the pin back, and no backup is left. */
    CHECK_EQ(esc_prog_leave(&g_m), ESC_PROG_MSG_NONE);
    model_run(2000u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_LEFT);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    CHECK_EQ(esc_prog_backup_state(&g_m), ESC_PROG_BACKUP_NONE);
    CHECK_EQ(esc_prog_backup_text(&g_m, g_text, sizeof(g_text)), 0u);
    CHECK(!esc_prog_holds_port(&g_m));
}

TEST_CASE(a_page_that_is_not_written_says_so)
{
    static uint8_t draft[ESC_LINK_PAGE_MAX];

    /* An AM32 of a revision that is not written. */
    sim_init_am(&esc, 5u, 2u);
    rig_up();
    model_up();
    CHECK_EQ(esc_prog_connect(&g_m, &k_conn), ESC_PROG_MSG_NONE);
    model_run(30000u);
    if (esc_port_state(&g_p) == LINK_ESC_READY) {
        memcpy(draft, g_f.image.page, g_f.image.len);
        draft[0x18] ^= 1u;
        CHECK_EQ(esc_prog_write(&g_m, draft, g_f.image.len),
                 ESC_PROG_MSG_READ_ONLY);
    }
    CHECK_EQ(esc.programs, 0);
}

TEST_CASE(a_file_loads_into_a_draft_or_says_why_not)
{
    uint16_t detail = 0u;
    uint16_t len = 0u;
    unsigned line = 0u;
    static uint8_t draft[ESC_LINK_PAGE_MAX];

    up_am_rig();
    model_up();
    CHECK(esc_prog_file_draft(&g_m, &len) == NULL);
    connected();
    size_t n = esc_prog_backup_text(&g_m, g_text, sizeof(g_text));

    CHECK(n != 0u);
    /* Its own file changes nothing, and is a draft. */
    CHECK_EQ(esc_prog_file_load(&g_m, g_text, n, &line), ESC_PROG_MSG_LOADED);
    CHECK_EQ(esc_prog_message(&g_m, &detail), ESC_PROG_MSG_LOADED);
    CHECK_EQ(detail, 0u);
    const uint8_t *d = esc_prog_file_draft(&g_m, &len);

    CHECK(d != NULL);
    CHECK_EQ(len, EW_MAP_LEN_AM);
    CHECK(memcmp(d, esc.am, len) == 0);
    /* Cut: refused, and no draft. */
    CHECK_EQ(esc_prog_file_load(&g_m, g_text, n / 2u, &line),
             ESC_PROG_MSG_FILE_REFUSED);
    CHECK_EQ(esc_prog_message(&g_m, &detail), ESC_PROG_MSG_FILE_REFUSED);
    CHECK_EQ(detail, EW_FILE_ERR_TRUNCATED);
    CHECK(esc_prog_file_draft(&g_m, &len) == NULL);
    /* Another layout: refused by the load. */
    static ew_file_t other;

    other = g_m.file;
    other.layout = 3u;
    other.page[1] = 3u;
    n = ew_file_format(&other, g_text, sizeof(g_text));
    CHECK(n != 0u);
    CHECK_EQ(esc_prog_file_load(&g_m, g_text, n, NULL),
             ESC_PROG_MSG_LOAD_REFUSED);
    CHECK_EQ(esc_prog_message(&g_m, &detail), ESC_PROG_MSG_LOAD_REFUSED);
    CHECK_EQ(detail, EW_LOAD_LAYOUT);
    /* NULLs. */
    CHECK_EQ(esc_prog_file_load(NULL, g_text, 1u, NULL), ESC_PROG_MSG_FILE_REFUSED);
    CHECK_EQ(esc_prog_file_load(&g_m, NULL, 1u, NULL), ESC_PROG_MSG_FILE_REFUSED);
    CHECK_EQ(esc_prog_file_restore(NULL, g_text, 1u), ESC_PROG_MSG_FILE_REFUSED);
    CHECK_EQ(esc_prog_file_restore(&g_m, NULL, 1u), ESC_PROG_MSG_FILE_REFUSED);
    CHECK(esc_prog_file_draft(NULL, &len) == NULL);
    CHECK_EQ(esc_prog_backup_state(NULL), ESC_PROG_BACKUP_NONE);
    CHECK_EQ(esc_prog_backup_text(NULL, g_text, 1u), 0u);
    esc_prog_backup_follow(NULL);
    esc_prog_backup_settled(NULL);
    (void)draft;
}

TEST_CASE(an_erased_am32_takes_only_a_restore)
{
    uint16_t detail = 0u;
    static uint8_t draft[ESC_LINK_PAGE_MAX];
    static uint8_t good[EW_MAP_LEN_AM];

    up_am_rig();
    model_up();
    connected();
    memcpy(good, esc.am, sizeof(good));
    size_t n = esc_prog_backup_text(&g_m, g_text, sizeof(g_text));

    CHECK(n != 0u);
    /* Not erased: a restore is refused. */
    CHECK_EQ(esc_prog_file_restore(&g_m, g_text, n),
             ESC_PROG_MSG_RESTORE_REFUSED);
    CHECK_EQ(esc_prog_message(&g_m, &detail), ESC_PROG_MSG_RESTORE_REFUSED);
    CHECK_EQ(detail, EW_RESTORE_NOT_ERASED);
    /* Erased, next session. */
    CHECK_EQ(esc_prog_leave(&g_m), ESC_PROG_MSG_NONE);
    model_run(2000u);
    memset(esc.am, 0xFF, sizeof(good));
    connected();
    CHECK(flag(LINK_EW_F_ERASED));
    memset(draft, 0x11, sizeof(draft));
    CHECK_EQ(esc_prog_write(&g_m, draft, EW_MAP_LEN_AM), ESC_PROG_MSG_ERASED);
    /* The cut file is refused, the whole one goes. */
    CHECK_EQ(esc_prog_file_restore(&g_m, g_text, n / 2u),
             ESC_PROG_MSG_FILE_REFUSED);
    CHECK_EQ(esc_prog_file_restore(&g_m, g_text, n), ESC_PROG_MSG_NONE);
    CHECK_EQ(esc_prog_file_restore(&g_m, g_text, n), ESC_PROG_MSG_BUSY);
    model_run(20000u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_RESTORE_DONE);
    CHECK_EQ(esc.am[0], 0x01u);
}

TEST_CASE(a_stop_or_lost_link_ends_a_flow)
{
    up_am_rig();
    model_up();
    CHECK_EQ(esc_prog_connect(&g_m, &k_conn), ESC_PROG_MSG_NONE);
    model_run(300u);
    /* The stop key: the link's own, as the control task has it. */
    esc_link_stop(&g_l);
    model_run(20000u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_CUT);
    CHECK(flag(LINK_EW_F_STOPPED));
    /* The way back: CLEAR, or a close. */
    CHECK_EQ(esc_prog_close(&g_m), ESC_PROG_MSG_NONE);
    model_run(50u);
    CHECK_EQ(esc_port_state(&g_p), LINK_ESC_FREE);
    /* The link goes in a flow. */
    CHECK_EQ(esc_prog_connect(&g_m, &k_conn), ESC_PROG_MSG_NONE);
    model_run(300u);
    esc_link_lost(&g_l);
    model_run(10u);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_NO_ANSWER);
    /* ABORT goes past whatever waits. */
    esc_link_came_up(&g_l, ESC_LINK_MINOR);
    model_run(20u);
    esc_prog_abort(&g_m);
    model_run(20u);
    CHECK_EQ(reg(LINK_EW_OP), LINK_ESC_OP_ABORT);
}

TEST_CASE(a_command_the_task_never_took_is_given_up)
{
    static esc_prog_cmd_t c;

    up_am_rig();
    model_up();
    CHECK_EQ(esc_prog_connect(&g_m, &k_conn), ESC_PROG_MSG_NONE);
    CHECK(esc_prog_next_cmd(&g_m, &c));
    CHECK(!esc_prog_next_cmd(&g_m, &c));
    /* It lies in the queue past its time: taken back, and not carried out. */
    g_f.now_ms += ESC_PROG_CMD_WAIT_MS;
    esc_prog_feed(&g_m, &g_f);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_NO_ANSWER);
    CHECK(!esc_prog_gate_claim(&g_gate, c.id));
    CHECK(!esc_prog_gate_claim(&g_gate, 0u));
    /* The link's answer to one it could not take. */
    CHECK_EQ(esc_prog_connect(&g_m, &k_conn), ESC_PROG_MSG_NONE);
    CHECK(esc_prog_next_cmd(&g_m, &c));
    g_answer_id = c.id;
    g_answer = ESC_LINK_START_BUSY;
    g_f.answer_id = c.id;
    g_f.answer = ESC_LINK_START_BUSY;
    esc_prog_feed(&g_m, &g_f);
    CHECK_EQ(esc_prog_message(&g_m, NULL), ESC_PROG_MSG_BUSY);
    CHECK(!esc_prog_holds_port(&g_m));
}

TEST_CASE(the_word_of_arm_and_the_runs)
{
    static esc_prog_feed_t f;
    static esc_prog_gate_t g2;
    static esc_prog_cmd_t c;

    esc_prog_gate_init(&g2);
    esc_wire_panel_gate(&g2);
    CHECK(!esc_wire_panel_holds_port());
    CHECK(!esc_wire_panel_next_cmd(&c));
    memset(&f, 0, sizeof(f));
    f.rd.known = true;
    f.rd.held = true;
    esc_wire_panel_feed(&f);
    CHECK(esc_wire_panel_holds_port());
    f.rd.held = false;
    esc_wire_panel_feed(&f);
    CHECK(!esc_wire_panel_holds_port());
}

int main(void)
{
    RUN(connect_is_open_enter_read);
    RUN(what_a_connect_refuses_with_nothing_sent);
    RUN(a_failure_on_the_way_says_what_failed);
    RUN(by_hand_stops_for_the_operator);
    RUN(a_write_needs_a_read_a_change_and_the_backup);
    RUN(a_page_that_is_not_written_says_so);
    RUN(a_file_loads_into_a_draft_or_says_why_not);
    RUN(an_erased_am32_takes_only_a_restore);
    RUN(a_stop_or_lost_link_ends_a_flow);
    RUN(a_command_the_task_never_took_is_given_up);
    RUN(the_word_of_arm_and_the_runs);
    return test_summary("esc_prog");
}
