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

#include "log_cadence.h"
#include "log_csv.h"
#include "log_numbers.h"
#include "log_writer.h"
#include "sense_chain.h"
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
    CHECK_EQ(an.n_columns, 22);
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
/*
 * A quantity nothing measured is an empty cell, not a zero.
 *
 * The bench has fields with no sensor behind them -- the motor's temperature
 * has none at all, and voltage and current only arrive if an ESC sends
 * extended telemetry. Writing 0 for those would put a measurement in the
 * permanent record where there was none, and the log outlives the run and the
 * operator's memory of what was fitted. log_parse_with() refuses an empty
 * cell rather than guessing, so an empty column reads back as absent.
 */
TEST_CASE(a_column_nothing_measured_is_empty_rather_than_zero)
{
    fresh(-1);
    log_writer_t w = writer();

    bench_state_t b;
    memset(&b, 0, sizeof(b));
    /* An ESC answered with its own temperature and nothing else -- which is
     * every ESC, since none of them knows the motor's. */
    b.flags = (uint16_t)(LINK_BN_RPM_OK | LINK_BN_TEMP_OK);
    b.rpm = 11419.0f;
    b.temp_esc = 46.3f;
    b.temp_motor = 0.0f;
    b.voltage = 0.0f;
    CHECK(log_writer_row(&w, 1.0f, &b));

    /* The row after the header: time;voltage;current;power;rpm;esc;motor;
     * charge;energy;ina voltage;ina current;esc current */
    const char *p = strchr(g_mem.buf, '\n');
    CHECK(p != NULL);
    ++p;

    char got[200];
    snprintf(got, sizeof(got), "%s", p);
    char *nl = strchr(got, '\n');
    if (nl != NULL) { *nl = '\0'; }

    /* Voltage, current and power answered for nothing; the motor's
     * temperature has no sensor; and with no current there is no charge or
     * energy to count.  Six empty cells, and the ones that did answer carry
     * numbers. */
    CHECK_STR_EQ(got, "1.000;;;;11419;46.3;;;;;;;;;;;;;;;;");
}

TEST_CASE(charge_and_energy_are_written_only_when_counted)
{
    /* A current with no voltage beside it: the run counted charge and could
     * not count energy, so one total is a number and the other is empty. */
    fresh(-1);
    log_writer_t w = writer();
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.flags = (uint16_t)LINK_BN_CURRENT_OK;
    b.counted = BENCH_COUNTED_CHARGE;
    b.current = 10.0f;
    b.charge_mah = 5.0f;
    b.energy_wh = 0.0f;
    CHECK(log_writer_row(&w, 1.0f, &b));

    const char *p = strchr(g_mem.buf, '\n');
    CHECK(p != NULL);
    ++p;
    char got[200];
    snprintf(got, sizeof(got), "%s", p);
    char *nl = strchr(got, '\n');
    if (nl != NULL) { *nl = '\0'; }
    CHECK_STR_EQ(got, "1.000;;10.00;;;;;5;;;;10.00;;;;;;;;;;");
}

TEST_CASE(the_values_survive_the_round_trip)
{
    fresh(-1);
    log_writer_t w = writer();

    bench_state_t b;
    memset(&b, 0, sizeof(b));
    /* A bench where everything answered.  The flags are what say so, and a
     * column whose flag is clear is written empty. */
    b.flags = (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                         | LINK_BN_RPM_OK | LINK_BN_TEMP_OK
                         | LINK_BN_TEMP_MOT_OK);
    b.counted = BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY;
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

/*
 * The source beside the reading.  With the INA228 as BENCH's source the
 * voltage and current are its own and the ESC's current is the SENSE
 * page's; without it the INA228's columns are empty and the ESC's current
 * is BENCH's.  The three columns come back by name, unit and value, and the
 * log viewer's map puts them with the INA228 and the ESC.
 */
TEST_CASE(the_sources_survive_the_round_trip)
{
    fresh(-1);
    log_writer_t w = writer();
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.flags = (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                         | LINK_BN_SENSED);
    b.voltage = 24.31f;
    b.current = 31.20f;
    b.power = 758.0f;
    bench_state_set_esc(&b, true, 24.50f, true, 36.75f, false);
    CHECK(log_writer_row(&w, 1.0f, &b));
    /* The ESC's current only: the INA228 is not fitted. */
    memset(&b, 0, sizeof(b));
    b.flags = (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK);
    b.voltage = 24.10f;
    b.current = 40.05f;
    CHECK(log_writer_row(&w, 1.05f, &b));

    CHECK(strstr(g_mem.buf, ";ina voltage (V);ina current (A);"
                            "esc current (A);window;") != NULL);
    static const char k_rows[] =
        "1.000;24.31;31.20;758;;;;;;24.31;31.20;36.75;;;;;;;;;;\n"
        "1.050;24.10;40.05;0;;;;;;;;40.05;;;;;;;;;;\n";
    const char *row = strchr(g_mem.buf, '\n') + 1;
    CHECK_STR_EQ(row, k_rows);

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_mem.buf, g_mem.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t an;
    CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
    CHECK_EQ(an.n_columns, 22);
    CHECK_EQ(an.ragged_rows, 0);

    log_source_t src2;
    log_mem_ctx_t ctx2;
    log_source_memory(&src2, &ctx2, g_mem.buf, g_mem.len);
    int cols[3] = { 9, 10, 11 };
    log_data_t data;
    CHECK_EQ(log_csv_build(&src2, &an, cols, 3, &data), LOG_OK);
    CHECK_EQ(data.count, 2);
    CHECK_STR_EQ(data.field[0].name, "ina voltage");
    CHECK_STR_EQ(data.field[0].unit, "V");
    CHECK_STR_EQ(data.field[0].group, "INA228");
    CHECK_STR_EQ(data.field[1].name, "ina current");
    CHECK_STR_EQ(data.field[1].unit, "A");
    CHECK_STR_EQ(data.field[1].group, "INA228");
    CHECK_STR_EQ(data.field[2].name, "esc current");
    CHECK_STR_EQ(data.field[2].unit, "A");
    CHECK_STR_EQ(data.field[2].group, "ESC");
    CHECK_NEAR(data.value[0][0], 24.31f, 0.005f);
    CHECK_NEAR(data.value[1][0], 31.20f, 0.005f);
    CHECK_NEAR(data.value[2][0], 36.75f, 0.005f);
    CHECK_NEAR(data.value[2][1], 40.05f, 0.005f);
    log_data_free(&data);
}

/*
 * The INA3221's window goes into the row that first carries it, by its
 * number, and into no other; a channel the window has no readings of is
 * empty.  The columns read back by name, unit and value.
 */
TEST_CASE(each_window_goes_into_one_row)
{
    fresh(-1);
    log_writer_t w = writer();
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.servo_new    = true;
    b.servo_window = 812u;
    b.servo_ok     = 0x01u;                /* CH1 only */
    b.servo_mean_ma[0] = 412;
    b.servo_max_ma[0]  = 1638;
    b.servo_min_mv[0]  = 5874u;
    CHECK(log_writer_row(&w, 1.0f, &b));
    b.servo_new = false;                   /* the panel, once it is posted */
    CHECK(log_writer_row(&w, 1.05f, &b));

    CHECK(strstr(g_mem.buf, ";window;ch1 current (A);ch1 max (A);"
                            "ch1 voltage (V);ch2 current (A)") != NULL);
    static const char k_rows[] =
        "1.000;;;;;;;;;;;;812;0.412;1.638;5.874;;;;;;\n"
        "1.050;;;;;;;;;;;;;;;;;;;;;\n";
    const char *row = strchr(g_mem.buf, '\n') + 1;
    CHECK_STR_EQ(row, k_rows);

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_mem.buf, g_mem.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t an;
    CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
    CHECK_EQ(an.n_columns, 22);
    CHECK_EQ(an.ragged_rows, 0);
    log_source_t src2;
    log_mem_ctx_t ctx2;
    log_source_memory(&src2, &ctx2, g_mem.buf, g_mem.len);
    int cols[3] = { 13, 14, 15 };
    log_data_t data;
    CHECK_EQ(log_csv_build(&src2, &an, cols, 3, &data), LOG_OK);
    CHECK_STR_EQ(data.field[0].name, "ch1 current");
    CHECK_STR_EQ(data.field[0].unit, "A");
    CHECK_STR_EQ(data.field[0].group, "INA3221");
    CHECK_STR_EQ(data.field[2].name, "ch1 voltage");
    CHECK_NEAR(data.value[0][0], 0.412f, 0.0005f);
    CHECK_NEAR(data.value[1][0], 1.638f, 0.0005f);
    CHECK_NEAR(data.value[2][0], 5.874f, 0.0005f);
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

/*
 * A commit after a failure is refused, however willing the sink is.
 *
 * The write failed and the file has a hole in it; a flush that succeeds
 * afterwards would clear the pending count and hand the caller a true, which
 * is a caller being told an incomplete file is safely on the card.  The
 * caller here is the panel's end-of-run commit, and what it does with a true
 * is report the run as written.
 */
TEST_CASE(a_commit_after_a_failure_is_refused)
{
    fresh(400);   /* fail once about four hundred bytes are in */
    log_writer_t w = writer();
    const int wrote = write_run(&w, 200);
    CHECK(wrote > 0);
    CHECK(log_writer_failed(&w));

    /* The sink is willing again -- a card that answers after a stall -- and
     * rows are still pending from before the failure. */
    g_mem.fail_after   = -1;
    g_mem.commit_fails = false;
    CHECK(!log_writer_commit(&w));
    CHECK(log_writer_failed(&w));
    /* And it does not clear the count on the way past, so nothing downstream
     * can read the writer as up to date. */
    CHECK(log_writer_pending(&w) > 0u);
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

/*
 * A bench run's rows carry the wall time since the arm (log_cadence.h): the
 * first row is not at 0, the rows come 52 to 53 ms apart, and a stretch
 * without samples is a step.  The reader counts time from the first row, so
 * the duration is the last row's time less the first's, the gap is found at
 * its length, and the rate is the rows' own.  The tick wraps inside the run.
 */
TEST_CASE(a_run_stamped_with_wall_time_reads_back_with_its_gap)
{
    fresh(-1);
    log_writer_t w = writer();
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.flags   = LINK_BN_VOLTAGE_OK;
    b.voltage = 12.0f;

    uint32_t now = 0xFFFFF000u;                 /* 4.096 s ahead of the wrap */
    log_cadence_t c;
    log_cadence_init(&c, now);
    float t = 0.0f;
    for (int i = 0; i < 300; ++i) {
        now += (i % 10 == 9) ? 52u : 53u;       /* 52.9 ms on average */
        if (i == 150) {
            now += 30000u;                      /* 30 s with no sample */
        }
        CHECK(log_cadence_row(&c, now, true, true, &t));
        CHECK(log_writer_row(&w, t, &b));
    }
    CHECK_NEAR(t, 300.0 * 0.0529 + 30.0, 1e-3);

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_mem.buf, g_mem.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t an;
    CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
    CHECK_EQ(an.time_index, 0);
    CHECK_STR_EQ(an.time_unit, "s");

    log_source_t src2;
    log_mem_ctx_t ctx2;
    log_source_memory(&src2, &ctx2, g_mem.buf, g_mem.len);
    int cols[1] = { 1 };
    log_data_t data;
    CHECK_EQ(log_csv_build(&src2, &an, cols, 1, &data), LOG_OK);
    CHECK_EQ(data.count, 300);
    CHECK_STR_EQ(data.time_name, "time");       /* kept: it never runs back */
    CHECK_NEAR(data.time[0], 0.0, 1e-6);
    CHECK_NEAR(data.time[149], 149.0 * 0.0529, 2e-3);
    CHECK_NEAR(data.time[150], 150.0 * 0.0529 + 30.0, 2e-3);
    /* 299 intervals of 52.9 ms and the 30 s. */
    CHECK_NEAR(data.duration_s, 299.0 * 0.0529 + 30.0, 2e-3);
    CHECK_NEAR(data.max_gap_s, 30.053, 2e-3);
    CHECK_NEAR(data.rate_hz, 1.0 / 0.053, 0.05);
    log_data_free(&data);
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

/* ------------------------------------------- one row per INA3221 window */

/*
 * One pass of the control task's log step, as advance_model_and_log() in
 * the panel's main.c makes it: the row decision, then one row for each
 * window the poll handed over and one when it handed over none, the sample
 * in the last.  Returns the rows written.
 */
static unsigned log_pass(log_writer_t *w, log_cadence_t *cad,
                         bench_state_t *bench, bool new_sample, bool run)
{
    float t_s = 0.0f;
    const bool logged = log_cadence_row(cad, chain_now(), new_sample, run,
                                        &t_s);
    const unsigned windows = sense_link_windows(&ch.sl);
    const unsigned rows    = log_cadence_rows(windows);
    unsigned written = 0u;
    for (unsigned k = 0u; k < rows; ++k) {
        if (k < windows) {
            (void)sense_link_take_window(&ch.sl, bench);
        }
        if (logged) {
            bench_state_t row;
            bench_state_log_row(bench, k + 1u == rows, &row);
            const bool ok = log_writer_row(w, t_s, &row);
            log_cadence_posted(cad, ok);
            written += ok ? 1u : 0u;
        }
        bench->servo_new = false;
    }
    return written;
}

/* What a bench log holds, read back cell by cell. */
typedef struct {
    unsigned rows;
    unsigned with_sample;     /* rows with a voltage cell                */
    unsigned with_window;     /* rows with a window number               */
    unsigned stepped;         /* window numbers not one after the last   */
    unsigned wrong;           /* a ch1 current that is another window's  */
    unsigned back;            /* a time before the row before            */
    unsigned same_time;       /* rows at the time of the row before      */
    unsigned bare;            /* window rows with no sample              */
    long     first, last;     /* window numbers                          */
} log_read_t;

static void read_log(log_read_t *r)
{
    memset(r, 0, sizeof(*r));
    r->first = r->last = -1;
    double last_t = -1.0;
    const char *p = strchr(g_mem.buf, '\n') + 1;       /* past the header */
    while (*p != '\0') {
        const char *end = strchr(p, '\n');
        char cell[22][24];
        unsigned n = 0u;
        const char *c = p;
        while (c <= end && n < 22u) {
            const char *sep = c;
            while (sep < end && *sep != ';') {
                ++sep;
            }
            const size_t len = (size_t)(sep - c);
            memcpy(cell[n], c, len);
            cell[n][len] = '\0';
            ++n;
            c = sep + 1;
        }
        CHECK_EQ(n, 22u);
        ++r->rows;
        const double t = atof(cell[0]);
        if (t < last_t) {
            ++r->back;
        }
        if (t == last_t) {
            ++r->same_time;
        }
        last_t = t;
        const bool sample = cell[1][0] != '\0';
        if (sample) {
            ++r->with_sample;
        }
        if (cell[12][0] != '\0') {
            const long win = atol(cell[12]);
            ++r->with_window;
            if (r->last >= 0 && ((r->last + 1) & 0xFFFF) != win) {
                ++r->stepped;
            }
            if (r->first < 0) {
                r->first = win;
            }
            r->last = win;
            if (lround(atof(cell[13]) * 1000.0)
                != chain_mean_ma((uint16_t)win)) {
                ++r->wrong;
            }
            if (!sample) {
                ++r->bare;
            }
        }
        p = end + 1;
    }
}

/* A bench sample that every poll brings, with a voltage to tell its row. */
static bench_state_t log_sample(void)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.flags   = LINK_BN_VOLTAGE_OK;
    b.voltage = 12.0f;
    b.valid   = true;
    return b;
}

/*
 * An armed bench polled every 53 ms for 20 s, the tick wrapping inside the
 * run: every 50 ms window of the INA3221 is one row, in order and under
 * its own number, every poll's sample is in exactly one row, and the poll
 * that catches a window up writes two rows at one time.
 */
TEST_CASE(a_bench_log_has_one_row_for_every_window_at_a_53_ms_poll)
{
    fresh(-1);
    log_writer_t w = writer();
    chain_start(LINK_PROTOCOL_MINOR, 0xFFFFE000u, 0x01u);
    chain_far(400u);
    chain_link_up();
    bench_state_t bench = log_sample();
    log_cadence_t cad;
    log_cadence_init(&cad, chain_now());
    /* Disarmed: the windows are taken and none is logged. */
    for (unsigned i = 0u; i < 20u; ++i) {
        chain_cycle(53u);
        CHECK_EQ(log_pass(&w, &cad, &bench, true, false), 0u);
        CHECK_EQ(sense_link_windows(&ch.sl), 0u);
    }
    CHECK_EQ(g_mem.len, 0u);
    const uint16_t before = bench.servo_window;   /* the last one taken */
    log_cadence_run_start(&cad, chain_now());
    const uint64_t began = ch.us;
    unsigned most = 0u;
    for (unsigned i = 0u; i < 380u; ++i) {
        chain_cycle(53u);
        const unsigned n = log_pass(&w, &cad, &bench, true, true);
        CHECK(n >= 1u);
        if (n > most) {
            most = n;
        }
    }
    CHECK(chain_now() < 0x00010000u);           /* across the tick wrap */
    CHECK_EQ(most, 2u);

    log_read_t r;
    read_log(&r);
    const unsigned closed = (unsigned)((ch.us - began) / 50000u);
    CHECK(r.with_window + 1u >= closed && r.with_window <= closed + 1u);
    CHECK_EQ(r.rows, r.with_window);            /* no row without one */
    CHECK_EQ(r.with_sample, 380u);              /* each sample once   */
    CHECK_EQ(r.bare, r.rows - 380u);
    CHECK_EQ(r.same_time, r.bare);
    CHECK(r.bare >= 20u);                       /* 3 ms a poll in 50  */
    CHECK_EQ(r.stepped, 0u);
    CHECK_EQ(r.wrong, 0u);
    CHECK_EQ(r.back, 0u);
    /* The run's first window is the one after the last taken before it:
     * a window taken while disarmed is in no row. */
    CHECK_EQ((uint16_t)((uint16_t)r.first - before), 1u);
    CHECK_EQ(log_cadence_sent(&cad), r.rows);
    CHECK_EQ(log_cadence_lost(&cad), 0);
    CHECK_EQ(sense_link_win_lost(&ch.sl), 0u);

    /* And the reader takes the file: its time column, no ragged row. */
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_mem.buf, g_mem.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t an;
    CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
    CHECK_EQ(an.n_columns, 22);
    CHECK_EQ(an.ragged_rows, 0);
    CHECK_EQ(an.time_index, 0);
    CHECK_EQ(an.row_count, (int)r.rows);
}

/*
 * Polls 150 ms apart: three windows a poll, three rows at one time, the
 * sample in the last.  And a poll without a window is still a row.
 */
TEST_CASE(a_poll_that_brings_three_windows_writes_three_rows)
{
    fresh(-1);
    log_writer_t w = writer();
    chain_start(LINK_PROTOCOL_MINOR, 1000u, 0x01u);
    chain_far(400u);
    chain_link_up();
    bench_state_t bench = log_sample();
    log_cadence_t cad;
    log_cadence_init(&cad, chain_now());
    for (unsigned i = 0u; i < 8u; ++i) {
        chain_cycle(150u);
        (void)log_pass(&w, &cad, &bench, true, false);
    }
    log_cadence_run_start(&cad, chain_now());
    for (unsigned i = 0u; i < 100u; ++i) {
        chain_cycle(150u);
        CHECK_EQ(log_pass(&w, &cad, &bench, true, true), 3u);
    }
    log_read_t r;
    read_log(&r);
    CHECK_EQ(r.rows, 300u);
    CHECK_EQ(r.with_window, 300u);
    CHECK_EQ(r.with_sample, 100u);
    CHECK_EQ(r.bare, 200u);
    CHECK_EQ(r.same_time, 200u);
    CHECK_EQ(r.stepped, 0u);
    CHECK_EQ(r.wrong, 0u);
    CHECK_EQ(r.back, 0u);
    /* The sample is in the row of the newest window: the third of each. */
    const char *row = strchr(g_mem.buf, '\n') + 1;
    for (unsigned k = 0u; k < 3u; ++k) {
        const char *sep = strchr(row, ';');
        CHECK_EQ(sep[1] == ';', k < 2u);       /* voltage cell empty */
        row = strchr(row, '\n') + 1;
    }

    /* A poll with no window -- SERVO_WIN's reply lost -- is one row with
     * the window cells empty, and the windows follow in the next. */
    fresh(-1);
    w = writer();
    ch.lose_win = 1u;
    chain_cycle(53u);
    CHECK_EQ(log_pass(&w, &cad, &bench, true, true), 1u);
    chain_cycle(53u);
    CHECK(log_pass(&w, &cad, &bench, true, true) >= 2u);
    read_log(&r);
    CHECK_EQ(r.with_sample, 2u);
    CHECK_EQ(r.with_window, r.rows - 1u);
    CHECK_EQ(r.stepped, 0u);
    /* A pass without a sample writes nothing. */
    const size_t len = g_mem.len;
    CHECK_EQ(log_pass(&w, &cad, &bench, false, true), 0u);
    CHECK_EQ(g_mem.len, len);
}

int main(void)
{
    RUN(a_written_run_reads_back);
    RUN(the_header_carries_the_units_and_no_row_is_wasted);
    RUN(a_column_nothing_measured_is_empty_rather_than_zero);
    RUN(charge_and_energy_are_written_only_when_counted);
    RUN(the_values_survive_the_round_trip);
    RUN(the_sources_survive_the_round_trip);
    RUN(each_window_goes_into_one_row);
    RUN(a_non_finite_reading_is_written_as_an_absent_cell);
    RUN(the_header_is_written_once_and_without_being_asked);
    RUN(a_failing_sink_latches_and_stops);
    RUN(a_commit_after_a_failure_is_refused);
    RUN(what_survived_a_failure_still_reads);
    RUN(a_writer_with_no_sink_fails_rather_than_crashes);
    RUN(no_more_than_one_interval_of_a_run_is_ever_uncommitted);
    RUN(what_a_power_cut_leaves_is_a_short_run_and_not_an_empty_file);
    RUN(a_slow_run_is_committed_on_the_clock_rather_than_on_the_count);
    RUN(a_run_stamped_with_wall_time_reads_back_with_its_gap);
    RUN(a_commit_by_hand_keeps_the_tail_and_an_empty_one_costs_nothing);
    RUN(a_commit_the_sink_refuses_latches_the_writer);
    RUN(a_sink_that_needs_no_commit_still_writes_the_whole_run);
    RUN(a_bench_log_has_one_row_for_every_window_at_a_53_ms_poll);
    RUN(a_poll_that_brings_three_windows_writes_three_rows);
    return test_summary("logwriter");
}
