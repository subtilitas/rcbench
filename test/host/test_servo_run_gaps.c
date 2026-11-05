/**
 * @file test_servo_run_gaps.c
 * @brief The SERVO screen's runs and the output encoder's gaps, as the panel
 * hands the readings over (servo_screen_encoder()).
 *
 * Cases:
 * - an_encoder_pulled_and_plugged_back_within_300ms_rides
 * - an_encoder_pulled_for_good_ends_the_test_as_an_error
 * - a_test_started_with_the_encoder_not_answering_ends_after_300ms
 * - the_meter_lost_in_an_encoder_gap_ends_the_test_at_once
 * - the_limit_run_ends_on_an_encoder_gap_of_301ms
 * - a_limit_run_started_with_the_encoder_not_answering_ends_after_300ms
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_limrun.h"
#include "servo_screen_rig.h"

/* AS5600 on in SETUP and held by the IOmcu, its readings every 40ms. */
static void enc_on(void)
{
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_screen_set_enc_held(true);
    bench.enc = true;
    bench.enc_next = bench.now;
    bench_frames(100u);                 /* an angle before the start */
}

/* The part stops answering: "none" once, stamped as the control task
 * found it, then nothing. */
static uint32_t enc_pull(void)
{
    bench.enc = false;
    const servo_test_enc_t gone = { .valid = false, .taken_ms = bench.now };
    servo_screen_encoder(&gone);
    return bench.now;
}

/* It answers at @p at and every 40ms after. */
static void enc_plug(uint32_t at)
{
    const servo_test_enc_t back = { .valid = true, .raw = 3000u,
                                    .taken_ms = at };
    servo_screen_encoder(&back);
    bench.enc = true;
    bench.enc_anchored = false;
    bench.enc_next = bench.now + 40u;
}

/* START on card @p card held until the run starts, at most 3s, then let
 * go: the run's first frame is now. */
static void hold_until_running(int card)
{
    open_test();
    const int y = SV_START_Y(card);
    ev(START_X, y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 150; ++i) {
        if (servo_screen_testing() || servo_screen_limit_running()) {
            break;
        }
        bench.now += 20u;
        servo_screen_clock(bench.now);
        scr->tick(0.02f);
    }
    ev(START_X, y, TOUCH_EVENT_UP, 1);
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

/*
 * A tester's case: the AS5600 plugged into the bus during a run drops out
 * for a moment.  Pulled and back after 300ms on the panel's clock, the run
 * goes on and ends PASS; the report and the CSV name the gap.
 */
TEST_CASE(an_encoder_pulled_and_plugged_back_within_300ms_rides)
{
    bench_fresh();
    short_runs();
    enc_on();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    bench_frames(3000u);
    const uint32_t t = enc_pull();
    bench_frames(300u);
    CHECK(servo_screen_testing());
    enc_plug(t + 300u);
    bench_frames(2000u);
    CHECK(servo_screen_testing());
    test_run_out();
    CHECK_EQ(bench.disarms, 0u);
    CHECK(strstr(bench.report, "Result:         PASS") != NULL);
    CHECK(strstr(bench.report, "AS5600 gaps: 1, longest 300ms.") != NULL);
    CHECK(strstr(bench.csv_head, ";travel angle (ms);angle gap (ms);meter;")
          != NULL);
    servo_screen_test_files(21, true);
    open_test();
    char want[64];
    snprintf(want, sizeof(want), TR(SV_CARD_LAST),
             ui_servo_str(SERVO_STR_PASS), "ATEST021.CSV + .TXT");
    CHECK(card_line_reads(0, 1, want, UI_C_TEXT_DIM));
}

/* Pulled for good: 320ms on, at the first frame past 300ms, the run ends
 * as an error -- the output off, the bench to SAFE -- and the card names
 * the AS5600, in each language. */
TEST_CASE(an_encoder_pulled_for_good_ends_the_test_as_an_error)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        bench_fresh();
        short_runs();
        enc_on();
        hold_start(2.3f);
        CHECK(servo_screen_testing());
        bench_frames(3000u);
        CHECK(bench.out);
        (void)enc_pull();
        bench_frames(300u);
        CHECK(servo_screen_testing());
        bench_frames(20u);
        CHECK(!servo_screen_testing());
        bench_frames(1000u);
        CHECK(!bench.out);
        CHECK_EQ(bench.disarms, 1u);
        open_test();
        char want[96];
        snprintf(want, sizeof(want), TR(SV_CARD_LAST_WHY),
                 ui_servo_str(SERVO_STR_ABORTED),
                 ui_servo_str(SERVO_STR_AB_ENC_SILENT));
        CHECK(card_line_reads(0, 1, want, UI_C_WARN));
        CHECK(strstr(bench.report, ui_servo_str(SERVO_STR_AB_ENC_SILENT))
              != NULL);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A run started while the encoder does not answer starts in a gap of it:
 * no reading within 300ms ends it. */
TEST_CASE(a_test_started_with_the_encoder_not_answering_ends_after_300ms)
{
    bench_fresh();
    short_runs();
    enc_on();
    bench_frames(200u);
    (void)enc_pull();
    hold_until_running(0);
    CHECK(servo_screen_testing());
    bench_frames(300u);
    CHECK(servo_screen_testing());
    bench_frames(20u);
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK_EQ(bench.disarms, 1u);
    CHECK(strstr(bench.report, "AS5600 not answering") != NULL);
}

/*
 * The INA3221 feeds the stall stop: it has no allowance.  An encoder gap
 * open when the meter stops answering changes nothing of that -- the run
 * ends at once with the meter's reason, not after 300ms and not with the
 * encoder's.
 */
TEST_CASE(the_meter_lost_in_an_encoder_gap_ends_the_test_at_once)
{
    bench_on_ina();
    enc_on();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    bench_frames(3000u);
    (void)enc_pull();
    bench_frames(100u);
    CHECK(servo_screen_testing());
    bench.ina = false;
    servo_screen_source_dropped(SERVO_SOURCE_WHY_SILENT, meter_changes + 1u);
    meter(SERVO_SOURCE_PDMINI);
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK(!bench.out);
    CHECK_EQ(bench.disarms, 1u);
    CHECK(strstr(bench.report, "Result:         ABORTED - INA3221 not "
                               "answering") != NULL);
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

/* A limit run with the encoder in use: a gap of 300ms rides, one of 301ms
 * ends it as an error, the output off and the bench to SAFE. */
TEST_CASE(the_limit_run_ends_on_an_encoder_gap_of_301ms)
{
    for (uint32_t len = 300u; len <= 301u; ++len) {
        lim_bench();
        enc_on();
        bench_frames(200u);
        hold_start_on(1, 2.3f);
        CHECK(servo_screen_limit_running());
        bench_frames(2000u);
        const uint32_t t = enc_pull();
        bench_frames(300u);
        CHECK(servo_screen_limit_running());
        enc_plug(t + len);
        bench_frames(40u);
        CHECK_EQ(servo_screen_limit_running(), len <= SERVO_GAP_MAX_MS);
        if (len > SERVO_GAP_MAX_MS) {
            bench_frames(1000u);
            CHECK(!bench.out);
            CHECK_EQ(bench.disarms, 1u);
            CHECK(strstr(bench.report, "AS5600 not answering") != NULL);
            CHECK(strstr(bench.report, "AS5600 gaps: 1, longest 301ms.")
                  != NULL);
        }
    }
}

TEST_CASE(a_limit_run_started_with_the_encoder_not_answering_ends_after_300ms)
{
    lim_bench();
    enc_on();
    bench_frames(200u);
    (void)enc_pull();
    hold_until_running(1);
    CHECK(servo_screen_limit_running());
    bench_frames(300u);
    CHECK(servo_screen_limit_running());
    bench_frames(20u);
    CHECK(!servo_screen_limit_running());
    bench_frames(1000u);
    CHECK(strstr(bench.report, "AS5600 not answering") != NULL);
}

int main(void)
{
    RUN(an_encoder_pulled_and_plugged_back_within_300ms_rides);
    RUN(an_encoder_pulled_for_good_ends_the_test_as_an_error);
    RUN(a_test_started_with_the_encoder_not_answering_ends_after_300ms);
    RUN(the_meter_lost_in_an_encoder_gap_ends_the_test_at_once);
    RUN(the_limit_run_ends_on_an_encoder_gap_of_301ms);
    RUN(a_limit_run_started_with_the_encoder_not_answering_ends_after_300ms);
    return test_summary("servo_run_gaps");
}
