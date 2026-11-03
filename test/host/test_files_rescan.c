/**
 * @file test_files_rescan.c
 * @brief RESCAN and the way back into FILES list every entry the card
 *        holds now: a log the logger writes while FILES shows its folder,
 *        a file a Wi-Fi upload writes, a folder made beside the panes,
 *        each in either pane and in a folder of more than one page.
 *
 * Cases:
 * - a_log_written_while_files_shows_its_folder_is_listed_on_rescan
 * - logs_written_into_an_emptied_folder_are_listed_while_open
 * - an_upload_that_ranks_first_is_listed_on_rescan
 * - a_new_folder_in_the_other_panes_folder_is_listed_on_rescan
 * - the_way_back_into_files_lists_what_came_meanwhile
 * - a_folder_of_more_than_a_page_lists_a_new_first_entry
 * - a_later_page_keeps_its_place_and_prev_finds_the_new_entry
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
    fk_file(&g_h.card, "USER/LOGS/ESC001.CSV", NULL, 4096u);
    tap_foot(FS_SPOT_RESCAN);
    CHECK_EQ(t->total, 61);
    CHECK_EQ(t->ahead, 0);
    CHECK_EQ(t->held, 48);
    CHECK_STR_EQ(t->page[0].name, "ESC001.CSV");
    CHECK_STR_EQ(on_screen(1, 0), "ESC001.CSV");
    CHECK_STR_EQ(t->page[47].name, "SERVO047.CSV");
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

int main(void)
{
    RUN(a_log_written_while_files_shows_its_folder_is_listed_on_rescan);
    RUN(logs_written_into_an_emptied_folder_are_listed_while_open);
    RUN(an_upload_that_ranks_first_is_listed_on_rescan);
    RUN(a_new_folder_in_the_other_panes_folder_is_listed_on_rescan);
    RUN(the_way_back_into_files_lists_what_came_meanwhile);
    RUN(a_folder_of_more_than_a_page_lists_a_new_first_entry);
    RUN(a_later_page_keeps_its_place_and_prev_finds_the_new_entry);
    fh_free(&g_h);
    free(s_fb);
    return test_summary("files_rescan");
}
