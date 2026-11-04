/**
 * @file test_servo_source_angle.c
 * @brief The SERVO screen's runs with ANGLE SOURCE IMU: the IMU's angle
 * handed in as AS5600 counts (servo_angle_enc()), as the panel does it.
 *
 * Cases:
 * - the_servo_screen_shows_the_imu_angle_when_the_source_is_imu
 * - the_automatic_test_judges_moves_on_imu_counts
 * - the_limit_run_report_names_the_source_and_the_angle
 * - the_csv_header_ends_in_angle_source_and_each_row_names_it
 * - the_run_log_angle_follows_the_source
 * - an_imu_silent_300ms_is_a_gap_and_a_second_within_10s_ends_the_run
 * - the_old_csv_header_without_the_column_still_reads
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_csv.h"
#include "servo_angle.h"
#include "servo_screen_rig.h"

/* ANGLE SOURCE IMU on @p axis with @p neg, the AS5600 off in SETUP and
 * not held by the IOmcu, the IMU's readings every 40ms. */
static void imu_on(uint8_t axis, bool neg)
{
    settings_set(SET_ENC_EN, 0.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    servo_screen_set_enc_held(false);
    servo_screen_set_imu_axis(axis, neg);
    bench.enc = true;
    bench.imu = true;
    bench.enc_next = bench.now;
    bench_frames(100u);                 /* an angle before the start */
}

/* The IMU's reading of @p deg at @p at, as the panel hands it in. */
static servo_test_enc_t imu_reading(bool valid, float deg, uint32_t at)
{
    const servo_angle_in_t in = { .valid = valid, .deg = deg,
                                  .still_ms = 200u, .taken_ms = at };
    servo_test_enc_t e;
    (void)servo_angle_enc(&in, &e);
    return e;
}

/* The IMU goes stale: one reading not valid, stamped as the panel found
 * it, then nothing. */
static uint32_t imu_pull(void)
{
    bench.enc = false;
    const servo_test_enc_t gone = imu_reading(false, 0.0f, bench.now);
    servo_screen_encoder(&gone);
    return bench.now;
}

/* Readings again from @p at, every 40ms after. */
static void imu_plug(uint32_t at)
{
    const servo_test_enc_t back =
        imu_reading(true, (bench.sv.position_us - 1500.0f) * 0.09f, at);
    servo_screen_encoder(&back);
    bench.enc = true;
    bench.enc_anchored = false;
    bench.enc_next = bench.now + 40u;
}

/* The automatic test to its end, 120s at most. */
static void test_run_out(void)
{
    for (int i = 0; i < 240 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
}

/* bench_on_ina() with the output on and servo_sim's own stops, as
 * test_servo_limit_panel.c sets up a limit run on the PWM pin GP2. */
static void lim_bench(void)
{
    bench_on_ina();
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    servo_sim_init(&bench.sv, &sc);
    bench.sv.position_us = 1500.0f;
    bench.out = true;
    servo_screen_set_pins(PIN(2), true);
    bench_frames(200u);
}

/* Whether @p line ends in @p tail. */
static bool ends_in(const char *line, const char *tail)
{
    const size_t n = strlen(line), k = strlen(tail);
    return n >= k && strcmp(line + n - k, tail) == 0;
}

/*
 * With the IMU as the source the screen takes its angle from count 0,
 * whatever the AS5600's centre and switch say; with the AS5600 as the
 * source the same reading is the AS5600's, off in SETUP: none.
 */
TEST_CASE(the_servo_screen_shows_the_imu_angle_when_the_source_is_imu)
{
    bench_fresh();
    settings_set(SET_ENC_EN, 0.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    float deg = 0.0f;
    for (int k = -3; k <= 3; ++k) {
        const float want = 30.0f * (float)k;
        const servo_test_enc_t e = imu_reading(true, want, bench.now);
        servo_screen_encoder(&e);
        CHECK(servo_screen_log_angle(&deg));
        CHECK_NEAR(deg, want, 180.0f / 4096.0f + 1e-4f);
    }
    const servo_test_enc_t e = imu_reading(true, 45.0f, bench.now);
    servo_screen_encoder(&e);
    /* A reading not valid: none, and nothing for the AS5600 source. */
    const servo_test_enc_t gone = imu_reading(false, 0.0f, bench.now);
    servo_screen_encoder(&gone);
    CHECK(!servo_screen_log_angle(&deg));
    servo_screen_encoder(&e);
    CHECK(servo_screen_log_angle(&deg));
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_AS5600);
    CHECK(!servo_screen_log_angle(&deg));
    /* The AS5600 on: the same count is 3000 counts from its centre. */
    settings_set(SET_ENC_EN, 1.0f);
    CHECK(servo_screen_log_angle(&deg));
    CHECK_NEAR(deg, servo_test_enc_deg(e.raw, 3000u), 1e-4f);
}

/*
 * The automatic test with the IMU reads the same moves the AS5600 gives
 * it -- the bench turns the horn by the same degrees -- and judges them
 * on counts: PASS, every move settled.  The IOmcu holds no AS5600.  The
 * report names the IMU as the encoder and as the angle source.
 */
TEST_CASE(the_automatic_test_judges_moves_on_imu_counts)
{
    bench_fresh();
    short_runs();
    imu_on(1u, true);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    test_run_out();
    CHECK_EQ(bench.disarms, 0u);
    CHECK(strstr(bench.report, "Result:         PASS") != NULL);
    CHECK(strstr(bench.report, "Encoder:        IMU Y-, in AS5600 counts "
                               "(0.088deg a count),")
          != NULL);
    CHECK(strstr(bench.report, "Angle source:   IMU Y-\n") != NULL);
    CHECK(strstr(bench.report, "Position: the IMU measures the horn;")
          != NULL);
    CHECK(strstr(bench.report, "AS5600 on the horn shaft") == NULL);
    CHECK(strstr(bench.report, "Settled: the angle has stayed within "
                               "1.05deg for 100ms.")
          != NULL);
    CHECK(strstr(bench.report, "ENCODER (angles in degrees from the centre")
          != NULL);
    /* The AS5600's run of the same servo, for the same judgement. */
    char imu_report[4096];
    snprintf(imu_report, sizeof(imu_report), "%s", bench.report);
    bench_fresh();
    short_runs();
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_AS5600);
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_screen_set_enc_held(true);
    bench.enc = true;
    bench.enc_next = bench.now;
    bench_frames(100u);
    hold_start(2.3f);
    test_run_out();
    CHECK(strstr(bench.report, "Result:         PASS") != NULL);
    CHECK(strstr(bench.report, "Angle source:   AS5600\n") != NULL);
    CHECK(strstr(bench.report, "Encoder:        AS5600 on the horn shaft, "
                               "centre count 3000,")
          != NULL);
    /* The encoder's table reads the same from both sources. */
    const char *a = strstr(imu_report, "ENCODER (");
    const char *b = strstr(bench.report, "ENCODER (");
    CHECK(a != NULL && b != NULL);
    if (a != NULL && b != NULL) {
        const char *ae = strstr(a, "\n\n"), *be = strstr(b, "\n\n");
        CHECK(ae != NULL && be != NULL);
        if (ae != NULL && be != NULL) {
            CHECK_EQ((long)(ae - a), (long)(be - b));
            CHECK(strncmp(a, b, (size_t)(ae - a)) == 0);
        }
    }
}

/* The limit run with the IMU: the report names the source and the
 * angle at each end's last free position by it.  An IMU without a
 * reading at the start: the run reads no angles and the report says why. */
TEST_CASE(the_limit_run_report_names_the_source_and_the_angle)
{
    lim_bench();
    imu_on(0u, false);
    bench_frames(200u);
    hold_start_on(1, 2.3f);
    CHECK(servo_screen_limit_running());
    for (int i = 0; i < 240 && servo_screen_limit_running(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_limit_running());
    bench_frames(500u);
    CHECK(strstr(bench.report, "RCBENCH SERVO LIMIT REPORT\n")
          == bench.report);
    CHECK(strstr(bench.report, "Angle source:   IMU X+\n") != NULL);
    CHECK(strstr(bench.report, "                IMU X+ ") != NULL);
    CHECK(strstr(bench.report, "deg at the last free position") != NULL);
    CHECK(strstr(bench.report, "AS5600") == NULL);

    lim_bench();
    imu_on(2u, true);
    bench.enc = false;
    (void)imu_pull();
    bench_frames(400u);
    hold_start_on(1, 2.3f);
    CHECK(servo_screen_limit_running());
    for (int i = 0; i < 240 && servo_screen_limit_running(); ++i) {
        bench_frames(500u);
    }
    bench_frames(500u);
    CHECK(strstr(bench.report, "Angle source:   IMU Z-, no reading at the "
                               "start, no angles")
          != NULL);
    CHECK(strstr(bench.report, "not fitted") == NULL);
    /* In German too. */
    ui_text_set_language(UI_LANG_DE);
    lim_bench();
    imu_on(0u, false);
    bench_frames(200u);
    hold_start_on(1, 2.3f);
    for (int i = 0; i < 240 && servo_screen_limit_running(); ++i) {
        bench_frames(500u);
    }
    bench_frames(500u);
    CHECK(strstr(bench.report, "Winkelquelle:    IMU X+\n") != NULL);
    CHECK(strstr(bench.report, "                 IMU X+ ") != NULL);
    ui_text_set_language(UI_LANG_EN);
}

/* The CSV's header ends in the source's column, and every row names the
 * source: the IMU with its axis and sign, or the AS5600. */
TEST_CASE(the_csv_header_ends_in_angle_source_and_each_row_names_it)
{
    bench_fresh();
    short_runs();
    imu_on(2u, false);
    hold_start(2.3f);
    test_run_out();
    CHECK(ends_in(bench.csv_head, ";clipped;angle source"));
    CHECK(ends_in(bench.csv_last, ";IMU Z+"));
    CHECK(bench.csv > 40u);

    bench_fresh();
    short_runs();
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_AS5600);
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_screen_set_enc_held(true);
    bench.enc = true;
    bench.enc_next = bench.now;
    bench_frames(100u);
    hold_start(2.3f);
    test_run_out();
    CHECK(ends_in(bench.csv_head, ";clipped;angle source"));
    CHECK(ends_in(bench.csv_last, ";AS5600"));

    /* The IMU chosen and stale at the start: the run reads no angles and
     * the report says why. */
    bench_fresh();
    short_runs();
    imu_on(1u, false);
    (void)imu_pull();
    bench_frames(400u);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    test_run_out();
    CHECK(ends_in(bench.csv_head, ";clipped"));
    CHECK(strstr(bench.report, "Angle source:   IMU Y+, no reading at the "
                               "start, no angles")
          != NULL);
    CHECK(strstr(bench.report, "ENCODER (") == NULL);

    /* No source in use: neither the column nor the value. */
    bench_fresh();
    short_runs();
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_AS5600);
    settings_set(SET_ENC_EN, 0.0f);
    hold_start(2.3f);
    test_run_out();
    CHECK(ends_in(bench.csv_head, ";clipped"));
    CHECK(strstr(bench.csv_last, "AS5600") == NULL);
}

/* The SERVO run log takes the angle MEASURED shows: from the source in
 * force, each with its own centre. */
TEST_CASE(the_run_log_angle_follows_the_source)
{
    bench_fresh();
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_screen_set_enc_held(true);
    float deg = 0.0f;
    /* The AS5600's reading 100 counts past its centre. */
    const servo_test_enc_t as = { true, 3100u, 0u, bench.now, false, false,
                                  false, false };
    servo_screen_encoder(&as);
    CHECK(servo_screen_log_angle(&deg));
    CHECK_NEAR(deg, 100.0f * 360.0f / 4096.0f, 0.01f);
    /* The IMU's 12.5deg: from count 0. */
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    const servo_test_enc_t imu = imu_reading(true, 12.5f, bench.now);
    servo_screen_encoder(&imu);
    CHECK(servo_screen_log_angle(&deg));
    CHECK_NEAR(deg, 12.5f, 180.0f / 4096.0f + 1e-4f);
    /* A link that goes down takes it, as for the AS5600. */
    servo_screen_set_link(false);
    CHECK(!servo_screen_log_angle(&deg));
    servo_screen_set_link(true);
    servo_screen_encoder(&imu);
    CHECK(servo_screen_log_angle(&deg));
    /* MEASURED's angle with the IMU stale: none, not the servo's own
     * feedback, since a source is chosen. */
    const servo_test_enc_t gone = imu_reading(false, 0.0f, bench.now);
    servo_screen_encoder(&gone);
    CHECK(!servo_screen_log_angle(&deg));
}

/*
 * The IMU is a measurement-only sensor: a reading 300ms late rides, as
 * the AS5600's does (servo_gap.h), and a second gap within 10s of the
 * first ends the run as an error, the output off and the bench to SAFE.
 */
TEST_CASE(an_imu_silent_300ms_is_a_gap_and_a_second_within_10s_ends_the_run)
{
    bench_fresh();
    short_runs();
    imu_on(0u, false);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    bench_frames(3000u);
    uint32_t t = imu_pull();
    bench_frames(300u);
    CHECK(servo_screen_testing());
    imu_plug(t + 300u);
    bench_frames(2000u);
    CHECK(servo_screen_testing());
    CHECK_EQ(bench.disarms, 0u);
    /* The second, 2.3s after the first began: the run ends. */
    t = imu_pull();
    bench_frames(100u);
    imu_plug(t + 100u);
    bench_frames(1000u);
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK(!bench.out);
    CHECK_EQ(bench.disarms, 1u);
    CHECK(strstr(bench.report, ui_servo_str(SERVO_STR_AB_ENC_GAPS)) != NULL);
    CHECK(strstr(bench.report, "IMU gaps: 2, the longest 300ms.") != NULL);
    CHECK(strstr(bench.report, "AS5600 gaps") == NULL);

    /* 301ms: the first gap ends the run. */
    bench_fresh();
    short_runs();
    imu_on(0u, false);
    hold_start(2.3f);
    bench_frames(3000u);
    (void)imu_pull();
    bench_frames(300u);
    CHECK(servo_screen_testing());
    bench_frames(20u);
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK_EQ(bench.disarms, 1u);
    CHECK(strstr(bench.report, ui_servo_str(SERVO_STR_AB_ENC_SILENT))
          != NULL);

    /* The limit run alike: 300ms rides, 301ms ends it, and its report
     * names the IMU's gaps. */
    for (uint32_t len = 300u; len <= 301u; ++len) {
        lim_bench();
        imu_on(0u, false);
        bench_frames(200u);
        hold_start_on(1, 2.3f);
        CHECK(servo_screen_limit_running());
        bench_frames(2000u);
        const uint32_t at = imu_pull();
        bench_frames(300u);
        CHECK(servo_screen_limit_running());
        imu_plug(at + len);
        bench_frames(40u);
        CHECK_EQ(servo_screen_limit_running(), len <= SERVO_GAP_MAX_MS);
        if (len > SERVO_GAP_MAX_MS) {
            bench_frames(1000u);
            CHECK(!bench.out);
            CHECK(strstr(bench.report, "IMU gaps: 1, the longest 301ms.")
                  != NULL);
            CHECK(strstr(bench.report, "AS5600 gaps") == NULL);
        }
    }
}

/* @p text as the LOGS reader takes it. */
static bool analyse(const char *text, log_analysis_t *an)
{
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, text, strlen(text));
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    return log_csv_analyse(&src, &opts, an) == LOG_OK;
}

/* The column named @p name, or -1. */
static int column(const log_analysis_t *an, const char *name)
{
    for (int i = 0; i < an->n_columns; ++i) {
        if (strcmp(an->columns[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * A file of the format before the angle source's column reads as it did,
 * its angle and meter found by name; a file with the column reads the same
 * columns and the source last.  The recorded files without an encoder
 * (fixtures/) read as they did.
 */
TEST_CASE(the_old_csv_header_without_the_column_still_reads)
{
    static const char k_row[] =
        "1.000;STEP;1;HOLD;1500;;6.00;6.00;2.00;0.120;0.720;CV;;"
        "1.23;;;PDMINI;;;;;";
    static char old_text[1024], new_text[1024];
    const char *head = servo_test_csv_header_enc();
    const size_t cut = strlen(head) - strlen(";angle source");
    CHECK(ends_in(head, ";angle source"));
    snprintf(old_text, sizeof(old_text), "%.*s\n%s\n%s\n", (int)cut, head,
             k_row, k_row);
    snprintf(new_text, sizeof(new_text), "%s\n%s;IMU X+\n%s;IMU X+\n", head,
             k_row, k_row);
    log_analysis_t a, b;
    CHECK(analyse(old_text, &a));
    CHECK(analyse(new_text, &b));
    CHECK_EQ(a.n_columns, 19 + 3);
    CHECK_EQ(b.n_columns, a.n_columns + 1);
    CHECK_EQ(a.ragged_rows, 0);
    CHECK_EQ(b.ragged_rows, 0);
    CHECK_EQ(a.row_count, 2);
    CHECK_EQ(b.row_count, 2);
    static const char *const k_named[] = { "time", "current", "angle",
                                           "meter", "clipped" };
    for (size_t k = 0u; k < sizeof(k_named) / sizeof(k_named[0]); ++k) {
        CHECK(column(&a, k_named[k]) >= 0);
        CHECK_EQ(column(&a, k_named[k]), column(&b, k_named[k]));
    }
    CHECK(a.columns[column(&a, "angle")].numeric);
    CHECK(b.columns[column(&b, "angle")].numeric);
    CHECK_EQ(column(&a, "angle source"), -1);
    CHECK_EQ(column(&b, "angle source"), b.n_columns - 1);
    CHECK(!b.columns[b.n_columns - 1].numeric);

    FILE *f = fopen("fixtures/servo-mg90s.csv", "rb");
    CHECK(f != NULL);
    if (f != NULL) {
        static char text[200000];
        const size_t n = fread(text, 1u, sizeof(text) - 1u, f);
        fclose(f);
        text[n] = '\0';
        log_analysis_t c;
        CHECK(analyse(text, &c));
        CHECK_EQ(c.n_columns, 13);
        CHECK_EQ(c.row_count, 870);
        CHECK_EQ(column(&c, "angle source"), -1);
    }
}

int main(void)
{
    RUN(the_servo_screen_shows_the_imu_angle_when_the_source_is_imu);
    RUN(the_automatic_test_judges_moves_on_imu_counts);
    RUN(the_limit_run_report_names_the_source_and_the_angle);
    RUN(the_csv_header_ends_in_angle_source_and_each_row_names_it);
    RUN(the_run_log_angle_follows_the_source);
    RUN(an_imu_silent_300ms_is_a_gap_and_a_second_within_10s_ends_the_run);
    RUN(the_old_csv_header_without_the_column_still_reads);
    return test_summary("servo_source_angle");
}
