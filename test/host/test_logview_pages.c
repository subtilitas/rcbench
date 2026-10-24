/**
 * @file test_logview_pages.c
 * @brief Host unit tests for the pages of the log viewer's list and its scroll bar.
 *
 * Tested is that the pages cover a card once in rank order, what the page
 * keys do at the ends and after a delete, swap or rescan, and the scroll bar.
 *
 * Cases:
 * - pages_of_a_card_cover_it_once_in_rank_order_both_ways
 * - pages_of_mixed_writers_run_newest_first_across_the_names
 * - runs_that_share_a_number_split_by_a_page_edge_are_each_on_one_page
 * - a_page_key_whose_file_has_gone_still_names_its_place
 * - a_card_of_one_page_or_none_shows_no_page_buttons
 * - forty_nine_runs_are_two_pages_and_the_second_holds_the_oldest
 * - ninety_six_and_ninety_seven_runs_page_at_48
 * - a_full_card_is_reached_to_its_oldest_run_a_walk_a_page
 * - files_the_bench_did_not_write_follow_the_runs_and_are_counted
 * - deleting_the_last_pages_only_file_shows_the_page_before
 * - after_delete_nothing_is_selected_and_the_scroll_stays
 * - rescan_on_page_three_reads_that_page_and_follows_a_shrunk_card
 * - prev_reads_the_first_page_from_the_top_whatever_changed_ahead
 * - a_card_swapped_mid_page_opens_on_its_first_page
 * - the_scroll_bar_jumps_drags_and_stays_on_its_page
 * - re_reading_the_card_selects_the_newest_run
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

/* -------------------------------------------------------------- pages --- */

/*
 * A card of @p runs runs and @p others other files, offered in an order that
 * is neither the rank nor its reverse, and its full rank order in @p all.
 */
#define PG_MAX (LOG_RUN_LAST + 8)

static log_viewer_file_t g_pg_all[PG_MAX];

static int g_pg_n;

static int g_pg_order[PG_MAX];

static int by_rank(const void *a, const void *b)
{
    return log_name_rank(((const log_viewer_file_t *)a)->name,
                         ((const log_viewer_file_t *)b)->name);
}

/* One entry on the card, @p size telling it from any other. */
static void pg_put(const char *name, uint32_t size)
{
    if (g_pg_n < PG_MAX) {
        memset(&g_pg_all[g_pg_n], 0, sizeof(g_pg_all[0]));
        snprintf(g_pg_all[g_pg_n].name, sizeof(g_pg_all[0].name), "%s", name);
        g_pg_all[g_pg_n].size = size;
        ++g_pg_n;
    }
}

/* The offer order: a stride coprime with any count up to PG_MAX scatters
 * it. */
static void pg_scatter(void)
{
    for (int i = 0; i < g_pg_n; ++i) {
        g_pg_order[i] = (int)(((long)i * 7919L) % g_pg_n);
    }
}

/* @p runs runs written by writer @p prefix, or with @p mixed by BENCH, ESC
 * and SERVO in turn, and @p others other files. */
static void pg_card_by(int runs, int others, bool mixed, log_prefix_t prefix)
{
    g_pg_n = 0;
    for (int r = 1; r <= runs; ++r) {
        char name[LOG_RUN_NAME_MAX];
        log_name_build(name, sizeof(name),
                       mixed ? (log_prefix_t)(r % (int)LOG_PREFIX_COUNT)
                             : prefix,
                       r, false);
        pg_put(name, (uint32_t)r);
    }
    for (int i = 0; i < others; ++i) {
        char name[LOG_VIEWER_NAME_MAX];
        snprintf(name, sizeof(name), "OTHER%03d.CSV", i);
        pg_put(name, 0u);
    }
    pg_scatter();
}

static void pg_card(int runs, int others)
{
    pg_card_by(runs, others, false, LOG_PREFIX_BENCH);
}

/* One walk: the page @p q asks for, into @p set of @p cap. */
static int pg_walk(log_viewer_file_t *set, int cap, log_viewer_page_t *q)
{
    log_select_page_t sel;
    log_select_page_begin(&sel, set, cap, q);
    for (int i = 0; i < g_pg_n; ++i) {
        log_select_page_offer(&sel, &g_pg_all[g_pg_order[i]]);
    }
    return log_select_page_end(&sel, q);
}

/* The edge of a page in rank order: first (@p sign 1) or last (-1). */
static const char *pg_edge(const log_viewer_file_t *set, int n, int sign)
{
    int best = 0;
    for (int i = 1; i < n; ++i) {
        if (sign * log_name_rank(set[i].name, set[best].name) < 0) {
            best = i;
        }
    }
    return set[best].name;
}

/* Whether @p set holds ranks [first, first + n) of the card, drawn in
 * display order. */
static bool pg_is_slice(const log_viewer_file_t *set, int n, int first,
                        const log_viewer_file_t *ranked)
{
    static log_viewer_file_t want[LOG_VIEWER_MAX_FILES];
    memcpy(want, ranked + first, (size_t)n * sizeof(want[0]));
    log_select_sort(want, n);
    for (int i = 0; i < n; ++i) {
        if (strcmp(set[i].name, want[i].name) != 0
            || set[i].size != want[i].size) {
            return false;
        }
    }
    return true;
}

/* Index of @p name in g_pg_all, or -1. */
static int pg_index(const char *name)
{
    for (int i = 0; i < g_pg_n; ++i) {
        if (strcmp(g_pg_all[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * One page as it is drawn: in strict rank order, so its runs stand newest
 * first whichever writer made them, and each entry counted in @p hits.
 */
static void pg_count(const log_viewer_file_t *set, int n, int *hits)
{
    for (int i = 0; i < n; ++i) {
        const int k = pg_index(set[i].name);
        CHECK(k >= 0);
        if (k >= 0) {
            ++hits[k];
        }
        if (i > 0 && log_name_rank(set[i - 1].name, set[i].name) >= 0) {
            T_FAIL("\"%s\" is drawn above \"%s\"", set[i - 1].name,
                   set[i].name);
        }
        const int a = (i > 0) ? log_run_number(set[i - 1].name) : -1;
        const int b = log_run_number(set[i].name);
        if (a >= 0 && b >= 0 && b > a) {
            T_FAIL("run %d is drawn under the older run %d", b, a);
        }
    }
}

/*
 * The card in g_pg_all, paged: NEXT's chain from the first page reaches every
 * entry once, in rank order, and PREV's chain from the last page walks the
 * same pages back.  Every entry is on exactly one page each way.
 */
static void pg_check(void)
{
    static log_viewer_file_t ranked[PG_MAX];
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    static int starts[PG_MAX / LOG_VIEWER_MAX_FILES + 2];
    static int fwd[PG_MAX];
    static int back[PG_MAX];
    memset(fwd, 0, sizeof(fwd));
    memset(back, 0, sizeof(back));
    memcpy(ranked, g_pg_all, (size_t)g_pg_n * sizeof(ranked[0]));
    qsort(ranked, (size_t)g_pg_n, sizeof(ranked[0]), by_rank);

    const int pages = (g_pg_n + LOG_VIEWER_MAX_FILES - 1)
                      / LOG_VIEWER_MAX_FILES;
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    int seen = 0;
    int p = 0;
    for (;;) {
        const int held = pg_walk(set, LOG_VIEWER_MAX_FILES, &q);
        CHECK_EQ(q.held, held);
        if (held == 0 || p > pages) {
            break;
        }
        CHECK_EQ(q.ahead, seen);
        CHECK(pg_is_slice(set, held, seen, ranked));
        CHECK_EQ(held, (g_pg_n - seen < LOG_VIEWER_MAX_FILES)
                           ? g_pg_n - seen : LOG_VIEWER_MAX_FILES);
        pg_count(set, held, fwd);
        starts[p++] = seen;
        seen += held;
        q.dir = LOG_PAGE_AFTER;
        snprintf(q.key, sizeof(q.key), "%s", pg_edge(set, held, -1));
    }
    CHECK_EQ(seen, g_pg_n);
    CHECK_EQ(p, pages);

    /* Back from the last page: BEFORE with no key is the last of all,
     * cut to what whole pages leave. */
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_BEFORE;
    if (pages == 0) {
        CHECK_EQ(pg_walk(set, LOG_VIEWER_MAX_FILES, &q), 0);
        CHECK_EQ(q.ahead, 0);
        return;
    }
    int held = pg_walk(set, g_pg_n - starts[pages - 1], &q);
    CHECK_EQ(q.ahead, starts[pages - 1]);
    CHECK(pg_is_slice(set, held, starts[pages - 1], ranked));
    pg_count(set, held, back);
    for (int k = pages - 2; k >= 0; --k) {
        snprintf(q.key, sizeof(q.key), "%s", pg_edge(set, held, 1));
        held = pg_walk(set, LOG_VIEWER_MAX_FILES, &q);
        CHECK_EQ(held, LOG_VIEWER_MAX_FILES);
        CHECK_EQ(q.ahead, starts[k]);
        CHECK(pg_is_slice(set, held, starts[k], ranked));
        pg_count(set, held, back);
    }
    /* And before the first page there is nothing. */
    snprintf(q.key, sizeof(q.key), "%s", pg_edge(set, held, 1));
    CHECK_EQ(pg_walk(set, LOG_VIEWER_MAX_FILES, &q), 0);
    CHECK_EQ(q.ahead, 0);

    for (int i = 0; i < g_pg_n; ++i) {
        if (fwd[i] != 1 || back[i] != 1) {
            T_FAIL("\"%s\" is on %d pages forward and %d back",
                   g_pg_all[i].name, fwd[i], back[i]);
        }
    }
}

TEST_CASE(pages_of_a_card_cover_it_once_in_rank_order_both_ways)
{
    /*
     * At the page boundaries: none, one page exactly, one over, two exactly,
     * two and one over, and a full card of 999 runs with files the bench did
     * not write behind them.
     */
    static const struct { int runs, others; } cards[] = {
        { 0, 0 }, { 48, 0 }, { 49, 0 }, { 96, 0 }, { 97, 0 },
        { 999, 0 }, { 999, 3 }, { 40, 9 },
    };
    for (size_t c = 0; c < sizeof(cards) / sizeof(cards[0]); ++c) {
        pg_card(cards[c].runs, cards[c].others);
        pg_check();
    }
}

TEST_CASE(pages_of_mixed_writers_run_newest_first_across_the_names)
{
    /*
     * The 7 writers in turn: 999 runs with and without other files
     * behind them, and the page boundaries.  Each page holds all seven
     * names and draws them by number, newest at the top; the pages run on
     * by number across the names.
     */
    static const int runs[] = { 47, 48, 49, 96, 97, 999 };
    for (size_t c = 0; c < sizeof(runs) / sizeof(runs[0]); ++c) {
        pg_card_by(runs[c], 0, true, LOG_PREFIX_BENCH);
        pg_check();
    }
    pg_card_by(LOG_RUN_LAST, 3, true, LOG_PREFIX_BENCH);
    pg_check();
    /* Each writer alone pages the same way. */
    pg_card_by(LOG_RUN_LAST, 0, false, LOG_PREFIX_ESC);
    pg_check();
    pg_card_by(LOG_RUN_LAST, 0, false, LOG_PREFIX_SERVO);
    pg_check();

    /* The first page of the mixed full card: 999 down to 952, all seven
     * names in it. */
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    pg_card_by(LOG_RUN_LAST, 0, true, LOG_PREFIX_BENCH);
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    CHECK_EQ(pg_walk(set, LOG_VIEWER_MAX_FILES, &q), LOG_VIEWER_MAX_FILES);
    CHECK_STR_EQ(set[0].name, "BENCH999.CSV");       /* 999% 7 == 5 */
    CHECK_STR_EQ(set[1].name, "SUPPL998.CSV");
    CHECK_STR_EQ(set[2].name, "MOTOR997.CSV");
    CHECK_STR_EQ(set[3].name, "SERVO996.CSV");
    CHECK_STR_EQ(set[4].name, "LIMIT995.CSV");
    CHECK_STR_EQ(set[5].name, "ATEST994.CSV");
    CHECK_STR_EQ(set[6].name, "ESC993.CSV");
    CHECK_STR_EQ(set[LOG_VIEWER_MAX_FILES - 1].name, "ATEST952.CSV");
}

/*
 * A card edited on a computer can hold one number under several names.  The
 * rank breaks that tie by name, BENCH before ESC before SERVO, case folded,
 * so the order stays strict and a page edge between them drops none.
 */
static void pg_tie_card(int above, const char *const *tied, int n_tied)
{
    g_pg_n = 0;
    for (int r = 42 + above; r > 42; --r) {
        char name[LOG_RUN_NAME_MAX];
        log_name_build(name, sizeof(name),
                       (log_prefix_t)(r % (int)LOG_PREFIX_COUNT), r, false);
        pg_put(name, (uint32_t)r);
    }
    for (int i = 0; i < n_tied; ++i) {
        pg_put(tied[i], 4200u + (uint32_t)i);
    }
    for (int r = 1; r < 42; ++r) {
        char name[LOG_RUN_NAME_MAX];
        log_name_build(name, sizeof(name),
                       (log_prefix_t)(r % (int)LOG_PREFIX_COUNT), r, false);
        pg_put(name, (uint32_t)r);
    }
    pg_scatter();
}

TEST_CASE(runs_that_share_a_number_split_by_a_page_edge_are_each_on_one_page)
{
    static const char *const upper[] = {
        "SERVO042.CSV", "BENCH042.CSV", "ESC042.CSV",
    };
    static const char *const lower[] = {
        "servo042.csv", "Bench042.CSV", "esc042.csv",
    };
    CHECK(log_name_rank("BENCH042.CSV", "ESC042.CSV") < 0);
    CHECK(log_name_rank("ESC042.CSV", "SERVO042.CSV") < 0);
    CHECK(log_name_rank("SERVO042.CSV", "BENCH042.CSV") > 0);
    CHECK(log_name_rank("servo042.csv", "Bench042.CSV") > 0);
    CHECK_EQ(log_name_rank("ESC042.CSV", "esc042.csv"), 0);
    CHECK(log_name_rank("BENCH043.CSV", "SERVO042.CSV") < 0);

    /* 47, 46 and 45 runs above: the edge after the first, after the
     * second, and after all three of the tied names; then no edge at 48. */
    static const int above[] = { 47, 46, 45, 48, 44 };
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    for (size_t a = 0; a < sizeof(above) / sizeof(above[0]); ++a) {
        for (int c = 0; c < 2; ++c) {
            pg_tie_card(above[a], c ? lower : upper, 3);
            pg_check();
        }
    }

    /* 47 above: page 1 ends on BENCH042, page 2 opens on ESC042, and PREV
     * from page 2 is page 1 again with BENCH042 at its foot. */
    pg_tie_card(47, upper, 3);
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    int held = pg_walk(set, LOG_VIEWER_MAX_FILES, &q);
    CHECK_EQ(held, LOG_VIEWER_MAX_FILES);
    CHECK_STR_EQ(set[held - 1].name, "BENCH042.CSV");
    q.dir = LOG_PAGE_AFTER;
    snprintf(q.key, sizeof(q.key), "%s", set[held - 1].name);
    held = pg_walk(set, LOG_VIEWER_MAX_FILES, &q);
    CHECK_EQ(held, 2 + 41);         /* the other two 042s and 041 to 001 */
    CHECK_EQ(q.ahead, LOG_VIEWER_MAX_FILES);
    CHECK_STR_EQ(set[0].name, "ESC042.CSV");
    CHECK_STR_EQ(set[1].name, "SERVO042.CSV");
    q.dir = LOG_PAGE_BEFORE;
    snprintf(q.key, sizeof(q.key), "%s", set[0].name);
    held = pg_walk(set, LOG_VIEWER_MAX_FILES, &q);
    CHECK_EQ(held, LOG_VIEWER_MAX_FILES);
    CHECK_EQ(q.ahead, 0);
    CHECK_STR_EQ(set[held - 1].name, "BENCH042.CSV");
}

TEST_CASE(a_page_key_whose_file_has_gone_still_names_its_place)
{
    /* FROM a name the card no longer holds starts with the entry ranking
     * behind it: a deleted page's first entry still finds the page. */
    pg_card(100, 0);
    /* BENCH060 is gone. */
    for (int i = 0; i < g_pg_n; ++i) {
        if (strcmp(g_pg_all[i].name, "BENCH060.CSV") == 0) {
            g_pg_all[i] = g_pg_all[g_pg_n - 1];
            --g_pg_n;
            break;
        }
    }
    for (int i = 0; i < g_pg_n; ++i) {
        g_pg_order[i] = i;
    }
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    snprintf(q.key, sizeof(q.key), "%s", "BENCH060.CSV");
    CHECK_EQ(pg_walk(set, LOG_VIEWER_MAX_FILES, &q), LOG_VIEWER_MAX_FILES);
    CHECK_EQ(q.ahead, 40);                     /* runs 100 to 61 */
    CHECK_STR_EQ(pg_edge(set, q.held, 1), "BENCH059.CSV");
    CHECK_STR_EQ(pg_edge(set, q.held, -1), "BENCH012.CSV");

    /* Case is folded: the key is the same file in lower case. */
    snprintf(q.key, sizeof(q.key), "%s", "bench059.csv");
    q.dir = LOG_PAGE_AFTER;
    CHECK_EQ(pg_walk(set, LOG_VIEWER_MAX_FILES, &q), LOG_VIEWER_MAX_FILES);
    CHECK_STR_EQ(pg_edge(set, q.held, 1), "BENCH058.CSV");

    /* No set, no room, no candidate: nothing kept, nothing past its end. */
    log_select_page_t sel;
    log_select_page_begin(&sel, NULL, 4, &q);
    log_select_page_offer(&sel, &g_pg_all[0]);
    log_select_page_offer(&sel, NULL);
    CHECK_EQ(log_select_page_end(&sel, NULL), 0);
    log_select_page_begin(NULL, set, 4, NULL);
    log_select_page_offer(NULL, &g_pg_all[0]);
    CHECK_EQ(log_select_page_end(NULL, &q), 0);
    /* A page of NULL is the first. */
    log_select_page_begin(&sel, set, 4, NULL);
    for (int i = 0; i < g_pg_n; ++i) {
        log_select_page_offer(&sel, &g_pg_all[i]);
    }
    CHECK_EQ(log_select_page_end(&sel, NULL), 4);
    CHECK_STR_EQ(pg_edge(set, 4, 1), "BENCH100.CSV");
}

/* The header row's PREV and NEXT, in screen coordinates. */
#define PREV_X  LV_PREV_CX

static void prev_page(void)
{
    tap(PREV_X, PG_CY);
}

/* The bar: x of its middle, and the track's top and height. */
#define BAR_X     LV_BAR_CX

#define BAR_TOP   LV_BAR_TOP

#define BAR_H     LV_BAR_H

/* The thumb on the drawn frame: the first accent row down the bar's middle,
 * and how many accent rows follow. */
static int thumb_top(void)
{
    for (int y = BAR_TOP; y < BAR_TOP + BAR_H; ++y) {
        if (gfx_pixel_get(&s_c, BAR_X, y) == UI_ACCENT
            || gfx_pixel_get(&s_c, BAR_X, y) == UI_PANEL_HI) {
            return y;
        }
    }
    return -1;
}

static int thumb_len(void)
{
    const int y0 = thumb_top();
    if (y0 < 0) {
        return 0;
    }
    const gfx_color_t col = gfx_pixel_get(&s_c, BAR_X, y0);
    int y = y0;
    while (y < BAR_TOP + BAR_H && gfx_pixel_get(&s_c, BAR_X, y) == col) {
        ++y;
    }
    return y - y0;
}

/* Nothing selected: OPEN opens nothing, and DELETE asks nothing, so the
 * confirm under it removes nothing. */
static void check_nothing_selected(void)
{
    const int removes = g_remove_calls;
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, removes);
}

TEST_CASE(a_card_of_one_page_or_none_shows_no_page_buttons)
{
    /* 0 files and exactly 48: the tab reads FILES, and where PREV and NEXT
     * would be a tap reads nothing from the card. */
    fresh();
    g_empty_card = true;
    log_viewer_refresh();
    draw();
    int calls = g_list_calls;
    next_page();
    prev_page();
    CHECK_EQ(g_list_calls, calls);

    runs_card(LOG_VIEWER_MAX_FILES);
    check_tab(0, 0, 0);
    calls = g_list_calls;
    next_page();
    prev_page();
    CHECK_EQ(g_list_calls, calls);
    check_newest(LOG_VIEWER_MAX_FILES);
}

TEST_CASE(forty_nine_runs_are_two_pages_and_the_second_holds_the_oldest)
{
    runs_card(49);
    check_tab(1, 48, 49);
    check_newest(49);

    /* PREV is grey on the first page: a tap reads nothing. */
    int calls = g_list_calls;
    prev_page();
    CHECK_EQ(g_list_calls, calls);

    next_page();
    CHECK_EQ(g_list_calls, calls + 1);       /* one walk for the page */
    check_tab(49, 49, 49);
    check_newest(1);

    /* NEXT is grey on the last. */
    calls = g_list_calls;
    next_page();
    CHECK_EQ(g_list_calls, calls);
    CHECK_EQ(g_last_page.dir, LOG_PAGE_AFTER);

    prev_page();
    CHECK_EQ(g_list_calls, calls + 1);
    CHECK_EQ(g_last_page.dir, LOG_PAGE_BEFORE);
    check_tab(1, 48, 49);
    check_newest(49);
}

TEST_CASE(ninety_six_and_ninety_seven_runs_page_at_48)
{
    runs_card(96);
    check_tab(1, 48, 96);
    next_page();
    check_tab(49, 96, 96);
    check_newest(48);
    int calls = g_list_calls;
    next_page();
    CHECK_EQ(g_list_calls, calls);

    runs_card(97);
    next_page();
    check_tab(49, 96, 97);
    check_newest(49);
    next_page();
    check_tab(97, 97, 97);
    check_newest(1);
    /* 48 ahead of the last page is a whole page: PREV shows page 2, not
     * the first. */
    prev_page();
    check_tab(49, 96, 97);
    check_newest(49);
    prev_page();
    check_tab(1, 48, 97);
    check_newest(97);
}

TEST_CASE(a_full_card_is_reached_to_its_oldest_run_a_walk_a_page)
{
    /* 999 runs: 21 pages, the last holding 39.  Each page is one listing. */
    runs_card(LOG_RUN_LAST);
    const int pages = (LOG_RUN_LAST + LOG_VIEWER_MAX_FILES - 1)
                      / LOG_VIEWER_MAX_FILES;
    for (int p = 1; p < pages; ++p) {
        const int calls = g_list_calls;
        next_page();
        CHECK_EQ(g_list_calls, calls + 1);
        const int a = p * LOG_VIEWER_MAX_FILES + 1;
        const int b = (a + LOG_VIEWER_MAX_FILES - 1 < LOG_RUN_LAST)
                      ? a + LOG_VIEWER_MAX_FILES - 1 : LOG_RUN_LAST;
        check_tab(a, b, LOG_RUN_LAST);
    }
    const int first = (pages - 1) * LOG_VIEWER_MAX_FILES + 1;
    check_tab(first, LOG_RUN_LAST, LOG_RUN_LAST);
    check_newest(LOG_RUN_LAST - first + 1);
    /* Back one page at a time, each page where NEXT found it. */
    for (int p = pages - 2; p >= 0; --p) {
        const int calls = g_list_calls;
        prev_page();
        CHECK_EQ(g_list_calls, calls + 1);
        const int a = p * LOG_VIEWER_MAX_FILES + 1;
        check_tab(a, a + LOG_VIEWER_MAX_FILES - 1, LOG_RUN_LAST);
        check_newest(LOG_RUN_LAST - a + 1);
    }
    check_tab(1, 48, LOG_RUN_LAST);
    check_newest(LOG_RUN_LAST);
}

TEST_CASE(files_the_bench_did_not_write_follow_the_runs_and_are_counted)
{
    /* 48 runs and 5 other .csv files: the runs fill the first page, the
     * others are the second, and the count holds all 53. */
    fresh();
    g_card_runs = 48;
    g_card_others = 5;
    log_viewer_refresh();
    check_tab(1, 48, 53);
    check_newest(48);
    next_page();
    check_tab(49, 53, 53);
    /* No run on that page: nothing selected, OPEN opens nothing. */
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    CHECK_STR_EQ(log_viewer_open_name(), "OTHER000.CSV");
}

TEST_CASE(deleting_the_last_pages_only_file_shows_the_page_before)
{
    runs_card(49);
    next_page();
    check_tab(49, 49, 49);
    /* The page selects its newest run, BENCH001: DELETE asks about it. */
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK(g_run_gone[1]);
    /* 48 left, one page: FILES, nothing selected, the newest run on top. */
    check_tab(0, 0, 0);
    check_nothing_selected();
    CHECK_EQ(top_row_run(), 49);

    /* Two on the last page: one deleted, the page stays. */
    runs_card(98);
    next_page();
    next_page();
    check_tab(97, 98, 98);
    tap(DELETE_X, FOOT_CY);                     /* BENCH002, selected */
    tap(DQ_DELETE_X, DQ_CY);
    CHECK(g_run_gone[2]);
    check_tab(97, 97, 97);
    check_nothing_selected();
    CHECK_EQ(top_row_run(), 1);
}

TEST_CASE(after_delete_nothing_is_selected_and_the_scroll_stays)
{
    /*
     * Page 2 of 150 runs, scrolled to its bottom: rows 61 to 55 show.  The
     * page's newest run, BENCH102, is off screen above them.  DELETE of
     * BENCH055 leaves nothing selected, so DELETE and OPEN act on nothing
     * and the next deletion needs a row chosen in sight.
     */
    runs_card(150);
    next_page();
    tap(BAR_X, BAR_TOP + BAR_H - 2);
    tap(400, BR_ROW_Y(BR_ROWS_SEEN - 1));
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 1);
    CHECK_STR_EQ(g_removed_name, "BENCH055.CSV");
    check_nothing_selected();
    CHECK_EQ(g_remove_calls, 1);
    CHECK(!g_run_gone[102]);
    /* The page is read again from BENCH102: 102 to 56, then 54.  The
     * scroll is the one before, 41, so the top row is the 42nd: 61. */
    check_tab(49, 96, 149);
    CHECK_EQ(top_row_run(), 61);

    /* The first page: the same, the scroll at its top. */
    runs_card(60);
    tap(400, BR_ROW_Y(2));
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK(g_run_gone[58]);
    check_nothing_selected();
    CHECK_EQ(g_remove_calls, 1);
    check_tab(1, 48, 59);
    CHECK_EQ(top_row_run(), 60);
}

TEST_CASE(rescan_on_page_three_reads_that_page_and_follows_a_shrunk_card)
{
    runs_card(150);
    next_page();
    next_page();
    check_tab(97, 144, 150);
    check_top(54);

    /* The same card read again: the same page, its first entry included. */
    tap(RESCAN_X, FOOT_CY);
    check_tab(97, 144, 150);
    check_top(54);
    tap(RESCAN_X, FOOT_CY);
    check_tab(97, 144, 150);
    check_top(54);

    /* Runs 1 to 60 gone behind the screen's back: the page starts where it
     * did, at run 54's place, and that is past the card's end now.  The
     * last page is shown, cut as the pages from the first cut it. */
    g_card_run0 = 61;
    g_card_runs = 90;
    tap(RESCAN_X, FOOT_CY);
    check_tab(49, 90, 90);
    check_top(102);

    /* A run recorded while page 1 shows is on it after RESCAN. */
    runs_card(60);
    tap(RESCAN_X, FOOT_CY);
    check_top(60);
    g_card_runs = 61;
    tap(RESCAN_X, FOOT_CY);
    check_tab(1, 48, 61);
    check_top(61);
}

TEST_CASE(prev_reads_the_first_page_from_the_top_whatever_changed_ahead)
{
    /* Page 2 of 100, and a run recorded meanwhile: 49 entries stand ahead
     * of the page now.  PREV shows the first page as the pages cut it,
     * the new run at its top, not the 48 just ahead of the page. */
    runs_card(100);
    next_page();
    check_tab(49, 96, 100);
    g_card_runs = 101;
    prev_page();
    check_tab(1, 48, 101);
    check_top(101);
    /* And the page after it is cut from the top as well. */
    next_page();
    check_tab(49, 96, 101);
    check_top(53);

    /* Everything ahead of page 2 gone behind the screen's back: PREV has
     * nothing before the key and shows the first page, not the last. */
    runs_card(100);
    next_page();
    for (int r = 53; r <= 100; ++r) {
        g_run_gone[r] = true;
    }
    prev_page();
    check_tab(1, 48, 52);
    check_top(52);

    /* Some of it gone: fewer than a page ahead, read from the top. */
    runs_card(100);
    next_page();
    for (int r = 60; r <= 100; ++r) {
        g_run_gone[r] = true;
    }
    prev_page();
    check_tab(1, 48, 59);
    check_top(59);
}

TEST_CASE(a_card_swapped_mid_page_opens_on_its_first_page)
{
    runs_card(150);
    next_page();
    check_tab(49, 96, 150);

    /* Another card's name in the header: its first page. */
    g_volume = "UNITEST";
    g_card_runs = 120;
    tap(RESCAN_X, FOOT_CY);
    check_tab(1, 48, 120);
    check_newest(120);

    /* Pulled, then put back: the read that finds no volume forgets the
     * page, and the next one starts at the first. */
    next_page();
    g_no_card = true;
    tap(RESCAN_X, FOOT_CY);
    g_no_card = false;
    tap(RESCAN_X, FOOT_CY);
    check_tab(1, 48, 120);

    /* Same name, other runs: the page's place in the new card's order,
     * with the tab's numbers the new card's. */
    next_page();
    g_card_run0 = 500;
    g_card_runs = 200;
    tap(RESCAN_X, FOOT_CY);
    /* The page started at run 72; every run of 500 to 699 ranks ahead of
     * it, so it is past the end and the last page shows. */
    check_tab(193, 200, 200);

    /* Swapped with no tap between, found by the RESCAN's own look: the
     * header named the old card until the list mounted the new one, and
     * the new one opens on its first page. */
    runs_card(150);
    next_page();
    check_tab(49, 96, 150);
    g_swap_volume = "SK64G";
    g_swap_runs = 120;
    tap(RESCAN_X, FOOT_CY);
    check_tab(1, 48, 120);
    check_newest(120);
    /* And PREV and NEXT go on from there. */
    next_page();
    check_tab(49, 96, 120);

    /* Swapped for a card of the same label: the mount number tells them
     * apart, at the RESCAN's own list and at the next one. */
    g_swap_volume = "SK64G";
    g_swap_runs = 150;
    tap(RESCAN_X, FOOT_CY);
    check_tab(1, 48, 150);
    next_page();
    ++g_mounts;                        /* mounted by another event */
    g_card_runs = 130;
    tap(RESCAN_X, FOOT_CY);
    check_tab(1, 48, 130);

    /* Mounted by another task after the top page was read from the old
     * card: the page is read again, from the new card's top. */
    g_swap_late_volume = "SK64G";
    g_swap_late_runs = 110;
    const int calls = g_list_calls;
    tap(RESCAN_X, FOOT_CY);
    CHECK_EQ(g_list_calls, calls + 2);
    check_tab(1, 48, 110);
    check_newest(110);
}

/* ---------------------------------------------------------- scroll bar --- */


TEST_CASE(the_scroll_bar_jumps_drags_and_stays_on_its_page)
{
    /* 48 runs: rows are runs 48 to 1 top down, and 41 is the last scroll. */
    runs_card(48);
    /* The thumb is the rows shown against the page: 332 * 7 / 48 = 48px,
     * drawn 2px in from each end, at the top of the track. */
    const int th = BAR_H * BR_ROWS_SEEN / 48;
    CHECK_EQ(th, 48);
    draw();
    CHECK_EQ(thumb_top(), BAR_TOP + 2);
    CHECK_EQ(thumb_len(), th - 4);
    /* A tap at the bottom of the track brings the thumb there. */
    tap(BAR_X, BAR_TOP + BAR_H - 2);
    draw();
    CHECK_EQ(thumb_top(), BAR_TOP + BAR_H - th + 2);
    CHECK_EQ(top_row_run(), 7);
    /* And at the top, back. */
    tap(BAR_X, BAR_TOP + 1);
    CHECK_EQ(top_row_run(), 48);

    /* A tap off the thumb brings the thumb's middle to the finger: 200px
     * down the track puts its top at 176 of 284px of travel, scroll
     * (176 * 41 + 142) / 284 = 25, and the top row is run 48 - 25. */
    tap(BAR_X, BAR_TOP + 200);
    draw();
    CHECK_EQ(top_row_run(), 23);
    CHECK_EQ(thumb_top(), BAR_TOP + (BAR_H - th) * 25 / 41 + 2);
    tap(BAR_X, BAR_TOP + 1);

    /* Dragging the thumb (48px at the top) by half the travel scrolls half
     * the page; past the track's end it stops at the end, and on a 49th
     * row there is nothing to scroll to. */
    send(TOUCH_EVENT_DOWN, BAR_X, BAR_TOP + th / 2);
    send(TOUCH_EVENT_MOVE, BAR_X, BAR_TOP + th / 2 + (BAR_H - th) / 2);
    draw();
    send(TOUCH_EVENT_UP, BAR_X, BAR_TOP + th / 2 + (BAR_H - th) / 2);
    CHECK_EQ(top_row_run(), 27);
    send(TOUCH_EVENT_DOWN, BAR_X, BAR_TOP + BAR_H / 2);
    send(TOUCH_EVENT_MOVE, BAR_X, 2000);
    send(TOUCH_EVENT_UP, BAR_X, 2000);
    CHECK_EQ(top_row_run(), 7);
    send(TOUCH_EVENT_DOWN, BAR_X, BAR_TOP + BAR_H - 1);
    send(TOUCH_EVENT_MOVE, BAR_X, -2000);
    send(TOUCH_EVENT_UP, BAR_X, -2000);
    CHECK_EQ(top_row_run(), 48);

    /* A tap on the bar selects no row and opens nothing, and reads no
     * card. */
    const int calls = g_list_calls;
    tap(BAR_X, BAR_TOP + 100);
    tap(BAR_X, BAR_TOP + 100);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK_EQ(g_list_calls, calls);

    /* With every row showing there is nothing to scroll: the thumb is the
     * whole track, and a tap or a drag on it leaves the top row. */
    runs_card(4);
    draw();
    CHECK_EQ(thumb_len(), BAR_H - 4);
    tap(BAR_X, BAR_TOP + BAR_H - 2);
    CHECK_EQ(top_row_run(), 4);
    send(TOUCH_EVENT_DOWN, BAR_X, BAR_TOP + 10);
    send(TOUCH_EVENT_MOVE, BAR_X, BAR_TOP + BAR_H);
    send(TOUCH_EVENT_UP, BAR_X, BAR_TOP + BAR_H);
    CHECK_EQ(top_row_run(), 4);

    /* The bar scrolls within the page: at the bottom of page 2 of 97 the
     * last row is the oldest run on that page, and NEXT does not move. */
    runs_card(97);
    next_page();
    tap(BAR_X, BAR_TOP + 1);
    CHECK_EQ(top_row_run(), 49);
    tap(BAR_X, BAR_TOP + BAR_H - 2);
    CHECK_EQ(top_row_run(), 8);
    check_tab(49, 96, 97);

    /* A touch loss ends a drag: the next move belongs to no press. */
    runs_card(48);
    send(TOUCH_EVENT_DOWN, BAR_X, BAR_TOP + 1);
    screen()->cancel();
    send(TOUCH_EVENT_MOVE, BAR_X, BAR_TOP + BAR_H);
    send(TOUCH_EVENT_UP, BAR_X, BAR_TOP + BAR_H);
    CHECK_EQ(top_row_run(), 48);
}

TEST_CASE(re_reading_the_card_selects_the_newest_run)
{
    fresh();
    g_card_runs = 30;          /* more than seven rows: the newest scrolls in */
    log_viewer_refresh();
    tap(OPEN_X, FOOT_CY);
    draw();
    char want[LOG_VIEWER_NAME_MAX];
    log_run_name(want, sizeof(want), 30);
    CHECK_STR_EQ(log_viewer_open_name(), want);

    /* A card with no numbered run selects nothing: OPEN opens nothing. */
    fresh();
    log_viewer_refresh();
    tap(OPEN_X, FOOT_CY);
    CHECK_STR_EQ(log_viewer_open_name(), "");
}

int main(void)
{
    RUN(pages_of_a_card_cover_it_once_in_rank_order_both_ways);
    RUN(pages_of_mixed_writers_run_newest_first_across_the_names);
    RUN(runs_that_share_a_number_split_by_a_page_edge_are_each_on_one_page);
    RUN(a_page_key_whose_file_has_gone_still_names_its_place);
    RUN(a_card_of_one_page_or_none_shows_no_page_buttons);
    RUN(forty_nine_runs_are_two_pages_and_the_second_holds_the_oldest);
    RUN(ninety_six_and_ninety_seven_runs_page_at_48);
    RUN(a_full_card_is_reached_to_its_oldest_run_a_walk_a_page);
    RUN(files_the_bench_did_not_write_follow_the_runs_and_are_counted);
    RUN(deleting_the_last_pages_only_file_shows_the_page_before);
    RUN(after_delete_nothing_is_selected_and_the_scroll_stays);
    RUN(rescan_on_page_three_reads_that_page_and_follows_a_shrunk_card);
    RUN(prev_reads_the_first_page_from_the_top_whatever_changed_ahead);
    RUN(a_card_swapped_mid_page_opens_on_its_first_page);
    RUN(the_scroll_bar_jumps_drags_and_stays_on_its_page);
    RUN(re_reading_the_card_selects_the_newest_run);
    return test_summary("logview_pages");
}
