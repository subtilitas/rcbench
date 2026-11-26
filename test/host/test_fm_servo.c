/**
 * @file test_fm_servo.c
 * @brief FILES' rules for the labelled servos' folders (fm_rules.h):
 *        `Servos` and every id folder in it SERVO, with no RENAME, MOVE or
 *        DELETE; a folder that is no id, and every file inside, FREE; the
 *        run names of a servo folder labelled as runs and reports at 000
 *        and 999; the analyzer's files and anything else labelled by their
 *        extension; a file the logger has open INFO only; a COPY of a
 *        servo file into USER/LOGS; lower case as upper.
 *
 * Cases:
 * - servos_and_id_folders_take_no_rename_move_or_delete
 * - a_folder_that_is_no_id_and_files_inside_are_free
 * - an_id_folder_at_the_23_character_limit_is_fixed
 * - run_names_of_a_servo_folder_are_runs_and_reports_000_to_999
 * - trend_and_baseline_files_stay_free_and_take_their_extension
 * - lower_case_is_read_as_upper
 * - a_servo_file_the_logger_holds_takes_info_only
 * - a_servo_file_copies_into_user_logs
 * - deleting_a_servo_run_leaves_its_report
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "fm_fake.h"
#include "fm_ops.h"
#include "fm_rules.h"
#include "log_take.h"

/** @brief An id folder of the plan's examples. */
#define ID_DIR CARD_SERVOS "/KST X-00001"

static fk_card_t g_card;
static fm_io_t g_io;
static fm_job_t g_job;
static fm_bench_t g_bench;
static uint8_t g_chunk[FM_CHUNK];

/** @brief A fresh card, the bench SAFE with nothing open, the link up. */
static void fresh(void)
{
    fk_free(&g_card);
    fk_init(&g_card);
    g_io = fk_io(&g_card);
    memset(&g_bench, 0, sizeof(g_bench));
    g_bench.link_up = true;
    memset(&g_job, 0, sizeof(g_job));
}

/**
 * @brief Whether folder @p name in @p dir is SERVO: RENAME, MOVE and
 *        DELETE all refused as FIXED, and OPEN, INFO and NEW pass.
 * @param dir   the folder it is in
 * @param name  the folder
 * @return true when all of it holds
 */
static bool kept(const char *dir, const char *name)
{
    const fm_op_t refused[] = { FM_OP_RENAME, FM_OP_MOVE, FM_OP_DELETE };
    const fm_op_t passed[] = { FM_OP_OPEN, FM_OP_INFO, FM_OP_NEW };
    bool ok = fm_place_of(&g_bench, dir, name, true) == FM_PLACE_SERVO;
    for (size_t i = 0u; i < 3u; ++i) {
        ok = ok && fm_allowed(&g_bench, refused[i], dir, name, true)
                       == FM_ERR_FIXED;
        ok = ok && fm_allowed(&g_bench, passed[i], dir, name, true) == FM_OK;
    }
    return ok;
}

/**
 * @brief Whether every operation a folder or a file takes passes on
 *        @p name in @p dir, and its place is FREE.
 * @param dir     the folder it is in
 * @param name    the entry
 * @param is_dir  a folder
 * @return true when all of it holds
 */
static bool free_entry(const char *dir, const char *name, bool is_dir)
{
    bool ok = fm_place_of(&g_bench, dir, name, is_dir) == FM_PLACE_FREE;
    const fm_op_t ops[] = { FM_OP_OPEN, FM_OP_INFO, FM_OP_RENAME,
                            FM_OP_MOVE, FM_OP_DELETE };
    for (size_t i = 0u; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        ok = ok && fm_allowed(&g_bench, ops[i], dir, name, is_dir) == FM_OK;
    }
    if (!is_dir) {
        ok = ok && fm_allowed(&g_bench, FM_OP_COPY, dir, name, false)
                       == FM_OK;
    }
    return ok;
}

TEST_CASE(servos_and_id_folders_take_no_rename_move_or_delete)
{
    fresh();
    CHECK(kept(CARD_ROOT, CARD_SERVOS));
    CHECK(kept(CARD_SERVOS, "KST X-00001"));
    CHECK(kept(CARD_SERVOS, "KST_X10-0000A"));
    CHECK(kept(CARD_SERVOS, "A-ZZZZZ"));
    /* The gate answers before the place, as for the bench's folders. */
    g_bench.armed = true;
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_DELETE, CARD_SERVOS, "KST X-00001",
                        true), FM_ERR_ARMED);
    /* A file named like the folder is no folder: FREE. */
    g_bench.armed = false;
    CHECK(free_entry(CARD_ROOT, CARD_SERVOS, false));
    CHECK(free_entry(CARD_SERVOS, "KST X-00001", false));
    /* Another folder's DELETE of an empty folder still passes: only the
     * servos' folders are kept whole. */
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_DELETE, "USER", "LOGS", true),
             FM_OK);
}

TEST_CASE(a_folder_that_is_no_id_and_files_inside_are_free)
{
    fresh();
    /* Not an id: no serial, a 4-character serial, a 6-character one, a
     * character the id does not take, a "." at the start. */
    static const char *const others[] = {
        "notes", "KST X-0001", "KST X-000001", "KST#X-00001", ".X-00001",
        "-00001",
    };
    for (size_t i = 0u; i < sizeof(others) / sizeof(others[0]); ++i) {
        CHECK(free_entry(CARD_SERVOS, others[i], true));
    }
    /* A folder inside an id folder, and one below that. */
    CHECK(free_entry(ID_DIR, "old", true));
    CHECK(free_entry(ID_DIR "/old", "KST X-00002", true));
    /* An id folder anywhere but in Servos. */
    CHECK(free_entry(CARD_ROOT, "KST X-00001", true));
    CHECK(free_entry("USER", "KST X-00001", true));
    /* The files of an id folder, runs and reports included. */
    static const char *const names[] = {
        "limit run_000.csv", "limit run_000.txt", "servo run_999.csv",
        "notes.txt", "photo.jpg",
    };
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
        CHECK(free_entry(ID_DIR, names[i], false));
    }
    CHECK(free_entry(CARD_SERVOS, "list.csv", false));
}

TEST_CASE(an_id_folder_at_the_23_character_limit_is_fixed)
{
    fresh();
    /* 17 characters of prefix, "-" and 5 of serial: 23. */
    CHECK(kept(CARD_SERVOS, "KST X10 HV WING L-ZZZZZ"));
    CHECK(kept(CARD_SERVOS, "ABCDEFGHIJKLMNOPQ-00000"));
    /* 18 characters of prefix: 24, no id. */
    CHECK(free_entry(CARD_SERVOS, "ABCDEFGHIJKLMNOPQR-00000", true));
    /* The longest run name inside it: 53 characters from the root. */
    const char *dir = CARD_SERVOS "/KST X10 HV WING L-ZZZZZ";
    CHECK_EQ(strlen(dir) + 1u + strlen("automatic test_999.csv"), 53u);
    CHECK_EQ(fm_kind_of(dir, "automatic test_999.csv", false), FM_KIND_RUN);
    CHECK(free_entry(dir, "automatic test_999.csv", false));
}

TEST_CASE(run_names_of_a_servo_folder_are_runs_and_reports_000_to_999)
{
    static const char *const stems[] = {
        "limit run", "rudder auto", "automatic test", "servo run",
    };
    static const int numbers[] = { 0, 1, 998, 999 };
    for (size_t s = 0u; s < sizeof(stems) / sizeof(stems[0]); ++s) {
        for (size_t k = 0u; k < sizeof(numbers) / sizeof(numbers[0]); ++k) {
            char csv[32];
            char txt[32];
            snprintf(csv, sizeof(csv), "%s_%03d.csv", stems[s], numbers[k]);
            snprintf(txt, sizeof(txt), "%s_%03d.txt", stems[s], numbers[k]);
            CHECK_EQ(fm_kind_of(ID_DIR, csv, false), FM_KIND_RUN);
            CHECK_EQ(fm_kind_of(ID_DIR, txt, false), FM_KIND_REPORT);
            /* The same names elsewhere are a CSV and a text file. */
            CHECK_EQ(fm_kind_of(CARD_USER_LOGS, csv, false), FM_KIND_CSV);
            CHECK_EQ(fm_kind_of(CARD_SERVOS, txt, false), FM_KIND_TEXT);
            CHECK_EQ(fm_kind_of(CARD_SERVOS "/notes", csv, false),
                     FM_KIND_CSV);
        }
    }
    /* Past the 3 digits, short of them, and another extension. */
    CHECK_EQ(fm_kind_of(ID_DIR, "limit run_1000.csv", false), FM_KIND_CSV);
    CHECK_EQ(fm_kind_of(ID_DIR, "limit run_00.csv", false), FM_KIND_CSV);
    CHECK_EQ(fm_kind_of(ID_DIR, "limit run_007.csv.bak", false),
             FM_KIND_OTHER);
    /* main's names in a servo folder stay what they are. */
    CHECK_EQ(fm_kind_of(ID_DIR, "ATEST005.CSV", false), FM_KIND_RUN);
    CHECK_EQ(fm_kind_of(ID_DIR, "sub", true), FM_KIND_FOLDER);
    CHECK_EQ(fm_kind_of(NULL, "limit run_000.csv", false), FM_KIND_CSV);
}

TEST_CASE(trend_and_baseline_files_stay_free_and_take_their_extension)
{
    fresh();
    static const char *const names[] = {
        "trend_limit run.csv", "trend_automatic test.csv", "baseline.txt",
    };
    static const fm_kind_t kinds[] = { FM_KIND_CSV, FM_KIND_CSV,
                                       FM_KIND_TEXT };
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
        CHECK(free_entry(ID_DIR, names[i], false));
        CHECK_EQ(fm_kind_of(ID_DIR, names[i], false), kinds[i]);
    }
}

TEST_CASE(lower_case_is_read_as_upper)
{
    fresh();
    CHECK(kept(CARD_ROOT, "servos"));
    CHECK(kept(CARD_ROOT, "SERVOS"));
    CHECK(kept("servos", "kst x-00001"));
    CHECK(kept("SERVOS", "kst_x10-0000a"));
    CHECK_EQ(fm_kind_of("servos/kst x-00001", "LIMIT RUN_000.CSV", false),
             FM_KIND_RUN);
    CHECK_EQ(fm_kind_of("SERVOS/KST X-00001", "Automatic Test_042.Txt",
                        false), FM_KIND_REPORT);
}

TEST_CASE(a_servo_file_the_logger_holds_takes_info_only)
{
    /* The marks of the folder are read by log_marks_busy(), the one rule
     * of the logger's open files: FILES answers BUSY for every operation
     * but INFO exactly where it says busy. */
    fresh();
    log_marks_t marks = { .run_dir = ID_DIR, .run = 7u,
                          .test_dir = ID_DIR, .test = 7u, .txt = 7u };
    g_bench.marks = &marks;
    static const char *const names[] = {
        "SERVO007.CSV", "ATEST007.TXT", "limit run_007.csv",
        "limit run_000.csv", "notes.txt",
    };
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
        const bool busy = log_marks_busy(&marks, ID_DIR, names[i]);
        CHECK_EQ(fm_busy(&g_bench, ID_DIR, names[i]), busy);
        CHECK_EQ(fm_allowed(&g_bench, FM_OP_OPEN, ID_DIR, names[i], false),
                 busy ? FM_ERR_BUSY : FM_OK);
        CHECK_EQ(fm_allowed(&g_bench, FM_OP_INFO, ID_DIR, names[i], false),
                 FM_OK);
    }
    CHECK(fm_busy(&g_bench, ID_DIR, "SERVO007.CSV"));
    /* The same name in another id folder is another file. */
    CHECK(!fm_busy(&g_bench, CARD_SERVOS "/KST X-00002", "SERVO007.CSV"));
    /* A write waits for the run whatever the file. */
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_DELETE, ID_DIR, "notes.txt", false),
             FM_ERR_LOGGING);
}

TEST_CASE(a_servo_file_copies_into_user_logs)
{
    fresh();
    static const uint8_t k_csv[] = "time,angle\n0.0,1.5\n";
    fk_dir(&g_card, ID_DIR);
    fk_dir(&g_card, CARD_USER_LOGS);
    fk_file(&g_card, ID_DIR "/limit run_000.csv", k_csv, sizeof(k_csv) - 1u);
    CHECK_EQ(fm_target_check(&g_bench, CARD_USER_LOGS, "limit run_000.csv",
                             false), FM_OK);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, g_card.mount, ID_DIR,
                         "limit run_000.csv", CARD_USER_LOGS), FM_OK);
    int steps = 0;
    while (fm_job_state(&g_job, NULL, NULL) == FM_JOB_RUNNING && steps < 100) {
        (void)fm_job_step(&g_job, &g_io, &g_bench, g_chunk, sizeof(g_chunk));
        ++steps;
    }
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    const fk_node_t *got = fk_get(&g_card,
                                  CARD_USER_LOGS "/limit run_000.csv");
    CHECK(got != NULL);
    if (got != NULL) {
        CHECK_EQ(got->size, sizeof(k_csv) - 1u);
        CHECK_EQ(memcmp(got->data, k_csv, got->size), 0);
    }
    /* In USER/LOGS it is a CSV LOGS lists, no servo's run. */
    CHECK_EQ(fm_kind_of(CARD_USER_LOGS, "limit run_000.csv", false),
             FM_KIND_CSV);
    /* And back into an id folder, and into Servos itself. */
    CHECK_EQ(fm_target_check(&g_bench, ID_DIR, "copy.csv", false), FM_OK);
    CHECK_EQ(fm_target_check(&g_bench, CARD_SERVOS, "copy.csv", false),
             FM_OK);
    fk_free(&g_card);
}

TEST_CASE(deleting_a_servo_run_leaves_its_report)
{
    /* The report a DELETE takes along is named by main's names
     * (log_name_report_of()); a servo's report stays on the card. */
    fresh();
    fk_dir(&g_card, ID_DIR);
    fk_file(&g_card, ID_DIR "/limit run_000.csv", NULL, 4u);
    fk_file(&g_card, ID_DIR "/limit run_000.txt", NULL, 4u);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, g_card.mount, ID_DIR,
                           "limit run_000.csv", false), FM_OK);
    int steps = 0;
    while (fm_job_state(&g_job, NULL, NULL) == FM_JOB_RUNNING && steps < 100) {
        (void)fm_job_step(&g_job, &g_io, &g_bench, g_chunk, sizeof(g_chunk));
        ++steps;
    }
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK(fk_get(&g_card, ID_DIR "/limit run_000.csv") == NULL);
    CHECK(fk_get(&g_card, ID_DIR "/limit run_000.txt") != NULL);
    fk_free(&g_card);
}

int main(void)
{
    RUN(servos_and_id_folders_take_no_rename_move_or_delete);
    RUN(a_folder_that_is_no_id_and_files_inside_are_free);
    RUN(an_id_folder_at_the_23_character_limit_is_fixed);
    RUN(run_names_of_a_servo_folder_are_runs_and_reports_000_to_999);
    RUN(trend_and_baseline_files_stay_free_and_take_their_extension);
    RUN(lower_case_is_read_as_upper);
    RUN(a_servo_file_the_logger_holds_takes_info_only);
    RUN(a_servo_file_copies_into_user_logs);
    RUN(deleting_a_servo_run_leaves_its_report);
    return test_summary("fm_servo");
}
