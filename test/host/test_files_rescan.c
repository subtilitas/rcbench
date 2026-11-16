/**
 * @file test_files_rescan.c
 * @brief RESCAN and the way back into FILES list every entry the card
 *        holds now: a log the logger writes while FILES shows its folder,
 *        a file a Wi-Fi upload writes, a folder made beside the panes,
 *        each in either pane and in a folder of more than one page.
 *        RESCAN selects each pane's newest file by run number, on the page
 *        on show or on the page that holds it, scrolled on screen.
 *
 * Cases:
 * - a_log_written_while_files_shows_its_folder_is_listed_on_rescan
 * - logs_written_into_an_emptied_folder_are_listed_while_open
 * - an_upload_that_ranks_first_is_listed_on_rescan
 * - a_new_folder_in_the_other_panes_folder_is_listed_on_rescan
 * - the_way_back_into_files_lists_what_came_meanwhile
 * - a_folder_of_more_than_a_page_lists_a_new_first_entry
 * - a_later_page_keeps_its_place_and_prev_finds_the_new_entry
 * - rescan_selects_the_newest_run_over_the_name_order
 * - one_number_under_two_names_selects_the_first_by_name
 * - a_folder_is_never_the_newest
 * - a_pane_without_runs_keeps_its_selection
 * - each_pane_selects_its_own_newest
 * - the_newest_is_scrolled_on_screen_on_the_page_kept
 * - a_newest_file_on_a_later_page_brings_that_page
 * - a_newest_file_ranked_first_brings_the_first_page
 * - the_page_brought_is_one_prev_and_next_step_through
 *
 * SPDX-License-Identifier: MIT
 */
#include "files_rig.h"

/** @brief Check @p c, and leave the case when it fails. */
#define NEED(c)                                                               \
    do {                                                                      \
        if (!(c)) {                                                           \
            CHECK(c);                                                         \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief The name on row @p r on screen of pane @p i, "" for none. */
static const char *on_screen(int i, int r)
{
    const fm_tree_t *t = files_screen_pane(i);
    const int at = files_screen_scroll(i) + r;
    return (at >= 0 && at < t->held) ? t->page[at].name : "";
}

/**
 * @brief A card whose USER/LOGS holds the logs of one kind that ranks
 *        late, SERVO001 to SERVO@p n, and FILES entered on it.
 * @param n  the logs, 0 to 120
 */
static void servo_logs(int n)
{
    fresh_bare();
    fk_dir(&g_h.card, "BENCH/LOGS");
    fk_dir(&g_h.card, "USER/LOGS");
    char path[64];
    for (int k = 1; k <= n; ++k) {
        snprintf(path, sizeof(path), "USER/LOGS/SERVO%03d.CSV", k);
        fk_file(&g_h.card, path, NULL, 4096u);
    }
    screen()->enter();
    settle();
    draw();
}

/*
 * USER/LOGS holds servo logs; an auto test writes ATEST001.CSV while FILES
 * shows the folder.  It ranks before every row: RESCAN lists it on top,
 * and the pane holds every entry of the folder.
 */
TEST_CASE(a_log_written_while_files_shows_its_folder_is_listed_on_rescan)
{
    servo_logs(3);
    const fm_tree_t *t = files_screen_pane(1);
    NEED(t->held == 3);
    fk_file(&g_h.card, "USER/LOGS/ATEST001.CSV", NULL, 15104u);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->total, 4);
    CHECK_EQ(t->held, 4);
    CHECK_EQ(t->ahead, 0);
    CHECK_STR_EQ(on_screen(1, 0), "ATEST001.CSV");
    CHECK_EQ(row_of(1, "SERVO003.CSV"), 3);
    /* A log later in the order is listed too. */
    fk_file(&g_h.card, "USER/LOGS/SERVO004.CSV", NULL, 4096u);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->held, 5);
    CHECK_EQ(row_of(1, "SERVO004.CSV"), 4);
}

/*
 * Every log deleted, then runs logged with FILES open: a servo run, then
 * an auto test whose CSV and report rank before it.  Each RESCAN lists
 * the folder whole, the files the logger holds open marked, and so does
 * the way back into FILES.
 */
TEST_CASE(logs_written_into_an_emptied_folder_are_listed_while_open)
{
    servo_logs(0);
    const fm_tree_t *t = files_screen_pane(1);
    NEED(t->total == 0);
    fk_file(&g_h.card, "USER/LOGS/SERVO001.CSV", NULL, 0u);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->held, 1);
    /* A bench log, open while RESCAN lists it. */
    fk_file(&g_h.card, "USER/LOGS/BENCH001.CSV", NULL, 0u);
    g_h.marks.run = 1u;
    snprintf(g_h.run_dir, sizeof(g_h.run_dir), "%s", "USER/LOGS");
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->held, 2);
    CHECK_STR_EQ(t->page[0].name, "BENCH001.CSV");
    CHECK(t->page[0].busy);
    /* The bench log ends; an auto test writes its CSV and report while
     * FILES is off screen, and the way back in lists them. */
    g_h.marks.run = 0u;
    screen()->leave();
    fk_file(&g_h.card, "USER/LOGS/ATEST001.CSV", NULL, 0u);
    fk_file(&g_h.card, "USER/LOGS/ATEST001.TXT", NULL, 0u);
    g_h.marks.test = 1u;
    g_h.marks.txt = 1u;
    screen()->enter();
    settle();
    CHECK_EQ(t->held, 4);
    CHECK_STR_EQ(t->page[0].name, "ATEST001.CSV");
    CHECK(t->page[0].busy);
    CHECK_STR_EQ(t->page[1].name, "ATEST001.TXT");
}

/*
 * A Wi-Fi upload into the root: a file in small letters that ranks before
 * every file of the root, after its folders, and a selection kept.
 */
TEST_CASE(an_upload_that_ranks_first_is_listed_on_rescan)
{
    fresh();
    const fm_tree_t *t = files_screen_pane(0);
    NEED(pick(0, "NOTES.TXT"));
    fk_file(&g_h.card, "a-setup.json", NULL, 731u);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->total, 4);
    CHECK_EQ(t->held, 4);
    CHECK_EQ(row_of(0, "a-setup.json"), 2);
    CHECK_STR_EQ(t->page[t->selected].name, "NOTES.TXT");
    /* The same in the right pane's folder, the pane not active. */
    fk_file(&g_h.card, "USER/LOGS/0-upload.txt", NULL, 20u);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(on_screen(1, 0), "0-upload.txt");
    CHECK_EQ(files_screen_pane(1)->held, 4);
}

/*
 * A folder made in the folder the other pane shows, from the Wi-Fi page:
 * folders rank first, so it goes on top of that pane.
 */
TEST_CASE(a_new_folder_in_the_other_panes_folder_is_listed_on_rescan)
{
    fresh();
    tap_row(1, 0);                      /* the right pane active */
    NEED(files_screen_active() == 1);
    fk_dir(&g_h.card, "ARCHIVE");
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(on_screen(0, 0), "ARCHIVE");
    CHECK_EQ(files_screen_pane(0)->held, 4);
}

/*
 * A log written while FILES is off screen: the way back into FILES lists
 * it, as RESCAN does.
 */
TEST_CASE(the_way_back_into_files_lists_what_came_meanwhile)
{
    servo_logs(3);
    screen()->leave();
    fk_file(&g_h.card, "USER/LOGS/MOTOR001.CSV", NULL, 4096u);
    fk_file(&g_h.card, "USER/LOGS/BENCH001.CSV", NULL, 4096u);
    screen()->enter();
    settle();
    const fm_tree_t *t = files_screen_pane(1);
    CHECK_EQ(t->held, 5);
    CHECK_STR_EQ(on_screen(1, 0), "BENCH001.CSV");
    CHECK_STR_EQ(on_screen(1, 1), "MOTOR001.CSV");
}

/*
 * 60 servo logs: the first page holds 48.  A log that ranks first comes
 * on top of the first page, and the last of the 48 goes to the next page.
 * The list, scrolled down, stays where it was.
 */
TEST_CASE(a_folder_of_more_than_a_page_lists_a_new_first_entry)
{
    servo_logs(60);
    const fm_tree_t *t = files_screen_pane(1);
    NEED(t->held == 48);
    fk_file(&g_h.card, "USER/LOGS/ESC061.CSV", NULL, 4096u);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->total, 61);
    CHECK_EQ(t->ahead, 0);
    CHECK_EQ(t->held, 48);
    CHECK_STR_EQ(t->page[0].name, "ESC061.CSV");
    CHECK_STR_EQ(on_screen(1, 0), "ESC061.CSV");
    CHECK_STR_EQ(t->page[47].name, "SERVO047.CSV");
    CHECK_EQ(t->selected, 0);           /* the newest run */
}

/*
 * On the second page a new entry that ranks before the page belongs to
 * the pages before: the page keeps its first entry, PREV shows the 48
 * entries before it, and a second PREV the new one on the first page.
 */
TEST_CASE(a_later_page_keeps_its_place_and_prev_finds_the_new_entry)
{
    servo_logs(60);
    tap(FS_SPOT_NEXT_CX(1), FS_SPOT_HEAD_CY);
    const fm_tree_t *t = files_screen_pane(1);
    NEED(t->ahead == 48);
    NEED(strcmp(t->page[0].name, "SERVO049.CSV") == 0);
    fk_file(&g_h.card, "USER/LOGS/ATEST001.CSV", NULL, 4096u);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->ahead, 49);
    CHECK_STR_EQ(t->page[0].name, "SERVO049.CSV");
    tap(FS_SPOT_PREV_CX(1), FS_SPOT_HEAD_CY);
    CHECK_EQ(t->ahead, 1);
    CHECK_STR_EQ(t->page[0].name, "SERVO001.CSV");
    tap(FS_SPOT_PREV_CX(1), FS_SPOT_HEAD_CY);
    CHECK_EQ(t->ahead, 0);
    CHECK_STR_EQ(t->page[0].name, "ATEST001.CSV");
}

/** @brief The selected entry's name of pane @p i, "" for none. */
static const char *chosen(int i)
{
    const fm_tree_t *t = files_screen_pane(i);
    return (t->selected >= 0 && t->selected < t->held)
               ? t->page[t->selected].name : "";
}

/**
 * @brief Files @p prefix@p from to @p prefix@p to, ".CSV", in @p dir.
 * @param dir     the folder
 * @param prefix  the writer's prefix
 * @param from    the first number
 * @param to      the last number
 */
static void logs(const char *dir, const char *prefix, int from, int to)
{
    char path[64];
    for (int k = from; k <= to; ++k) {
        snprintf(path, sizeof(path), "%s/%s%03d.CSV", dir, prefix, k);
        fk_file(&g_h.card, path, NULL, 4096u);
    }
}

/*
 * The right pane's USER/LOGS: ATEST005, LIMIT006 and runs written since,
 * with a labelled name and a file of no run number among them.  RESCAN
 * keeps the name order and selects run 8, which ranks after ATEST007 and
 * before SERVO004 by name.  The active pane stays the left one.
 */
TEST_CASE(rescan_selects_the_newest_run_over_the_name_order)
{
    fresh();
    NEED(files_screen_active() == 0);
    fk_file(&g_h.card, "USER/LOGS/SERVO004.CSV", NULL, 4096u);
    fk_file(&g_h.card, "USER/LOGS/MOTOR008_kv1200.CSV", NULL, 4096u);
    fk_file(&g_h.card, "USER/LOGS/ATEST007.CSV", NULL, 4096u);
    fk_file(&g_h.card, "USER/LOGS/a-notes.txt", NULL, 20u);
    tap_foot(FS_SPOT_RESCAN);
    const fm_tree_t *t = files_screen_pane(1);
    CHECK_EQ(t->held, 7);
    CHECK_STR_EQ(t->newest, "MOTOR008_kv1200.CSV");
    CHECK_STR_EQ(chosen(1), "MOTOR008_kv1200.CSV");
    CHECK_STR_EQ(t->page[0].name, "a-notes.txt");
    CHECK(row_of(1, "ATEST007.CSV") < row_of(1, "MOTOR008_kv1200.CSV"));
    CHECK(row_of(1, "MOTOR008_kv1200.CSV") < row_of(1, "SERVO004.CSV"));
    CHECK_EQ(files_screen_active(), 0);
    /* The left pane, the root, holds no run: nothing selected still. */
    CHECK_EQ(files_screen_pane(0)->selected, -1);
}

/*
 * A run's report written before its CSV: one number under two names, the
 * first by name, ATEST009.CSV, is selected.  A file without a run number
 * written after it is not the newest.
 */
TEST_CASE(one_number_under_two_names_selects_the_first_by_name)
{
    fresh();
    fk_file(&g_h.card, "USER/LOGS/ATEST009.TXT", NULL, 2210u);
    fk_file(&g_h.card, "USER/LOGS/ATEST009.CSV", NULL, 15104u);
    fk_file(&g_h.card, "USER/LOGS/0-upload.csv", NULL, 20u);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(chosen(1), "ATEST009.CSV");
    CHECK_EQ(row_of(1, "ATEST009.CSV") + 1, row_of(1, "ATEST009.TXT"));
}

/*
 * A folder named as a run, with a higher number than every file: folders
 * are never chosen, LIMIT006.TXT is.
 */
TEST_CASE(a_folder_is_never_the_newest)
{
    fresh();
    fk_dir(&g_h.card, "USER/LOGS/MOTOR099.CSV");
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(row_of(1, "MOTOR099.CSV"), 0);
    CHECK(files_screen_pane(1)->page[0].is_dir);
    CHECK_STR_EQ(chosen(1), "LIMIT006.TXT");
}

/*
 * The root holds folders and NOTES.TXT, no run: RESCAN leaves the bar on
 * the row selected, a file or a folder, and an empty folder stays with
 * nothing selected.
 */
TEST_CASE(a_pane_without_runs_keeps_its_selection)
{
    fresh();
    NEED(pick(0, "NOTES.TXT"));
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(chosen(0), "NOTES.TXT");
    CHECK_STR_EQ(files_screen_pane(0)->newest, "");
    NEED(pick(0, "BENCH"));
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(chosen(0), "BENCH");
    fk_dir(&g_h.card, "EMPTY");
    tap_foot(FS_SPOT_RESCAN);
    NEED(open_dir(0, "EMPTY"));
    const fm_tree_t *t = files_screen_pane(0);
    NEED(t->held == 0);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->held, 0);
    CHECK_EQ(t->selected, -1);
    CHECK_EQ(files_screen_scroll(0), 0);
}

/*
 * The left pane in BENCH/LOGS, the right in USER/LOGS: each selects the
 * newest run of its own folder, and a run written into one folder moves
 * that pane's selection alone.
 */
TEST_CASE(each_pane_selects_its_own_newest)
{
    fresh();
    logs("BENCH/LOGS", "SUPPL", 2, 2);
    logs("BENCH/LOGS", "MOTOR", 3, 3);
    NEED(open_dir(0, "BENCH"));
    NEED(open_dir(0, "LOGS"));
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(chosen(0), "MOTOR003.CSV");
    CHECK_STR_EQ(chosen(1), "LIMIT006.TXT");
    CHECK_EQ(files_screen_active(), 0);
    logs("USER/LOGS", "ATEST", 10, 10);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(chosen(0), "MOTOR003.CSV");
    CHECK_STR_EQ(chosen(1), "ATEST010.CSV");
}

/*
 * 30 servo logs on one page: the newest, row 29, is scrolled on screen
 * as the bottom row, the page kept.  A run that ranks first then scrolls
 * the list back to the top.
 */
TEST_CASE(the_newest_is_scrolled_on_screen_on_the_page_kept)
{
    servo_logs(30);
    const fm_tree_t *t = files_screen_pane(1);
    NEED(files_screen_scroll(1) == 0);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->ahead, 0);
    CHECK_EQ(t->held, 30);
    CHECK_STR_EQ(chosen(1), "SERVO030.CSV");
    CHECK_EQ(files_screen_scroll(1), 30 - FS_SPOT_ROWS);
    CHECK_STR_EQ(on_screen(1, FS_SPOT_ROWS - 1), "SERVO030.CSV");
    logs("USER/LOGS", "ATEST", 31, 31);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_STR_EQ(chosen(1), "ATEST031.CSV");
    CHECK_EQ(files_screen_scroll(1), 0);
    CHECK_STR_EQ(on_screen(1, 0), "ATEST031.CSV");
    draw();
}

/*
 * 60 servo logs, the first page on show: the newest, SERVO060, is on the
 * second page.  RESCAN reads that page, as NEXT would, selects it and
 * scrolls it on screen; PREV goes back to the first page.
 */
TEST_CASE(a_newest_file_on_a_later_page_brings_that_page)
{
    servo_logs(60);
    const fm_tree_t *t = files_screen_pane(1);
    NEED(t->ahead == 0 && t->held == 48);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->ahead, 48);
    CHECK_EQ(t->held, 12);
    CHECK_STR_EQ(t->page[0].name, "SERVO049.CSV");
    CHECK_STR_EQ(chosen(1), "SERVO060.CSV");
    CHECK_STR_EQ(on_screen(1, FS_SPOT_ROWS - 1), "SERVO060.CSV");
    /* A second RESCAN finds it on the page and stays. */
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->ahead, 48);
    CHECK_STR_EQ(chosen(1), "SERVO060.CSV");
    tap(FS_SPOT_PREV_CX(1), FS_SPOT_HEAD_CY);
    CHECK_EQ(t->ahead, 0);
    CHECK_STR_EQ(t->page[0].name, "SERVO001.CSV");
}

/*
 * The second page on show and a run written that ranks before every
 * entry: no entry ranks ahead of it, so RESCAN brings the first page with
 * the run on top.
 */
TEST_CASE(a_newest_file_ranked_first_brings_the_first_page)
{
    servo_logs(60);
    tap(FS_SPOT_NEXT_CX(1), FS_SPOT_HEAD_CY);
    const fm_tree_t *t = files_screen_pane(1);
    NEED(t->ahead == 48);
    logs("USER/LOGS", "ATEST", 61, 61);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->ahead, 0);
    CHECK_EQ(t->held, 48);
    CHECK_STR_EQ(chosen(1), "ATEST061.CSV");
    CHECK_EQ(t->selected, 0);
    CHECK_EQ(files_screen_scroll(1), 0);
}

/*
 * MOTOR001 to MOTOR060 and SERVO061 to SERVO120, the third page on show
 * (entries 97 to 120 once MOTOR121 ranks in at entry 60).  The page that
 * holds MOTOR121 is the one PREV shows from there, entries 49 to 96, not a
 * page that starts at the run; NEXT from it is the page that was on show.
 */
TEST_CASE(the_page_brought_is_one_prev_and_next_step_through)
{
    fresh_bare();
    fk_dir(&g_h.card, "BENCH/LOGS");
    fk_dir(&g_h.card, "USER/LOGS");
    logs("USER/LOGS", "MOTOR", 1, 60);
    logs("USER/LOGS", "SERVO", 61, 120);
    screen()->enter();
    settle();
    tap(FS_SPOT_NEXT_CX(1), FS_SPOT_HEAD_CY);
    tap(FS_SPOT_NEXT_CX(1), FS_SPOT_HEAD_CY);
    const fm_tree_t *t = files_screen_pane(1);
    NEED(t->ahead == 96);
    logs("USER/LOGS", "MOTOR", 121, 121);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->total, 121);
    CHECK_EQ(t->ahead, 49);
    CHECK_EQ(t->held, 48);
    CHECK_STR_EQ(t->page[0].name, "MOTOR050.CSV");
    CHECK_STR_EQ(chosen(1), "MOTOR121.CSV");
    CHECK_EQ(t->selected, 11);
    /* The list came from a later page: the run is the top row on screen. */
    CHECK_STR_EQ(on_screen(1, 0), "MOTOR121.CSV");
    CHECK_STR_EQ(t->page[47].name, "SERVO096.CSV");
    tap(FS_SPOT_NEXT_CX(1), FS_SPOT_HEAD_CY);
    CHECK_EQ(t->ahead, 97);
    CHECK_STR_EQ(t->page[0].name, "SERVO097.CSV");
}

int main(void)
{
    RUN(a_log_written_while_files_shows_its_folder_is_listed_on_rescan);
    RUN(logs_written_into_an_emptied_folder_are_listed_while_open);
    RUN(an_upload_that_ranks_first_is_listed_on_rescan);
    RUN(a_new_folder_in_the_other_panes_folder_is_listed_on_rescan);
    RUN(the_way_back_into_files_lists_what_came_meanwhile);
    RUN(a_folder_of_more_than_a_page_lists_a_new_first_entry);
    RUN(a_later_page_keeps_its_place_and_prev_finds_the_new_entry);
    RUN(rescan_selects_the_newest_run_over_the_name_order);
    RUN(one_number_under_two_names_selects_the_first_by_name);
    RUN(a_folder_is_never_the_newest);
    RUN(a_pane_without_runs_keeps_its_selection);
    RUN(each_pane_selects_its_own_newest);
    RUN(the_newest_is_scrolled_on_screen_on_the_page_kept);
    RUN(a_newest_file_on_a_later_page_brings_that_page);
    RUN(a_newest_file_ranked_first_brings_the_first_page);
    RUN(the_page_brought_is_one_prev_and_next_step_through);
    fh_free(&g_h);
    free(s_fb);
    return test_summary("files_rescan");
}
