/*
 * The run log of a bench ARMED from SERVO (SERVOnnn.CSV): the motor run's
 * columns, with voltage, current, power, charge and energy taken from the
 * supply's readings (log_rail_t) and the other columns from the bench.  A
 * motor run's rows are what they were.  Every case writes into memory and
 * compares the row text, or reads the file back with the CSV reader.
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

/* The PD mini at 6.00V, 0.035A, as the station read it at idle. */
static supply_state_t pd_mini(void)
{
    supply_state_t s;
    memset(&s, 0, sizeof(s));
    s.set_v    = 6.0f;
    s.set_i    = 2.0f;
    s.v        = 6.0f;
    s.i        = 0.035f;
    s.p        = 0.21f;
    s.mode     = SUPPLY_MODE_CV;
    s.output   = true;
    s.online   = true;
    s.ok       = (uint8_t)(SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    s.samples  = 7u;
    return s;
}

/* A BENCH page with nothing measured: no ESC, no INA228. */
static bench_state_t bench_empty(void)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.valid = true;
    return b;
}

/* --------------------------------------------------- the supply's cells */

TEST_CASE(a_servo_row_with_a_supply_reading_carries_its_figures)
{
    log_writer_t w = writer();
    const supply_state_t s = pd_mini();
    const bench_state_t b = bench_empty();
    log_rail_t r;
    log_rail_take(&r, &s, false);
    bench_totals_t t;
    bench_totals_reset(&t);
    t.mah = 1.4f;
    t.wh = 0.0082f;
    t.counted = (uint8_t)(BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY);
    log_rail_count(&t, &r, 0.0f, true);
    CHECK(log_writer_servo_row(&w, 1.0f, &b, &r, NULL));

    char got[LOG_WRITER_LINE_MAX];
    line_of(1u, got, sizeof(got));
    /* time; voltage; current; power; rpm; esc; motor; charge; energy; ina
     * voltage; ina current; esc current; window; ch1 to ch3; source. */
    CHECK_STR_EQ(got, "1.000;6.00;0.035;0.21;;;;1;0.01;;;;;;;;;;;;;;;;measured");
}

TEST_CASE(a_servo_row_without_a_supply_reading_has_empty_supply_cells)
{
    /* A supply that does not answer, a rail never taken, and no rail at all
     * write the same row: every cell nothing measured is empty. */
    supply_state_t s = pd_mini();
    s.online = false;
    s.ok = 0u;
    const bench_state_t b = bench_empty();
    static const char k_empty[] = "1.000;;;;;;;;;;;;;;;;;;;;;;;;measured";

    log_writer_t w = writer();
    log_rail_t r;
    log_rail_take(&r, &s, false);
    bench_totals_t t;
    bench_totals_reset(&t);
    log_rail_count(&t, &r, 0.05f, true);
    CHECK_EQ(t.counted, 0u);
    CHECK(log_writer_servo_row(&w, 1.0f, &b, &r, NULL));
    char got[LOG_WRITER_LINE_MAX];
    line_of(1u, got, sizeof(got));
    CHECK_STR_EQ(got, k_empty);

    log_writer_t w2 = writer();
    log_rail_t none;
    memset(&none, 0, sizeof(none));
    CHECK(log_writer_servo_row(&w2, 1.0f, &b, &none, NULL));
    line_of(1u, got, sizeof(got));
    CHECK_STR_EQ(got, k_empty);

    log_writer_t w3 = writer();
    CHECK(log_writer_servo_row(&w3, 1.0f, &b, NULL, NULL));
    line_of(1u, got, sizeof(got));
    CHECK_STR_EQ(got, k_empty);
}

TEST_CASE(a_supply_reading_with_one_half_writes_only_that_half)
{
    /* A current with no voltage: current and charge, and no power or
     * energy, which need both. */
    supply_state_t s = pd_mini();
    s.ok = (uint8_t)SUPPLY_OK_CURRENT;
    const bench_state_t b = bench_empty();
    log_writer_t w = writer();
    log_rail_t r;
    log_rail_take(&r, &s, false);
    bench_totals_t t;
    bench_totals_reset(&t);
    log_rail_count(&t, &r, 1.0f, true);
    CHECK_EQ(t.counted, (uint8_t)BENCH_COUNTED_CHARGE);
    CHECK(log_writer_servo_row(&w, 1.0f, &b, &r, NULL));
    char got[LOG_WRITER_LINE_MAX];
    line_of(1u, got, sizeof(got));
    CHECK_STR_EQ(got, "1.000;;0.035;;;;;0;;;;;;;;;;;;;;;;;measured");
}

TEST_CASE(the_supply_model_stays_out_of_a_measured_row)
{
    /* The PD mini disabled in SETUP: the supply is the panel's model.  A
     * row of measured bench readings takes none of its figures; a row the
     * model produced takes them. */
    supply_state_t s = pd_mini();
    s.modelled = true;
    log_rail_t r;
    log_rail_take(&r, &s, false);
    CHECK_EQ(r.ok, 0u);
    log_rail_take(&r, &s, true);
    CHECK_EQ(r.ok, (uint8_t)(SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT));
    CHECK_NEAR(r.v, 6.0f, 1e-6f);

    bench_state_t b = bench_empty();
    b.flags = (uint16_t)LINK_BN_SIMULATED;
    log_writer_t w = writer();
    CHECK(log_writer_servo_row(&w, 2.0f, &b, &r, NULL));
    char got[LOG_WRITER_LINE_MAX];
    line_of(1u, got, sizeof(got));
    CHECK_STR_EQ(got, "2.000;6.00;0.035;0.21;;;;;;;;;;;;;;;;;;;;;model");

    /* A PD mini reading still fresh when the link drops: the bench row is
     * the model's, and the measured supply is withheld from it. */
    s.modelled = false;
    log_rail_take(&r, &s, true);
    CHECK(r.withheld);
    CHECK_EQ(r.ok, 0u);
    log_rail_take(&r, &s, false);
    CHECK(!r.withheld);

    /* NULL takes nothing. */
    log_rail_take(&r, NULL, false);
    CHECK_EQ(r.ok, 0u);
    CHECK_EQ(r.counted, 0u);
}

TEST_CASE(a_measured_row_after_a_modelled_stretch_takes_no_model_totals)
{
    /* The PD mini disabled and the link lost mid-run: the model's rows
     * count the model's supply.  The link back, the measured rows withhold
     * all five cells, the totals of the stretch before included, and those
     * totals stand for the next modelled row. */
    supply_state_t s = pd_mini();
    s.modelled = true;
    bench_totals_t t;
    bench_totals_reset(&t);
    log_rail_t r;
    log_rail_take(&r, &s, true);
    CHECK(!r.withheld);
    log_rail_count(&t, &r, 1.0f, true);
    CHECK_EQ(r.counted,
             (uint8_t)(BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY));
    const float mah = t.mah;

    log_rail_take(&r, &s, false);
    CHECK(r.withheld);
    log_rail_count(&t, &r, 1.0f, true);
    CHECK_EQ(r.counted, 0u);
    CHECK_NEAR(t.mah, mah, 1e-6f);
    const bench_state_t b = bench_empty();
    log_writer_t w = writer();
    CHECK(log_writer_servo_row(&w, 3.0f, &b, &r, NULL));
    char got[LOG_WRITER_LINE_MAX];
    line_of(1u, got, sizeof(got));
    CHECK_STR_EQ(got, "3.000;;;;;;;;;;;;;;;;;;;;;;;;measured");

    log_rail_take(&r, &s, true);
    log_rail_count(&t, &r, 0.0f, true);
    CHECK_NEAR(r.charge_mah, mah, 1e-6f);
    CHECK(r.counted != 0u);
}

TEST_CASE(a_servo_row_keeps_the_bench_columns)
{
    /* An INA228 as BENCH's source and an ESC answering on a servo run: the
     * INA228's figures stay in their own columns, rpm and the ESC's in
     * theirs, and voltage to energy are the supply's. */
    bench_state_t b = bench_empty();
    b.flags = (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                         | LINK_BN_SENSED | LINK_BN_RPM_OK | LINK_BN_TEMP_OK);
    b.voltage = 5.95f;
    b.current = 0.04f;
    b.power = 0.24f;
    b.rpm = 1200.0f;
    b.temp_esc = 31.5f;
    bench_state_set_esc(&b, false, 0.0f, true, 0.30f, false);
    b.charge_mah = 99.0f;
    b.counted = (uint8_t)BENCH_COUNTED_CHARGE;
    const supply_state_t s = pd_mini();
    log_rail_t r;
    log_rail_take(&r, &s, false);
    log_writer_t w = writer();
    CHECK(log_writer_servo_row(&w, 1.0f, &b, &r, NULL));
    char got[LOG_WRITER_LINE_MAX];
    line_of(1u, got, sizeof(got));
    CHECK_STR_EQ(got,
                 "1.000;6.00;0.035;0.21;1200;31.5;;;;5.95;0.04;0.30;;;;;;;;;;;"
                 ";;measured");
}

/* ------------------------------------------------------- the run totals */

TEST_CASE(the_supply_totals_count_from_the_arm_while_driving)
{
    const supply_state_t s = pd_mini();
    log_rail_t r;
    log_rail_take(&r, &s, false);
    bench_totals_t t;
    bench_totals_reset(&t);
    /* 3600 steps of 1.0s at 0.035A and 0.21W: 35mAh and 0.21Wh. */
    for (int k = 0; k < 3600; ++k) {
        log_rail_count(&t, &r, 1.0f, true);
    }
    CHECK_NEAR(r.charge_mah, 35.0f, 0.05f);
    CHECK_NEAR(r.energy_wh, 0.21f, 0.001f);
    CHECK_EQ(r.counted,
             (uint8_t)(BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY));

    /* A step longer than BENCH_TOTALS_MAX_STEP_S counts 1.0s; a negative
     * or NaN step nothing; and with the bank SAFE nothing either, while the
     * totals stay on the rail. */
    const float before = t.mah;
    log_rail_count(&t, &r, 30.0f, true);
    CHECK_NEAR(t.mah - before, 0.035f / 3.6f, 1e-4f);
    const float after = t.mah;
    log_rail_count(&t, &r, -1.0f, true);
    log_rail_count(&t, &r, NAN, true);
    log_rail_count(&t, &r, 1.0f, false);
    CHECK_NEAR(t.mah, after, 1e-6f);
    CHECK_NEAR(r.charge_mah, after, 1e-6f);

    /* A new arm starts at nothing counted. */
    bench_totals_reset(&t);
    log_rail_count(&t, &r, 0.0f, false);
    CHECK_EQ(r.counted, 0u);
    CHECK_NEAR(r.charge_mah, 0.0f, 1e-6f);
    log_rail_count(NULL, &r, 1.0f, true);
    log_rail_count(&t, NULL, 1.0f, true);
}

/* ------------------------------------------------- the motor run's rows */

TEST_CASE(a_motor_row_is_unchanged)
{
    /* The rows of the writer's own suite (test_logwriter), byte for byte:
     * log_writer_row() takes no supply. */
    bench_state_t b = bench_empty();
    b.flags = (uint16_t)(LINK_BN_RPM_OK | LINK_BN_TEMP_OK);
    b.rpm = 11419.0f;
    b.temp_esc = 46.3f;
    log_writer_t w = writer();
    CHECK(log_writer_row(&w, 1.0f, &b));
    char got[LOG_WRITER_LINE_MAX];
    line_of(1u, got, sizeof(got));
    CHECK_STR_EQ(got, "1.000;;;;11419;46.3;;;;;;;;;;;;;;;;;measured");

    b = bench_empty();
    b.flags = (uint16_t)LINK_BN_CURRENT_OK;
    b.counted = BENCH_COUNTED_CHARGE;
    b.current = 10.0f;
    b.charge_mah = 5.0f;
    log_writer_t w2 = writer();
    CHECK(log_writer_row(&w2, 1.0f, &b));
    line_of(1u, got, sizeof(got));
    CHECK_STR_EQ(got, "1.000;;10.00;;;;;5;;;;10.00;;;;;;;;;;;measured");
}

TEST_CASE(a_servo_file_has_the_motor_files_columns_and_two_more)
{
    /* The motor file's header, with the pulse and the angle before
     * `source`: LOGS, the card reader and the tools read both by one rule.
     * With CH1 to CH3, and with a sensor map's groups. */
    for (int by_map = 0; by_map < 2; ++by_map) {
        log_cols_t cols;
        memset(&cols, 0, sizeof(cols));
        cols.n = 2u;
        cols.ch[0] = 0u;
        cols.ch[1] = 4u;
        snprintf(cols.name[0], sizeof(cols.name[0]), "port 1");
        snprintf(cols.name[1], sizeof(cols.name[1]), "supply");
        const bench_state_t b = bench_empty();
        log_writer_t w = writer();
        log_writer_set_cols(&w, by_map ? &cols : NULL);
        CHECK(log_writer_row(&w, 1.0f, &b));
        char motor[512];
        line_of(0u, motor, sizeof(motor));
        log_writer_t w2 = writer();
        log_writer_set_cols(&w2, by_map ? &cols : NULL);
        CHECK(log_writer_servo_row(&w2, 1.0f, &b, NULL, NULL));
        char servo[512];
        line_of(0u, servo, sizeof(servo));
        char want[512];
        const size_t keep = strlen(motor) - strlen("source");
        snprintf(want, sizeof(want), "%.*spulse (us);angle (deg);source",
                 (int)keep, motor);
        CHECK_STR_EQ(servo, want);
        CHECK(strncmp(motor + keep, "source", 6) == 0);
    }
}

TEST_CASE(a_servo_run_reads_back_with_its_supply_columns)
{
    /* 40 rows at 0.053s, the supply stepping 4.80V to 6.00V halfway: the
     * reader finds every row whole and the supply's columns numeric. */
    log_writer_t w = writer();
    supply_state_t s = pd_mini();
    const bench_state_t b = bench_empty();
    bench_totals_t t;
    bench_totals_reset(&t);
    for (int k = 0; k < 40; ++k) {
        s.v = (k < 20) ? 4.8f : 6.0f;
        s.p = s.v * s.i;
        log_rail_t r;
        log_rail_take(&r, &s, false);
        log_rail_count(&t, &r, 0.053f, true);
        CHECK(log_writer_servo_row(&w, 0.036f + 0.053f * (float)k, &b, &r,
                                     NULL));
    }
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_mem.buf, g_mem.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
    CHECK_EQ(a.ragged_rows, 0);
    CHECK_EQ(a.n_columns, 25);
    CHECK_EQ(a.time_index, 0);
    for (int c = 1; c <= 3; ++c) {
        CHECK(a.columns[c].numeric);
    }
    CHECK(a.columns[7].numeric);
    CHECK(a.columns[8].numeric);
}

int main(void)
{
    RUN(a_servo_row_with_a_supply_reading_carries_its_figures);
    RUN(a_servo_row_without_a_supply_reading_has_empty_supply_cells);
    RUN(a_supply_reading_with_one_half_writes_only_that_half);
    RUN(the_supply_model_stays_out_of_a_measured_row);
    RUN(a_measured_row_after_a_modelled_stretch_takes_no_model_totals);
    RUN(a_servo_row_keeps_the_bench_columns);
    RUN(the_supply_totals_count_from_the_arm_while_driving);
    RUN(a_motor_row_is_unchanged);
    RUN(a_servo_file_has_the_motor_files_columns_and_two_more);
    RUN(a_servo_run_reads_back_with_its_supply_columns);
    return test_summary("servo_runlog");
}
