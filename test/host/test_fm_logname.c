/**
 * @file test_fm_logname.c
 * @brief RENAME LOG's rename of the MOTOR log closed last (fm_logname.h)
 *        over a card in memory (fm_fake.h): the label written and taken
 *        off again; a taken name; a log renamed elsewhere; the card pulled
 *        or swapped since the run; ARMED and a log open; a full and a
 *        write-protected card; and the refusals before the card, an empty
 *        record, a label too long or with a mark FAT refuses.  Every
 *        refusal leaves both names as they were.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "fm_fake.h"
#include "fm_logname.h"
#include "fm_ops.h"
#include "fm_rules.h"
#include "log_name.h"
#include "log_take.h"

/* ------------------------------------------------------------ the rigs */

static fk_card_t g_card;
static fm_io_t g_io;
static fm_job_t g_job;
static fm_bench_t g_bench;
static fm_logname_t g_last;
static log_marks_t g_marks;
static char g_new[LOG_LABELLED_SIZE];

/** @brief The log's bytes: a run's worth, the pattern of its first path. */
#define LOG_BYTES 4321u
/** @brief The log as the logger closed it. */
#define LOG_PATH "BENCH/LOGS/MOTOR042.CSV"

/**
 * @brief A card holding MOTOR041.CSV and MOTOR042.CSV in BENCH/LOGS, the
 *        bench SAFE with nothing open, MOTOR042.CSV the log closed last.
 */
static void fresh(void)
{
    fk_free(&g_card);
    fk_init(&g_card);
    g_io = fk_io(&g_card);
    memset(&g_bench, 0, sizeof(g_bench));
    g_bench.link_up = true;
    memset(&g_job, 0, sizeof(g_job));
    fk_file(&g_card, "BENCH/LOGS/MOTOR041.CSV", NULL, 100u);
    fk_file(&g_card, LOG_PATH, NULL, LOG_BYTES);
    memset(&g_last, 0, sizeof(g_last));
    snprintf(g_last.dir, sizeof(g_last.dir), "%s", CARD_AUTO_LOGS);
    snprintf(g_last.name, sizeof(g_last.name), "MOTOR042.CSV");
    g_last.mount = g_card.mount;
}

/**
 * @brief RENAME LOG with @p label; on success the record follows, as the
 *        panel's logger keeps it.
 * @param label  the label
 * @return what the rename answered
 */
static fm_err_t rename_to(const char *label)
{
    const fm_err_t err = fm_logname_run(&g_job, &g_io, &g_bench, &g_last,
                                        label, g_new, sizeof(g_new));
    if (err == FM_OK) {
        snprintf(g_last.name, sizeof(g_last.name), "%s", g_new);
    }
    return err;
}

/**
 * @brief Whether the log is at @p name in BENCH/LOGS with its bytes, and
 *        nowhere else under the number.
 * @param name  the name it should have
 * @return true when it is
 */
static bool log_is(const char *name)
{
    char path[FM_PATH_SIZE];
    snprintf(path, sizeof(path), "%s/%s", CARD_AUTO_LOGS, name);
    int under_42 = 0;
    for (int i = 0; i < g_card.count; ++i) {
        const char *slash = strrchr(g_card.n[i].path, '/');
        if (slash != NULL && log_name_number(slash + 1) == 42) {
            ++under_42;
        }
    }
    return under_42 == 1 && fk_holds_pattern(&g_card, path, LOG_PATH,
                                             LOG_BYTES);
}

/** @brief Whether nothing is left open or held. */
static bool clean(void)
{
    return g_card.holds == 0 && fk_open_files(&g_card) == 0;
}

/* ------------------------------------------------------------ the cases */

TEST_CASE(a_label_is_written_behind_the_number_and_taken_off_again)
{
    fresh();
    CHECK_EQ(rename_to("kv1200"), FM_OK);
    CHECK_STR_EQ(g_new, "MOTOR042_kv1200.CSV");
    CHECK(log_is("MOTOR042_kv1200.CSV"));
    CHECK_EQ(g_card.calls[FK_WRITE], 0);
    /* Renamed again: the old label is replaced, not added to. */
    CHECK_EQ(rename_to("  2S 5000mAh v1.2_a  "), FM_OK);
    CHECK_STR_EQ(g_new, "MOTOR042_2S 5000mAh v1.2_a.CSV");
    CHECK(log_is("MOTOR042_2S 5000mAh v1.2_a.CSV"));
    /* An empty label gives the plain name back. */
    CHECK_EQ(rename_to(""), FM_OK);
    CHECK(log_is("MOTOR042.CSV"));
    /* The longest label, 46 bytes: a 59-byte name. */
    char lab[LOG_LABEL_MAX + 1u];
    memset(lab, 'k', LOG_LABEL_MAX);
    lab[LOG_LABEL_MAX] = '\0';
    CHECK_EQ(rename_to(lab), FM_OK);
    CHECK_EQ((int)strlen(g_new), (int)LOG_LABELLED_MAX);
    CHECK(log_is(g_new));
    /* A change of case of the label alone is the same entry. */
    CHECK_EQ(rename_to("KV"), FM_OK);
    CHECK_EQ(rename_to("kv"), FM_OK);
    CHECK(log_is("MOTOR042_kv.CSV"));
    CHECK(fk_get(&g_card, "BENCH/LOGS/MOTOR041.CSV") != NULL);
    CHECK(clean());
}

TEST_CASE(a_taken_name_is_refused_and_both_names_stay)
{
    fresh();
    fk_file(&g_card, "BENCH/LOGS/motor042_KV.csv", NULL, 7u);
    CHECK_EQ(rename_to("kv"), FM_ERR_EXISTS);
    CHECK_STR_EQ(g_new, "MOTOR042_kv.CSV");
    CHECK(fk_holds_pattern(&g_card, LOG_PATH, LOG_PATH, LOG_BYTES));
    CHECK(fk_holds_pattern(&g_card, "BENCH/LOGS/motor042_KV.csv",
                           "BENCH/LOGS/motor042_KV.csv", 7u));
    CHECK_EQ(g_card.calls[FK_RENAME], 0);
    CHECK(clean());
}

TEST_CASE(a_log_renamed_or_deleted_elsewhere_is_not_found)
{
    fresh();
    /* FILES gave it another name since the run. */
    fk_file(&g_card, "BENCH/LOGS/MOTOR042_FILES.CSV", NULL, 3u);
    CHECK_EQ(g_io.unlink(g_io.ctx, LOG_PATH), FM_OK);
    CHECK_EQ(rename_to("kv"), FM_ERR_NOT_FOUND);
    CHECK(fk_get(&g_card, "BENCH/LOGS/MOTOR042_kv.CSV") == NULL);
    CHECK(fk_get(&g_card, "BENCH/LOGS/MOTOR042_FILES.CSV") != NULL);
    CHECK(clean());
}

TEST_CASE(a_card_pulled_or_swapped_since_the_run_is_not_written)
{
    /* Pulled: no card. */
    fresh();
    fk_pull(&g_card);
    CHECK_EQ(rename_to("kv"), FM_ERR_NO_CARD);
    /* Pulled and put back: another mount, which may be another card with
     * a MOTOR042.CSV of its own.  Nothing is renamed on it. */
    fk_insert(&g_card);
    CHECK(g_card.mount != g_last.mount);
    CHECK_EQ(rename_to("kv"), FM_ERR_CARD_GONE);
    CHECK(log_is("MOTOR042.CSV"));
    CHECK_EQ(g_card.calls[FK_RENAME], 0);
    /* The record of the mount it is on now: renamed. */
    g_last.mount = g_card.mount;
    CHECK_EQ(rename_to("kv"), FM_OK);
    CHECK(log_is("MOTOR042_kv.CSV"));
    /* Pulled in the rename itself. */
    fk_fail(&g_card, FK_RENAME, 1, FM_ERR_CARD_GONE);
    CHECK_EQ(rename_to("x"), FM_ERR_CARD_GONE);
    CHECK(clean());
}

TEST_CASE(armed_or_a_log_open_refuses_and_changes_nothing)
{
    fresh();
    g_bench.armed = true;
    CHECK_EQ(rename_to("kv"), FM_ERR_ARMED);
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_FAILED);
    CHECK(log_is("MOTOR042.CSV"));
    /* SAFE again, a new run's log open: the card is read-only. */
    g_bench.armed = false;
    memset(&g_marks, 0, sizeof(g_marks));
    g_marks.run = 43u;
    g_marks.run_dir = CARD_AUTO_LOGS;
    g_bench.marks = &g_marks;
    fk_file(&g_card, "BENCH/LOGS/MOTOR043.CSV", NULL, 9u);
    CHECK_EQ(rename_to("kv"), FM_ERR_LOGGING);
    CHECK(log_is("MOTOR042.CSV"));
    CHECK(fk_holds_pattern(&g_card, "BENCH/LOGS/MOTOR043.CSV",
                           "BENCH/LOGS/MOTOR043.CSV", 9u));
    /* The log itself still open, as before its close: refused the same. */
    g_marks.run = 42u;
    CHECK_EQ(rename_to("kv"), FM_ERR_LOGGING);
    /* A test's report open in USER/LOGS: still a log open. */
    g_marks.run = 0u;
    g_marks.run_dir = NULL;
    g_marks.txt = 5u;
    g_marks.test_dir = CARD_USER_LOGS;
    CHECK_EQ(rename_to("kv"), FM_ERR_LOGGING);
    /* All closed: renamed. */
    g_marks.txt = 0u;
    CHECK_EQ(rename_to("kv"), FM_OK);
    CHECK(log_is("MOTOR042_kv.CSV"));
    CHECK_EQ(g_card.calls[FK_RENAME], 1);
    CHECK(clean());
}

TEST_CASE(a_full_or_write_protected_card_keeps_the_old_name)
{
    fresh();
    fk_fail(&g_card, FK_RENAME, 1, FM_ERR_FULL);
    CHECK_EQ(rename_to("kv"), FM_ERR_FULL);
    CHECK(log_is("MOTOR042.CSV"));
    fk_fail(&g_card, FK_RENAME, 1, FM_ERR_DENIED);
    CHECK_EQ(rename_to("kv"), FM_ERR_DENIED);
    CHECK(log_is("MOTOR042.CSV"));
    CHECK(clean());
}

TEST_CASE(refusals_before_the_card_touch_nothing)
{
    fresh();
    const long calls = g_card.calls[FK_ANY];
    /* No MOTOR log since the start. */
    fm_logname_t none;
    memset(&none, 0, sizeof(none));
    CHECK_EQ(fm_logname_run(&g_job, &g_io, &g_bench, &none, "kv", g_new,
                            sizeof(g_new)), FM_ERR_ARG);
    CHECK_STR_EQ(g_new, "");
    /* Not a run log. */
    fm_logname_t other = g_last;
    snprintf(other.name, sizeof(other.name), "SWEEP.CSV");
    CHECK_EQ(fm_logname_run(&g_job, &g_io, &g_bench, &other, "kv", g_new,
                            sizeof(g_new)), FM_ERR_ARG);
    /* 47 bytes: over the 59-byte name. */
    char lab[LOG_LABEL_MAX + 2u];
    memset(lab, 'k', LOG_LABEL_MAX + 1u);
    lab[LOG_LABEL_MAX + 1u] = '\0';
    CHECK_EQ(rename_to(lab), FM_ERR_NAME);
    CHECK_STR_EQ(g_new, "");
    /* Each mark a FAT name cannot hold. */
    static const char *const k_bad[] = { "a*b", "a/b", "a:b", "a?b", "a\"b",
                                         "a<b", "a>b", "a\\b", "a|b",
                                         "\x01" };
    for (size_t i = 0u; i < sizeof(k_bad) / sizeof(k_bad[0]); ++i) {
        if (rename_to(k_bad[i]) != FM_ERR_NAME) {
            T_FAIL("label \"%s\" was not refused", k_bad[i]);
        }
    }
    /* The name it has. */
    CHECK_EQ(rename_to(""), FM_ERR_ARG);
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_FAILED);
    /* NULLs. */
    CHECK_EQ(fm_logname_run(NULL, &g_io, &g_bench, &g_last, "kv", g_new,
                            sizeof(g_new)), FM_ERR_ARG);
    CHECK_EQ(fm_logname_run(&g_job, &g_io, &g_bench, NULL, "kv", g_new,
                            sizeof(g_new)), FM_ERR_ARG);
    CHECK_EQ(fm_logname_run(&g_job, &g_io, &g_bench, &g_last, "kv", NULL,
                            8u), FM_ERR_ARG);
    CHECK_EQ(fm_logname_run(&g_job, &g_io, &g_bench, &g_last, "kv", g_new,
                            0u), FM_ERR_ARG);
    CHECK_EQ(g_card.calls[FK_ANY], calls);
    CHECK(log_is("MOTOR042.CSV"));
    /* A card the io cannot reach: the job fails, nothing is left held. */
    CHECK_EQ(fm_logname_run(&g_job, NULL, &g_bench, &g_last, "kv", g_new,
                            sizeof(g_new)), FM_ERR_ARG);
    CHECK(clean());
}

int main(void)
{
    RUN(a_label_is_written_behind_the_number_and_taken_off_again);
    RUN(a_taken_name_is_refused_and_both_names_stay);
    RUN(a_log_renamed_or_deleted_elsewhere_is_not_found);
    RUN(a_card_pulled_or_swapped_since_the_run_is_not_written);
    RUN(armed_or_a_log_open_refuses_and_changes_nothing);
    RUN(a_full_or_write_protected_card_keeps_the_old_name);
    RUN(refusals_before_the_card_touch_nothing);
    fk_free(&g_card);
    return test_summary("fm_logname");
}
