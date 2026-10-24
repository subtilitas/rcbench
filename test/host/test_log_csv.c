/*
 * Host unit tests for the CSV analysis (shared/logfile/log_csv.c): finding
 * the delimiter, the decimal convention, the units and the time axis of a
 * file, building the arrays that are plotted, the row cap, the allocator and
 * the error paths.
 *
 * The cases follow logwiju's test/verify-csv.mjs, run against the same four
 * fixtures under fixtures/.  Further cases cover what this reader does that
 * the browser one does not: streaming a file it cannot hold, a row cap, and
 * a file that cannot be read to its end.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <stdlib.h>
#include <string.h>

#include "log_csv.h"
#include "log_fields.h"
#include "log_numbers.h"

#include "logfile_rig.h"

/* ------------------------------------------------------------------ csv --- */

static const log_column_t *column_named(const log_analysis_t *a, const char *name)
{
    for (int i = 0; i < a->n_columns; ++i) {
        if (strcmp(a->columns[i].name, name) == 0) {
            return &a->columns[i];
        }
    }
    return NULL;
}

static const log_field_t *field_named(const log_data_t *d, const char *name)
{
    for (int i = 0; i < d->n_fields; ++i) {
        if (strcmp(d->field[i].name, name) == 0) {
            return &d->field[i];
        }
    }
    return NULL;
}

static const float *series_named(const log_data_t *d, const char *name)
{
    for (int i = 0; i < d->n_fields; ++i) {
        if (strcmp(d->field[i].name, name) == 0) {
            return d->value[i];
        }
    }
    return NULL;
}

static void check_series(const log_data_t *d, const char *name,
                         const double *want, int n)
{
    const float *got = series_named(d, name);
    if (got == NULL) {
        T_FAIL("no series \"%s\"", name);
        return;
    }
    if (d->count != n) {
        T_FAIL("%s: %d rows, want %d", name, d->count, n);
        return;
    }
    for (int i = 0; i < n; ++i) {
        CHECK_NEAR(got[i], want[i], 1e-3);
    }
}

TEST_CASE(english_file_reads_as_english)
{
    if (!load_fixture("en.csv")) {
        return;
    }
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&g_src, NULL, &a), LOG_OK);
    CHECK_EQ(a.convention, LOG_CONV_EN);
    CHECK_EQ(a.delimiter, ',');
    CHECK_EQ(a.n_columns, 5);
    CHECK_EQ(a.row_count, 4);
    CHECK_EQ(a.ragged_rows, 0);
    CHECK(a.time_index >= 0);
    CHECK_STR_EQ(a.columns[a.time_index].name, "time");

    static const char *const want_units[] = { "", "V", "A", "", "" };
    for (int i = 0; i < a.n_columns; ++i) {
        CHECK_STR_EQ(a.columns[i].unit, want_units[i]);
    }

    log_data_t d;
    CHECK_EQ(log_csv_build(&g_src, &a, NULL, 0, &d), LOG_OK);
    CHECK_EQ(d.count, 4);
    static const double volts[] = { 22.34, 22.31, 22.28, 22.20 };
    static const double amps[] = { 10.23, 11.05, 12.40, 15.02 };
    static const double rpm[] = { 1234, 1567, 1890, 2345 };
    check_series(&d, "voltage", volts, 4);
    check_series(&d, "current", amps, 4);
    check_series(&d, "rpm", rpm, 4);
    CHECK_NEAR(d.duration_s, 0.06, 1e-5);

    const log_field_t *v = field_named(&d, "voltage");
    if (v != NULL) {
        CHECK_STR_EQ(v->unit, "V");
    }
    log_data_free(&d);
}

TEST_CASE(german_file_reads_as_german)
{
    if (!load_fixture("de.csv")) {
        return;
    }
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&g_src, NULL, &a), LOG_OK);
    CHECK_EQ(a.convention, LOG_CONV_DE);
    CHECK_EQ(a.delimiter, ';');
    CHECK(a.time_index >= 0);
    CHECK_STR_EQ(a.columns[a.time_index].name, "Zeit");

    log_data_t d;
    CHECK_EQ(log_csv_build(&g_src, &a, NULL, 0, &d), LOG_OK);
    /* The same physical numbers as the English file. */
    static const double volts[] = { 22.34, 22.31, 22.28, 22.20 };
    static const double amps[] = { 10.23, 11.05, 12.40, 15.02 };
    static const double rpm[] = { 1234, 1567, 1890, 2345 };
    check_series(&d, "Spannung", volts, 4);
    check_series(&d, "Strom", amps, 4);
    check_series(&d, "Drehzahl", rpm, 4); /* the dot groups thousands here */
    CHECK_NEAR(d.duration_s, 0.06, 1e-5);

    const log_field_t *f = field_named(&d, "Spannung");
    if (f != NULL) {
        CHECK_STR_EQ(f->unit, "V");
    }
    f = field_named(&d, "Strom");
    if (f != NULL) {
        CHECK_STR_EQ(f->unit, "A");
    }
    log_data_free(&d);
}

TEST_CASE(header_units_and_a_millisecond_time_axis)
{
    if (!load_fixture("units-header.csv")) {
        return;
    }
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&g_src, NULL, &a), LOG_OK);
    static const char *const want_units[] = { "ms", "m", "km/h", "W" };
    for (int i = 0; i < a.n_columns && i < 4; ++i) {
        CHECK_STR_EQ(a.columns[i].unit, want_units[i]);
    }
    CHECK_STR_EQ(a.time_unit, "ms");
    CHECK_EQ(a.ragged_rows, 0); /* the grouped numbers are quoted */

    log_data_t d;
    CHECK_EQ(log_csv_build(&g_src, &a, NULL, 0, &d), LOG_OK);
    CHECK_NEAR(d.duration_s, 0.3, 1e-5);
    static const double power[] = { 1500, 1520, 1610, 1700 };
    check_series(&d, "power", power, 4);
    log_data_free(&d);
}

TEST_CASE(forcing_the_wrong_convention_rejects_rather_than_corrupts)
{
    if (!load_fixture("en.csv")) {
        return;
    }
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    opts.convention = LOG_CONV_DE;

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&g_src, &opts, &a), LOG_OK);
    CHECK(a.convention_forced);
    const log_column_t *v = column_named(&a, "voltage");
    if (v == NULL) {
        T_FAIL("no voltage column");
        return;
    }
    CHECK_EQ(v->parsed, 0);
    CHECK(!v->numeric);
}

TEST_CASE(ragged_rows_are_reported_not_absorbed)
{
    if (!load_fixture("ragged.csv")) {
        return;
    }
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&g_src, NULL, &a), LOG_OK);
    CHECK_EQ(a.ragged_rows, 2);
    CHECK_EQ(a.max_fields, 3);
}

TEST_CASE(the_row_cap_is_reported_rather_than_silent)
{
    /* The row cap stops the read and reports it.  A viewer that shows the
     * first N rows without saying so misrepresents the run. */
    static char text[4096];
    size_t n = (size_t)snprintf(text, sizeof(text), "t,v\n");
    for (int i = 0; i < 200; ++i) {
        n += (size_t)snprintf(text + n, sizeof(text) - n, "%d,%d\n", i, i * 2);
    }

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, n);

    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    opts.max_rows = 50;

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
    CHECK_EQ(a.row_count, 50);
    CHECK(a.truncated);

    log_data_t d;
    CHECK_EQ(log_csv_build(&src, &a, NULL, 0, &d), LOG_OK);
    CHECK_EQ(d.count, 50);
    CHECK_NEAR(d.value[0][49], 98.0, 1e-6);
    CHECK_NEAR(d.rate_hz, 1.0, 1e-6);
    log_data_free(&d);
}

TEST_CASE(a_file_with_no_numbers_is_an_error_not_an_empty_plot)
{
    static const char text[] = "name,note\nalpha,one\nbeta,two\n";
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, sizeof(text) - 1u);

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    CHECK(!a.columns[0].numeric);
    CHECK_EQ(a.time_index, -1);

    log_data_t d;
    CHECK_EQ(log_csv_build(&src, &a, NULL, 0, &d), LOG_ERR_NO_NUMERIC);
    log_data_free(&d);
}

TEST_CASE(without_a_time_column_the_x_axis_is_the_row_index)
{
    static const char text[] = "a,b\n5,1\n3,2\n9,3\n";
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, sizeof(text) - 1u);

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    /* Column "a" is numeric but not monotonic, and neither name says time. */
    CHECK_EQ(a.time_index, -1);

    log_data_t d;
    CHECK_EQ(log_csv_build(&src, &a, NULL, 0, &d), LOG_OK);
    CHECK_EQ(d.count, 3);
    CHECK_NEAR(d.time[0], 0.0, 1e-9);
    CHECK_NEAR(d.time[2], 2.0, 1e-9);
    CHECK_STR_EQ(d.time_name, "");
    log_data_free(&d);
}

TEST_CASE(an_unreadable_cell_holds_the_trace_instead_of_breaking_it)
{
    /* One bad cell in five keeps the column above the 80% threshold that
     * decides whether it is numeric.  Below that threshold the column is
     * reported as non-numeric rather than the cell as bad. */
    static const char text[] = "t,v\n0,1.5\n1,n/a\n2,3.5\n3,4.0\n4,4.5\n";
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, sizeof(text) - 1u);

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);

    log_data_t d;
    if (log_csv_build(&src, &a, NULL, 0, &d) != LOG_OK) {
        T_FAIL("build failed");
        return;
    }
    CHECK_EQ(d.count, 5);
    CHECK_NEAR(d.value[0][1], 1.5, 1e-6); /* held, not zeroed */
    CHECK_EQ(d.unparsed_cells, 1);
    log_data_free(&d);
}

TEST_CASE(only_the_selected_columns_are_materialised)
{
    /* The reason for the two-phase split: on the board, memory is spent on the
     * traces the user asked for, not on every column in the file. */
    static const char text[] = "t,a,b,c\n0,1,2,3\n1,4,5,6\n";
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, sizeof(text) - 1u);

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    CHECK_EQ(a.time_index, 0);

    const int pick[] = { 3 };
    log_data_t d;
    CHECK_EQ(log_csv_build(&src, &a, pick, 1, &d), LOG_OK);
    CHECK_EQ(d.n_fields, 1);
    CHECK_STR_EQ(d.field[0].name, "c");
    CHECK_NEAR(d.value[0][1], 6.0, 1e-6);
    CHECK(d.value[1] == NULL);
    log_data_free(&d);
}

TEST_CASE(unit_tallies_and_headers_at_their_limits)
{
    log_unit_tally_t t;
    char unit[LOG_UNIT_MAX];
    bool mixed = false;

    /* More distinct suffixes than slots: the winner stops mattering, but that
     * the column is mixed is still reportable. */
    log_unit_tally_reset(&t);
    static const char *const many[] = { "A", "V", "W", "Hz", "rpm", "mAh" };
    for (size_t i = 0; i < sizeof(many) / sizeof(many[0]); ++i) {
        log_unit_tally_add(&t, many[i]);
    }
    log_unit_tally_add(&t, "V");
    log_unit_tally_add(&t, "V");
    log_unit_dominant(&t, unit, sizeof(unit), &mixed);
    CHECK_STR_EQ(unit, "V");
    CHECK(mixed);

    log_unit_tally_reset(&t);
    log_unit_tally_add(&t, NULL);
    log_unit_tally_add(&t, "");
    log_unit_dominant(&t, unit, sizeof(unit), &mixed);
    CHECK_STR_EQ(unit, "");
    CHECK(!mixed);
    log_unit_dominant(NULL, unit, sizeof(unit), &mixed);
    CHECK_STR_EQ(unit, "");

    char name[LOG_NAME_MAX];
    log_unit_from_header(NULL, name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "");

    /* Brackets with nothing usable in them stay part of the name. */
    log_unit_from_header("(V)", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "(V)");
    log_unit_from_header("power output", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "power output");
    CHECK_STR_EQ(unit, "");
    log_unit_from_header("speed  km/h", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "speed");
    CHECK_STR_EQ(unit, "km/h");
    log_unit_from_header("temp_[\xC2\xB0" "C]", name, sizeof(name), unit,
                         sizeof(unit));
    CHECK_STR_EQ(name, "temp");
    CHECK_STR_EQ(unit, "\xC2\xB0" "C");

    CHECK_NEAR(log_time_unit_scale(NULL), 0.0, 1e-15);
    CHECK_NEAR(log_time_unit_scale(""), 0.0, 1e-15);
    CHECK_NEAR(log_time_unit_scale("Stunden"), 3600.0, 1e-9);
}

/* ------------------------------------------------------- csv edge cases -- */

static log_err_t analyse_text(const char *text, const log_csv_opts_t *opts,
                              log_analysis_t *a)
{
    static log_source_t src;
    static log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));
    return log_csv_analyse(&src, opts, a);
}

TEST_CASE(the_error_paths_name_themselves)
{
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(NULL, NULL, &a), LOG_ERR_ARG);
    CHECK_EQ(log_csv_analyse((log_source_t *)&a, NULL, NULL), LOG_ERR_ARG);
    CHECK_EQ(log_csv_build(NULL, NULL, NULL, 0, NULL), LOG_ERR_ARG);

    CHECK_EQ(analyse_text("", NULL, &a), LOG_ERR_NO_HEADER);
    CHECK_EQ(analyse_text("t,v\n", NULL, &a), LOG_ERR_NO_ROWS);

    /* 26 columns is more than the screen can plot, so the excess is reported
     * rather than dropped. */
    static char wide[1024];
    size_t n = 0;
    for (int i = 0; i < LOG_MAX_COLUMNS + 4; ++i) {
        n += (size_t)snprintf(wide + n, sizeof(wide) - n, "%sc%d",
                              (i > 0) ? "," : "", i);
    }
    (void)snprintf(wide + n, sizeof(wide) - n, "\n1\n");
    CHECK_EQ(analyse_text(wide, NULL, &a), LOG_ERR_TOO_MANY_COLUMNS);

    for (int e = LOG_OK; e <= LOG_ERR_ARG; ++e) {
        CHECK(log_err_str((log_err_t)e)[0] != '\0');
    }
    CHECK(log_err_str((log_err_t)99)[0] != '\0');
    CHECK_STR_EQ(log_csv_delimiter_label('\t'), "TAB");
    CHECK_STR_EQ(log_csv_delimiter_label('|'), "PIPE");
    CHECK_STR_EQ(log_csv_delimiter_label('!'), "?");
    CHECK_EQ(log_csv_sniff("", 0), ',');
    CHECK_EQ(log_csv_sniff("no delimiters here\n", 19), ',');
}

TEST_CASE(a_byte_order_mark_and_blank_lines_are_skipped)
{
    static const char text[] =
        "\xEF\xBB\xBF" "\n\nt,v\n\n0,1\n\n1,2\n\n";
    log_analysis_t a;
    CHECK_EQ(analyse_text(text, NULL, &a), LOG_OK);
    CHECK_EQ(a.n_columns, 2);
    CHECK_EQ(a.row_count, 2);
    CHECK_STR_EQ(a.columns[0].name, "t");
}

TEST_CASE(an_empty_header_cell_still_names_its_column)
{
    static const char text[] = ",v\n1,2\n3,4\n";
    log_analysis_t a;
    CHECK_EQ(analyse_text(text, NULL, &a), LOG_OK);
    CHECK_STR_EQ(a.columns[0].name, "column 1");
}

TEST_CASE(the_time_column_and_its_unit_can_be_forced)
{
    static const char text[] = "a,b,c\n0,5,100\n1,4,200\n2,3,300\n";
    log_csv_opts_t opts;
    log_analysis_t a;

    log_csv_opts_default(&opts);
    opts.time_index = 2;
    opts.time_unit = "ms";
    CHECK_EQ(analyse_text(text, &opts, &a), LOG_OK);
    CHECK_EQ(a.time_index, 2);
    CHECK_STR_EQ(a.time_unit, "ms");

    /* Forcing it off puts the row number back on the x axis. */
    log_csv_opts_default(&opts);
    opts.time_index = -1;
    CHECK_EQ(analyse_text(text, &opts, &a), LOG_OK);
    CHECK_EQ(a.time_index, -1);
    CHECK_STR_EQ(a.time_unit, "s");

    /* Out of range is treated as none rather than as an index into nothing. */
    log_csv_opts_default(&opts);
    opts.time_index = 99;
    CHECK_EQ(analyse_text(text, &opts, &a), LOG_OK);
    CHECK_EQ(a.time_index, -1);
}

TEST_CASE(a_time_unit_is_guessed_from_the_name_then_the_magnitude)
{
    log_analysis_t a;

    CHECK_EQ(analyse_text("millisSinceStart,v\n0,1\n10,2\n20,3\n", NULL, &a),
             LOG_OK);
    CHECK_STR_EQ(a.time_unit, "ms");

    CHECK_EQ(analyse_text("microsSinceStart,v\n0,1\n10,2\n20,3\n", NULL, &a),
             LOG_OK);
    CHECK_STR_EQ(a.time_unit, "us");

    /* Nothing in the name, so the span decides: a bench run is minutes long,
     * which makes a span in the millions microseconds. */
    CHECK_EQ(analyse_text("time,v\n0,1\n4000000,2\n8000000,3\n", NULL, &a),
             LOG_OK);
    CHECK_STR_EQ(a.time_unit, "us");
    CHECK_EQ(analyse_text("time,v\n0,1\n4000,2\n8000,3\n", NULL, &a), LOG_OK);
    CHECK_STR_EQ(a.time_unit, "ms");
}

TEST_CASE(a_time_axis_that_runs_backwards_falls_back_to_the_row_number)
{
    /* The column was forced, and it descends.  Plotting it would draw the run
     * in reverse, so the x axis becomes the row number instead. */
    static const char text[] = "t,v\n9,1\n5,2\n7,3\n2,4\n";
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    opts.time_index = 0;

    static log_source_t src;
    static log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
    log_data_t d;
    CHECK_EQ(log_csv_build(&src, &a, NULL, 0, &d), LOG_OK);
    CHECK_STR_EQ(d.time_name, "");
    CHECK_NEAR(d.time[3], 3.0, 1e-9);
    log_data_free(&d);
}

TEST_CASE(a_missing_time_cell_holds_the_clock_rather_than_jumping_to_zero)
{
    static const char text[] = "t,v\n0,1\n,2\n2,3\n3,4\n4,5\n";
    static log_source_t src;
    static log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    log_data_t d;
    CHECK_EQ(log_csv_build(&src, &a, NULL, 0, &d), LOG_OK);
    CHECK_NEAR(d.time[1], 0.0, 1e-9); /* held at the previous value */
    CHECK_NEAR(d.time[2], 2.0, 1e-9);
    log_data_free(&d);
}

TEST_CASE(blackbox_column_names_keep_their_grouping_through_a_csv)
{
    static const char text[] =
        "time,motor[0],vbatLatest\n0,100,1550\n1,200,1540\n2,300,1530\n";
    static log_source_t src;
    static log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    log_data_t d;
    CHECK_EQ(log_csv_build(&src, &a, NULL, 0, &d), LOG_OK);
    for (int k = 0; k < d.n_fields; ++k) {
        if (strcmp(d.field[k].name, "motor[0]") == 0) {
            CHECK_STR_EQ(d.field[k].group, "Motor");
        }
        if (strcmp(d.field[k].name, "vbatLatest") == 0) {
            CHECK_STR_EQ(d.field[k].group, "Power");
        }
    }
    log_data_free(&d);
}

TEST_CASE(a_column_with_a_unit_but_no_blackbox_name_groups_by_unit)
{
    static const char text[] =
        "t,shunt (mA)\n0,10\n1,20\n2,30\n";
    static log_source_t src;
    static log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    log_data_t d;
    CHECK_EQ(log_csv_build(&src, &a, NULL, 0, &d), LOG_OK);
    CHECK_STR_EQ(d.field[0].group, "Unit mA");
    log_data_free(&d);

    static const char plain[] = "t,x\n0,10\n1,20\n2,30\n";
    log_source_memory(&src, &ctx, plain, strlen(plain));
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    CHECK_EQ(log_csv_build(&src, &a, NULL, 0, &d), LOG_OK);
    CHECK_STR_EQ(d.field[0].group, "Data");
    log_data_free(&d);
}

static int s_alloc_budget;

static void *stingy_alloc(size_t n)
{
    if (s_alloc_budget-- <= 0) {
        return NULL;
    }
    return malloc(n);
}

TEST_CASE(running_out_of_memory_is_reported_not_crashed_into)
{
    /* On the board the sample arrays come out of PSRAM (pseudo-static
     * random-access memory), and a long enough log exhausts it.  That comes
     * back as an error the screen can show. */
    static const char text[] = "t,a,b\n0,1,2\n1,3,4\n2,5,6\n";
    static log_source_t src;
    static log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);

    log_set_allocator(stingy_alloc, free);
    s_alloc_budget = 1; /* enough for the time axis, not for the traces */
    log_data_t d;
    CHECK_EQ(log_csv_build(&src, &a, NULL, 0, &d), LOG_ERR_MEMORY);
    log_data_free(&d);

    s_alloc_budget = 0;
    CHECK_EQ(log_csv_build(&src, &a, NULL, 0, &d), LOG_ERR_MEMORY);
    log_data_free(&d);

    log_set_allocator(NULL, NULL); /* back to malloc/free */
    CHECK_EQ(log_csv_build(&src, &a, NULL, 0, &d), LOG_OK);
    log_data_free(&d);
}

TEST_CASE(a_real_file_on_disk_reads_the_same_as_one_in_memory)
{
    /* This is the path the firmware takes: fopen, read, rewind, read again. */
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, "de.csv");
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        T_FAIL("cannot open %s", path);
        return;
    }

    log_source_t src;
    log_source_stdio(&src, f);

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    CHECK_EQ(a.convention, LOG_CONV_DE);
    CHECK_EQ(a.delimiter, ';');
    CHECK_EQ(a.row_count, 4);

    log_data_t d;
    CHECK_EQ(log_csv_build(&src, &a, NULL, 0, &d), LOG_OK);
    static const double volts[] = { 22.34, 22.31, 22.28, 22.20 };
    check_series(&d, "Spannung", volts, 4);
    log_data_free(&d);
    fclose(f);
}

/* ------------------------------------------------- convention evidence --- */

/*
 * Three trailing digits are ambiguous only when the integer part could be a
 * leading thousands group.  "0.000" and "12345,678" cannot be grouped, so
 * they are decimal points and count as evidence.  Millisecond timestamps are
 * the common case; treating them as "no evidence" lets an English log be
 * read as German at 1000x.
 */
TEST_CASE(an_integer_part_that_cannot_be_a_group_settles_the_convention)
{
    CHECK(log_evidence_of("0.000") == LOG_EV_EN);
    CHECK(log_evidence_of("0.020") == LOG_EV_EN);
    CHECK(log_evidence_of("0,500") == LOG_EV_DE);
    CHECK(log_evidence_of("12345.678") == LOG_EV_EN);
    CHECK(log_evidence_of("12345,678") == LOG_EV_DE);

    /* Still genuinely ambiguous: "1,024" is a legal group and a legal value. */
    CHECK(log_evidence_of("1.234") == LOG_EV_AMBIGUOUS);
    CHECK(log_evidence_of("1,024") == LOG_EV_AMBIGUOUS);

    const char *text = "time,rpm\n0.000,1000\n0.020,1050\n0.040,1100\n0.060,1150\n";
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));

    log_analysis_t a;
    CHECK(log_csv_analyse(&src, NULL, &a) == LOG_OK);
    CHECK(a.convention == LOG_CONV_EN);
    CHECK_EQ(a.time_index, 0);

    const int pick[1] = { 1 };
    log_data_t d;
    CHECK(log_csv_build(&src, &a, pick, 1, &d) == LOG_OK);
    CHECK_NEAR(d.duration_s, 0.06, 1e-6);
    CHECK_NEAR(d.rate_hz, 50.0, 0.5);
    log_data_free(&d);
}

/*
 * The residual after the digits is a unit only if it can be one.  A clock time
 * or an ISO 8601 date otherwise reads as its leading number with the rest as
 * a "unit", which makes the column report itself numeric, constant, and fit
 * to be the time axis.
 */
TEST_CASE(a_timestamp_is_not_a_number_with_a_unit)
{
    log_value_t v;
    CHECK(!log_split_value("2024-01-15T10:00:00", &v));
    CHECK(!log_split_value("12:00:01", &v));
    CHECK(!log_split_value("1e999999", &v));

    /* A real unit still comes through, and a trailing separator belongs to
     * the number rather than becoming a unit of its own. */
    check_split("22.34V", "22.34", 1, 0, "V");
    check_split("1500,", "1500", 1, 0, "");

    const char *text = "time,rpm\n2024-01-15T10:00:00,1000\n"
                       "2024-01-15T10:00:01,1100\n2024-01-15T10:00:02,1200\n";
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));

    log_analysis_t a;
    CHECK(log_csv_analyse(&src, NULL, &a) == LOG_OK);
    CHECK(!a.columns[0].numeric);
    CHECK_EQ(a.time_index, -1);
}

/* An exponent is applied before the result is checked, so an overflow is a
 * rejected cell rather than an infinity in the column's min/max. */
TEST_CASE(an_exponent_that_overflows_is_rejected)
{
    double v = 0.0;
    CHECK(!log_parse_with("1e400", LOG_CONV_EN, &v, NULL, 0));
    CHECK(log_parse_with("1e-400", LOG_CONV_EN, &v, NULL, 0));
    CHECK_NEAR(v, 0.0, 1e-9);
    CHECK(log_parse_with("1.5e3", LOG_CONV_EN, &v, NULL, 0));
    CHECK_NEAR(v, 1500.0, 1e-9);
}

int main(void)
{
    RUN(english_file_reads_as_english);
    RUN(german_file_reads_as_german);
    RUN(header_units_and_a_millisecond_time_axis);
    RUN(forcing_the_wrong_convention_rejects_rather_than_corrupts);
    RUN(ragged_rows_are_reported_not_absorbed);
    RUN(the_row_cap_is_reported_rather_than_silent);
    RUN(a_file_with_no_numbers_is_an_error_not_an_empty_plot);
    RUN(without_a_time_column_the_x_axis_is_the_row_index);
    RUN(an_unreadable_cell_holds_the_trace_instead_of_breaking_it);
    RUN(only_the_selected_columns_are_materialised);
    RUN(unit_tallies_and_headers_at_their_limits);
    RUN(the_error_paths_name_themselves);
    RUN(a_byte_order_mark_and_blank_lines_are_skipped);
    RUN(an_empty_header_cell_still_names_its_column);
    RUN(the_time_column_and_its_unit_can_be_forced);
    RUN(a_time_unit_is_guessed_from_the_name_then_the_magnitude);
    RUN(a_time_axis_that_runs_backwards_falls_back_to_the_row_number);
    RUN(a_missing_time_cell_holds_the_clock_rather_than_jumping_to_zero);
    RUN(blackbox_column_names_keep_their_grouping_through_a_csv);
    RUN(a_column_with_a_unit_but_no_blackbox_name_groups_by_unit);
    RUN(running_out_of_memory_is_reported_not_crashed_into);
    RUN(a_real_file_on_disk_reads_the_same_as_one_in_memory);
    RUN(an_integer_part_that_cannot_be_a_group_settles_the_convention);
    RUN(a_timestamp_is_not_a_number_with_a_unit);
    RUN(an_exponent_that_overflows_is_rejected);
    return test_summary("log_csv");
}
