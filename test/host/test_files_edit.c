/**
 * @file test_files_edit.c
 * @brief The text editor on FILES' viewer: EDIT, the keys into the text,
 *        the cursor, the limits of the text, and the question over unsaved
 *        changes on every way out: the home tag, LOGS' opener, another
 *        open, STOP and a route of the application.
 *
 * Cases:
 * - edit_opens_the_keyboard_and_a_key_types_at_the_cursor
 * - the_cursor_moves_by_tap_and_arrows_and_backspace_takes_a_unit
 * - enter_inserts_the_files_line_end
 * - a_text_of_1mib_types_nothing_more_and_one_byte_over_does_not_open
 * - the_line_limit_holds_at_65536_line_ends
 * - the_home_tag_climbs_the_editor_and_asks_before_unsaved_text_goes
 * - revert_asks_and_puts_the_file_back
 * - logs_returns_after_the_question_and_another_open_is_held
 * - stop_and_a_route_away_keep_the_text
 * - the_journal_and_the_fault_log_take_no_edit
 * - an_empty_file_takes_text
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui_band.h"

#include "files_edit_rig.h"

/** @brief A report as servo_report.c lays one out, in English. */
static const char k_report[] =
    "RCBENCH SERVO TEST REPORT\n"
    "Result:         PASS\n"
    "Device:         MG90S\n";

/*
 * EDIT opens the editor with its keyboard over the lower screen; a key
 * types at the cursor, which starts on the top line, and the header says
 * MODIFIED.  DONE closes the keyboard and the footer shows SAVE.
 */
TEST_CASE(edit_opens_the_keyboard_and_a_key_types_at_the_cursor)
{
    open_text("VIEW.TXT", k_report);
    CHECK_EQ(text_view_state(), TEXT_VIEW_SHOWN);
    CHECK(!text_view_editing());
    edit();
    CHECK(text_view_editing());
    CHECK(text_view_keyboard());
    CHECK_EQ(text_view_rows(), 4);
    CHECK(!text_view_dirty());
    CHECK_EQ(text_view_cursor(NULL), 0u);
    ed_type("Ab1 ");
    CHECK(text_view_dirty());
    CHECK_STR_EQ(row_at(0), "Ab1 RCBENCH SERVO TEST REPORT");
    CHECK_EQ(text_view_cursor(NULL), 4u);
    /* The marks layer, and back. */
    ed_type("#;");
    CHECK_STR_EQ(row_at(0), "Ab1 #;RCBENCH SERVO TEST REPORT");
    ed_key(UI_TK_DONE);
    CHECK(!text_view_keyboard());
    CHECK_EQ(text_view_rows(), TEXT_VIEW_ROWS_NORMAL);
    /* KEYBOARD opens it again, the text as it was. */
    foot(FS_SPOT_TV_KEYS_CX);
    CHECK(text_view_keyboard());
    CHECK_STR_EQ(row_at(0), "Ab1 #;RCBENCH SERVO TEST REPORT");
    /* Nothing reached the card. */
    CHECK(card_holds("USER/LOGS/VIEW.TXT", k_report, strlen(k_report)));
}

/*
 * A tap on the text puts the cursor on the nearest cell: the first cell, a
 * cell in the middle, past the line's end; "<-" and "->" move it one unit
 * and stop at the text's ends; DEL takes the unit before it, a 2-byte
 * letter as one.
 */
TEST_CASE(the_cursor_moves_by_tap_and_arrows_and_backspace_takes_a_unit)
{
    open_text("VIEW.TXT", "abcdef\nx\xC3\xA4y\nlast");
    edit();
    /* Row 0, cell 3: between c and d. */
    tap(FS_SPOT_TV_TEXT_X + 3 * 10, FS_SPOT_TV_TEXT_Y + 5);
    CHECK_EQ(text_view_cursor(NULL), 3u);
    /* Half a cell to the right of cell 3 rounds to cell 4. */
    tap(FS_SPOT_TV_TEXT_X + 3 * 10 + 6, FS_SPOT_TV_TEXT_Y + 5);
    CHECK_EQ(text_view_cursor(NULL), 4u);
    /* Left of the first cell: the line's start. */
    tap(FS_SPOT_TV_TEXT_X - 20, FS_SPOT_TV_TEXT_Y + 5);
    CHECK_EQ(text_view_cursor(NULL), 0u);
    /* Past the end of row 1: its line's end, before its line feed. */
    size_t line = 9u;
    tap(FS_SPOT_TV_TEXT_X + 60 * 10, FS_SPOT_TV_TEXT_Y + FS_SPOT_TV_PITCH + 5);
    CHECK_EQ(text_view_cursor(&line), 11u);
    CHECK_EQ(line, 1u);
    /* Under the last line: the text's end. */
    tap(FS_SPOT_TV_TEXT_X, FS_SPOT_TV_TEXT_Y + 3 * FS_SPOT_TV_PITCH + 5);
    CHECK_EQ(text_view_cursor(&line), 16u);
    CHECK_EQ(line, 2u);
    ed_key(UI_TK_RIGHT);                    /* at the end: stays */
    CHECK_EQ(text_view_cursor(NULL), 16u);
    /* Back over "last" and the line feed onto "y", then over the 2-byte
     * letter as one unit. */
    for (int i = 0; i < 5; ++i) {
        ed_key(UI_TK_LEFT);
    }
    CHECK_EQ(text_view_cursor(NULL), 11u);
    ed_key(UI_TK_LEFT);
    CHECK_EQ(text_view_cursor(NULL), 10u);
    ed_key(UI_TK_LEFT);
    CHECK_EQ(text_view_cursor(NULL), 8u);
    ed_key(UI_TK_RIGHT);
    CHECK_EQ(text_view_cursor(NULL), 10u);
    ed_key(UI_TK_DEL);                      /* the 2 bytes of the letter */
    CHECK_EQ(text_view_cursor(NULL), 8u);
    CHECK_STR_EQ(row_at(1), "xy");
    /* DEL at the line's start joins it to the line above. */
    ed_key(UI_TK_LEFT);
    ed_key(UI_TK_DEL);
    CHECK_STR_EQ(row_at(0), "abcdefxy");
    /* And at the text's start it takes nothing. */
    tap(FS_SPOT_TV_TEXT_X - 20, FS_SPOT_TV_TEXT_Y + 5);
    ed_key(UI_TK_DEL);
    ed_key(UI_TK_LEFT);
    CHECK_EQ(text_view_cursor(NULL), 0u);
    CHECK_STR_EQ(row_at(0), "abcdefxy");
}

/*
 * ENTER types the file's line end: CR LF in a CR LF file, LF in an LF file
 * and in an empty one.  The text moves to the cursor's row when it goes
 * below the 4 rows over the keyboard.
 */
TEST_CASE(enter_inserts_the_files_line_end)
{
    open_text("CRLF.TXT", "one\r\ntwo\r\n");
    edit();
    tap(FS_SPOT_TV_TEXT_X + 3 * 10, FS_SPOT_TV_TEXT_Y + 5);   /* after one */
    ed_type("\n");
    ed_key(UI_TK_DONE);
    save();
    CHECK(card_holds("USER/LOGS/CRLF.TXT", "one\r\n\r\ntwo\r\n", 12u));
    open_text("LF.TXT", "a\n");
    edit();
    ed_type("\n\n\n\n\nz");
    CHECK_EQ(text_view_cursor(NULL), 6u);
    /* The cursor's row is the last of the 4 rows. */
    size_t r = 9u;
    CHECK_EQ(text_view_top(&r), 2u);
    CHECK_STR_EQ(row_at(3), "za");
    ed_key(UI_TK_DONE);
    save();
    CHECK(card_holds("USER/LOGS/LF.TXT", "\n\n\n\n\nza\n", 8u));
}

/*
 * A file of exactly 1MiB opens and takes no more: a key types nothing and
 * the header says why; DEL makes room for one.  A file of 1MiB and 1 byte
 * does not open.
 */
TEST_CASE(a_text_of_1mib_types_nothing_more_and_one_byte_over_does_not_open)
{
    static char big[TEXT_BUF_FILE_MAX + 1u];
    for (size_t i = 0; i < sizeof(big); ++i) {
        big[i] = (i % 64u == 63u) ? '\n' : 'a';
    }
    open_file(CARD_USER_LOGS, "BIG.TXT", big, TEXT_BUF_FILE_MAX);
    CHECK_EQ(text_view_state(), TEXT_VIEW_SHOWN);
    char line0[64];
    memset(line0, 'a', 63u);
    line0[63] = '\0';
    edit();
    ed_type("q");
    CHECK(!text_view_dirty());
    CHECK_STR_EQ(note(), ui_tr(TX_ED_FULL));
    CHECK_STR_EQ(row_at(0), line0);
    ed_key(UI_TK_RIGHT);
    ed_key(UI_TK_DEL);
    CHECK(text_view_dirty());
    CHECK_STR_EQ(note(), "");
    ed_type("q");
    line0[0] = 'q';
    CHECK_STR_EQ(row_at(0), line0);
    ed_type("r");
    CHECK_STR_EQ(note(), ui_tr(TX_ED_FULL));
    /* One byte over: refused before a byte is read. */
    CHECK_EQ(files_screen_open_text(CARD_USER_LOGS, "BIG.TXT",
                                    g_h.card.mount, TEXT_BUF_FILE_MAX + 1u,
                                    false),
             FM_ERR_TOO_BIG);
}

/*
 * 65,536 line ends open; ENTER at that count types nothing and says the
 * count, a letter still types.  DEL of a line end makes room for ENTER.
 */
TEST_CASE(the_line_limit_holds_at_65536_line_ends)
{
    static char lines[TEXT_BUF_ENDS_MAX];
    memset(lines, '\n', sizeof(lines));
    open_file(CARD_USER_LOGS, "LINES.TXT", lines, sizeof(lines));
    CHECK_EQ(text_view_state(), TEXT_VIEW_SHOWN);
    edit();
    ed_type("\n");
    CHECK(!text_view_dirty());
    char want[64];
    snprintf(want, sizeof(want), ui_tr(TX_ED_LINES_FULL), 65536u);
    CHECK_STR_EQ(note(), want);
    ed_type("w");
    CHECK(text_view_dirty());
    CHECK_STR_EQ(note(), "");
    ed_key(UI_TK_RIGHT);
    ed_key(UI_TK_DEL);                      /* one line end fewer */
    ed_type("\n");
    CHECK_STR_EQ(note(), "");
}

/*
 * The home tag climbs one level a tap: the keyboard closes, then the
 * editor asks while its text is unsaved.  Over the question it is STAY.
 * DISCARD puts the text back and shows the viewer; the next tap leaves
 * the viewer for the panes.
 */
TEST_CASE(the_home_tag_climbs_the_editor_and_asks_before_unsaved_text_goes)
{
    open_text("VIEW.TXT", k_report);
    edit();
    /* Unchanged: no question. */
    CHECK(home());
    CHECK(!text_view_keyboard());
    CHECK(home());
    CHECK(!text_view_editing());
    CHECK_EQ(files_screen_view(), FILES_VIEW_TEXT);
    edit();
    ed_type("x");
    CHECK(home());                          /* the keyboard */
    CHECK(text_view_editing());
    CHECK(home());                          /* the question */
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_BACK);
    CHECK(home());                          /* as STAY */
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_NONE);
    CHECK(text_view_dirty());
    CHECK(home());
    tap(FS_SPOT_TV_STAY_CX, FS_SPOT_TV_Q_CY);
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_NONE);
    CHECK(text_view_editing());
    CHECK_STR_EQ(row_at(0), "xRCBENCH SERVO TEST REPORT");
    /* A press that slides off DISCARD does nothing. */
    CHECK(home());
    send(TOUCH_EVENT_DOWN, FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    send(TOUCH_EVENT_MOVE, FS_SPOT_TV_DISCARD_CX + 200, FS_SPOT_TV_Q_CY);
    send(TOUCH_EVENT_UP, FS_SPOT_TV_DISCARD_CX + 200, FS_SPOT_TV_Q_CY);
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_BACK);
    tap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_NONE);
    CHECK(!text_view_editing());
    CHECK_STR_EQ(row_at(0), "RCBENCH SERVO TEST REPORT");
    CHECK(home());
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK(card_holds("USER/LOGS/VIEW.TXT", k_report, strlen(k_report)));
}

/*
 * REVERT is shut while the text is as on the card; with changes it asks,
 * STAY keeps them and DISCARD puts the file back, the editor still on.
 */
TEST_CASE(revert_asks_and_puts_the_file_back)
{
    open_text("VIEW.TXT", k_report);
    edit();
    ed_key(UI_TK_DONE);
    foot(FS_SPOT_TV_REVERT_CX);
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_NONE);
    foot(FS_SPOT_TV_KEYS_CX);
    ed_type("zz");
    ed_key(UI_TK_DONE);
    foot(FS_SPOT_TV_REVERT_CX);
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_REVERT);
    tap(FS_SPOT_TV_STAY_CX, FS_SPOT_TV_Q_CY);
    CHECK_STR_EQ(row_at(0), "zzRCBENCH SERVO TEST REPORT");
    foot(FS_SPOT_TV_REVERT_CX);
    tap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    CHECK(text_view_editing());
    CHECK(!text_view_dirty());
    CHECK_STR_EQ(row_at(0), "RCBENCH SERVO TEST REPORT");
    /* SAVE on the text as on the card writes nothing. */
    const long writes = g_h.card.calls[FK_WRITE];
    save();
    CHECK_EQ(g_h.card.calls[FK_WRITE], writes);
    CHECK_STR_EQ(note(), ui_tr(TX_ED_NO_CHANGES));
}

/*
 * A report LOGS opened: the editor's question comes before the home tag
 * returns to LOGS.  LOGS opening another file over unsaved text shows the
 * question first: STAY keeps the text and drops the open, DISCARD opens
 * the other file.
 */
TEST_CASE(logs_returns_after_the_question_and_another_open_is_held)
{
    fresh_bare();
    card_default();
    put_n("USER/LOGS/ATEST007.TXT", k_report, strlen(k_report));
    put_n("USER/LOGS/NOTE.TXT", "note\n", 5u);
    ui_router_goto(SCREEN_LOGS);
    CHECK_EQ(files_screen_open_text(CARD_USER_LOGS, "ATEST007.TXT",
                                    g_h.card.mount, strlen(k_report), true),
             FM_OK);
    ui_router_goto(SCREEN_FILES);
    settle();
    edit();
    ed_type("q");
    CHECK(home());
    CHECK(home());
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_BACK);
    tap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    CHECK(!home());                         /* the router goes to LOGS */
    CHECK_EQ(ui_router_parent(SCREEN_FILES), SCREEN_LOGS);

    /* Another open over unsaved text. */
    CHECK_EQ(files_screen_open_text(CARD_USER_LOGS, "ATEST007.TXT",
                                    g_h.card.mount, strlen(k_report), true),
             FM_OK);
    settle();
    edit();
    ed_type("q");
    CHECK_EQ(files_screen_open_text(CARD_USER_LOGS, "NOTE.TXT",
                                    g_h.card.mount, 5u, true),
             FM_OK);
    settle();
    draw();
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_OPEN);
    CHECK(!text_view_keyboard());
    CHECK_STR_EQ(text_view_name(), "ATEST007.TXT");
    tap(FS_SPOT_TV_STAY_CX, FS_SPOT_TV_Q_CY);
    CHECK_STR_EQ(text_view_name(), "ATEST007.TXT");
    CHECK(text_view_dirty());
    CHECK_STR_EQ(row_at(0), "qRCBENCH SERVO TEST REPORT");
    /* The home tag over the question drops the open too. */
    CHECK_EQ(files_screen_open_text(CARD_USER_LOGS, "NOTE.TXT",
                                    g_h.card.mount, 5u, true),
             FM_OK);
    CHECK(home());
    CHECK_STR_EQ(text_view_name(), "ATEST007.TXT");
    CHECK_EQ(files_screen_open_text(CARD_USER_LOGS, "NOTE.TXT",
                                    g_h.card.mount, 5u, true),
             FM_OK);
    tap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    settle();
    CHECK_STR_EQ(text_view_name(), "NOTE.TXT");
    CHECK_EQ(text_view_state(), TEXT_VIEW_SHOWN);
    CHECK(!text_view_editing());
    CHECK_STR_EQ(row_at(0), "note");
    CHECK(card_holds("USER/LOGS/ATEST007.TXT", k_report, strlen(k_report)));
}

/*
 * STOP on the band stops the bench and leaves the screen and the text as
 * they are; ARMED then shuts SAVE.  A route the application takes leaves
 * at once: the editor and its text stay for the way back in.
 */
TEST_CASE(stop_and_a_route_away_keep_the_text)
{
    open_text("VIEW.TXT", k_report);
    edit();
    ed_type("s");
    const gfx_rect_t r = ui_band_stop_rect();
    touch_event_t e = {
        .type = TOUCH_EVENT_DOWN,
        .point = { .id = 3u, .x = (int16_t)(r.x + r.w / 2),
                   .y = (int16_t)(r.y + r.h / 2) },
    };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK(ui_router_take_stop());
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK(text_view_editing());
    CHECK(text_view_dirty());
    CHECK_STR_EQ(row_at(0), "sRCBENCH SERVO TEST REPORT");
    /* A route away and back. */
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK_EQ(files_screen_view(), FILES_VIEW_TEXT);
    ui_router_goto(SCREEN_FILES);
    settle();
    CHECK(text_view_editing());
    CHECK(text_view_dirty());
    CHECK_STR_EQ(row_at(0), "sRCBENCH SERVO TEST REPORT");
    /* Unchanged text closes with the screen, as the viewer does. */
    ed_key(UI_TK_DONE);
    foot(FS_SPOT_TV_REVERT_CX);
    tap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    CHECK(!text_view_dirty());
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_EQ(text_view_state(), TEXT_VIEW_CLOSED);
}

/*
 * The servo programmer's journal does not open; the fault log opens in
 * the viewer and EDIT is shut there, a tap saying why.
 */
TEST_CASE(the_journal_and_the_fault_log_take_no_edit)
{
    fresh_bare();
    put_n("BENCH/RCBENCH.LOG", "fault 1\n", 8u);
    ui_router_goto(SCREEN_FILES);
    settle();
    CHECK_EQ(files_screen_open_text("BENCH", "RCBENCH.LOG", g_h.card.mount,
                                    8u, false),
             FM_OK);
    settle();
    CHECK_EQ(text_view_state(), TEXT_VIEW_SHOWN);
    edit();
    CHECK(!text_view_editing());
    char want[128];
    files_err_text(FM_ERR_FIXED, "RCBENCH.LOG", want, sizeof(want));
    CHECK_STR_EQ(note(), want);
    CHECK(!text_view_edit());
}

/*
 * An empty file shows the editor's one line, and takes text.
 */
TEST_CASE(an_empty_file_takes_text)
{
    open_file(CARD_USER_LOGS, "EMPTY.TXT", "", 0u);
    CHECK_EQ(text_view_lines(), 0u);
    edit();
    CHECK(text_view_editing());
    CHECK_EQ(text_view_lines(), 1u);
    ed_type("hi");
    CHECK_STR_EQ(row_at(0), "hi");
    ed_key(UI_TK_DONE);
    save();
    CHECK(card_holds("USER/LOGS/EMPTY.TXT", "hi", 2u));
}

int main(void)
{
    RUN(edit_opens_the_keyboard_and_a_key_types_at_the_cursor);
    RUN(the_cursor_moves_by_tap_and_arrows_and_backspace_takes_a_unit);
    RUN(enter_inserts_the_files_line_end);
    RUN(a_text_of_1mib_types_nothing_more_and_one_byte_over_does_not_open);
    RUN(the_line_limit_holds_at_65536_line_ends);
    RUN(the_home_tag_climbs_the_editor_and_asks_before_unsaved_text_goes);
    RUN(revert_asks_and_puts_the_file_back);
    RUN(logs_returns_after_the_question_and_another_open_is_held);
    RUN(stop_and_a_route_away_keep_the_text);
    RUN(the_journal_and_the_fault_log_take_no_edit);
    RUN(an_empty_file_takes_text);
    fh_free(&g_h);
    free(s_fb);
    return test_summary("files_edit");
}
