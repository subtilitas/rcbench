/*
 * The logger, checked against this project's own reader.
 *
 * Writing a file and looking at it proves nothing: the question is whether the
 * reader gets the numbers back, and the reader is the one that decides a
 * file's decimal convention from every value in it and rejects what does not
 * conform.  So every case here writes a run and parses it, and the assertion
 * is on what came out.
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

static mem_sink_t g_mem;

static void fresh(int fail_after)
{
    memset(&g_mem, 0, sizeof(g_mem));
    g_mem.fail_after = fail_after;
}

static log_writer_t writer(void)
{
    const log_sink_t sink = { mem_write, &g_mem };
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
 * The case the whole file exists for: a run written by the logger is a run the
 * reader understands, without being told anything about it.
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
     * and the log viewer opens the file again for exactly this reason --
     * reusing it walks off the end of what has already been read.
     */
    log_source_t src2;
    log_mem_ctx_t ctx2;
    log_source_memory(&src2, &ctx2, g_mem.buf, g_mem.len);

    /*
     * Column 0 is the time axis and becomes data.time; the reader plots at
     * most LOG_MAX_SERIES at once, which is four, so four value columns are
     * asked for rather than all eight.  That is the reader's design -- four
     * traces on one time base -- not a limit of the file.
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
    CHECK(log_writer_header(&w));          /* asking now changes nothing */

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
    return test_summary("logwriter");
}
