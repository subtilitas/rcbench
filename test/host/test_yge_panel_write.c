/*
 * Writing with programming for YGE ESCs' screens (yge_panel*.c) on the bench
 * of yge_panel_rig.h: WRITE n VALUES greyed with no write possible and a
 * press on it doing nothing; its 2s hold released at 1999ms and held to
 * 2000ms; the first write saving the backup to BENCH/YGE/CONF before any
 * frame, the card missing keeping it in RAM; a read-only table with no WRITE;
 * READ AGAIN and LEAVE over edits not written, each a page held 2s; LEAVE
 * without edits a tap; and a file loaded into the draft.
 *
 * SPDX-License-Identifier: MIT
 */
#include "yge_panel_rig.h"

/* The BEC row on MODE & BEC set to @p text. */
static void bec_to(const char *text)
{
    const int b = row_named(1, "BEC VOLTAGE");

    yg_tap(yge_panel_value_rect(b));
    kp_type(text);
}

/* A finger on WRITE n VALUES, frames of 0.25s to @p total_s, and off. */
static void write_held(float total_s)
{
    const gfx_rect_t r = yge_panel_hold_rect();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2),
                                   .strength = 40 } };
    float left = total_s;

    yge_pages.event(&e);
    while (left > 0.25f) {
        yge_pages.tick(0.25f);
        left -= 0.25f;
    }
    yge_pages.tick(left);
    e.type = TOUCH_EVENT_UP;
    yge_pages.event(&e);
}

/* The bench run until no flow is out. */
static void settle(void)
{
    for (int i = 0; i < 8000 && yge_prog_busy(yge_panel_model()); ++i) {
        bench_run(5u);
    }
    bench_run(300u);
}

TEST_CASE(write_is_greyed_with_no_write_possible)
{
    session_up();
    connected();
    /* No change: the footer's WRITE takes no press. */
    CHECK(yge_panel_hold_rect().w > 0);
    CHECK(!yge_panel_hold_live());
    const int before = g_commands;

    write_held(2.2f);
    settle();
    CHECK_EQ(g_commands, before);
    CHECK_EQ(g_card_requests, 0);
    /* A change makes it live. */
    bec_to("8");
    CHECK(yge_panel_hold_live());
    CHECK_STR_EQ(yge_panel_check_line(), "CHECK BEC VOLTAGE 8.0V");
}

TEST_CASE(write_at_1999ms_sends_nothing_and_at_2000ms_saves_then_writes)
{
    session_up();
    connected();
    bec_to("8");
    const int before = g_commands;

    write_held(1.999f);
    settle();
    CHECK_EQ(g_commands, before);
    CHECK_EQ(g_card_requests, 0);
    CHECK_EQ(g_p.ses.orig[OY_I_BEC], 74u);
    /* 2000ms: the backup to the card first, then the write. */
    write_held(2.0f);
    CHECK(yge_prog_card_waits(yge_panel_model()));
    CHECK_EQ(g_commands, before);
    settle();
    CHECK_EQ(g_card_requests, 1);
    CHECK_EQ(g_card_files, 1);
    CHECK_STR_EQ(g_names[0], "YGE001-5712.TXT");
    /* The backup holds the table as read: 7.4V. */
    CHECK(strstr(g_texts[0], "BEC VOLTAGE") != NULL);
    CHECK_EQ(g_commands, before + 1);
    CHECK_EQ(g_cmd_op[before], LINK_YGE_OP_WRITE);
    CHECK_EQ(g_cmd_key[before], LINK_YGE_KEY);
    CHECK_EQ(sim.table[OY_I_BEC], 80u);
    CHECK_STR_EQ(yge_panel_message(), "Written and read back.");
    CHECK_EQ(yge_panel_changed(), 0);
    /* The second write of the session saves no backup. */
    bec_to("7.4");
    write_held(2.0f);
    settle();
    CHECK_EQ(g_card_requests, 1);
    CHECK_EQ(sim.table[OY_I_BEC], 74u);
}

TEST_CASE(no_card_keeps_the_old_values_in_ram_and_writes)
{
    session_up();
    connected();
    g_card_in = false;
    bec_to("8");
    write_held(2.0f);
    settle();
    CHECK_EQ(g_card_requests, 1);
    CHECK_EQ(g_card_files, 0);
    CHECK_EQ(yge_prog_backup_where(yge_panel_model()), YGE_PROG_BACKUP_RAM);
    CHECK_EQ(sim.table[OY_I_BEC], 80u);
}

TEST_CASE(a_card_that_does_not_answer_sends_no_write)
{
    session_up();
    connected();
    g_card_wait = true;
    bec_to("8");
    const int before = g_commands;

    write_held(2.0f);
    bench_run(9990u);
    CHECK_EQ(g_commands, before);
    bench_run(100u);
    CHECK_EQ(g_commands, before);
    CHECK_STR_EQ(yge_panel_message(),
                 "The card did not answer within 10s: nothing written.");
    CHECK_EQ(sim.table[OY_I_BEC], 74u);
}

TEST_CASE(a_read_only_table_has_no_write_and_no_live_row)
{
    session_up();
    sim.table[OY_I_FW_LO] = (uint16_t)(103546u & 0xFFFFu);
    sim.table[OY_I_FW_HI] = (uint16_t)(103546u >> 16);
    connected();
    CHECK((g_f.rd.flags & LINK_YP_F_WRITABLE) == 0u);
    CHECK_EQ(yge_panel_hold_rect().w, 0);
    tab(1);
    for (int i = 0; yge_panel_row_name(i) != NULL; ++i) {
        CHECK(!yge_panel_row_live(i));
    }
    /* A tap on a row opens nothing. */
    yg_tap(yge_panel_value_rect(0));
    CHECK(!yge_panel_keypad()->open);
    CHECK_EQ(ygp_s.ch_what, YGP_CH_NONE);
}

TEST_CASE(leave_with_no_edits_is_a_tap_and_with_edits_a_page_held_2s)
{
    session_up();
    connected();
    bec_to("8");
    yg_tap(yge_panel_action_rect(3));
    CHECK_EQ(yge_panel_page(), YGP_PAGE_LEAVE);
    CHECK_STR_EQ(yge_panel_page_line(0), "1 edits are not written and are lost:");
    CHECK_STR_EQ(yge_panel_page_line(1), "BEC VOLTAGE: 7.4V -> 8.0V");
    /* BACK keeps the edits. */
    yg_tap(yge_panel_cancel_rect());
    CHECK_EQ(yge_panel_page(), YGP_PAGE_NONE);
    CHECK_EQ(yge_panel_changed(), 1);
    /* BACK at the top asks the same; 1999ms leaves nothing. */
    yg_tap(yge_panel_back_rect());
    CHECK_EQ(yge_panel_page(), YGP_PAGE_LEAVE);
    write_held(1.999f);
    settle();
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_SESSION);
    write_held(2.0f);
    settle();
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_CONNECT);
    CHECK(!yge_panel_holds_port());
    CHECK(!g.v.asked_on);
    /* Again, with no edits: LEAVE is a tap. */
    connected();
    yg_tap(yge_panel_action_rect(3));
    settle();
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_CONNECT);
    CHECK_STR_EQ(yge_panel_message(),
                 "YGE session closed. The pin is back on its port.");
}

TEST_CASE(read_again_over_edits_asks_first)
{
    session_up();
    connected();
    bec_to("8");
    const int before = g_commands;

    yg_tap(yge_panel_action_rect(0));
    CHECK_EQ(yge_panel_page(), YGP_PAGE_READ);
    yg_tap(yge_panel_cancel_rect());
    CHECK_EQ(g_commands, before);
    CHECK_EQ(yge_panel_changed(), 1);
    yg_tap(yge_panel_action_rect(0));
    write_held(2.0f);
    settle();
    CHECK_EQ(g_cmd_op[before], LINK_YGE_OP_READ);
    CHECK_EQ(yge_panel_changed(), 0);
    /* With no edits READ AGAIN reads at once. */
    yg_tap(yge_panel_action_rect(0));
    settle();
    CHECK_EQ(g_cmd_op[before + 1], LINK_YGE_OP_READ);
}

TEST_CASE(a_file_loads_into_the_draft)
{
    char text[OY_FILE_MAX];
    uint16_t t[OPENYGE_MAX_PARAMS];

    session_up();
    connected();
    /* A file of this ESC with BEC 8.0V and 12 pinion teeth more. */
    memcpy(t, g_f.image.value, sizeof(t));
    t[OY_I_BEC] = 80u;
    t[21] = 24u;
    const size_t n = oy_file_format(t, g_f.image.entries, text, sizeof(text));

    CHECK(n > 0u);
    snprintf(g_names[0], sizeof(g_names[0]), "YGE001-5712.TXT");
    memcpy(g_texts[0], text, n);
    g_texts[0][n] = '\0';
    g_card_files = 1;
    yg_tap(yge_panel_action_rect(1));
    CHECK_EQ(ygp_s.ch_what, YGP_CH_FILES);
    CHECK_STR_EQ(yge_panel_choice_label(0), "YGE001-5712");
    yg_tap(yge_panel_choice_rect(0));
    bench_run(10u);
    CHECK_EQ(yge_panel_changed(), 2);
    CHECK_STR_EQ(yge_panel_message(),
                 "2 values loaded from the file into the draft. WRITE sends them.");
    CHECK_STR_EQ(yge_panel_check_line(), "CHECK BEC VOLTAGE 8.0V");
    write_held(2.0f);
    settle();
    CHECK_EQ(sim.table[21], 24u);
    CHECK_EQ(sim.table[OY_I_BEC], 80u);
    /* A file of another type is refused and the draft stays. */
    t[OY_I_TYPE] = 848u;
    const size_t m = oy_file_format(t, g_f.image.entries, text, sizeof(text));

    memcpy(g_texts[1], text, m);
    g_texts[1][m] = '\0';
    snprintf(g_names[1], sizeof(g_names[1]), "YGE002-848.TXT");
    g_card_files = 2;
    yg_tap(yge_panel_action_rect(1));
    yg_tap(yge_panel_choice_rect(0));
    bench_run(10u);
    CHECK_EQ(yge_panel_changed(), 0);
    CHECK_STR_EQ(yge_panel_message(),
                 "File not loaded: the file is for another ESC type.");
    /* An empty folder says so. */
    g_card_files = 0;
    yg_tap(yge_panel_action_rect(1));
    bench_run(10u);
    CHECK_EQ(ygp_s.ch_what, YGP_CH_NONE);
    CHECK_STR_EQ(yge_panel_message(), "No YGE files in BENCH/YGE/CONF.");
}

int main(void)
{
    RUN(write_is_greyed_with_no_write_possible);
    RUN(write_at_1999ms_sends_nothing_and_at_2000ms_saves_then_writes);
    RUN(no_card_keeps_the_old_values_in_ram_and_writes);
    RUN(a_card_that_does_not_answer_sends_no_write);
    RUN(a_read_only_table_has_no_write_and_no_live_row);
    RUN(leave_with_no_edits_is_a_tap_and_with_edits_a_page_held_2s);
    RUN(read_again_over_edits_asks_first);
    RUN(a_file_loads_into_the_draft);
    return test_summary("yge_panel_write");
}
