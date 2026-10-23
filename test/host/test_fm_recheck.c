/**
 * @file test_fm_recheck.c
 * @brief What the file manager's jobs ask again once they run (fm_ops.h),
 *        over a card in memory (fm_fake.h): the fault log's copy when the
 *        IOmcu link drops before the open and between two chunks; a
 *        rename or a move of a name that changed its kind since it was
 *        listed; a run's report that cannot be deleted with its log.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "fm_fake.h"
#include "fm_ops.h"

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
 * @brief One step of the job.
 * @return its state after the step
 */
static fm_job_state_t step(void)
{
    return fm_job_step(&g_job, &g_io, &g_bench, g_chunk, sizeof(g_chunk));
}

/**
 * @brief Whether nothing is left open or held.
 * @return true for a clean card
 */
static bool clean(void)
{
    return g_card.holds == 0 && fk_open_files(&g_card) == 0;
}

TEST_CASE(the_link_dropping_before_the_open_stops_a_fault_log_copy)
{
    fresh();
    fk_file(&g_card, CARD_FAULT_LOG, NULL, 3u * FM_CHUNK);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, CARD_BENCH, "RCBENCH.LOG",
                         ""), FM_OK);
    g_bench.link_up = false;
    CHECK_EQ(step(), FM_JOB_STOPPED);
    CHECK_EQ(g_job.err, FM_ERR_BUSY);
    CHECK_EQ(g_card.calls[FK_OPEN], 0);
    CHECK(fk_get(&g_card, "RCBENCH.LOG.NEW") == NULL);
    CHECK(clean());
}

TEST_CASE(the_link_dropping_between_two_chunks_stops_a_fault_log_copy)
{
    fresh();
    fk_file(&g_card, CARD_FAULT_LOG, NULL, 3u * FM_CHUNK);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, CARD_BENCH, "RCBENCH.LOG",
                         ""), FM_OK);
    CHECK_EQ(step(), FM_JOB_RUNNING);        /* opened */
    CHECK_EQ(step(), FM_JOB_RUNNING);        /* chunk 1 */
    g_bench.link_up = false;
    const long reads = g_card.calls[FK_READ];
    CHECK_EQ(step(), FM_JOB_STOPPED);
    CHECK_EQ(g_job.err, FM_ERR_BUSY);
    CHECK_EQ(g_job.done, FM_CHUNK);
    CHECK_EQ(g_card.calls[FK_READ], reads);
    CHECK(fk_get(&g_card, "RCBENCH.LOG.NEW") == NULL);
    CHECK(fk_get(&g_card, "RCBENCH.LOG") == NULL);
    CHECK(clean());
    /* The link up throughout: the copy arrives. */
    fresh();
    fk_file(&g_card, CARD_FAULT_LOG, NULL, 3u * FM_CHUNK);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, CARD_BENCH, "RCBENCH.LOG",
                         ""), FM_OK);
    int n = 0;
    while (step() == FM_JOB_RUNNING && n < 100) {
        ++n;
    }
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK(fk_holds_pattern(&g_card, "RCBENCH.LOG", CARD_FAULT_LOG,
                           3u * FM_CHUNK));
}

TEST_CASE(a_file_listed_that_is_a_fixed_folder_now_is_not_renamed)
{
    fresh();
    /* Listed as a file named BENCH, which the rules call free; the card
     * holds the bench's folder under that name now. */
    fk_file(&g_card, "BENCH/LOGS/SERVO001.CSV", NULL, 5u);
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "", "BENCH", false, "B2"),
             FM_OK);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_ARG);
    CHECK_EQ(g_card.calls[FK_RENAME], 0);
    CHECK(fk_get(&g_card, "BENCH") != NULL);
    CHECK(fk_get(&g_card, "B2") == NULL);
    /* Told the truth, the rules refuse it before the card. */
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "", "BENCH", true, "B2"),
             FM_ERR_FIXED);
    /* A folder listed that is a file now, for RENAME and MOVE. */
    fk_file(&g_card, "WAS", NULL, 5u);
    fk_dir(&g_card, "DST");
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "", "WAS", true, "IS"),
             FM_OK);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_ARG);
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "", "WAS", true, "DST"),
             FM_OK);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_ARG);
    CHECK_EQ(g_card.calls[FK_RENAME], 0);
    CHECK(fk_get(&g_card, "WAS") != NULL);
    CHECK(clean());
}

TEST_CASE(a_report_that_cannot_go_with_its_run_is_the_answer)
{
    fresh();
    fk_file(&g_card, "USER/LOGS/ATEST005.CSV", NULL, 10u);
    fk_file(&g_card, "USER/LOGS/ATEST005.TXT", NULL, 10u);
    fk_fail(&g_card, FK_UNLINK, 2, FM_ERR_IO);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, CARD_USER_LOGS,
                           "ATEST005.CSV", false), FM_OK);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_IO);
    CHECK(fk_get(&g_card, "USER/LOGS/ATEST005.CSV") == NULL);
    CHECK(fk_get(&g_card, "USER/LOGS/ATEST005.TXT") != NULL);
    /* The log refused: the report is not tried. */
    fresh();
    fk_file(&g_card, "USER/LOGS/ATEST005.CSV", NULL, 10u);
    fk_file(&g_card, "USER/LOGS/ATEST005.TXT", NULL, 10u);
    fk_fail(&g_card, FK_UNLINK, 1, FM_ERR_DENIED);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, CARD_USER_LOGS,
                           "ATEST005.CSV", false), FM_OK);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_DENIED);
    CHECK_EQ(g_card.calls[FK_UNLINK], 1);
    CHECK(fk_get(&g_card, "USER/LOGS/ATEST005.CSV") != NULL);
    CHECK(fk_get(&g_card, "USER/LOGS/ATEST005.TXT") != NULL);
    CHECK(clean());
}

int main(void)
{
    RUN(the_link_dropping_before_the_open_stops_a_fault_log_copy);
    RUN(the_link_dropping_between_two_chunks_stops_a_fault_log_copy);
    RUN(a_file_listed_that_is_a_fixed_folder_now_is_not_renamed);
    RUN(a_report_that_cannot_go_with_its_run_is_the_answer);
    fk_free(&g_card);
    return test_summary("fm_recheck");
}
