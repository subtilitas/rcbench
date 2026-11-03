/**
 * @file test_files_screen.c
 * @brief FILES' panes over a card in memory: where they start, their order
 *        and counts, the taps that select, switch and open, UP, PREV and
 *        NEXT, the scroll by drag and by the bar, the card gone, swapped or
 *        never there, a task that does not start, a listing under way, and
 *        RESCAN, which keeps each pane's page, top row and selection.
 *
 * Cases:
 * - the_panes_start_in_the_root_and_user_log_folder
 * - folders_come_first_and_hidden_names_are_counted
 * - a_tap_selects_and_a_second_tap_opens_a_folder
 * - up_climbs_one_folder_and_the_root_has_none
 * - a_tap_on_the_other_pane_makes_it_the_active_one
 * - a_folder_of_120_entries_pages_with_prev_and_next
 * - a_list_scrolls_by_a_drag_and_by_its_bar
 * - a_folder_that_holds_only_a_hidden_file_is_not_empty
 * - with_no_card_both_panes_say_so_and_writes_are_refused
 * - without_a_card_task_both_panes_show_no_card
 * - a_task_that_does_not_start_says_so
 * - a_swapped_card_sends_both_panes_to_their_start_folders
 * - a_listing_under_way_holds_the_next_until_it_is_answered
 * - a_folder_gone_before_it_is_entered_keeps_the_pane_and_says_so
 * - every_way_in_reads_both_panes_again_and_looks_at_the_slot
 * - the_files_the_logger_writes_are_marked
 * - the_user_log_folder_in_force_is_the_right_panes_start
 * - the_message_line_follows_the_language
 * - rescan_keeps_each_panes_page_top_row_and_selection
 * - rescan_keeps_a_later_page_from_its_first_entry
 * - rescan_selects_none_when_the_selected_entry_is_gone
 * - rescan_selects_none_when_the_selection_leaves_the_page
 * - rescan_keeps_the_top_rows_entry_on_top
 * - rescan_holds_the_scroll_to_fewer_rows
 * - rescan_of_a_folder_gone_shows_no_rows
 * - rescan_with_the_card_out_says_no_card
 * - rescan_waits_behind_a_request_under_way
 * - rescan_during_a_page_turn_turns_the_page_first
 *
 * SPDX-License-Identifier: MIT
 */
#include "files_rig.h"

TEST_CASE(the_panes_start_in_the_root_and_user_log_folder)
{
    fresh();
    const fm_tree_t *l = files_screen_pane(0);
    const fm_tree_t *r = files_screen_pane(1);
    CHECK_STR_EQ(l->dir, "");
    CHECK_STR_EQ(r->dir, "USER/LOGS");
    CHECK_EQ(files_screen_active(), 0);
    CHECK_EQ(l->selected, -1);
    CHECK(!files_screen_busy());
    CHECK_EQ(g_h.submits, 2u);
    CHECK_EQ(g_h.looks, 1u);     /* the first request looked at the slot */
    CHECK(files_screen_pane(2) == NULL);
    CHECK(files_screen_pane(-1) == NULL);
    CHECK_EQ(files_screen_scroll(5), 0);
    draw();
}

TEST_CASE(folders_come_first_and_hidden_names_are_counted)
{
    fresh();
    const fm_tree_t *l = files_screen_pane(0);
    CHECK_EQ(l->held, 3);
    CHECK_EQ(l->total, 3);
    CHECK_STR_EQ(l->page[0].name, "BENCH");
    CHECK_STR_EQ(l->page[1].name, "USER");
    CHECK_STR_EQ(l->page[2].name, "NOTES.TXT");
    CHECK_EQ(l->unlisted, 1u);   /* .DS_Store */
    CHECK_EQ(l->page[0].place, FM_PLACE_FIXED);
    CHECK_EQ(l->page[2].kind, FM_KIND_TEXT);
    const fm_tree_t *r = files_screen_pane(1);
    CHECK_EQ(r->held, 3);
    CHECK_STR_EQ(r->page[0].name, "ATEST005.CSV");
    CHECK_EQ(r->page[0].kind, FM_KIND_RUN);
    CHECK_EQ(r->page[2].kind, FM_KIND_REPORT);
}

TEST_CASE(a_tap_selects_and_a_second_tap_opens_a_folder)
{
    fresh();
    tap_row(0, 0);
    CHECK_EQ(files_screen_pane(0)->selected, 0);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "");
    tap_row(0, 0);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "BENCH");
    CHECK_EQ(files_screen_pane(0)->held, 4);   /* ESC, KST, LOGS, the log */
    CHECK_STR_EQ(files_screen_pane(0)->page[3].name, "RCBENCH.LOG");
    CHECK_EQ(files_screen_pane(0)->page[3].kind, FM_KIND_FAULT);
    /* OPEN on a selected folder does the same. */
    CHECK(pick(0, "KST"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "BENCH/KST");
    /* A tap past the last row selects nothing. */
    tap_row(0, 3);
    CHECK_EQ(files_screen_pane(0)->selected, -1);
}

TEST_CASE(up_climbs_one_folder_and_the_root_has_none)
{
    fresh();
    const unsigned before = g_h.submits;
    tap(FS_SPOT_UP_CX(0), FS_SPOT_HEAD_CY);
    CHECK_EQ(g_h.submits, before);        /* the root: nothing to climb */
    CHECK(open_dir(0, "USER"));
    CHECK(open_dir(0, "LOGS"));
    CHECK_STR_EQ(files_screen_pane(0)->dir, "USER/LOGS");
    tap(FS_SPOT_UP_CX(0), FS_SPOT_HEAD_CY);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "USER");
    /* The folder come from is selected. */
    CHECK_EQ(files_screen_pane(0)->selected, row_of(0, "LOGS"));
    tap(FS_SPOT_UP_CX(0), FS_SPOT_HEAD_CY);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "");
    CHECK_EQ(files_screen_pane(0)->selected, row_of(0, "USER"));
    /* UP of the right pane climbs that pane and makes it active. */
    tap(FS_SPOT_UP_CX(1), FS_SPOT_HEAD_CY);
    CHECK_STR_EQ(files_screen_pane(1)->dir, "USER");
    CHECK_EQ(files_screen_active(), 1);
    /* A press that slides off UP climbs nothing. */
    send(TOUCH_EVENT_DOWN, FS_SPOT_UP_CX(1), FS_SPOT_HEAD_CY);
    send(TOUCH_EVENT_MOVE, FS_SPOT_UP_CX(1), FS_SPOT_HEAD_CY + 200);
    send(TOUCH_EVENT_UP, FS_SPOT_UP_CX(1), FS_SPOT_HEAD_CY + 200);
    settle();
    CHECK_STR_EQ(files_screen_pane(1)->dir, "USER");
}

TEST_CASE(a_tap_on_the_other_pane_makes_it_the_active_one)
{
    fresh();
    CHECK_EQ(files_screen_active(), 0);
    tap_row(1, 2);
    CHECK_EQ(files_screen_active(), 1);
    CHECK_EQ(files_screen_pane(1)->selected, 2);
    /* The first tap there selected; the left pane keeps its own. */
    tap_row(0, 2);
    CHECK_EQ(files_screen_active(), 0);
    CHECK_EQ(files_screen_pane(0)->selected, 2);
    CHECK_EQ(files_screen_pane(1)->selected, 2);
    tap_row(1, 2);
    CHECK_EQ(files_screen_active(), 1);
    /* A second tap on a file of the active pane opens nothing here: the
     * report stays where it is, and the message says where it opens. */
    tap_row(1, 2);
    CHECK_STR_EQ(files_screen_pane(1)->dir, "USER/LOGS");
}

TEST_CASE(a_folder_of_120_entries_pages_with_prev_and_next)
{
    fresh_bare();
    card_default();
    char path[64];
    for (int i = 0; i < 120; ++i) {
        snprintf(path, sizeof(path), "BIG/F%03d.TXT", i);
        fk_file(&g_h.card, path, NULL, 100u);
    }
    screen()->enter();
    settle();
    CHECK(open_dir(0, "BIG"));
    const fm_tree_t *t = files_screen_pane(0);
    CHECK_EQ(t->total, 120);
    CHECK_EQ(t->held, 48);
    CHECK_EQ(t->ahead, 0);
    CHECK_STR_EQ(t->page[0].name, "F000.TXT");
    draw();
    tap(FS_SPOT_NEXT_CX(0), FS_SPOT_HEAD_CY);
    CHECK_EQ(t->ahead, 48);
    CHECK_STR_EQ(t->page[0].name, "F048.TXT");
    tap(FS_SPOT_NEXT_CX(0), FS_SPOT_HEAD_CY);
    CHECK_EQ(t->ahead, 96);
    CHECK_EQ(t->held, 24);
    /* NEXT at the last page goes nowhere. */
    const unsigned before = g_h.submits;
    tap(FS_SPOT_NEXT_CX(0), FS_SPOT_HEAD_CY);
    CHECK_EQ(g_h.submits, before);
    tap(FS_SPOT_PREV_CX(0), FS_SPOT_HEAD_CY);
    CHECK_EQ(t->ahead, 48);
    tap(FS_SPOT_PREV_CX(0), FS_SPOT_HEAD_CY);
    CHECK_EQ(t->ahead, 0);
    tap(FS_SPOT_PREV_CX(0), FS_SPOT_HEAD_CY);
    CHECK_EQ(t->ahead, 0);
    /* The right pane, one page, has no PREV and NEXT: a tap there is the
     * header row's, which holds nothing at that point. */
    const unsigned now = g_h.submits;
    tap(FS_SPOT_NEXT_CX(1), FS_SPOT_HEAD_CY);
    CHECK_EQ(g_h.submits, now);
}

TEST_CASE(a_list_scrolls_by_a_drag_and_by_its_bar)
{
    fresh_bare();
    card_default();
    char path[64];
    for (int i = 0; i < 20; ++i) {
        snprintf(path, sizeof(path), "MANY/F%02d.TXT", i);
        fk_file(&g_h.card, path, NULL, 10u);
    }
    screen()->enter();
    settle();
    CHECK(open_dir(0, "MANY"));
    CHECK_EQ(files_screen_pane(0)->held, 20);
    CHECK_EQ(files_screen_scroll(0), 0);
    /* A drag up of 100px moves the list 2 rows, and selects nothing. */
    send(TOUCH_EVENT_DOWN, FS_SPOT_ROW_CX(0), FS_SPOT_ROW_CY(4));
    send(TOUCH_EVENT_MOVE, FS_SPOT_ROW_CX(0), FS_SPOT_ROW_CY(4) - 100);
    send(TOUCH_EVENT_UP, FS_SPOT_ROW_CX(0), FS_SPOT_ROW_CY(4) - 100);
    CHECK_EQ(files_screen_scroll(0), 2);
    CHECK_EQ(files_screen_pane(0)->selected, -1);
    /* The thumb taken and pulled to the track's foot: the last rows. */
    send(TOUCH_EVENT_DOWN, FS_SPOT_BAR_CX(0), FS_SPOT_BAR_TOP + 40);
    send(TOUCH_EVENT_MOVE, FS_SPOT_BAR_CX(0),
         FS_SPOT_BAR_TOP + FS_SPOT_BAR_H + 50);
    send(TOUCH_EVENT_UP, FS_SPOT_BAR_CX(0),
         FS_SPOT_BAR_TOP + FS_SPOT_BAR_H + 50);
    CHECK_EQ(files_screen_scroll(0), 20 - FS_SPOT_ROWS);
    draw();
    /* A press on the track above the thumb brings it there. */
    send(TOUCH_EVENT_DOWN, FS_SPOT_BAR_CX(0), FS_SPOT_BAR_TOP + 2);
    send(TOUCH_EVENT_UP, FS_SPOT_BAR_CX(0), FS_SPOT_BAR_TOP + 2);
    CHECK_EQ(files_screen_scroll(0), 0);
    /* A row tapped after a scroll is the row under the finger. */
    send(TOUCH_EVENT_DOWN, FS_SPOT_BAR_CX(0), FS_SPOT_BAR_TOP + 200);
    send(TOUCH_EVENT_UP, FS_SPOT_BAR_CX(0), FS_SPOT_BAR_TOP + 200);
    const int first = files_screen_scroll(0);
    CHECK(first > 0);
    tap_row(0, 1);
    CHECK_EQ(files_screen_pane(0)->selected, first + 1);
    /* A second finger presses nothing while the first holds a row. */
    send_id(TOUCH_EVENT_DOWN, 1, FS_SPOT_ROW_CX(0), FS_SPOT_ROW_CY(3));
    send_id(TOUCH_EVENT_DOWN, 2, FS_SPOT_ROW_CX(0), FS_SPOT_ROW_CY(4));
    send_id(TOUCH_EVENT_UP, 2, FS_SPOT_ROW_CX(0), FS_SPOT_ROW_CY(4));
    CHECK_EQ(files_screen_pane(0)->selected, first + 1);
    send_id(TOUCH_EVENT_UP, 1, FS_SPOT_ROW_CX(0), FS_SPOT_ROW_CY(3));
    CHECK_EQ(files_screen_pane(0)->selected, first + 3);
}

TEST_CASE(a_folder_that_holds_only_a_hidden_file_is_not_empty)
{
    fresh_bare();
    card_default();
    fk_file(&g_h.card, "HID/.DS_Store", NULL, 6148u);
    screen()->enter();
    settle();
    CHECK(open_dir(0, "HID"));
    CHECK_EQ(files_screen_pane(0)->held, 0);
    CHECK_EQ(files_screen_pane(0)->unlisted, 1u);
    draw();
    /* DELETE of it, from the folder above, is refused by the card: it is
     * not empty. */
    tap(FS_SPOT_UP_CX(0), FS_SPOT_HEAD_CY);
    CHECK(pick(0, "HID"));
    tap_foot(FS_SPOT_DELETE);
    CHECK_EQ(files_screen_view(), FILES_VIEW_DELETE);
    tap(FS_SPOT_Q_DELETE_CX, FS_SPOT_BOX_CY);
    CHECK(fk_get(&g_h.card, "HID") != NULL);
    CHECK(strstr(files_screen_message(), "not empty") != NULL);
}

TEST_CASE(with_no_card_both_panes_say_so_and_writes_are_refused)
{
    fresh_bare();
    fk_pull(&g_h.card);
    g_h.state = CARD_STATE_NO_FAT;
    screen()->enter();
    settle();
    CHECK_EQ(files_screen_pane(0)->total, -1);
    CHECK_EQ(files_screen_pane(1)->total, -1);
    draw();
    tap_foot(FS_SPOT_NEW);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_STR_EQ(files_screen_message(), TR(FM_E_NO_CARD));
    tap_foot(FS_SPOT_COPY);
    CHECK_STR_EQ(files_screen_message(), TR(FM_SELECT_FIRST));
    /* A card put in and RESCAN: both panes read. */
    fk_insert(&g_h.card);
    card_default();
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(files_screen_pane(0)->total, 3);
    CHECK_EQ(files_screen_pane(1)->total, 3);
}

TEST_CASE(without_a_card_task_both_panes_show_no_card)
{
    fresh_bare();
    files_screen_set_io(NULL);
    screen()->reset();
    screen()->enter();
    settle();
    CHECK_EQ(files_screen_pane(0)->total, -1);
    CHECK_EQ(files_screen_pane(1)->total, -1);
    CHECK(!files_screen_busy());
    draw();
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(files_screen_pane(0)->total, -1);
    CHECK_EQ(g_h.submits, 0u);
}

TEST_CASE(a_task_that_does_not_start_says_so)
{
    fresh_bare();
    card_default();
    g_h.fail_submit = true;
    screen()->enter();
    settle();
    CHECK_STR_EQ(files_screen_message(), TR(LOG_ERR_MEMORY));
    CHECK_EQ(files_screen_pane(0)->total, -1);
    CHECK_EQ(files_screen_pane(1)->total, -1);
    CHECK(!files_screen_busy());
    draw();
    /* Once it starts, RESCAN reads both. */
    g_h.fail_submit = false;
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(files_screen_pane(0)->total, 3);
}

TEST_CASE(a_swapped_card_sends_both_panes_to_their_start_folders)
{
    fresh();
    CHECK(open_dir(0, "BENCH"));
    tap(FS_SPOT_UP_CX(1), FS_SPOT_HEAD_CY);
    CHECK_STR_EQ(files_screen_pane(1)->dir, "USER");
    CHECK_STR_EQ(files_screen_pane(0)->dir, "BENCH");
    fk_pull(&g_h.card);
    fk_insert(&g_h.card);           /* the same files, another mount */
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "");
    CHECK_STR_EQ(files_screen_pane(1)->dir, "USER/LOGS");
    CHECK_EQ(files_screen_pane(0)->mount, g_h.card.mount);
    CHECK_EQ(files_screen_pane(1)->mount, g_h.card.mount);
    CHECK_EQ(files_screen_pane(0)->total, 3);
}

TEST_CASE(a_listing_under_way_holds_the_next_until_it_is_answered)
{
    fresh_bare();
    card_default();
    g_h.defer = true;
    screen()->enter();
    settle();
    CHECK(files_screen_busy());
    CHECK_EQ(g_h.submits, 1u);
    draw();                       /* the heading says the card is read */
    /* The footer takes no job while a request is under way. */
    tap_foot(FS_SPOT_NEW);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK(fh_run(&g_h));
    settle();
    CHECK_EQ(g_h.submits, 2u);    /* the right pane's, now */
    CHECK(fh_run(&g_h));
    settle();
    CHECK(!files_screen_busy());
    CHECK_EQ(files_screen_pane(0)->total, 3);
    CHECK_EQ(files_screen_pane(1)->total, 3);
    CHECK(!fh_run(&g_h));
}

TEST_CASE(a_folder_gone_before_it_is_entered_keeps_the_pane_and_says_so)
{
    fresh();
    CHECK(pick(0, "USER"));
    fk_fail(&g_h.card, FK_WALK, 1, FM_ERR_NOT_FOUND);
    tap_foot(FS_SPOT_OPEN);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "");
    char want[160];
    snprintf(want, sizeof(want), TR(FM_E_NOT_FOUND), "USER");
    CHECK_STR_EQ(files_screen_message(), want);
    draw();
    /* A path deeper than the card's walk takes is refused as a name. */
    char path[160] = "";
    for (int d = 0; d < 17; ++d) {
        strcat(path, (d == 0) ? "D" : "/D");
    }
    fk_dir(&g_h.card, path);
    tap_foot(FS_SPOT_RESCAN);
    CHECK(pick(0, "D"));
    for (int d = 0; d < 16; ++d) {
        CHECK(open_dir(0, "D"));
    }
    CHECK_EQ(fm_path_depth(files_screen_pane(0)->dir), 16u);
    CHECK(pick(0, "D"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(fm_path_depth(files_screen_pane(0)->dir), 16u);
    CHECK_STR_EQ(files_screen_message(), TR(FM_E_NAME));
}

TEST_CASE(every_way_in_reads_both_panes_again_and_looks_at_the_slot)
{
    fresh();
    CHECK(open_dir(0, "BENCH"));
    const unsigned subs = g_h.submits;
    const unsigned looks = g_h.looks;
    screen()->leave();
    fk_file(&g_h.card, "BENCH/NEW.TXT", NULL, 4u);
    screen()->enter();
    settle();
    CHECK_EQ(g_h.submits, subs + 2u);
    CHECK_EQ(g_h.looks, looks + 1u);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "BENCH");
    CHECK(row_of(0, "NEW.TXT") >= 0);
}

TEST_CASE(the_files_the_logger_writes_are_marked)
{
    fresh();
    g_h.marks.test = 5u;            /* ATEST005.CSV in USER/LOGS is open */
    tap_foot(FS_SPOT_RESCAN);
    const fm_tree_t *r = files_screen_pane(1);
    CHECK(r->page[row_of(1, "ATEST005.CSV")].busy);
    CHECK(!r->page[row_of(1, "LIMIT006.TXT")].busy);
    draw();
    /* And KST.TXT is the programmer's record. */
    CHECK(open_dir(0, "BENCH"));
    CHECK(open_dir(0, "KST"));
    CHECK(open_dir(0, "CONF"));
    CHECK_EQ(files_screen_pane(0)->page[row_of(0, "KST.TXT")].place,
             FM_PLACE_LOCKED);
    draw();
}

TEST_CASE(the_user_log_folder_in_force_is_the_right_panes_start)
{
    fresh_bare();
    card_default();
    fk_dir(&g_h.card, "MYLOGS");
    settings_set_text(SET_TEXT_LOG_USER_DIR, "MYLOGS");
    screen()->enter();
    settle();
    CHECK_STR_EQ(files_screen_pane(1)->dir, "MYLOGS");
    /* That folder is one the bench keeps now. */
    CHECK_EQ(files_screen_pane(0)->page[row_of(0, "MYLOGS")].place,
             FM_PLACE_FIXED);
    settings_set_text(SET_TEXT_LOG_USER_DIR, CARD_USER_LOGS);
}

/**
 * @brief The message line in @p lang for @p id with @p name.
 * @param lang  the language
 * @param id    the text, a format of one name
 * @param name  the name
 * @return the line, in a buffer the next call overwrites
 */
static const char *line_in(ui_lang_t lang, ui_text_id_t id, const char *name)
{
    static char out[192];
    snprintf(out, sizeof(out), ui_tr_in(lang, id), name);
    return out;
}

TEST_CASE(the_message_line_follows_the_language)
{
    fresh();
    /* A refusal with no name: COPY with nothing selected. */
    tap_foot(FS_SPOT_COPY);
    const char *en = ui_tr_in(UI_LANG_EN, TX_FM_SELECT_FIRST);
    const char *de = ui_tr_in(UI_LANG_DE, TX_FM_SELECT_FIRST);
    CHECK(strcmp(en, de) != 0);
    CHECK_STR_EQ(files_screen_message(), en);
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(files_screen_message(), de);
    draw();
    /* A result that names its file, said in German, then read in English. */
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_COPY);
    CHECK_STR_EQ(files_screen_message(),
                 line_in(UI_LANG_DE, TX_FM_COPIED, "NOTES.TXT"));
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(files_screen_message(),
                 line_in(UI_LANG_EN, TX_FM_COPIED, "NOTES.TXT"));
    /* A refusal that names its file: the copy is there now. */
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_COPY);
    CHECK_STR_EQ(files_screen_message(),
                 line_in(UI_LANG_EN, TX_FM_E_EXISTS, "NOTES.TXT"));
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(files_screen_message(),
                 line_in(UI_LANG_DE, TX_FM_E_EXISTS, "NOTES.TXT"));
    draw();
    ui_text_set_language(UI_LANG_EN);
}

/**
 * @brief Remove @p path from the card, as a computer does behind the
 *        panel's back: a file, or an empty folder.
 */
static void gone(const char *path)
{
    const fm_io_t io = fk_io(&g_h.card);
    const fk_node_t *n = fk_get(&g_h.card, path);
    CHECK(n != NULL);
    if (n == NULL) {
        return;
    }
    const fm_err_t err = n->dir ? io.rmdir(io.ctx, path)
                                : io.unlink(io.ctx, path);
    CHECK(err == FM_OK);
}

/** @brief Pane @p i's list dragged up by @p rows rows, 4 at a drag. */
static void scroll_down(int i, int rows)
{
    while (rows > 0) {
        const int k = (rows > 4) ? 4 : rows;
        const int y0 = FS_SPOT_ROW_CY(FS_SPOT_ROWS - 1);
        const int y1 = y0 - k * 48 - 10;
        send(TOUCH_EVENT_DOWN, FS_SPOT_ROW_CX(i), y0);
        send(TOUCH_EVENT_MOVE, FS_SPOT_ROW_CX(i), y1);
        send(TOUCH_EVENT_UP, FS_SPOT_ROW_CX(i), y1);
        rows -= k;
    }
}

/** @brief The name on row @p r on screen of pane @p i. */
static const char *on_screen(int i, int r)
{
    const fm_tree_t *t = files_screen_pane(i);
    const int at = files_screen_scroll(i) + r;
    return (at >= 0 && at < t->held) ? t->page[at].name : "";
}

/** @brief The selected entry's name of pane @p i, "" for none. */
static const char *chosen(int i)
{
    const fm_tree_t *t = files_screen_pane(i);
    return (t->selected >= 0 && t->selected < t->held)
               ? t->page[t->selected].name : "";
}

/**
 * @brief The card of a beta bench: SERVO001 to SERVO030 in BENCH/LOGS,
 *        ATEST001 to ATEST012 in USER/LOGS; the left pane in BENCH/LOGS
 *        with SERVO018.CSV on top and SERVO020.CSV selected, the right
 *        one with ATEST004.CSV on top and ATEST006.CSV selected.
 */
static void two_scrolled_panes(void)
{
    fresh_bare();
    card_default();
    char path[64];
    for (int k = 2; k <= 30; ++k) {
        snprintf(path, sizeof(path), "BENCH/LOGS/SERVO%03d.CSV", k);
        fk_file(&g_h.card, path, NULL, 4096u);
    }
    gone("USER/LOGS/ATEST005.CSV");
    gone("USER/LOGS/ATEST005.TXT");
    gone("USER/LOGS/LIMIT006.TXT");
    for (int k = 1; k <= 12; ++k) {
        snprintf(path, sizeof(path), "USER/LOGS/ATEST%03d.CSV", k);
        fk_file(&g_h.card, path, NULL, 15104u);
    }
    screen()->enter();
    settle();
    CHECK(open_dir(0, "BENCH"));
    CHECK(open_dir(0, "LOGS"));
    scroll_down(0, 17);
    scroll_down(1, 3);
    CHECK_STR_EQ(on_screen(0, 0), "SERVO018.CSV");
    CHECK_STR_EQ(on_screen(1, 0), "ATEST004.CSV");
    tap_row(1, 2);
    tap_row(0, 2);
    CHECK_STR_EQ(chosen(0), "SERVO020.CSV");
    CHECK_STR_EQ(chosen(1), "ATEST006.CSV");
    CHECK_EQ(files_screen_active(), 0);
    draw();
}

TEST_CASE(rescan_keeps_each_panes_page_top_row_and_selection)
{
    two_scrolled_panes();
    const unsigned subs = g_h.submits;
    const unsigned looks = g_h.looks;
    tap_foot(FS_SPOT_RESCAN);
    /* One read of each pane, the slot looked at first. */
    CHECK_EQ(g_h.submits, subs + 2u);
    CHECK_EQ(g_h.looks, looks + 1u);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "BENCH/LOGS");
    CHECK_STR_EQ(files_screen_pane(1)->dir, "USER/LOGS");
    CHECK_EQ(files_screen_scroll(0), 17);
    CHECK_EQ(files_screen_scroll(1), 3);
    CHECK_STR_EQ(on_screen(0, 0), "SERVO018.CSV");
    CHECK_STR_EQ(on_screen(1, 0), "ATEST004.CSV");
    CHECK_STR_EQ(chosen(0), "SERVO020.CSV");
    CHECK_STR_EQ(chosen(1), "ATEST006.CSV");
    CHECK_EQ(files_screen_active(), 0);
    /* A file new on the card is listed in its place in the order. */
    fk_file(&g_h.card, "BENCH/LOGS/SERVO031.CSV", NULL, 4096u);
    tap_foot(FS_SPOT_RESCAN);
    CHECK(row_of(0, "SERVO031.CSV") >= 0);
    CHECK_STR_EQ(on_screen(0, 0), "SERVO018.CSV");
    CHECK_STR_EQ(chosen(0), "SERVO020.CSV");
    /* And a second RESCAN in a row changes nothing more. */
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(on_screen(0, 0), "SERVO018.CSV");
    CHECK_STR_EQ(chosen(1), "ATEST006.CSV");
}

TEST_CASE(rescan_keeps_a_later_page_from_its_first_entry)
{
    fresh_bare();
    card_default();
    char path[64];
    for (int k = 0; k < 120; ++k) {
        snprintf(path, sizeof(path), "BIG/F%03d.TXT", k);
        fk_file(&g_h.card, path, NULL, 100u);
    }
    screen()->enter();
    settle();
    CHECK(open_dir(0, "BIG"));
    tap(FS_SPOT_NEXT_CX(0), FS_SPOT_HEAD_CY);
    const fm_tree_t *t = files_screen_pane(0);
    CHECK_EQ(t->ahead, 48);
    scroll_down(0, 5);
    tap_row(0, 1);
    CHECK_STR_EQ(chosen(0), "F054.TXT");
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->ahead, 48);
    CHECK_STR_EQ(t->page[0].name, "F048.TXT");
    CHECK_STR_EQ(on_screen(0, 0), "F053.TXT");
    CHECK_STR_EQ(chosen(0), "F054.TXT");
    /* The page's first entry gone: its key still names its place, and
     * the page starts at the next entry. */
    gone("BIG/F048.TXT");
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->total, 119);
    CHECK_EQ(t->ahead, 48);
    CHECK_STR_EQ(t->page[0].name, "F049.TXT");
    CHECK_STR_EQ(on_screen(0, 0), "F053.TXT");
    CHECK_STR_EQ(chosen(0), "F054.TXT");
    /* Every entry from the page's first on gone: the last page. */
    for (int k = 49; k < 120; ++k) {
        snprintf(path, sizeof(path), "BIG/F%03d.TXT", k);
        gone(path);
    }
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->total, 48);
    CHECK_EQ(t->ahead, 0);
    CHECK_EQ(t->held, 48);
    CHECK_EQ(files_screen_scroll(0), 48 - FS_SPOT_ROWS);
    CHECK_EQ(t->selected, -1);
    draw();
}

TEST_CASE(rescan_selects_none_when_the_selected_entry_is_gone)
{
    two_scrolled_panes();
    /* The selected file gone: nothing is selected, as after a DELETE, and
     * the top row stays. */
    gone("BENCH/LOGS/SERVO020.CSV");
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(files_screen_pane(0)->selected, -1);
    CHECK(row_of(0, "SERVO021.CSV") >= 0);
    CHECK_STR_EQ(on_screen(0, 0), "SERVO018.CSV");
    CHECK_EQ(files_screen_scroll(0), 17);
    /* MOVE and RENAME then ask for a selection and change nothing. */
    tap_foot(FS_SPOT_MOVE);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_STR_EQ(files_screen_message(), TR(FM_SELECT_FIRST));
    tap_foot(FS_SPOT_RENAME);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_STR_EQ(files_screen_message(), TR(FM_SELECT_FIRST));
    CHECK(fk_get(&g_h.card, "BENCH/LOGS/SERVO021.CSV") != NULL);
    /* The other pane keeps its selection. */
    CHECK_STR_EQ(chosen(1), "ATEST006.CSV");
    /* The folder's last entry gone: nothing selected either. */
    scroll_down(1, 4);
    tap_row(1, FS_SPOT_ROWS - 1);
    CHECK_STR_EQ(chosen(1), "ATEST012.CSV");
    gone("USER/LOGS/ATEST012.CSV");
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(files_screen_pane(1)->selected, -1);
    CHECK_EQ(files_screen_pane(1)->held, 11);
    /* A selection made again holds over the next RESCAN. */
    tap_row(1, 0);
    const char *again = chosen(1);
    CHECK(again[0] != '\0');
    char kept[FM_NAME_BUF];
    snprintf(kept, sizeof(kept), "%s", again);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(chosen(1), kept);
    /* An emptied folder: no rows, and none selected. */
    for (int k = 1; k <= 11; ++k) {
        char path[64];
        snprintf(path, sizeof(path), "USER/LOGS/ATEST%03d.CSV", k);
        gone(path);
    }
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(files_screen_pane(1)->held, 0);
    CHECK_EQ(files_screen_pane(1)->selected, -1);
    CHECK_EQ(files_screen_scroll(1), 0);
    /* Nothing selected: DELETE asks for a selection. */
    tap_foot(FS_SPOT_DELETE);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_STR_EQ(files_screen_message(), TR(FM_SELECT_FIRST));
}

TEST_CASE(rescan_selects_none_when_the_selection_leaves_the_page)
{
    fresh_bare();
    card_default();
    char path[64];
    for (int k = 0; k < 120; ++k) {
        snprintf(path, sizeof(path), "BIG/F%03d.TXT", k);
        fk_file(&g_h.card, path, NULL, 100u);
    }
    screen()->enter();
    settle();
    CHECK(open_dir(0, "BIG"));
    scroll_down(0, 43);
    tap_row(0, FS_SPOT_ROWS - 1);
    CHECK_STR_EQ(chosen(0), "F047.TXT");
    /* An entry new ahead of F047: the page fills up to F046, F047 stands
     * on the next page, and nothing is selected. */
    fk_file(&g_h.card, "BIG/F010A.TXT", NULL, 100u);
    tap_foot(FS_SPOT_RESCAN);
    const fm_tree_t *t = files_screen_pane(0);
    CHECK_EQ(t->held, 48);
    CHECK_STR_EQ(t->page[47].name, "F046.TXT");
    CHECK_EQ(t->selected, -1);
    CHECK_EQ(files_screen_scroll(0), 48 - FS_SPOT_ROWS);
}

TEST_CASE(rescan_keeps_the_top_rows_entry_on_top)
{
    two_scrolled_panes();
    /* Two entries above the top row gone: the top row's entry stays on
     * top, two rows nearer the page's start. */
    gone("BENCH/LOGS/SERVO002.CSV");
    gone("BENCH/LOGS/SERVO003.CSV");
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(on_screen(0, 0), "SERVO018.CSV");
    CHECK_EQ(files_screen_scroll(0), 15);
    /* The top row's own entry gone: the entry now at its place. */
    gone("BENCH/LOGS/SERVO018.CSV");
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(on_screen(0, 0), "SERVO019.CSV");
    CHECK_STR_EQ(chosen(0), "SERVO020.CSV");
}

TEST_CASE(rescan_holds_the_scroll_to_fewer_rows)
{
    two_scrolled_panes();
    /* SERVO018 to SERVO030 gone: 18 rows are left, and the list shows
     * its last 6 with no empty row under them. */
    char path[64];
    for (int k = 18; k <= 30; ++k) {
        snprintf(path, sizeof(path), "BENCH/LOGS/SERVO%03d.CSV", k);
        gone(path);
    }
    tap_foot(FS_SPOT_RESCAN);
    const fm_tree_t *t = files_screen_pane(0);
    CHECK_EQ(t->held, 17);
    CHECK_EQ(files_screen_scroll(0), 17 - FS_SPOT_ROWS);
    CHECK_STR_EQ(on_screen(0, FS_SPOT_ROWS - 1), "SERVO017.CSV");
    CHECK_EQ(t->selected, -1);
    draw();
}

TEST_CASE(rescan_of_a_folder_gone_shows_no_rows)
{
    two_scrolled_panes();
    for (int k = 1; k <= 30; ++k) {
        char path[64];
        snprintf(path, sizeof(path), "BENCH/LOGS/SERVO%03d.CSV", k);
        gone(path);
    }
    gone("BENCH/LOGS");
    tap_foot(FS_SPOT_RESCAN);
    const fm_tree_t *t = files_screen_pane(0);
    /* The pane stays in the folder and says it is not there, as UP and
     * every read of a folder gone do. */
    CHECK_STR_EQ(t->dir, "BENCH/LOGS");
    CHECK_EQ(t->held, 0);
    CHECK_EQ(t->total, 0);
    CHECK_EQ(t->selected, -1);
    CHECK_EQ(files_screen_scroll(0), 0);
    /* Nothing selected: DELETE asks for a selection. */
    tap_foot(FS_SPOT_DELETE);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_STR_EQ(files_screen_message(), TR(FM_SELECT_FIRST));
    draw();
    /* The other pane keeps its place. */
    CHECK_STR_EQ(on_screen(1, 0), "ATEST004.CSV");
    CHECK_STR_EQ(chosen(1), "ATEST006.CSV");
    /* UP leads out of it. */
    tap(FS_SPOT_UP_CX(0), FS_SPOT_HEAD_CY);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "BENCH");
    CHECK_EQ(row_of(0, "LOGS"), -1);
}

TEST_CASE(rescan_with_the_card_out_says_no_card)
{
    two_scrolled_panes();
    fk_pull(&g_h.card);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(files_screen_pane(0)->total, -1);
    CHECK_EQ(files_screen_pane(1)->total, -1);
    CHECK_EQ(files_screen_pane(0)->held, 0);
    CHECK_EQ(files_screen_scroll(0), 0);
    CHECK_EQ(files_screen_scroll(1), 0);
    draw();
    /* The card back: another mount, so both panes start again. */
    fk_insert(&g_h.card);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "");
    CHECK_STR_EQ(files_screen_pane(1)->dir, "USER/LOGS");
    CHECK_EQ(files_screen_pane(1)->total, 12);
}

TEST_CASE(rescan_waits_behind_a_request_under_way)
{
    two_scrolled_panes();
    g_h.defer = true;
    tap(FS_SPOT_UP_CX(0), FS_SPOT_HEAD_CY);
    CHECK(files_screen_busy());
    const unsigned subs = g_h.submits;
    /* RESCAN under way of UP sends nothing yet. */
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(g_h.submits, subs);
    CHECK_EQ(files_screen_scroll(1), 3);
    /* UP answered, then RESCAN's reads one at a time, left first. */
    CHECK(fh_run(&g_h));
    settle();
    CHECK_EQ(g_h.submits, subs + 1u);
    CHECK(fh_run(&g_h));
    settle();
    CHECK_EQ(g_h.submits, subs + 2u);
    CHECK(fh_run(&g_h));
    settle();
    CHECK(!fh_run(&g_h));
    CHECK(!files_screen_busy());
    CHECK_STR_EQ(files_screen_pane(0)->dir, "BENCH");
    CHECK_STR_EQ(chosen(0), "LOGS");
    CHECK_STR_EQ(on_screen(1, 0), "ATEST004.CSV");
    CHECK_STR_EQ(chosen(1), "ATEST006.CSV");
}

TEST_CASE(rescan_during_a_page_turn_turns_the_page_first)
{
    fresh_bare();
    card_default();
    char path[64];
    for (int k = 0; k < 120; ++k) {
        snprintf(path, sizeof(path), "BIG/F%03d.TXT", k);
        fk_file(&g_h.card, path, NULL, 100u);
    }
    screen()->enter();
    settle();
    CHECK(open_dir(0, "BIG"));
    tap_row(0, 2);
    CHECK_STR_EQ(chosen(0), "F002.TXT");
    g_h.defer = true;
    tap(FS_SPOT_NEXT_CX(0), FS_SPOT_HEAD_CY);
    CHECK(files_screen_busy());
    tap_foot(FS_SPOT_RESCAN);
    while (fh_run(&g_h)) {
        settle();
    }
    /* NEXT is answered as NEXT, its page with nothing selected; RESCAN
     * then reads that page and keeps it so. */
    const fm_tree_t *t = files_screen_pane(0);
    CHECK_EQ(t->ahead, 48);
    CHECK_STR_EQ(t->page[0].name, "F048.TXT");
    CHECK_EQ(t->selected, -1);
    CHECK_EQ(files_screen_scroll(0), 0);
}

int main(void)
{
    RUN(the_panes_start_in_the_root_and_user_log_folder);
    RUN(folders_come_first_and_hidden_names_are_counted);
    RUN(a_tap_selects_and_a_second_tap_opens_a_folder);
    RUN(up_climbs_one_folder_and_the_root_has_none);
    RUN(a_tap_on_the_other_pane_makes_it_the_active_one);
    RUN(a_folder_of_120_entries_pages_with_prev_and_next);
    RUN(a_list_scrolls_by_a_drag_and_by_its_bar);
    RUN(a_folder_that_holds_only_a_hidden_file_is_not_empty);
    RUN(with_no_card_both_panes_say_so_and_writes_are_refused);
    RUN(without_a_card_task_both_panes_show_no_card);
    RUN(a_task_that_does_not_start_says_so);
    RUN(a_swapped_card_sends_both_panes_to_their_start_folders);
    RUN(a_listing_under_way_holds_the_next_until_it_is_answered);
    RUN(a_folder_gone_before_it_is_entered_keeps_the_pane_and_says_so);
    RUN(every_way_in_reads_both_panes_again_and_looks_at_the_slot);
    RUN(the_files_the_logger_writes_are_marked);
    RUN(the_user_log_folder_in_force_is_the_right_panes_start);
    RUN(the_message_line_follows_the_language);
    RUN(rescan_keeps_each_panes_page_top_row_and_selection);
    RUN(rescan_keeps_a_later_page_from_its_first_entry);
    RUN(rescan_selects_none_when_the_selected_entry_is_gone);
    RUN(rescan_selects_none_when_the_selection_leaves_the_page);
    RUN(rescan_keeps_the_top_rows_entry_on_top);
    RUN(rescan_holds_the_scroll_to_fewer_rows);
    RUN(rescan_of_a_folder_gone_shows_no_rows);
    RUN(rescan_with_the_card_out_says_no_card);
    RUN(rescan_waits_behind_a_request_under_way);
    RUN(rescan_during_a_page_turn_turns_the_page_first);
    fh_free(&g_h);
    free(s_fb);
    return test_summary("files_screen");
}
