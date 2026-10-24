/*
 * A SERVO run's own columns (log_writer_servo_row()): the commanded pulse
 * and the measured angle before `source`, each written only from a figure
 * whose source is the row's, and voltage and current from the INA3221's
 * window while an INA3221 channel is the servo rail's meter.  And what LOGS
 * plots first in such a file (log_fields_picks()).
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "log_cols.h"
#include "log_csv.h"
#include "log_fields.h"
#include "log_writer.h"

/* ------------------------------------------------------------- the sink */

typedef struct {
    char   buf[16 * 1024];
    size_t len;
} mem_sink_t;

static mem_sink_t g_mem;

static int mem_write(void *ctx, const void *data, size_t len)
{
    mem_sink_t *m = (mem_sink_t *)ctx;
    if (m->len + len >= sizeof(m->buf)) {
        return -1;
    }
    memcpy(m->buf + m->len, data, len);
    m->len += len;
    m->buf[m->len] = '\0';
    return (int)len;
}

static log_writer_t writer(void)
{
    memset(&g_mem, 0, sizeof(g_mem));
    const log_sink_t sink = { .write = mem_write, .flush = NULL,
                              .ctx = &g_mem };
    log_writer_t w;
    log_writer_init(&w, &sink);
    return w;
}

/* Row @p k of the file, 0 the header, without its line feed. */
static void line_of(unsigned k, char *out, size_t cap)
{
    const char *p = g_mem.buf;
    for (unsigned i = 0u; i < k && p != NULL; ++i) {
        p = strchr(p, '\n');
        if (p != NULL) {
            ++p;
        }
    }
    out[0] = '\0';
    if (p == NULL) {
        return;
    }
    const char *e = strchr(p, '\n');
    const size_t n = (e != NULL) ? (size_t)(e - p) : strlen(p);
    const size_t take = (n < cap - 1u) ? n : cap - 1u;
    memcpy(out, p, take);
    out[take] = '\0';
}

/* The last two cells before `source` of row @p k, as "pulse;angle": the
 * text between the third-last and the last separator. */
static void servo_cells(unsigned k, char *out, size_t cap)
{
    char row[LOG_WRITER_LINE_MAX];
    line_of(k, row, sizeof(row));
    int seps[3] = { -1, -1, -1 };
    for (int i = 0; row[i] != '\0'; ++i) {
        if (row[i] == ';') {
            seps[0] = seps[1];
            seps[1] = seps[2];
            seps[2] = i;
        }
    }
    out[0] = '\0';
    if (seps[0] < 0) {
        return;
    }
    snprintf(out, cap, "%.*s", seps[2] - seps[0] - 1, row + seps[0] + 1);
}

static bench_state_t bench_empty(void)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.valid = true;
    return b;
}

/* -------------------------------------------------- the pulse and angle */

TEST_CASE(a_servo_row_carries_the_pulse_and_the_angle)
{
    const bench_state_t b = bench_empty();
    log_servo_t sv;
    log_servo_unpack(log_servo_pack(true, 1500u, true, 12.34f), false, &sv);
    CHECK(sv.pulse_ok && sv.angle_ok);
    CHECK_EQ(sv.pulse_us, 1500u);
    CHECK_NEAR(sv.angle_deg, 12.34f, 0.006f);
    log_writer_t w = writer();
    CHECK(log_writer_servo_row(&w, 1.0f, &b, NULL, &sv));
    char got[64];
    servo_cells(1u, got, sizeof(got));
    CHECK_STR_EQ(got, "1500;12.3");

    /* Signed, to 0.1deg, at both ends of the circle. */
    log_servo_unpack(log_servo_pack(true, 2100u, true, -179.96f), false, &sv);
    CHECK(log_writer_servo_row(&w, 1.05f, &b, NULL, &sv));
    servo_cells(2u, got, sizeof(got));
    CHECK_STR_EQ(got, "2100;-180.0");
}

TEST_CASE(no_feedback_leaves_the_angle_empty)
{
    /* No encoder reading: the pulse alone.  No pulse on the port, a
     * programming session holding it: neither.  NULL: neither. */
    const bench_state_t b = bench_empty();
    log_servo_t sv;
    log_writer_t w = writer();
    log_servo_unpack(log_servo_pack(true, 1000u, false, 5.0f), false, &sv);
    CHECK(sv.pulse_ok && !sv.angle_ok);
    CHECK(log_writer_servo_row(&w, 1.0f, &b, NULL, &sv));
    log_servo_unpack(log_servo_pack(false, 1000u, false, 0.0f), false, &sv);
    CHECK(!sv.pulse_ok && !sv.angle_ok);
    CHECK(log_writer_servo_row(&w, 1.05f, &b, NULL, &sv));
    CHECK(log_writer_servo_row(&w, 1.1f, &b, NULL, NULL));
    char got[64];
    servo_cells(1u, got, sizeof(got));
    CHECK_STR_EQ(got, "1000;");
    servo_cells(2u, got, sizeof(got));
    CHECK_STR_EQ(got, ";");
    servo_cells(3u, got, sizeof(got));
    CHECK_STR_EQ(got, ";");
}

TEST_CASE(the_angle_is_a_measurement_and_the_pulse_a_command)
{
    /* The encoder reads through the IOmcu: a modelled row takes no angle.
     * The pulse is the panel's own command, on a row of either source. */
    log_servo_t sv;
    const uint32_t packed = log_servo_pack(true, 1500u, true, 30.0f);
    log_servo_unpack(packed, true, &sv);
    CHECK(sv.pulse_ok);
    CHECK(!sv.angle_ok);
    log_servo_unpack(packed, false, &sv);
    CHECK(sv.angle_ok);
}

TEST_CASE(a_pulse_or_angle_out_of_range_packs_as_none)
{
    log_servo_t sv;
    /* A pulse of 0 is no pulse; an angle that is not finite or past what
     * the packing holds, +-327.67deg, is no angle. */
    log_servo_unpack(log_servo_pack(true, 0u, true, NAN), false, &sv);
    CHECK(!sv.pulse_ok && !sv.angle_ok);
    log_servo_unpack(log_servo_pack(true, 65535u, true, 400.0f), false, &sv);
    CHECK(sv.pulse_ok && !sv.angle_ok);
    CHECK_EQ(sv.pulse_us, 65535u);
    log_servo_unpack(log_servo_pack(true, 900u, true, -327.67f), false, &sv);
    CHECK(sv.angle_ok);
    CHECK_NEAR(sv.angle_deg, -327.67f, 0.006f);
    log_servo_unpack(log_servo_pack(true, 900u, true, INFINITY), false, &sv);
    CHECK(!sv.angle_ok);
    log_servo_unpack(0u, false, NULL);
}

/* ------------------------------------------------- the INA3221 as meter */

static sense_link_win_t window(uint16_t mv, int16_t ma)
{
    sense_link_win_t w;
    memset(&w, 0, sizeof(w));
    w.number  = 41u;
    w.current = true;
    w.voltage = true;
    w.mean_ma = ma;
    w.max_ma  = ma;
    w.min_ma  = ma;
    w.mean_mv = mv;
    w.min_mv  = mv;
    return w;
}

TEST_CASE(the_ina3221_window_is_the_rail_while_it_is_the_meter)
{
    /* The window's mean bus voltage and mean current, and their product,
     * as SERVO's line shows them. */
    const sense_link_win_t win = window(5980u, 412);
    log_rail_t r;
    log_rail_take_ina(&r, &win, false);
    CHECK(!r.withheld);
    CHECK_EQ(r.ok, (uint8_t)(SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT));
    CHECK_NEAR(r.v, 5.98f, 1e-4f);
    CHECK_NEAR(r.i, 0.412f, 1e-5f);
    CHECK_NEAR(r.p, 5.98f * 0.412f, 1e-4f);
    bench_totals_t t;
    bench_totals_reset(&t);
    log_rail_count(&t, &r, 1.0f, true);
    const bench_state_t b = bench_empty();
    log_writer_t w = writer();
    CHECK(log_writer_servo_row(&w, 1.0f, &b, &r, NULL));
    char got[LOG_WRITER_LINE_MAX];
    line_of(1u, got, sizeof(got));
    CHECK(strncmp(got, "1.000;5.98;0.412;2.46;;;;0;0.00;", 32) == 0);

    /* A negative current is a reading; so is a clipped window's mean. */
    const sense_link_win_t neg = window(6010u, -20);
    log_rail_take_ina(&r, &neg, false);
    CHECK_NEAR(r.i, -0.020f, 1e-5f);
}

TEST_CASE(a_window_without_a_quantity_leaves_its_cells_empty)
{
    sense_link_win_t win = window(5980u, 412);
    win.voltage = false;
    log_rail_t r;
    log_rail_take_ina(&r, &win, false);
    CHECK_EQ(r.ok, (uint8_t)SUPPLY_OK_CURRENT);
    win.voltage = true;
    win.current = false;
    log_rail_take_ina(&r, &win, false);
    CHECK_EQ(r.ok, (uint8_t)SUPPLY_OK_VOLTAGE);
    /* A modelled row takes no INA3221 window: the part reads through the
     * IOmcu. */
    win.current = true;
    log_rail_take_ina(&r, &win, true);
    CHECK(r.withheld);
    CHECK_EQ(r.ok, 0u);
    log_rail_take_ina(&r, NULL, false);
    CHECK_EQ(r.ok, 0u);
    CHECK(!r.withheld);
    log_rail_take_ina(NULL, &win, false);
}

/* ------------------------------------------------------ the whole file */

TEST_CASE(the_fullest_servo_file_reads_back_whole)
{
    /* Three groups of a sensor map: 13 + 9 + 2 + 1 = 25 columns, the most
     * the reader takes. */
    log_cols_t cols;
    memset(&cols, 0, sizeof(cols));
    cols.n = LOG_COLS_GROUPS;
    for (unsigned k = 0u; k < cols.n; ++k) {
        cols.ch[k] = (uint8_t)k;
        snprintf(cols.name[k], sizeof(cols.name[k]), "out %u", k + 1u);
    }
    log_writer_t w = writer();
    log_writer_set_cols(&w, &cols);
    const bench_state_t b = bench_empty();
    log_servo_t sv;
    log_servo_unpack(log_servo_pack(true, 1500u, true, 1.0f), false, &sv);
    for (int k = 0; k < 10; ++k) {
        CHECK(log_writer_servo_row(&w, 0.05f * (float)k, &b, NULL, &sv));
    }
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_mem.buf, g_mem.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
    CHECK_EQ(a.n_columns, LOG_MAX_COLUMNS);
    CHECK_EQ(a.ragged_rows, 0);
    CHECK_STR_EQ(a.columns[22].name, "pulse");
    CHECK_STR_EQ(a.columns[23].name, "angle");
    CHECK(a.columns[22].numeric && a.columns[23].numeric);
    CHECK_STR_EQ(log_field_meta("pulse").group, "Servo");
    CHECK_STR_EQ(log_field_meta("angle").group, "Servo");
}

/* --------------------------------------------------- what LOGS plots */

static void analyse(const char *text, log_analysis_t *a)
{
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    CHECK_EQ(log_csv_analyse(&src, &opts, a), LOG_OK);
}

TEST_CASE(a_servo_file_plots_pulse_angle_current_and_voltage_first)
{
    log_analysis_t a;
    analyse("time (s);voltage (V);current (A);power (W);pulse (us);"
            "angle (deg);source\n"
            "0.050;6.00;0.035;0.21;1500;0.4;measured\n"
            "0.100;6.00;0.120;0.72;1600;9.1;measured\n", &a);
    int picks[LOG_MAX_SERIES];
    CHECK_EQ(log_fields_picks(&a, picks, LOG_MAX_SERIES), 4);
    CHECK_STR_EQ(a.columns[picks[0]].name, "pulse");
    CHECK_STR_EQ(a.columns[picks[1]].name, "angle");
    CHECK_STR_EQ(a.columns[picks[2]].name, "current");
    CHECK_STR_EQ(a.columns[picks[3]].name, "voltage");

    /* An angle column with nothing in it: the next numeric columns fill
     * the place, the time axis never. */
    analyse("time (s);voltage (V);current (A);power (W);pulse (us);"
            "angle (deg);source\n"
            "0.050;6.00;0.035;0.21;1500;;measured\n"
            "0.100;6.00;0.120;0.72;1600;;measured\n", &a);
    CHECK_EQ(log_fields_picks(&a, picks, LOG_MAX_SERIES), 4);
    CHECK_STR_EQ(a.columns[picks[0]].name, "pulse");
    CHECK_STR_EQ(a.columns[picks[1]].name, "current");
    CHECK_STR_EQ(a.columns[picks[2]].name, "voltage");
    CHECK_STR_EQ(a.columns[picks[3]].name, "power");
}

TEST_CASE(a_file_without_a_pulse_plots_its_first_numeric_columns)
{
    log_analysis_t a;
    analyse("time (s);voltage (V);current (A);power (W);rpm (rpm);"
            "esc (C);source\n"
            "0.050;12.00;1.00;12;1000;30.0;measured\n"
            "0.100;12.10;1.10;13;1100;30.5;measured\n", &a);
    int picks[LOG_MAX_SERIES];
    CHECK_EQ(log_fields_picks(&a, picks, LOG_MAX_SERIES), 4);
    for (int k = 0; k < 4; ++k) {
        CHECK_EQ(picks[k], k + 1);
    }
    CHECK_EQ(log_fields_picks(&a, picks, 2), 2);
    CHECK_EQ(log_fields_picks(NULL, picks, 4), 0);
    CHECK_EQ(log_fields_picks(&a, NULL, 4), 0);
}

int main(void)
{
    RUN(a_servo_row_carries_the_pulse_and_the_angle);
    RUN(no_feedback_leaves_the_angle_empty);
    RUN(the_angle_is_a_measurement_and_the_pulse_a_command);
    RUN(a_pulse_or_angle_out_of_range_packs_as_none);
    RUN(the_ina3221_window_is_the_rail_while_it_is_the_meter);
    RUN(a_window_without_a_quantity_leaves_its_cells_empty);
    RUN(the_fullest_servo_file_reads_back_whole);
    RUN(a_servo_file_plots_pulse_angle_current_and_voltage_first);
    RUN(a_file_without_a_pulse_plots_its_first_numeric_columns);
    return test_summary("servo_runlog_cols");
}
