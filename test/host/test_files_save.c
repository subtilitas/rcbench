/**
 * @file test_files_save.c
 * @brief The editor's SAVE on FILES' card task: the text through
 *        "<name>.NEW", the write gate before and during the save, a card
 *        pulled part way, a KST file the servo programmer then reads as
 *        EDITED, and an ESC profile's note.
 *
 * Cases:
 * - save_writes_the_text_through_new_and_keeps_the_name
 * - save_is_shut_while_armed_and_the_text_stays
 * - a_log_that_opens_before_the_save_runs_stops_it
 * - armed_part_way_stops_the_save_and_removes_its_new
 * - a_card_pulled_part_way_or_at_the_rename_keeps_the_text
 * - the_text_holds_still_while_it_is_saved
 * - a_kst_file_edited_loses_its_checksum_and_loads_as_edited
 * - an_esc_profile_says_it_is_read_at_the_next_start
 * - a_new_left_by_a_cut_save_refuses_the_next_one
 * - an_open_asked_during_a_save_waits_for_it
 * - a_save_that_fails_under_the_question_leaves_discard_live
 * - the_text_holds_still_while_the_save_waits_its_turn
 * - save_refuses_a_nul_in_the_first_4kb_and_revert_keeps_the_text
 * - save_takes_out_every_line_the_programmer_reads_as_a_checksum
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_file.h"
#include "kst_file.h"
#include "kst_sim.h"

#include "files_edit_rig.h"

/** @brief A report's first lines. */
static const char k_report[] =
    "RCBENCH SERVO TEST REPORT\n"
    "Result:         PASS\n";

/**
 * @brief A text of @p n bytes of 40-byte numbered lines, into a static
 *        buffer.
 * @param n  bytes, a multiple of 40 up to 65,520
 * @return the text
 */
static const char *lines_of(size_t n)
{
    static char buf[65536];
    for (size_t at = 0u; at + 40u <= n; at += 40u) {
        snprintf(buf + at, 41u, "%038zu\n", at / 40u);
        buf[at + 39u] = '\n';
    }
    buf[n] = '\0';
    return buf;
}

/** @brief The bytes of @p text with @p ins in front. */
static const char *with_front(const char *ins, const char *text)
{
    static char out[65600];
    snprintf(out, sizeof(out), "%s%s", ins, text);
    return out;
}

/*
 * SAVE writes the text as "<name>.NEW", deletes the old file and gives the
 * new one its name: the card then holds the text under the name, and no
 * ".NEW".  The note names the file; the header shows EDITING again, and a
 * second SAVE writes nothing.
 */
TEST_CASE(save_writes_the_text_through_new_and_keeps_the_name)
{
    open_text("VIEW.TXT", k_report);
    edit();
    ed_type("ok ");
    ed_key(UI_TK_DONE);
    save();
    CHECK(card_holds("USER/LOGS/VIEW.TXT", with_front("ok ", k_report),
                     strlen(k_report) + 3u));
    CHECK(fk_get(&g_h.card, "USER/LOGS/VIEW.TXT.NEW") == NULL);
    CHECK(!text_view_dirty());
    CHECK(!text_view_saving());
    CHECK_STR_EQ(note(), said(TX_ED_SAVED, "VIEW.TXT"));
    const long writes = g_h.card.calls[FK_WRITE];
    save();
    CHECK_EQ(g_h.card.calls[FK_WRITE], writes);
    /* DISCARD now puts back the text as saved, not as first loaded. */
    foot(FS_SPOT_TV_KEYS_CX);
    ed_type("x");
    ed_key(UI_TK_DONE);
    foot(FS_SPOT_TV_REVERT_CX);
    tap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    CHECK_STR_EQ(row_at(0), "ok RCBENCH SERVO TEST REPORT");
    /* The file's size in the pane follows the save. */
    CHECK(home());
    CHECK(home());
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    const int r = row_of(1, "VIEW.TXT");
    CHECK(r >= 0);
    CHECK_EQ(files_screen_pane(1)->page[r].size,
             (uint64_t)(strlen(k_report) + 3u));
}

/*
 * The bench switches to ARMED during an edit: SAVE is shut and the note
 * says why, a tap on it writes nothing, and the text stays and still takes
 * keys.  Back to SAFE, SAVE writes it.
 */
TEST_CASE(save_is_shut_while_armed_and_the_text_stays)
{
    open_text("VIEW.TXT", k_report);
    edit();
    ed_type("a");
    g_h.armed = true;
    settle();
    ed_type("b");
    CHECK_STR_EQ(row_at(0), "abRCBENCH SERVO TEST REPORT");
    ed_key(UI_TK_DONE);
    char why[128];
    files_err_text(FM_ERR_ARMED, "VIEW.TXT", why, sizeof(why));
    CHECK_STR_EQ(note(), why);
    const unsigned submits = g_h.submits;
    save();
    CHECK_EQ(g_h.submits, submits);
    CHECK(text_view_dirty());
    CHECK(card_holds("USER/LOGS/VIEW.TXT", k_report, strlen(k_report)));
    /* ARMED by the router's status alone shuts it too. */
    g_h.armed = false;
    const ui_bench_status_t armed = { .link_up = true, .armed = true };
    ui_router_set_status(&armed);
    settle();
    save();
    CHECK_EQ(g_h.submits, submits);
    const ui_bench_status_t safe = { .link_up = true };
    ui_router_set_status(&safe);
    settle();
    CHECK_STR_EQ(note(), "");
    save();
    CHECK(card_holds("USER/LOGS/VIEW.TXT", with_front("ab", k_report),
                     strlen(k_report) + 2u));
}

/*
 * A log run opens between the tap on SAVE and the save's start on the
 * card's task: the job stops before it opens a file, the card keeps the
 * file as it was and no ".NEW", and the text stays unsaved.
 */
TEST_CASE(a_log_that_opens_before_the_save_runs_stops_it)
{
    open_text("VIEW.TXT", k_report);
    edit();
    ed_type("L");
    ed_key(UI_TK_DONE);
    g_h.defer = true;
    save();
    CHECK(text_view_saving());
    CHECK_STR_EQ(note(), ui_tr(TX_ED_SAVING));
    g_h.marks.run = 3u;                 /* a run opens in BENCH/LOGS */
    CHECK(fh_run(&g_h));
    g_h.defer = false;
    settle();
    CHECK(!text_view_saving());
    CHECK(text_view_dirty());
    CHECK_STR_EQ(note(), ui_tr(TX_FM_E_STOP_LOGGING));
    CHECK(card_holds("USER/LOGS/VIEW.TXT", k_report, strlen(k_report)));
    CHECK(fk_get(&g_h.card, "USER/LOGS/VIEW.TXT.NEW") == NULL);
    /* While the run is open SAVE stays shut; after it, it writes. */
    settle();
    char why[128];
    files_err_text(FM_ERR_LOGGING, "VIEW.TXT", why, sizeof(why));
    save();
    CHECK_STR_EQ(note(), why);
    g_h.marks.run = 0u;
    settle();
    save();
    CHECK(!text_view_dirty());
    CHECK(card_holds("USER/LOGS/VIEW.TXT", with_front("L", k_report),
                     strlen(k_report) + 1u));
}

/*
 * ARMED after the save's first chunk of 16KiB: it stops at the next one,
 * its ".NEW" is removed and the old file stays.  The save reads the old
 * file first, 40,000B in 4 steps, to compare its sum (files_text.c).
 */
TEST_CASE(armed_part_way_stops_the_save_and_removes_its_new)
{
    const char *text = lines_of(40000u);
    open_text("LONG.TXT", text);
    edit();
    ed_type("A");
    ed_key(UI_TK_DONE);
    g_h.steps = 0;
    g_h.arm_after_steps = 6;
    save();
    CHECK(!text_view_saving());
    CHECK(text_view_dirty());
    CHECK_STR_EQ(note(), ui_tr(TX_FM_E_STOP_ARMED));
    CHECK(card_holds("USER/LOGS/LONG.TXT", text, 40000u));
    CHECK(fk_get(&g_h.card, "USER/LOGS/LONG.TXT.NEW") == NULL);
    CHECK(g_h.card.calls[FK_WRITE] >= 1);
}

/*
 * The card pulled at the save's second chunk: the note says it was not
 * saved and the old file is as it was.  Pulled at the rename, after the
 * old file was deleted: "<name>.NEW" holds the whole text for a RENAME.
 * Either way the text stays unsaved in the editor.
 */
TEST_CASE(a_card_pulled_part_way_or_at_the_rename_keeps_the_text)
{
    const char *text = lines_of(40000u);
    static char edited[40002];
    open_text("LONG.TXT", text);
    snprintf(edited, sizeof(edited), "P%s", text);
    edit();
    ed_type("P");
    ed_key(UI_TK_DONE);
    fk_fail(&g_h.card, FK_WRITE, 2, FM_ERR_CARD_GONE);
    save();
    CHECK(text_view_dirty());
    CHECK_STR_EQ(note(), ui_tr(TX_ED_NOT_SAVED_CARD));
    CHECK(card_holds("USER/LOGS/LONG.TXT", text, 40000u));
    CHECK_STR_EQ(row_at(0), "P00000000000000000000000000000000000000");

    open_text("LONG.TXT", text);
    edit();
    ed_type("P");
    ed_key(UI_TK_DONE);
    fk_fail(&g_h.card, FK_RENAME, 1, FM_ERR_CARD_GONE);
    save();
    CHECK(text_view_dirty());
    CHECK_STR_EQ(note(), ui_tr(TX_ED_NOT_SAVED_CARD));
    CHECK(fk_get(&g_h.card, "USER/LOGS/LONG.TXT") == NULL);
    CHECK(card_holds("USER/LOGS/LONG.TXT.NEW", edited, 40001u));
}

/*
 * While the card's task writes the text, a key types nothing, REVERT and
 * SAVE are shut and the home tag waits: the bytes the task reads are the
 * bytes SAVE was tapped on.
 */
TEST_CASE(the_text_holds_still_while_it_is_saved)
{
    open_text("VIEW.TXT", k_report);
    edit();
    ed_type("h");
    ed_key(UI_TK_DONE);
    g_h.defer = true;
    save();
    CHECK(text_view_saving());
    foot(FS_SPOT_TV_KEYS_CX);
    ed_type("zz");
    ed_key(UI_TK_DONE);
    foot(FS_SPOT_TV_REVERT_CX);
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_NONE);
    CHECK(home());
    CHECK(text_view_editing());
    CHECK_STR_EQ(row_at(0), "hRCBENCH SERVO TEST REPORT");
    /* Leaving FILES keeps the editor while it saves. */
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(fh_run(&g_h));
    g_h.defer = false;
    ui_router_goto(SCREEN_FILES);
    settle();
    CHECK(!text_view_saving());
    CHECK(!text_view_dirty());
    CHECK(card_holds("USER/LOGS/VIEW.TXT", with_front("h", k_report),
                     strlen(k_report) + 1u));
}

/*
 * A KST file as the programmer writes it, VERIFIED by its checksum line.
 * Edited and saved, it loses that line, the note says so, and the
 * programmer's reader takes it as an EDITED file that loads into the
 * servo it came from.  Unedited, it keeps its checksum.
 */
TEST_CASE(a_kst_file_edited_loses_its_checksum_and_loads_as_edited)
{
    const kst_image_t img = sim_bench_image();
    static char text[KST_FILE_MAX];
    const size_t len = kst_file_format(&img, KST_TYPE_V6, text, sizeof(text));
    NEED(len > 0u);
    kst_file_t f;
    NEED(kst_file_parse(text, len, &f, NULL) == KST_FILE_OK);
    NEED(f.kind == KST_FILE_VERIFIED);
    NEED(strstr(text, "checksum ") != NULL);
    open_file("BENCH/KST/CONF", "BENCH1.TXT", text, len);
    edit();
    ed_key(UI_TK_DONE);
    save();
    CHECK_STR_EQ(note(), ui_tr(TX_ED_NO_CHANGES));
    CHECK(card_holds("BENCH/KST/CONF/BENCH1.TXT", text, len));
    /* A comment line under the header. */
    foot(FS_SPOT_TV_KEYS_CX);
    tap(FS_SPOT_TV_TEXT_X + 90 * 10, FS_SPOT_TV_TEXT_Y + 5);
    ed_type("\n# bench edit");
    ed_key(UI_TK_DONE);
    save();
    CHECK(!text_view_dirty());
    CHECK_STR_EQ(note(), ui_tr(TX_ED_SAVED_KST));
    const fk_node_t *n = fk_get(&g_h.card, "BENCH/KST/CONF/BENCH1.TXT");
    NEED(n != NULL);
    char *saved = calloc(n->size + 1u, 1u);
    REQUIRE_ALLOC(saved);
    memcpy(saved, n->data, n->size);
    CHECK(strstr(saved, "checksum") == NULL);
    CHECK(strstr(saved, "format 2\n# bench edit\nType: V6\n") != NULL);
    /* The rest is the file as written, its checksum line of 18 bytes
     * gone and the comment's 13 in. */
    CHECK_EQ(n->size, len - 18u + 13u);
    unsigned bad = 0u;
    CHECK_EQ(kst_file_parse(saved, n->size, &f, &bad), KST_FILE_OK);
    CHECK_EQ(f.kind, KST_FILE_EDITED_TYPED);
    kst_file_load_t ld;
    CHECK_EQ(kst_file_load(&f, &img, &img, KST_TYPE_V6, &ld),
             KST_FILE_LOAD_OK);
    CHECK_EQ(ld.kind, KST_FILE_EDITED_TYPED);
    free(saved);
}

/*
 * A profile in BENCH/ESC/CONF is read at the panel's start: the note after
 * its save says so.
 */
TEST_CASE(an_esc_profile_says_it_is_read_at_the_next_start)
{
    open_file("BENCH/ESC/CONF", "MY.json", "{\"a\": 1}\n", 9u);
    edit();
    ed_type(" ");
    ed_key(UI_TK_DONE);
    save();
    CHECK_STR_EQ(note(), ui_tr(TX_ED_SAVED_ESC));
    CHECK(card_holds("BENCH/ESC/CONF/MY.json", " {\"a\": 1}\n", 10u));
}

/*
 * A "<name>.NEW" on the card, the only whole copy of a save cut short, is
 * never written over: the next SAVE refuses and names it, and the text
 * stays unsaved.
 */
TEST_CASE(a_new_left_by_a_cut_save_refuses_the_next_one)
{
    fresh_bare();
    card_default();
    put_n("USER/LOGS/VIEW.TXT", k_report, strlen(k_report));
    put_n("USER/LOGS/VIEW.TXT.NEW", "older\n", 6u);
    ui_router_goto(SCREEN_FILES);
    settle();
    CHECK_EQ(files_screen_open_text(CARD_USER_LOGS, "VIEW.TXT",
                                    g_h.card.mount, strlen(k_report), false),
             FM_OK);
    settle();
    edit();
    ed_type("n");
    ed_key(UI_TK_DONE);
    save();
    CHECK(text_view_dirty());
    char want[128];
    files_err_text(FM_ERR_EXISTS, "VIEW.TXT.NEW", want, sizeof(want));
    CHECK_STR_EQ(note(), want);
    CHECK(card_holds("USER/LOGS/VIEW.TXT.NEW", "older\n", 6u));
    CHECK(card_holds("USER/LOGS/VIEW.TXT", k_report, strlen(k_report)));
}

/** @brief LONG.TXT edited by one "Q" in front, SAVE tapped with the card
 *         task holding requests, and NOTE.TXT opened over it. */
static const char *save_held_then_open(void)
{
    const char *text = lines_of(40000u);
    open_text("LONG.TXT", text);
    put_n("USER/LOGS/NOTE.TXT", "note\n", 5u);
    edit();
    ed_type("Q");
    ed_key(UI_TK_DONE);
    g_h.defer = true;
    save();
    CHECK(text_view_saving());
    CHECK_EQ(files_screen_open_text(CARD_USER_LOGS, "NOTE.TXT",
                                    g_h.card.mount, 5u, false),
             FM_OK);
    settle();
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_OPEN);
    /* DISCARD is shut while the card task reads the text. */
    tap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_OPEN);
    CHECK_STR_EQ(text_view_name(), "LONG.TXT");
    CHECK_STR_EQ(row_at(0), "Q00000000000000000000000000000000000000");
    return text;
}

/*
 * Another file opened over a text being saved: the question shows, its
 * DISCARD does nothing while the save reads the text, and the save writes
 * the text as SAVE was tapped on.  Saved, the text has no changes left to
 * lose: the question closes and the other file opens.
 */
TEST_CASE(an_open_asked_during_a_save_waits_for_it)
{
    const char *text = save_held_then_open();
    static char edited[40002];
    snprintf(edited, sizeof(edited), "Q%s", text);
    CHECK(fh_run(&g_h));
    g_h.defer = false;
    settle();
    CHECK(card_holds("USER/LOGS/LONG.TXT", edited, 40001u));
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_NONE);
    CHECK_STR_EQ(text_view_name(), "NOTE.TXT");
    CHECK_EQ(text_view_state(), TEXT_VIEW_SHOWN);
    CHECK_STR_EQ(row_at(0), "note");
}

/*
 * The save under the question fails (the card full at its second chunk):
 * the question stays, the text unsaved, and DISCARD acts again.
 */
TEST_CASE(a_save_that_fails_under_the_question_leaves_discard_live)
{
    const char *text = save_held_then_open();
    fk_fail(&g_h.card, FK_WRITE, 2, FM_ERR_FULL);
    CHECK(fh_run(&g_h));
    g_h.defer = false;
    settle();
    CHECK(!text_view_saving());
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_OPEN);
    CHECK(text_view_dirty());
    CHECK(card_holds("USER/LOGS/LONG.TXT", text, 40000u));
    CHECK(fk_get(&g_h.card, "USER/LOGS/LONG.TXT.NEW") == NULL);
    tap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    settle();
    CHECK_STR_EQ(text_view_name(), "NOTE.TXT");
    CHECK_STR_EQ(row_at(0), "note");
}

/*
 * SAVE tapped while a listing runs on the card task: the save waits for
 * its turn, and from the tap on no key types and REVERT does not ask.
 */
TEST_CASE(the_text_holds_still_while_the_save_waits_its_turn)
{
    open_text("VIEW.TXT", k_report);
    edit();
    ed_type("a");
    ed_key(UI_TK_DONE);
    g_h.defer = true;
    save();
    CHECK(fh_run(&g_h));                /* the save */
    settle();                           /* its answer; the panes' reads */
    CHECK(files_screen_busy());
    CHECK(!text_view_saving());
    foot(FS_SPOT_TV_KEYS_CX);
    ed_type("b");
    ed_key(UI_TK_DONE);
    save();                             /* waits for the listing */
    CHECK(text_view_saving());
    CHECK_STR_EQ(note(), ui_tr(TX_ED_SAVING));
    foot(FS_SPOT_TV_KEYS_CX);
    ed_type("c");
    ed_key(UI_TK_DEL);
    ed_key(UI_TK_DONE);
    CHECK_STR_EQ(row_at(0), "abRCBENCH SERVO TEST REPORT");
    foot(FS_SPOT_TV_REVERT_CX);
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_NONE);
    for (int i = 0; i < 4 && fh_run(&g_h); ++i) {
        settle();
    }
    g_h.defer = false;
    settle();
    CHECK(!text_view_saving());
    CHECK(!text_view_dirty());
    CHECK(card_holds("USER/LOGS/VIEW.TXT", with_front("ab", k_report),
                     strlen(k_report) + 2u));
}

/*
 * A file with a NUL at byte 4,096, one past the first 4kB the open tests.
 * A deleted first byte brings the NUL into those 4kB: SAVE refuses, as
 * FILES would not open the file again, the card keeps the file and REVERT
 * puts all 4,100 bytes back.  A byte typed in front moves the NUL out
 * again, and SAVE writes the file.
 */
TEST_CASE(save_refuses_a_nul_in_the_first_4kb_and_revert_keeps_the_text)
{
    static char text[4100];
    for (size_t i = 0u; i < 4096u; ++i) {
        text[i] = (i % 40u == 39u) ? '\n' : 'x';
    }
    memcpy(text + 4096, "\0yz\n", 4u);
    NEED(text[4095] == 'x');
    open_file(CARD_USER_LOGS, "NUL.TXT", text, sizeof(text));
    NEED(text_view_state() == TEXT_VIEW_SHOWN);
    edit();
    ed_key(UI_TK_RIGHT);
    ed_key(UI_TK_DEL);
    ed_key(UI_TK_DONE);
    NEED(text_view_dirty());
    const long writes = g_h.card.calls[FK_WRITE];
    save();
    CHECK(!text_view_saving());
    CHECK_STR_EQ(note(), ui_tr(TX_ED_NUL_AHEAD));
    CHECK_EQ(g_h.card.calls[FK_WRITE], writes);
    CHECK(card_holds("USER/LOGS/NUL.TXT", text, sizeof(text)));
    CHECK(text_view_dirty());
    foot(FS_SPOT_TV_REVERT_CX);
    NEED(text_view_ask() == TEXT_VIEW_ASK_REVERT);
    tap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    CHECK(!text_view_dirty());
    static char back[4100];
    CHECK_EQ(text_view_fill(NULL, 0u, (uint8_t *)back, sizeof(back)),
             sizeof(text));
    CHECK_EQ(text_view_fill(NULL, sizeof(text), (uint8_t *)back, 1u), 0u);
    CHECK(memcmp(back, text, sizeof(text)) == 0);
    /* One byte in front: the NUL is at 4,096 again. */
    foot(FS_SPOT_TV_KEYS_CX);
    ed_key(UI_TK_RIGHT);
    ed_key(UI_TK_DEL);
    ed_type("a");
    ed_key(UI_TK_DONE);
    save();
    settle();
    CHECK(!text_view_dirty());
    text[0] = 'a';
    CHECK(card_holds("USER/LOGS/NUL.TXT", text, sizeof(text)));
}

/*
 * An ESC settings file as the programmer writes it: a change puts `edited` in place
 * of the checksum line, so the programmer reads its field lines.  Unedited, it keeps
 * its checksum.
 */
TEST_CASE(an_esc_file_edited_says_edited_and_reads_as_edited)
{
    static char text[EW_FILE_MAX];
    static uint8_t page[EW_MAP_LEN_AM];
    static ew_file_t f;
    ew_contact_t c;
    ew_ident_t id;

    memset(&c, 0, sizeof(c));
    memset(&id, 0, sizeof(id));
    memset(page, 1, sizeof(page));
    page[1] = 4u;
    c.cpu = EW_CPU_ARM;
    c.pin_code = 0x26u;
    c.flash_code = 0x1Fu;
    c.protocol = 2u;
    id.fw = EW_FW_AM;
    id.layout = 4u;
    id.writable = true;
    id.len = EW_MAP_LEN_AM;
    snprintf(id.name, sizeof(id.name), "BOARD");
    NEED(ew_file_capture(&f, &c, &id, page));
    const size_t len = ew_file_format(&f, text, sizeof(text));

    NEED(len > 0u);
    NEED(ew_file_parse(text, len, &f, NULL) == EW_FILE_OK);
    NEED(f.kind == EW_FILE_VERIFIED);
    open_file("BENCH/ESCW/CONF", "ESCW001.TXT", text, len);
    edit();
    ed_key(UI_TK_DONE);
    save();
    CHECK_STR_EQ(note(), ui_tr(TX_ED_NO_CHANGES));
    CHECK(card_holds("BENCH/ESCW/CONF/ESCW001.TXT", text, len));
    foot(FS_SPOT_TV_KEYS_CX);
    tap(FS_SPOT_TV_TEXT_X + 90 * 10, FS_SPOT_TV_TEXT_Y + 5);
    ed_type("\n# bench edit");
    ed_key(UI_TK_DONE);
    save();
    CHECK(!text_view_dirty());
    CHECK_STR_EQ(note(), ui_tr(TX_ED_SAVED_EW));
    const fk_node_t *n = fk_get(&g_h.card, "BENCH/ESCW/CONF/ESCW001.TXT");

    NEED(n != NULL);
    char *saved = calloc(n->size + 1u, 1u);

    REQUIRE_ALLOC(saved);
    memcpy(saved, n->data, n->size);
    CHECK(strstr(saved, "checksum") == NULL);
    CHECK(strstr(saved, "\nedited\n") != NULL);
    CHECK_EQ(n->size, len - 17u + 6u + 13u);
    unsigned bad = 0u;

    CHECK_EQ(ew_file_parse(saved, n->size, &f, &bad), EW_FILE_OK);
    CHECK_EQ(f.kind, EW_FILE_EDITED);
    free(saved);
}

/**
 * @brief A KST file as kst_file_format() writes it, its checksum line
 *        swapped for @p sum (and a line end), into @p out.
 * @param sum  the line in its place, without a line end
 * @param out  KST_FILE_MAX + 64 bytes
 * @return the text's bytes
 */
static size_t kst_with_sum(const char *sum, char *out)
{
    const kst_image_t img = sim_bench_image();
    const size_t len = kst_file_format(&img, KST_TYPE_V6, out, KST_FILE_MAX);
    char *at = strstr(out, "checksum ");
    if (len == 0u || at == NULL) {
        return 0u;
    }
    const size_t n = strlen(sum);
    memcpy(at, sum, n);
    at[n] = '\n';
    return (size_t)(at - out) + n + 1u;
}

/*
 * The checksum line in the forms the programmer's reader takes for one
 * (kst_file_checksum_line()): another case, '=' or ':' after the word,
 * a tab, spaces in front, the word alone, and two such lines.  SAVE takes
 * every one of them out, and the reader then loads the file as EDITED.
 */
TEST_CASE(save_takes_out_every_line_the_programmer_reads_as_a_checksum)
{
    static const char *const sums[] = {
        "CHECKSUM 00000000",  "checksum=00000000",
        "checksum:00000000",  "Checksum\t00000000",
        "   checksum 0",      "checksum",
        "checksum 0\nCheckSum: 1",
    };
    static char text[KST_FILE_MAX + 64];
    const kst_image_t img = sim_bench_image();
    for (size_t i = 0u; i < sizeof(sums) / sizeof(sums[0]); ++i) {
        const size_t len = kst_with_sum(sums[i], text);
        NEED(len > 0u);
        open_file("BENCH/KST/CONF", "BENCH1.TXT", text, len);
        edit();
        ed_key(UI_TK_DONE);
        foot(FS_SPOT_TV_KEYS_CX);
        tap(FS_SPOT_TV_TEXT_X + 90 * 10, FS_SPOT_TV_TEXT_Y + 5);
        ed_type("\n# bench edit");
        ed_key(UI_TK_DONE);
        save();
        CHECK(!text_view_dirty());
        CHECK_STR_EQ(note(), ui_tr(TX_ED_SAVED_KST));
        const fk_node_t *n = fk_get(&g_h.card, "BENCH/KST/CONF/BENCH1.TXT");
        NEED(n != NULL);
        kst_file_t f;
        unsigned bad = 0u;
        CHECK_EQ(kst_file_parse((const char *)n->data, n->size, &f, &bad),
                 KST_FILE_OK);
        CHECK_EQ(f.kind, KST_FILE_EDITED_TYPED);
        kst_file_load_t ld;
        CHECK_EQ(kst_file_load(&f, &img, &img, KST_TYPE_V6, &ld),
                 KST_FILE_LOAD_OK);
    }
}

int main(void)
{
    RUN(save_writes_the_text_through_new_and_keeps_the_name);
    RUN(save_is_shut_while_armed_and_the_text_stays);
    RUN(a_log_that_opens_before_the_save_runs_stops_it);
    RUN(armed_part_way_stops_the_save_and_removes_its_new);
    RUN(a_card_pulled_part_way_or_at_the_rename_keeps_the_text);
    RUN(the_text_holds_still_while_it_is_saved);
    RUN(a_kst_file_edited_loses_its_checksum_and_loads_as_edited);
    RUN(an_esc_file_edited_says_edited_and_reads_as_edited);
    RUN(an_esc_profile_says_it_is_read_at_the_next_start);
    RUN(a_new_left_by_a_cut_save_refuses_the_next_one);
    RUN(an_open_asked_during_a_save_waits_for_it);
    RUN(a_save_that_fails_under_the_question_leaves_discard_live);
    RUN(the_text_holds_still_while_the_save_waits_its_turn);
    RUN(save_refuses_a_nul_in_the_first_4kb_and_revert_keeps_the_text);
    RUN(save_takes_out_every_line_the_programmer_reads_as_a_checksum);
    fh_free(&g_h);
    free(s_fb);
    return test_summary("files_save");
}
