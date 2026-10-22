/**
 * @file test_servo_run.c
 * @brief The automatic test as the servo screen starts and reports it: the start
 * line, the caps, the refusal and the encoder's angle columns.
 *
 * Cases:
 * - an_encoder_the_coprocessor_does_not_hold_gives_the_run_no_angle_columns
 * - the_dut_page_sets_the_encoders_centre_from_the_live_count
 * - a_link_that_goes_down_takes_the_encoders_angle_with_it
 * - a_run_with_the_encoder_on_writes_the_angle_columns
 * - a_run_started_without_a_magnet_says_so_in_its_report
 * - a_run_with_the_encoder_off_writes_the_old_columns
 * - start_test_starts_a_second_run
 * - the_start_line_follows_the_report_being_taken
 * - arm_first_goes_once_the_bench_is_armed
 * - a_run_outside_the_caps_is_refused_before_the_warning
 * - the_result_names_the_report_only_when_it_was_written
 * - a_refusal_goes_with_the_edit_that_answers_it
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

/* START TEST twice without a fresh screen: the second hold starts a second
 * run, and the button is drawn as a full redraw would between them. */
TEST_CASE(start_test_starts_a_second_run)
{
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    settle_drawn();
    two_buffers();
    open_test();
    CHECK(both_whole());
    CHECK_EQ(fb[(START_Y + UI_BAND_H * 0) * W + START_X - 60],
             fb1[(START_Y + UI_BAND_H * 0) * W + START_X - 60]);
    close_settings();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK_EQ(bench.ends, 2u);
}

/* The page redraws START TEST and its line when the last report has been
 * taken: no stale LAST REPORT STILL WRITING, no button left disabled. */
TEST_CASE(the_start_line_follows_the_report_being_taken)
{
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    servo_screen_test_files(12, true);
    open_test();
    two_buffers();
    /* Frames that keep the report back from the card. */
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        for (uint32_t k = 0; k < 500u; k += 20u) {
            bench.now += 20u;
            servo_screen_clock(bench.now);
            supply_cmd_t c;
            while (supply_screen_poll_cmd(&c)) {
                bench.out = c.off ? false : (c.on ? true : bench.out);
            }
            supply_screen_set_output(bench.out);
            servo_cmd_t sc;
            while (servo_screen_take(&sc)) {
                if (sc.kind == SERVO_CMD_POSITION) {
                    bench.cmd = sc.value_us;
                }
            }
            const float amps = bench.out ? servo_sim_step(&bench.sv, bench.cmd, bench.now)
                                     : 0.0f;
            if ((int32_t)(bench.now - bench.next) >= 0) {
                bench.next += 100u;
                supply_state_t st;
                memset(&st, 0, sizeof(st));
                st.online = true;
                st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
                st.output = bench.out;
                st.set_v = supply_screen_set_v();
                st.v = bench.out ? st.set_v : 0.0f;
                st.i = amps;
                st.samples = ++bench.samples;
                st.taken_ms = bench.now;
                st.stamped = true;
                servo_screen_supply(&st);
            }
            scr->tick(0.02f);
        }
        scr->render(&cv, 0);
        scr->render(&cv1, 1);
    }
    CHECK(!servo_screen_testing());
    CHECK(both_whole());                         /* the report waits */
    bench_drain();                               /* taken */
    for (int i = 0; i < 4; ++i) {
        scr->tick(0.02f);
        scr->render((i & 1) ? &cv1 : &cv, i & 1);
    }
    CHECK(both_whole());
}

/* ARM FIRST goes once the bench is armed, and a page drawn then is the one
 * a full redraw draws. */
TEST_CASE(arm_first_goes_once_the_bench_is_armed)
{
    fresh();
    memset(&bench, 0, sizeof(bench));
    open_test();
    tap(START_X, START_Y);                       /* ARM FIRST */
    two_buffers();
    gfx_color_t *was = malloc(FB_BYTES);
    REQUIRE_ALLOC(was);
    memcpy(was, fb, FB_BYTES);
    servo_screen_set_armed(true);
    settle_drawn();                              /* ARM's flash is over */
    CHECK(both_whole());
    CHECK(memcmp(fb, was, FB_BYTES) != 0);
    free(was);
}

/* A run whose steps the caps do not reach is refused before any warning:
 * with VOLTAGE MAX 8.0 V and HV SERVO on, START TEST opens no HV warning
 * naming 8.0 V, and no run starts. */
TEST_CASE(a_run_outside_the_caps_is_refused_before_the_warning)
{
    bench_fresh();
    short_runs();
    settings_set(SET_SUPPLY_V_MAX, 8.0f);
    supply_screen_limits_changed();
    open_run_settings(0);
    tap(HV_ROW_X, HV_ROW_Y);                      /* HV SERVO on */
    CHECK(home_tag());                           /* back to the cards */
    tap(START_X, START_Y);
    scr->tick(0.02f);
    scr->render(&cv, 0);
    CHECK(fb[UI_MARGIN * W + UI_MARGIN] != ui_theme_color(UI_C_DANGER));
    close_settings();
    hold_start(2.3f);
    CHECK(!servo_screen_testing());

    /* The warning open at 21 V, and the cap brought under its top step
     * while it stands: the hold starts nothing. */
    bench_fresh();
    short_runs();
    open_run_settings(0);
    tap(HV_ROW_X, HV_ROW_Y);
    CHECK(home_tag());
    tap(START_X, START_Y);
    scr->tick(0.02f);
    scr->render(&cv, 0);
    CHECK_EQ(fb[UI_MARGIN * W + UI_MARGIN], ui_theme_color(UI_C_DANGER));
    gfx_color_t *named = malloc(FB_BYTES);
    REQUIRE_ALLOC(named);
    memcpy(named, fb, FB_BYTES);
    settings_set(SET_SUPPLY_V_MAX, 8.0f);
    supply_screen_limits_changed();
    scr->tick(0.02f);
    servo_invalidate();
    scr->render(&cv, 0);
    /* Still the run's 8.40 V, not a value snapped under the cap. */
    for (int y = 60; y < 100; ++y) {
        CHECK_EQ(memcmp(&fb[(size_t)y * W], &named[(size_t)y * W],
                        490 * sizeof(gfx_color_t)), 0);
    }
    free(named);
    hold_apply(2.3f);
    CHECK(!servo_screen_testing());
}

/* The left card names the report only when the card took it whole. */
TEST_CASE(the_result_names_the_report_only_when_it_was_written)
{
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    servo_screen_cancel_arm();
    scr->tick(0.02f);
    servo_screen_test_files(12, false);
    settle_drawn();
    two_buffers();
    CHECK(both_whole());
    gfx_color_t *without = malloc(FB_BYTES);
    REQUIRE_ALLOC(without);
    memcpy(without, fb, FB_BYTES);
    servo_screen_test_files(12, true);
    CHECK(both_whole());
    CHECK(memcmp(fb, without, FB_BYTES) != 0);
    free(without);
}

/* The overlay's pixels, the left card, against @p want's. */
static bool overlay_same(const gfx_color_t *want)
{
    for (int y = 6; y < 426; ++y) {
        if (memcmp(&fb[(size_t)y * W + 6], &want[(size_t)y * W + 6],
                   488 * sizeof(gfx_color_t)) != 0) {
            return false;
        }
    }
    return true;
}

/* A refusal goes with the change that answers it: NO STEP CHOSEN, then
 * STEP 4.8 V turned on; SUPPLY NOT ANSWERING, then the supply answering.
 * The page is then the one drawn with nothing refused. */
TEST_CASE(a_refusal_goes_with_the_edit_that_answers_it)
{
    gfx_color_t *want = malloc(FB_BYTES);
    REQUIRE_ALLOC(want);
    for (int k = 0; k < 2; ++k) {
        /* The page with nothing refused. */
        bench_fresh();
        settings_set(SET_SERVO_STEP_60, 0.0f);
        settings_set(SET_SERVO_BROWNOUT, 0.0f);
        open_test();
        scr->tick(0.02f);
        servo_invalidate();
        scr->render(&cv, 0);
        memcpy(want, fb, FB_BYTES);

        /* Refused, then answered. */
        bench_fresh();
        settings_set(SET_SERVO_STEP_60, 0.0f);
        settings_set(SET_SERVO_BROWNOUT, 0.0f);
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        st.set_v = supply_screen_set_v();
        if (k == 0) {
            settings_set(SET_SERVO_STEP_48, 0.0f);
        } else {
            st.samples = ++bench.samples;            /* not answering */
            servo_screen_supply(&st);
        }
        hold_start(2.3f);
        CHECK(!servo_screen_testing());
        if (k == 0) {
            settings_set(SET_SERVO_STEP_48, 1.0f);
        } else {
            st.online = true;
            st.samples = ++bench.samples;
            servo_screen_supply(&st);
        }
        for (int i = 0; i < 10; ++i) {
            scr->tick(0.02f);
        }
        servo_invalidate();
        scr->render(&cv, 0);
        CHECK(overlay_same(want));
    }
    free(want);
}

/* ENC CENTRE on SETTINGS' DUT page, row 2: the AS5600 is switched on
 * SETUP > INTERFACES, its one home, and the DUT page holds no AS5600 row. */
TEST_CASE(the_dut_page_sets_the_encoders_centre_from_the_live_count)
{
    fresh();
    open_settings();
    tap(TAB_X(PAGE_DUT), TAB_Y);               /* DUT */
    /* Where AS5600 was, nothing: the encoder stays off. */
    tap(ROW_L_X, ROW_Y(3));
    CHECK(!settings_get_bool(SET_ENC_EN));
    settings_set(SET_ENC_EN, 1.0f);            /* SETUP > INTERFACES */

    /* No reading yet: the tap sets nothing. */
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 0);
    tap(ROW_L_X, ROW_Y(2));                    /* ENC CENTRE */
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 0);

    /* A live count is the centre. */
    const servo_test_enc_t e = { true, 2871u, 0u, 5000u, false, false, false, false };
    servo_screen_encoder(&e);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 2871);

    /* A sensor that answers and sees no magnet has no live count. */
    const servo_test_enc_t dark = { .valid = false, .raw = 777u,
                                    .taken_ms = 5020u, .no_magnet = true };
    servo_screen_encoder(&dark);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 2871);

    /* A reading that has gone sets nothing; with the encoder off in SETUP
     * the row is faint and sets nothing either. */
    const servo_test_enc_t e2 = { true, 100u, 0u, 5040u, false, false, false, false };
    servo_screen_encoder(&e2);
    settings_set(SET_ENC_EN, 0.0f);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 2871);
}

TEST_CASE(a_link_that_goes_down_takes_the_encoders_angle_with_it)
{
    fresh();
    open_settings();
    tap(TAB_X(PAGE_DUT), TAB_Y);               /* DUT */
    settings_set(SET_ENC_EN, 1.0f);            /* SETUP > INTERFACES */
    settings_set(SET_ENC_CENTRE, 1000.0f);
    const servo_test_enc_t e = { true, 2871u, 0u, 5000u, false, false, false, false };
    servo_screen_encoder(&e);

    /* The link drops: ENC CENTRE takes nothing from the last count. */
    servo_screen_set_link(false);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 1000);

    /* A reading queued before the drop and drained after it is not used
     * either. */
    servo_screen_encoder(&e);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 1000);

    /* The link back and no reading yet: still nothing; a reading that
     * arrives with the link is the live count. */
    servo_screen_set_link(true);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 1000);
    const servo_test_enc_t f = { true, 1500u, 0u, 6000u, false, false, false, false };
    servo_screen_encoder(&f);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 1500);
}

TEST_CASE(a_run_with_the_encoder_on_writes_the_angle_columns)
{
    bench_fresh();
    short_runs();
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_screen_set_enc_held(true);
    bench.enc = true;
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 240 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK(strstr(bench.csv_head, ";travel (ms);angle (deg);travel angle (ms)") != NULL);
    CHECK(strstr(bench.report, "ENCODER (angles in degrees from the centre count") != NULL);
    CHECK(strstr(bench.report, "centre count 3000,") != NULL);
    CHECK(strstr(bench.report, "Commanded: ") != NULL);
    CHECK(bench.ends >= 1u);
}

/* "No magnet" reaches the screen once, when it begins.  A run started
 * after that is told the state it starts in, and its report gives the
 * reason for its missing angles. */
TEST_CASE(a_run_started_without_a_magnet_says_so_in_its_report)
{
    bench_fresh();
    short_runs();
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_screen_set_enc_held(true);
    const servo_test_enc_t dark = { .valid = false, .no_magnet = true };
    servo_screen_encoder(&dark);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 240 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK(strstr(bench.report, "No angle reading reached the run.") != NULL);
    CHECK(strstr(bench.report, "No magnet: the AS5600 reported none 1 time(s).")
          != NULL);

    /* A sensor that stopped answering before the run is not "no magnet". */
    bench_fresh();
    short_runs();
    settings_set(SET_ENC_EN, 1.0f);
    servo_screen_set_enc_held(true);
    servo_screen_encoder(&dark);
    const servo_test_enc_t gone = { .valid = false };
    servo_screen_encoder(&gone);
    hold_start(2.3f);
    for (int i = 0; i < 240 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    bench_frames(1000u);
    CHECK(strstr(bench.report, "No angle reading reached the run.") != NULL);
    CHECK(strstr(bench.report, "No magnet:") == NULL);
}

TEST_CASE(a_run_with_the_encoder_off_writes_the_old_columns)
{
    bench_fresh();
    short_runs();
    bench.enc = true;                               /* readings, AS5600 off */
    hold_start(2.3f);
    for (int i = 0; i < 240 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    bench_frames(1000u);
    CHECK(strstr(bench.csv_head, "angle") == NULL);
    CHECK(strstr(bench.report, "ENCODER") == NULL);
}

/* The SENSE set-up is written only while the bank is disarmed: an AS5600
 * switched on while armed is not in the IOmcu, and a run started then
 * has no angles to log. */
TEST_CASE(an_encoder_the_coprocessor_does_not_hold_gives_the_run_no_angle_columns)
{
    bench_fresh();
    short_runs();
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_screen_set_enc_held(false);
    bench.enc = true;
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 240 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    bench_frames(1000u);
    CHECK(strstr(bench.csv_head, "angle") == NULL);
    CHECK(strstr(bench.report, "ENCODER") == NULL);
}

int main(void)
{
    RUN(an_encoder_the_coprocessor_does_not_hold_gives_the_run_no_angle_columns);
    RUN(the_dut_page_sets_the_encoders_centre_from_the_live_count);
    RUN(a_link_that_goes_down_takes_the_encoders_angle_with_it);
    RUN(a_run_with_the_encoder_on_writes_the_angle_columns);
    RUN(a_run_started_without_a_magnet_says_so_in_its_report);
    RUN(a_run_with_the_encoder_off_writes_the_old_columns);
    RUN(start_test_starts_a_second_run);
    RUN(the_start_line_follows_the_report_being_taken);
    RUN(arm_first_goes_once_the_bench_is_armed);
    RUN(a_run_outside_the_caps_is_refused_before_the_warning);
    RUN(the_result_names_the_report_only_when_it_was_written);
    RUN(a_refusal_goes_with_the_edit_that_answers_it);
    return test_summary("servo_run");
}
