/**
 * @file test_fm_req.c
 * @brief The runner of the FILES screen's requests (fm_req.h) over a card
 *        in memory (fm_fake.h): a page, a folder entered and left, INFO of
 *        a file and of a folder, a job to its end with the bench and CANCEL
 *        asked before each step, a look at the slot first, and the
 *        refusals of a request with nothing to run on.
 *
 * Cases:
 * - a_page_enter_and_up_run_the_pane_in_place
 * - the_log_folders_of_the_request_reach_the_rules
 * - info_reads_a_files_size_and_time
 * - info_counts_a_folders_entries_and_their_bytes
 * - info_of_a_name_on_another_card_or_another_kind_is_refused
 * - a_job_runs_to_its_end_and_reports_its_bytes
 * - the_bench_is_asked_before_every_step_of_a_copy
 * - cancel_stops_a_copy_at_its_next_chunk
 * - a_job_refused_at_its_start_keeps_its_refusal
 * - a_look_comes_before_the_request_only_when_asked
 * - a_request_with_nothing_to_run_on_is_refused
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "fm_fake.h"
#include "fm_ops.h"
#include "fm_req.h"

static fk_card_t g_card;
static fm_io_t g_io;
static fm_req_t g_req;
static uint8_t g_chunk[FM_CHUNK];
static int g_benches;          /* bench() calls                         */
static int g_arm_after;        /* ARMED from this bench() call on; -1   */
static int g_cancel_after;     /* CANCEL from this bench() call on; -1  */
static int g_looks;            /* look() calls                          */
static int g_progress;         /* progress() calls                      */
static uint64_t g_done;        /* the last progress                     */
static uint64_t g_total;
static log_marks_t g_marks;

/** @brief bench(): SAFE until g_arm_after calls, the link up. */
static void bench(fm_bench_t *out, void *ctx)
{
    (void)ctx;
    ++g_benches;
    out->armed = g_arm_after >= 0 && g_benches > g_arm_after;
    out->link_up = true;
    out->marks = &g_marks;
}

/** @brief cancelled(): from g_cancel_after bench() calls on. */
static bool cancelled(void *ctx)
{
    (void)ctx;
    return g_cancel_after >= 0 && g_benches > g_cancel_after;
}

/** @brief progress(): counted, the last kept. */
static void progress(uint64_t done, uint64_t total, void *ctx)
{
    (void)ctx;
    ++g_progress;
    g_done = done;
    g_total = total;
}

/** @brief look(): counted. */
static void look(void *ctx)
{
    (void)ctx;
    ++g_looks;
}

/** @brief The runner over the card. */
static fm_runner_t runner(void)
{
    const fm_runner_t r = {
        .io = &g_io, .bench = bench, .cancelled = cancelled,
        .progress = progress, .look = look, .chunk = g_chunk,
        .size = sizeof(g_chunk), .ctx = NULL,
    };
    return r;
}

/** @brief Run g_req. */
static fm_err_t run(void)
{
    const fm_runner_t r = runner();
    return fm_req_run(&g_req, &r);
}

/** @brief A fresh card with a few folders and files, nothing asked yet. */
static void fresh(void)
{
    fk_free(&g_card);
    fk_init(&g_card);
    g_io = fk_io(&g_card);
    memset(&g_req, 0, sizeof(g_req));
    memset(&g_marks, 0, sizeof(g_marks));
    g_benches = 0;
    g_arm_after = -1;
    g_cancel_after = -1;
    g_looks = 0;
    g_progress = 0;
    g_done = g_total = 0u;
    fk_dir(&g_card, "USER/LOGS");
    fk_dir(&g_card, "LOGS2");
    fk_file(&g_card, "USER/LOGS/LIMIT006.TXT", NULL, 1873u);
    fk_file(&g_card, "USER/LOGS/ATEST005.CSV", NULL, 15104u);
    fk_file(&g_card, "USER/LOGS/.DS_Store", NULL, 6148u);
    fk_file(&g_card, "BIG.CSV", NULL, 3u * FM_CHUNK + 5u);
}

TEST_CASE(a_page_enter_and_up_run_the_pane_in_place)
{
    fresh();
    g_req.kind = FM_REQ_PAGE;
    fm_tree_init(&g_req.tree, CARD_ROOT);
    g_req.go = FM_GO_FIRST;
    CHECK_EQ(run(), FM_OK);
    CHECK_EQ(g_req.err, FM_OK);
    CHECK_EQ(g_req.tree.held, 3);       /* LOGS2, USER, BIG.CSV */
    CHECK_STR_EQ(g_req.tree.page[0].name, "LOGS2");
    CHECK_STR_EQ(g_req.tree.page[1].name, "USER");
    CHECK_EQ(g_benches, 1);

    g_req.kind = FM_REQ_ENTER;
    g_req.row = 1;
    CHECK_EQ(run(), FM_OK);
    CHECK_STR_EQ(g_req.tree.dir, "USER");
    CHECK_EQ(g_req.tree.held, 1);
    g_req.row = 0;
    CHECK_EQ(run(), FM_OK);
    CHECK_STR_EQ(g_req.tree.dir, "USER/LOGS");
    CHECK_EQ(g_req.tree.held, 2);
    CHECK_EQ(g_req.tree.unlisted, 1u);  /* .DS_Store, counted */

    g_req.kind = FM_REQ_UP;
    CHECK_EQ(run(), FM_OK);
    CHECK_STR_EQ(g_req.tree.dir, "USER");
    CHECK_EQ(run(), FM_OK);
    CHECK_STR_EQ(g_req.tree.dir, "");
    /* Up from the root is refused, and the pane stays. */
    CHECK_EQ(run(), FM_ERR_ARG);
    CHECK_STR_EQ(g_req.tree.dir, "");
    /* Enter on a file's row is refused. */
    g_req.kind = FM_REQ_ENTER;
    g_req.row = 2;
    CHECK_EQ(run(), FM_ERR_ARG);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(the_log_folders_of_the_request_reach_the_rules)
{
    fresh();
    /* LOGS2 is the user log folder of this request: a place the bench
     * keeps. */
    g_req.kind = FM_REQ_PAGE;
    fm_tree_init(&g_req.tree, CARD_ROOT);
    memcpy(g_req.user_logs, "LOGS2", 6);
    CHECK_EQ(run(), FM_OK);
    CHECK_STR_EQ(g_req.tree.page[0].name, "LOGS2");
    CHECK_EQ(g_req.tree.page[0].place, FM_PLACE_FIXED);
    /* Without it, USER/LOGS is the user log folder and LOGS2 is free. */
    g_req.user_logs[0] = '\0';
    CHECK_EQ(run(), FM_OK);
    CHECK_EQ(g_req.tree.page[0].place, FM_PLACE_FREE);
    CHECK_EQ(g_req.tree.page[1].place, FM_PLACE_FIXED);  /* USER */

    fm_bench_t b;
    memset(&b, 0, sizeof(b));
    memcpy(g_req.auto_logs, "LOGS2", 6);
    fm_req_bench(&g_req, &b);
    CHECK(b.user_logs == NULL);
    CHECK_STR_EQ(b.auto_logs, "LOGS2");
    fm_req_bench(NULL, &b);
    fm_req_bench(&g_req, NULL);
}

TEST_CASE(info_reads_a_files_size_and_time)
{
    fresh();
    g_card.mtime = 1715949296;     /* 2024-05-17 12:34:56 */
    g_req.kind = FM_REQ_INFO;
    memcpy(g_req.dir, "USER/LOGS", 10);
    memcpy(g_req.name, "LIMIT006.TXT", 13);
    g_req.mount = g_card.mount;
    CHECK_EQ(run(), FM_OK);
    CHECK_EQ(g_req.info.size, 1873u);
    CHECK_EQ(g_req.info.mtime, 1715949296);
    CHECK_EQ(g_req.info.entries, 0u);
    /* A card that gives no time. */
    g_card.mtime = -1;
    CHECK_EQ(run(), FM_OK);
    CHECK_EQ(g_req.info.mtime, -1);
    /* A name no longer there. */
    memcpy(g_req.name, "LIMIT007.TXT", 13);
    CHECK_EQ(run(), FM_ERR_NOT_FOUND);
    CHECK_EQ(g_req.info.size, 0u);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(info_counts_a_folders_entries_and_their_bytes)
{
    fresh();
    g_req.kind = FM_REQ_INFO;
    memcpy(g_req.dir, "USER", 5);
    memcpy(g_req.name, "LOGS", 5);
    g_req.is_dir = true;
    g_req.mount = g_card.mount;
    CHECK_EQ(run(), FM_OK);
    CHECK_EQ(g_req.info.entries, 2u);
    CHECK_EQ(g_req.info.unlisted, 1u);
    CHECK_EQ(g_req.info.size, 1873u + 15104u);
    /* The root's folder USER: one folder, no bytes. */
    g_req.dir[0] = '\0';
    memcpy(g_req.name, "USER", 5);
    CHECK_EQ(run(), FM_OK);
    CHECK_EQ(g_req.info.entries, 1u);
    CHECK_EQ(g_req.info.size, 0u);
    /* A walk that fails is the answer. */
    fk_fail(&g_card, FK_WALK, 1, FM_ERR_IO);
    CHECK_EQ(run(), FM_ERR_IO);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(info_of_a_name_on_another_card_or_another_kind_is_refused)
{
    fresh();
    g_req.kind = FM_REQ_INFO;
    memcpy(g_req.name, "USER", 5);
    g_req.is_dir = false;            /* listed as a file, a folder now */
    g_req.mount = g_card.mount;
    CHECK_EQ(run(), FM_ERR_NOT_FOUND);
    g_req.is_dir = true;
    g_req.mount = g_card.mount + 1u; /* listed from another card */
    CHECK_EQ(run(), FM_ERR_CARD_GONE);
    fk_pull(&g_card);
    CHECK_EQ(run(), FM_ERR_NO_CARD);
    fk_insert(&g_card);
    /* A path the core refuses. */
    g_req.mount = g_card.mount;
    memcpy(g_req.name, "..", 3);
    CHECK_EQ(run(), FM_ERR_NAME);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(a_job_runs_to_its_end_and_reports_its_bytes)
{
    fresh();
    g_req.kind = FM_REQ_JOB;
    fm_bench_t b;
    memset(&b, 0, sizeof(b));
    b.link_up = true;
    CHECK_EQ(fm_job_copy(&g_req.job, &b, g_card.mount, "", "BIG.CSV",
                         "LOGS2"), FM_OK);
    CHECK_EQ(run(), FM_OK);
    CHECK_EQ(g_req.job.state, FM_JOB_DONE);
    CHECK(fk_holds_pattern(&g_card, "LOGS2/BIG.CSV", "BIG.CSV",
                           3u * FM_CHUNK + 5u));
    CHECK_EQ(g_done, 3u * FM_CHUNK + 5u);
    CHECK_EQ(g_total, 3u * FM_CHUNK + 5u);
    CHECK(g_progress >= 4);
    CHECK_EQ(g_card.holds, 0);
    CHECK_EQ(fk_open_files(&g_card), 0);
}

TEST_CASE(the_bench_is_asked_before_every_step_of_a_copy)
{
    fresh();
    g_req.kind = FM_REQ_JOB;
    CHECK_EQ(fm_job_copy(&g_req.job, NULL, g_card.mount, "", "BIG.CSV",
                         "LOGS2"), FM_OK);
    /* ARMED from the third step on: two chunks at most, then stopped, the
     * .NEW removed. */
    g_arm_after = 2;
    CHECK_EQ(run(), FM_ERR_ARMED);
    CHECK_EQ(g_req.job.state, FM_JOB_STOPPED);
    CHECK(fk_get(&g_card, "LOGS2/BIG.CSV") == NULL);
    CHECK(fk_get(&g_card, "LOGS2/BIG.CSV.NEW") == NULL);
    CHECK(fk_get(&g_card, "BIG.CSV") != NULL);
    CHECK(g_benches >= 3);
    CHECK(g_done < 3u * FM_CHUNK + 5u);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(cancel_stops_a_copy_at_its_next_chunk)
{
    for (int after = 0; after < 5; ++after) {
        fresh();
        g_req.kind = FM_REQ_JOB;
        CHECK_EQ(fm_job_copy(&g_req.job, NULL, g_card.mount, "", "BIG.CSV",
                             "LOGS2"), FM_OK);
        g_cancel_after = after;
        const fm_err_t err = run();
        if (g_req.job.state == FM_JOB_DONE) {
            /* The cancel came after the last chunk: the copy is whole. */
            CHECK_EQ(err, FM_OK);
            CHECK(fk_holds_pattern(&g_card, "LOGS2/BIG.CSV", "BIG.CSV",
                                   3u * FM_CHUNK + 5u));
        } else {
            CHECK_EQ(err, FM_ERR_CANCELLED);
            CHECK(fk_get(&g_card, "LOGS2/BIG.CSV") == NULL);
            CHECK(fk_get(&g_card, "LOGS2/BIG.CSV.NEW") == NULL);
        }
        CHECK_EQ(g_card.holds, 0);
        CHECK_EQ(fk_open_files(&g_card), 0);
    }
}

TEST_CASE(a_job_refused_at_its_start_keeps_its_refusal)
{
    fresh();
    g_req.kind = FM_REQ_JOB;
    fm_bench_t b;
    memset(&b, 0, sizeof(b));
    b.armed = true;
    CHECK_EQ(fm_job_copy(&g_req.job, &b, g_card.mount, "", "BIG.CSV",
                         "LOGS2"), FM_ERR_ARMED);
    CHECK_EQ(run(), FM_ERR_ARMED);
    CHECK_EQ(g_benches, 0);
    /* A job never started is refused as an argument. */
    memset(&g_req.job, 0, sizeof(g_req.job));
    CHECK_EQ(run(), FM_ERR_ARG);
    CHECK_EQ(g_card.calls[FK_ANY], 0);
}

TEST_CASE(a_look_comes_before_the_request_only_when_asked)
{
    fresh();
    g_req.kind = FM_REQ_PAGE;
    fm_tree_init(&g_req.tree, CARD_ROOT);
    CHECK_EQ(run(), FM_OK);
    CHECK_EQ(g_looks, 0);
    g_req.look = true;
    CHECK_EQ(run(), FM_OK);
    CHECK_EQ(g_looks, 1);
    /* A runner with no look runs the request all the same. */
    fm_runner_t r = runner();
    r.look = NULL;
    r.progress = NULL;
    r.cancelled = NULL;
    CHECK_EQ(fm_req_run(&g_req, &r), FM_OK);
    CHECK_EQ(g_looks, 1);
    g_req.kind = FM_REQ_JOB;
    CHECK_EQ(fm_job_mkdir(&g_req.job, NULL, g_card.mount, "", "NEW1"),
             FM_OK);
    CHECK_EQ(fm_req_run(&g_req, &r), FM_OK);
    CHECK(fk_get(&g_card, "NEW1") != NULL);
}

TEST_CASE(a_request_with_nothing_to_run_on_is_refused)
{
    fresh();
    CHECK_EQ(fm_req_run(NULL, NULL), FM_ERR_ARG);
    g_req.kind = FM_REQ_PAGE;
    CHECK_EQ(fm_req_run(&g_req, NULL), FM_ERR_ARG);
    CHECK_EQ(g_req.err, FM_ERR_ARG);
    fm_runner_t r = runner();
    r.io = NULL;
    CHECK_EQ(fm_req_run(&g_req, &r), FM_ERR_ARG);
    r = runner();
    r.bench = NULL;
    CHECK_EQ(fm_req_run(&g_req, &r), FM_ERR_ARG);
    g_req.kind = FM_REQ_NONE;
    CHECK_EQ(run(), FM_ERR_ARG);
    g_req.kind = (fm_req_kind_t)99;
    CHECK_EQ(run(), FM_ERR_ARG);
    CHECK_EQ(g_card.calls[FK_ANY], 0);
}

int main(void)
{
    RUN(a_page_enter_and_up_run_the_pane_in_place);
    RUN(the_log_folders_of_the_request_reach_the_rules);
    RUN(info_reads_a_files_size_and_time);
    RUN(info_counts_a_folders_entries_and_their_bytes);
    RUN(info_of_a_name_on_another_card_or_another_kind_is_refused);
    RUN(a_job_runs_to_its_end_and_reports_its_bytes);
    RUN(the_bench_is_asked_before_every_step_of_a_copy);
    RUN(cancel_stops_a_copy_at_its_next_chunk);
    RUN(a_job_refused_at_its_start_keeps_its_refusal);
    RUN(a_look_comes_before_the_request_only_when_asked);
    RUN(a_request_with_nothing_to_run_on_is_refused);
    fk_free(&g_card);
    return test_summary("fm_req");
}
