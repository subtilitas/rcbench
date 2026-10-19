/*
 * Making a folder of the card one level at a time, card_make_dirs(), over a
 * card held in memory: every level made in order, a level that is there
 * already, a file holding a level's name, a card that takes no folder, and
 * the paths of the bench's folders.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "kst_store.h"

/* ------------------------------------------------------------ the card */

#define FAKE_MAX 16

typedef struct {
    char       dirs[FAKE_MAX][CARD_DIR_MAX];
    int        n_dirs;
    char       files[FAKE_MAX][CARD_DIR_MAX];
    int        n_files;
    bool       protect;           /* write-protected: no new folder      */
    char       calls[FAKE_MAX][CARD_DIR_MAX];
    int        n_calls;
} fake_t;

static fake_t g;

static bool has(char (*list)[CARD_DIR_MAX], int n, const char *path)
{
    for (int i = 0; i < n; ++i) {
        if (strcmp(list[i], path) == 0) {
            return true;
        }
    }
    return false;
}

/* As the panel's panel_card_make_dir() answers: a folder made or found, a
 * file of that name, or a card that refuses. */
static card_dir_t fake_make(void *ctx, const char *level)
{
    (void)ctx;
    if (g.n_calls < FAKE_MAX) {
        snprintf(g.calls[g.n_calls++], CARD_DIR_MAX, "%s", level);
    }
    if (has(g.dirs, g.n_dirs, level)) {
        return CARD_DIR_OK;
    }
    if (has(g.files, g.n_files, level)) {
        return CARD_DIR_FILE;
    }
    if (g.protect) {
        return CARD_DIR_DENIED;
    }
    if (g.n_dirs >= FAKE_MAX) {
        return CARD_DIR_FAILED;
    }
    snprintf(g.dirs[g.n_dirs++], CARD_DIR_MAX, "%s", level);
    return CARD_DIR_OK;
}

static void reset(void)
{
    memset(&g, 0, sizeof(g));
}

/* ------------------------------------------------------------ the cases */

TEST_CASE(each_level_is_made_shortest_first)
{
    reset();
    CHECK_EQ(card_make_dirs(CARD_KST_CONF, fake_make, &g), CARD_DIR_OK);
    CHECK_EQ(g.n_calls, 3);
    CHECK_STR_EQ(g.calls[0], "BENCH");
    CHECK_STR_EQ(g.calls[1], "BENCH/KST");
    CHECK_STR_EQ(g.calls[2], "BENCH/KST/CONF");
    CHECK_EQ(g.n_dirs, 3);
}

TEST_CASE(a_folder_that_is_there_is_no_error)
{
    reset();
    CHECK_EQ(card_make_dirs(CARD_ESC_CONF, fake_make, &g), CARD_DIR_OK);
    /* The second time every level is there: asked, and none made. */
    g.n_calls = 0;
    CHECK_EQ(card_make_dirs(CARD_ESC_CONF, fake_make, &g), CARD_DIR_OK);
    CHECK_EQ(g.n_calls, 3);
    CHECK_EQ(g.n_dirs, 3);
    /* A sibling shares BENCH and makes its own two. */
    CHECK_EQ(card_make_dirs(CARD_KST_CONF, fake_make, &g), CARD_DIR_OK);
    CHECK_EQ(g.n_dirs, 5);
}

TEST_CASE(a_file_named_bench_stops_at_the_first_level)
{
    reset();
    snprintf(g.files[g.n_files++], CARD_DIR_MAX, "%s", CARD_BENCH);
    CHECK_EQ(card_make_dirs(CARD_KST_CONF, fake_make, &g), CARD_DIR_FILE);
    CHECK_EQ(g.n_calls, 1);         /* nothing below BENCH is tried */
    CHECK_EQ(g.n_dirs, 0);
    /* A file at the last level: the two above are made, the answer is the
     * file's. */
    reset();
    snprintf(g.files[g.n_files++], CARD_DIR_MAX, "%s", "BENCH/KST/CONF");
    CHECK_EQ(card_make_dirs(CARD_KST_CONF, fake_make, &g), CARD_DIR_FILE);
    CHECK_EQ(g.n_dirs, 2);
}

TEST_CASE(a_write_protected_card_is_denied_and_a_made_tree_still_answers)
{
    reset();
    g.protect = true;
    CHECK_EQ(card_make_dirs(CARD_AUTO_LOGS, fake_make, &g), CARD_DIR_DENIED);
    CHECK_EQ(g.n_dirs, 0);
    /* Folders made before the card was write-protected answer as
     * folders. */
    reset();
    CHECK_EQ(card_make_dirs(CARD_KST_CONF, fake_make, &g), CARD_DIR_OK);
    g.protect = true;
    CHECK_EQ(card_make_dirs(CARD_KST_CONF, fake_make, &g), CARD_DIR_OK);
    CHECK_EQ(card_make_dirs(CARD_USER_LOGS, fake_make, &g), CARD_DIR_DENIED);
}

TEST_CASE(the_root_makes_nothing_and_bad_arguments_make_nothing)
{
    reset();
    CHECK_EQ(card_make_dirs(CARD_ROOT, fake_make, &g), CARD_DIR_OK);
    CHECK_EQ(g.n_calls, 0);
    CHECK_EQ(card_make_dirs(NULL, fake_make, &g), CARD_DIR_ARG);
    CHECK_EQ(card_make_dirs(CARD_BENCH, NULL, &g), CARD_DIR_ARG);
    /* 23 characters fit CARD_DIR_MAX (24) with the terminator, 24 do
     * not. */
    char dir[CARD_DIR_MAX + 1];
    memset(dir, 'A', sizeof(dir));
    dir[8] = '/';
    dir[17] = '/';
    dir[CARD_DIR_MAX - 1] = '\0';
    CHECK_EQ(card_make_dirs(dir, fake_make, &g), CARD_DIR_OK);
    CHECK_EQ(g.n_calls, 3);
    dir[CARD_DIR_MAX - 1] = 'A';
    dir[CARD_DIR_MAX] = '\0';
    g.n_calls = 0;
    CHECK_EQ(card_make_dirs(dir, fake_make, &g), CARD_DIR_ARG);
    CHECK_EQ(g.n_calls, 0);
}

TEST_CASE(a_full_card_fails_where_it_fills)
{
    reset();
    g.n_dirs = FAKE_MAX - 1;   /* room for one more folder */
    CHECK_EQ(card_make_dirs(CARD_HITEC_CONF, fake_make, &g),
             CARD_DIR_FAILED);
    CHECK_EQ(g.n_calls, 2);
}

TEST_CASE(the_bench_files_are_under_bench)
{
    CHECK_STR_EQ(CARD_ESC_CONF, "BENCH/ESC/CONF");
    CHECK_STR_EQ(CARD_KST_CONF, "BENCH/KST/CONF");
    CHECK_STR_EQ(CARD_HITEC_CONF, "BENCH/HITEC/CONF");
    CHECK_STR_EQ(CARD_FAULT_LOG, "BENCH/RCBENCH.LOG");
    CHECK_STR_EQ(CARD_FAULT_OLD, "BENCH/RCBENCH.OLD");
    /* The servo files' store is that folder, not KST of 0.21. */
    CHECK_STR_EQ(KST_STORE_DIR, CARD_KST_CONF);
    /* The write record's two paths fit the 64-byte buffers of
     * panel_card.c: "/sdcard/BENCH/KST/CONF/KST.NEW". */
    CHECK(strlen("/sdcard/" CARD_KST_CONF "/KST.NEW") < 64u);
}

int main(void)
{
    RUN(each_level_is_made_shortest_first);
    RUN(a_folder_that_is_there_is_no_error);
    RUN(a_file_named_bench_stops_at_the_first_level);
    RUN(a_write_protected_card_is_denied_and_a_made_tree_still_answers);
    RUN(the_root_makes_nothing_and_bad_arguments_make_nothing);
    RUN(a_full_card_fails_where_it_fills);
    RUN(the_bench_files_are_under_bench);
    return test_summary("card_tree");
}
