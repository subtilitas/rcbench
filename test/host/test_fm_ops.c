/**
 * @file test_fm_ops.c
 * @brief The file manager's jobs over a card in memory (fm_fake.h), and the
 *        rules they ask first (fm_rules.h): copies of 0B, 1B, 16,383B,
 *        16,384B, 16,385B and 1MiB; a copy stopped after each chunk, by
 *        CANCEL, by a full card, by a card pulled and by the gate; every
 *        overwrite refused; move, rename, delete and NEW at their refusals;
 *        the save sequence cut after the write, after the delete and after
 *        the rename; the load at its limit; the gate as a pure function.
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

/* ------------------------------------------------------------ the rigs */

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
 * @brief Step the job to its end.
 * @return the steps it took, -1 past 1,000
 */
static int run_out(void)
{
    int n = 0;
    while (fm_job_state(&g_job, NULL, NULL) == FM_JOB_RUNNING) {
        if (++n > 1000) {
            return -1;
        }
        (void)step();
    }
    return n;
}

/**
 * @brief Whether nothing is left open or held.
 * @return true for a clean card
 */
static bool clean(void)
{
    return g_card.holds == 0 && fk_open_files(&g_card) == 0;
}

/* ---------------------------------------------------------- the gate */

TEST_CASE(reads_pass_every_state_and_writes_only_safe_and_quiet)
{
    log_marks_t marks = { .run_dir = CARD_AUTO_LOGS,
                          .test_dir = CARD_USER_LOGS };
    fm_bench_t b = { .marks = &marks };
    for (int op = 0; op < FM_OP_COUNT; ++op) {
        CHECK_EQ(fm_gate((fm_op_t)op, &b), FM_OK);
        CHECK_EQ(fm_gate((fm_op_t)op, NULL), FM_OK);
    }
    static const fm_op_t writes[] = {
        FM_OP_COPY, FM_OP_MOVE, FM_OP_RENAME, FM_OP_DELETE, FM_OP_NEW,
        FM_OP_SAVE,
    };
    static const fm_op_t reads[] = { FM_OP_OPEN, FM_OP_INFO, FM_OP_EDIT };
    for (int k = 0; k < 4; ++k) {
        b.armed = (k == 0);
        marks.run = (k == 1) ? 3u : 0u;
        marks.test = (k == 2) ? 4u : 0u;
        marks.txt = (k == 3) ? 5u : 0u;
        const fm_err_t want = (k == 0) ? FM_ERR_ARMED : FM_ERR_LOGGING;
        for (size_t i = 0u; i < sizeof(writes) / sizeof(writes[0]); ++i) {
            CHECK_EQ(fm_gate(writes[i], &b), want);
            CHECK(fm_op_writes(writes[i]));
        }
        for (size_t i = 0u; i < sizeof(reads) / sizeof(reads[0]); ++i) {
            CHECK_EQ(fm_gate(reads[i], &b), FM_OK);
            CHECK(!fm_op_writes(reads[i]));
        }
    }
    /* ARMED with a run open says ARMED. */
    const log_marks_t open_run = { .run_dir = CARD_AUTO_LOGS, .run = 3u,
                                   .test_dir = CARD_USER_LOGS };
    b.armed = true;
    b.marks = &open_run;
    CHECK_EQ(fm_gate(FM_OP_DELETE, &b), FM_ERR_ARMED);
    CHECK(!fm_op_writes(FM_OP_COUNT));
    CHECK(!fm_logging(NULL));
    b.marks = NULL;
    b.armed = false;
    CHECK_EQ(fm_gate(FM_OP_DELETE, &b), FM_OK);
}

TEST_CASE(each_place_allows_what_it_allows)
{
    fresh();
    /* A fixed folder: entered, never renamed, moved or deleted. */
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_OPEN, "", "BENCH", true), FM_OK);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_RENAME, "", "BENCH", true),
             FM_ERR_FIXED);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_MOVE, "", "USER", true),
             FM_ERR_FIXED);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_DELETE, "USER", "LOGS", true),
             FM_ERR_FIXED);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_NEW, "USER", "LOGS", true), FM_OK);
    /* The journal: INFO only. */
    for (int op = 0; op < FM_OP_COUNT; ++op) {
        if (op == FM_OP_NEW) {
            continue;
        }
        CHECK_EQ(fm_allowed(&g_bench, (fm_op_t)op, CARD_KST_CONF, "KST.NEW",
                            false),
                 (op == FM_OP_INFO) ? FM_OK : FM_ERR_LOCKED);
    }
    /* The fault log: viewed always, copied and deleted while the link is
     * up, never edited, renamed or moved. */
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_OPEN, CARD_BENCH, "RCBENCH.LOG",
                        false), FM_OK);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_DELETE, CARD_BENCH, "RCBENCH.LOG",
                        false), FM_OK);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_COPY, CARD_BENCH, "RCBENCH.OLD",
                        false), FM_OK);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_EDIT, CARD_BENCH, "RCBENCH.LOG",
                        false), FM_ERR_FIXED);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_RENAME, CARD_BENCH, "RCBENCH.LOG",
                        false), FM_ERR_FIXED);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_MOVE, CARD_BENCH, "RCBENCH.TMP",
                        false), FM_ERR_FIXED);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_SAVE, CARD_BENCH, "RCBENCH.TMP",
                        false), FM_ERR_FIXED);
    g_bench.link_up = false;
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_DELETE, CARD_BENCH, "RCBENCH.LOG",
                        false), FM_ERR_BUSY);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_COPY, CARD_BENCH, "RCBENCH.LOG",
                        false), FM_ERR_BUSY);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_OPEN, CARD_BENCH, "RCBENCH.LOG",
                        false), FM_OK);
    CHECK_EQ(fm_allowed(NULL, FM_OP_DELETE, CARD_BENCH, "RCBENCH.LOG",
                        false), FM_ERR_BUSY);
    /* A busy file: INFO only.  Kinds a job cannot take. */
    const log_marks_t marks = { .run_dir = CARD_AUTO_LOGS, .run = 7u,
                                .test_dir = CARD_USER_LOGS };
    const fm_bench_t busy = { .marks = &marks };
    CHECK_EQ(fm_allowed(&busy, FM_OP_OPEN, CARD_AUTO_LOGS, "MOTOR007.CSV",
                        false), FM_ERR_BUSY);
    CHECK_EQ(fm_allowed(&busy, FM_OP_INFO, CARD_AUTO_LOGS, "MOTOR007.CSV",
                        false), FM_OK);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_COPY, "", "DIR", true), FM_ERR_ARG);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_EDIT, "", "DIR", true), FM_ERR_ARG);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_NEW, "", "FILE.TXT", false),
             FM_ERR_ARG);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_NEW, "", "", false), FM_OK);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_OPEN, "", "", false), FM_ERR_ARG);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_COUNT, "", "A", false), FM_ERR_ARG);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_OPEN, NULL, "A", false), FM_ERR_ARG);
    /* The gate answers before the place. */
    g_bench.armed = true;
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_RENAME, "", "BENCH", true),
             FM_ERR_ARMED);
    CHECK_EQ(fm_allowed(&g_bench, FM_OP_INFO, "", "BENCH", true), FM_OK);
}

TEST_CASE(a_new_name_takes_59_bytes_and_no_bench_name)
{
    char n59[64];
    char n60[64];
    memset(n59, 'N', 55u);
    memcpy(n59 + 55, ".TXT", 5u);
    memset(n60, 'N', 56u);
    memcpy(n60 + 56, ".TXT", 5u);
    CHECK_EQ(FM_NAME_MAX, 59);
    for (int typed = 0; typed < 2; ++typed) {
        const bool t = (typed != 0);
        CHECK_EQ(fm_target_check(NULL, "", n59, t), FM_OK);
        CHECK_EQ(fm_target_check(NULL, "", n60, t), FM_ERR_NAME);
        CHECK_EQ(fm_target_check(NULL, "", "", t), FM_ERR_NAME);
        CHECK_EQ(fm_target_check(NULL, "", "A/B", t), FM_ERR_NAME);
        CHECK_EQ(fm_target_check(NULL, "", ".", t), FM_ERR_NAME);
        CHECK_EQ(fm_target_check(NULL, "", "..", t), FM_ERR_NAME);
        CHECK_EQ(fm_target_check(NULL, CARD_KST_CONF, "kst.txt", t),
                 FM_ERR_LOCKED);
        CHECK_EQ(fm_target_check(NULL, CARD_BENCH, "RCBENCH.TMP", t),
                 FM_ERR_FIXED);
        CHECK_EQ(fm_target_check(NULL, "", "BENCH", t), FM_OK);
        CHECK_EQ(fm_target_check(NULL, NULL, "A", t), FM_ERR_ARG);
    }
    /* A typed name follows fm_name_check(); a name carried over from the
     * card keeps the characters a computer gave it. */
    CHECK_EQ(fm_target_check(NULL, "", "A:B.TXT", true), FM_ERR_NAME);
    CHECK_EQ(fm_target_check(NULL, "", "A:B.TXT", false), FM_OK);
    CHECK_EQ(fm_target_check(NULL, "", "CON.TXT", true), FM_ERR_NAME);
    CHECK_EQ(fm_target_check(NULL, "", " A.TXT", true), FM_ERR_NAME);
    /* A path whose ".NEW" does not fit in FM_PATH_MAX (120) bytes. */
    char dir[FM_PATH_SIZE];
    memset(dir, 'D', 110u);
    dir[110] = '\0';
    CHECK_EQ(fm_target_check(NULL, dir, "ABCDEFGHI", false), FM_ERR_NAME);
    CHECK_EQ(fm_target_check(NULL, dir, "ABCDE", false), FM_OK);
    CHECK_EQ(fm_target_check(NULL, "/A", "B", false), FM_ERR_NAME);
    /* Path answers, as the jobs read them. */
    CHECK_EQ(fm_path_err(FM_PATH_OK), FM_OK);
    CHECK_EQ(fm_path_err(FM_PATH_ARG), FM_ERR_ARG);
    CHECK_EQ(fm_path_err(FM_PATH_BAD), FM_ERR_NAME);
    CHECK_EQ(fm_path_err(FM_PATH_LONG), FM_ERR_NAME);
    CHECK_EQ(fm_path_err(FM_PATH_DEEP), FM_ERR_NAME);
    CHECK(fm_path_within("USER", "user/logs"));
    CHECK(fm_path_within("USER", "USER"));
    CHECK(!fm_path_within("USER", "USERS"));
    CHECK(!fm_path_within("USER/LOGS", "USER"));
    CHECK(!fm_path_within("USER", "UZER/LOGS"));
    CHECK(fm_path_within("", "ANY"));
    CHECK(!fm_path_within(NULL, "ANY"));
    CHECK(!fm_same_path(NULL, ""));
    CHECK(fm_read_at_start(CARD_ESC_CONF, "yge.JSON"));
    CHECK(!fm_read_at_start(CARD_ESC_CONF, "yge.txt"));
    CHECK(!fm_read_at_start("", "yge.json"));
    CHECK(!fm_read_at_start(CARD_ESC_CONF, NULL));
}

/* -------------------------------------------------------------- copy */

TEST_CASE(a_copy_of_every_size_arrives_whole_through_its_new)
{
    static const size_t sizes[] = {
        0u, 1u, 16383u, 16384u, 16385u, 1048576u,
    };
    for (size_t s = 0u; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        fresh();
        fk_file(&g_card, "USER/LOGS/A.TXT", NULL, sizes[s]);
        fk_dir(&g_card, "COPIES");
        CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, CARD_USER_LOGS, "A.TXT",
                             "COPIES"), FM_OK);
        uint64_t done = 1u;
        uint64_t total = 1u;
        CHECK_EQ(fm_job_state(&g_job, &done, &total), FM_JOB_RUNNING);
        CHECK_EQ(done, 0);
        CHECK_EQ(step(), FM_JOB_RUNNING);        /* checked and opened */
        CHECK(fk_get(&g_card, "COPIES/A.TXT.NEW") != NULL);
        const int steps = run_out();
        /* The chunks, the read that finds the end, sync, rename. */
        const int chunks = (int)((sizes[s] + FM_CHUNK - 1u) / FM_CHUNK);
        CHECK_EQ(steps, chunks + 1 + 2);
        CHECK_EQ(fm_job_state(&g_job, &done, &total), FM_JOB_DONE);
        CHECK_EQ(g_job.err, FM_OK);
        CHECK_EQ(done, sizes[s]);
        CHECK_EQ(total, sizes[s]);
        CHECK(fk_holds_pattern(&g_card, "COPIES/A.TXT", "USER/LOGS/A.TXT",
                               sizes[s]));
        CHECK(fk_holds_pattern(&g_card, "USER/LOGS/A.TXT", "USER/LOGS/A.TXT",
                               sizes[s]));
        CHECK(fk_get(&g_card, "COPIES/A.TXT.NEW") == NULL);
        CHECK(clean());
        /* A finished job steps no more. */
        CHECK_EQ(step(), FM_JOB_DONE);
    }
}

TEST_CASE(a_copy_cancelled_after_each_chunk_leaves_no_new)
{
    const size_t size = 4u * FM_CHUNK + 5u;
    for (int stop = 0; stop <= 5; ++stop) {
        fresh();
        fk_file(&g_card, "SRC.TXT", NULL, size);
        fk_dir(&g_card, "DST");
        CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "DST"),
                 FM_OK);
        CHECK_EQ(step(), FM_JOB_RUNNING);
        for (int k = 0; k < stop; ++k) {
            CHECK_EQ(step(), FM_JOB_RUNNING);
        }
        fm_job_cancel(&g_job);
        CHECK_EQ(step(), FM_JOB_STOPPED);
        CHECK_EQ(g_job.err, FM_ERR_CANCELLED);
        CHECK(fk_get(&g_card, "DST/SRC.TXT.NEW") == NULL);
        CHECK(fk_get(&g_card, "DST/SRC.TXT") == NULL);
        CHECK(fk_holds_pattern(&g_card, "SRC.TXT", "SRC.TXT", size));
        CHECK(clean());
    }
    /* Cancelled before its first step: nothing opened, nothing made. */
    fresh();
    fk_file(&g_card, "SRC.TXT", NULL, 10u);
    fk_dir(&g_card, "DST");
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "DST"), FM_OK);
    fm_job_cancel(&g_job);
    CHECK_EQ(step(), FM_JOB_STOPPED);
    CHECK_EQ(g_card.calls[FK_OPEN], 0);
    fm_job_cancel(NULL);
}

TEST_CASE(a_card_full_at_chunk_n_removes_the_new)
{
    const size_t size = 3u * FM_CHUNK;
    for (int n = 1; n <= 3; ++n) {
        fresh();
        fk_file(&g_card, "SRC.TXT", NULL, size);
        fk_dir(&g_card, "DST");
        g_card.free_bytes = (uint64_t)(n - 1) * FM_CHUNK + 100u;
        CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "DST"),
                 FM_OK);
        (void)run_out();
        CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_FAILED);
        CHECK_EQ(g_job.err, FM_ERR_FULL);
        CHECK_EQ(g_job.done, (uint64_t)(n - 1) * FM_CHUNK);
        CHECK(fk_get(&g_card, "DST/SRC.TXT.NEW") == NULL);
        CHECK(fk_get(&g_card, "DST/SRC.TXT") == NULL);
        CHECK(clean());
    }
}

TEST_CASE(a_card_pulled_at_chunk_n_fails_and_keeps_its_partial_new)
{
    const size_t size = 3u * FM_CHUNK;
    for (int n = 1; n <= 3; ++n) {
        fresh();
        fk_file(&g_card, "SRC.TXT", NULL, size);
        fk_dir(&g_card, "DST");
        CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "DST"),
                 FM_OK);
        fk_fail(&g_card, FK_WRITE, n, FM_ERR_CARD_GONE);
        (void)run_out();
        CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_FAILED);
        CHECK_EQ(g_job.err, FM_ERR_CARD_GONE);
        /* The card went with the write: its ".NEW" stays on it. */
        const fk_node_t *left = fk_get(&g_card, "DST/SRC.TXT.NEW");
        CHECK(left != NULL);
        if (left != NULL) {
            CHECK_EQ(left->size, (size_t)(n - 1) * FM_CHUNK);
        }
        CHECK(fk_get(&g_card, "DST/SRC.TXT") == NULL);
        CHECK(clean());
    }
    /* Pulled between two steps: the hold is refused, nothing more is
     * called on the card, and the handles are let go. */
    fresh();
    fk_file(&g_card, "SRC.TXT", NULL, size);
    fk_dir(&g_card, "DST");
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "DST"), FM_OK);
    CHECK_EQ(step(), FM_JOB_RUNNING);
    CHECK_EQ(step(), FM_JOB_RUNNING);
    fk_pull(&g_card);
    const long unlinks = g_card.calls[FK_UNLINK];
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_NO_CARD);
    CHECK_EQ(g_card.calls[FK_UNLINK], unlinks);
    CHECK(clean());
}

TEST_CASE(a_job_on_another_card_writes_nothing_to_it)
{
    fresh();
    fk_file(&g_card, "SRC.TXT", NULL, 100u);
    fk_dir(&g_card, "DST");
    fk_pull(&g_card);
    fk_insert(&g_card);                 /* mount 2: the names were mount 1's */
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "DST"), FM_OK);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_CARD_GONE);
    CHECK_EQ(g_card.calls[FK_OPEN] + g_card.calls[FK_RENAME]
             + g_card.calls[FK_UNLINK], 0);
    CHECK(fk_get(&g_card, "DST/SRC.TXT.NEW") == NULL);
}

TEST_CASE(a_copy_that_fails_part_way_removes_its_new)
{
    /* A read, the sync, the close and the final rename, each failing. */
    static const fk_call_t calls[] = { FK_READ, FK_SYNC, FK_CLOSE,
                                       FK_RENAME };
    for (size_t c = 0u; c < sizeof(calls) / sizeof(calls[0]); ++c) {
        fresh();
        fk_file(&g_card, "SRC.TXT", NULL, 2u * FM_CHUNK);
        fk_dir(&g_card, "DST");
        CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "DST"),
                 FM_OK);
        fk_fail(&g_card, calls[c], (calls[c] == FK_READ) ? 2 : 1, FM_ERR_IO);
        (void)run_out();
        CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_FAILED);
        CHECK_EQ(g_job.err, FM_ERR_IO);
        CHECK(fk_get(&g_card, "DST/SRC.TXT.NEW") == NULL);
        CHECK(fk_get(&g_card, "DST/SRC.TXT") == NULL);
        CHECK(fk_holds_pattern(&g_card, "SRC.TXT", "SRC.TXT",
                               2u * FM_CHUNK));
        CHECK(clean());
    }
}

TEST_CASE(a_copy_overwrites_nothing)
{
    fresh();
    fk_file(&g_card, "SRC.TXT", NULL, 100u);
    fk_file(&g_card, "DST/SRC.TXT", NULL, 7u);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "DST"), FM_OK);
    CHECK_EQ(run_out(), 1);
    CHECK_EQ(g_job.err, FM_ERR_EXISTS);
    CHECK(fk_holds_pattern(&g_card, "DST/SRC.TXT", "DST/SRC.TXT", 7u));
    /* Case folded, as FAT finds it. */
    fresh();
    fk_file(&g_card, "SRC.TXT", NULL, 100u);
    fk_file(&g_card, "dst/src.txt", NULL, 7u);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "DST"), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_EXISTS);
    /* A ".NEW" there already may be the only whole copy of a save. */
    fresh();
    fk_file(&g_card, "SRC.TXT", NULL, 100u);
    fk_file(&g_card, "DST/SRC.TXT.NEW", NULL, 9u);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "DST"), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_EXISTS);
    CHECK(fk_holds_pattern(&g_card, "DST/SRC.TXT.NEW", "DST/SRC.TXT.NEW",
                           9u));
    CHECK(fk_get(&g_card, "DST/SRC.TXT") == NULL);
    CHECK(clean());
}

TEST_CASE(a_copy_is_refused_where_the_rules_refuse_it)
{
    fresh();
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "A", "X.TXT", "a"),
             FM_ERR_SAME);
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_FAILED);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, CARD_KST_CONF, "KST.TXT", ""),
             FM_ERR_LOCKED);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "KST.TXT", CARD_KST_CONF),
             FM_ERR_LOCKED);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "RCBENCH.LOG",
                         CARD_BENCH), FM_ERR_FIXED);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, CARD_BENCH, "RCBENCH.LOG",
                         ""), FM_OK);
    g_bench.link_up = false;
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, CARD_BENCH, "RCBENCH.LOG",
                         ""), FM_ERR_BUSY);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", NULL, "A"), FM_ERR_ARG);
    CHECK_EQ(fm_job_copy(NULL, &g_bench, 1u, "", "A", "B"), FM_ERR_ARG);
    /* A name of 63 characters is listed and has no room for ".NEW". */
    char n63[64];
    memset(n63, 'L', 59u);
    memcpy(n63 + 59, ".TXT", 5u);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", n63, "A"), FM_ERR_NAME);
    const log_marks_t marks = { .run_dir = CARD_AUTO_LOGS, .run = 7u,
                                .test_dir = CARD_USER_LOGS };
    const fm_bench_t busy = { .marks = &marks, .link_up = true };
    CHECK_EQ(fm_job_copy(&g_job, &busy, 1u, CARD_AUTO_LOGS, "MOTOR007.CSV",
                         ""), FM_ERR_LOGGING);
    g_bench.armed = true;
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "A.TXT", "B"),
             FM_ERR_ARMED);
    CHECK_EQ(g_job.err, FM_ERR_ARMED);
    /* A copy of a folder, or into one not there. */
    fresh();
    fk_dir(&g_card, "DIR");
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "DIR", "B"), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_ARG);
    fk_file(&g_card, "A.TXT", NULL, 5u);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "A.TXT", "NOWHERE"),
             FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_NOT_FOUND);
    CHECK(clean());
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "GONE.TXT", "DIR"),
             FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_NOT_FOUND);
}

TEST_CASE(a_full_folder_takes_no_copy_and_no_new_entry)
{
    fresh();
    fk_file(&g_card, "SRC.TXT", NULL, 100u);
    fk_file(&g_card, "FULL/ONE.TXT", NULL, 1u);
    fk_file(&g_card, "FULL/TWO.TXT", NULL, 1u);
    g_card.dir_cap = 2;
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "FULL"),
             FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_FULL);
    CHECK(fk_get(&g_card, "FULL/SRC.TXT.NEW") == NULL);
    CHECK_EQ(fm_job_mkdir(&g_job, &g_bench, 1u, "FULL", "SUB"), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_FULL);
    CHECK_EQ(fm_job_create(&g_job, &g_bench, 1u, "FULL", "N.TXT"), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_FULL);
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "", "SRC.TXT", false, "FULL"),
             FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_FULL);
    CHECK(fk_get(&g_card, "SRC.TXT") != NULL);
    CHECK(clean());
}

TEST_CASE(the_gate_stops_a_copy_at_its_next_chunk_and_a_queued_rename)
{
    for (int why = 0; why < 2; ++why) {
        fresh();
        log_marks_t marks = { .run_dir = CARD_AUTO_LOGS,
                              .test_dir = CARD_USER_LOGS };
        g_bench.marks = &marks;
        fk_file(&g_card, "SRC.TXT", NULL, 3u * FM_CHUNK);
        fk_dir(&g_card, "DST");
        CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "DST"),
                 FM_OK);
        CHECK_EQ(step(), FM_JOB_RUNNING);
        CHECK_EQ(step(), FM_JOB_RUNNING);
        if (why == 0) {
            g_bench.armed = true;
        } else {
            marks.run = 12u;
        }
        CHECK_EQ(step(), FM_JOB_STOPPED);
        CHECK_EQ(g_job.err, (why == 0) ? FM_ERR_ARMED : FM_ERR_LOGGING);
        CHECK_EQ(g_job.done, FM_CHUNK);
        CHECK(fk_get(&g_card, "DST/SRC.TXT.NEW") == NULL);
        CHECK(clean());
    }
    /* ARMED between the start and the first step: stopped before the
     * card is written. */
    fresh();
    fk_file(&g_card, "SRC.TXT", NULL, 10u);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "SRC.TXT", "DST"), FM_OK);
    g_bench.armed = true;
    CHECK_EQ(step(), FM_JOB_STOPPED);
    CHECK_EQ(g_card.calls[FK_OPEN], 0);
    /* A rename queued SAFE and run ARMED is refused before its call;
     * started ARMED, refused at once. */
    fresh();
    fk_file(&g_card, "OLD.TXT", NULL, 10u);
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "", "OLD.TXT", false,
                           "NEW.TXT"), FM_OK);
    g_bench.armed = true;
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_ARMED);
    CHECK(fk_get(&g_card, "OLD.TXT") != NULL);
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "", "OLD.TXT", false,
                           "B.TXT"), FM_ERR_ARMED);
    CHECK_EQ(step(), FM_JOB_FAILED);
}

/* ------------------------------------------------- move, rename, delete */

TEST_CASE(move_goes_across_folders_and_never_onto_a_name)
{
    fresh();
    fk_file(&g_card, "A/F.TXT", NULL, 50u);
    fk_dir(&g_card, "B");
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "A", "F.TXT", false, "B"),
             FM_OK);
    CHECK_EQ(run_out(), 1);
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK(fk_get(&g_card, "A/F.TXT") == NULL);
    CHECK(fk_holds_pattern(&g_card, "B/F.TXT", "A/F.TXT", 50u));
    CHECK_EQ(g_card.calls[FK_WRITE], 0);
    /* Onto a taken name: both stay. */
    fk_file(&g_card, "A/F.TXT", NULL, 20u);
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "A", "F.TXT", false, "B"),
             FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_EXISTS);
    CHECK(fk_holds_pattern(&g_card, "A/F.TXT", "A/F.TXT", 20u));
    CHECK(fk_holds_pattern(&g_card, "B/F.TXT", "A/F.TXT", 50u));
    /* Into its own folder, a folder into itself or below it. */
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "A", "F.TXT", false, "a"),
             FM_ERR_SAME);
    fk_dir(&g_card, "C/D");
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "", "C", true, "C/D"),
             FM_ERR_SAME);
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "", "C", true, "C"),
             FM_ERR_SAME);
    /* A folder that holds entries stays; an empty one moves. */
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "", "C", true, "B"), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_NOT_EMPTY);
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "C", "D", true, "B"), FM_OK);
    (void)run_out();
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK(fk_get(&g_card, "B/D") != NULL && fk_get(&g_card, "B/D")->dir);
    /* The wrong kind, a fixed folder, a locked target. */
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "A", "F.TXT", true, "C"),
             FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_ARG);
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "", "USER", true, "B"),
             FM_ERR_FIXED);
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "A", "KST.NEW", false,
                         CARD_KST_CONF), FM_ERR_LOCKED);
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "A", "RCBENCH.OLD", false,
                         CARD_BENCH), FM_ERR_FIXED);
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "A", "", false, "B"),
             FM_ERR_ARG);
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "A", NULL, false, "B"),
             FM_ERR_ARG);
    CHECK_EQ(fm_job_move(NULL, &g_bench, 1u, "A", "F", false, "B"),
             FM_ERR_ARG);
    CHECK(clean());
}

TEST_CASE(a_moved_name_of_63_characters_keeps_it)
{
    fresh();
    char n63[64];
    char path[80];
    memset(n63, 'L', 59u);
    memcpy(n63 + 59, ".TXT", 5u);
    snprintf(path, sizeof(path), "A/%s", n63);
    fk_file(&g_card, path, NULL, 5u);
    fk_dir(&g_card, "B");
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "A", n63, false, "B"), FM_OK);
    (void)run_out();
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    char n64[66];
    memset(n64, 'L', 60u);
    memcpy(n64 + 60, ".TXT", 5u);
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "A", n64, false, "B"),
             FM_ERR_NAME);
}

TEST_CASE(a_refused_rename_leaves_both_names_as_they_were)
{
    fresh();
    fk_file(&g_card, "D/ONE.TXT", NULL, 11u);
    fk_file(&g_card, "D/TWO.TXT", NULL, 22u);
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "D", "ONE.TXT", false,
                           "two.txt"), FM_OK);
    (void)run_out();
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_EXISTS);
    CHECK(fk_holds_pattern(&g_card, "D/ONE.TXT", "D/ONE.TXT", 11u));
    CHECK(fk_holds_pattern(&g_card, "D/TWO.TXT", "D/TWO.TXT", 22u));
    /* The card refuses: a write-protected card. */
    fk_fail(&g_card, FK_RENAME, 1, FM_ERR_DENIED);
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "D", "ONE.TXT", false,
                           "THREE.TXT"), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_DENIED);
    CHECK(fk_get(&g_card, "D/ONE.TXT") != NULL);
    CHECK(fk_get(&g_card, "D/THREE.TXT") == NULL);
    /* A change of case is the same entry. */
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "D", "ONE.TXT", false,
                           "One.txt"), FM_OK);
    (void)run_out();
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK_STR_EQ(fk_get(&g_card, "D/ONE.TXT")->path, "D/One.txt");
    /* Refusals before the card: same name, fixed, locked, 60 characters,
     * a vanished source. */
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "D", "One.txt", false,
                           "One.txt"), FM_ERR_ARG);
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "", "BENCH", true, "B2"),
             FM_ERR_FIXED);
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, CARD_KST_CONF, "A.TXT",
                           false, "KST.TXT"), FM_ERR_LOCKED);
    char n60[64];
    memset(n60, 'N', 56u);
    memcpy(n60 + 56, ".TXT", 5u);
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "D", "One.txt", false, n60),
             FM_ERR_NAME);
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "D", "GONE.TXT", false,
                           "X.TXT"), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_NOT_FOUND);
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, NULL, "A", false, "B"),
             FM_ERR_ARG);
    CHECK_EQ(fm_job_rename(NULL, &g_bench, 1u, "", "A", false, "B"),
             FM_ERR_ARG);
    /* A folder with entries is renamed with them. */
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "", "D", true, "E"), FM_OK);
    (void)run_out();
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK(fk_get(&g_card, "E/TWO.TXT") != NULL);
    CHECK(clean());
}

TEST_CASE(delete_takes_a_run_report_along_and_no_full_folder)
{
    fresh();
    fk_file(&g_card, "USER/LOGS/ATEST005.CSV", NULL, 10u);
    fk_file(&g_card, "USER/LOGS/ATEST005.TXT", NULL, 10u);
    fk_file(&g_card, "USER/LOGS/ATEST006.TXT", NULL, 10u);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, CARD_USER_LOGS,
                           "ATEST005.CSV", false), FM_OK);
    CHECK_EQ(run_out(), 1);
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK(fk_get(&g_card, "USER/LOGS/ATEST005.CSV") == NULL);
    CHECK(fk_get(&g_card, "USER/LOGS/ATEST005.TXT") == NULL);
    CHECK(fk_get(&g_card, "USER/LOGS/ATEST006.TXT") != NULL);
    /* A report alone is one file. */
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, CARD_USER_LOGS,
                           "ATEST006.TXT", false), FM_OK);
    (void)run_out();
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    /* A folder: only empty. */
    fk_file(&g_card, "OLD/X.TXT", NULL, 1u);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, "", "OLD", true), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_NOT_EMPTY);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, "OLD", "X.TXT", false),
             FM_OK);
    (void)run_out();
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, "", "OLD", true), FM_OK);
    (void)run_out();
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK(fk_get(&g_card, "OLD") == NULL);
    /* A folder whose only entry is a name too long to list is not empty. */
    char path[90];
    memset(path, 'L', 80u);
    memcpy(path, "OLD2/", 5u);
    path[80] = '\0';
    fk_file(&g_card, path, NULL, 1u);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, "", "OLD2", true), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_NOT_EMPTY);
    /* Refusals: fixed, busy, the wrong kind, a name gone. */
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, "", "BENCH", true),
             FM_ERR_FIXED);
    const log_marks_t marks = { .run_dir = CARD_AUTO_LOGS, .run = 0u,
                                .test_dir = CARD_USER_LOGS, .txt = 9u };
    fm_bench_t quiet = g_bench;
    quiet.marks = &marks;
    CHECK_EQ(fm_job_delete(&g_job, &quiet, 1u, CARD_USER_LOGS,
                           "ATEST009.CSV", false), FM_ERR_LOGGING);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, "", "OLD2", false), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_ARG);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, "", "NONE.TXT", false),
             FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_NOT_FOUND);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, NULL, "A", false),
             FM_ERR_ARG);
    CHECK_EQ(fm_job_delete(NULL, &g_bench, 1u, "", "A", false), FM_ERR_ARG);
    /* The fault log, with the link up. */
    fk_file(&g_card, "BENCH/RCBENCH.LOG", NULL, 100u);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, CARD_BENCH, "RCBENCH.LOG",
                           false), FM_OK);
    (void)run_out();
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK(clean());
}

TEST_CASE(new_makes_a_folder_or_an_empty_file_never_over_a_name)
{
    fresh();
    CHECK_EQ(fm_job_mkdir(&g_job, &g_bench, 1u, "", "NOTES"), FM_OK);
    CHECK_EQ(run_out(), 1);
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK(fk_get(&g_card, "NOTES")->dir);
    CHECK_EQ(fm_job_mkdir(&g_job, &g_bench, 1u, "", "notes"), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_EXISTS);
    CHECK_EQ(fm_job_create(&g_job, &g_bench, 1u, "NOTES", "A.TXT"), FM_OK);
    (void)run_out();
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK_EQ(fk_get(&g_card, "NOTES/A.TXT")->size, 0);
    CHECK_EQ(fm_job_create(&g_job, &g_bench, 1u, "NOTES", "a.txt"), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_EXISTS);
    /* A close that fails takes the half-made file away again. */
    fk_fail(&g_card, FK_CLOSE, 1, FM_ERR_IO);
    CHECK_EQ(fm_job_create(&g_job, &g_bench, 1u, "NOTES", "B.TXT"), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_IO);
    CHECK(fk_get(&g_card, "NOTES/B.TXT") == NULL);
    /* Refusals before the card. */
    CHECK_EQ(fm_job_mkdir(&g_job, &g_bench, 1u, CARD_KST_CONF, "KST.TXT"),
             FM_ERR_LOCKED);
    CHECK_EQ(fm_job_create(&g_job, &g_bench, 1u, "", "A/B"), FM_ERR_NAME);
    CHECK_EQ(fm_job_create(&g_job, &g_bench, 1u, NULL, "A"), FM_ERR_ARG);
    CHECK_EQ(fm_job_mkdir(NULL, &g_bench, 1u, "", "A"), FM_ERR_ARG);
    CHECK_EQ(fm_job_create(NULL, &g_bench, 1u, "", "A"), FM_ERR_ARG);
    g_bench.armed = true;
    CHECK_EQ(fm_job_mkdir(&g_job, &g_bench, 1u, "", "X"), FM_ERR_ARMED);
    CHECK(clean());
}

/* -------------------------------------------------------------- save */

/** @brief The text a save writes: its offsets' low bytes. */
static uint8_t g_text[3u * FM_CHUNK];

/**
 * @brief Hand over the text from @p at, as an editor's buffer does.
 * @param ctx  a size_t: the bytes it holds, fewer than asked for to end
 *             short
 * @param at   the first byte wanted
 * @param dst  takes the bytes
 * @param cap  room in @p dst
 * @return the bytes handed over
 */
// cppcheck-suppress constParameterCallback
static size_t fill_text(void *ctx, uint64_t at, uint8_t *dst, size_t cap)
{
    const size_t have = *(const size_t *)ctx;
    if (at >= have) {
        return 0u;
    }
    const size_t n = (have - (size_t)at < cap) ? have - (size_t)at : cap;
    memcpy(dst, g_text + at, n);
    return n;
}

/**
 * @brief Whether @p path holds the first @p n bytes of the text.
 * @param path  the file
 * @param n     its bytes
 * @return true when it does
 */
static bool holds_text(const char *path, size_t n)
{
    const fk_node_t *e = fk_get(&g_card, path);
    return e != NULL && e->size == n
           && (n == 0u || memcmp(e->data, g_text, n) == 0);
}

TEST_CASE(a_save_replaces_a_file_through_its_new)
{
    for (size_t i = 0u; i < sizeof(g_text); ++i) {
        g_text[i] = (uint8_t)(i * 13u + 1u);
    }
    static const size_t sizes[] = { 0u, FM_CHUNK, FM_CHUNK + 1u };
    for (size_t s = 0u; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        fresh();
        size_t have = sizes[s];
        fk_file(&g_card, "R/NOTE.TXT", NULL, 77u);
        CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, "R", "NOTE.TXT",
                             sizes[s], fill_text, &have), FM_OK);
        CHECK(!g_job.note_restart);
        const int steps = run_out();
        /* Check, the chunks, the end, sync, delete, rename. */
        CHECK_EQ(steps, 1 + (int)((sizes[s] + FM_CHUNK - 1u) / FM_CHUNK)
                            + 1 + 3);
        CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
        CHECK(holds_text("R/NOTE.TXT", sizes[s]));
        CHECK(fk_get(&g_card, "R/NOTE.TXT.NEW") == NULL);
        CHECK(clean());
    }
    /* A file not there yet; an ESC profile says it is read at start. */
    fresh();
    size_t have = 10u;
    fk_dir(&g_card, CARD_ESC_CONF);
    CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, CARD_ESC_CONF, "yge.json",
                         10u, fill_text, &have), FM_OK);
    CHECK(g_job.note_restart);
    (void)run_out();
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK(holds_text("BENCH/ESC/CONF/yge.json", 10u));
}

TEST_CASE(a_save_cut_at_each_step_leaves_one_whole_copy)
{
    const size_t n = FM_CHUNK + 9u;
    /* Steps: 1 check, 2-3 chunks, 4 end, 5 sync, 6 delete, 7 rename. */
    for (int cut = 5; cut <= 7; ++cut) {
        fresh();
        size_t have = n;
        fk_file(&g_card, "NOTE.TXT", NULL, 40u);
        CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, "", "NOTE.TXT", n,
                             fill_text, &have), FM_OK);
        for (int k = 0; k < cut; ++k) {
            (void)step();
        }
        fk_pull(&g_card);
        (void)step();
        const bool old = fk_holds_pattern(&g_card, "NOTE.TXT", "NOTE.TXT",
                                          40u);
        if (cut == 5) {
            /* After the write: the file as it was, and a whole ".NEW". */
            CHECK(old);
            CHECK(holds_text("NOTE.TXT.NEW", n));
            CHECK_EQ(g_job.err, FM_ERR_NO_CARD);
        } else if (cut == 6) {
            /* After the delete: the ".NEW" alone holds the text. */
            CHECK(fk_get(&g_card, "NOTE.TXT") == NULL);
            CHECK(holds_text("NOTE.TXT.NEW", n));
        } else {
            CHECK(holds_text("NOTE.TXT", n));
            CHECK(fk_get(&g_card, "NOTE.TXT.NEW") == NULL);
            CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
        }
        CHECK(clean());
    }
}

TEST_CASE(a_save_that_fails_keeps_what_holds_the_text)
{
    const size_t n = 100u;
    /* The rename after the delete: the ".NEW" is the only copy. */
    fresh();
    size_t have = n;
    fk_file(&g_card, "NOTE.TXT", NULL, 40u);
    fk_fail(&g_card, FK_RENAME, 1, FM_ERR_IO);
    CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, "", "NOTE.TXT", n, fill_text,
                         &have), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_IO);
    CHECK(fk_get(&g_card, "NOTE.TXT") == NULL);
    CHECK(holds_text("NOTE.TXT.NEW", n));
    /* That ".NEW" is never written over by the next save. */
    CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, "", "NOTE.TXT", n, fill_text,
                         &have), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_EXISTS);
    CHECK(holds_text("NOTE.TXT.NEW", n));
    /* The delete refused: the file stands, the ".NEW" goes. */
    fresh();
    fk_file(&g_card, "NOTE.TXT", NULL, 40u);
    fk_fail(&g_card, FK_UNLINK, 1, FM_ERR_DENIED);
    CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, "", "NOTE.TXT", n, fill_text,
                         &have), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_DENIED);
    CHECK(fk_holds_pattern(&g_card, "NOTE.TXT", "NOTE.TXT", 40u));
    CHECK(fk_get(&g_card, "NOTE.TXT.NEW") == NULL);
    /* Full, cancelled, ARMED, a text that ends short: the file stands. */
    for (int why = 0; why < 4; ++why) {
        fresh();
        have = (why == 3) ? 50u : n;
        fk_file(&g_card, "NOTE.TXT", NULL, 40u);
        g_card.free_bytes = (why == 0) ? 10u : g_card.free_bytes;
        CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, "", "NOTE.TXT", n,
                             fill_text, &have), FM_OK);
        CHECK_EQ(step(), FM_JOB_RUNNING);
        if (why == 1) {
            fm_job_cancel(&g_job);
        } else if (why == 2) {
            g_bench.armed = true;
        }
        (void)run_out();
        CHECK(fm_job_state(&g_job, NULL, NULL) != FM_JOB_DONE);
        CHECK(fk_holds_pattern(&g_card, "NOTE.TXT", "NOTE.TXT", 40u));
        CHECK(fk_get(&g_card, "NOTE.TXT.NEW") == NULL);
        CHECK(clean());
    }
    /* A folder of that name, and refusals before the card. */
    fresh();
    fk_dir(&g_card, "NOTE.TXT");
    CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, "", "NOTE.TXT", n, fill_text,
                         &have), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_ARG);
    CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, CARD_KST_CONF, "KST.TXT", n,
                         fill_text, &have), FM_ERR_LOCKED);
    CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, "", "A.TXT", n, NULL, &have),
             FM_ERR_ARG);
    CHECK_EQ(fm_job_save(NULL, &g_bench, 1u, "", "A.TXT", n, fill_text,
                         &have), FM_ERR_ARG);
    CHECK(clean());
}

/* -------------------------------------------------------------- load */

TEST_CASE(a_load_reads_up_to_its_room_and_not_a_byte_more)
{
    static uint8_t buf[2u * FM_CHUNK];
    fresh();
    fk_file(&g_card, "R.TXT", NULL, sizeof(buf));
    fk_file(&g_card, "BIG.TXT", NULL, sizeof(buf) + 1u);
    /* A read: the gate lets it run while ARMED. */
    g_bench.armed = true;
    CHECK_EQ(fm_job_load(&g_job, &g_bench, 1u, "", "R.TXT", buf,
                         sizeof(buf)), FM_OK);
    (void)run_out();
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK_EQ(g_job.done, sizeof(buf));
    bool same = true;
    for (size_t i = 0u; i < sizeof(buf); ++i) {
        same = same && buf[i] == fk_pattern("R.TXT", i);
    }
    CHECK(same);
    CHECK_EQ(fm_job_load(&g_job, &g_bench, 1u, "", "BIG.TXT", buf,
                         sizeof(buf)), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_TOO_BIG);
    CHECK(clean());
    /* A file that grew past its room since its stat. */
    CHECK_EQ(fm_job_load(&g_job, &g_bench, 1u, "", "BIG.TXT", buf,
                         sizeof(buf)), FM_OK);
    g_job.cap = sizeof(buf);
    g_job.step = 1;
    CHECK_EQ(g_io.open(g_io.ctx, "BIG.TXT", FM_OPEN_READ, &g_job.in), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_TOO_BIG);
    CHECK(clean());
    /* Cancelled, busy, gone, or no room given. */
    CHECK_EQ(fm_job_load(&g_job, &g_bench, 1u, "", "R.TXT", buf,
                         sizeof(buf)), FM_OK);
    CHECK_EQ(step(), FM_JOB_RUNNING);
    fm_job_cancel(&g_job);
    CHECK_EQ(step(), FM_JOB_STOPPED);
    CHECK(clean());
    const log_marks_t marks = { .run_dir = CARD_AUTO_LOGS, .run = 3u,
                                .test_dir = CARD_USER_LOGS };
    const fm_bench_t busy = { .marks = &marks };
    CHECK_EQ(fm_job_load(&g_job, &busy, 1u, CARD_AUTO_LOGS, "SUPPL003.CSV",
                         buf, sizeof(buf)), FM_ERR_BUSY);
    CHECK_EQ(fm_job_load(&g_job, &g_bench, 1u, "", "NONE.TXT", buf,
                         sizeof(buf)), FM_OK);
    (void)run_out();
    CHECK_EQ(g_job.err, FM_ERR_NOT_FOUND);
    CHECK_EQ(fm_job_load(&g_job, &g_bench, 1u, "", "R.TXT", NULL, 4u),
             FM_ERR_ARG);
    CHECK_EQ(fm_job_load(NULL, &g_bench, 1u, "", "R.TXT", buf, 4u),
             FM_ERR_ARG);
}

TEST_CASE(a_step_without_a_job_a_card_or_a_chunk_is_refused)
{
    fresh();
    CHECK_EQ(fm_job_step(NULL, &g_io, &g_bench, g_chunk, sizeof(g_chunk)),
             FM_JOB_IDLE);
    uint64_t done = 5u;
    uint64_t total = 5u;
    CHECK_EQ(fm_job_state(NULL, &done, &total), FM_JOB_IDLE);
    CHECK_EQ(done, 0);
    CHECK_EQ(total, 0);
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_IDLE);
    fk_file(&g_card, "A.TXT", NULL, 10u);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "A.TXT", "B"), FM_OK);
    CHECK_EQ(fm_job_step(&g_job, NULL, &g_bench, g_chunk, sizeof(g_chunk)),
             FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_ARG);
    /* A copy given no chunk to copy through. */
    fk_dir(&g_card, "B");
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "A.TXT", "B"), FM_OK);
    CHECK_EQ(step(), FM_JOB_RUNNING);
    CHECK_EQ(fm_job_step(&g_job, &g_io, &g_bench, NULL, 0u), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_ARG);
    CHECK(fk_get(&g_card, "B/A.TXT.NEW") == NULL);
    CHECK(clean());
    /* A job of no kind. */
    memset(&g_job, 0, sizeof(g_job));
    g_job.state = FM_JOB_RUNNING;
    g_job.mount = 1u;
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_ARG);
    CHECK(clean());
}

int main(void)
{
    RUN(reads_pass_every_state_and_writes_only_safe_and_quiet);
    RUN(each_place_allows_what_it_allows);
    RUN(a_new_name_takes_59_bytes_and_no_bench_name);
    RUN(a_copy_of_every_size_arrives_whole_through_its_new);
    RUN(a_copy_cancelled_after_each_chunk_leaves_no_new);
    RUN(a_card_full_at_chunk_n_removes_the_new);
    RUN(a_card_pulled_at_chunk_n_fails_and_keeps_its_partial_new);
    RUN(a_job_on_another_card_writes_nothing_to_it);
    RUN(a_copy_that_fails_part_way_removes_its_new);
    RUN(a_copy_overwrites_nothing);
    RUN(a_copy_is_refused_where_the_rules_refuse_it);
    RUN(a_full_folder_takes_no_copy_and_no_new_entry);
    RUN(the_gate_stops_a_copy_at_its_next_chunk_and_a_queued_rename);
    RUN(move_goes_across_folders_and_never_onto_a_name);
    RUN(a_moved_name_of_63_characters_keeps_it);
    RUN(a_refused_rename_leaves_both_names_as_they_were);
    RUN(delete_takes_a_run_report_along_and_no_full_folder);
    RUN(new_makes_a_folder_or_an_empty_file_never_over_a_name);
    RUN(a_save_replaces_a_file_through_its_new);
    RUN(a_save_cut_at_each_step_leaves_one_whole_copy);
    RUN(a_save_that_fails_keeps_what_holds_the_text);
    RUN(a_load_reads_up_to_its_room_and_not_a_byte_more);
    RUN(a_step_without_a_job_a_card_or_a_chunk_is_refused);
    fk_free(&g_card);
    return test_summary("fm_ops");
}
