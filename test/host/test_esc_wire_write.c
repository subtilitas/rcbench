/*
 * The ESC signal-wire session's WRITE on the bench of esc_wire_rig.h: the hold writes
 * what the page lists and nothing else, from the hold to the write's outcome the
 * session takes ABORT and STOP only, a card answer finds nothing to act on once the
 * session moved on, the list's pages, and what a backup is taken from.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_wire_int.h"

#include "esc_wire_rig.h"

/** @brief REVERSED changed to OFF: an AM32's byte 0x11. */
static void edit_reversed(void)
{
    ew_tap(esc_wire_panel_tab_rect(1));
    ew_tap(esc_wire_panel_value_rect(2));
    ew_tap(esc_wire_panel_choice_rect(0));
    CHECK_EQ(ewp_s.draft[0x11], 0u);
}

/** @brief Runs the bench until no request is out, at most 10s. */
static void wait_idle(void)
{
    for (int i = 0; i < 2000 && esc_wire_panel_stage() != ESC_WIRE_SESSION; ++i) {
        bench_run(5u);
    }
}

/** @brief The hold of an AM32 session with one edit, its card slow: the backup waits. */
static void hold_with_slow_card(void)
{
    session_up(up_am_rig);
    connected();
    edit_reversed();
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_WRITE);
    g_card_wait = true;
    ew_hold(2.2f);
    bench_run(20u);
    CHECK_EQ(ewp_s.w_step, EWP_W_BACKUP);
}

/** @brief A press only, at the middle of @p r, with finger @p id. */
static void press(gfx_rect_t r, uint8_t id)
{
    const touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                              .point = { .id = id, .x = (int16_t)(r.x + r.w / 2),
                                         .y = (int16_t)(r.y + r.h / 2), .strength = 40 } };

    esc_wire_pages.event(&e);
}

TEST_CASE(an_edit_after_the_hold_is_not_taken_and_the_hold_writes_the_page)
{
    hold_with_slow_card();
    /* The rows are gone, and so are the tabs. */
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_RUNNING);
    press(esc_wire_panel_tab_rect(4), 1u);
    ew_tap(esc_wire_panel_value_rect(8));
    CHECK_EQ(ewp_s.ch_what, EWP_CH_NONE);
    /* Whatever the draft holds, the snapshot goes. */
    ewp_s.draft[0x16] = 0u;
    g_card_wait = false;
    bench_run(50u);
    wait_idle();
    CHECK_EQ(esc.am[0x11], 0u);
    CHECK_EQ(esc.am[0x16], 1u);
    /* The record's change lines; the file behind them names every field. */
    CHECK(strstr(g_record, "REVERSED: ") != NULL);
    CHECK(strstr(g_record, "STUCK ROTOR: ") == NULL);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_WRITE_DONE));
}

TEST_CASE(from_the_hold_on_no_files_no_read_and_no_second_write)
{
    hold_with_slow_card();
    const uint32_t reads = esc_wire_panel_model()->f.rd.reads;

    ew_tap(esc_wire_panel_action_rect(1));
    CHECK_EQ(ewp_s.card.op, (uint8_t)ESC_CARD_SAVE);
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NONE);
    CHECK(esc_wire_panel_hold_rect().w == 0);
    /* BACK names ABORT, which ends it. */
    ew_tap(esc_wire_panel_back_rect());
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NONE);
    CHECK(strstr(esc_wire_panel_message(), TR(EW_ABORT)) != NULL);
    CHECK_EQ(esc_wire_panel_model()->f.rd.reads, reads);
}

TEST_CASE(abort_gives_up_a_write_that_waits_for_the_card)
{
    hold_with_slow_card();
    CHECK(ewp_safe_at(esc_wire_panel_action_rect(0).x + 5,
                      esc_wire_panel_action_rect(0).y + 5));
    press(esc_wire_panel_action_rect(0), 1u);
    CHECK_EQ(ewp_s.w_step, EWP_W_NONE);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_WRITE_GIVEN_UP));
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_SESSION);
    g_card_wait = false;
    bench_run(100u);
    CHECK_EQ(esc.am[0x11], 1u);
    CHECK_EQ(ewp_changed_count(), 1);
}

TEST_CASE(a_stop_under_a_write_that_waits_ends_it_and_the_late_answer_does_nothing)
{
    hold_with_slow_card();
    esc_link_stop(&g_l);
    bench_run(300u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_STOPPED);
    CHECK_EQ(ewp_s.w_step, EWP_W_NONE);
    g_card_wait = false;
    bench_run(100u);
    CHECK_EQ(esc.am[0x11], 1u);
    CHECK_EQ(g_card_requests, 0);
}

TEST_CASE(a_save_answer_dropped_on_its_number_does_not_leave_the_write_waiting)
{
    hold_with_slow_card();
    static esc_card_req_t r;

    g_card_wait = false;
    CHECK(esc_wire_panel_card_take(&r));
    /* The answer of an older request. */
    r.id = ewp_s.card_id - 1u;
    esc_card_run(&k_ew_io, &r, &g_done);
    esc_wire_panel_card_done(&g_done);
    CHECK_EQ(ewp_s.w_step, EWP_W_NONE);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_WRITE_GIVEN_UP));
    bench_run(100u);
    CHECK_EQ(esc.am[0x11], 1u);
}

TEST_CASE(connect_and_leave_clear_the_step_and_the_snapshot)
{
    static const uint8_t k_zero[sizeof(ewp_s.w_draft)];

    session_up(up_am_rig);
    connected();
    edit_reversed();
    ew_tap(esc_wire_panel_action_rect(2));
    /* Back from the page, then LEAVE. */
    ew_tap(esc_wire_panel_cancel_rect());
    CHECK(memcmp(ewp_s.w_draft, k_zero, sizeof(k_zero)) != 0);
    ew_tap(esc_wire_panel_back_rect());
    ew_hold(2.2f);
    CHECK_EQ(ewp_s.w_step, EWP_W_NONE);
    CHECK(memcmp(ewp_s.w_draft, k_zero, sizeof(k_zero)) == 0);
    bench_run(2000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    /* A snapshot and a step left from before go with CONNECT too. */
    ewp_s.w_draft[3] = 9u;
    ewp_s.w_step = EWP_W_RECORD;
    const uint32_t id = ewp_s.card_id;

    ewp_hold_apply();
    CHECK_EQ(ewp_s.w_step, EWP_W_NONE);
    CHECK(memcmp(ewp_s.w_draft, k_zero, sizeof(k_zero)) == 0);
    CHECK(ewp_s.card_id != id);
}

TEST_CASE(a_late_card_answer_acts_on_nothing_of_a_new_session)
{
    hold_with_slow_card();
    static esc_card_req_t r;

    CHECK(esc_wire_panel_card_take(&r));
    esc_link_stop(&g_l);
    bench_run(300u);
    ew_tap(esc_wire_panel_action_rect(1));
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    connected();
    edit_reversed();
    CHECK_EQ(esc_prog_backup_state(esc_wire_panel_model()), ESC_PROG_BACKUP_OWED);
    esc_card_run(&k_ew_io, &r, &g_done);
    esc_wire_panel_card_done(&g_done);
    bench_run(100u);
    /* Not settled by the old session's save, nothing written, no message of it. */
    CHECK_EQ(esc_prog_backup_state(esc_wire_panel_model()), ESC_PROG_BACKUP_OWED);
    CHECK_EQ(esc.am[0x11], 1u);
    CHECK_EQ(ewp_s.w_step, EWP_W_NONE);
}

TEST_CASE(a_load_in_flight_blocks_write)
{
    session_up(up_am_rig);
    connected();
    snprintf(g_names[0], sizeof(g_names[0]), "ESCW005.TXT");
    snprintf(g_texts[0], sizeof(g_texts[0]),
             "ESC settings backup, format 1\nFirmware: AM32\nLayout: 4\n"
             "CURRENT P = 200\nedited\n");
    g_card_files = 1;
    edit_reversed();
    ew_tap(esc_wire_panel_action_rect(1));
    bench_run(10u);
    g_card_wait = true;
    ew_tap(esc_wire_panel_choice_rect(1));
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NONE);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_CARD_BUSY));
    g_card_wait = false;
    bench_run(20u);
    CHECK_EQ(ewp_s.draft[0x09], 200u);
}

TEST_CASE(a_load_answer_under_an_open_page_is_dropped_with_a_message)
{
    static esc_card_req_t r;

    session_up(up_am_rig);
    connected();
    snprintf(g_names[0], sizeof(g_names[0]), "ESCW005.TXT");
    snprintf(g_texts[0], sizeof(g_texts[0]),
             "ESC settings backup, format 1\nFirmware: AM32\nLayout: 4\n"
             "CURRENT P = 200\nedited\n");
    g_card_files = 1;
    edit_reversed();
    ew_tap(esc_wire_panel_action_rect(1));
    bench_run(10u);
    g_card_wait = true;
    ew_tap(esc_wire_panel_choice_rect(1));
    CHECK(esc_wire_panel_card_take(&r));
    g_card_wait = false;
    ewp_s.card_out = false;
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_WRITE);
    ewp_s.card_out = true;
    esc_card_run(&k_ew_io, &r, &g_done);
    esc_wire_panel_card_done(&g_done);
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_WRITE);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_CARD_LATE));
    CHECK_EQ(ewp_s.draft[0x09], 1u);
}

TEST_CASE(what_the_page_lists_is_what_the_hold_writes_byte_by_byte)
{
    static uint8_t want[EW_MAP_LEN_AM];

    session_up(up_am_rig);
    connected();
    /* 3 fields, one of them a second look. */
    ewp_s.draft[0x11] = 0u;
    ewp_s.draft[0x16] = 0u;
    ewp_s.draft[0x06] = 0u;
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK(strncmp(esc_wire_panel_page_line(0), "CHECK STUCK ROTOR", 17u) == 0);
    CHECK(esc_wire_panel_page_line_warns(0));
    CHECK(!esc_wire_panel_page_line_warns(1));
    int lines = 0;

    while (esc_wire_panel_page_line(lines) != NULL) {
        ++lines;
    }
    /* The changes and the note. */
    CHECK_EQ(lines, 3 + 1);
    memcpy(want, esc.am, sizeof(want));
    want[0x11] = 0u;
    want[0x16] = 0u;
    want[0x06] = 0u;
    ew_hold(2.2f);
    bench_run(50u);
    wait_idle();
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_WRITE_DONE));
    CHECK_EQ(memcmp(esc.am, want, sizeof(want)), 0);
}

/** @brief 25 changes on an AM32, none of them a second look, and one that is. */
static int many_changes(void)
{
    const ew_field_t *f;
    int n = 0;

    for (size_t i = 0u; (f = ew_map_nth(EW_FW_AM, 4u, i)) != NULL; ++i) {
        if (f->access != (uint8_t)EW_ACC_EDIT) {
            continue;
        }
        if ((f->flags & EW_FF_WARN) != 0u) {
            if (f->off == 0x16u) {
                ewp_s.draft[f->off] = 0u;
                ++n;
            }
        } else if (n < 25) {
            ewp_s.draft[f->off] = (uint8_t)(ewp_s.draft[f->off] == f->lo ? f->lo + 1u : f->lo);
            ++n;
        }
    }
    return n;
}

TEST_CASE(the_list_has_pages_and_the_hold_comes_with_the_last)
{
    session_up(up_am_rig);
    connected();
    const int n = many_changes();
    int seen = 0;

    CHECK_EQ(ewp_changed_count(), n);
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK_EQ(ewp_write_pages(), (n + 11) / 12);
    /* The second look comes first, on the first page. */
    CHECK(strncmp(esc_wire_panel_page_line(0), "CHECK", 5u) == 0);
    CHECK(esc_wire_panel_hold_rect().w == 0);
    CHECK(esc_wire_panel_next_rect().w > 0);
    for (int p = 0; p < ewp_write_pages(); ++p) {
        const bool last = p == ewp_write_pages() - 1;

        CHECK_EQ(esc_wire_panel_hold_rect().w != 0, last);
        for (int i = 0; esc_wire_panel_page_line(i) != NULL; ++i) {
            seen += strchr(esc_wire_panel_page_line(i), '>') != NULL ? 1 : 0;
        }
        if (!last) {
            ew_tap(esc_wire_panel_next_rect());
        }
    }
    /* Every change was listed. */
    CHECK_EQ(seen, n);
    /* PREV keeps what was shown: the hold stays. */
    ew_tap(esc_wire_panel_prev_rect());
    CHECK(esc_wire_panel_hold_rect().w != 0);
    CHECK_EQ(ewp_s.w_page, ewp_write_pages() - 2);
    ew_tap(esc_wire_panel_cancel_rect());
    /* A new page starts over. */
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK(esc_wire_panel_hold_rect().w == 0);
}

TEST_CASE(a_short_list_has_no_turn_buttons)
{
    session_up(up_am_rig);
    connected();
    edit_reversed();
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK(esc_wire_panel_next_rect().w == 0);
    CHECK(esc_wire_panel_prev_rect().w == 0);
    CHECK(esc_wire_panel_hold_rect().w != 0);
}

TEST_CASE(an_erased_page_is_no_backup_and_the_read_after_a_restore_is)
{
    static char f[EW_FILE_MAX];
    static ew_file_t pf;

    session_up(up_am_rig);
    connected();
    const size_t n = esc_prog_backup_text(esc_wire_panel_model(), f, sizeof(f));

    CHECK(n > 0u);
    ew_tap(esc_wire_panel_back_rect());
    ew_hold(2.2f);
    bench_run(2000u);
    memset(esc.am, 0xFF, sizeof(esc.am));
    snprintf(g_names[0], sizeof(g_names[0]), "ESCW001.TXT");
    memcpy(g_texts[0], f, n);
    g_texts[0][n] = '\0';
    g_card_files = 1;
    connected();
    CHECK_EQ(esc_prog_backup_state(esc_wire_panel_model()), ESC_PROG_BACKUP_NONE);
    CHECK_EQ(esc_prog_no_backup(esc_wire_panel_model()), ESC_PROG_NO_BACKUP_ERASED);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_ERASED_NO_BACKUP));
    ew_tap(esc_wire_panel_action_rect(0));
    bench_run(10u);
    ew_tap(esc_wire_panel_choice_rect(0));
    bench_run(10u);
    ew_hold(2.2f);
    bench_run(50u);
    wait_idle();
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_RESTORE_DONE));
    /* The read after the restore is the session's backup. */
    CHECK_EQ(esc_prog_backup_state(esc_wire_panel_model()), ESC_PROG_BACKUP_OWED);
    edit_reversed();
    ew_tap(esc_wire_panel_action_rect(2));
    ew_hold(2.2f);
    bench_run(50u);
    wait_idle();
    CHECK_EQ(g_card_files, 2);
    CHECK_EQ(ew_file_parse(g_texts[1], strlen(g_texts[1]), &pf, NULL), EW_FILE_OK);
    CHECK_EQ(pf.layout, 4u);
    CHECK(pf.page[0] != 0xFFu);
}

TEST_CASE(a_page_of_ff_only_is_no_backup_without_the_flag)
{
    session_up(up_am_rig);
    g_hand = true;
    memset(&g_f, 0, sizeof(g_f));
    g_f.rd.phase = ESC_LINK_READY;
    g_f.rd.known = true;
    g_f.rd.held = true;
    g_f.rd.ours = true;
    g_f.rd.state = LINK_ESC_READY;
    g_f.rd.fw = EW_FW_AM;
    g_f.rd.layout = 4u;
    g_f.rd.flags = LINK_EW_F_HAVE_READ | LINK_EW_F_WRITABLE;
    g_f.rd.reads = 1u;
    g_f.image.gen = 1u;
    g_f.image.reads = 1u;
    g_f.image.len = EW_MAP_LEN_AM;
    memset(g_f.image.page, 0xFF, EW_MAP_LEN_AM);
    esc_wire_panel_feed(&g_f);
    CHECK_EQ(esc_prog_backup_state(esc_wire_panel_model()), ESC_PROG_BACKUP_NONE);
}

/** @brief A read ESCape32 fed by hand, its `music` @p music. */
static void e32_with_music(const char *music)
{
    session_up(up_am_rig);
    g_hand = true;
    memset(&g_f, 0, sizeof(g_f));
    g_f.rd.phase = ESC_LINK_READY;
    g_f.rd.known = true;
    g_f.rd.held = true;
    g_f.rd.ours = true;
    g_f.rd.state = LINK_ESC_READY;
    g_f.rd.gpio = 5u;
    g_f.rd.fw = EW_FW_E32;
    g_f.rd.layout = 17u;
    g_f.rd.len = EW_MAP_LEN_E32;
    g_f.rd.reads = 1u;
    g_f.rd.flags = LINK_EW_F_HAVE_READ | LINK_EW_F_WRITABLE;
    g_f.image.gen = 1u;
    g_f.image.reads = 1u;
    g_f.image.len = EW_MAP_LEN_E32;
    for (size_t i = 0u; i < EW_E32_KEYS; ++i) {
        ew_e32_put(g_f.image.page, i, ew_map_e32[i].lo);
    }
    snprintf(g_f.image.music, sizeof(g_f.image.music), "%s", music);
    esc_wire_panel_feed(&g_f);
}

TEST_CASE(music_with_a_byte_the_format_has_not_refuses_and_says_why)
{
    e32_with_music("a\xC3\xA4");
    CHECK_EQ(esc_prog_backup_state(esc_wire_panel_model()), ESC_PROG_BACKUP_NONE);
    CHECK_EQ(esc_prog_no_backup(esc_wire_panel_model()), ESC_PROG_NO_BACKUP_MUSIC);
    ew_e32_put(ewp_s.draft, 0u, 1);
    ewp_s.tabs.selected = 1;
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NONE);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MUSIC_NO_BACKUP));
    /* The same reason from the hold, if the state moved under it. */
    ewp_page_write();
    ewp_hold_apply();
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MUSIC_NO_BACKUP));
    CHECK_EQ(ewp_s.w_step, EWP_W_NONE);
    /* The identity tab shows what it can. */
    CHECK_STR_EQ(esc_wire_panel_music_line(), "MUSIC a??");
}

TEST_CASE(music_shows_on_the_identity_tab)
{
    e32_with_music("happy");
    CHECK_EQ(esc_prog_backup_state(esc_wire_panel_model()), ESC_PROG_BACKUP_OWED);
    CHECK_STR_EQ(esc_wire_panel_music_line(), "MUSIC happy");
    e32_with_music("");
    CHECK_STR_EQ(esc_wire_panel_music_line(), "MUSIC --");
}

int main(void)
{
    RUN(an_edit_after_the_hold_is_not_taken_and_the_hold_writes_the_page);
    RUN(from_the_hold_on_no_files_no_read_and_no_second_write);
    RUN(abort_gives_up_a_write_that_waits_for_the_card);
    RUN(a_stop_under_a_write_that_waits_ends_it_and_the_late_answer_does_nothing);
    RUN(a_save_answer_dropped_on_its_number_does_not_leave_the_write_waiting);
    RUN(connect_and_leave_clear_the_step_and_the_snapshot);
    RUN(a_late_card_answer_acts_on_nothing_of_a_new_session);
    RUN(a_load_in_flight_blocks_write);
    RUN(a_load_answer_under_an_open_page_is_dropped_with_a_message);
    RUN(what_the_page_lists_is_what_the_hold_writes_byte_by_byte);
    RUN(the_list_has_pages_and_the_hold_comes_with_the_last);
    RUN(a_short_list_has_no_turn_buttons);
    RUN(an_erased_page_is_no_backup_and_the_read_after_a_restore_is);
    RUN(a_page_of_ff_only_is_no_backup_without_the_flag);
    RUN(music_with_a_byte_the_format_has_not_refuses_and_says_why);
    RUN(music_shows_on_the_identity_tab);
    return test_summary("esc_wire_write");
}
