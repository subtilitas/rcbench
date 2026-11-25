/*
 * Programming for YGE ESCs on the panel's half of the pages,
 * shared/bench/yge_prog.c and yge_prog_file.c: CONNECT as OPEN, ENTER and
 * READ with every refusal that sends nothing; BY HAND; ARM refused while the
 * port is held, and the band's port; a stop latched lets only CLOSE, LEAVE
 * and ABORT through; the first write of a session saves the backup first,
 * without a card keeps it in RAM, and a card silent 10000ms sends nothing; a
 * draft the guard refuses sends nothing; a file loaded against the read copy.
 *
 * SPDX-License-Identifier: MIT
 */
#include "arming.h"
#include "heartbeat.h"
#include "prog5v_box.h"
#include "yge_prog_rig.h"

TEST_CASE(connect_is_open_enter_read)
{
    rig_up();
    model_up();
    CHECK(!yge_prog_holds_port(&g_m));
    CHECK(yge_prog_idle(&g_m));
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_NONE);
    /* From the ask on, ARM and the runs wait. */
    CHECK(yge_prog_holds_port(&g_m));
    CHECK(yge_prog_busy(&g_m));
    CHECK(!yge_prog_idle(&g_m));
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_BUSY);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_READ_DONE);
    CHECK(!yge_prog_busy(&g_m));
    CHECK_EQ(g.on_mv, 7400u);
    CHECK_EQ(g.on_ma, 1000u);
    CHECK_EQ(g_commands, 3);
    CHECK_EQ(g_cmd_op[0], LINK_YGE_OP_OPEN);
    CHECK_EQ(g_cmd_op[1], LINK_YGE_OP_ENTER);
    CHECK_EQ(g_cmd_key[1], LINK_YGE_KEY);
    CHECK_EQ(g_cmd_op[2], LINK_YGE_OP_READ);
    CHECK_EQ(g_cmd_key[2], 0u);
    CHECK_EQ(g_f.image.entries, 32u);
    CHECK_EQ(yge_prog_backup_where(&g_m), YGE_PROG_BACKUP_HELD);
    /* LEAVE, then CLOSE: the pin back, the supply off. */
    CHECK_EQ(yge_prog_leave(&g_m), YGE_PROG_MSG_NONE);
    model_run(500u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_LEFT);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK(!g.v.asked_on);
    CHECK(!yge_prog_holds_port(&g_m));
    CHECK_EQ(yge_prog_backup_where(&g_m), YGE_PROG_BACKUP_NONE);
}

TEST_CASE(every_refusal_of_a_connect_sends_nothing)
{
    yge_prog_connect_t c;

    rig_up();
    model_up();
    yge_prog_limits(&g_m, 8400u, 2000u);
    static const struct {
        uint8_t port, power;
        uint16_t mv, ma, boot;
    } bad[] = {
        { 4u, LINK_YGE_SWITCHED, 7400u, 1000u, 1000u },
        { 0u, LINK_YGE_POWER_COUNT, 7400u, 1000u, 1000u },
        { 0u, LINK_YGE_SWITCHED, 2999u, 1000u, 1000u },
        { 0u, LINK_YGE_SWITCHED, 8401u, 1000u, 1000u },
        { 0u, LINK_YGE_SWITCHED, 7400u, 49u, 1000u },
        { 0u, LINK_YGE_SWITCHED, 7400u, 2001u, 1000u },
        { 0u, LINK_YGE_SWITCHED, 7400u, 1000u, 999u },
        { 0u, LINK_YGE_BY_HAND, 0u, 0u, 20001u },
    };
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        c.port = bad[i].port;
        c.power.power = bad[i].power;
        c.power.mv = bad[i].mv;
        c.power.ma = bad[i].ma;
        c.power.boot_ms = bad[i].boot;
        CHECK_EQ(yge_prog_connect(&g_m, &c), YGE_PROG_MSG_RANGE);
    }
    yge_prog_limits(&g_m, 0u, 0u);
    c = k_conn;
    c.power.mv = 20001u;
    CHECK_EQ(yge_prog_connect(&g_m, &c), YGE_PROG_MSG_RANGE);
    c.power.mv = 7400u;
    c.power.ma = 3001u;
    CHECK_EQ(yge_prog_connect(&g_m, &c), YGE_PROG_MSG_RANGE);
    CHECK_EQ(yge_prog_connect(&g_m, NULL), YGE_PROG_MSG_RANGE);
    g_armed = true;
    model_run(5u);
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_ARMED);
    g_armed = false;
    g_stopped = true;
    model_run(5u);
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_STOPPED);
    g_stopped = false;
    model_run(5u);
    /* A page another writer holds. */
    open_port(2u);
    model_run(600u);
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_HELD);
    CHECK_EQ(yge_prog_read(&g_m), YGE_PROG_MSG_NOT_READY);
    CHECK_EQ(refusal(), LINK_YGE_REF_NONE);
    model_run(100u);
    CHECK_EQ(g_commands, 0);
    CHECK(yge_prog_holds_port(&g_m));
    /* No IOmcu, an older one. */
    yge_link_lost(&g_l);
    model_run(5u);
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_NO_LINK);
    CHECK(yge_prog_idle(&g_m));
    yge_link_came_up(&g_l, YGE_LINK_MINOR - 1u);
    model_run(5u);
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_NO_PAGE);
    CHECK_EQ(g_commands, 0);
}

TEST_CASE(by_hand_waits_for_the_battery)
{
    yge_prog_connect_t c = k_conn;

    rig_up();
    model_up();
    c.power.power = LINK_YGE_BY_HAND;
    c.power.mv = 0u;
    c.power.ma = 0u;
    CHECK_EQ(yge_prog_hand_done(&g_m), YGE_PROG_MSG_NOT_READY);
    CHECK_EQ(yge_prog_connect(&g_m, &c), YGE_PROG_MSG_NONE);
    model_run(500u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_HAND_WAIT);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_HAND_WAIT);
    CHECK_EQ(g.ons, 0);
    CHECK(!g.listen);
    g.battery = true;
    CHECK_EQ(yge_prog_hand_done(&g_m), YGE_PROG_MSG_NONE);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_READ_DONE);
    CHECK_EQ(g.ons, 0);
    CHECK((g_f.rd.flags & LINK_YP_F_BY_HAND) != 0u);
}

/* ARM refused from the ask of CONNECT to the pin back, whoever holds it; the
 * band names the port. */
TEST_CASE(arm_is_refused_while_the_port_is_held)
{
    arming_t a;
    const uint32_t t0 = 1000u;
    const uint32_t at = t0 + HEARTBEAT_SETTLE_MS;

    rig_up();
    model_up();
    CHECK_EQ(yge_prog_taken_port(&g_f.rd), -1);
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_NONE);
    CHECK(yge_prog_holds_port(&g_m));
    model_run(30000u);
    CHECK(yge_prog_port_taken(&g_f.rd));
    CHECK_EQ(yge_prog_taken_port(&g_f.rd), 0);
    CHECK_EQ(prog5v_box_held_port(-1, yge_prog_taken_port(&g_f.rd), true), 0u);
    /* The control task's arm with the port taken. */
    arming_init(&a, t0 - 10u, HEARTBEAT_SETTLE_MS);
    arming_set_line_wait(&a, ARMING_LINE_WAIT_MS);
    arming_touch_seen(&a, t0);
    arming_request_arm_ask(&a, t0, 1u, 1u);
    arming_touch_seen(&a, at);
    arming_line_report(&a, true);
    CHECK_EQ(arming_step(&a, at), ARMING_ACT_ARM);
    arming_hs_facts_t facts = { .link_up = true, .rate_settled = true,
                                .port_taken = yge_prog_port_taken(&g_f.rd) };

    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_HANDED, &facts, at),
             ARMING_HS_PROG);
    /* The IOmcu refuses it as well, whatever the panel does. */
    CHECK(yge_port_quiet_mask(&g_p) != 0u);
    /* The pin back: no more hold. */
    CHECK_EQ(yge_prog_leave(&g_m), YGE_PROG_MSG_NONE);
    model_run(500u);
    CHECK(!yge_prog_port_taken(&g_f.rd));
    CHECK(!yge_prog_holds_port(&g_m));
    CHECK_EQ(yge_prog_taken_port(&g_f.rd), -1);
    CHECK_EQ(prog5v_box_held_port(-1, yge_prog_taken_port(&g_f.rd), false),
             PROG5V_HELD_NONE);
}

TEST_CASE(a_stop_latched_lets_only_close_leave_and_abort_through)
{
    rig_up();
    for (uint8_t k = 0u; k <= (uint8_t)YGE_PROG_CMD_ABORT; ++k) {
        const bool through = k == YGE_PROG_CMD_CLOSE || k == YGE_PROG_CMD_LEAVE
                             || k == YGE_PROG_CMD_ABORT;

        CHECK(yge_prog_cmd_allowed(k, false));
        CHECK_EQ(yge_prog_cmd_allowed(k, true), through);
    }
    model_up();
    connected();
    g_stopped = true;
    model_run(5u);
    CHECK_EQ(yge_prog_read(&g_m), YGE_PROG_MSG_STOPPED);
    CHECK_EQ(yge_prog_clear(&g_m), YGE_PROG_MSG_STOPPED);
    CHECK_EQ(yge_prog_write(&g_m, draft_with(21u, 13u), 32u),
             YGE_PROG_MSG_STOPPED);
    CHECK_EQ(yge_prog_enter_again(&g_m, &k_conn), YGE_PROG_MSG_STOPPED);
    CHECK_EQ(yge_prog_hand_done(&g_m), YGE_PROG_MSG_STOPPED);
    /* A command made before the stop is refused at the link's task. */
    g_stopped = false;
    model_run(5u);
    CHECK_EQ(yge_prog_read(&g_m), YGE_PROG_MSG_NONE);
    g_stopped = true;
    const int before = g_commands;

    model_run(100u);
    CHECK_EQ(g_commands, before);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_BUSY);
    /* LEAVE goes under the stop, and the pin comes back. */
    CHECK_EQ(yge_prog_leave(&g_m), YGE_PROG_MSG_NONE);
    model_run(500u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_LEFT);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_FREE);
    CHECK_EQ(commands_of(LINK_YGE_OP_LEAVE), 1);
    CHECK_EQ(commands_of(LINK_YGE_OP_CLOSE), 1);
}

TEST_CASE(the_first_write_saves_the_backup_first)
{
    oy_file_t f;
    size_t len = 0u;

    rig_up();
    model_up();
    connected();
    CHECK(!yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                              sizeof(g_tag), &g_card_id));
    CHECK_EQ(yge_prog_write(&g_m, draft_with(21u, 13u), 32u),
             YGE_PROG_MSG_NONE);
    model_run(100u);
    CHECK_EQ(commands_of(LINK_YGE_OP_WRITE), 0);
    CHECK(yge_prog_busy(&g_m));
    CHECK(yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                             sizeof(g_tag), &g_card_id));
    CHECK(!yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                              sizeof(g_tag), &g_card_id));
    CHECK_EQ(strcmp(g_tag, "5712"), 0);
    CHECK_EQ(oy_file_parse(g_text, len, &f, NULL), OY_FILE_OK);
    CHECK_EQ(f.value[21], sim.table[21]);
    CHECK_EQ(f.value[21], 12u);
    yge_prog_card_done(&g_m, g_card_id, YGE_PROG_CARD_OK);
    CHECK_EQ(yge_prog_backup_where(&g_m), YGE_PROG_BACKUP_CARD);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_WRITE_DONE);
    CHECK_EQ(commands_of(LINK_YGE_OP_WRITE), 1);
    CHECK_EQ(sim.table[21], 13u);
    /* The second write of the session goes at once. */
    CHECK_EQ(yge_prog_write(&g_m, draft_with(21u, 14u), 32u),
             YGE_PROG_MSG_NONE);
    CHECK(!yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                              sizeof(g_tag), &g_card_id));
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_WRITE_DONE);
    CHECK_EQ(sim.table[21], 14u);
    /* The backup is the table as the session found it. */
    len = yge_prog_backup_text(&g_m, g_text, sizeof(g_text));
    CHECK_EQ(oy_file_parse(g_text, len, &f, NULL), OY_FILE_OK);
    CHECK_EQ(f.value[21], 12u);
}

TEST_CASE(no_card_keeps_the_old_values_in_ram_and_writes)
{
    size_t len = 0u;
    uint16_t detail = 0u;

    rig_up();
    model_up();
    connected();
    CHECK_EQ(yge_prog_write(&g_m, draft_with(21u, 13u), 32u),
             YGE_PROG_MSG_NONE);
    CHECK(yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                             sizeof(g_tag), &g_card_id));
    yge_prog_card_done(&g_m, g_card_id, YGE_PROG_CARD_NO_CARD);
    CHECK_EQ(yge_prog_message(&g_m, &detail), YGE_PROG_MSG_BACKUP_RAM);
    CHECK_EQ(detail, YGE_PROG_CARD_NO_CARD);
    CHECK_EQ(yge_prog_backup_where(&g_m), YGE_PROG_BACKUP_RAM);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_WRITE_DONE);
    CHECK_EQ(sim.table[21], 13u);
    CHECK_EQ(yge_prog_backup_where(&g_m), YGE_PROG_BACKUP_RAM);
    CHECK(yge_prog_backup_text(&g_m, g_text, sizeof(g_text)) != 0u);
}

/* The card silent: at 9999ms the write still waits, at 10000ms it goes
 * nowhere; a late answer sends nothing. */
TEST_CASE(a_card_silent_10000ms_sends_no_write)
{
    size_t len = 0u;
    yge_prog_feed_t f;

    rig_up();
    model_up();
    connected();
    CHECK_EQ(yge_prog_write(&g_m, draft_with(21u, 13u), 32u),
             YGE_PROG_MSG_NONE);
    CHECK(yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                             sizeof(g_tag), &g_card_id));
    f = g_f;
    f.now_ms = g_m.card_ms + YGE_PROG_CARD_WAIT_MS - 1u;
    yge_prog_feed(&g_m, &f);
    CHECK(yge_prog_busy(&g_m));
    f.now_ms = g_m.card_ms + YGE_PROG_CARD_WAIT_MS;
    yge_prog_feed(&g_m, &f);
    CHECK(!yge_prog_busy(&g_m));
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_NO_ANSWER);
    yge_prog_card_done(&g_m, g_card_id, YGE_PROG_CARD_OK);
    model_run(500u);
    CHECK_EQ(commands_of(LINK_YGE_OP_WRITE), 0);
    CHECK_EQ(yge_prog_backup_where(&g_m), YGE_PROG_BACKUP_CARD);
    CHECK_EQ(sim.table[21], 12u);
}

/* A card answer still out when its session ended: dropped and counted, never
 * the backup of the next session, which asks the card for its own. */
TEST_CASE(a_late_card_answer_of_an_ended_session_is_dropped)
{
    size_t len = 0u;
    yge_prog_feed_t f;

    rig_up();
    model_up();
    connected();
    CHECK_EQ(yge_prog_write(&g_m, draft_with(21u, 13u), 32u),
             YGE_PROG_MSG_NONE);
    CHECK(yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                             sizeof(g_tag), &g_card_id));
    const uint32_t old_id = g_card_id;

    f = g_f;
    f.now_ms = g_m.card_ms + YGE_PROG_CARD_WAIT_MS;
    yge_prog_feed(&g_m, &f);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_NO_ANSWER);
    CHECK_EQ(yge_prog_leave(&g_m), YGE_PROG_MSG_NONE);
    model_run(500u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_LEFT);
    connected();
    CHECK_EQ(yge_prog_write(&g_m, draft_with(21u, 13u), 32u),
             YGE_PROG_MSG_NONE);
    CHECK(yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                             sizeof(g_tag), &g_card_id));
    CHECK(g_card_id != old_id);
    /* The old answer, and one with no number. */
    yge_prog_card_done(&g_m, old_id, YGE_PROG_CARD_OK);
    yge_prog_card_done(&g_m, 0u, YGE_PROG_CARD_OK);
    CHECK_EQ(yge_prog_card_stale(&g_m), 2u);
    CHECK_EQ(yge_prog_card_stale(NULL), 0u);
    CHECK_EQ(yge_prog_backup_where(&g_m), YGE_PROG_BACKUP_HELD);
    CHECK(yge_prog_busy(&g_m));
    model_run(500u);
    CHECK_EQ(commands_of(LINK_YGE_OP_WRITE), 0);
    /* Its own answer counts. */
    yge_prog_card_done(&g_m, g_card_id, YGE_PROG_CARD_NO_CARD);
    CHECK_EQ(yge_prog_backup_where(&g_m), YGE_PROG_BACKUP_RAM);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_WRITE_DONE);
    CHECK_EQ(commands_of(LINK_YGE_OP_WRITE), 1);
    /* Answered once: the same number again is stale. */
    yge_prog_card_done(&g_m, g_card_id, YGE_PROG_CARD_OK);
    CHECK_EQ(yge_prog_card_stale(&g_m), 3u);
    CHECK_EQ(yge_prog_backup_where(&g_m), YGE_PROG_BACKUP_RAM);
}

TEST_CASE(a_stop_while_the_card_saves_sends_no_write)
{
    size_t len = 0u;

    rig_up();
    model_up();
    connected();
    CHECK_EQ(yge_prog_write(&g_m, draft_with(21u, 13u), 32u),
             YGE_PROG_MSG_NONE);
    CHECK(yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                             sizeof(g_tag), &g_card_id));
    g_stopped = true;
    model_run(5u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_STOPPED);
    CHECK(!yge_prog_busy(&g_m));
    g_stopped = false;
    yge_prog_card_done(&g_m, g_card_id, YGE_PROG_CARD_OK);
    model_run(500u);
    CHECK_EQ(commands_of(LINK_YGE_OP_WRITE), 0);
    CHECK_EQ(sim.table[21], 12u);
}

TEST_CASE(every_refusal_of_a_write_sends_nothing)
{
    uint16_t detail = 0u;
    size_t len = 0u;

    rig_up();
    model_up();
    CHECK_EQ(yge_prog_write(&g_m, NULL, 32u), YGE_PROG_MSG_BUSY);
    CHECK_EQ(yge_prog_write(&g_m, draft_with(21u, 13u), 32u),
             YGE_PROG_MSG_NOT_READY);
    connected();
    /* The guard's word, here, before anything goes. */
    CHECK_EQ(yge_prog_write(&g_m, draft_with(25u, 1u), 32u), YGE_PROG_MSG_GUARD);
    (void)yge_prog_message(&g_m, &detail);
    CHECK_EQ(detail, OY_REF_TELEMETRY);
    CHECK_EQ(yge_prog_bad(&g_m), 25u);
    CHECK_EQ(yge_prog_write(&g_m, draft_with(11u, 1u), 32u), YGE_PROG_MSG_GUARD);
    CHECK_EQ(yge_prog_bad(&g_m), 11u);
    CHECK_EQ(yge_prog_write(&g_m, draft_with(21u, 0u), 32u), YGE_PROG_MSG_GUARD);
    (void)yge_prog_message(&g_m, &detail);
    CHECK_EQ(detail, OY_REF_VALUE);
    CHECK_EQ(yge_prog_write(&g_m, g_f.image.value, 32u), YGE_PROG_MSG_NO_CHANGE);
    CHECK_EQ(yge_prog_write(&g_m, draft_with(21u, 13u), 31u),
             YGE_PROG_MSG_STALE);
    g_armed = true;
    model_run(5u);
    CHECK_EQ(yge_prog_write(&g_m, draft_with(21u, 13u), 32u),
             YGE_PROG_MSG_ARMED);
    g_armed = false;
    model_run(100u);
    CHECK(!yge_prog_card_take(&g_m, g_text, sizeof(g_text), &len, g_tag,
                              sizeof(g_tag), &g_card_id));
    CHECK_EQ(commands_of(LINK_YGE_OP_WRITE), 0);
    CHECK(!yge_prog_busy(&g_m));
}

TEST_CASE(a_read_only_table_is_not_written)
{
    uint16_t detail = 0u;
    uint16_t d[YGE_LINK_VALUES_MAX];

    rig_up();
    sim.table[12] = 1000u;  /* V0.01000: below the lowest writable firmware */
    sim.table[13] = 0u;
    model_up();
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_NONE);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_READ_DONE);
    memcpy(d, g_f.image.value, sizeof(d));
    d[21] = 13u;
    CHECK_EQ(yge_prog_write(&g_m, d, 32u), YGE_PROG_MSG_READ_ONLY);
    (void)yge_prog_message(&g_m, &detail);
    CHECK_EQ(detail, OY_REF_RO_FIRMWARE);
    model_run(100u);
    CHECK_EQ(commands_of(LINK_YGE_OP_WRITE), 0);
}

TEST_CASE(a_file_against_the_read_copy)
{
    unsigned line = 0u;
    uint16_t n = 0u;
    uint16_t detail = 0u;
    uint16_t t[YGE_LINK_VALUES_MAX];

    rig_up();
    model_up();
    CHECK_EQ(yge_prog_file_load(&g_m, NULL, 0u, NULL), YGE_PROG_MSG_FILE_REFUSED);
    CHECK_EQ(yge_prog_file_load(&g_m, "x\n", 2u, &line),
             YGE_PROG_MSG_FILE_REFUSED);
    CHECK_EQ(line, 1u);
    CHECK(yge_prog_file_draft(&g_m, NULL) == NULL);
    connected();
    memcpy(t, g_f.image.value, sizeof(t));
    t[21] = 13u;
    size_t len = oy_file_format(t, 32u, g_text, sizeof(g_text));

    CHECK_EQ(yge_prog_file_load(&g_m, g_text, len, NULL), YGE_PROG_MSG_LOADED);
    (void)yge_prog_message(&g_m, &detail);
    CHECK_EQ(detail, 1u);
    const uint16_t *d = yge_prog_file_draft(&g_m, &n);

    CHECK(d != NULL);
    CHECK_EQ(n, 32u);
    CHECK_EQ(d[21], 13u);
    /* Another type code. */
    t[11] = 848u;
    len = oy_file_format(t, 32u, g_text, sizeof(g_text));
    CHECK_EQ(yge_prog_file_load(&g_m, g_text, len, NULL),
             YGE_PROG_MSG_LOAD_REFUSED);
    (void)yge_prog_message(&g_m, &detail);
    CHECK_EQ(detail, OY_LOAD_TYPE);
    CHECK(yge_prog_file_draft(&g_m, NULL) == NULL);
}

TEST_CASE(an_abort_and_an_error_say_what_ended_it)
{
    rig_up();
    model_up();
    connected();
    CHECK_EQ(yge_prog_read(&g_m), YGE_PROG_MSG_NONE);
    model_run(100u);
    yge_prog_abort(&g_m);
    model_run(200u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_ABORTED);
    CHECK(!yge_prog_busy(&g_m));
    CHECK_ALL_OFF();
    /* ENTER again, then the supply drops under the session. */
    CHECK_EQ(yge_prog_clear(&g_m), YGE_PROG_MSG_NONE);
    model_run(100u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_CLEARED);
    CHECK_EQ(yge_prog_enter_again(&g_m, &k_conn), YGE_PROG_MSG_NONE);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_READ_DONE);
    g.dropped = true;
    g.v.on = false;
    g.v.off = true;
    model_run(300u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_SUPPLY);
    CHECK_EQ(yge_link_faults(&g_l), 1u);
    CHECK(yge_prog_holds_port(&g_m));
    CHECK_ALL_OFF();
}

int main(void)
{
    RUN(connect_is_open_enter_read);
    RUN(every_refusal_of_a_connect_sends_nothing);
    RUN(by_hand_waits_for_the_battery);
    RUN(arm_is_refused_while_the_port_is_held);
    RUN(a_stop_latched_lets_only_close_leave_and_abort_through);
    RUN(the_first_write_saves_the_backup_first);
    RUN(no_card_keeps_the_old_values_in_ram_and_writes);
    RUN(a_card_silent_10000ms_sends_no_write);
    RUN(a_late_card_answer_of_an_ended_session_is_dropped);
    RUN(a_stop_while_the_card_saves_sends_no_write);
    RUN(every_refusal_of_a_write_sends_nothing);
    RUN(a_read_only_table_is_not_written);
    RUN(a_file_against_the_read_copy);
    RUN(an_abort_and_an_error_say_what_ended_it);
    return test_summary("yge_prog");
}
