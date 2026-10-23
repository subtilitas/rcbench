/**
 * @file test_fm_recheck.c
 * @brief What the file manager's jobs ask again once they run (fm_ops.h),
 *        over a card in memory (fm_fake.h): the fault log's copy when the
 *        IOmcu link drops before the open and between two chunks; a
 *        rename or a move of a name that changed its kind since it was
 *        listed; a run's report that cannot be deleted with its log; each
 *        single-call job queued while SAFE and run ARMED, with a log open,
 *        and SAFE; a save whose absent target another writer made between
 *        two chunks; a report whose stat fails after its run went; a card
 *        swapped during a pane's fallback walk; a folder holding only a
 *        hidden file; a copy or a save whose ".NEW" is the programmer's
 *        journal; names whose case differs in an umlaut.  And fm_name.h
 *        with the core's headers in one unit.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <string.h>

#include "greatest.h"

/* fm_name.h first, then the core's headers that use its limits: one
 * definition of each, in any order, under -Werror. */
#include "fm_name.h"
#include "fm_io.h"
#include "fm_tree.h"
#include "fm_ops.h"

#include "card_tree.h"
#include "fm_fake.h"

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

/** @brief The five jobs of one call. */
enum { ONE_MOVE, ONE_RENAME, ONE_DELETE, ONE_MKDIR, ONE_CREATE, ONE_COUNT };

/**
 * @brief A fresh card with A/F.TXT and folder B, and job @p which started
 *        while the bench is SAFE with nothing open.
 * @param which  ONE_MOVE to ONE_CREATE
 * @return the start's answer
 */
static fm_err_t queue_one(int which)
{
    fresh();
    fk_file(&g_card, "A/F.TXT", NULL, 10u);
    fk_dir(&g_card, "B");
    switch (which) {
    case ONE_MOVE:
        return fm_job_move(&g_job, &g_bench, 1u, "A", "F.TXT", false, "B");
    case ONE_RENAME:
        return fm_job_rename(&g_job, &g_bench, 1u, "A", "F.TXT", false,
                             "G.TXT");
    case ONE_DELETE:
        return fm_job_delete(&g_job, &g_bench, 1u, "A", "F.TXT", false);
    case ONE_MKDIR:
        return fm_job_mkdir(&g_job, &g_bench, 1u, "B", "NEW");
    case ONE_CREATE:
    default:
        return fm_job_create(&g_job, &g_bench, 1u, "B", "NEW.TXT");
    }
}

/**
 * @brief Whether the card holds what queue_one() made and nothing more,
 *        and no call that changes it was made.
 * @return true for an untouched card
 */
static bool untouched(void)
{
    return g_card.count == 3 && fk_get(&g_card, "A/F.TXT") != NULL
           && g_card.calls[FK_RENAME] + g_card.calls[FK_UNLINK]
                  + g_card.calls[FK_MKDIR] + g_card.calls[FK_OPEN] == 0;
}

TEST_CASE(a_single_call_queued_safe_is_refused_once_the_bench_is_armed)
{
    for (int which = 0; which < ONE_COUNT; ++which) {
        CHECK_EQ(queue_one(which), FM_OK);
        g_bench.armed = true;
        CHECK_EQ(step(), FM_JOB_FAILED);
        CHECK_EQ(g_job.err, FM_ERR_ARMED);
        CHECK(untouched());
        CHECK(clean());
    }
}

TEST_CASE(a_single_call_queued_safe_is_refused_while_a_log_is_open)
{
    const log_marks_t marks = { .run_dir = CARD_AUTO_LOGS, .run = 4u,
                                .test_dir = CARD_USER_LOGS };
    for (int which = 0; which < ONE_COUNT; ++which) {
        CHECK_EQ(queue_one(which), FM_OK);
        g_bench.marks = &marks;
        CHECK_EQ(step(), FM_JOB_FAILED);
        CHECK_EQ(g_job.err, FM_ERR_LOGGING);
        CHECK(untouched());
        CHECK(clean());
    }
}

TEST_CASE(a_single_call_queued_and_run_safe_is_done)
{
    static const char *const made[] = {
        "B/F.TXT", "A/G.TXT", NULL, "B/NEW", "B/NEW.TXT",
    };
    for (int which = 0; which < ONE_COUNT; ++which) {
        CHECK_EQ(queue_one(which), FM_OK);
        CHECK_EQ(step(), FM_JOB_DONE);
        CHECK_EQ(g_job.err, FM_OK);
        if (made[which] != NULL) {
            CHECK(fk_get(&g_card, made[which]) != NULL);
        }
        const bool kept = (which == ONE_MKDIR || which == ONE_CREATE);
        CHECK_EQ(fk_get(&g_card, "A/F.TXT") != NULL, kept);
        CHECK(clean());
    }
    /* The fault log deleted while the link was up, run with it down. */
    fresh();
    fk_file(&g_card, CARD_FAULT_LOG, NULL, 10u);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, CARD_BENCH, "RCBENCH.LOG",
                           false), FM_OK);
    g_bench.link_up = false;
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_BUSY);
    CHECK(fk_get(&g_card, CARD_FAULT_LOG) != NULL);
}

TEST_CASE(the_names_and_paths_have_one_definition_each)
{
    _Static_assert(FM_NAME_BUF == FM_LIST_NAME_MAX + 1u,
                   "a listed name and its terminator");
    CHECK_EQ(FM_NAME_MAX, 59);
    CHECK_EQ(FM_PATH_MAX, 120);
    CHECK_EQ(sizeof(((fm_tree_t *)0)->dir), FM_PATH_SIZE);
    CHECK_EQ(sizeof(((fm_entry_t *)0)->name), FM_NAME_BUF);
    CHECK_EQ(sizeof(((fm_job_t *)0)->tmp), FM_PATH_SIZE);
}

/** @brief The text a save writes. */
static uint8_t g_text[2u * FM_CHUNK];

/**
 * @brief Hand over the text from @p at.
 * @param ctx  unused
 * @param at   the first byte wanted
 * @param dst  takes the bytes
 * @param cap  room in @p dst
 * @return the bytes handed over
 */
static size_t fill_text(void *ctx, uint64_t at, uint8_t *dst, size_t cap)
{
    (void)ctx;
    const size_t left = sizeof(g_text) - (size_t)at;
    const size_t n = (left < cap) ? left : cap;
    memcpy(dst, g_text + at, n);
    return n;
}

TEST_CASE(a_save_never_deletes_a_file_another_writer_made_meanwhile)
{
    fresh();
    fk_dir(&g_card, "D");
    CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, "D", "NOTE.TXT",
                         sizeof(g_text), fill_text, NULL), FM_OK);
    CHECK_EQ(step(), FM_JOB_RUNNING);        /* opened: no NOTE.TXT */
    CHECK_EQ(step(), FM_JOB_RUNNING);        /* chunk 1 */
    fk_file(&g_card, "D/NOTE.TXT", NULL, 7u);
    int n = 0;
    while (step() == FM_JOB_RUNNING && n < 20) {
        ++n;
    }
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_EXISTS);
    CHECK(fk_holds_pattern(&g_card, "D/NOTE.TXT", "D/NOTE.TXT", 7u));
    const fk_node_t *kept = fk_get(&g_card, "D/NOTE.TXT.NEW");
    CHECK(kept != NULL && kept->size == sizeof(g_text));
    CHECK(clean());
    /* Absent at the open and still absent: the save is done. */
    fresh();
    fk_dir(&g_card, "D");
    CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, "D", "NOTE.TXT",
                         sizeof(g_text), fill_text, NULL), FM_OK);
    n = 0;
    while (step() == FM_JOB_RUNNING && n < 20) {
        ++n;
    }
    CHECK_EQ(fm_job_state(&g_job, NULL, NULL), FM_JOB_DONE);
    CHECK_EQ(g_card.calls[FK_UNLINK], 0);
    CHECK(fk_get(&g_card, "D/NOTE.TXT") != NULL);
}

TEST_CASE(a_report_stat_that_fails_after_its_run_went_is_the_answer)
{
    fresh();
    fk_file(&g_card, "USER/LOGS/ATEST005.CSV", NULL, 10u);
    fk_file(&g_card, "USER/LOGS/ATEST005.TXT", NULL, 10u);
    /* Stat 1 is the run's, stat 2 the report's. */
    fk_fail(&g_card, FK_STAT, 2, FM_ERR_IO);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, CARD_USER_LOGS,
                           "ATEST005.CSV", false), FM_OK);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_IO);
    CHECK(fk_get(&g_card, "USER/LOGS/ATEST005.CSV") == NULL);
    CHECK(fk_get(&g_card, "USER/LOGS/ATEST005.TXT") != NULL);
    /* No report at all is no error. */
    fresh();
    fk_file(&g_card, "USER/LOGS/ATEST006.CSV", NULL, 10u);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, CARD_USER_LOGS,
                           "ATEST006.CSV", false), FM_OK);
    CHECK_EQ(step(), FM_JOB_DONE);
    CHECK(clean());
}

TEST_CASE(a_card_swapped_during_the_fallback_walk_is_walked_again)
{
    fresh();
    fk_dir(&g_card, "D");
    char path[FK_PATH];
    for (int i = 0; i < 50; ++i) {
        snprintf(path, sizeof(path), "D/F%03d.TXT", i);
        fk_file(&g_card, path, NULL, 1u);
    }
    fm_tree_t tree;
    fm_tree_init(&tree, "D");
    CHECK_EQ(fm_tree_page(&tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(fm_tree_page(&tree, &g_io, NULL, FM_GO_NEXT), FM_OK);
    CHECK_EQ(tree.held, 2);
    CHECK_EQ(g_io.unlink(g_io.ctx, "D/F048.TXT"), FM_OK);
    CHECK_EQ(g_io.unlink(g_io.ctx, "D/F049.TXT"), FM_OK);
    /* SAME finds nothing from its key on; its fallback walk to the last
     * page finds the card gone and the next one mounted. */
    g_card.gone_walk = g_card.calls[FK_WALK] + 2;
    CHECK_EQ(fm_tree_page(&tree, &g_io, NULL, FM_GO_SAME), FM_OK);
    CHECK_EQ(tree.total, 48);
    CHECK_EQ(tree.held, 48);
    CHECK_EQ(tree.mount, 2u);
    CHECK_EQ(g_card.holds, 0);
}

TEST_CASE(a_folder_holding_only_a_hidden_file_is_not_empty)
{
    fresh();
    fk_file(&g_card, "OLD/.DS_Store", NULL, 4u);
    fk_dir(&g_card, "DST");
    fm_tree_t tree;
    fm_tree_init(&tree, "OLD");
    CHECK_EQ(fm_tree_page(&tree, &g_io, NULL, FM_GO_FIRST), FM_OK);
    CHECK_EQ(tree.held, 0);
    CHECK_EQ(tree.unlisted, 1u);
    CHECK_EQ(fm_job_move(&g_job, &g_bench, 1u, "", "OLD", true, "DST"),
             FM_OK);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_NOT_EMPTY);
    CHECK_EQ(fm_job_delete(&g_job, &g_bench, 1u, "", "OLD", true), FM_OK);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_NOT_EMPTY);
    CHECK(fk_get(&g_card, "OLD/.DS_Store") != NULL);
    CHECK_EQ(g_card.calls[FK_RENAME] + g_card.calls[FK_RMDIR], 0);
    CHECK(clean());
}

TEST_CASE(a_copy_or_a_save_whose_new_is_the_journal_is_locked)
{
    fresh();
    fk_file(&g_card, "KST", NULL, 10u);
    fk_dir(&g_card, CARD_KST_CONF);
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "KST", CARD_KST_CONF),
             FM_ERR_LOCKED);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(fm_job_save(&g_job, &g_bench, 1u, CARD_KST_CONF, "kst",
                         sizeof(g_text), fill_text, NULL), FM_ERR_LOCKED);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(fm_job_create(&g_job, &g_bench, 1u, CARD_KST_CONF, "KST"),
             FM_ERR_LOCKED);
    CHECK_EQ(g_card.calls[FK_OPEN] + g_card.calls[FK_STAT], 0);
    CHECK(fk_get(&g_card, "BENCH/KST/CONF/KST.NEW") == NULL);
    /* Elsewhere a file named KST is any file. */
    fk_dir(&g_card, "OTHER");
    CHECK_EQ(fm_job_copy(&g_job, &g_bench, 1u, "", "KST", "OTHER"), FM_OK);
    CHECK_EQ(fm_target_check(NULL, CARD_KST_CONF, "KST", false),
             FM_ERR_LOCKED);
    CHECK_EQ(fm_target_check(NULL, CARD_KST_CONF, "KST1", false), FM_OK);
}

/** @brief Ä, ä, Ö, ö, Ü, ü and ß in UTF-8. */
#define U_AE "\xC3\x84"
#define L_AE "\xC3\xA4"
#define U_OE "\xC3\x96"
#define L_OE "\xC3\xB6"
#define U_UE "\xC3\x9C"
#define L_UE "\xC3\xBC"
#define SZ   "\xC3\x9F"

TEST_CASE(names_differing_in_an_umlauts_case_are_one_name)
{
    CHECK(fm_same_path(U_AE "RGER", L_AE "rger"));
    CHECK(fm_same_path("M" U_OE "WE/" U_UE "BER", "m" L_OE "we/" L_UE "ber"));
    CHECK(fm_path_within(U_AE "RGER", L_AE "rger/" U_UE ".TXT"));
    CHECK(!fm_path_within(U_AE "RGER", L_AE "rgerX"));
    /* ß has no upper case in FatFs's table: only ß matches it. */
    CHECK(fm_same_path("STRA" SZ "E", "stra" SZ "e"));
    CHECK(!fm_same_path("STRA" SZ "E", "STRASSE"));
    CHECK(!fm_same_path(L_AE, "A"));
    /* A rename that changes an umlaut's case is one entry, not an
     * overwrite. */
    fresh();
    fk_file(&g_card, U_AE ".TXT", NULL, 6u);
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "", U_AE ".TXT", false,
                           L_AE ".txt"), FM_OK);
    CHECK_EQ(step(), FM_JOB_DONE);
    CHECK_STR_EQ(fk_get(&g_card, U_AE ".TXT")->path, L_AE ".txt");
    /* Another letter is another name, and taken it is refused. */
    fk_file(&g_card, U_OE ".TXT", NULL, 6u);
    CHECK_EQ(fm_job_rename(&g_job, &g_bench, 1u, "", L_AE ".txt", false,
                           L_OE ".txt"), FM_OK);
    CHECK_EQ(step(), FM_JOB_FAILED);
    CHECK_EQ(g_job.err, FM_ERR_EXISTS);
    CHECK(clean());
}

int main(void)
{
    RUN(the_link_dropping_before_the_open_stops_a_fault_log_copy);
    RUN(the_link_dropping_between_two_chunks_stops_a_fault_log_copy);
    RUN(a_file_listed_that_is_a_fixed_folder_now_is_not_renamed);
    RUN(a_report_that_cannot_go_with_its_run_is_the_answer);
    RUN(a_single_call_queued_safe_is_refused_once_the_bench_is_armed);
    RUN(a_single_call_queued_safe_is_refused_while_a_log_is_open);
    RUN(a_single_call_queued_and_run_safe_is_done);
    RUN(the_names_and_paths_have_one_definition_each);
    RUN(a_save_never_deletes_a_file_another_writer_made_meanwhile);
    RUN(a_report_stat_that_fails_after_its_run_went_is_the_answer);
    RUN(a_card_swapped_during_the_fallback_walk_is_walked_again);
    RUN(a_folder_holding_only_a_hidden_file_is_not_empty);
    RUN(a_copy_or_a_save_whose_new_is_the_journal_is_locked);
    RUN(names_differing_in_an_umlauts_case_are_one_name);
    fk_free(&g_card);
    return test_summary("fm_recheck");
}
