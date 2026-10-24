/**
 * @file test_files_ops.c
 * @brief FILES' operations over a card in memory: COPY and MOVE to the
 *        other pane, RENAME and NEW through the name keyboard, DELETE
 *        behind its question, INFO, OPEN into LOGS, the write gate while
 *        ARMED and while a run is logged, a copy's box with CANCEL and an
 *        ARM part way, the places the bench keeps, and the home tag over
 *        every box.
 *
 * Cases:
 * - copy_puts_the_file_into_the_other_panes_folder
 * - copy_into_the_folder_it_is_in_is_refused
 * - writes_are_refused_while_armed_and_reads_are_not
 * - a_run_being_logged_shuts_the_gate_too
 * - a_copy_shows_its_box_and_cancel_stops_it
 * - an_arm_during_a_copy_stops_it_at_the_next_chunk
 * - move_takes_a_file_across
 * - rename_takes_the_name_typed
 * - a_name_the_card_refuses_keeps_the_keyboard_open
 * - delete_asks_and_takes_a_runs_report_along
 * - cancel_and_the_home_tag_close_the_question
 * - a_folder_is_deleted_only_when_empty
 * - new_makes_a_folder_and_an_empty_text_file
 * - a_press_beside_news_choice_closes_it
 * - info_shows_a_file_and_a_folder
 * - the_places_the_bench_keeps_refuse_what_they_refuse
 * - the_fault_log_copies_only_while_the_link_is_up
 * - open_hands_a_log_folders_csv_to_logs
 * - the_home_tag_climbs_every_box_and_then_leaves
 * - a_quick_single_call_shows_no_box
 * - an_existing_name_is_never_overwritten
 *
 * SPDX-License-Identifier: MIT
 */
#include "files_rig.h"

#include "log_csv.h"
#include "log_viewer_screen.h"

/** @brief The message the table writes for @p id and @p name. */
static const char *msg(ui_text_id_t id, const char *name)
{
    static char out[192];
    snprintf(out, sizeof(out), ui_tr(id), name);
    return out;
}

TEST_CASE(copy_puts_the_file_into_the_other_panes_folder)
{
    fresh();
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_COPY);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK(fk_holds_pattern(&g_h.card, "USER/LOGS/NOTES.TXT", "NOTES.TXT",
                           512u));
    CHECK(fk_get(&g_h.card, "NOTES.TXT") != NULL);
    CHECK(fk_get(&g_h.card, "USER/LOGS/NOTES.TXT.NEW") == NULL);
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_COPIED, "NOTES.TXT"));
    /* The copy is listed and selected in the other pane. */
    const int r = row_of(1, "NOTES.TXT");
    CHECK(r >= 0);
    CHECK_EQ(files_screen_pane(1)->selected, r);
    CHECK_EQ(g_h.card.holds, 0);
    draw();
}

TEST_CASE(copy_into_the_folder_it_is_in_is_refused)
{
    fresh();
    CHECK(open_dir(0, "USER"));
    CHECK(open_dir(0, "LOGS"));
    CHECK(pick(0, "LIMIT006.TXT"));
    const unsigned before = g_h.submits;
    tap_foot(FS_SPOT_COPY);
    CHECK_EQ(g_h.submits, before);
    CHECK_STR_EQ(files_screen_message(), TR(FM_E_SAME));
    /* A folder is not copied. */
    tap(FS_SPOT_UP_CX(0), FS_SPOT_HEAD_CY);
    CHECK(pick(0, "LOGS"));
    tap_foot(FS_SPOT_COPY);
    CHECK_EQ(g_h.submits, before + 1u);    /* only the UP's listing */
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_E_ARG, "LOGS"));
}

TEST_CASE(writes_are_refused_while_armed_and_reads_are_not)
{
    fresh();
    g_h.armed = true;
    settle();
    draw();
    CHECK(pick(0, "NOTES.TXT"));
    const int b[] = { FS_SPOT_COPY, FS_SPOT_MOVE, FS_SPOT_RENAME,
                      FS_SPOT_DELETE, FS_SPOT_NEW };
    for (size_t i = 0; i < sizeof(b) / sizeof(b[0]); ++i) {
        tap_foot(b[i]);
        CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
        CHECK_STR_EQ(files_screen_message(), TR(FM_GATE_ARMED));
    }
    CHECK(fk_get(&g_h.card, "USER/LOGS/NOTES.TXT") == NULL);
    /* INFO reads. */
    tap_foot(FS_SPOT_INFO);
    CHECK_EQ(files_screen_view(), FILES_VIEW_INFO);
    tap(FS_SPOT_CLOSE_CX, FS_SPOT_CLOSE_CY);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    /* So does entering a folder. */
    CHECK(open_dir(0, "USER"));
    /* ARMED as the band shows it shuts the gate as well. */
    g_h.armed = false;
    ui_bench_status_t st = { .link_up = true, .armed = true };
    ui_router_set_status(&st);
    CHECK(pick(0, "LOGS"));
    tap_foot(FS_SPOT_RENAME);
    CHECK_STR_EQ(files_screen_message(), TR(FM_GATE_ARMED));
    st.armed = false;
    ui_router_set_status(&st);
    settle();
    draw();
}

TEST_CASE(a_run_being_logged_shuts_the_gate_too)
{
    fresh();
    g_h.marks.run = 1u;             /* SERVO001.CSV in BENCH/LOGS is open */
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_COPY);
    CHECK_STR_EQ(files_screen_message(), TR(FM_GATE_LOGGING));
    draw();
    g_h.marks.run = 0u;
    tap_foot(FS_SPOT_COPY);
    CHECK(fk_get(&g_h.card, "USER/LOGS/NOTES.TXT") != NULL);
}

TEST_CASE(a_copy_shows_its_box_and_cancel_stops_it)
{
    fresh();
    fk_file(&g_h.card, "BIG.BIN", NULL, 5u * FM_CHUNK);
    tap_foot(FS_SPOT_RESCAN);
    CHECK(pick(0, "BIG.BIN"));
    g_h.defer = true;
    tap_foot(FS_SPOT_COPY);
    CHECK_EQ(files_screen_view(), FILES_VIEW_JOB);
    CHECK(files_screen_busy());
    g_h.fixed_progress = true;
    g_h.done = 2u * FM_CHUNK;
    g_h.total = 5u * FM_CHUNK;
    settle();
    draw();
    tap(FS_SPOT_JOB_CANCEL_CX, FS_SPOT_BOX_CY);
    CHECK_EQ(g_h.cancels, 1u);
    CHECK(g_h.cancel);
    /* The task stops the copy at its next chunk. */
    g_h.fixed_progress = false;
    CHECK(fh_run(&g_h));
    settle();
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_STR_EQ(files_screen_message(), TR(FM_E_CANCELLED));
    CHECK(fk_get(&g_h.card, "USER/LOGS/BIG.BIN") == NULL);
    CHECK(fk_get(&g_h.card, "USER/LOGS/BIG.BIN.NEW") == NULL);
    CHECK(fh_run(&g_h));           /* the listings after it */
    settle();
    CHECK(fh_run(&g_h));
    settle();
    CHECK(!files_screen_busy());
}

TEST_CASE(an_arm_during_a_copy_stops_it_at_the_next_chunk)
{
    fresh();
    fk_file(&g_h.card, "BIG.BIN", NULL, 5u * FM_CHUNK);
    tap_foot(FS_SPOT_RESCAN);
    CHECK(pick(0, "BIG.BIN"));
    g_h.steps = 0;
    g_h.arm_after_steps = 2;
    tap_foot(FS_SPOT_COPY);
    CHECK_STR_EQ(files_screen_message(), TR(FM_E_STOP_ARMED));
    CHECK(fk_get(&g_h.card, "USER/LOGS/BIG.BIN") == NULL);
    CHECK(fk_get(&g_h.card, "USER/LOGS/BIG.BIN.NEW") == NULL);
    CHECK_EQ(g_h.card.holds, 0);
    /* A run opening part way says so in its own words. */
    g_h.arm_after_steps = -1;
    g_h.marks.txt = 0u;
    fk_fail(&g_h.card, FK_WRITE, 2, FM_ERR_FULL);
    tap_foot(FS_SPOT_COPY);
    CHECK_STR_EQ(files_screen_message(), TR(FM_E_FULL));
}

TEST_CASE(move_takes_a_file_across)
{
    fresh();
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_MOVE);
    CHECK(fk_get(&g_h.card, "NOTES.TXT") == NULL);
    CHECK(fk_holds_pattern(&g_h.card, "USER/LOGS/NOTES.TXT", "NOTES.TXT",
                           512u));
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_MOVED, "NOTES.TXT"));
    CHECK_EQ(row_of(0, "NOTES.TXT"), -1);
    CHECK_EQ(files_screen_pane(1)->selected, row_of(1, "NOTES.TXT"));
    /* And back, from the right pane: the left pane is the target now. */
    CHECK(pick(1, "NOTES.TXT"));
    tap_foot(FS_SPOT_MOVE);
    CHECK(fk_get(&g_h.card, "NOTES.TXT") != NULL);
    CHECK_EQ(files_screen_pane(0)->selected, row_of(0, "NOTES.TXT"));
}

TEST_CASE(rename_takes_the_name_typed)
{
    fresh();
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_RENAME);
    CHECK_EQ(files_screen_view(), FILES_VIEW_NAME);
    draw();
    tk_name("Bench notes.txt");
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK(fk_get(&g_h.card, "NOTES.TXT") == NULL);
    CHECK(fk_holds_pattern(&g_h.card, "Bench notes.txt", "NOTES.TXT", 512u));
    char want[128];
    snprintf(want, sizeof(want), TR(FM_RENAMED), "NOTES.TXT",
             "Bench notes.txt");
    CHECK_STR_EQ(files_screen_message(), want);
    CHECK_EQ(files_screen_pane(0)->selected, row_of(0, "Bench notes.txt"));
    /* CANCEL leaves the name as it was. */
    tap_foot(FS_SPOT_RENAME);
    tk_key(UI_TK_CANCEL);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK(fk_get(&g_h.card, "Bench notes.txt") != NULL);
}

TEST_CASE(a_name_the_card_refuses_keeps_the_keyboard_open)
{
    fresh();
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_RENAME);
    static const char *const bad[] = { "", "a:b", " x", "x.", "CON",
                                       "com1.txt" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        tk_name(bad[i]);
        CHECK_EQ(files_screen_view(), FILES_VIEW_NAME);
        draw();
    }
    /* The keyboard takes 59 bytes: a 60th key types nothing, and the 59
     * are a name the card takes. */
    tk_name("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK(fk_get(&g_h.card, "NOTES.TXT") == NULL);
    CHECK(fk_get(&g_h.card,
                 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
          != NULL);
}

TEST_CASE(delete_asks_and_takes_a_runs_report_along)
{
    fresh();
    CHECK(pick(1, "ATEST005.CSV"));
    tap_foot(FS_SPOT_DELETE);
    CHECK_EQ(files_screen_view(), FILES_VIEW_DELETE);
    CHECK(fk_get(&g_h.card, "USER/LOGS/ATEST005.CSV") != NULL);
    draw();
    /* Nothing behind the question answers. */
    tap_foot(FS_SPOT_RESCAN);
    tap_row(0, 0);
    CHECK_EQ(files_screen_view(), FILES_VIEW_DELETE);
    tap(FS_SPOT_Q_DELETE_CX, FS_SPOT_BOX_CY);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK(fk_get(&g_h.card, "USER/LOGS/ATEST005.CSV") == NULL);
    CHECK(fk_get(&g_h.card, "USER/LOGS/ATEST005.TXT") == NULL);
    CHECK(fk_get(&g_h.card, "USER/LOGS/LIMIT006.TXT") != NULL);
    CHECK_STR_EQ(files_screen_message(), msg(TX_LOG_DELETED,
                                             "ATEST005.CSV"));
    CHECK_EQ(files_screen_pane(1)->held, 1);
    CHECK_EQ(files_screen_pane(1)->selected, -1);
}

TEST_CASE(cancel_and_the_home_tag_close_the_question)
{
    fresh();
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_DELETE);
    tap(FS_SPOT_Q_CANCEL_CX, FS_SPOT_BOX_CY);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    tap_foot(FS_SPOT_DELETE);
    CHECK(home());
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    /* A finger that slides off DELETE deletes nothing. */
    tap_foot(FS_SPOT_DELETE);
    send(TOUCH_EVENT_DOWN, FS_SPOT_Q_DELETE_CX, FS_SPOT_BOX_CY);
    send(TOUCH_EVENT_MOVE, FS_SPOT_Q_DELETE_CX, FS_SPOT_BOX_CY + 120);
    send(TOUCH_EVENT_UP, FS_SPOT_Q_DELETE_CX, FS_SPOT_BOX_CY + 120);
    settle();
    CHECK(fk_get(&g_h.card, "NOTES.TXT") != NULL);
    /* Only the finger that pressed DELETE releases it. */
    send_id(TOUCH_EVENT_DOWN, 1, FS_SPOT_Q_DELETE_CX, FS_SPOT_BOX_CY);
    send_id(TOUCH_EVENT_DOWN, 2, FS_SPOT_Q_DELETE_CX, FS_SPOT_BOX_CY);
    send_id(TOUCH_EVENT_UP, 2, FS_SPOT_Q_DELETE_CX, FS_SPOT_BOX_CY);
    settle();
    CHECK(fk_get(&g_h.card, "NOTES.TXT") != NULL);
    /* A touch loss drops the press. */
    screen()->cancel();
    send_id(TOUCH_EVENT_UP, 1, FS_SPOT_Q_DELETE_CX, FS_SPOT_BOX_CY);
    settle();
    CHECK(fk_get(&g_h.card, "NOTES.TXT") != NULL);
    tap(FS_SPOT_Q_DELETE_CX, FS_SPOT_BOX_CY);
    CHECK(fk_get(&g_h.card, "NOTES.TXT") == NULL);
}

TEST_CASE(a_folder_is_deleted_only_when_empty)
{
    fresh();
    fk_file(&g_h.card, "OLD/A.TXT", NULL, 3u);
    fk_dir(&g_h.card, "EMPTY");
    tap_foot(FS_SPOT_RESCAN);
    CHECK(pick(0, "OLD"));
    tap_foot(FS_SPOT_DELETE);
    draw();
    tap(FS_SPOT_Q_DELETE_CX, FS_SPOT_BOX_CY);
    CHECK(fk_get(&g_h.card, "OLD") != NULL);
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_E_NOT_EMPTY, "OLD"));
    CHECK(pick(0, "EMPTY"));
    tap_foot(FS_SPOT_DELETE);
    tap(FS_SPOT_Q_DELETE_CX, FS_SPOT_BOX_CY);
    CHECK(fk_get(&g_h.card, "EMPTY") == NULL);
}

TEST_CASE(new_makes_a_folder_and_an_empty_text_file)
{
    fresh();
    tap_foot(FS_SPOT_NEW);
    CHECK_EQ(files_screen_view(), FILES_VIEW_NEW);
    draw();
    tap(FS_SPOT_NEW_CX, FS_SPOT_NEW_CY(0));
    CHECK_EQ(files_screen_view(), FILES_VIEW_NAME);
    tk_name("runs 2026");
    const fk_node_t *n = fk_get(&g_h.card, "runs 2026");
    CHECK(n != NULL && n->dir);
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_MADE, "runs 2026"));
    CHECK_EQ(files_screen_pane(0)->selected, row_of(0, "runs 2026"));
    /* Inside it, an empty text file. */
    CHECK(open_dir(0, "runs 2026"));
    tap_foot(FS_SPOT_NEW);
    tap(FS_SPOT_NEW_CX, FS_SPOT_NEW_CY(1));
    tk_name("todo.txt");
    n = fk_get(&g_h.card, "runs 2026/todo.txt");
    CHECK(n != NULL && !n->dir && n->size == 0u);
    CHECK_EQ(files_screen_pane(0)->selected, row_of(0, "todo.txt"));
    /* A taken name is refused. */
    tap_foot(FS_SPOT_NEW);
    tap(FS_SPOT_NEW_CX, FS_SPOT_NEW_CY(1));
    tk_name("TODO.TXT");
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_E_EXISTS, "TODO.TXT"));
}

TEST_CASE(a_press_beside_news_choice_closes_it)
{
    fresh();
    tap_foot(FS_SPOT_NEW);
    CHECK_EQ(files_screen_view(), FILES_VIEW_NEW);
    /* On a row of the panes: the choice closes, the row is not selected. */
    tap_row(0, 2);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_EQ(files_screen_pane(0)->selected, -1);
    tap_foot(FS_SPOT_NEW);
    CHECK(home());
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    /* A press that slides off a row makes nothing. */
    tap_foot(FS_SPOT_NEW);
    send(TOUCH_EVENT_DOWN, FS_SPOT_NEW_CX, FS_SPOT_NEW_CY(0));
    send(TOUCH_EVENT_MOVE, FS_SPOT_NEW_CX, FS_SPOT_NEW_CY(0) + 60);
    send(TOUCH_EVENT_UP, FS_SPOT_NEW_CX, FS_SPOT_NEW_CY(0) + 60);
    settle();
    CHECK_EQ(files_screen_view(), FILES_VIEW_NEW);
}

TEST_CASE(info_shows_a_file_and_a_folder)
{
    fresh();
    g_h.card.mtime = 1715949296;    /* 2024-05-17 12:34:56 */
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_INFO);
    CHECK_EQ(files_screen_view(), FILES_VIEW_INFO);
    CHECK_EQ(g_h.last_kind, FM_REQ_INFO);
    draw();
    tap(FS_SPOT_CLOSE_CX, FS_SPOT_CLOSE_CY);
    CHECK(pick(0, "USER"));
    tap_foot(FS_SPOT_INFO);
    CHECK_EQ(files_screen_view(), FILES_VIEW_INFO);
    draw();
    /* Under way, the box says the card is read. */
    tap(FS_SPOT_CLOSE_CX, FS_SPOT_CLOSE_CY);
    g_h.defer = true;
    tap_foot(FS_SPOT_INFO);
    draw();
    CHECK(fh_run(&g_h));
    settle();
    draw();
    /* A name gone since the listing. */
    g_h.defer = false;
    tap(FS_SPOT_CLOSE_CX, FS_SPOT_CLOSE_CY);
    CHECK(pick(0, "NOTES.TXT"));
    fk_fail(&g_h.card, FK_STAT, 1, FM_ERR_NOT_FOUND);
    tap_foot(FS_SPOT_INFO);
    CHECK_EQ(files_screen_view(), FILES_VIEW_INFO);
    draw();
    /* With nothing selected INFO says so. */
    tap(FS_SPOT_CLOSE_CX, FS_SPOT_CLOSE_CY);
    tap_row(0, 2);                /* the second tap: OPEN, refused */
    CHECK(home() == false);
}

TEST_CASE(the_places_the_bench_keeps_refuse_what_they_refuse)
{
    fresh();
    CHECK(pick(0, "BENCH"));
    tap_foot(FS_SPOT_RENAME);
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_E_FIXED, "BENCH"));
    tap_foot(FS_SPOT_DELETE);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    tap_foot(FS_SPOT_MOVE);
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_E_FIXED, "BENCH"));
    /* What a fixed folder holds is the person's: NEW inside it works. */
    CHECK(open_dir(0, "BENCH"));
    CHECK(open_dir(0, "KST"));
    CHECK(open_dir(0, "CONF"));
    CHECK(pick(0, "KST.TXT"));
    tap_foot(FS_SPOT_COPY);
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_E_LOCKED, "KST.TXT"));
    tap_foot(FS_SPOT_INFO);
    CHECK_EQ(files_screen_view(), FILES_VIEW_INFO);
    draw();
    tap(FS_SPOT_CLOSE_CX, FS_SPOT_CLOSE_CY);
    CHECK(pick(0, "MG90S.TXT"));
    tap_foot(FS_SPOT_COPY);
    CHECK(fk_get(&g_h.card, "USER/LOGS/MG90S.TXT") != NULL);
    /* A file the logger writes takes INFO only. */
    g_h.marks.test = 5u;
    tap_foot(FS_SPOT_RESCAN);
    CHECK(pick(1, "ATEST005.CSV"));
    tap_foot(FS_SPOT_RENAME);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    g_h.marks.test = 0u;
    tap_foot(FS_SPOT_RENAME);
    CHECK_EQ(files_screen_view(), FILES_VIEW_NAME);
    CHECK(home());
    g_h.marks.test = 5u;
    tap_foot(FS_SPOT_RENAME);
    CHECK_STR_EQ(files_screen_message(), TR(FM_GATE_LOGGING));
    g_h.marks.test = 0u;
}

TEST_CASE(the_fault_log_copies_only_while_the_link_is_up)
{
    fresh();
    CHECK(open_dir(0, "BENCH"));
    CHECK(pick(0, "RCBENCH.LOG"));
    g_h.link_up = false;
    tap_foot(FS_SPOT_COPY);
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_E_BUSY, "RCBENCH.LOG"));
    tap_foot(FS_SPOT_RENAME);
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_E_FIXED, "RCBENCH.LOG"));
    g_h.link_up = true;
    tap_foot(FS_SPOT_COPY);
    CHECK(fk_get(&g_h.card, "USER/LOGS/RCBENCH.LOG") != NULL);
    tap_foot(FS_SPOT_INFO);
    draw();
}

/* LOGS over the same folder: one CSV it lists and opens. */
static log_mem_ctx_t s_lv_ctx;
static const char k_csv[] = "time,voltage\n0.0,22.3\n0.1,22.2\n0.2,22.1\n";

/** @brief LOGS' list(): ATEST005.CSV, and ATEST006.CSV 1 byte over what
 *         OPEN reads. */
static int lv_list(log_viewer_file_t *out, int max_entries,
                   log_viewer_page_t *page, void *ctx)
{
    (void)ctx;
    if (max_entries < 2) {
        return 0;
    }
    memset(out, 0, 2u * sizeof(out[0]));
    snprintf(out[0].name, sizeof(out[0].name), "%s", "ATEST005.CSV");
    out[0].size = sizeof(k_csv) - 1u;
    snprintf(out[1].name, sizeof(out[1].name), "%s", "ATEST006.CSV");
    out[1].size = LOG_VIEWER_OPEN_MAX + 1u;
    if (page != NULL) {
        page->ahead = 0;
        page->held = 2;
    }
    return 2;
}

/** @brief Added to the card's mount number LOGS' mounts() answers: 1
 *         reads as a card swapped since FILES listed it. */
static unsigned s_lv_mount_shift;

/** @brief LOGS' mounts(): the card's mount number, plus the shift. */
static unsigned lv_mounts(void *ctx)
{
    (void)ctx;
    return g_h.card.mount + s_lv_mount_shift;
}

/** @brief LOGS' open(): the CSV from memory. */
static bool lv_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    if (strcmp(name, "ATEST005.CSV") != 0) {
        return false;
    }
    log_source_memory(src, &s_lv_ctx, k_csv, sizeof(k_csv) - 1u);
    return true;
}

TEST_CASE(open_hands_a_log_folders_csv_to_logs)
{
    fresh();
    fk_file(&g_h.card, "USER/LOGS/ATEST006.CSV", NULL, 64u);
    const log_viewer_io_t lv = { .list = lv_list, .open = lv_open,
                                 .mounts = lv_mounts };
    log_viewer_set_io(&lv);
    ui_router_goto(SCREEN_SETUP);
    ui_router_goto(SCREEN_FILES);
    settle();
    CHECK(pick(1, "ATEST005.CSV"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK_STR_EQ(log_viewer_open_name(), "ATEST005.CSV");
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);
    /* A file OPEN refuses, with the import of ATEST005.CSV left open in
     * LOGS: the list with the reason, not the other run's data. */
    ui_router_goto(SCREEN_FILES);
    settle();
    CHECK(pick(1, "ATEST006.CSV"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(strstr(log_viewer_message(), "ATEST006.CSV") != NULL);
    /* A name LOGS does not list stays in FILES and says where files
     * open. */
    ui_router_goto(SCREEN_FILES);
    settle();
    CHECK(pick(1, "LIMIT006.TXT"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK_STR_EQ(files_screen_message(), TR(FM_OPEN_WHERE));
    /* A text file of the root is no file LOGS shows. */
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK_STR_EQ(files_screen_message(), TR(FM_OPEN_WHERE));
    CHECK_EQ(log_viewer_show(LOG_TAB_COUNT, "X", g_h.card.mount),
             LOG_SHOW_NOT_LISTED);
    CHECK_EQ(log_viewer_show(LOG_TAB_USER, "", g_h.card.mount),
             LOG_SHOW_NOT_LISTED);
    CHECK_EQ(log_viewer_show(LOG_TAB_USER, NULL, g_h.card.mount),
             LOG_SHOW_NOT_LISTED);
    /* A card swapped after FILES listed it, the same name on the new one:
     * nothing is opened, and FILES says the card changed. */
    CHECK(pick(1, "ATEST005.CSV"));
    s_lv_mount_shift = 1u;
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK_STR_EQ(files_screen_message(), TR(FM_E_CARD_GONE));
    s_lv_mount_shift = 0u;
    const log_viewer_io_t none = { 0 };
    log_viewer_set_io(&none);
}

TEST_CASE(the_home_tag_climbs_every_box_and_then_leaves)
{
    fresh();
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_INFO);
    CHECK(home());
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    tap_foot(FS_SPOT_RENAME);
    CHECK(home());
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    /* Over a copy under way the home tag is CANCEL, and the box stays
     * until the task has stopped it. */
    g_h.defer = true;
    tap_foot(FS_SPOT_COPY);
    CHECK_EQ(files_screen_view(), FILES_VIEW_JOB);
    CHECK(home());
    CHECK_EQ(g_h.cancels, 1u);
    CHECK_EQ(files_screen_view(), FILES_VIEW_JOB);
    CHECK(fh_run(&g_h));
    settle();
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    while (fh_run(&g_h)) {
        settle();
    }
    g_h.defer = false;
    CHECK(!home());
    /* Leaving with a job under way keeps its box for the way back. */
    g_h.defer = true;
    tap_foot(FS_SPOT_COPY);
    screen()->leave();
    CHECK_EQ(files_screen_view(), FILES_VIEW_JOB);
    while (fh_run(&g_h)) {
        settle();
    }
    screen()->enter();
    settle();
    while (fh_run(&g_h)) {
        settle();
    }
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    /* Leaving closes a box and the keyboard. */
    g_h.defer = false;
    tap_foot(FS_SPOT_RENAME);
    screen()->leave();
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
}

TEST_CASE(a_quick_single_call_shows_no_box)
{
    fresh();
    CHECK(pick(0, "NOTES.TXT"));
    g_h.defer = true;
    tap_foot(FS_SPOT_RENAME);
    g_tk_layer = 0;
    tk_key(UI_TK_CLR);
    tk_type("n2");
    tk_key(UI_TK_OK);
    CHECK_EQ(files_screen_view(), FILES_VIEW_JOB);
    /* 0.19s under way: the panes; then the box. */
    files_screen_invalidate();
    draw();
    screen()->tick(0.2f);
    draw();
    CHECK(fh_run(&g_h));
    settle();
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK(fk_get(&g_h.card, "n2") != NULL);
    while (fh_run(&g_h)) {
        settle();
    }
}

TEST_CASE(an_existing_name_is_never_overwritten)
{
    fresh();
    fk_file(&g_h.card, "USER/LOGS/NOTES.TXT", NULL, 7u);
    tap_foot(FS_SPOT_RESCAN);
    CHECK(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_COPY);
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_E_EXISTS, "NOTES.TXT"));
    CHECK(fk_holds_pattern(&g_h.card, "USER/LOGS/NOTES.TXT",
                           "USER/LOGS/NOTES.TXT", 7u));
    tap_foot(FS_SPOT_MOVE);
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_E_EXISTS, "NOTES.TXT"));
    CHECK(fk_get(&g_h.card, "NOTES.TXT") != NULL);
}

int main(void)
{
    RUN(copy_puts_the_file_into_the_other_panes_folder);
    RUN(copy_into_the_folder_it_is_in_is_refused);
    RUN(writes_are_refused_while_armed_and_reads_are_not);
    RUN(a_run_being_logged_shuts_the_gate_too);
    RUN(a_copy_shows_its_box_and_cancel_stops_it);
    RUN(an_arm_during_a_copy_stops_it_at_the_next_chunk);
    RUN(move_takes_a_file_across);
    RUN(rename_takes_the_name_typed);
    RUN(a_name_the_card_refuses_keeps_the_keyboard_open);
    RUN(delete_asks_and_takes_a_runs_report_along);
    RUN(cancel_and_the_home_tag_close_the_question);
    RUN(a_folder_is_deleted_only_when_empty);
    RUN(new_makes_a_folder_and_an_empty_text_file);
    RUN(a_press_beside_news_choice_closes_it);
    RUN(info_shows_a_file_and_a_folder);
    RUN(the_places_the_bench_keeps_refuse_what_they_refuse);
    RUN(the_fault_log_copies_only_while_the_link_is_up);
    RUN(open_hands_a_log_folders_csv_to_logs);
    RUN(the_home_tag_climbs_every_box_and_then_leaves);
    RUN(a_quick_single_call_shows_no_box);
    RUN(an_existing_name_is_never_overwritten);
    fh_free(&g_h);
    free(s_fb);
    return test_summary("files_ops");
}
