/*
 * Host unit tests for the order of a card's runs in the log viewer.
 *
 * Tested is which run outranks which, how the list is cut and what it says
 * when it was cut.
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

TEST_CASE(a_run_name_and_its_number_are_one_rule)
{
    /*
     * The logger writes the name and the viewer reads the number back out of
     * it, and that number is the only order the card carries: the board has no
     * clock that survives a power cycle, so every file on it is dated
     * 1980-01-01.  Every run in range has to survive the round trip, because
     * one that does not stops looking like a run and sorts with the files
     * nobody can date.
     */
    for (int i = LOG_RUN_FIRST; i <= LOG_RUN_LAST; ++i) {
        char name[LOG_RUN_NAME_MAX];
        log_run_name(name, sizeof(name), i);
        if (log_run_number(name) != i) {
            T_FAIL("run %d wrote \"%s\", read back %d", i, name,
                   log_run_number(name));
            break;
        }
    }
    char first[LOG_RUN_NAME_MAX];
    log_run_name(first, sizeof(first), LOG_RUN_FIRST);
    CHECK_STR_EQ(first, "BENCH001.CSV");
    log_run_name(first, sizeof(first), LOG_RUN_LAST);
    CHECK_STR_EQ(first, "BENCH999.CSV");

    /* A name off a computer, in lower case, is the same run. */
    CHECK_EQ(log_run_number("bench042.csv"), 42);

    /* A run's report carries its number under its own suffix, and is not a
     * run itself. */
    log_report_name(first, sizeof(first), 7);
    CHECK_STR_EQ(first, "BENCH007.TXT");
    CHECK_EQ(log_report_number(first), 7);
    CHECK_EQ(log_report_number("bench999.txt"), 999);
    CHECK_EQ(log_report_number("BENCH007.CSV"), -1);
    CHECK_EQ(log_report_number("BENCH000.TXT"), -1);
    CHECK_EQ(log_run_number("BENCH007.TXT"), -1);
    log_report_name(first, sizeof(first), LOG_RUN_LAST + 1);
    CHECK_STR_EQ(first, "");

    /* And what is not a run of this bench's: a short number, a long one, one
     * below where the numbering starts, another suffix, and a name that only
     * begins like one. */
    CHECK_EQ(log_run_number("BENCH12.CSV"), -1);
    CHECK_EQ(log_run_number("BENCH0001.CSV"), -1);
    CHECK_EQ(log_run_number("BENCHABC.CSV"), -1);
    CHECK_EQ(log_run_number("BENCH000.CSV"), -1);
    CHECK_EQ(log_run_number("BENCH001.TXT"), -1);
    CHECK_EQ(log_run_number("BENCH001.CSV.BAK"), -1);
    CHECK_EQ(log_run_number("BENCH"), -1);
    CHECK_EQ(log_run_number("SWEEP_920KV.CSV"), -1);
    CHECK_EQ(log_run_number(""), -1);
    CHECK_EQ(log_run_number(NULL), -1);

    /* A number it cannot write is not written as a near miss. */
    char name[LOG_RUN_NAME_MAX];
    log_run_name(name, sizeof(name), LOG_RUN_FIRST - 1);
    CHECK_STR_EQ(name, "");
    log_run_name(name, sizeof(name), LOG_RUN_LAST + 1);
    CHECK_STR_EQ(name, "");
    char small[LOG_RUN_NAME_MAX - 1];
    log_run_name(small, sizeof(small), LOG_RUN_FIRST);
    CHECK_STR_EQ(small, "");
    log_run_name(NULL, sizeof(name), LOG_RUN_FIRST); /* nowhere to write it */
}

TEST_CASE(the_newest_run_outranks_the_rest_of_the_card)
{
    /* Which entry a full list keeps.  A run is the only entry whose age is
     * known, so runs come first and the newest of them first of all;
     * everything else follows in the order it is drawn. */
    CHECK(log_name_rank("BENCH200.CSV", "BENCH199.CSV") < 0);
    CHECK(log_name_rank("BENCH049.CSV", "BENCH050.CSV") > 0);
    CHECK(log_name_rank("BENCH100.CSV", "BENCH099.CSV") < 0);
    CHECK_EQ(log_name_rank("BENCH007.CSV", "BENCH007.CSV"), 0);

    /* A run against a file nobody can date, both ways round. */
    CHECK(log_name_rank("BENCH001.CSV", "SWEEP_920KV.CSV") < 0);
    CHECK(log_name_rank("SWEEP_920KV.CSV", "BENCH999.CSV") > 0);

    /* And two of those: by name, with case folded, as the list draws them. */
    CHECK(log_name_rank("aaa.csv", "BBB.CSV") < 0);
    CHECK(log_name_rank("SWEEP.CSV", "PRUEFUNG.CSV") > 0);
    CHECK_EQ(log_name_rank("SWEEP.CSV", "sweep.csv"), 0);
    CHECK(log_name_rank(NULL, "SWEEP.CSV") < 0);
    CHECK(log_name_rank("SWEEP.CSV", NULL) > 0);
}

TEST_CASE(a_card_of_999_runs_offers_the_newest_that_fit)
{
    /*
     * The runs arrive oldest first, which is the order a FAT (File Allocation
     * Table) directory hands its entries back until a file is deleted and its
     * slot filled again.  Keeping the first LOG_VIEWER_MAX_FILES to arrive
     * would put the oldest runs on the first page and the newest on the
     * last.
     */
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    int held = 0;
    for (int i = LOG_RUN_FIRST; i <= LOG_RUN_LAST; ++i) {
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        log_run_name(f.name, sizeof(f.name), i);
        f.size = ((uint64_t)i << 32) | (uint64_t)i;   /* past 4 GiB */
        held = log_select_keep(set, held, LOG_VIEWER_MAX_FILES, &f);
    }
    CHECK_EQ(held, LOG_VIEWER_MAX_FILES);

    /* Drawn newest at the top, the order the pages run in. */
    log_select_sort(set, held);
    for (int r = 0; r < held; ++r) {
        char want[LOG_RUN_NAME_MAX];
        log_run_name(want, sizeof(want), LOG_RUN_LAST - r);
        if (strcmp(set[r].name, want) != 0) {
            T_FAIL("row %d: got \"%s\", want \"%s\"", r, set[r].name, want);
            break;
        }
    }
    /* The whole entry travels with the name it was kept by. */
    CHECK(set[0].size
          == (((uint64_t)LOG_RUN_LAST << 32) | (uint64_t)LOG_RUN_LAST));
}

/*
 * Directories sort above files whichever side of the comparison they arrive
 * on. qsort decides that, so a set whose only directory is already first
 * exercises one answer and never the other, and a comparator that returned
 * the same sign both ways would order correctly here and wrongly on a card
 * that happened to be read in another order.
 */
TEST_CASE(a_directory_sorts_first_from_either_side_of_the_comparison)
{
    static const char *const names[] = { "A.CSV", "DIR", "B.CSV", "ZDIR" };
    static const bool dirs[]         = { false,   true,  false,   true };

    log_viewer_file_t set[4];
    for (int i = 0; i < 4; ++i) {
        memset(&set[i], 0, sizeof(set[i]));
        snprintf(set[i].name, sizeof(set[i].name), "%s", names[i]);
        set[i].is_dir = dirs[i];
    }
    log_select_sort(set, 4);
    CHECK_STR_EQ(set[0].name, "DIR");
    CHECK_STR_EQ(set[1].name, "ZDIR");
    CHECK_STR_EQ(set[2].name, "A.CSV");
    CHECK_STR_EQ(set[3].name, "B.CSV");

    /* And again with the input reversed, so the comparator meets the same
     * pairs the other way round. */
    for (int i = 0; i < 4; ++i) {
        memset(&set[i], 0, sizeof(set[i]));
        snprintf(set[i].name, sizeof(set[i].name), "%s", names[3 - i]);
        set[i].is_dir = dirs[3 - i];
    }
    log_select_sort(set, 4);
    CHECK_STR_EQ(set[0].name, "DIR");
    CHECK_STR_EQ(set[1].name, "ZDIR");
    CHECK_STR_EQ(set[2].name, "A.CSV");
    CHECK_STR_EQ(set[3].name, "B.CSV");
}

TEST_CASE(a_full_list_keeps_runs_over_what_it_cannot_date)
{
    /* Four slots and five entries, offered in the order a card holds them.
     * Runs take the slots; of what is left the name decides, so the file that
     * sorts last is the one that goes. */
    static const struct {
        const char *name;
        bool is_dir;
    } offered[] = {
        { "SWEEP.CSV", false },
        { "BENCH001.CSV", false },
        { "OLD", true },
        { "BENCH002.CSV", false },
        { "BENCH003.CSV", false },
    };
    log_viewer_file_t set[4];
    int held = 0;
    for (size_t i = 0; i < sizeof(offered) / sizeof(offered[0]); ++i) {
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        snprintf(f.name, sizeof(f.name), "%s", offered[i].name);
        f.is_dir = offered[i].is_dir;
        held = log_select_keep(set, held, (int)(sizeof(set) / sizeof(set[0])),
                               &f);
    }
    CHECK_EQ(held, 4);

    /* A candidate that ranks below a full set is dropped where it stands, and
     * the set does not move: the four already held are the four to keep. */
    log_viewer_file_t late;
    memset(&late, 0, sizeof(late));
    snprintf(late.name, sizeof(late.name), "%s", "ZZZ_IMPORT.CSV");
    CHECK_EQ(log_select_keep(set, held, 4, &late), 4);

    log_select_sort(set, held);
    /* Directories first, then the newest run: the order the browse list
     * draws. */
    CHECK_STR_EQ(set[0].name, "OLD");
    CHECK_STR_EQ(set[1].name, "BENCH003.CSV");
    CHECK_STR_EQ(set[2].name, "BENCH002.CSV");
    CHECK_STR_EQ(set[3].name, "BENCH001.CSV");

    /* A set with no room takes nothing and says so. */
    log_viewer_file_t one;
    memset(&one, 0, sizeof(one));
    snprintf(one.name, sizeof(one.name), "%s", "BENCH999.CSV");
    CHECK_EQ(log_select_keep(set, 0, 0, &one), 0);
    CHECK_EQ(log_select_keep(NULL, 0, 4, &one), 0);
    CHECK_EQ(log_select_keep(set, held, 4, NULL), held);
    log_select_sort(NULL, 4);
    log_select_sort(set, 1);
    CHECK_STR_EQ(set[0].name, "OLD");
}

TEST_CASE(a_list_that_was_cut_says_so)
{
    /*
     * A card takes LOG_RUN_LAST runs and the list holds LOG_VIEWER_MAX_FILES,
     * so most of a full card is not on the screen.  The browse panel's tab
     * carries both numbers when that happens: a list that quietly shows a
     * subset reads as the whole card, and the run that is missing from it
     * reads as a run that was never written.
     */
    fresh();
    g_card_runs = LOG_VIEWER_MAX_FILES;  /* exactly fits: nothing to say */
    log_viewer_refresh();
    draw();
    CHECK_EQ(browse_tab_width(), tab_width_for("FILES"));

    fresh();
    g_card_runs = 200;
    log_viewer_refresh();
    draw();
    char want[32];
    snprintf(want, sizeof(want), "FILES 1 TO %d OF %d", LOG_VIEWER_MAX_FILES,
             200);
    CHECK_EQ(browse_tab_width(), tab_width_for(want));
}

TEST_CASE(a_count_above_the_list_does_not_reach_past_it)
{
    /*
     * The count is what the card holds, which is more than the lister wrote
     * into the array.  Rows come out of the array, so the list has to stop at
     * what is in it: scrolling to the bottom of a 200-run card lands on the
     * oldest run written, not on whatever follows the array.
     */
    fresh();
    g_card_runs = 200;
    log_viewer_refresh();

    /* Two drags: one press scrolls by its own travel and the bottom of a
     * 48-row list is further down than that. */
    for (int i = 0; i < 2; ++i) {
        send(TOUCH_EVENT_DOWN, 400, 300);
        send(TOUCH_EVENT_MOVE, 400, -700);
        send(TOUCH_EVENT_UP, 400, -700);
    }
    draw();

    /* The top row is now six from the end of what was written, and the last
     * run written is the oldest one on the page, the newest at the top. */
    char want[LOG_RUN_NAME_MAX];
    log_run_name(want, sizeof(want),
                 g_card_runs - LOG_VIEWER_MAX_FILES + BR_ROWS_SEEN);
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    CHECK_STR_EQ(log_viewer_open_name(), want);
}

int main(void)
{
    RUN(a_run_name_and_its_number_are_one_rule);
    RUN(the_newest_run_outranks_the_rest_of_the_card);
    RUN(a_card_of_999_runs_offers_the_newest_that_fit);
    RUN(a_directory_sorts_first_from_either_side_of_the_comparison);
    RUN(a_full_list_keeps_runs_over_what_it_cannot_date);
    RUN(a_list_that_was_cut_says_so);
    RUN(a_count_above_the_list_does_not_reach_past_it);
    return test_summary("logview_rank");
}
