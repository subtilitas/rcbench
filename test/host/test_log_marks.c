/**
 * @file test_log_marks.c
 * @brief The traces and markers of LOGS' XY graph (log_marks.h), read from
 *        the files the bench writes: a LIMIT RUN of servo_limrun_rig.h
 *        written by the run and read back, its bound rows the markers;
 *        LIMIT's traces split by search and side, searches 2 and 3 dashed;
 *        a side with no bound; RUDDR's SEARCH and STEPS pictures, of a file
 *        of 13 columns and of one of firmware 0.23.17; a file of one row;
 *        a blank line, which the build skips; the row ceiling; the marker
 *        ceiling; and the walk of the rows (log_csv_each_row()).
 *
 * The failures under test: a row in the wrong trace or in none; a trace
 * that joins two searches or two sides; a marker missing, doubled or at
 * another pulse than the file's; a STEP row drawn on SEARCH or a SEARCH row
 * on STEPS; a row index that drifts from the build's sample index.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_limrun_rig.h"

#include "log_fields.h"
#include "log_marks.h"
#include "log_preset.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

static log_analysis_t g_an;
static log_markset_t g_mk;
static log_data_t g_data;
static log_mem_ctx_t g_ctx;

/**
 * @brief Analyse @p text, build its first numeric columns and read its
 *        marks for @p id.
 * @return LOG_OK, or the first step's error.
 */
static log_err_t read_marks(const char *text, log_preset_id_t id)
{
    log_source_t src;
    log_source_memory(&src, &g_ctx, text, strlen(text));
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_err_t err = log_csv_analyse(&src, &opts, &g_an);
    if (err != LOG_OK) {
        return err;
    }
    int picks[LOG_MAX_SERIES];
    const int n = log_preset_picks(id, &g_an, picks, LOG_MAX_SERIES);
    log_data_free(&g_data);
    err = log_csv_build(&src, &g_an, n > 0 ? picks : NULL, n, &g_data);
    if (err != LOG_OK) {
        return err;
    }
    return log_marks_read(&src, &g_an, id, &g_mk);
}

/** @brief Row @p i's trace. */
static int trace_of(int i)
{
    return g_mk.row[i] & LOG_ROW_TRACE;
}

/** @brief How many markers of @p kind there are; @p at takes the first's
 *         x (a line) or level. */
static int marks_of(log_mark_kind_t kind, float *at)
{
    int n = 0;
    for (int m = 0; m < g_mk.n_marks; ++m) {
        if (g_mk.mark[m].kind == kind) {
            if (n == 0 && at != NULL) {
                *at = g_mk.mark[m].at;
            }
            ++n;
        }
    }
    return n;
}

/** @brief Whether a line of @p kind stands at @p x. */
static bool line_at(log_mark_kind_t kind, float x)
{
    for (int m = 0; m < g_mk.n_marks; ++m) {
        if (g_mk.mark[m].kind == kind && !g_mk.mark[m].level
            && g_mk.mark[m].at == x) {
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------- LIMIT */

/* A run on servo_sim, search only: what the run wrote is read back; its
 * center, both bound pulses and end points stand where the run found them,
 * the levels are the run's free current and threshold, and every row is
 * in the trace of its side. */
TEST_CASE(a_limit_run_written_by_the_run_reads_back_its_markers)
{
    rig_init(0u, false);
    g.cfg.program = false;
    g.enc = true;
    REQUIRE(start());
    run_for(RUN_MS);
    REQUIRE(g.run.verdict == SERVO_LIMRUN_FOUND);
    REQUIRE(read_marks(g.csv, LOG_PRESET_LIMIT) == LOG_OK);
    CHECK_EQ(g_mk.rows, (int)g.run.rows_n);
    CHECK_EQ(g_mk.rows, g_data.count);
    CHECK(!g_mk.cut);
    CHECK_EQ(g_mk.n_traces, 2);
    CHECK_EQ(g_mk.trace[0].side, LOG_SIDE_LOW);
    CHECK_EQ(g_mk.trace[1].side, LOG_SIDE_HIGH);
    CHECK(!g_mk.trace[0].dashed && !g_mk.trace[1].dashed);
    for (unsigned k = 0u; k < g.run.rows_n; ++k) {
        CHECK_EQ(trace_of((int)k), g.run.rows[k].side == SERVO_LIMRUN_HI);
    }
    float at = 0.0f;
    CHECK_EQ(marks_of(LOG_MARK_CENTER, &at), 1);
    CHECK_FEQ(at, (float)CENTER);
    CHECK_EQ(marks_of(LOG_MARK_BOUND, NULL), 2);
    CHECK_EQ(marks_of(LOG_MARK_END, NULL), 2);
    for (int s = 0; s < 2; ++s) {
        const servo_limrun_side_t *f = &g.run.first[s];
        CHECK(line_at(LOG_MARK_BOUND, (float)f->bind_us));
        CHECK(line_at(LOG_MARK_END, (float)f->limit_us));
    }
    /* Each search ends on its bound row: no LAST beside a BOUND. */
    CHECK_EQ(marks_of(LOG_MARK_LAST, NULL), 0);
    CHECK_EQ(marks_of(LOG_MARK_FREE, NULL), 2);
    CHECK_EQ(marks_of(LOG_MARK_THRESHOLD, NULL), 2);
    const int cur = log_fields_find(&g_an, "current", true);
    for (int m = 0; m < g_mk.n_marks; ++m) {
        const log_mark_t *mk = &g_mk.mark[m];
        if (mk->level) {
            CHECK_EQ(mk->column, cur);
            CHECK_FEQ(mk->x0, (float)CENTER);
            CHECK(mk->x1 != (float)CENTER);
        }
    }
    /* The low side's free current and threshold, as the run judged them. */
    const servo_limrun_side_t *lo = &g.run.first[0];
    bool free_ok = false;
    for (int m = 0; m < g_mk.n_marks; ++m) {
        const log_mark_t *mk = &g_mk.mark[m];
        if (mk->kind == LOG_MARK_FREE && mk->x1 < (float)CENTER) {
            CHECK_NEAR(mk->at, lo->baseline_a, 0.0015f);
            free_ok = true;
        }
    }
    CHECK(free_ok);
}

/* A LIMIT file with the overset's search and the last check: a trace for
 * each search of each side, in the order they first appear, the first
 * search solid and the others dashed. */
static const char k_limit3[] =
    "row;search;side;pulse (us);current (A);voltage (V);angle (deg);state;"
    "free (A);threshold (A);end point (us);info\n"
    "1;1;LOW;1500;0.050;6.00;0.0;BASE;0.050;;;dut=X10\n"
    "2;1;LOW;1490;0.051;6.00;-0.9;FREE;0.050;0.200;;\n"
    "3;1;LOW;1480;0.300;6.00;-1.8;BOUND;0.050;0.200;1505;\n"
    "4;1;HIGH;1500;0.050;6.00;0.0;BASE;0.050;;;\n"
    "5;1;HIGH;1510;0.300;6.00;0.9;BOUND;0.050;0.200;1485;\n"
    "6;2;LOW;1500;0.050;6.00;0.0;BASE;0.050;;;\n"
    "7;2;LOW;1470;0.320;6.00;-2.7;BOUND;0.052;0.202;1495;\n"
    "8;3;HIGH;1500;0.050;6.00;0.0;BASE;0.050;;;\n"
    "9;3;HIGH;1520;0.310;6.00;1.8;BOUND;0.051;0.201;1495;\n";

TEST_CASE(limit_traces_split_by_search_and_side_and_dash_the_later_ones)
{
    REQUIRE(read_marks(k_limit3, LOG_PRESET_LIMIT) == LOG_OK);
    CHECK_EQ(g_mk.rows, 9);
    CHECK_EQ(g_mk.n_traces, 4);
    static const int want[9] = { 0, 0, 0, 1, 1, 2, 2, 3, 3 };
    for (int i = 0; i < 9; ++i) {
        CHECK_EQ(trace_of(i), want[i]);
    }
    CHECK_EQ(g_mk.trace[0].search, 1u);
    CHECK_EQ(g_mk.trace[2].search, 2u);
    CHECK_EQ(g_mk.trace[3].search, 3u);
    CHECK(!g_mk.trace[1].dashed);
    CHECK(g_mk.trace[2].dashed && g_mk.trace[3].dashed);
    CHECK_EQ(g_mk.trace[2].side, LOG_SIDE_LOW);
    /* The last BOUND row of each side: search 2's on LOW, 3's on HIGH. */
    CHECK_EQ(marks_of(LOG_MARK_BOUND, NULL), 2);
    CHECK(line_at(LOG_MARK_BOUND, 1470.0f));
    CHECK(line_at(LOG_MARK_BOUND, 1520.0f));
    CHECK(line_at(LOG_MARK_END, 1495.0f));
    CHECK(!line_at(LOG_MARK_END, 1505.0f));
    /* Their levels, from the last row with a threshold. */
    float thr = 0.0f;
    CHECK_EQ(marks_of(LOG_MARK_THRESHOLD, &thr), 2);
    CHECK_NEAR(thr, 0.202f, 1e-6f);
}

/* The high side ran to the end of its range free: no BOUND and no END POINT
 * there, a LAST at its outermost pulse, the low side's markers as they
 * are. */
static const char k_limit_free[] =
    "row;search;side;pulse (us);current (A);voltage (V);angle (deg);state;"
    "free (A);threshold (A);end point (us);info\n"
    "1;1;LOW;1500;0.050;6.00;;BASE;0.050;;;\n"
    "2;1;LOW;1490;0.300;6.00;;BOUND;0.050;0.200;1515;\n"
    "3;1;HIGH;1500;0.050;6.00;;BASE;0.050;;;\n"
    "4;1;HIGH;1510;0.052;6.00;;FREE;0.050;0.200;;\n"
    "5;1;HIGH;1520;0.053;6.00;;FREE;0.050;0.200;;\n";

TEST_CASE(a_side_with_no_bound_marks_its_last_position)
{
    REQUIRE(read_marks(k_limit_free, LOG_PRESET_LIMIT) == LOG_OK);
    CHECK_EQ(marks_of(LOG_MARK_BOUND, NULL), 1);
    CHECK(line_at(LOG_MARK_BOUND, 1490.0f));
    CHECK_EQ(marks_of(LOG_MARK_END, NULL), 1);
    CHECK(line_at(LOG_MARK_END, 1515.0f));
    CHECK_EQ(marks_of(LOG_MARK_LAST, NULL), 1);
    CHECK(line_at(LOG_MARK_LAST, 1520.0f));
    /* Both sides keep their levels: the free side's run to 1520us. */
    CHECK_EQ(marks_of(LOG_MARK_FREE, NULL), 2);
    bool high = false;
    for (int m = 0; m < g_mk.n_marks; ++m) {
        if (g_mk.mark[m].kind == LOG_MARK_FREE && g_mk.mark[m].x1 == 1520.0f) {
            high = g_mk.mark[m].x0 == 1500.0f;
        }
        /* The low side's level runs from the center down. */
        if (g_mk.mark[m].kind == LOG_MARK_FREE && g_mk.mark[m].x1 == 1490.0f) {
            CHECK_FEQ(g_mk.mark[m].x0, 1500.0f);
        }
    }
    CHECK(high);
}

/* One row: the center and its side's LAST at one pulse, one trace. */
TEST_CASE(a_file_of_one_row_has_one_trace_and_its_center)
{
    static const char one[] =
        "row;search;side;pulse (us);current (A);voltage (V);angle (deg);"
        "state;free (A);threshold (A);end point (us);info\n"
        "1;1;LOW;1500;0.050;6.00;;BASE;0.050;;;\n";
    REQUIRE(read_marks(one, LOG_PRESET_LIMIT) == LOG_OK);
    CHECK_EQ(g_mk.rows, 1);
    CHECK_EQ(g_data.count, 1);
    CHECK_EQ(g_mk.n_traces, 1);
    CHECK_EQ(trace_of(0), 0);
    CHECK(line_at(LOG_MARK_CENTER, 1500.0f));
    CHECK(line_at(LOG_MARK_LAST, 1500.0f));
    CHECK_EQ(marks_of(LOG_MARK_FREE, NULL), 0);
}

/* A blank line inside the file: the build skips it, and so does the walk,
 * so row 2 of the marks is sample 2 of the build. */
TEST_CASE(a_blank_line_keeps_the_rows_in_step_with_the_build)
{
    static const char gap[] =
        "row;search;side;pulse (us);current (A);voltage (V);angle (deg);"
        "state;free (A);threshold (A);end point (us);info\n"
        "1;1;LOW;1500;0.050;6.00;;BASE;0.050;;;\n"
        "\n"
        ";;;;;;;;;;;\n"
        "2;1;LOW;1490;0.051;6.00;;FREE;0.050;0.200;;\n"
        "3;1;HIGH;1500;0.050;6.00;;BASE;0.050;;;\n";
    REQUIRE(read_marks(gap, LOG_PRESET_LIMIT) == LOG_OK);
    CHECK_EQ(g_mk.rows, g_data.count);
    CHECK_EQ(g_data.count, 3);
    CHECK_FEQ(g_data.value[0][2], 1500.0f);
    CHECK_EQ(trace_of(1), 0);
    CHECK_EQ(trace_of(2), 1);
}

/* ---------------------------------------------------------------- RUDDR */

/* RUDDRnnn.CSV as servo_rudder_auto_report.c writes it: four steps a side
 * at 1000us and 2000us, the last of each after a write and the second of
 * the low side outside the tolerance, three verifies, then the search from
 * the center down and up. */
static const char k_ruddr[] =
    "step;side (us);write;value;measured (deg);goal (deg);measured (mm);"
    "goal (mm);within;kind;pulse (us);current (A);info\n"
    "1;1000;0;;-24.00;-30.00;;;0;STEP;1000;;dut=X10\n"
    "2;1000;1;120;-29.20;-30.00;;;1;STEP;1000;;\n"
    "3;2000;0;;25.00;30.00;;;0;STEP;2000;;\n"
    "4;2000;1;125;30.10;30.00;;;1;STEP;2000;;\n"
    "5;1000;V;;-29.50;-30.00;;;1;VERIFY;1000;;\n"
    "6;2000;V;;30.00;30.00;;;1;VERIFY;2000;;\n"
    "7;1500;V;;0.40;0.00;;;0;VERIFY;1500;;\n"
    "8;1500;S;50;0.00;;;;;SEARCH;1500;0.050\n"
    "9;1490;S;52;-0.90;;;;;SEARCH;1490;0.052\n"
    "10;1480;S;300;-1.80;;;;;SEARCH;1480;0.300\n"
    "11;1500;S;50;0.00;;;;;SEARCH;1500;0.050\n"
    "12;1510;S;51;0.90;;;;;SEARCH;1510;0.051\n"
    "13;1520;S;310;1.80;;;;;SEARCH;1520;0.310\n";

TEST_CASE(ruddr_search_draws_the_search_rows_a_trace_a_side)
{
    REQUIRE(read_marks(k_ruddr, LOG_PRESET_RUDDR_SEARCH) == LOG_OK);
    CHECK_EQ(g_mk.rows, 13);
    for (int i = 0; i < 7; ++i) {
        CHECK_EQ(trace_of(i), (int)LOG_TRACE_NONE);
    }
    for (int i = 7; i < 10; ++i) {
        CHECK_EQ(trace_of(i), 0);
    }
    for (int i = 10; i < 13; ++i) {
        CHECK_EQ(trace_of(i), 1);
    }
    CHECK_EQ(g_mk.n_traces, 2);
    CHECK_EQ(g_mk.trace[0].side, LOG_SIDE_LOW);
    CHECK_EQ(g_mk.trace[1].side, LOG_SIDE_HIGH);
    CHECK(line_at(LOG_MARK_CENTER, 1500.0f));
    CHECK_EQ(marks_of(LOG_MARK_CENTER, NULL), 1);
    CHECK(line_at(LOG_MARK_END, 1000.0f));
    CHECK(line_at(LOG_MARK_END, 2000.0f));
    CHECK_EQ(marks_of(LOG_MARK_END, NULL), 2);
    CHECK(line_at(LOG_MARK_LAST, 1480.0f));
    CHECK(line_at(LOG_MARK_LAST, 1520.0f));
    CHECK_EQ(marks_of(LOG_MARK_FREE, NULL), 0);
}

/* A search that reaches a side's end point: END POINT there, no LAST
 * beside it; the other side's LAST stays. */
TEST_CASE(a_search_ending_on_its_end_point_marks_it_once)
{
    static const char reach[] =
        "step;side (us);write;value;measured (deg);goal (deg);"
        "measured (mm);goal (mm);within;kind;pulse (us);current (A);info\n"
        "1;1490;0;;-1.00;-1.00;;;1;STEP;1490;;\n"
        "2;2000;0;;1.00;1.00;;;1;STEP;2000;;\n"
        "3;1500;S;50;0.00;;;;;SEARCH;1500;0.050\n"
        "4;1490;S;52;-0.90;;;;;SEARCH;1490;0.052\n"
        "5;1500;S;50;0.00;;;;;SEARCH;1500;0.050\n"
        "6;1510;S;51;0.90;;;;;SEARCH;1510;0.051\n";
    REQUIRE(read_marks(reach, LOG_PRESET_RUDDR_SEARCH) == LOG_OK);
    CHECK(line_at(LOG_MARK_END, 1490.0f));
    CHECK(!line_at(LOG_MARK_LAST, 1490.0f));
    CHECK(line_at(LOG_MARK_LAST, 1510.0f));
    CHECK_EQ(marks_of(LOG_MARK_LAST, NULL), 1);
}

TEST_CASE(ruddr_steps_draws_the_steps_a_trace_a_side_and_the_verifies)
{
    REQUIRE(read_marks(k_ruddr, LOG_PRESET_RUDDR_STEPS) == LOG_OK);
    CHECK_EQ(g_mk.n_traces, 3);
    CHECK_EQ(trace_of(0), 0);
    CHECK_EQ(trace_of(1), 0);
    CHECK_EQ(trace_of(2), 1);
    CHECK_EQ(trace_of(3), 1);
    CHECK_EQ(trace_of(4), 2);
    CHECK_EQ(trace_of(6), 2);
    for (int i = 7; i < 13; ++i) {
        CHECK_EQ(trace_of(i), (int)LOG_TRACE_NONE);
    }
    CHECK_EQ(g_mk.trace[0].side, LOG_SIDE_LOW);
    CHECK_EQ(g_mk.trace[1].side, LOG_SIDE_HIGH);
    CHECK(g_mk.trace[2].points);
    CHECK(!g_mk.trace[0].points);
    /* Outside the tolerance: steps 1 and 3 and the center's verify. */
    CHECK((g_mk.row[0] & LOG_ROW_WARN) != 0u);
    CHECK((g_mk.row[1] & LOG_ROW_WARN) == 0u);
    CHECK((g_mk.row[2] & LOG_ROW_WARN) != 0u);
    CHECK((g_mk.row[6] & LOG_ROW_WARN) != 0u);
    /* A tick on the steps measured after a write. */
    CHECK((g_mk.row[0] & LOG_ROW_TICK) == 0u);
    CHECK((g_mk.row[1] & LOG_ROW_TICK) != 0u);
    CHECK((g_mk.row[3] & LOG_ROW_TICK) != 0u);
    CHECK((g_mk.row[4] & LOG_ROW_TICK) == 0u);
    CHECK_EQ(g_mk.n_marks, 0);
}

/* A RUDDR file of firmware 0.23.17: no kind, the rows told by write.  STEPS
 * reads it as the new one; SEARCH has no pulse column to place a row on. */
TEST_CASE(a_ruddr_file_of_0_23_17_is_told_by_its_write_column)
{
    static const char old[] =
        "step;side (us);write;value;measured (deg);goal (deg);"
        "measured (mm);goal (mm);within\n"
        "1;1000;0;;-24.00;-30.00;;;0\n"
        "2;2000;1;125;30.10;30.00;;;1\n"
        "3;1000;V;;-29.50;-30.00;;;1\n"
        "4;1500;S;50;0.00;;;;\n";
    REQUIRE(read_marks(old, LOG_PRESET_RUDDR_STEPS) == LOG_OK);
    CHECK_EQ(trace_of(0), 0);
    CHECK_EQ(trace_of(1), 1);
    CHECK_EQ(trace_of(2), 2);
    CHECK_EQ(trace_of(3), (int)LOG_TRACE_NONE);
    CHECK((g_mk.row[1] & LOG_ROW_TICK) != 0u);
    REQUIRE(read_marks(old, LOG_PRESET_RUDDR_SEARCH) == LOG_OK);
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(trace_of(i), (int)LOG_TRACE_NONE);
    }
    CHECK_EQ(g_mk.n_marks, 0);
}

/* ------------------------------------------------------------ ceilings */

/* LOG_MARKS_ROWS rows classified, the next one sets cut. */
TEST_CASE(rows_past_the_ceiling_are_cut)
{
    static char big[(LOG_MARKS_ROWS + 2) * 48];
    size_t at = (size_t)snprintf(big, sizeof(big),
                                 "row;search;side;pulse (us);current (A);"
                                 "state\n");
    for (int i = 0; i < LOG_MARKS_ROWS + 1; ++i) {
        at += (size_t)snprintf(big + at, sizeof(big) - at,
                               "%d;1;LOW;%d;0.05;FREE\n", i + 1, 1500 - i);
    }
    REQUIRE(read_marks(big, LOG_PRESET_LIMIT) == LOG_OK);
    CHECK(g_mk.cut);
    CHECK_EQ(g_mk.rows, LOG_MARKS_ROWS);
    CHECK_EQ(g_data.count, LOG_MARKS_ROWS + 1);
    /* A file of exactly the ceiling is whole. */
    char *nl = big;
    for (int i = 0; i < LOG_MARKS_ROWS + 1; ++i) {
        nl = strchr(nl, '\n') + 1;
    }
    *nl = '\0';
    REQUIRE(read_marks(big, LOG_PRESET_LIMIT) == LOG_OK);
    CHECK(!g_mk.cut);
    CHECK_EQ(g_mk.rows, LOG_MARKS_ROWS);
}

/* The 256th marker is kept, the 257th counted. */
TEST_CASE(markers_past_the_ceiling_are_counted)
{
    memset(&g_mk, 0, sizeof(g_mk));
    const log_mark_t m = { LOG_MARK_LAST, false, -1, 1.0f, 1.0f, 1.0f, -1, -1 };
    for (int i = 0; i < LOG_MARKS_MAX; ++i) {
        CHECK(log_marks_add(&g_mk, &m));
    }
    CHECK(!log_marks_add(&g_mk, &m));
    CHECK_EQ(g_mk.n_marks, LOG_MARKS_MAX);
    CHECK_EQ(g_mk.dropped, 1);
    CHECK(!log_marks_add(NULL, &m));
    CHECK(!log_marks_add(&g_mk, NULL));
}

/** @brief log_row_fn: counts the rows and stops after 2. */
static bool count_two(void *ctx, int row, const char *const *cells, int n)
{
    (void)cells;
    (void)n;
    *(int *)ctx = row + 1;
    return row < 1;
}

/* The walk: NULL refused, a stop asked for ends it, any other preset puts
 * every row in trace 0 and sets no marker. */
TEST_CASE(the_walk_refuses_null_stops_when_asked_and_knows_its_presets)
{
    REQUIRE(read_marks(k_limit3, LOG_PRESET_LIMIT) == LOG_OK);
    log_source_t src;
    log_source_memory(&src, &g_ctx, k_limit3, strlen(k_limit3));
    int rows = 0;
    CHECK_EQ(log_csv_each_row(NULL, &g_an, count_two, &rows), LOG_ERR_ARG);
    CHECK_EQ(log_csv_each_row(&src, NULL, count_two, &rows), LOG_ERR_ARG);
    CHECK_EQ(log_csv_each_row(&src, &g_an, NULL, &rows), LOG_ERR_ARG);
    CHECK_EQ(log_csv_each_row(&src, &g_an, count_two, &rows), LOG_OK);
    CHECK_EQ(rows, 2);
    CHECK_EQ(log_marks_read(NULL, &g_an, LOG_PRESET_LIMIT, &g_mk),
             LOG_ERR_ARG);
    CHECK_EQ(log_marks_read(&src, &g_an, LOG_PRESET_ATEST, &g_mk), LOG_OK);
    CHECK_EQ(g_mk.n_traces, 1);
    CHECK_EQ(trace_of(8), 0);
    CHECK_EQ(g_mk.n_marks, 0);
}

int main(void)
{
    RUN(a_limit_run_written_by_the_run_reads_back_its_markers);
    RUN(limit_traces_split_by_search_and_side_and_dash_the_later_ones);
    RUN(a_side_with_no_bound_marks_its_last_position);
    RUN(a_file_of_one_row_has_one_trace_and_its_center);
    RUN(a_blank_line_keeps_the_rows_in_step_with_the_build);
    RUN(ruddr_search_draws_the_search_rows_a_trace_a_side);
    RUN(a_search_ending_on_its_end_point_marks_it_once);
    RUN(ruddr_steps_draws_the_steps_a_trace_a_side_and_the_verifies);
    RUN(a_ruddr_file_of_0_23_17_is_told_by_its_write_column);
    RUN(rows_past_the_ceiling_are_cut);
    RUN(markers_past_the_ceiling_are_counted);
    RUN(the_walk_refuses_null_stops_when_asked_and_knows_its_presets);
    log_data_free(&g_data);
    return test_summary("log_marks");
}
