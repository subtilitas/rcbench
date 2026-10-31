/**
 * @file test_fm_match.c
 * @brief A save that checks what it replaces (fm_job_match(), fm_ops.h),
 *        over a card in memory (fm_fake.h): the sum equal replaces the
 *        file, the sum different or the file gone fails CHANGED with
 *        nothing written; a taken name fails ABSENT; ARMED, CANCEL and a
 *        card pulled while the old file is read stop the save with no
 *        ".NEW"; a ".NEW" already there is kept; and the check is refused
 *        for any job but a SAVE that has not run.  A LOAD and a SAVE sum
 *        the bytes they read or write (fm_job_sum()): the sum the next
 *        SAVE of the file compares with.
 *
 * Cases:
 * - a_matching_sum_replaces_the_file
 * - a_different_sum_fails_changed_and_writes_nothing
 * - a_refusal_gives_the_sum_it_found
 * - a_file_gone_since_its_sum_fails_changed
 * - absent_refuses_a_taken_name_and_makes_a_free_one
 * - armed_or_cancel_while_summing_stop_with_no_new
 * - a_card_pulled_while_summing_fails_card_gone
 * - a_new_there_already_is_kept_after_the_sum
 * - the_check_is_refused_for_other_jobs_and_after_a_step
 * - a_load_and_a_save_sum_the_bytes_they_pass
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "fm_fake.h"
#include "fm_io.h"
#include "fm_ops.h"

static fk_card_t g_card;
static fm_io_t g_io;
static fm_job_t g_job;
static fm_bench_t g_bench;
static uint8_t g_chunk[FM_CHUNK];
/** @brief The text a save writes: 2.5 chunks, a byte pattern. */
static uint8_t g_text[FM_CHUNK * 5u / 2u];

/** @brief The old file: three chunks and one byte, so the sum's last read
 *         holds one byte. */
#define OLD_SIZE (3u * FM_CHUNK + 1u)

/** @brief A fresh card, the bench SAFE, the link up, the text set. */
static void fresh(void)
{
    fk_free(&g_card);
    fk_init(&g_card);
    g_io = fk_io(&g_card);
    memset(&g_bench, 0, sizeof(g_bench));
    g_bench.link_up = true;
    memset(&g_job, 0, sizeof(g_job));
    for (size_t i = 0u; i < sizeof(g_text); ++i) {
        g_text[i] = (uint8_t)('a' + i % 26u);
    }
}

/**
 * @brief The test's sum: 31 times the sum so far plus each byte.
 * @param sum   the sum so far
 * @param data  the bytes
 * @param n     how many
 * @return the sum with them
 */
static uint32_t sum31(uint32_t sum, const void *data, size_t n)
{
    const uint8_t *b = data;
    for (size_t i = 0u; i < n; ++i) {
        sum = sum * 31u + b[i];
    }
    return sum;
}

/**
 * @brief sum31() of what the card holds at @p path.
 * @param path  a file
 * @return its sum; 0 for none
 */
static uint32_t sum_of(const char *path)
{
    const fk_node_t *n = fk_get(&g_card, path);
    return (n != NULL) ? sum31(0u, n->data, n->size) : 0u;
}

/**
 * @brief A save's bytes: g_text.
 * @param ctx  unused
 * @param at   the offset
 * @param dst  takes the bytes
 * @param cap  room
 * @return the bytes given
 */
static size_t fill_text(void *ctx, uint64_t at, uint8_t *dst, size_t cap)
{
    (void)ctx;
    const size_t left = sizeof(g_text) - (size_t)at;
    const size_t n = (left < cap) ? left : cap;
    memcpy(dst, g_text + at, n);
    return n;
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
 * @brief Steps until the job stops running, 100 at most.
 * @return its state
 */
static fm_job_state_t run(void)
{
    int n = 0;
    while (step() == FM_JOB_RUNNING && n < 100) {
        ++n;
    }
    return fm_job_state(&g_job, NULL, NULL);
}

/** @brief Start the save of g_text as D/NOTE.TXT. */
static void save(void)
{
    CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, "D", "NOTE.TXT",
                         sizeof(g_text), fill_text, NULL), FM_OK);
}

/**
 * @brief Whether nothing is left open or held.
 * @return true for a clean card
 */
static bool clean(void)
{
    return g_card.holds == 0 && fk_open_files(&g_card) == 0;
}

/**
 * @brief Whether D/NOTE.TXT holds g_text.
 * @return true when it does
 */
static bool holds_text(void)
{
    const fk_node_t *n = fk_get(&g_card, "D/NOTE.TXT");
    return n != NULL && n->size == sizeof(g_text)
           && memcmp(n->data, g_text, sizeof(g_text)) == 0;
}

TEST_CASE(a_matching_sum_replaces_the_file)
{
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, OLD_SIZE);
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31,
                          sum_of("D/NOTE.TXT")), FM_OK);
    CHECK_EQ(step(), FM_JOB_RUNNING);        /* the old file opened */
    CHECK(fk_get(&g_card, "D/NOTE.TXT.NEW") == NULL);
    /* Four reads of the old file: three chunks, one byte, the end. */
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(step(), FM_JOB_RUNNING);
        CHECK(fk_get(&g_card, "D/NOTE.TXT.NEW") == NULL);
    }
    CHECK_EQ(step(), FM_JOB_RUNNING);        /* the end: ".NEW" opened */
    CHECK(fk_get(&g_card, "D/NOTE.TXT.NEW") != NULL);
    CHECK_EQ(run(), FM_JOB_DONE);
    CHECK(holds_text());
    CHECK(fk_get(&g_card, "D/NOTE.TXT.NEW") == NULL);
    CHECK(g_job.dst_was);
    CHECK(clean());
    /* An empty old file sums to the start value. */
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, 0u);
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31, 0u), FM_OK);
    CHECK_EQ(run(), FM_JOB_DONE);
    CHECK(holds_text());
    CHECK(clean());
}

TEST_CASE(a_different_sum_fails_changed_and_writes_nothing)
{
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, OLD_SIZE);
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31,
                          sum_of("D/NOTE.TXT") + 1u), FM_OK);
    CHECK_EQ(run(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_CHANGED);
    CHECK(fk_holds_pattern(&g_card, "D/NOTE.TXT", "D/NOTE.TXT", OLD_SIZE));
    CHECK(fk_get(&g_card, "D/NOTE.TXT.NEW") == NULL);
    CHECK_EQ(g_card.calls[FK_WRITE], 0);
    CHECK_EQ(g_card.calls[FK_UNLINK], 0);
    CHECK(clean());
    /* One byte changed at the old file's last byte is found. */
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, OLD_SIZE);
    const uint32_t was = sum_of("D/NOTE.TXT");
    fk_node_t *n = (fk_node_t *)fk_get(&g_card, "D/NOTE.TXT");
    n->data[OLD_SIZE - 1u] ^= 1u;
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31, was), FM_OK);
    CHECK_EQ(run(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_CHANGED);
    CHECK(clean());
    /* Without the check the same save replaces it. */
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, OLD_SIZE);
    save();
    CHECK_EQ(run(), FM_JOB_DONE);
    CHECK(holds_text());
}

/*
 * fm_job_found(): a refusal over a file that is there gives the file's sum,
 * which a next SAVE takes; no other answer gives one.
 */
TEST_CASE(a_refusal_gives_the_sum_it_found)
{
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, OLD_SIZE);
    const uint32_t now = sum_of("D/NOTE.TXT");
    uint32_t found = 0x5A5A5A5Au;
    save();
    CHECK(!fm_job_found(&g_job, &found));       /* no step has run */
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31, now + 1u), FM_OK);
    CHECK_EQ(run(), FM_JOB_FAILED);
    CHECK(fm_job_found(&g_job, &found));
    CHECK_EQ(found, now);
    CHECK(!fm_job_found(NULL, &found));
    CHECK(!fm_job_found(&g_job, NULL));
    /* The sum found replaces the file; a job that wrote has none to give. */
    save();
    CHECK(!fm_job_found(&g_job, &found));
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31, found), FM_OK);
    CHECK_EQ(run(), FM_JOB_DONE);
    CHECK(holds_text());
    found = 0x5A5A5A5Au;
    CHECK(!fm_job_found(&g_job, &found));
    CHECK_EQ(found, 0x5A5A5A5Au);
    /* A file that is gone has no sum. */
    fresh();
    fk_dir(&g_card, "D");
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31, 0u), FM_OK);
    CHECK_EQ(run(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_CHANGED);
    CHECK(!fm_job_found(&g_job, &found));
    /* A read that fails part way has none either. */
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, OLD_SIZE);
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31, 1u), FM_OK);
    fk_fail(&g_card, FK_READ, 1, FM_ERR_IO);
    CHECK_EQ(run(), FM_JOB_FAILED);
    CHECK(!fm_job_found(&g_job, &found));
}

TEST_CASE(a_file_gone_since_its_sum_fails_changed)
{
    fresh();
    fk_dir(&g_card, "D");
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31, 0u), FM_OK);
    CHECK_EQ(run(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_CHANGED);
    CHECK(fk_get(&g_card, "D/NOTE.TXT") == NULL);
    CHECK(fk_get(&g_card, "D/NOTE.TXT.NEW") == NULL);
    CHECK_EQ(g_card.calls[FK_OPEN], 0);
    CHECK(clean());
    /* Gone between the stat and the open: the same answer. */
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, 10u);
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31,
                          sum_of("D/NOTE.TXT")), FM_OK);
    fk_fail(&g_card, FK_OPEN, 1, FM_ERR_NOT_FOUND);
    CHECK_EQ(run(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_CHANGED);
    CHECK(fk_get(&g_card, "D/NOTE.TXT.NEW") == NULL);
    CHECK(clean());
}

TEST_CASE(absent_refuses_a_taken_name_and_makes_a_free_one)
{
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, 10u);
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_ABSENT, NULL, 0u), FM_OK);
    CHECK_EQ(run(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_EXISTS);
    CHECK(fk_holds_pattern(&g_card, "D/NOTE.TXT", "D/NOTE.TXT", 10u));
    CHECK(fk_get(&g_card, "D/NOTE.TXT.NEW") == NULL);
    CHECK(clean());
    /* A folder of the name is taken too, and stays a folder. */
    fresh();
    fk_dir(&g_card, "D/NOTE.TXT");
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_ABSENT, NULL, 0u), FM_OK);
    CHECK_EQ(run(), FM_JOB_FAILED);
    CHECK(fk_get(&g_card, "D/NOTE.TXT")->dir);
    /* The name free: made. */
    fresh();
    fk_dir(&g_card, "D");
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_ABSENT, NULL, 0u), FM_OK);
    CHECK_EQ(run(), FM_JOB_DONE);
    CHECK(holds_text());
    CHECK(!g_job.dst_was);
    CHECK(clean());
}

TEST_CASE(armed_or_cancel_while_summing_stop_with_no_new)
{
    for (int how = 0; how < 3; ++how) {
        fresh();
        fk_file(&g_card, "D/NOTE.TXT", NULL, OLD_SIZE);
        save();
        CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31,
                              sum_of("D/NOTE.TXT")), FM_OK);
        CHECK_EQ(step(), FM_JOB_RUNNING);    /* opened */
        CHECK_EQ(step(), FM_JOB_RUNNING);    /* chunk 1 summed */
        if (how == 0) {
            g_bench.armed = true;
        } else if (how == 1) {
            fm_job_cancel(&g_job);
        } else {
            static const log_marks_t marks = { .run_dir = CARD_AUTO_LOGS,
                                               .run = 4u };
            g_bench.marks = &marks;
        }
        CHECK_EQ(step(), FM_JOB_STOPPED);
        CHECK_EQ(g_job.err, (how == 0) ? FM_ERR_ARMED
                            : (how == 1) ? FM_ERR_CANCELLED
                                         : FM_ERR_LOGGING);
        CHECK(fk_holds_pattern(&g_card, "D/NOTE.TXT", "D/NOTE.TXT",
                               OLD_SIZE));
        CHECK(fk_get(&g_card, "D/NOTE.TXT.NEW") == NULL);
        CHECK_EQ(g_card.calls[FK_WRITE], 0);
        CHECK(clean());
    }
}

TEST_CASE(a_card_pulled_while_summing_fails_card_gone)
{
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, OLD_SIZE);
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31,
                          sum_of("D/NOTE.TXT")), FM_OK);
    CHECK_EQ(step(), FM_JOB_RUNNING);
    fk_fail(&g_card, FK_READ, 2, FM_ERR_CARD_GONE);
    CHECK_EQ(run(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_CARD_GONE);
    CHECK(fk_holds_pattern(&g_card, "D/NOTE.TXT", "D/NOTE.TXT", OLD_SIZE));
    CHECK(fk_get(&g_card, "D/NOTE.TXT.NEW") == NULL);
    CHECK_EQ(fk_open_files(&g_card), 0);
    /* Pulled between two steps: the next step finds another card. */
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, OLD_SIZE);
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31,
                          sum_of("D/NOTE.TXT")), FM_OK);
    CHECK_EQ(step(), FM_JOB_RUNNING);
    CHECK_EQ(step(), FM_JOB_RUNNING);
    fk_pull(&g_card);
    fk_insert(&g_card);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_CARD_GONE);
    CHECK_EQ(fk_open_files(&g_card), 0);
    CHECK(fk_get(&g_card, "D/NOTE.TXT.NEW") == NULL);
}

TEST_CASE(a_new_there_already_is_kept_after_the_sum)
{
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, 10u);
    fk_file(&g_card, "D/NOTE.TXT.NEW", NULL, 5u);
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31,
                          sum_of("D/NOTE.TXT")), FM_OK);
    CHECK_EQ(run(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_EXISTS);
    CHECK(fk_holds_pattern(&g_card, "D/NOTE.TXT.NEW", "D/NOTE.TXT.NEW",
                           5u));
    CHECK(fk_holds_pattern(&g_card, "D/NOTE.TXT", "D/NOTE.TXT", 10u));
    CHECK(clean());
}

TEST_CASE(the_check_is_refused_for_other_jobs_and_after_a_step)
{
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, 10u);
    CHECK_EQ(fm_job_match(NULL, FM_MATCH_ANY, NULL, 0u), FM_ERR_ARG);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "D", "NOTE.TXT", ""), FM_OK);
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_ABSENT, NULL, 0u), FM_ERR_ARG);
    CHECK_EQ(g_job.match, FM_MATCH_ANY);
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, NULL, 0u), FM_ERR_ARG);
    CHECK_EQ(fm_job_match(&g_job, (fm_match_t)7, sum31, 0u), FM_ERR_ARG);
    CHECK_EQ(g_job.match, FM_MATCH_ANY);
    CHECK_EQ(step(), FM_JOB_RUNNING);
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_ABSENT, NULL, 0u), FM_ERR_ARG);
    CHECK_EQ(run(), FM_JOB_DONE);
    /* A save refused at its start takes no check. */
    g_bench.armed = true;
    CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, "D", "NOTE.TXT",
                         sizeof(g_text), fill_text, NULL), FM_ERR_ARMED);
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_ANY, NULL, 0u), FM_ERR_ARG);
}

/*
 * fm_job_sum(): a LOAD's sum is the file's, a SAVE's the bytes it wrote,
 * also with a sum checked first; refused for another job, without a sum
 * and after a step.
 */
TEST_CASE(a_load_and_a_save_sum_the_bytes_they_pass)
{
    static uint8_t room[OLD_SIZE];
    fresh();
    fk_file(&g_card, "D/NOTE.TXT", NULL, OLD_SIZE);
    CHECK_EQ(fm_job_load(&g_job, &g_bench, 1u, "D", "NOTE.TXT", room,
                         sizeof(room)), FM_OK);
    CHECK_EQ(fm_job_sum(&g_job, sum31), FM_OK);
    CHECK_EQ(run(), FM_JOB_DONE);
    CHECK_EQ(g_job.data_sum, sum_of("D/NOTE.TXT"));
    const uint32_t old = g_job.data_sum;
    save();
    CHECK_EQ(fm_job_match(&g_job, FM_MATCH_SUM, sum31, old), FM_OK);
    CHECK_EQ(fm_job_sum(&g_job, sum31), FM_OK);
    CHECK_EQ(run(), FM_JOB_DONE);
    CHECK(holds_text());
    CHECK_EQ(g_job.data_sum, sum31(0u, g_text, sizeof(g_text)));
    CHECK_EQ(g_job.data_sum, sum_of("D/NOTE.TXT"));
    CHECK(clean());
    /* Refusals leave the job as it was. */
    CHECK_EQ(fm_job_sum(NULL, sum31), FM_ERR_ARG);
    save();
    CHECK_EQ(fm_job_sum(&g_job, NULL), FM_ERR_ARG);
    CHECK_EQ(step(), FM_JOB_RUNNING);
    CHECK_EQ(fm_job_sum(&g_job, sum31), FM_ERR_ARG);
    CHECK(g_job.data_fn == NULL);
    CHECK_EQ(run(), FM_JOB_DONE);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "D", "NOTE.TXT", ""), FM_OK);
    CHECK_EQ(fm_job_sum(&g_job, sum31), FM_ERR_ARG);
}

int main(void)
{
    RUN(a_matching_sum_replaces_the_file);
    RUN(a_different_sum_fails_changed_and_writes_nothing);
    RUN(a_refusal_gives_the_sum_it_found);
    RUN(a_file_gone_since_its_sum_fails_changed);
    RUN(absent_refuses_a_taken_name_and_makes_a_free_one);
    RUN(armed_or_cancel_while_summing_stop_with_no_new);
    RUN(a_card_pulled_while_summing_fails_card_gone);
    RUN(a_new_there_already_is_kept_after_the_sum);
    RUN(the_check_is_refused_for_other_jobs_and_after_a_step);
    RUN(a_load_and_a_save_sum_the_bytes_they_pass);
    fk_free(&g_card);
    return test_summary("fm_match");
}
