/**
 * @file test_servo_name.c
 * @brief The names of a labelled servo's files (servo_name.h): every kind's
 *        name built and read back at 000 and 999, names that are no run
 *        (_1000, _99, a trailing .bak, another file of the folder), any
 *        case, the report of a run, the kind of each writer, and
 *        Servos/<id> read back with a space in the id.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "servo_id.h"
#include "servo_name.h"

TEST_CASE(every_kind_is_named_and_read_back_at_000_and_999)
{
    static const char *const want[SERVO_KIND_COUNT][2] = {
        { "limit run_000.csv", "limit run_999.txt" },
        { "rudder auto_000.csv", "rudder auto_999.txt" },
        { "automatic test_000.csv", "automatic test_999.txt" },
        { "servo run_000.csv", "servo run_999.txt" },
    };
    for (int k = 0; k < (int)SERVO_KIND_COUNT; ++k) {
        char name[SERVO_NAME_SIZE] = "x";
        servo_run_name(name, sizeof(name), (servo_kind_t)k, SERVO_RUN_FIRST,
                       false);
        CHECK_STR_EQ(name, want[k][0]);
        servo_kind_t got_k = SERVO_KIND_COUNT;
        bool report = true;
        CHECK_EQ(servo_run_parse(name, &got_k, &report), 0);
        CHECK_EQ(got_k, (servo_kind_t)k);
        CHECK(!report);

        servo_run_name(name, sizeof(name), (servo_kind_t)k, SERVO_RUN_LAST,
                       true);
        CHECK_STR_EQ(name, want[k][1]);
        CHECK_EQ(servo_run_parse(name, &got_k, &report), 999);
        CHECK_EQ(got_k, (servo_kind_t)k);
        CHECK(report);
    }
    /* The longest name fills SERVO_NAME_SIZE; a byte less takes none. */
    char name[SERVO_NAME_SIZE];
    servo_run_name(name, sizeof(name), SERVO_KIND_ATEST, 999, false);
    CHECK_EQ(strlen(name), SERVO_NAME_SIZE - 1u);
    char small[SERVO_NAME_SIZE - 1u] = "x";
    servo_run_name(small, sizeof(small), SERVO_KIND_ATEST, 999, false);
    CHECK_STR_EQ(small, "");
}

TEST_CASE(a_number_or_kind_out_of_range_names_nothing)
{
    char name[SERVO_NAME_SIZE] = "x";
    servo_run_name(name, sizeof(name), SERVO_KIND_LIMIT, 1000, false);
    CHECK_STR_EQ(name, "");
    strcpy(name, "x");
    servo_run_name(name, sizeof(name), SERVO_KIND_LIMIT, -1, false);
    CHECK_STR_EQ(name, "");
    strcpy(name, "x");
    servo_run_name(name, sizeof(name), SERVO_KIND_COUNT, 1, false);
    CHECK_STR_EQ(name, "");
    servo_run_name(NULL, 8u, SERVO_KIND_LIMIT, 1, false);
    servo_run_name(name, 0u, SERVO_KIND_LIMIT, 1, false);
    CHECK_STR_EQ(servo_kind_stem(SERVO_KIND_RUN), "servo run");
    CHECK_STR_EQ(servo_kind_stem(SERVO_KIND_COUNT), "");
    CHECK_STR_EQ(servo_kind_stem((servo_kind_t)-1), "");
}

TEST_CASE(only_a_stem_three_digits_and_csv_or_txt_is_a_run)
{
    servo_kind_t k = SERVO_KIND_COUNT;
    bool report = false;
    /* Any case: a computer may write upper case. */
    CHECK_EQ(servo_run_parse("LIMIT RUN_007.CSV", &k, &report), 7);
    CHECK_EQ(k, SERVO_KIND_LIMIT);
    CHECK(!report);
    CHECK_EQ(servo_run_parse("Rudder Auto_042.Txt", &k, &report), 42);
    CHECK_EQ(k, SERVO_KIND_RAUTO);
    CHECK(report);
    CHECK_EQ(servo_run_parse("servo run_500.csv", NULL, NULL), 500);

    CHECK_EQ(servo_run_parse("limit run_1000.csv", &k, NULL), -1);
    CHECK_EQ(servo_run_parse("limit run_99.csv", &k, NULL), -1);
    CHECK_EQ(servo_run_parse("limit run_007.csv.bak", &k, NULL), -1);
    CHECK_EQ(servo_run_parse("limit run_007.bak", &k, NULL), -1);
    CHECK_EQ(servo_run_parse("limit run_0a7.csv", &k, NULL), -1);
    CHECK_EQ(servo_run_parse("limit run-007.csv", &k, NULL), -1);
    CHECK_EQ(servo_run_parse("limit_run_007.csv", &k, NULL), -1);
    CHECK_EQ(servo_run_parse("limitrun_007.csv", &k, NULL), -1);
    CHECK_EQ(servo_run_parse("trend_limit run.csv", &k, NULL), -1);
    CHECK_EQ(servo_run_parse("baseline.txt", &k, NULL), -1);
    CHECK_EQ(servo_run_parse("LIMIT007.CSV", &k, NULL), -1);
    CHECK_EQ(servo_run_parse("", &k, NULL), -1);
    CHECK_EQ(servo_run_parse(NULL, &k, NULL), -1);
    CHECK_EQ(k, SERVO_KIND_RAUTO);   /* untouched by a name that is no run */
}

TEST_CASE(a_runs_report_has_its_number_and_txt)
{
    char out[SERVO_NAME_SIZE] = "x";
    servo_run_report_of(out, sizeof(out), "limit run_007.csv");
    CHECK_STR_EQ(out, "limit run_007.txt");
    servo_run_report_of(out, sizeof(out), "AUTOMATIC TEST_999.CSV");
    CHECK_STR_EQ(out, "automatic test_999.txt");
    servo_run_report_of(out, sizeof(out), "servo run_000.txt");
    CHECK_STR_EQ(out, "servo run_000.txt");
    strcpy(out, "x");
    servo_run_report_of(out, sizeof(out), "notes.csv");
    CHECK_STR_EQ(out, "");
    strcpy(out, "x");
    servo_run_report_of(out, sizeof(out), NULL);
    CHECK_STR_EQ(out, "");
    servo_run_report_of(NULL, 8u, "limit run_007.csv");
}

TEST_CASE(the_kind_of_each_writer)
{
    CHECK_EQ(servo_kind_of_prefix(LOG_PREFIX_LIMIT), SERVO_KIND_LIMIT);
    CHECK_EQ(servo_kind_of_prefix(LOG_PREFIX_RUDDR), SERVO_KIND_RAUTO);
    CHECK_EQ(servo_kind_of_prefix(LOG_PREFIX_ATEST), SERVO_KIND_ATEST);
    CHECK_EQ(servo_kind_of_prefix(LOG_PREFIX_SERVO), SERVO_KIND_RUN);
    CHECK_EQ(servo_kind_of_prefix(LOG_PREFIX_MOTOR), SERVO_KIND_COUNT);
    CHECK_EQ(servo_kind_of_prefix(LOG_PREFIX_SUPPL), SERVO_KIND_COUNT);
    CHECK_EQ(servo_kind_of_prefix(LOG_PREFIX_BENCH), SERVO_KIND_COUNT);
    CHECK_EQ(servo_kind_of_prefix(LOG_PREFIX_COUNT), SERVO_KIND_COUNT);
}

TEST_CASE(a_servo_folder_is_read_back_to_its_id)
{
    char id[SERVO_ID_SIZE] = "x";
    CHECK(servo_dir_parse("Servos/KST X10-00001", id, sizeof(id)));
    CHECK_STR_EQ(id, "KST X10-00001");
    CHECK(servo_dir_parse("servos/kst x10-0000a", id, sizeof(id)));
    CHECK_STR_EQ(id, "KST X10-0000A");
    CHECK(servo_dir_parse("SERVOS/KST X10 LEFT WING-ZZZZZ", id, sizeof(id)));
    CHECK_STR_EQ(id, "KST X10 LEFT WING-ZZZZZ");
    CHECK(servo_dir_parse("Servos/A-00001", NULL, 0u));

    /* The folder round trip. */
    char dir[CARD_RUN_DIR_MAX];
    CHECK(servo_id_folder(dir, sizeof(dir), "KST X10-00001"));
    CHECK(servo_dir_parse(dir, id, sizeof(id)));
    CHECK_STR_EQ(id, "KST X10-00001");

    strcpy(id, "kept");
    CHECK(!servo_dir_parse("Servos/notes", id, sizeof(id)));
    CHECK(!servo_dir_parse("Servos", id, sizeof(id)));
    CHECK(!servo_dir_parse("Servos/", id, sizeof(id)));
    CHECK(!servo_dir_parse("Servos/KST-00001/x", id, sizeof(id)));
    CHECK(!servo_dir_parse("USER/LOGS", id, sizeof(id)));
    CHECK(!servo_dir_parse("/Servos/KST-00001", id, sizeof(id)));
    CHECK(!servo_dir_parse("ServosX/KST-00001", id, sizeof(id)));
    CHECK(!servo_dir_parse(NULL, id, sizeof(id)));
    CHECK_STR_EQ(id, "kept");
    char small[9];
    CHECK(!servo_dir_parse("Servos/KST-00001", small, sizeof(small)));
    CHECK(servo_dir_parse("Servos/KS-00001", small, sizeof(small)));
    CHECK_STR_EQ(small, "KS-00001");
}

int main(void)
{
    RUN(every_kind_is_named_and_read_back_at_000_and_999);
    RUN(a_number_or_kind_out_of_range_names_nothing);
    RUN(only_a_stem_three_digits_and_csv_or_txt_is_a_run);
    RUN(a_runs_report_has_its_number_and_txt);
    RUN(the_kind_of_each_writer);
    RUN(a_servo_folder_is_read_back_to_its_id);
    return test_summary("servo_name");
}
