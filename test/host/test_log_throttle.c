/*
 * A MOTOR file's commanded throttle (`throttle (%)`, after `rpm`): on an
 * IOmcu's sample the THROTTLE in force when it was read, the last one the
 * IOmcu acknowledged, 0 under an ARM that arms no motor (bench_throttle_t);
 * on the model's the throttle it runs at.  log_writer_row() writes it to
 * 0.1%, and the reader reads it back as column "throttle" in %, grouped as
 * Motor.  A file written without the
 * column reads as before.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "bench_state.h"
#include "log_csv.h"
#include "log_fields.h"
#include "log_writer.h"
#include "telemetry_sim.h"

/* ------------------------------------------------------------- the sink */

typedef struct {
    char   buf[8 * 1024];
    size_t len;
} sink_t;

static int sink_write(void *ctx, const void *data, size_t len)
{
    sink_t *s = (sink_t *)ctx;
    if (s->len + len > sizeof(s->buf) - 1u) {
        return -1;
    }
    memcpy(s->buf + s->len, data, len);
    s->len += len;
    s->buf[s->len] = '\0';
    return (int)len;
}

static sink_t g_sink;

static log_writer_t writer(void)
{
    memset(&g_sink, 0, sizeof(g_sink));
    const log_sink_t sk = { .write = sink_write, .flush = NULL,
                            .ctx = &g_sink };
    log_writer_t w;
    log_writer_init(&w, &sk);
    return w;
}

/* Line @p k of the sink (0 the header), without its line feed. */
static void line_of(unsigned k, char *out, size_t cap)
{
    const char *p = g_sink.buf;
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

/* Cell @p c of line @p k, empty when the line has fewer cells. */
static void cell_of(unsigned k, unsigned c, char *out, size_t cap)
{
    char line[LOG_WRITER_LINE_MAX];
    line_of(k, line, sizeof(line));
    const char *p = line;
    for (unsigned i = 0u; i < c && p != NULL; ++i) {
        p = strchr(p, ';');
        if (p != NULL) {
            ++p;
        }
    }
    out[0] = '\0';
    if (p == NULL) {
        return;
    }
    const char *e = strchr(p, ';');
    const size_t n = (e != NULL) ? (size_t)(e - p) : strlen(p);
    const size_t take = (n < cap - 1u) ? n : cap - 1u;
    memcpy(out, p, take);
    out[take] = '\0';
}

/* A measured sample: voltage, current and rpm, as an IOmcu reports them. */
static bench_state_t measured(float rpm)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.flags = (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                         | LINK_BN_RPM_OK);
    b.voltage = 16.80f;
    b.current = 12.5f;
    b.power = b.voltage * b.current;
    b.rpm = rpm;
    b.valid = true;
    return b;
}

/* -------------------------------------------------- the throttle cell */

TEST_CASE(a_motor_row_carries_the_commanded_throttle)
{
    log_writer_t w = writer();
    static const float k_pct[] = { 0.0f, 42.5f, 100.0f };
    for (unsigned k = 0u; k < 3u; ++k) {
        bench_state_t b = measured(1000.0f * (float)(k + 1u));
        bench_state_set_throttle(&b, k_pct[k]);
        CHECK(log_writer_row(&w, 0.05f * (float)k, &b));
    }
    char got[LOG_WRITER_LINE_MAX];
    line_of(0u, got, sizeof(got));
    CHECK(strstr(got, ";rpm (rpm);throttle (%);esc (C);") != NULL);
    cell_of(1u, 5u, got, sizeof(got));
    CHECK_STR_EQ(got, "0.0");
    cell_of(2u, 5u, got, sizeof(got));
    CHECK_STR_EQ(got, "42.5");
    cell_of(3u, 5u, got, sizeof(got));
    CHECK_STR_EQ(got, "100.0");
    /* The cells either side are rpm's and the ESC temperature's. */
    line_of(2u, got, sizeof(got));
    CHECK(strstr(got, ";2000;42.5;;") != NULL);

    /* Read back: column 5, named "throttle", in %, grouped as Motor. */
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_sink.buf, g_sink.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
    CHECK_EQ(a.n_columns, 24);
    CHECK_EQ(a.ragged_rows, 0);
    CHECK_STR_EQ(a.columns[5].name, "throttle");
    CHECK_STR_EQ(a.columns[5].unit, "%");
    CHECK(a.columns[5].numeric);
    log_source_t src2;
    log_mem_ctx_t ctx2;
    log_source_memory(&src2, &ctx2, g_sink.buf, g_sink.len);
    const int cols[2] = { 4, 5 };
    log_data_t data;
    CHECK_EQ(log_csv_build(&src2, &a, cols, 2, &data), LOG_OK);
    CHECK_EQ(data.count, 3);
    CHECK_STR_EQ(data.field[1].name, "throttle");
    CHECK_STR_EQ(data.field[1].unit, "%");
    CHECK_STR_EQ(data.field[1].group, "Motor");
    CHECK_NEAR(data.value[1][0], 0.0f, 1e-3f);
    CHECK_NEAR(data.value[1][1], 42.5f, 1e-3f);
    CHECK_NEAR(data.value[1][2], 100.0f, 1e-3f);
    CHECK_NEAR(data.value[0][1], 2000.0f, 0.5f);
    log_data_free(&data);
    CHECK_STR_EQ(log_field_meta("throttle").group, "Motor");
    CHECK_STR_EQ(log_field_meta("throttle").unit, "%");
}

TEST_CASE(the_throttle_is_clamped_and_one_not_finite_is_none)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    bench_state_set_throttle(&b, -5.0f);
    CHECK(b.throttle_ok);
    CHECK_NEAR(b.throttle_pct, 0.0f, 1e-6f);
    bench_state_set_throttle(&b, 150.0f);
    CHECK_NEAR(b.throttle_pct, 100.0f, 1e-6f);
    bench_state_set_throttle(&b, 0.1f);
    CHECK_NEAR(b.throttle_pct, 0.1f, 1e-6f);
    bench_state_set_throttle(&b, INFINITY);
    CHECK(!b.throttle_ok);
    bench_state_set_throttle(&b, 30.0f);
    bench_state_set_throttle(&b, NAN);
    CHECK(!b.throttle_ok);
    CHECK_NEAR(b.throttle_pct, 0.0f, 1e-6f);
    bench_state_set_throttle(NULL, 30.0f);

    /* None is an empty cell, not 0. */
    log_writer_t w = writer();
    b = measured(5000.0f);
    bench_state_set_throttle(&b, NAN);
    CHECK(log_writer_row(&w, 0.0f, &b));
    char got[32];
    cell_of(1u, 5u, got, sizeof(got));
    CHECK_STR_EQ(got, "");
    /* One OUT_SPAN step is the smallest throttle, and it is written. */
    bench_state_set_throttle(&b, 100.0f / 1000.0f);
    CHECK(log_writer_row(&w, 0.05f, &b));
    cell_of(2u, 5u, got, sizeof(got));
    CHECK_STR_EQ(got, "0.1");
}

/*
 * One poll of the control task as the panel makes it: the sample is asked
 * (bench_throttle_sample()), read, then the control page is written with
 * the THROTTLE a command of this pass set, then the row is taken.
 */
typedef struct {
    bool acked;           /* the IOmcu acknowledges the control write */
    bool throttle_armed;  /* the ARM written arms the throttle outputs */
    uint16_t hundredths;  /* the THROTTLE written */
} pass_t;

static float pass_row(bench_throttle_t *t, const pass_t *p)
{
    bench_throttle_sample(t);
    bench_state_t b = measured(5000.0f);   /* the BENCH read */
    bench_throttle_written(t, p->acked, p->throttle_armed, p->hundredths);
    bench_throttle_to_row(t, &b);
    return b.throttle_ok ? b.throttle_pct : -1.0f;
}

TEST_CASE(a_throttle_written_in_one_pass_is_on_the_next_passes_row)
{
    bench_throttle_t t;
    bench_throttle_reset(&t);
    /* LINK_CT_THROTTLE is in 0.01% steps, the same for PWM and DShot. */
    const pass_t up = { true, true, 4250u };
    CHECK_NEAR(pass_row(&t, &up), 0.0f, 1e-6f);    /* sampled before */
    CHECK_NEAR(pass_row(&t, &up), 42.5f, 1e-4f);
    const pass_t more = { true, true, 7000u };
    CHECK_NEAR(pass_row(&t, &more), 42.5f, 1e-4f);
    CHECK_NEAR(pass_row(&t, &more), 70.0f, 1e-4f);
    /* Past LINK_THROTTLE_MAX is held to it, in force as on the row; one
     * step is 0.01%. */
    const pass_t over = { true, true, 12000u };
    (void)pass_row(&t, &over);
    CHECK_EQ(t.in_force, (uint16_t)LINK_THROTTLE_MAX);
    CHECK_NEAR(pass_row(&t, &over), 100.0f, 1e-4f);
    bench_throttle_written(&t, true, true, (uint16_t)LINK_THROTTLE_MAX);
    CHECK_EQ(t.in_force, (uint16_t)LINK_THROTTLE_MAX);
    bench_throttle_written(&t, true, true,
                           (uint16_t)(LINK_THROTTLE_MAX + 1u));
    CHECK_EQ(t.in_force, (uint16_t)LINK_THROTTLE_MAX);
    const pass_t step = { true, true, 1u };
    (void)pass_row(&t, &step);
    CHECK_NEAR(pass_row(&t, &step), 0.01f, 1e-5f);
}

TEST_CASE(a_refused_or_unanswered_write_keeps_the_throttle_in_force)
{
    bench_throttle_t t;
    bench_throttle_reset(&t);
    const pass_t on = { true, true, 3000u };
    (void)pass_row(&t, &on);
    CHECK_NEAR(pass_row(&t, &on), 30.0f, 1e-4f);
    /* A NACK, or no answer: the IOmcu holds what it held. */
    const pass_t refused = { false, true, 9000u };
    CHECK_NEAR(pass_row(&t, &refused), 30.0f, 1e-4f);
    CHECK_NEAR(pass_row(&t, &refused), 30.0f, 1e-4f);
    const pass_t refused_off = { false, false, 0u };
    CHECK_NEAR(pass_row(&t, &refused_off), 30.0f, 1e-4f);
    CHECK_NEAR(pass_row(&t, &refused_off), 30.0f, 1e-4f);
    /* Answered again: in force from the row after. */
    const pass_t back = { true, true, 9000u };
    CHECK_NEAR(pass_row(&t, &back), 30.0f, 1e-4f);
    CHECK_NEAR(pass_row(&t, &back), 90.0f, 1e-4f);
}

TEST_CASE(an_arm_of_0_written_puts_0_in_force)
{
    bench_throttle_t t;
    bench_throttle_reset(&t);
    const pass_t on = { true, true, 6000u };
    (void)pass_row(&t, &on);
    CHECK_NEAR(pass_row(&t, &on), 60.0f, 1e-4f);
    /* The poll writes ARM = 0 (a stop, a disarm asked, a release owed)
     * while the panel's THROTTLE still reads 60%: 0 is in force, a figure
     * and not an empty cell. */
    const pass_t held = { true, false, 6000u };
    CHECK_NEAR(pass_row(&t, &held), 60.0f, 1e-4f);
    CHECK_NEAR(pass_row(&t, &held), 0.0f, 1e-6f);
    /* A row of a measured sample with 0 in force reads 0.0. */
    log_writer_t w = writer();
    bench_state_t b = measured(0.0f);
    bench_throttle_sample(&t);
    bench_throttle_to_row(&t, &b);
    CHECK(log_writer_row(&w, 0.0f, &b));
    char got[32];
    cell_of(1u, 5u, got, sizeof(got));
    CHECK_STR_EQ(got, "0.0");
    /* The link's edge: nothing in force until a write is acknowledged,
     * and no sample of the old link's value either. */
    (void)pass_row(&t, &on);
    (void)pass_row(&t, &on);
    bench_throttle_reset(&t);
    bench_state_t edge = measured(0.0f);
    bench_throttle_to_row(&t, &edge);
    CHECK(edge.throttle_ok);
    CHECK_NEAR(edge.throttle_pct, 0.0f, 1e-6f);
    CHECK_NEAR(pass_row(&t, &on), 0.0f, 1e-6f);
    /* NULL does nothing. */
    bench_throttle_reset(NULL);
    bench_throttle_written(NULL, true, true, 100u);
    bench_throttle_sample(NULL);
    bench_throttle_to_row(NULL, &b);
    bench_throttle_to_row(&t, NULL);
}

TEST_CASE(the_model_writes_the_throttle_it_runs_at)
{
    telemetry_sim_t sim;
    telemetry_sim_init(&sim, NULL);
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    telemetry_sim_step(&sim, 45.0f, 0.05f, &b);
    CHECK(b.throttle_ok);
    CHECK_NEAR(b.throttle_pct, 45.0f, 1e-6f);
    CHECK(bench_state_simulated(&b));
    telemetry_sim_step(&sim, 120.0f, 0.05f, &b);
    CHECK_NEAR(b.throttle_pct, 100.0f, 1e-6f);
    telemetry_sim_step(&sim, -1.0f, 0.05f, &b);
    CHECK_NEAR(b.throttle_pct, 0.0f, 1e-6f);

    /* A modelled row carries it as a measured one does. */
    log_writer_t w = writer();
    telemetry_sim_step(&sim, 62.5f, 0.05f, &b);
    CHECK(log_writer_row(&w, 0.0f, &b));
    char got[32];
    cell_of(1u, 5u, got, sizeof(got));
    CHECK_STR_EQ(got, "62.5");
    char line[LOG_WRITER_LINE_MAX];
    line_of(1u, line, sizeof(line));
    CHECK(strstr(line, ";model") != NULL);
}

TEST_CASE(a_row_of_a_window_alone_has_no_throttle)
{
    bench_state_t b = measured(3000.0f);
    bench_state_set_throttle(&b, 70.0f);
    b.servo_new = true;
    b.servo_window = 9u;
    bench_state_t part;
    bench_state_log_row(&b, false, &part);
    CHECK(!part.throttle_ok);
    bench_state_t whole;
    bench_state_log_row(&b, true, &whole);
    CHECK(whole.throttle_ok);
    CHECK_NEAR(whole.throttle_pct, 70.0f, 1e-6f);

    log_writer_t w = writer();
    CHECK(log_writer_row(&w, 0.0f, &part));
    whole.servo_new = false;
    CHECK(log_writer_row(&w, 0.0f, &whole));
    char got[32];
    cell_of(1u, 5u, got, sizeof(got));
    CHECK_STR_EQ(got, "");
    cell_of(2u, 5u, got, sizeof(got));
    CHECK_STR_EQ(got, "70.0");
}

/* ------------------------------------------------ a file without it */

/*
 * A MOTOR file written before the column: 23 columns, `esc` right after
 * `rpm`.  It reads whole, rpm where it was, no column is "throttle", and
 * LOGS plots the same four columns first as in a file with the column.
 */
TEST_CASE(a_file_without_the_throttle_column_still_reads)
{
    static const char k_old[] =
        "time (s);voltage (V);current (A);power (W);rpm (rpm);esc (C);"
        "motor (C);charge (mAh);energy (Wh);ina voltage (V);"
        "ina current (A);esc current (A);window;ch1 current (A);"
        "ch1 max (A);ch1 voltage (V);ch2 current (A);ch2 max (A);"
        "ch2 voltage (V);ch3 current (A);ch3 max (A);ch3 voltage (V);"
        "source\n"
        "0.050;16.80;12.50;210;1000;31.0;;1;0.01;;;12.40;;;;;;;;;;;"
        "measured\n"
        "0.100;16.78;12.60;211;2000;31.2;;1;0.01;;;12.50;;;;;;;;;;;"
        "measured\n"
        "0.150;16.75;12.80;214;3000;31.5;;2;0.01;;;12.70;;;;;;;;;;;"
        "measured\n";
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, k_old, strlen(k_old));
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
    CHECK_EQ(a.n_columns, 23);
    CHECK_EQ(a.row_count, 3);
    CHECK_EQ(a.ragged_rows, 0);
    CHECK_EQ(a.time_index, 0);
    CHECK_STR_EQ(a.columns[4].name, "rpm");
    CHECK_STR_EQ(a.columns[5].name, "esc");
    for (int c = 0; c < a.n_columns; ++c) {
        CHECK(strcmp(a.columns[c].name, "throttle") != 0);
    }
    int old_picks[LOG_MAX_SERIES];
    const int n_old = log_fields_picks(&a, old_picks, LOG_MAX_SERIES);
    CHECK_EQ(n_old, 4);
    log_source_t src2;
    log_mem_ctx_t ctx2;
    log_source_memory(&src2, &ctx2, k_old, strlen(k_old));
    log_data_t data;
    CHECK_EQ(log_csv_build(&src2, &a, old_picks, n_old, &data), LOG_OK);
    CHECK_EQ(data.count, 3);
    CHECK_STR_EQ(data.field[3].name, "rpm");
    CHECK_NEAR(data.value[3][2], 3000.0f, 0.5f);
    log_data_free(&data);

    /* The same samples with the column: the first four picks by name are
     * the old file's. */
    log_writer_t w = writer();
    for (unsigned k = 0u; k < 3u; ++k) {
        bench_state_t b = measured(1000.0f * (float)(k + 1u));
        bench_state_set_throttle(&b, 20.0f * (float)(k + 1u));
        CHECK(log_writer_row(&w, 0.05f * (float)(k + 1u), &b));
    }
    log_source_t src3;
    log_mem_ctx_t ctx3;
    log_source_memory(&src3, &ctx3, g_sink.buf, g_sink.len);
    log_analysis_t b;
    CHECK_EQ(log_csv_analyse(&src3, &opts, &b), LOG_OK);
    int new_picks[LOG_MAX_SERIES];
    CHECK_EQ(log_fields_picks(&b, new_picks, LOG_MAX_SERIES), n_old);
    for (int k = 0; k < n_old; ++k) {
        CHECK_STR_EQ(b.columns[new_picks[k]].name,
                     a.columns[old_picks[k]].name);
    }
}

int main(void)
{
    RUN(a_motor_row_carries_the_commanded_throttle);
    RUN(the_throttle_is_clamped_and_one_not_finite_is_none);
    RUN(a_throttle_written_in_one_pass_is_on_the_next_passes_row);
    RUN(a_refused_or_unanswered_write_keeps_the_throttle_in_force);
    RUN(an_arm_of_0_written_puts_0_in_force);
    RUN(the_model_writes_the_throttle_it_runs_at);
    RUN(a_row_of_a_window_alone_has_no_throttle);
    RUN(a_file_without_the_throttle_column_still_reads);
    return test_summary("log_throttle");
}
