/*
 * Host unit tests for the log file parsing core.
 *
 * The expectations are logwiju's own: this suite is a port of that project's
 * test/verify-numbers.mjs and test/verify-csv.mjs, run against the same four
 * fixtures, which are checked in under fixtures/.  Porting a parser without
 * porting its tests would just be rewriting it.
 *
 * On top of those there are cases for the things this version does and the
 * browser one does not: streaming a file it cannot hold, a row cap, and quoted
 * fields that straddle a read boundary.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <stdlib.h>
#include <string.h>

#include "log_csv.h"
#include "log_fields.h"
#include "log_numbers.h"

#ifndef FIXTURE_DIR
#define FIXTURE_DIR "fixtures"
#endif

/* ------------------------------------------------------------- numbers --- */

static void check_split(const char *raw, const char *digits, int sign, int exp,
                        const char *unit)
{
    log_value_t v;
    if (!log_split_value(raw, &v)) {
        T_FAIL("split(\"%s\") failed", raw);
        return;
    }
    if (strcmp(v.digits, digits) != 0 || v.sign != sign || v.exp != exp ||
        strcmp(v.unit, unit) != 0) {
        T_FAIL("split(\"%s\") -> {%s, %d, %d, \"%s\"}, want {%s, %d, %d, \"%s\"}",
               raw, v.digits, v.sign, v.exp, v.unit, digits, sign, exp, unit);
    }
}

TEST_CASE(split_value_separates_number_and_unit)
{
    check_split("10.23A", "10.23", 1, 0, "A");
    check_split("22,34V", "22,34", 1, 0, "V");
    check_split("-3,5 \xC2\xB0" "C", "3,5", -1, 0, "\xC2\xB0" "C");
    check_split("12 %", "12", 1, 0, "%");
    check_split("1.5e3 m/s", "1.5", 1, 3, "m/s");

    log_value_t v;
    CHECK(!log_split_value("   ", &v));
    CHECK(!log_split_value("n/a", &v));
    CHECK(!log_split_value("", &v));
}

TEST_CASE(evidence_reports_what_a_value_proves)
{
    CHECK_EQ(log_evidence_of("1.234,56"), LOG_EV_DE);
    CHECK_EQ(log_evidence_of("1,234.56"), LOG_EV_EN);
    CHECK_EQ(log_evidence_of("10,23"), LOG_EV_DE);
    CHECK_EQ(log_evidence_of("1.5"), LOG_EV_EN);
    CHECK_EQ(log_evidence_of("1.234.567"), LOG_EV_DE);
    CHECK_EQ(log_evidence_of("1,234,567"), LOG_EV_EN);
    CHECK_EQ(log_evidence_of("1,234"), LOG_EV_AMBIGUOUS);
    CHECK_EQ(log_evidence_of("1.234"), LOG_EV_AMBIGUOUS);
    CHECK_EQ(log_evidence_of("42"), LOG_EV_NONE);
}

static log_conv_t infer(const char *const *vals, int n, log_ambig_t fallback,
                        bool *confident, bool *conflict)
{
    log_votes_t v;
    log_votes_reset(&v);
    for (int i = 0; i < n; ++i) {
        log_votes_add(&v, vals[i]);
    }
    return log_votes_result(&v, fallback, confident, conflict);
}

static double parsed(const char *raw, log_conv_t conv)
{
    double out = 0.0;
    if (!log_parse_with(raw, conv, &out, NULL, 0)) {
        T_FAIL("parse(\"%s\") rejected", raw);
        return 0.0;
    }
    return out;
}

TEST_CASE(one_value_settles_the_whole_column)
{
    /* "10,23" proves comma-decimal, so "1,234" must read 1.234 not 1234. */
    static const char *const de_col[] = { "1,234", "10,23", "5,678" };
    bool confident = false;
    log_conv_t c = infer(de_col, 3, LOG_AMBIG_THOUSANDS, &confident, NULL);
    CHECK_EQ(c, LOG_CONV_DE);
    CHECK(confident);
    CHECK_NEAR(parsed("1,234", c), 1.234, 1e-9);
    CHECK_NEAR(parsed("10,23", c), 10.23, 1e-9);
    CHECK_NEAR(parsed("5,678", c), 5.678, 1e-9);

    /* Same digits, but English evidence present. */
    static const char *const en_col[] = { "1,234", "10.23", "5,678" };
    c = infer(en_col, 3, LOG_AMBIG_THOUSANDS, NULL, NULL);
    CHECK_EQ(c, LOG_CONV_EN);
    CHECK_NEAR(parsed("1,234", c), 1234.0, 1e-9);
    CHECK_NEAR(parsed("5,678", c), 5678.0, 1e-9);
}

TEST_CASE(a_wholly_ambiguous_column_uses_the_fallback)
{
    static const char *const col[] = { "1,234", "5,678" };
    bool confident = true;
    CHECK_EQ(infer(col, 2, LOG_AMBIG_THOUSANDS, &confident, NULL), LOG_CONV_EN);
    CHECK(!confident);
    CHECK_EQ(infer(col, 2, LOG_AMBIG_DECIMAL, NULL, NULL), LOG_CONV_DE);
    CHECK_NEAR(parsed("1,234", LOG_CONV_EN), 1234.0, 1e-9);
    CHECK_NEAR(parsed("1,234", LOG_CONV_DE), 1.234, 1e-9);
}

TEST_CASE(conflicting_evidence_is_reported)
{
    static const char *const col[] = { "10,23", "10.23" };
    bool conflict = false;
    (void)infer(col, 2, LOG_AMBIG_THOUSANDS, NULL, &conflict);
    CHECK(conflict);
}

TEST_CASE(parse_with_a_known_convention)
{
    CHECK_NEAR(parsed("1.234,56", LOG_CONV_DE), 1234.56, 1e-9);
    CHECK_NEAR(parsed("1,234.56", LOG_CONV_EN), 1234.56, 1e-9);
    CHECK_NEAR(parsed("-1.234,5", LOG_CONV_DE), -1234.5, 1e-9);
    CHECK_NEAR(parsed("1 234,56", LOG_CONV_DE), 1234.56, 1e-9);
    CHECK_NEAR(parsed("12,345,678", LOG_CONV_EN), 12345678.0, 1e-9);
    CHECK_NEAR(parsed("12.345.678", LOG_CONV_DE), 12345678.0, 1e-9);

    char unit[8];
    double v = 0.0;
    CHECK(log_parse_with("22,34V", LOG_CONV_DE, &v, unit, sizeof(unit)));
    CHECK_NEAR(v, 22.34, 1e-9);
    CHECK_STR_EQ(unit, "V");

    CHECK(log_parse_loose("10,23", LOG_AMBIG_THOUSANDS, &v));
    CHECK_NEAR(v, 10.23, 1e-9);
}

TEST_CASE(the_wrong_convention_is_rejected_not_guessed)
{
    /* The whole point: stripping "." out of the German 1.234,56 and reading it
     * as English gives 1.23456 -- a silently wrong number.  Reject instead. */
    double v = 0.0;
    CHECK(!log_parse_with("1.234,56", LOG_CONV_EN, &v, NULL, 0));
    CHECK(!log_parse_with("1,234.56", LOG_CONV_DE, &v, NULL, 0));
    CHECK(!log_parse_with("1,23,456", LOG_CONV_EN, &v, NULL, 0));
    CHECK(!log_parse_with("1.2.3", LOG_CONV_EN, &v, NULL, 0));
    CHECK(!log_parse_with("10,23", LOG_CONV_EN, &v, NULL, 0));
    CHECK(!log_parse_with("10.23", LOG_CONV_DE, &v, NULL, 0));

    /* A plain integer reads the same either way. */
    CHECK_NEAR(parsed("42", LOG_CONV_DE), 42.0, 1e-9);
    CHECK_NEAR(parsed("42", LOG_CONV_EN), 42.0, 1e-9);
}

TEST_CASE(units_from_values_and_headers)
{
    log_unit_tally_t t;
    char unit[LOG_UNIT_MAX];
    bool mixed = true;

    log_unit_tally_reset(&t);
    log_unit_tally_add(&t, "A");
    log_unit_tally_add(&t, "A");
    log_unit_tally_add(&t, "");
    log_unit_dominant(&t, unit, sizeof(unit), &mixed);
    CHECK_STR_EQ(unit, "A");
    CHECK(!mixed);

    log_unit_tally_reset(&t);
    log_unit_tally_add(&t, "A");
    log_unit_tally_add(&t, "V");
    log_unit_tally_add(&t, "A");
    log_unit_dominant(&t, unit, sizeof(unit), &mixed);
    CHECK_STR_EQ(unit, "A");
    CHECK(mixed);

    char name[LOG_NAME_MAX];
    log_unit_from_header("voltage (V)", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "voltage");
    CHECK_STR_EQ(unit, "V");

    log_unit_from_header("current [A]", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "current");
    CHECK_STR_EQ(unit, "A");

    log_unit_from_header("altitude m", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "altitude");
    CHECK_STR_EQ(unit, "m");

    /* An axis index is part of the name, not a unit. */
    log_unit_from_header("gyroADC[0]", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "gyroADC[0]");
    CHECK_STR_EQ(unit, "");

    log_unit_from_header("motor", name, sizeof(name), unit, sizeof(unit));
    CHECK_STR_EQ(name, "motor");
    CHECK_STR_EQ(unit, "");

    CHECK_NEAR(log_time_unit_scale("ms"), 1e-3, 1e-15);
    CHECK_NEAR(log_time_unit_scale("us"), 1e-6, 1e-18);
    CHECK_NEAR(log_time_unit_scale("min"), 60.0, 1e-9);
    CHECK_NEAR(log_time_unit_scale("nonsense"), 0.0, 1e-15);
}

TEST_CASE(blackbox_field_names_keep_their_grouping)
{
    CHECK_STR_EQ(log_field_meta("motor[0]").group, "Motor");
    CHECK_STR_EQ(log_field_meta("eRPM[3]").group, "RPM");
    CHECK_STR_EQ(log_field_meta("vbatLatest").group, "Power");
    CHECK_STR_EQ(log_field_meta("amperageLatest").unit, "A");
    CHECK_STR_EQ(log_field_meta("escTemperature[1]").group, "ESC");
    CHECK_STR_EQ(log_field_meta("stateFlags").group, "Flags");
    CHECK_STR_EQ(log_field_meta("GPS_numSat").group, "GPS");
    CHECK_STR_EQ(log_field_meta("axisP[0]").group, "PID");
    CHECK_STR_EQ(log_field_meta("something").group, "Other");
    /* An exact rule must not swallow a longer name. */
    CHECK_STR_EQ(log_field_meta("timeSinceArm").group, "Other");
}

/* ----------------------------------------------------------------- rows --- */

static void split_check(const char *text, char delim, int row_index,
                        const char *const *want, int want_n)
{
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));

    log_reader_t r;
    log_reader_init(&r, &src);

    char store[LOG_ROW_MAX];
    char *fields[LOG_MAX_COLUMNS];
    int total = 0;
    for (int i = 0; i <= row_index; ++i) {
        int n = log_reader_row(&r, delim, store, sizeof(store), fields,
                               LOG_MAX_COLUMNS, &total);
        if (n < 0) {
            T_FAIL("row %d missing in \"%s\"", row_index, text);
            return;
        }
        if (i != row_index) {
            continue;
        }
        if (n != want_n) {
            T_FAIL("row %d has %d fields, want %d", row_index, n, want_n);
            return;
        }
        for (int k = 0; k < n; ++k) {
            if (strcmp(fields[k], want[k]) != 0) {
                T_FAIL("row %d field %d: got \"%s\", want \"%s\"", row_index, k,
                       fields[k], want[k]);
            }
        }
    }
}

TEST_CASE(rows_honour_quotes_and_doubled_quotes)
{
    static const char *const r0[] = { "a", "b,c", "d" };
    static const char *const r1[] = { "1", "2", "3" };
    split_check("a,\"b,c\",d\n1,2,3", ',', 0, r0, 3);
    split_check("a,\"b,c\",d\n1,2,3", ',', 1, r1, 3);

    static const char *const esc[] = { "a", "say \"hi\"", "c" };
    split_check("a,\"say \"\"hi\"\"\",c", ',', 0, esc, 3);

    /* CRLF, and a last line with no newline at all. */
    static const char *const crlf[] = { "x", "y" };
    split_check("x,y\r\nz,w", ',', 0, crlf, 2);
    static const char *const tail[] = { "z", "w" };
    split_check("x,y\r\nz,w", ',', 1, tail, 2);
}

TEST_CASE(a_quoted_field_may_straddle_a_read_boundary)
{
    /* The reader refills in 256-byte chunks.  A field that crosses one, and a
     * doubled quote whose halves land in different chunks, are exactly the
     * cases a chunked reader gets wrong. */
    char text[900];
    size_t n = 0;
    n += (size_t)snprintf(text + n, sizeof(text) - n, "a,b\n\"");
    for (int i = 0; i < 250; ++i) {
        text[n++] = 'x';
    }
    n += (size_t)snprintf(text + n, sizeof(text) - n, "\"\"end\",2\n");
    text[n] = '\0';

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, n);
    log_reader_t r;
    log_reader_init(&r, &src);

    char store[LOG_ROW_MAX];
    char *fields[LOG_MAX_COLUMNS];
    int total = 0;
    (void)log_reader_row(&r, ',', store, sizeof(store), fields, LOG_MAX_COLUMNS,
                         &total);
    int got = log_reader_row(&r, ',', store, sizeof(store), fields,
                             LOG_MAX_COLUMNS, &total);
    CHECK_EQ(got, 2);
    if (got == 2) {
        CHECK_EQ((long)strlen(fields[0]), 254);
        CHECK_STR_EQ(fields[0] + 250, "\"end");
        CHECK_STR_EQ(fields[1], "2");
    }
}

/* ------------------------------------------------------------------ csv --- */

static char g_text[8192];
static size_t g_len;
static log_source_t g_src;
static log_mem_ctx_t g_ctx;

static bool load_fixture(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        T_FAIL("cannot open %s", path);
        return false;
    }
    g_len = fread(g_text, 1, sizeof(g_text) - 1, f);
    fclose(f);
    g_text[g_len] = '\0';
    log_source_memory(&g_src, &g_ctx, g_text, g_len);
    return true;
}

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

TEST_CASE(delimiter_is_sniffed_from_a_sample)
{
    if (!load_fixture("en.csv")) {
        return;
    }
    CHECK_EQ(log_csv_sniff(g_text, g_len), ',');
    if (!load_fixture("de.csv")) {
        return;
    }
    CHECK_EQ(log_csv_sniff(g_text, g_len), ';');
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
    /* The board cannot hold an arbitrarily long log, so it stops -- and says
     * so.  A viewer that quietly showed the first N rows would be lying about
     * the run. */
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
    /* One bad cell in five keeps the column above the 80 % bar that decides
     * whether it is numeric at all -- below that the file, not the cell, is
     * the problem, and the analysis says so instead. */
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

TEST_CASE(exotic_grouping_characters_are_not_decimal_points)
{
    /* Swiss exporters group with an apostrophe, German ones with a no-break
     * or narrow no-break space.  None of those is ever a decimal point, so
     * they come out of the digits before anything else looks at them. */
    check_split("1\xC2\xA0" "234,56", "1234,56", 1, 0, "");
    check_split("1\xE2\x80\xAF" "234,56", "1234,56", 1, 0, "");
    check_split("1'234'567", "1234567", 1, 0, "");
    check_split("12\xC2\xA0%", "12", 1, 0, "%");

    CHECK_NEAR(parsed("1\xC2\xA0" "234,56", LOG_CONV_DE), 1234.56, 1e-9);
    CHECK_NEAR(parsed("1'234'567", LOG_CONV_EN), 1234567.0, 1e-9);

    /* A trailing no-break space is whitespace, not a unit. */
    check_split("22,34\xC2\xA0", "22,34", 1, 0, "");
}

TEST_CASE(exponents_and_degenerate_input)
{
    check_split("1.5e-3", "1.5", 1, -3, "");
    check_split("+42", "42", 1, 0, "");
    CHECK_NEAR(parsed("1.5e-3", LOG_CONV_EN), 0.0015, 1e-12);

    /* An 'e' with no exponent behind it is part of the unit, not a number. */
    check_split("5e", "5", 1, 0, "e");

    log_value_t v;
    CHECK(!log_split_value(".5", &v));  /* must start on a digit */
    CHECK(!log_split_value(NULL, &v));
    CHECK(!log_split_value("1", NULL));
    /* Longer than any real measurement, and longer than the digit buffer. */
    CHECK(!log_split_value("111111111111111111111111111111111111111111111", &v));

    CHECK_EQ(log_evidence_of(NULL), LOG_EV_NONE);
    CHECK(!log_is_well_formed(NULL, LOG_CONV_EN));
    CHECK(!log_is_well_formed("1.2.3", LOG_CONV_EN));
    CHECK(!log_is_well_formed("1,2345", LOG_CONV_EN));
    CHECK(!log_is_well_formed("1,23a", LOG_CONV_DE));
}

TEST_CASE(a_dot_grouped_ambiguous_file_follows_the_fallback)
{
    /* The mirror of the comma case: when only "1.234" is ever seen, reading
     * the dot as thousands means German. */
    static const char *const col[] = { "1.234", "5.678" };
    bool confident = true;
    CHECK_EQ(infer(col, 2, LOG_AMBIG_THOUSANDS, &confident, NULL), LOG_CONV_DE);
    CHECK(!confident);
    CHECK_EQ(infer(col, 2, LOG_AMBIG_DECIMAL, NULL, NULL), LOG_CONV_EN);
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

    /* One column per byte of the alphabet is more than this screen can plot,
     * and saying so beats quietly dropping the rest. */
    static char wide[1024];
    size_t n = 0;
    for (int i = 0; i < LOG_MAX_COLUMNS + 4; ++i) {
        n += (size_t)snprintf(wide + n, sizeof(wide) - n, "%sc%d",
                              (i > 0) ? "," : "", i);
    }
    n += (size_t)snprintf(wide + n, sizeof(wide) - n, "\n1\n");
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
    /* On the board the sample arrays come out of PSRAM, and a long enough log
     * will exhaust it.  That has to come back as an error the screen can show. */
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

TEST_CASE(an_over_long_row_is_truncated_without_running_off_the_end)
{
    /* A file whose rows are longer than the row buffer is malformed for this
     * viewer, but it must not be a buffer overrun. */
    static char text[LOG_ROW_MAX * 3];
    size_t n = (size_t)snprintf(text, sizeof(text), "a,b\n");
    for (int i = 0; i < LOG_ROW_MAX + 200 && n + 4u < sizeof(text); ++i) {
        text[n++] = (char)('0' + (i % 10));
    }
    n += (size_t)snprintf(text + n, sizeof(text) - n, ",7\n1,2\n");
    text[n] = '\0';

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, n);

    log_analysis_t a;
    log_err_t err = log_csv_analyse(&src, NULL, &a);
    CHECK(err == LOG_OK);
    CHECK_EQ(a.n_columns, 2);
}

/* ------------------------------------------------- convention evidence --- */

/*
 * Three trailing digits are the ambiguous shape only when the integer part
 * could be a leading thousands group.  "0.000" and "12345,678" cannot be
 * grouped, so they are decimal points and nothing else -- millisecond
 * timestamps are the common case, and calling them "no evidence" is what let
 * an ordinary English log be read as German at 1000x.
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

    int pick[1] = { 1 };
    log_data_t d;
    CHECK(log_csv_build(&src, &a, pick, 1, &d) == LOG_OK);
    CHECK_NEAR(d.duration_s, 0.06, 1e-6);
    CHECK_NEAR(d.rate_hz, 50.0, 0.5);
    log_data_free(&d);
}

/*
 * A repeated separator is only grouping when the groups are groups.  A
 * version, an IP address and a dotted date all have the shape and none of
 * them is a number -- and one such column used to hand the whole file to the
 * wrong convention.
 */
TEST_CASE(a_version_or_an_address_casts_no_vote)
{
    CHECK(log_evidence_of("1.234.567") == LOG_EV_DE);
    CHECK(log_evidence_of("1.2.3") == LOG_EV_NONE);
    CHECK(log_evidence_of("192.168.0.1") == LOG_EV_NONE);
    CHECK(log_evidence_of("15.01.2024") == LOG_EV_NONE);
    CHECK(log_evidence_of("1.0234.567") == LOG_EV_NONE);
}

/*
 * The residual after the digits is a unit only if it can be one.  A clock time
 * or an ISO date otherwise reads as its leading number with the rest as a
 * "unit", which makes the column report itself numeric, constant, and fit to
 * be the time axis.
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

/* ---------------------------------------------------- a card that fails --- */

/*
 * A source that stops part way, the way a bad sector or a pulled card does.
 * Without an error channel this is indistinguishable from a clean end of file,
 * and a truncated log is then presented as a complete short one -- with a
 * confident row count, duration and sample rate computed from the part that
 * happened to be readable.
 */
typedef struct {
    const char *text;
    size_t len;
    size_t pos;
    size_t fail_after;   /* bytes to hand over before failing */
    bool   failed;
} failing_ctx_t;

static size_t failing_read(void *vctx, char *buf, size_t max)
{
    failing_ctx_t *c = (failing_ctx_t *)vctx;
    if (c->pos >= c->len) {
        return 0;               /* a genuine end of file */
    }
    if (c->pos >= c->fail_after) {
        c->failed = true;       /* data left, but the card stopped answering */
        return 0;
    }
    size_t left = c->len - c->pos;
    size_t room = c->fail_after - c->pos;
    if (max > left) { max = left; }
    if (max > room) { max = room; }
    memcpy(buf, c->text + c->pos, max);
    c->pos += max;
    return max;
}

static bool failing_error(void *vctx)
{
    return ((failing_ctx_t *)vctx)->failed;
}

static bool failing_rewind(void *vctx)
{
    failing_ctx_t *c = (failing_ctx_t *)vctx;
    c->pos = 0;
    c->failed = false;
    return true;
}

TEST_CASE(a_read_error_is_not_a_clean_end_of_file)
{
    static char text[8000];
    size_t n = 0;
    n += (size_t)snprintf(text + n, sizeof(text) - n, "time,rpm\n");
    for (int i = 0; i < 400; ++i) {
        n += (size_t)snprintf(text + n, sizeof(text) - n, "%d.%03d,%d\n",
                              i / 100, (i % 100) * 10, 1000 + i);
    }

    failing_ctx_t ctx = { text, n, 0, n, false };
    log_source_t src = { failing_read, failing_rewind, failing_error, &ctx };

    /* Healthy first: the whole file is there. */
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    CHECK_EQ(a.row_count, 400);

    /* Now the card gives up a quarter of the way in. */
    ctx.fail_after = 1000;
    CHECK(failing_rewind(&ctx));
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_ERR_READ);

    /* And a failure during the build pass is refused too, rather than
     * returning a short array the screen would plot as the whole run. */
    ctx.fail_after = n;
    CHECK(failing_rewind(&ctx));
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    int pick[1] = { 1 };
    log_data_t d;
    ctx.fail_after = 1000;
    CHECK_EQ(log_csv_build(&src, &a, pick, 1, &d), LOG_ERR_READ);
}

/*
 * A row too long for the field store loses every field after the overflow --
 * they are emitted as empty cells, which downstream look exactly like blanks
 * a file legitimately contains.  The count is what lets the screen say so.
 */
TEST_CASE(a_row_too_long_for_the_store_is_counted_not_swallowed)
{
    static char text[LOG_ROW_MAX * 3];
    size_t n = 0;
    n += (size_t)snprintf(text + n, sizeof(text) - n, "time,note,rpm\n");
    n += (size_t)snprintf(text + n, sizeof(text) - n, "0,short,1000\n");
    n += (size_t)snprintf(text + n, sizeof(text) - n, "1,");
    for (size_t i = 0; i < LOG_ROW_MAX + 40u; ++i) {
        text[n++] = 'x';
    }
    n += (size_t)snprintf(text + n, sizeof(text) - n, ",1100\n");
    n += (size_t)snprintf(text + n, sizeof(text) - n, "2,short,1200\n");

    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, n);

    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    CHECK_EQ(a.row_count, 3);
    CHECK_EQ(a.long_rows, 1);
}

int main(void)
{
    RUN(split_value_separates_number_and_unit);
    RUN(evidence_reports_what_a_value_proves);
    RUN(one_value_settles_the_whole_column);
    RUN(a_wholly_ambiguous_column_uses_the_fallback);
    RUN(conflicting_evidence_is_reported);
    RUN(parse_with_a_known_convention);
    RUN(the_wrong_convention_is_rejected_not_guessed);
    RUN(units_from_values_and_headers);
    RUN(blackbox_field_names_keep_their_grouping);
    RUN(rows_honour_quotes_and_doubled_quotes);
    RUN(a_quoted_field_may_straddle_a_read_boundary);
    RUN(delimiter_is_sniffed_from_a_sample);
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
    RUN(exotic_grouping_characters_are_not_decimal_points);
    RUN(exponents_and_degenerate_input);
    RUN(a_dot_grouped_ambiguous_file_follows_the_fallback);
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
    RUN(an_over_long_row_is_truncated_without_running_off_the_end);
    RUN(an_integer_part_that_cannot_be_a_group_settles_the_convention);
    RUN(a_version_or_an_address_casts_no_vote);
    RUN(a_timestamp_is_not_a_number_with_a_unit);
    RUN(an_exponent_that_overflows_is_rejected);
    RUN(a_read_error_is_not_a_clean_end_of_file);
    RUN(a_row_too_long_for_the_store_is_counted_not_swallowed);
    return test_summary("logfile");
}
