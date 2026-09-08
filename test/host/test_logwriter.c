/*
 * The logger, checked against this project's own reader: every case writes a
 * run, parses it with the CSV (comma-separated values) reader, and asserts on
 * what comes out.  The reader decides a file's decimal convention from every
 * value in it and rejects cells that do not conform, so a round trip proves
 * the format.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "greatest.h"

#include "log_csv.h"
#include "log_numbers.h"
#include "log_writer.h"
#include "telemetry_sim.h"

/* ------------------------------------------------------------- the sink */

typedef struct {
    char   buf[64 * 1024];
    size_t len;
    int    fail_after;   /**< -1 never; else fail once this many bytes are in */
    int    commits;      /**< how many times the sink was told to keep it */
    size_t kept;         /**< bytes the last commit made durable */
    bool   commit_fails;
} mem_sink_t;

static int mem_write(void *ctx, const void *data, size_t len)
{
    mem_sink_t *m = (mem_sink_t *)ctx;
    if (m->fail_after >= 0 && (int)m->len >= m->fail_after) {
        return -1;
    }
    if (m->len + len > sizeof(m->buf)) {
        return -1;
    }
    memcpy(m->buf + m->len, data, len);
    m->len += len;
    return (int)len;
}

/*
 * What a card does at a commit, in the only part a host can model: bytes
 * written before it survive a power cut, bytes written after it do not.
 */
static bool mem_flush(void *ctx)
{
    mem_sink_t *m = (mem_sink_t *)ctx;
    if (m->commit_fails) {
        return false;
    }
    ++m->commits;
    m->kept = m->len;
    return true;
}

static mem_sink_t g_mem;

static void fresh(int fail_after)
{
    memset(&g_mem, 0, sizeof(g_mem));
    g_mem.fail_after = fail_after;
}

static log_writer_t writer(void)
{
    const log_sink_t sink = { .write = mem_write, .flush = mem_flush,
                              .ctx = &g_mem };
    log_writer_t w;
    log_writer_init(&w, &sink);
    return w;
}

/* The same sink with nothing to commit to, which is what memory really is. */
static log_writer_t writer_without_commit(void)
{
    const log_sink_t sink = { .write = mem_write, .flush = NULL,
                              .ctx = &g_mem };
    log_writer_t w;
    log_writer_init(&w, &sink);
    return w;
}

/** Write a scripted run and return how many rows went in. */
static int write_run(log_writer_t *w, int rows)
{
    telemetry_sim_t sim;
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);
    int n = 0;
    for (int i = 0; i < rows; ++i) {
        const float t = (float)i * 0.05f;
        const float th = (i < 20) ? 0.0f : (i < 60) ? 45.0f : 88.0f;
        telemetry_sim_step(&sim, th, 0.05f, &b);
        if (log_writer_row(w, t, &b)) {
            ++n;
        }
    }
    return n;
}

/* -------------------------------------------------------- the round trip */

/*
 * A run written by the logger is a run the reader understands without being
 * told anything about it.
 */
TEST_CASE(a_written_run_reads_back)
{
    fresh(-1);
    log_writer_t w = writer();
    const int rows = write_run(&w, 120);
    CHECK_EQ(rows, 120);
    CHECK(!log_writer_failed(&w));

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_mem.buf, g_mem.len);

    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t an;
    CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
    CHECK_EQ(an.n_columns, 9);
    CHECK_EQ(an.row_count, 120);
    /* Detected, not assumed: the file never says which convention it uses. */
    CHECK_EQ(an.delimiter, ';');
    CHECK_EQ(an.convention, LOG_CONV_EN);
    CHECK(an.convention_confident);
    CHECK_EQ(an.ragged_rows, 0);
}

/* Units belong in the header.  A separate units row is read for its units and
 * then counted as data as well, which reports the first row as unreadable. */
TEST_CASE(the_header_carries_the_units_and_no_row_is_wasted)
{
    fresh(-1);
    log_writer_t w = writer();
    write_run(&w, 10);

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_mem.buf, g_mem.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t an;
    CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);

    CHECK_EQ(an.row_count, 10);           /* ten written, ten read */
    CHECK_EQ(an.ragged_rows, 0);
    /* And the units came through with the names. */
    CHECK(strstr(g_mem.buf, "voltage (V)") != NULL);
    CHECK(strstr(g_mem.buf, "\nvoltage") == NULL);   /* not on its own row */
}

/* The numbers themselves, to the precision they were printed at. */
TEST_CASE(the_values_survive_the_round_trip)
{
    fresh(-1);
    log_writer_t w = writer();

    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.voltage = 24.31f; b.current = 68.14f; b.power = 1656.0f;
    b.rpm = 13581.0f;   b.temp_esc = 46.3f; b.temp_motor = 58.9f;
    b.charge_mah = 1843.0f; b.energy_wh = 44.72f;
    CHECK(log_writer_row(&w, 1.234f, &b));
    /* A second row, because data.time is seconds *from the first sample* --
     * one row would read 0.000 whatever was written. */
    b.voltage = 20.71f; b.current = 32.5f; b.power = 673.0f; b.rpm = 11419.0f;
    b.energy_wh = 45.10f;
    CHECK(log_writer_row(&w, 1.284f, &b));

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_mem.buf, g_mem.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t an;
    CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);

    /*
     * A fresh source for the build.  analyse consumes the one it is given,
     * and the log viewer opens the file again for the same reason: reusing
     * the source walks off the end of what is already read.
     */
    log_source_t src2;
    log_mem_ctx_t ctx2;
    log_source_memory(&src2, &ctx2, g_mem.buf, g_mem.len);

    /*
     * Column 0 is the time axis and becomes data.time; the reader plots at
     * most LOG_MAX_SERIES at once, which is four, so four value columns are
     * asked for rather than all eight.  That is the reader's design (four
     * traces on one time base), not a limit of the file.
     */
    int cols[LOG_MAX_SERIES];
    for (int i = 0; i < LOG_MAX_SERIES; ++i) { cols[i] = i + 1; }
    log_data_t data;
    CHECK_EQ(log_csv_build(&src2, &an, cols, LOG_MAX_SERIES, &data), LOG_OK);
    CHECK_EQ(data.count, 2);
    CHECK_EQ(data.n_fields, LOG_MAX_SERIES);
    CHECK_EQ(data.unparsed_cells, 0);

    /* Time is seconds from the first sample. */
    CHECK_NEAR(data.time[0], 0.0f, 0.001f);
    CHECK_NEAR(data.time[1], 0.050f, 0.002f);

    CHECK_NEAR(data.value[0][0], 24.31f, 0.005f);
    CHECK_NEAR(data.value[1][0], 68.14f, 0.005f);
    CHECK_NEAR(data.value[2][0], 1656.0f, 0.5f);
    CHECK_NEAR(data.value[3][0], 13581.0f, 0.5f);
    CHECK_NEAR(data.value[0][1], 20.71f, 0.005f);
    CHECK_NEAR(data.value[3][1], 11419.0f, 0.5f);

    /* And the units came off the header, not out of thin air. */
    CHECK_STR_EQ(data.field[0].unit, "V");
    CHECK_STR_EQ(data.field[3].unit, "rpm");
    log_data_free(&data);
}

/* A reading that is not finite is an absent cell, not "nan" and not zero. */
TEST_CASE(a_non_finite_reading_is_written_as_an_absent_cell)
{
    fresh(-1);
    log_writer_t w = writer();
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.voltage = NAN;
    b.current = 3.5f;
    CHECK(log_writer_row(&w, 0.0f, &b));

    CHECK(strstr(g_mem.buf, "nan") == NULL);
    CHECK(strstr(g_mem.buf, "NAN") == NULL);
    /* time, then an empty cell where the voltage was. */
    CHECK(strstr(g_mem.buf, "0.000;;") != NULL);
}

/* The header appears once, and appears even if the caller never asks. */
TEST_CASE(the_header_is_written_once_and_without_being_asked)
{
    fresh(-1);
    log_writer_t w = writer();
    bench_state_t b;
    memset(&b, 0, sizeof(b));

    CHECK(log_writer_row(&w, 0.0f, &b));   /* never called the header */
    CHECK(log_writer_row(&w, 0.05f, &b));
    CHECK(log_writer_header(&w));          /* a later call changes nothing */

    int seen = 0;
    for (const char *p = g_mem.buf; (p = strstr(p, "time (s)")) != NULL; ++p) {
        ++seen;
    }
    CHECK_EQ(seen, 1);
}

/*
 * A card that fills or is pulled out mid-run.  The writer has to stop rather
 * than keep returning success, because a log that silently loses its tail is
 * worse than one that says it is short.
 */
TEST_CASE(a_failing_sink_latches_and_stops)
{
    fresh(400);   /* fail once about four hundred bytes are in */
    log_writer_t w = writer();
    const int wrote = write_run(&w, 200);

    CHECK(wrote > 0);
    CHECK(wrote < 200);
    CHECK(log_writer_failed(&w));

    /* And it stays failed: no later row sneaks in. */
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    CHECK(!log_writer_row(&w, 99.0f, &b));
    CHECK_EQ((int)w.rows, wrote);
}

/* What it produced before failing must still parse: a truncated log is a
 * short log, not a corrupt one. */
TEST_CASE(what_survived_a_failure_still_reads)
{
    fresh(1200);
    log_writer_t w = writer();
    write_run(&w, 200);
    CHECK(log_writer_failed(&w));

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_mem.buf, g_mem.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t an;
    CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
    CHECK(an.row_count > 0);
    CHECK_EQ(an.delimiter, ';');
}

TEST_CASE(a_writer_with_no_sink_fails_rather_than_crashes)
{
    log_writer_t w;
    log_writer_init(&w, NULL);
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    CHECK(!log_writer_row(&w, 0.0f, &b));
    CHECK(log_writer_failed(&w));
    log_writer_init(NULL, NULL);           /* survivable */
    CHECK(!log_writer_row(NULL, 0.0f, &b));
    CHECK(!log_writer_commit(NULL));
    CHECK_EQ((int)log_writer_pending(NULL), 0);
}

/* ------------------------------------------------- what a power cut costs */

/*
 * The bound the rule exists for: at no point in a run is more than
 * LOG_WRITER_FLUSH_ROWS of it uncommitted, and no uncommitted row is older
 * than LOG_WRITER_FLUSH_S.
 *
 * Driven at the panel's 20 Hz, where 20 rows and 1.0 s are the same instant.
 */
TEST_CASE(no_more_than_one_interval_of_a_run_is_ever_uncommitted)
{
    fresh(-1);
    log_writer_t w = writer();

    telemetry_sim_t sim;
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);

    float oldest_uncommitted = 0.0f;
    for (int i = 1; i <= 200; ++i) {
        const float t = (float)i * 0.05f;
        telemetry_sim_step(&sim, 50.0f, 0.05f, &b);
        if (log_writer_pending(&w) == 0u) {
            oldest_uncommitted = t;     /* this row starts the next batch */
        }
        CHECK(log_writer_row(&w, t, &b));
        CHECK(log_writer_pending(&w) <= LOG_WRITER_FLUSH_ROWS);
        if (log_writer_pending(&w) > 0u) {
            CHECK((t - oldest_uncommitted) <= LOG_WRITER_FLUSH_S);
        }
    }
    /* Ten seconds of run at one commit a second. */
    CHECK_EQ(g_mem.commits, 10);
    CHECK_EQ((int)log_writer_pending(&w), 0);
    CHECK(!log_writer_failed(&w));
    /* And every byte written is a byte kept, because the run ended on one. */
    CHECK_EQ((int)g_mem.kept, (int)g_mem.len);
}

/*
 * A power cut is what the sink kept, and it parses.  Twenty-nine rows in, the
 * first twenty are on the card and the other nine are not.
 */
TEST_CASE(what_a_power_cut_leaves_is_a_short_run_and_not_an_empty_file)
{
    fresh(-1);
    log_writer_t w = writer();
    write_run(&w, 29);
    CHECK_EQ(g_mem.commits, 1);
    CHECK(g_mem.kept > 0);
    CHECK(g_mem.kept < g_mem.len);      /* the tail did not survive */

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_mem.buf, g_mem.kept);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t an;
    CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
    CHECK_EQ(an.row_count, 20);         /* the rows the commit covered */
    CHECK_EQ(an.delimiter, ';');
    CHECK_EQ(an.ragged_rows, 0);
}

/*
 * Rows slower than 20 Hz are committed on the clock rather than on the count,
 * so a run whose samples arrive every 300 ms does not carry ten seconds of
 * itself uncommitted.
 */
TEST_CASE(a_slow_run_is_committed_on_the_clock_rather_than_on_the_count)
{
    fresh(-1);
    log_writer_t w = writer();
    bench_state_t b;
    memset(&b, 0, sizeof(b));

    for (int i = 1; i <= 3; ++i) {
        CHECK(log_writer_row(&w, (float)i * 0.3f, &b));
    }
    CHECK_EQ(g_mem.commits, 0);            /* 0.9 s, three rows: not yet */
    CHECK_EQ((int)log_writer_pending(&w), 3);

    CHECK(log_writer_row(&w, 1.2f, &b));   /* 1.2 s since the last commit */
    CHECK_EQ(g_mem.commits, 1);
    CHECK_EQ((int)log_writer_pending(&w), 0);
}

/* A run that has gone quiet is committed by hand, and an empty one is not: a
 * commit with nothing pending is a card transaction that buys nothing. */
TEST_CASE(a_commit_by_hand_keeps_the_tail_and_an_empty_one_costs_nothing)
{
    fresh(-1);
    log_writer_t w = writer();
    bench_state_t b;
    memset(&b, 0, sizeof(b));

    CHECK(log_writer_commit(&w));          /* nothing written yet */
    CHECK_EQ(g_mem.commits, 0);

    CHECK(log_writer_row(&w, 0.05f, &b));
    CHECK_EQ((int)log_writer_pending(&w), 1);
    CHECK(log_writer_commit(&w));
    CHECK_EQ(g_mem.commits, 1);
    CHECK_EQ((int)g_mem.kept, (int)g_mem.len);

    CHECK(log_writer_commit(&w));          /* and again changes nothing */
    CHECK_EQ(g_mem.commits, 1);
    CHECK(!log_writer_failed(&w));
}

/*
 * A commit the sink refuses is the card gone.  It latches like a failed
 * write, because rows the sink will not keep are rows the file has lost.
 */
TEST_CASE(a_commit_the_sink_refuses_latches_the_writer)
{
    fresh(-1);
    log_writer_t w = writer();
    bench_state_t b;
    memset(&b, 0, sizeof(b));

    CHECK(log_writer_row(&w, 0.05f, &b));
    g_mem.commit_fails = true;
    CHECK(!log_writer_commit(&w));
    CHECK(log_writer_failed(&w));
    CHECK(!log_writer_row(&w, 0.10f, &b));
    CHECK_EQ((int)w.rows, 1);
}

/* A sink with nothing to commit to writes the same file. */
TEST_CASE(a_sink_that_needs_no_commit_still_writes_the_whole_run)
{
    fresh(-1);
    log_writer_t w = writer_without_commit();
    CHECK_EQ(write_run(&w, 120), 120);
    CHECK(!log_writer_failed(&w));
    CHECK_EQ(g_mem.commits, 0);
    CHECK_EQ((int)log_writer_pending(&w), 0);

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_mem.buf, g_mem.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t an;
    CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
    CHECK_EQ(an.row_count, 120);
}

int main(void)
{
    RUN(a_written_run_reads_back);
    RUN(the_header_carries_the_units_and_no_row_is_wasted);
    RUN(the_values_survive_the_round_trip);
    RUN(a_non_finite_reading_is_written_as_an_absent_cell);
    RUN(the_header_is_written_once_and_without_being_asked);
    RUN(a_failing_sink_latches_and_stops);
    RUN(what_survived_a_failure_still_reads);
    RUN(a_writer_with_no_sink_fails_rather_than_crashes);
    RUN(no_more_than_one_interval_of_a_run_is_ever_uncommitted);
    RUN(what_a_power_cut_leaves_is_a_short_run_and_not_an_empty_file);
    RUN(a_slow_run_is_committed_on_the_clock_rather_than_on_the_count);
    RUN(a_commit_by_hand_keeps_the_tail_and_an_empty_one_costs_nothing);
    RUN(a_commit_the_sink_refuses_latches_the_writer);
    RUN(a_sink_that_needs_no_commit_still_writes_the_whole_run);
    return test_summary("logwriter");
}
