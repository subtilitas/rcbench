/*
 * Making a folder of the card one level at a time, card_make_dirs(), over a
 * card held in memory: every level made in order, a level that is there
 * already, a file holding a level's name, a card that takes no folder, and
 * the paths of the bench's folders.  And the log folders a user can set,
 * card_folder_check(): every refusal at both edges of its limit.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"

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
    CHECK_STR_EQ(CARD_ESCW_CONF, "BENCH/ESCW/CONF");
    CHECK_STR_EQ(CARD_FAULT_LOG, "BENCH/RCBENCH.LOG");
    CHECK_STR_EQ(CARD_FAULT_OLD, "BENCH/RCBENCH.OLD");
    /* The write record's two paths fit the 64-byte buffers of
     * panel_card.c: "/sdcard/BENCH/KST/CONF/KST.NEW". */
    CHECK(strlen("/sdcard/" CARD_KST_CONF "/KST.NEW") < 64u);
}

/* ------------------------------------------------- the log folders */

/* What card_folder_check() says of @p in, with out filled on OK. */
static card_folder_t check(const char *in, char *out)
{
    strcpy(out, "UNTOUCHED");
    return card_folder_check(in, out, CARD_DIR_MAX);
}

TEST_CASE(the_defaults_are_taken)
{
    char out[CARD_DIR_MAX];
    CHECK_EQ(check(CARD_AUTO_LOGS, out), CARD_FOLDER_OK);
    CHECK_STR_EQ(out, "BENCH/LOGS");
    CHECK_EQ(check(CARD_USER_LOGS, out), CARD_FOLDER_OK);
    CHECK_STR_EQ(out, "USER/LOGS");
    /* One folder, and the bench's own top folder. */
    CHECK_EQ(check("LOGS", out), CARD_FOLDER_OK);
    CHECK_STR_EQ(out, "LOGS");
    CHECK_EQ(check("BENCH", out), CARD_FOLDER_OK);
    CHECK_EQ(check("A", out), CARD_FOLDER_OK);
    CHECK_STR_EQ(out, "A");
}

TEST_CASE(an_empty_folder_is_refused_wherever_it_is)
{
    char out[CARD_DIR_MAX];
    CHECK_EQ(check("", out), CARD_FOLDER_EMPTY);
    CHECK_STR_EQ(out, "UNTOUCHED");
    CHECK_EQ(check("/", out), CARD_FOLDER_EMPTY);
    CHECK_EQ(check("/LOGS", out), CARD_FOLDER_EMPTY);
    CHECK_EQ(check("LOGS/", out), CARD_FOLDER_EMPTY);
    CHECK_EQ(check("A//B", out), CARD_FOLDER_EMPTY);
    CHECK_EQ(check("//", out), CARD_FOLDER_EMPTY);
    CHECK_STR_EQ(out, "UNTOUCHED");
}

TEST_CASE(a_folder_takes_8_characters_and_not_9)
{
    char out[CARD_DIR_MAX];
    CHECK_EQ(check("BENCHLOG", out), CARD_FOLDER_OK);
    CHECK_STR_EQ(out, "BENCHLOG");
    CHECK_EQ(check("BENCHLOGS", out), CARD_FOLDER_LONG);
    CHECK_STR_EQ(out, "UNTOUCHED");
    /* In the second place too, and at the longest path: 8, "/", 8. */
    CHECK_EQ(check("ABCDEFGH/12345678", out), CARD_FOLDER_OK);
    CHECK_STR_EQ(out, "ABCDEFGH/12345678");
    CHECK_EQ(strlen(out), 17u);
    CHECK_EQ(check("ABCDEFGH/123456789", out), CARD_FOLDER_LONG);
    CHECK_EQ(check("ABCDEFGHI/1", out), CARD_FOLDER_LONG);
    /* The keyboard's whole 23 characters, in one folder and in two. */
    CHECK_EQ(check("ABCDEFGHIJKLMNOPQRSTUVW", out), CARD_FOLDER_LONG);
    CHECK_EQ(check("ABCDEFGH/ABCDEFGHIJKLMN", out), CARD_FOLDER_LONG);
    CHECK_EQ(check("ABCDEFG/ABCDEFG/ABCDEFG", out), CARD_FOLDER_DEEP);
    CHECK_STR_EQ(out, "UNTOUCHED");
}

TEST_CASE(a_path_goes_2_folders_deep_and_not_3)
{
    char out[CARD_DIR_MAX];
    CHECK_EQ(check("A/B", out), CARD_FOLDER_OK);
    CHECK_STR_EQ(out, "A/B");
    CHECK_EQ(check("A/B/C", out), CARD_FOLDER_DEEP);
    CHECK_EQ(check("A/B/", out), CARD_FOLDER_DEEP);
    CHECK_STR_EQ(out, "UNTOUCHED");
}

TEST_CASE(a_character_outside_the_set_is_refused)
{
    char out[CARD_DIR_MAX];
    CHECK_EQ(check("MY LOGS", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check(" LOGS", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check("LOGS ", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check("LOGS.OLD", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check(".", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check("..", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check("A\\B", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check("LOG*", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check("A/\xc3\x9c", out), CARD_FOLDER_CHAR);
    CHECK_STR_EQ(out, "UNTOUCHED");
    /* Every character of the set, at both ends of its ranges. */
    CHECK_EQ(check("AZ09_-/Z_-9A0", out), CARD_FOLDER_OK);
    CHECK_STR_EQ(out, "AZ09_-/Z_-9A0");
    CHECK_EQ(check("@", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check("[", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check("`", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check("{", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check(":", out), CARD_FOLDER_CHAR);
}

TEST_CASE(lower_case_is_taken_as_upper_case)
{
    char out[CARD_DIR_MAX];
    CHECK_EQ(check("bench/logs", out), CARD_FOLDER_OK);
    CHECK_STR_EQ(out, "BENCH/LOGS");
    CHECK_EQ(check("My_Log-2", out), CARD_FOLDER_OK);
    CHECK_STR_EQ(out, "MY_LOG-2");
    CHECK_EQ(check("aux", out), CARD_FOLDER_RESERVED);
    CHECK_EQ(check("bench/kst", out), CARD_FOLDER_BENCH);
}

TEST_CASE(every_device_name_is_refused_in_either_place)
{
    static const char *const devices[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8",
        "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8",
        "LPT9",
    };
    char out[CARD_DIR_MAX];
    char in[CARD_DIR_MAX];
    for (size_t i = 0u; i < sizeof(devices) / sizeof(devices[0]); ++i) {
        CHECK_EQ(check(devices[i], out), CARD_FOLDER_RESERVED);
        snprintf(in, sizeof(in), "%s/LOGS", devices[i]);
        CHECK_EQ(check(in, out), CARD_FOLDER_RESERVED);
        snprintf(in, sizeof(in), "LOGS/%s", devices[i]);
        CHECK_EQ(check(in, out), CARD_FOLDER_RESERVED);
    }
    CHECK_STR_EQ(out, "UNTOUCHED");
    /* A name that starts like one is a name. */
    static const char *const names[] = {
        "COM0", "LPT0", "COM10", "CONS", "AUXL", "NULL", "PRN1", "CO",
        "COM", "LPT", "AU", "XAUX",
    };
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
        CHECK_EQ(check(names[i], out), CARD_FOLDER_OK);
        CHECK_STR_EQ(out, names[i]);
    }
}

TEST_CASE(the_benchs_own_folders_are_refused)
{
    char out[CARD_DIR_MAX];
    CHECK_EQ(check("BENCH/ESC", out), CARD_FOLDER_BENCH);
    CHECK_EQ(check("BENCH/KST", out), CARD_FOLDER_BENCH);
    CHECK_EQ(check("BENCH/HITEC", out), CARD_FOLDER_BENCH);
    CHECK_EQ(check("BENCH/ESCW", out), CARD_FOLDER_BENCH);
    CHECK_STR_EQ(out, "UNTOUCHED");
    /* Below them is 3 deep, refused before the bench is asked. */
    CHECK_EQ(check("BENCH/KST/CONF", out), CARD_FOLDER_DEEP);
    /* A name that starts like one is another folder. */
    CHECK_EQ(check("BENCH/KSTX", out), CARD_FOLDER_OK);
    CHECK_STR_EQ(out, "BENCH/KSTX");
    CHECK_EQ(check("BENCH/ES", out), CARD_FOLDER_OK);
    CHECK_EQ(check("BENCH/HITECS", out), CARD_FOLDER_OK);
    CHECK_EQ(check("BENCH/ESCWX", out), CARD_FOLDER_OK);
    CHECK_EQ(check("BENCHX/KST", out), CARD_FOLDER_OK);
    CHECK_EQ(check("KST", out), CARD_FOLDER_OK);
    CHECK_EQ(check("USER/KST", out), CARD_FOLDER_OK);
}

TEST_CASE(the_first_fault_from_the_left_answers)
{
    char out[CARD_DIR_MAX];
    CHECK_EQ(check("A B/C/D", out), CARD_FOLDER_CHAR);
    CHECK_EQ(check("ABCDEFGHI/A B", out), CARD_FOLDER_LONG);
    CHECK_EQ(check("AUX/A B", out), CARD_FOLDER_RESERVED);
    CHECK_EQ(check("A/B/C D", out), CARD_FOLDER_DEEP);
    CHECK_EQ(check("/A B", out), CARD_FOLDER_EMPTY);
}

TEST_CASE(a_short_out_and_null_arguments_are_refused)
{
    char out[CARD_DIR_MAX];
    CHECK_EQ(card_folder_check(NULL, out, sizeof(out)), CARD_FOLDER_ARG);
    /* No out: only the answer. */
    CHECK_EQ(card_folder_check("LOGS", NULL, 0u), CARD_FOLDER_OK);
    CHECK_EQ(card_folder_check("AUX", NULL, 0u), CARD_FOLDER_RESERVED);
    /* "LOGS" and its terminator are 5 bytes: 4 is too short. */
    strcpy(out, "UNTOUCHED");
    CHECK_EQ(card_folder_check("LOGS", out, 4u), CARD_FOLDER_ARG);
    CHECK_STR_EQ(out, "UNTOUCHED");
    CHECK_EQ(card_folder_check("LOGS", out, 5u), CARD_FOLDER_OK);
    CHECK_STR_EQ(out, "LOGS");
}

TEST_CASE(a_refused_folder_falls_back)
{
    char out[CARD_DIR_MAX];
    CHECK_STR_EQ(card_folder_use("my/logs", CARD_AUTO_LOGS, out, sizeof(out)),
                 "MY/LOGS");
    CHECK_STR_EQ(card_folder_use("A/B/C", CARD_AUTO_LOGS, out, sizeof(out)),
                 CARD_AUTO_LOGS);
    CHECK_STR_EQ(card_folder_use("", CARD_USER_LOGS, out, sizeof(out)),
                 CARD_USER_LOGS);
    CHECK_STR_EQ(card_folder_use(NULL, CARD_USER_LOGS, out, sizeof(out)),
                 CARD_USER_LOGS);
    CHECK_STR_EQ(card_folder_use("BENCH/KST", CARD_USER_LOGS, out,
                                 sizeof(out)), CARD_USER_LOGS);
    CHECK(card_folder_use("LOGS", CARD_USER_LOGS, NULL, 0u)
          == (const char *)CARD_USER_LOGS);
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
    RUN(the_defaults_are_taken);
    RUN(an_empty_folder_is_refused_wherever_it_is);
    RUN(a_folder_takes_8_characters_and_not_9);
    RUN(a_path_goes_2_folders_deep_and_not_3);
    RUN(a_character_outside_the_set_is_refused);
    RUN(lower_case_is_taken_as_upper_case);
    RUN(every_device_name_is_refused_in_either_place);
    RUN(the_benchs_own_folders_are_refused);
    RUN(the_first_fault_from_the_left_answers);
    RUN(a_short_out_and_null_arguments_are_refused);
    RUN(a_refused_folder_falls_back);
    return test_summary("card_tree");
}
