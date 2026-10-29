/**
 * @file test_fm_tree.c
 * @brief A pane of the file manager over a card in memory (fm_fake.h):
 *        folders of 0, 1, 47, 48, 49 and 999 entries, paging by key to
 *        every edge and back, a pane started at a key it does not hold, the pane's order, names of 63 and 64
 *        characters, the places and kinds of every path card_tree.h names
 *        and of the two log folders, a busy name, entering and leaving at
 *        the root, and a card gone part way through a walk.  And the page
 *        keeping under it (fm_select.h) at its NULL and empty edges.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "fm_fake.h"
#include "fm_select.h"
#include "fm_tree.h"
#include "hitec_card.h"

/* ------------------------------------------------------------ the rigs */

static fk_card_t g_card;
static fm_io_t g_io;
static fm_tree_t g_tree;

/**
 * @brief A fresh card and a pane in @p dir.
 * @param dir  the pane's folder
 */
static void fresh(const char *dir)
{
    fk_free(&g_card);
    fk_init(&g_card);
    g_io = fk_io(&g_card);
    fm_tree_init(&g_tree, dir);
}

/**
 * @brief Fill folder @p dir with @p n files F000.TXT on, made in an order
 *        that is not the pane's: file i is number i * 7 mod 1000.
 * @param dir  the folder
 * @param n    how many, at most 1000
 */
static void fill(const char *dir, int n)
{
    if (dir[0] != '\0') {
        fk_dir(&g_card, dir);
    }
    char path[FK_PATH];
    for (int i = 0; i < n; ++i) {
        /* 7 is prime to 1000: every number once, out of order. */
        const int k = (i * 7) % 1000;
        snprintf(path, sizeof(path), "%s%sF%03d.TXT", dir,
                 (dir[0] != '\0') ? "/" : "", k);
        fk_file(&g_card, path, NULL, (size_t)k);
    }
}

/**
 * @brief Whether the page runs in the pane's order.
 * @return true when every row stands below the one before
 */
static bool page_in_order(void)
{
    for (int i = 1; i < g_tree.held; ++i) {
        if (fm_tree_rank(&g_tree.page[i - 1], &g_tree.page[i]) >= 0) {
            return false;
        }
    }
    return true;
}

/**
 * @brief A name of @p n characters ending in ".TXT".
 * @param out  room for it
 * @param n    its length, 5 or more
 */
static void long_name(char *out, size_t n)
{
    memset(out, 'L', n - 4u);
    memcpy(out + n - 4u, ".TXT", 5u);
}

/* ------------------------------------------------------- folder sizes */

TEST_CASE(an_empty_folder_is_an_empty_page)
{
    fresh("EMPTY");
    fk_dir(&g_card, "EMPTY");
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_tree.held, 0);
    CHECK_EQ(g_tree.total, 0);
    CHECK_EQ(g_tree.ahead, 0);
    CHECK(!fm_tree_has_prev(&g_tree));
    CHECK(!fm_tree_has_next(&g_tree));
    CHECK(fm_tree_selected(&g_tree) == NULL);
    /* NEXT, PREV, LAST and SAME of nothing are the same nothing. */
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_NEXT), FM_OK);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_PREV), FM_OK);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_LAST), FM_OK);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_SAME), FM_OK);
    CHECK_EQ(g_tree.held, 0);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(one_entry_is_one_row)
{
    fresh("");
    fk_file(&g_card, "ONLY.TXT", NULL, 300u);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_tree.held, 1);
    CHECK_EQ(g_tree.total, 1);
    CHECK_STR_EQ(g_tree.page[0].name, "ONLY.TXT");
    CHECK_EQ(g_tree.page[0].size, 300);
    CHECK(!g_tree.page[0].is_dir);
    CHECK_EQ(g_tree.page[0].kind, FM_KIND_TEXT);
    CHECK_EQ(g_tree.page[0].place, FM_PLACE_FREE);
    CHECK(!fm_tree_has_next(&g_tree));
    CHECK_EQ(g_tree.mount, 1u);
}

TEST_CASE(entries_47_48_and_49_fill_one_page_and_spill_the_49th)
{
    const int sizes[] = { 47, 48, 49 };
    for (size_t s = 0u; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        fresh("D");
        fill("D", sizes[s]);
        CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
        const int first = (sizes[s] < FM_PAGE_MAX) ? sizes[s] : FM_PAGE_MAX;
        CHECK_EQ(g_tree.held, first);
        CHECK_EQ(g_tree.total, sizes[s]);
        CHECK(page_in_order());
        CHECK_EQ(fm_tree_has_next(&g_tree), sizes[s] > FM_PAGE_MAX);
        CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_NEXT), FM_OK);
        if (sizes[s] > FM_PAGE_MAX) {
            CHECK_EQ(g_tree.held, 1);
            CHECK_EQ(g_tree.ahead, FM_PAGE_MAX);
            CHECK(fm_tree_has_prev(&g_tree));
        } else {
            /* Past the end is the last page, which is the only one. */
            CHECK_EQ(g_tree.held, first);
            CHECK_EQ(g_tree.ahead, 0);
        }
    }
}

TEST_CASE(entries_999_page_forward_and_back_to_every_edge)
{
    fresh("USER/LOGS");
    fill("USER/LOGS", 999);
    static bool seen[1000];
    memset(seen, 0, sizeof(seen));
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    int pages = 1;
    int rows = 0;
    char last[FM_NAME_BUF] = "";
    for (;;) {
        CHECK_EQ(g_tree.total, 999);
        CHECK_EQ(g_tree.ahead, rows);
        CHECK(page_in_order());
        /* Each page reads on from the one before it. */
        if (last[0] != '\0') {
            CHECK(strcmp(last, g_tree.page[0].name) < 0);
        }
        for (int i = 0; i < g_tree.held; ++i) {
            int k = -1;
            CHECK_EQ(sscanf(g_tree.page[i].name, "F%03d.TXT", &k), 1);
            CHECK(k >= 0 && k < 1000 && !seen[k]);
            if (k >= 0 && k < 1000) {
                seen[k] = true;
            }
        }
        rows += g_tree.held;
        memcpy(last, g_tree.page[g_tree.held - 1].name, sizeof(last));
        if (!fm_tree_has_next(&g_tree) || pages > 30) {
            break;
        }
        CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_NEXT), FM_OK);
        ++pages;
    }
    CHECK_EQ(rows, 999);
    CHECK_EQ(pages, 21);
    CHECK_EQ(g_tree.held, 999 - 20 * FM_PAGE_MAX);
    /* Back to the first page, a full page each time. */
    int back = 0;
    while (fm_tree_has_prev(&g_tree) && back < 30) {
        CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_PREV), FM_OK);
        CHECK_EQ(g_tree.held, FM_PAGE_MAX);
        ++back;
    }
    CHECK_EQ(back, 20);
    CHECK_EQ(g_tree.ahead, 0);
    /* PREV at the first page stays there; LAST is the last 48 entries,
     * whatever the page edges were. */
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_PREV), FM_OK);
    CHECK_EQ(g_tree.ahead, 0);
    CHECK_EQ(g_tree.held, FM_PAGE_MAX);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_LAST), FM_OK);
    CHECK_EQ(g_tree.ahead, 999 - FM_PAGE_MAX);
    CHECK_EQ(g_tree.held, FM_PAGE_MAX);
    CHECK(!fm_tree_has_next(&g_tree));
    /* One walk per page; the PREV past the start takes a second. */
    CHECK_EQ(g_card.calls[FK_WALK], 21 + 20 + 2 + 1);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(a_pane_started_at_a_key_reads_after_before_and_from_it)
{
    fresh("D");
    char path[FK_PATH];
    for (int i = 0; i < 100; ++i) {
        snprintf(path, sizeof(path), "D/F%03d.TXT", i);
        fk_file(&g_card, path, NULL, 1u);
    }
    fk_dir(&g_card, "D/SUB");
    fm_entry_t key;
    memset(&key, 0, sizeof(key));
    /* F047.TXT is the 49th row: SUB, then F000 to F047. */
    snprintf(key.name, sizeof(key.name), "F047.TXT");
    fm_tree_at(&g_tree, "D", &key);
    CHECK_STR_EQ(g_tree.dir, "D");
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_NEXT), FM_OK);
    CHECK_EQ(g_tree.held, FM_PAGE_MAX);
    CHECK_STR_EQ(g_tree.page[0].name, "F048.TXT");
    CHECK_EQ(g_tree.ahead, 49);
    CHECK(page_in_order());
    fm_tree_at(&g_tree, "D", &key);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_PREV), FM_OK);
    CHECK_EQ(g_tree.held, FM_PAGE_MAX);
    CHECK_STR_EQ(g_tree.page[0].name, "SUB");
    CHECK_STR_EQ(g_tree.page[FM_PAGE_MAX - 1].name, "F046.TXT");
    CHECK_EQ(g_tree.ahead, 0);
    fm_tree_at(&g_tree, "D", &key);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_SAME), FM_OK);
    CHECK_STR_EQ(g_tree.page[0].name, "F047.TXT");
    /* A key not on the card names its place: "F0475" ranks after F047. */
    snprintf(key.name, sizeof(key.name), "f0475");
    fm_tree_at(&g_tree, "D", &key);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_SAME), FM_OK);
    CHECK_STR_EQ(g_tree.page[0].name, "F048.TXT");
    /* The key's kind places it: a folder "A" ranks ahead of the folder
     * SUB, a file "A" behind every folder and ahead of every file. */
    snprintf(key.name, sizeof(key.name), "A");
    key.is_dir = true;
    fm_tree_at(&g_tree, "D", &key);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_NEXT), FM_OK);
    CHECK_STR_EQ(g_tree.page[0].name, "SUB");
    key.is_dir = false;
    fm_tree_at(&g_tree, "D", &key);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_NEXT), FM_OK);
    CHECK_STR_EQ(g_tree.page[0].name, "F000.TXT");
    fm_tree_at(&g_tree, "D", &key);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_PREV), FM_OK);
    CHECK_EQ(g_tree.held, 1);
    CHECK_STR_EQ(g_tree.page[0].name, "SUB");
    /* No key is fm_tree_init(); a key of 64 bytes is cut to the row's 63. */
    fm_tree_at(&g_tree, "D", NULL);
    CHECK_EQ(g_tree.held, 0);
    CHECK_EQ(g_tree.selected, -1);
    memset(key.name, 'A', sizeof(key.name));
    fm_tree_at(&g_tree, "D", &key);
    CHECK_EQ(strlen(g_tree.page[0].name), FM_LIST_NAME_MAX);
    fm_tree_at(NULL, "D", &key);
}

TEST_CASE(a_page_whose_files_went_shows_the_last_page)
{
    fresh("D");
    fill("D", 50);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_NEXT), FM_OK);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_NEXT), FM_OK);
    CHECK_EQ(g_tree.held, 2);
    CHECK_EQ(g_tree.ahead, 48);
    CHECK_STR_EQ(g_tree.page[0].name, "F336.TXT");
    /* The two rows deleted elsewhere: SAME finds nothing from its key on
     * and shows the folder's last page. */
    CHECK_EQ(g_io.unlink(g_io.ctx, "D/F336.TXT"), FM_OK);
    CHECK_EQ(g_io.unlink(g_io.ctx, "D/F343.TXT"), FM_OK);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_SAME), FM_OK);
    CHECK_EQ(g_tree.total, 48);
    CHECK_EQ(g_tree.held, 48);
    CHECK_EQ(g_tree.ahead, 0);
}

/*
 * SAME on the first page reads from the folder's first entry: an entry new
 * on the card that ranks before the first row comes onto the page.  On a
 * later page SAME reads from the page's first entry, and such an entry
 * counts among the ones ahead.
 */
TEST_CASE(same_on_the_first_page_takes_a_new_first_entry)
{
    fresh("D");
    fill("D", 60);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_STR_EQ(g_tree.page[0].name, "F000.TXT");
    fk_file(&g_card, "D/A.TXT", NULL, 1u);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_SAME), FM_OK);
    CHECK_EQ(g_tree.total, 61);
    CHECK_EQ(g_tree.ahead, 0);
    CHECK_EQ(g_tree.held, 48);
    CHECK_STR_EQ(g_tree.page[0].name, "A.TXT");
    CHECK_STR_EQ(g_tree.page[47].name, "F322.TXT");
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_NEXT), FM_OK);
    CHECK_EQ(g_tree.ahead, 48);
    CHECK_STR_EQ(g_tree.page[0].name, "F329.TXT");
    fk_file(&g_card, "D/B.TXT", NULL, 1u);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_SAME), FM_OK);
    CHECK_EQ(g_tree.ahead, 49);
    CHECK_STR_EQ(g_tree.page[0].name, "F329.TXT");
}

/* --------------------------------------------------------- the order */

TEST_CASE(folders_come_first_and_names_fold_their_case)
{
    fresh("");
    fk_file(&g_card, "b.txt", NULL, 1u);
    fk_dir(&g_card, "zeta");
    fk_file(&g_card, "A.TXT", NULL, 1u);
    fk_dir(&g_card, "Alpha");
    fk_file(&g_card, "C.csv", NULL, 1u);
    fk_file(&g_card, "a1.txt", NULL, 1u);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_tree.held, 6);
    static const char *const want[] = {
        "Alpha", "zeta", "A.TXT", "a1.txt", "b.txt", "C.csv",
    };
    for (int i = 0; i < 6; ++i) {
        CHECK_STR_EQ(g_tree.page[i].name, want[i]);
    }
    CHECK(g_tree.page[0].is_dir && g_tree.page[1].is_dir);
    CHECK_EQ(g_tree.page[0].size, 0);
    /* Two names one with case folded, which FAT does not hold, follow by
     * their bytes: the order stays strict. */
    fm_entry_t x;
    fm_entry_t y;
    memset(&x, 0, sizeof(x));
    memset(&y, 0, sizeof(y));
    snprintf(x.name, sizeof(x.name), "ABC");
    snprintf(y.name, sizeof(y.name), "abc");
    CHECK(fm_tree_rank(&x, &y) < 0);
    CHECK(fm_tree_rank(&y, &x) > 0);
    CHECK_EQ(fm_tree_rank(&x, &x), 0);
}

TEST_CASE(names_of_63_characters_are_listed_and_of_64_counted)
{
    for (int hand = 0; hand < 2; ++hand) {
        fresh("");
        g_card.hand_long = (hand != 0);
        char n63[80] = "";
        char n64[80] = "";
        char n200[220] = "";
        long_name(n63, 63u);
        long_name(n64, 64u);
        long_name(n200, 200u);
        fk_file(&g_card, n63, NULL, 1u);
        fk_file(&g_card, n64, NULL, 1u);
        fk_file(&g_card, n200, NULL, 1u);
        fk_file(&g_card, "SHORT.TXT", NULL, 1u);
        CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
        /* Whether the card's walk or the pane drops them, each is counted
         * once. */
        CHECK_EQ(g_tree.held, 2);
        CHECK_EQ(g_tree.total, 2);
        CHECK_EQ(g_tree.unlisted, 2);
        CHECK_STR_EQ(g_tree.page[0].name, n63);
        CHECK_EQ(strlen(g_tree.page[0].name), 63);
    }
}

TEST_CASE(dot_entries_are_no_rows)
{
    fresh("");
    fk_file(&g_card, "REAL.TXT", NULL, 1u);
    /* A card that hands over "." and ".." lists neither. */
    g_card.show_dots = true;
    fk_dir(&g_card, "..");
    fk_dir(&g_card, ".");
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_tree.held, 1);
    CHECK_EQ(g_tree.total, 1);
    CHECK_STR_EQ(g_tree.page[0].name, "REAL.TXT");
}

/* ----------------------------------------------- places, kinds, marks */

TEST_CASE(every_bench_folder_is_fixed_and_others_free)
{
    static const char *const fixed[] = {
        CARD_BENCH, CARD_AUTO_LOGS, "BENCH/ESC", CARD_ESC_CONF, "BENCH/KST",
        CARD_KST_CONF, "BENCH/HITEC", CARD_HITEC_CONF, CARD_USER,
        CARD_USER_LOGS,
    };
    for (size_t i = 0u; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
        char dir[FK_PATH];
        const char *cut = strrchr(fixed[i], '/');
        const size_t k = (cut != NULL) ? (size_t)(cut - fixed[i]) : 0u;
        memcpy(dir, fixed[i], k);
        dir[k] = '\0';
        const char *name = (cut != NULL) ? cut + 1 : fixed[i];
        CHECK_EQ(fm_place_of(NULL, dir, name, true), FM_PLACE_FIXED);
    }
    /* Case folded, as FAT finds them. */
    CHECK_EQ(fm_place_of(NULL, "bench", "kst", true), FM_PLACE_FIXED);
    CHECK_EQ(fm_place_of(NULL, "", "user", true), FM_PLACE_FIXED);
    /* Below the bench's folders, and beside them, the person's. */
    CHECK_EQ(fm_place_of(NULL, CARD_KST_CONF, "OLD", true), FM_PLACE_FREE);
    CHECK_EQ(fm_place_of(NULL, CARD_USER_LOGS, "RUNS", true), FM_PLACE_FREE);
    CHECK_EQ(fm_place_of(NULL, "", "USERS", true), FM_PLACE_FREE);
    CHECK_EQ(fm_place_of(NULL, "", "BENCHX", true), FM_PLACE_FREE);
    CHECK_EQ(fm_place_of(NULL, CARD_BENCH, "LOG", true), FM_PLACE_FREE);
    /* A file named as a bench folder is a file. */
    CHECK_EQ(fm_place_of(NULL, "", "BENCH", false), FM_PLACE_FREE);
    CHECK_EQ(fm_place_of(NULL, "", "", true), FM_PLACE_FREE);
    CHECK_EQ(fm_place_of(NULL, "", NULL, true), FM_PLACE_FREE);
    /* The pane carries the place per row. */
    fresh("");
    fk_dir(&g_card, "BENCH");
    fk_dir(&g_card, "MINE");
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_tree.page[0].place, FM_PLACE_FIXED);
    CHECK_EQ(g_tree.page[1].place, FM_PLACE_FREE);
}

TEST_CASE(the_two_log_folders_and_their_parents_are_fixed)
{
    const fm_bench_t b = {
        .user_logs = "data/runs", .auto_logs = "AUTO",
    };
    CHECK_EQ(fm_place_of(&b, "", "DATA", true), FM_PLACE_FIXED);
    CHECK_EQ(fm_place_of(&b, "DATA", "RUNS", true), FM_PLACE_FIXED);
    CHECK_EQ(fm_place_of(&b, "", "AUTO", true), FM_PLACE_FIXED);
    CHECK_EQ(fm_place_of(&b, "DATA", "OTHER", true), FM_PLACE_FREE);
    CHECK_EQ(fm_place_of(&b, "DATA/RUNS", "X", true), FM_PLACE_FREE);
    /* A stored folder the logger would not write to is the default. */
    const fm_bench_t bad = { .user_logs = "A/B/C", .auto_logs = "BENCH/KST" };
    CHECK_EQ(fm_place_of(&bad, "", "A", true), FM_PLACE_FREE);
    CHECK_EQ(fm_place_of(&bad, CARD_USER, "LOGS", true), FM_PLACE_FIXED);
}

TEST_CASE(the_journal_is_locked_and_the_fault_log_is_its_own)
{
    CHECK_EQ(fm_place_of(NULL, CARD_KST_CONF, "KST.TXT", false),
             FM_PLACE_LOCKED);
    CHECK_EQ(fm_place_of(NULL, "bench/kst/conf", "kst.new", false),
             FM_PLACE_LOCKED);
    CHECK_EQ(fm_place_of(NULL, CARD_KST_CONF, "KST1.TXT", false),
             FM_PLACE_FREE);
    CHECK_EQ(fm_place_of(NULL, CARD_USER_LOGS, "KST.TXT", false),
             FM_PLACE_FREE);
    /* The Hitec programmer's record, by HITEC_CARD_RECORD's name. */
    CHECK_EQ(fm_place_of(NULL, CARD_HITEC_CONF, HITEC_CARD_RECORD ".TXT",
                         false), FM_PLACE_LOCKED);
    CHECK_EQ(fm_place_of(NULL, "bench/hitec/conf", "hitec.new", false),
             FM_PLACE_LOCKED);
    CHECK_EQ(fm_place_of(NULL, CARD_HITEC_CONF, "HITEC1.TXT", false),
             FM_PLACE_FREE);
    CHECK_EQ(fm_place_of(NULL, CARD_KST_CONF, "HITEC.TXT", false),
             FM_PLACE_FREE);
    CHECK_EQ(fm_place_of(NULL, CARD_BENCH, "RCBENCH.LOG", false),
             FM_PLACE_FAULT);
    CHECK_EQ(fm_place_of(NULL, CARD_BENCH, "rcbench.old", false),
             FM_PLACE_FAULT);
    CHECK_EQ(fm_place_of(NULL, CARD_BENCH, "RCBENCH.TMP", false),
             FM_PLACE_FAULT);
    /* Firmware 0.21.0 and older wrote one in the root: the person's. */
    CHECK_EQ(fm_place_of(NULL, "", "RCBENCH.LOG", false), FM_PLACE_FREE);
    fresh(CARD_KST_CONF);
    fk_file(&g_card, "BENCH/KST/CONF/KST.TXT", NULL, 10u);
    fk_file(&g_card, "BENCH/KST/CONF/SERVO1.TXT", NULL, 10u);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_tree.held, 2);
    CHECK_EQ(g_tree.page[0].place, FM_PLACE_LOCKED);
    CHECK_EQ(g_tree.page[0].kind, FM_KIND_KST);
    CHECK_EQ(g_tree.page[1].place, FM_PLACE_FREE);
}

TEST_CASE(every_kind_has_its_name)
{
    CHECK_EQ(fm_kind_of("", "X", true), FM_KIND_FOLDER);
    CHECK_EQ(fm_kind_of(CARD_USER_LOGS, "ATEST042.CSV", false), FM_KIND_RUN);
    CHECK_EQ(fm_kind_of(CARD_AUTO_LOGS, "servo007.csv", false), FM_KIND_RUN);
    CHECK_EQ(fm_kind_of(CARD_USER_LOGS, "ATEST042.TXT", false),
             FM_KIND_REPORT);
    CHECK_EQ(fm_kind_of(CARD_KST_CONF, "MY SERVO.TXT", false), FM_KIND_KST);
    CHECK_EQ(fm_kind_of("", "NOTES.TXT", false), FM_KIND_TEXT);
    CHECK_EQ(fm_kind_of(CARD_ESC_CONF, "yge.json", false), FM_KIND_PROFILE);
    CHECK_EQ(fm_kind_of("", "yge.json", false), FM_KIND_JSON);
    CHECK_EQ(fm_kind_of(CARD_BENCH, "RCBENCH.OLD", false), FM_KIND_FAULT);
    CHECK_EQ(fm_kind_of("", "sweep.csv", false), FM_KIND_CSV);
    CHECK_EQ(fm_kind_of("", ".csv", false), FM_KIND_OTHER);
    CHECK_EQ(fm_kind_of("", "IMAGE.BIN", false), FM_KIND_OTHER);
    CHECK_EQ(fm_kind_of("", NULL, false), FM_KIND_OTHER);
    CHECK_EQ(fm_kind_of(NULL, "A.TXT", false), FM_KIND_TEXT);
}

TEST_CASE(a_file_the_logger_writes_is_marked_busy)
{
    fresh(CARD_AUTO_LOGS);
    fk_file(&g_card, "BENCH/LOGS/SERVO005.CSV", NULL, 10u);
    fk_file(&g_card, "BENCH/LOGS/SERVO004.CSV", NULL, 10u);
    const log_marks_t marks = {
        .run_dir = CARD_AUTO_LOGS, .run = 5u, .test_dir = CARD_USER_LOGS,
    };
    const fm_bench_t b = { .marks = &marks };
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, &b, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_tree.held, 2);
    CHECK(!g_tree.page[0].busy);   /* SERVO004.CSV */
    CHECK(g_tree.page[1].busy);    /* SERVO005.CSV */
    /* The same number in the other folder is not that run. */
    CHECK(!fm_busy(&b, CARD_USER_LOGS, "SERVO005.CSV"));
    CHECK(!fm_busy(NULL, CARD_AUTO_LOGS, "SERVO005.CSV"));
}

/* ------------------------------------------------- entering and leaving */

TEST_CASE(enter_and_up_walk_the_tree_and_stop_at_the_root)
{
    fresh("");
    fk_file(&g_card, "USER/LOGS/ATEST001.CSV", NULL, 5u);
    fk_file(&g_card, "TOP.TXT", NULL, 5u);
    CHECK_EQ(fm_tree_up(&g_tree, &g_io, NULL), FM_ERR_ARG);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_tree.held, 2);
    /* A file is not entered; nor a row off the page. */
    CHECK_EQ(fm_tree_enter(&g_tree, &g_io, NULL, 1), FM_ERR_ARG);
    CHECK_EQ(fm_tree_enter(&g_tree, &g_io, NULL, 2), FM_ERR_ARG);
    CHECK_EQ(fm_tree_enter(&g_tree, &g_io, NULL, -1), FM_ERR_ARG);
    CHECK_EQ(fm_tree_enter(&g_tree, &g_io, NULL, 0), FM_OK);
    CHECK_STR_EQ(g_tree.dir, "USER");
    CHECK_EQ(fm_tree_enter(&g_tree, &g_io, NULL, 0), FM_OK);
    CHECK_STR_EQ(g_tree.dir, "USER/LOGS");
    CHECK_EQ(g_tree.held, 1);
    CHECK(fm_tree_selected(&g_tree) == NULL);
    /* Up comes back with the folder come from selected. */
    CHECK_EQ(fm_tree_up(&g_tree, &g_io, NULL), FM_OK);
    CHECK_STR_EQ(g_tree.dir, "USER");
    CHECK(fm_tree_selected(&g_tree) != NULL);
    CHECK_STR_EQ(fm_tree_selected(&g_tree)->name, "LOGS");
    CHECK_EQ(fm_tree_up(&g_tree, &g_io, NULL), FM_OK);
    CHECK_STR_EQ(g_tree.dir, "");
    CHECK_STR_EQ(fm_tree_selected(&g_tree)->name, "USER");
    CHECK_EQ(fm_tree_up(&g_tree, &g_io, NULL), FM_ERR_ARG);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(a_folder_gone_before_entering_leaves_the_pane_where_it_was)
{
    fresh("");
    fk_dir(&g_card, "GONE");
    fk_file(&g_card, "STAY.TXT", NULL, 5u);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(fm_tree_select(&g_tree, 0), FM_OK);
    CHECK_EQ(g_io.rmdir(g_io.ctx, "GONE"), FM_OK);
    CHECK_EQ(fm_tree_enter(&g_tree, &g_io, NULL, 0), FM_ERR_NOT_FOUND);
    CHECK_STR_EQ(g_tree.dir, "");
    CHECK_EQ(g_tree.held, 1);
    CHECK_STR_EQ(g_tree.page[0].name, "STAY.TXT");
    CHECK(fm_tree_selected(&g_tree) == NULL);
}

TEST_CASE(a_folder_not_made_yet_is_an_empty_page)
{
    fresh(CARD_USER_LOGS);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST),
             FM_ERR_NOT_FOUND);
    CHECK_EQ(g_tree.held, 0);
    CHECK_EQ(g_tree.total, 0);
}

TEST_CASE(a_path_past_120_bytes_or_16_folders_is_not_entered)
{
    fresh("");
    /* 2 folders of 50 characters: 101 bytes; a folder of 30 below them
     * makes 132. */
    char dir[FM_PATH_SIZE];
    memset(dir, 'D', 101u);
    dir[50] = '/';
    dir[101] = '\0';
    char deep[FK_PATH];
    snprintf(deep, sizeof(deep), "%s/%s", dir,
             "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEE");
    fk_dir(&g_card, deep);
    fm_tree_init(&g_tree, dir);
    CHECK_STR_EQ(g_tree.dir, dir);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_tree.held, 1);
    CHECK_EQ(fm_tree_enter(&g_tree, &g_io, NULL, 0), FM_ERR_NAME);
    CHECK_STR_EQ(g_tree.dir, dir);
    /* 16 folders deep is the deepest. */
    fresh("");
    char d16[FK_PATH] = "A";
    for (int i = 1; i < 16; ++i) {
        strcat(d16, "/A");
    }
    char d17[FK_PATH];
    memcpy(d17, d16, strlen(d16) + 1u);
    strcat(d17, "/A");
    fk_dir(&g_card, d17);
    fm_tree_init(&g_tree, d16);
    CHECK_STR_EQ(g_tree.dir, d16);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_tree.held, 1);
    CHECK_EQ(fm_tree_enter(&g_tree, &g_io, NULL, 0), FM_ERR_NAME);
    fm_tree_init(&g_tree, d17);
    CHECK_STR_EQ(g_tree.dir, "");
    /* A start folder fm_path_check() refuses is the root. */
    char huge[FM_PATH_SIZE + 1];
    memset(huge, 'H', sizeof(huge) - 1u);
    huge[sizeof(huge) - 1u] = '\0';
    fm_tree_init(&g_tree, huge);
    CHECK_STR_EQ(g_tree.dir, "");
    fm_tree_init(&g_tree, "/USER");
    CHECK_STR_EQ(g_tree.dir, "");
    fm_tree_init(&g_tree, NULL);
    CHECK_STR_EQ(g_tree.dir, "");
}

TEST_CASE(the_selection_stays_on_its_name_and_goes_with_it)
{
    fresh("");
    fk_file(&g_card, "A.TXT", NULL, 1u);
    fk_file(&g_card, "B.TXT", NULL, 1u);
    fk_file(&g_card, "C.TXT", NULL, 1u);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(fm_tree_select(&g_tree, 2), FM_OK);
    CHECK_EQ(fm_tree_select(&g_tree, 3), FM_ERR_ARG);
    CHECK_EQ(fm_tree_select(&g_tree, -2), FM_ERR_ARG);
    CHECK_EQ(fm_tree_select(NULL, 0), FM_ERR_ARG);
    CHECK_EQ(g_io.unlink(g_io.ctx, "A.TXT"), FM_OK);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_SAME), FM_OK);
    CHECK_EQ(g_tree.held, 2);
    CHECK_EQ(g_tree.selected, 1);
    CHECK_STR_EQ(fm_tree_selected(&g_tree)->name, "C.TXT");
    CHECK_EQ(g_io.unlink(g_io.ctx, "C.TXT"), FM_OK);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_SAME), FM_OK);
    CHECK(fm_tree_selected(&g_tree) == NULL);
    CHECK_EQ(fm_tree_select(&g_tree, -1), FM_OK);
}

/* ---------------------------------------------------------- the card */

TEST_CASE(a_card_gone_during_a_walk_is_walked_again_once)
{
    fresh("D");
    fill("D", 10);
    g_card.gone_at_entry = 3;
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_card.calls[FK_WALK], 2);
    CHECK_EQ(g_tree.held, 10);
    CHECK_EQ(g_tree.total, 10);
    CHECK_EQ(g_tree.mount, 2u);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(a_card_that_fails_or_goes_at_both_walks_is_no_page)
{
    fresh("D");
    fill("D", 10);
    fk_fail(&g_card, FK_WALK, 1, FM_ERR_IO);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_ERR_IO);
    CHECK_EQ(g_tree.held, 0);
    CHECK_EQ(g_tree.total, -1);
    /* Found gone, and the slot holds no card for the second walk. */
    fk_fail(&g_card, FK_WALK, 1, FM_ERR_CARD_GONE);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_ERR_NO_CARD);
    CHECK_EQ(g_tree.total, -1);
    fk_insert(&g_card);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_tree.held, 10);
    /* A hold refused once: walked on the second try. */
    fk_fail(&g_card, FK_HOLD, 1, FM_ERR_IO);
    const long walks = g_card.calls[FK_WALK];
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(g_card.calls[FK_WALK], walks + 1);
    /* Gone at both walks. */
    g_card.gone_at_entry = 0;
    fk_fail(&g_card, FK_HOLD, 2, FM_ERR_IO);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST),
             FM_ERR_CARD_GONE);
    CHECK_EQ(g_tree.total, -1);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(no_card_is_no_page_and_null_is_refused)
{
    fresh("");
    fk_pull(&g_card);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_ERR_NO_CARD);
    CHECK_EQ(g_tree.total, -1);
    CHECK_EQ(fm_tree_page(NULL, &g_io, NULL, FM_GO_FIRST), FM_ERR_ARG);
    CHECK_EQ(fm_tree_page(&g_tree, NULL, NULL, FM_GO_FIRST), FM_ERR_ARG);
    CHECK_EQ(fm_tree_enter(NULL, &g_io, NULL, 0), FM_ERR_ARG);
    CHECK_EQ(fm_tree_up(NULL, &g_io, NULL), FM_ERR_ARG);
    CHECK(fm_tree_selected(NULL) == NULL);
    CHECK(!fm_tree_has_prev(NULL));
    CHECK(!fm_tree_has_next(NULL));
    fm_tree_init(NULL, "");
}

/*
 * What a page tells of a file: on it, not there, or on another page.  100
 * files F000.TXT to F693.TXT, every 7th number: page 1 runs F000 to F329,
 * page 2 F336 to F665, and the last page, the last 48, F364 to F693.
 */
TEST_CASE(a_page_tells_a_file_there_not_there_or_on_another_page)
{
    fresh("D");
    fill("D", 100);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(fm_tree_has_file(&g_tree, "F007.TXT"), FM_HAS_YES);
    CHECK_EQ(fm_tree_has_file(&g_tree, "f007.txt"), FM_HAS_YES);
    /* Inside the page's span and not on it. */
    CHECK_EQ(fm_tree_has_file(&g_tree, "F009.TXT"), FM_HAS_NO);
    /* The last row, and one past it with a page after. */
    CHECK_EQ(fm_tree_has_file(&g_tree, "F329.TXT"), FM_HAS_YES);
    CHECK_EQ(fm_tree_has_file(&g_tree, "F330.TXT"), FM_HAS_UNKNOWN);
    CHECK_EQ(fm_tree_has_file(&g_tree, "F336.TXT"), FM_HAS_UNKNOWN);
    /* Page 2: before its first row with a page ahead. */
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_NEXT), FM_OK);
    CHECK_EQ(fm_tree_has_file(&g_tree, "F335.TXT"), FM_HAS_UNKNOWN);
    CHECK_EQ(fm_tree_has_file(&g_tree, "F336.TXT"), FM_HAS_YES);
    CHECK_EQ(fm_tree_has_file(&g_tree, "F337.TXT"), FM_HAS_NO);
    /* The last page: nothing after it. */
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_LAST), FM_OK);
    CHECK_EQ(fm_tree_has_file(&g_tree, "F693.TXT"), FM_HAS_YES);
    CHECK_EQ(fm_tree_has_file(&g_tree, "F999.TXT"), FM_HAS_NO);
    CHECK_EQ(fm_tree_has_file(&g_tree, "F364.TXT"), FM_HAS_YES);
    CHECK_EQ(fm_tree_has_file(&g_tree, "F363.TXT"), FM_HAS_UNKNOWN);
    /* A folder of the name is no file. */
    fresh("E");
    fk_dir(&g_card, "E");
    fk_dir(&g_card, "E/ATEST001.TXT");
    fk_file(&g_card, "E/ATEST001.CSV", NULL, 10u);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(fm_tree_has_file(&g_tree, "ATEST001.TXT"), FM_HAS_NO);
    CHECK_EQ(fm_tree_has_file(&g_tree, "ATEST001.CSV"), FM_HAS_YES);
    /* An empty folder holds nothing. */
    fresh("EMPTY");
    fk_dir(&g_card, "EMPTY");
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(fm_tree_has_file(&g_tree, "F000.TXT"), FM_HAS_NO);
    /* No card, no name, no pane. */
    fk_pull(&g_card);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_ERR_NO_CARD);
    CHECK_EQ(fm_tree_has_file(&g_tree, "F000.TXT"), FM_HAS_UNKNOWN);
    CHECK_EQ(fm_tree_has_file(&g_tree, ""), FM_HAS_UNKNOWN);
    CHECK_EQ(fm_tree_has_file(&g_tree, NULL), FM_HAS_UNKNOWN);
    CHECK_EQ(fm_tree_has_file(NULL, "F000.TXT"), FM_HAS_UNKNOWN);
    /* A page whose row count is out of its bounds tells nothing. */
    fresh("E");
    fk_dir(&g_card, "E");
    fk_file(&g_card, "E/A.TXT", NULL, 1u);
    CHECK_EQ(fm_tree_page(&g_tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(fm_tree_has_file(&g_tree, "A.TXT"), FM_HAS_YES);
    g_tree.held = FM_PAGE_MAX + 1;
    CHECK_EQ(fm_tree_has_file(&g_tree, "A.TXT"), FM_HAS_UNKNOWN);
    g_tree.held = -1;
    CHECK_EQ(fm_tree_has_file(&g_tree, "A.TXT"), FM_HAS_UNKNOWN);
}

/* --------------------------------------------------- the page keeping */

/**
 * @brief The order of two ints, as fm_select takes it, with answers far
 *        from 1 so the keeping cannot rely on their size.
 * @param a  an int
 * @param b  an int
 * @return 2e9, -2e9 or 0
 */
static int by_int(const void *a, const void *b)
{
    const int x = *(const int *)a;
    const int y = *(const int *)b;
    return (x > y) ? 2000000000 : (x < y) ? -2000000000 : 0;
}

TEST_CASE(the_page_keeping_takes_any_entry_size_and_null_edges)
{
    int set[4];
    int held = 0;
    static const int in[] = { 9, 3, 7, 1, 8, 2 };
    for (size_t i = 0u; i < sizeof(in) / sizeof(in[0]); ++i) {
        held = fm_select_keep(set, sizeof(set[0]), held, 4, &in[i], by_int);
    }
    CHECK_EQ(held, 4);
    CHECK_EQ(set[0], 1);
    CHECK_EQ(set[3], 7);
    CHECK_EQ(fm_select_keep(NULL, sizeof(int), 2, 4, &in[0], by_int), 2);
    CHECK_EQ(fm_select_keep(set, 0u, 2, 4, &in[0], by_int), 2);
    CHECK_EQ(fm_select_keep(set, sizeof(int), 2, 4, NULL, by_int), 2);
    CHECK_EQ(fm_select_keep(set, sizeof(int), 2, 4, &in[0], NULL), 2);
    CHECK_EQ(fm_select_keep(set, sizeof(int), 2, 0, &in[0], by_int), 2);

    /* The page before 8: the last entries ahead of it, sorted. */
    fm_select_t sel;
    const int key = 8;
    fm_select_begin(&sel, set, sizeof(set[0]), 2, by_int, FM_PAGE_BEFORE,
                    &key);
    for (size_t i = 0u; i < sizeof(in) / sizeof(in[0]); ++i) {
        fm_select_offer(&sel, &in[i]);
    }
    int ahead = -1;
    CHECK_EQ(fm_select_end(&sel, NULL, &ahead), 2);
    CHECK_EQ(set[0], 3);
    CHECK_EQ(set[1], 7);
    CHECK_EQ(ahead, 2);

    /* Without an order there is no key: AFTER is the first page. */
    fm_select_begin(&sel, set, sizeof(set[0]), 2, NULL, FM_PAGE_AFTER, &key);
    CHECK(sel.key == NULL);
    CHECK_EQ(sel.dir, FM_PAGE_FROM);
    CHECK_EQ(sel.capacity, 0);
    fm_select_offer(&sel, &in[0]);
    CHECK_EQ(fm_select_end(&sel, NULL, NULL), 0);

    fm_select_begin(NULL, set, sizeof(set[0]), 2, by_int, FM_PAGE_FROM, NULL);
    fm_select_offer(NULL, &in[0]);
    fm_select_offer(&sel, NULL);
    ahead = -1;
    CHECK_EQ(fm_select_end(NULL, NULL, &ahead), 0);
    CHECK_EQ(ahead, 0);
    CHECK_EQ(fm_select_end(NULL, NULL, NULL), 0);
}

int main(void)
{
    RUN(an_empty_folder_is_an_empty_page);
    RUN(one_entry_is_one_row);
    RUN(entries_47_48_and_49_fill_one_page_and_spill_the_49th);
    RUN(entries_999_page_forward_and_back_to_every_edge);
    RUN(a_pane_started_at_a_key_reads_after_before_and_from_it);
    RUN(a_page_whose_files_went_shows_the_last_page);
    RUN(same_on_the_first_page_takes_a_new_first_entry);
    RUN(folders_come_first_and_names_fold_their_case);
    RUN(names_of_63_characters_are_listed_and_of_64_counted);
    RUN(dot_entries_are_no_rows);
    RUN(every_bench_folder_is_fixed_and_others_free);
    RUN(the_two_log_folders_and_their_parents_are_fixed);
    RUN(the_journal_is_locked_and_the_fault_log_is_its_own);
    RUN(every_kind_has_its_name);
    RUN(a_file_the_logger_writes_is_marked_busy);
    RUN(enter_and_up_walk_the_tree_and_stop_at_the_root);
    RUN(a_folder_gone_before_entering_leaves_the_pane_where_it_was);
    RUN(a_folder_not_made_yet_is_an_empty_page);
    RUN(a_path_past_120_bytes_or_16_folders_is_not_entered);
    RUN(the_selection_stays_on_its_name_and_goes_with_it);
    RUN(a_card_gone_during_a_walk_is_walked_again_once);
    RUN(a_card_that_fails_or_goes_at_both_walks_is_no_page);
    RUN(no_card_is_no_page_and_null_is_refused);
    RUN(a_page_tells_a_file_there_not_there_or_on_another_page);
    RUN(the_page_keeping_takes_any_entry_size_and_null_edges);
    fk_free(&g_card);
    return test_summary("fm_tree");
}
