/*
 * RUDDER AUTO's HOLD 2s: ARM + START on the SERVO screen and the
 * programmer (servo_rudder_auto_panel.c, servo_ready.c,
 * servo_start_seq.c): on the rbmod carrier one hold switches PORT PWM and
 * PROG 5V, reads them back, arms and starts; the read-back's 1000ms and
 * ARMED's 500ms on the screen, each failure with every output off and its
 * step named; the PD mini switched on where it feeds the servo; a tap
 * that stops the sequence; READY's rows where the rig's bench gives them
 * a value or a fix.  The rig is servo_rauto_kst_rig.h.
 *
 * Cases:
 * - on_the_carrier_one_hold_switches_port_pwm_and_5v_arms_and_starts
 * - the_5v_read_back_has_1000ms_and_a_late_one_ends_all_off
 * - armed_has_500ms_and_a_refused_arm_ends_all_off
 * - a_pd_mini_off_is_switched_on_by_the_hold
 * - a_tap_on_the_hold_during_the_sequence_ends_it_all_off
 * - a_finger_on_arm_start_locks_port_target_and_pwm
 * - settings_edited_after_entry_are_the_ones_the_run_takes
 * - a_second_finger_opens_nothing_under_arm_start
 * - a_row_during_the_steps_opens_no_keypad_and_the_hold_still_stops
 * - the_run_takes_the_settings_the_hold_fired_on
 * - a_window_measured_before_the_5v_refuses_nothing
 * - a_rail_dead_after_the_5v_fails_naming_prog_5v
 * - a_pd_mini_off_on_a_rail_fed_from_elsewhere_refuses_the_hold
 * - a_pd_mini_set_low_or_past_6v_runs_at_5v_and_stays_there
 * - a_pd_mini_that_does_not_answer_5v_fails_the_read_back
 * - a_pd_mini_capped_under_5v_refuses_the_hold
 * - a_tap_on_a_red_zero_row_takes_the_zero_while_the_encoder_reads
 * - a_tap_on_a_met_row_opens_and_names_nothing
 * - port_pwm_on_with_the_5v_off_reads_amber_with_no_text_of_its_own
 * - a_run_under_way_keeps_the_angle_row_on_its_source_without_a_reading
 * - a_latched_failsafe_is_cleared_and_the_hold_reaches_the_run
 * - a_read_that_finds_no_servo_ends_with_the_pd_mini_off
 * - the_arm_row_reads_safe_while_the_read_session_holds_the_bench_safe
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_kst_rig.h"
#include "ui_setlist.h"

/* The owner's case on the rbmod carrier: SAFE, PORT PWM off, PROG 5V off.
 * READY reads PROG 5V and PORT PWM START SWITCHES IT ON and ARM START
 * ARMS in amber, and the hold is offered.  One hold: PORT PWM on and
 * saved, PROG 5V ON asked and read back, the pulse in SAFE, ARM, ARMED,
 * the run.  STOP RUN ends it with the bench SAFE and the PROG 5V off. */
TEST_CASE(on_the_carrier_one_hold_switches_port_pwm_and_5v_arms_and_starts)
{
    carrier_safe_off(40u);
    CHECK(rauto_hold_blocked(NULL) == NULL);
    CHECK_STR_EQ(rauto_blocked(), ui_servo_str(SERVO_STR_START_PROG_5V));
    rdy_row_t r = row_now(RAR_SUPPLY);
    CHECK_STR_EQ(r.label, "PROG 5V");
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(RDY_SEQ_ON));
    r = row_now(RAR_PWM);
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    r = row_now(RAR_ARM);
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(RDY_SEQ_ARM));
    CHECK_EQ(row_now(RAR_PORT).st, (uint8_t)RDY_OK);
    CHECK_EQ(row_now(RAR_ZERO).st, (uint8_t)RDY_OK);
    (void)settings_save();
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    CHECK(settings_save_asked());
    CHECK(prog5v_box_on());
    CHECK_EQ(ra.p5_ons, 1u);
    CHECK_EQ(ra.arms, 1u);
    CHECK(svs_seq_failure() == NULL);
    CHECK_EQ(row_now(RAR_SUPPLY).st, (uint8_t)RDY_LIVE);
    CHECK_EQ(row_now(RAR_ARM).st, (uint8_t)RDY_LIVE);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_rauto_running());
    ra_ms(2000u);
    CHECK(!ra.armed);
    CHECK(!prog5v_box_on());
    CHECK(ra.p5_offs >= 1u);
    ra.p5_task = false;
    carrier_done();
}

/* The PROG 5V's read-back has 1000ms, the port's bit and a window begun
 * after both inside it: read back at 800ms the run starts; at 1100ms the
 * sequence fails as READ-BACK names PROG 5V, in the first line and in the
 * alert band, with no ARM asked, the bench SAFE and the PROG 5V off. */
TEST_CASE(the_5v_read_back_has_1000ms_and_a_late_one_ends_all_off)
{
    char b[128], want[128], cause[64];
    for (int late = 0; late < 2; ++late) {
        /* The ON is taken on the frame after the hold fires: 20ms of it
         * come off the delay. */
        carrier_safe_off(late ? 1100u - 20u : 800u - 20u);
        ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
        ra_ms(2100u);
        ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
        CHECK(svs_seq_busy());
        CHECK_EQ(ra.arms, 0u);
        ra_ms(1200u);
        if (!late) {
            CHECK(servo_screen_rauto_running());
            CHECK_EQ(ra.arms, 1u);
            rauto_end_now(SERVO_TEST_AB_OPERATOR);
        } else {
            CHECK(!servo_screen_rauto_running());
            CHECK(!svs_seq_busy());
            CHECK_EQ(ra.arms, 0u);
            snprintf(cause, sizeof(cause), TR(SQ_NO_READBACK), 1000u);
            snprintf(want, sizeof(want), TR(SQ_FAILED), "PROG 5V", cause);
            CHECK_STR_EQ(svs_seq_failure(), want);
            CHECK_STR_EQ(rauto_line(0, b, sizeof(b)), want);
            CHECK_STR_EQ(ui_router_alert(), want);
            CHECK(ra.p5_offs >= 1u);
            CHECK(!prog5v_box_on());
            CHECK(!ra.armed);
        }
        ra_ms(2000u);
        ra.p5_task = false;
        carrier_done();
    }
}

/* ARMED has 500ms: an ARM the bench drops fails the sequence at ARM, the
 * PD mini's output and every other output off, the bench SAFE. */
TEST_CASE(armed_has_500ms_and_a_refused_arm_ends_all_off)
{
    char want[128], cause[64];
    ra_setup(true, true);
    ra.armed = false;
    servo_screen_set_armed(false);
    ra.no_arm = true;
    ra_ms(100u);
    CHECK(rauto_hold_blocked(NULL) == NULL);
    const unsigned offs = ra.offs;
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(2060u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    CHECK(svs_seq_busy());
    ra_ms(400u);
    CHECK(svs_seq_busy());
    ra_ms(200u);
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_rauto_running());
    snprintf(cause, sizeof(cause), TR(SQ_NOT_ARMED), 500u);
    snprintf(want, sizeof(want), TR(SQ_FAILED), "ARM", cause);
    CHECK_STR_EQ(svs_seq_failure(), want);
    ra_ms(100u);
    CHECK(ra.offs > offs);
    CHECK(!g_rig.sup_on);
    CHECK(ra.disarms >= 1u);
    ra.no_arm = false;
}

/* Where the PD mini feeds the servo with its output off, PD MINI reads
 * START SWITCHES IT ON and the hold switches it on before ARM. */
TEST_CASE(a_pd_mini_off_is_switched_on_by_the_hold)
{
    ra_setup(true, true);
    ra.armed = false;
    servo_screen_set_armed(false);
    g_rig.sup_on = false;
    ra_pd_reading(false);
    ra_ms(100u);
    rdy_row_t r = row_now(RAR_SUPPLY);
    CHECK_STR_EQ(r.label, "PD MINI");
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK(rauto_hold_blocked(NULL) == NULL);
    ra_hold_start();
    CHECK_EQ(ra.pd_ons, 1u);
    CHECK(servo_screen_rauto_running());
    rauto_end_now(SERVO_TEST_AB_OPERATOR);
    ra_ms(2000u);
    CHECK(!g_rig.sup_on);
}

/* A tap on the hold while the sequence waits for its read-back: the
 * sequence ends as STOP, every output off. */
TEST_CASE(a_tap_on_the_hold_during_the_sequence_ends_it_all_off)
{
    carrier_safe_off(800u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(2100u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    CHECK(svs_seq_busy());
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 2);
    CHECK(!svs_seq_busy());
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 2);
    char want[128];
    snprintf(want, sizeof(want), TR(SQ_FAILED), "PROG 5V", "STOP");
    CHECK_STR_EQ(svs_seq_failure(), want);
    ra_ms(1500u);
    CHECK(!servo_screen_rauto_running());
    CHECK(!prog5v_box_on());
    CHECK_EQ(ra.arms, 0u);
    ra.p5_task = false;
    carrier_done();
}

/* A second finger while ARM + START is held, and while its sequence runs:
 * PORT opens nothing, TARGET and PORT PWM read locked, so the hold arms
 * the output shown when it began.  With the finger lifted, or the sequence
 * ended, PORT opens again.  An open PORT list takes no START press. */
TEST_CASE(a_finger_on_arm_start_locks_port_target_and_pwm)
{
    carrier_safe_off(600u);
    const int all = 1 + (int)LINK_PG_PORTS_MAX;
    CHECK(!servo_screen_arm_held());
    CHECK(!servo_screen_port_pwm_locked());
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(500u);
    CHECK(servo_screen_arm_held());
    CHECK(prog_choice_locked());
    CHECK(servo_screen_target_locked());
    CHECK(servo_screen_port_pwm_locked());
    ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_UP, 2);
    CHECK_EQ(servo_screen_port_choices(), -1);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    ra_ms(100u);
    CHECK(!servo_screen_arm_held());
    CHECK(!servo_screen_port_pwm_locked());
    ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_UP, 2);
    CHECK_EQ(servo_screen_port_choices(), all);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(500u);
    CHECK(!servo_screen_arm_held());
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);

    ra.p5_task = false;
    carrier_done();
    carrier_safe_off(600u);
    CHECK_EQ(servo_screen_port_choices(), -1);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(2100u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    CHECK(svs_seq_busy());
    CHECK(servo_screen_arm_held());
    CHECK(servo_screen_target_locked());
    ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_UP, 2);
    CHECK_EQ(servo_screen_port_choices(), -1);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1 + (int)RIG_PORT);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 3);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 3);
    ra_ms(1500u);
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_arm_held());
    ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_UP, 2);
    CHECK_EQ(servo_screen_port_choices(), all);
    ra.p5_task = false;
    carrier_done();
}

/* The settings the start reads are read when the hold fires, not when the
 * screen opened: targets and margin set on the screen shown are the
 * report's, and PORT PWM switched off on it is what the sequence finds. */
TEST_CASE(settings_edited_after_entry_are_the_ones_the_run_takes)
{
    char want[128];
    carrier_safe_off(40u);
    settings_set(SET_RUDDER_MARGIN, 0.0f);
    settings_set(SET_RUDDER_DEFL_LO, 12.5f);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    ra_ms(2000u);
    CHECK(!servo_screen_rauto_running());
    snprintf(want, sizeof(want), ui_servo_str(SERVO_STR_RA_R_TARGETS), 12.5,
             20.0, 0u);
    CHECK(strstr(ra.report, want) != NULL);
    settings_set(SET_RUDDER_MARGIN, 10.0f);
    settings_set(SET_RUDDER_DEFL_LO, 20.0f);
    ra.p5_task = false;
    carrier_done();
}

/* A second finger on SETTINGS or on a READY row while ARM + START's 2s
 * hold fills: nothing opens and the card takes no tap, so the hold's own release still ends it, and
 * a finger lifted at 1s switches nothing, arms nothing and starts
 * nothing. */
TEST_CASE(a_second_finger_opens_nothing_under_arm_start)
{
    carrier_safe_off(40u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(500u);
    CHECK(servo_screen_arm_held());
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_UP, 2);
    CHECK(!servo_screen_list()->open);
    ui_router_set_alert(NULL);
    for (int r = 0; r < (int)RAR_COUNT; ++r) {
        ev(SV_RDY_X, SV_RDY_ROW_Y(r), TOUCH_EVENT_DOWN, 2);
        ev(SV_RDY_X, SV_RDY_ROW_Y(r), TOUCH_EVENT_UP, 2);
        CHECK(!servo_screen_list()->open);
        CHECK_EQ(servo_screen_port_choices(), -1);
        CHECK(servo_screen_arm_held());
        CHECK(ui_router_alert() == NULL);   /* the card took no tap */
    }
    ra_ms(500u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    ra_ms(2000u);
    CHECK(!servo_screen_arm_held());
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_rauto_running());
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    CHECK(!prog5v_box_on());
    CHECK_EQ(ra.arms, 0u);
    ra.p5_task = false;
    carrier_done();
}

/* During the steps a tap on MARGIN opens no keypad, and a press on the
 * hold still ends them: no ARM, no run, every output off. */
TEST_CASE(a_row_during_the_steps_opens_no_keypad_and_the_hold_still_stops)
{
    carrier_safe_off(600u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(2100u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    CHECK(svs_seq_busy());
    ev(SV_RA_L_X, SV_RA_ROW_Y(1), TOUCH_EVENT_DOWN, 2);
    ev(SV_RA_L_X, SV_RA_ROW_Y(1), TOUCH_EVENT_UP, 2);
    ra_ms(20u);
    CHECK(!servo_screen_keypad()->open);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 3);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 3);
    CHECK(!svs_seq_busy());
    ra_ms(1500u);
    CHECK(!servo_screen_rauto_running());
    CHECK_EQ(ra.arms, 0u);
    CHECK(!prog5v_box_on());
    ra.p5_task = false;
    carrier_done();
}

/* A setting changed after the hold fired is not the run's: MARGIN set to
 * 25% during the steps, the report gives the 0% the hold fired on. */
TEST_CASE(the_run_takes_the_settings_the_hold_fired_on)
{
    char want[128];
    carrier_safe_off(300u);
    settings_set(SET_RUDDER_MARGIN, 0.0f);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(2100u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    CHECK(svs_seq_busy());
    settings_set(SET_RUDDER_MARGIN, 25.0f);
    ra_ms(1000u);
    CHECK(servo_screen_rauto_running());
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    ra_ms(2000u);
    snprintf(want, sizeof(want), ui_servo_str(SERVO_STR_RA_R_TARGETS), 20.0,
             20.0, 0u);
    CHECK(strstr(ra.report, want) != NULL);
    settings_set(SET_RUDDER_MARGIN, 10.0f);
    ra.p5_task = false;
    carrier_done();
}

/* The window under way when PROG 5V reads back ON was measured with the
 * rail off (0V for its first part): the steps wait for one begun after
 * the 5V, which reads 6.0V, and the run starts with no failure. */
TEST_CASE(a_window_measured_before_the_5v_refuses_nothing)
{
    for (uint32_t delay = 20u; delay <= 100u; delay += 10u) {
        carrier_safe_off(delay);
        ra_hold_start();
        CHECK(svs_seq_failure() == NULL);
        CHECK(servo_screen_rauto_running());
        CHECK_EQ(ra.arms, 1u);
        rauto_end_now(SERVO_TEST_AB_OPERATOR);
        ra_ms(2000u);
        ra.p5_task = false;
        carrier_done();
    }
}

/* PROG 5V reads back ON and the rail stays at 0V: the window begun after
 * it refuses, and the step that failed is PROG 5V's, its rail's line in
 * the band, all off, no ARM. */
TEST_CASE(a_rail_dead_after_the_5v_fails_naming_prog_5v)
{
    char want[128];
    carrier_safe_off(40u);
    ra.rail_dead = true;
    ra_hold_start();
    CHECK(!servo_screen_rauto_running());
    CHECK_EQ(ra.arms, 0u);
    snprintf(want, sizeof(want), TR(SQ_FAILED), "PROG 5V",
             TR(SV_LIM_PROG_RAIL));
    CHECK_STR_EQ(svs_seq_failure(), want);
    ra_ms(500u);
    CHECK(!prog5v_box_on());
    ra.rail_dead = false;
    ra.p5_task = false;
    carrier_done();
}

/* The PD mini answers with its output off while the INA3221 reads the
 * rail at 6.0V: fed from elsewhere, PD MINI's row is red with the PD-off
 * text, and the hold refuses, switching nothing.  With the rail dead the
 * same row is amber and the hold is offered. */
TEST_CASE(a_pd_mini_off_on_a_rail_fed_from_elsewhere_refuses_the_hold)
{
    ra_setup(true, true);
    ra.armed = false;
    servo_screen_set_armed(false);
    g_rig.sup_on = false;
    ra_pd_reading(false);
    ra.rail_fed = true;
    ra_ms(200u);
    rdy_row_t r = row_now(RAR_SUPPLY);
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK_STR_EQ(r.why, TR(SV_LIM_PROG_PD_OFF));
    rauto_row_t row = RAR_COUNT;
    CHECK_STR_EQ(rauto_hold_blocked(&row), TR(SV_LIM_PROG_PD_OFF));
    CHECK_EQ(row, RAR_SUPPLY);
    ra_hold_start();
    CHECK(!svs_seq_busy());
    CHECK_EQ(ra.pd_ons, 0u);
    CHECK_EQ(ra.arms, 0u);
    ra.rail_fed = false;
    ra_ms(200u);
    CHECK_EQ(row_now(RAR_SUPPLY).st, (uint8_t)RDY_SEQ);
    CHECK(rauto_hold_blocked(NULL) == NULL);
}

/* The PD mini off, set to @p v: its row is amber, the hold offered; START
 * sets it to 5.0V before its ON, the run's sessions ask 5000mV, and after
 * the run it stays at 5.0V.  A set point of 4.0V and one past a standard
 * servo's 6.0V take the same way. */
static void pd_set_to_5v_by_the_hold(float v)
{
    ra_setup(true, true);
    ra.armed = false;
    servo_screen_set_armed(false);
    settings_set(SET_SUPPLY_V_START, v);
    supply_screen_settings_loaded();
    CHECK(fabsf(supply_screen_set_v() - v) < 0.01f);
    g_rig.sup_on = false;
    ra_pd_reading(false);
    ra_ms(200u);
    rdy_row_t r = row_now(RAR_SUPPLY);
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK(rauto_hold_blocked(NULL) == NULL);
    ra_hold_start();
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 0.005f);
    CHECK_EQ(ra.pd_ons, 1u);
    CHECK(servo_screen_rauto_running());
    CHECK_STR_EQ(row_now(RAR_SUPPLY).value, "5.0V");
    rauto_end_now(SERVO_TEST_AB_OPERATOR);
    ra_ms(2000u);
    CHECK(!servo_screen_rauto_running());
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 0.005f);
    settings_set(SET_SUPPLY_V_START, settings_def(SET_SUPPLY_V_START)->def);
    supply_screen_settings_loaded();
}

TEST_CASE(a_pd_mini_set_low_or_past_6v_runs_at_5v_and_stays_there)
{
    pd_set_to_5v_by_the_hold(4.0f);
    pd_set_to_5v_by_the_hold(8.4f);

    /* The output already on at 8.4V: the row is met, START lowers it to
     * 5.0V with the output on, and the read-back waits for a reading at
     * 5.0V.  The output never goes off on the way. */
    ra_setup(true, true);
    ra.armed = false;
    servo_screen_set_armed(false);
    settings_set(SET_SUPPLY_V_START, 8.4f);
    supply_screen_settings_loaded();
    ra_pd_reading(true);
    ra_ms(200u);
    CHECK(rauto_hold_blocked(NULL) == NULL);
    ra_hold_start();
    CHECK(ra.pd_ons <= 1u);
    CHECK_EQ(ra.offs, 0u);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 0.005f);
    CHECK(fabsf(ra.pd_sent_v - 5.0f) < 0.005f);
    CHECK(servo_screen_rauto_running());
    rauto_end_now(SERVO_TEST_AB_OPERATOR);
    ra_ms(2000u);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 0.005f);
    settings_set(SET_SUPPLY_V_START, settings_def(SET_SUPPLY_V_START)->def);
    supply_screen_settings_loaded();
}

/* The PD mini on at 8.4V answers no new set point: its readings hold 8.4V
 * after START asked 5.0V.  The read-back fails at 1000ms naming the PD
 * mini, the run does not start, and every output goes off. */
TEST_CASE(a_pd_mini_that_does_not_answer_5v_fails_the_read_back)
{
    char b[96];
    ra_setup(true, true);
    ra.armed = false;
    servo_screen_set_armed(false);
    settings_set(SET_SUPPLY_V_START, 8.4f);
    supply_screen_settings_loaded();
    ra_pd_reading(true);
    ra_ms(200u);
    ra.pd_deaf = true;
    CHECK(rauto_hold_blocked(NULL) == NULL);
    ra_hold_start();
    ra_ms(1500u);
    CHECK(!servo_screen_rauto_running());
    CHECK(!svs_seq_busy());
    CHECK(fabsf(ra.pd_sent_v - 8.4f) < 0.005f);
    CHECK_STR_EQ(rauto_line(0, b, sizeof(b)),
                 "PD MINI FAILED: NO READ-BACK IN 1000ms");
    CHECK(!g_rig.sup_on);
    CHECK(!ra.armed);
    settings_set(SET_SUPPLY_V_START, settings_def(SET_SUPPLY_V_START)->def);
    supply_screen_settings_loaded();
}

/* SUPPLY's LIMITS cap the voltage at 4.5V: PD MINI's row is red with
 * its text, the hold refuses and switches nothing, the set point stays,
 * and a tap on the row opens SUPPLY's LIMITS. */
TEST_CASE(a_pd_mini_capped_under_5v_refuses_the_hold)
{
    char want[128], got[128];
    ra_setup(true, true);
    ra.armed = false;
    servo_screen_set_armed(false);
    settings_set(SET_SUPPLY_V_MAX, 4.5f);
    supply_screen_limits_changed();
    g_rig.sup_on = false;
    ra_pd_reading(false);
    ra_ms(200u);
    const float before = supply_screen_set_v();
    rdy_row_t r = row_now(RAR_SUPPLY);
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK_STR_EQ(r.why, TR(SV_RUN_PD_5V));
    CHECK(r.fix);
    CHECK_STR_EQ(rauto_hold_blocked(NULL), TR(SV_RUN_PD_5V));
    snprintf(want, sizeof(want), TR(RDY_BAND), "PD MINI", TR(SV_RUN_PD_5V));
    CHECK_STR_EQ(ready_refusal(got, sizeof(got)), want);
    ra_hold_start();
    CHECK(!svs_seq_busy());
    CHECK_EQ(ra.pd_ons, 0u);
    CHECK(!servo_screen_rauto_running());
    CHECK(fabsf(supply_screen_set_v() - before) < 0.005f);
    const ui_screen_id_t was = ui_router_current();
    ev(SV_RDY_X, SV_RDY_ROW_Y(RAR_SUPPLY), TOUCH_EVENT_DOWN, 1);
    ev(SV_RDY_X, SV_RDY_ROW_Y(RAR_SUPPLY), TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_SUPPLY);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_EQ(supply_screen_settings_tab(), 0);
    ui_router_goto(was);
    settings_set(SET_SUPPLY_V_MAX, settings_def(SET_SUPPLY_V_MAX)->def);
    supply_screen_limits_changed();
}

/* ZERO is red until taken; with the encoder reading, a tap on its row
 * takes ZERO as RUDDER's button does, and the row reads SET in green. */
TEST_CASE(a_tap_on_a_red_zero_row_takes_the_zero_while_the_encoder_reads)
{
    ra_setup(false, false);
    ra.armed = false;
    servo_screen_set_armed(false);
    ra_ms(200u);
    rdy_row_t r = row_now(RAR_ZERO);
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK_STR_EQ(r.why, TR(RA_NO_ZERO));
    CHECK(r.fix);
    ui_router_set_alert(NULL);
    ev(SV_RDY_X, SV_RDY_ROW_Y(RAR_ZERO), TOUCH_EVENT_DOWN, 1);
    ev(SV_RDY_X, SV_RDY_ROW_Y(RAR_ZERO), TOUCH_EVENT_UP, 1);
    ra_ms(100u);
    r = row_now(RAR_ZERO);
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    CHECK_STR_EQ(r.value, TR(RDY_SET));
    CHECK(!r.fix);
    CHECK(ui_router_alert() == NULL);
}

/* A met row has no fix and no text: a tap on METER, ANGLE, ZERO, RANGE,
 * PROGRAMMER, SUPPLY, PORT PWM or ARM opens no list and no keypad, names
 * nothing in the band and changes no row. */
TEST_CASE(a_tap_on_a_met_row_opens_and_names_nothing)
{
    static const rauto_row_t k_rows[] = {
        RAR_METER, RAR_ANGLE, RAR_ZERO, RAR_RANGE, RAR_PROG, RAR_SUPPLY,
        RAR_PWM, RAR_ARM,
    };
    ra_setup(true, true);
    ra_ms(200u);
    for (size_t k = 0; k < sizeof(k_rows) / sizeof(k_rows[0]); ++k) {
        const rdy_row_t before = row_now((int)k_rows[k]);
        CHECK(before.st == (uint8_t)RDY_OK || before.st == (uint8_t)RDY_LIVE);
        CHECK(before.why == NULL);
        ui_router_set_alert(NULL);
        ev(SV_RDY_X, SV_RDY_ROW_Y(k_rows[k]), TOUCH_EVENT_DOWN, 1);
        ev(SV_RDY_X, SV_RDY_ROW_Y(k_rows[k]), TOUCH_EVENT_UP, 1);
        CHECK(ui_router_alert() == NULL);
        CHECK(!servo_screen_list()->open);
        CHECK(!servo_screen_keypad()->open);
        CHECK_EQ(servo_screen_port_choices(), -1);
        const rdy_row_t after = row_now((int)k_rows[k]);
        CHECK_EQ(after.st, before.st);
        CHECK_STR_EQ(after.value, before.value);
    }
}

/* On the carrier, SAFE, PORT PWM on and PROG 5V off: the pulse comes with
 * the 5V the hold switches, so PORT PWM reads START SWITCHES IT ON in
 * amber with no text of its own, and PROG 5V's row carries the gate's. */
TEST_CASE(port_pwm_on_with_the_5v_off_reads_amber_with_no_text_of_its_own)
{
    carrier_safe_off(40u);
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
    ra_ms(100u);
    rdy_row_t r = row_now(RAR_PWM);
    CHECK_STR_EQ(r.label, TR(RDY_PWM));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(RDY_SEQ_ON));
    CHECK(r.why == NULL);
    CHECK(!r.fix);
    r = row_now(RAR_SUPPLY);
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.why, ui_servo_str(SERVO_STR_START_PROG_5V));
    CHECK(rauto_hold_blocked(NULL) == NULL);
    ra.p5_task = false;
    carrier_done();
}

/* A reading that goes while the run is under way: the ANGLE row stays
 * green on the source's name, with no degrees after it. */
TEST_CASE(a_run_under_way_keeps_the_angle_row_on_its_source_without_a_reading)
{
    carrier_safe_off(40u);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    rdy_row_t r = row_now(RAR_ANGLE);
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    CHECK(strstr(r.value, "AS5600 ") == r.value);
    CHECK(strstr(r.value, "\xC2\xB0") != NULL);
    const servo_test_enc_t none = { .valid = false, .taken_ms = g_rig.ms };
    servo_screen_encoder(&none);
    CHECK(servo_screen_rauto_running());
    r = row_now(RAR_ANGLE);
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    CHECK_STR_EQ(r.value, "AS5600");
    rauto_end_now(SERVO_TEST_AB_OPERATOR);
    ra_ms(2000u);
    ra.p5_task = false;
    carrier_done();
}

/* The station's case on the rbmod carrier: the IOmcu's failsafe latched
 * (FAULT 01) after a panel-only restart, SAFE, PORT PWM and PROG 5V off.
 * One hold: CLEAR, then PORT PWM, PROG 5V ON taken and read back, ARMED
 * and the run.  The 5V's ON is never asked in the failsafe.  With an IOmcu
 * that does not take the CLEAR the steps fail at FAILSAFE CLEAR instead,
 * the 5V never asked, and the first line and the band name it. */
TEST_CASE(a_latched_failsafe_is_cleared_and_the_hold_reaches_the_run)
{
    char b[128], want[128], cause[64];
    for (int dead = 0; dead < 2; ++dead) {
        carrier_safe_off(40u);
        ra.latched = true;
        ra.clear_dead = dead != 0;
        ra_faults();
        ra_ms(100u);
        CHECK(rauto_hold_blocked(NULL) == NULL);
        ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
        for (int i = 0; i < 150 && !svs_seq_busy(); ++i) {
            ra_ms(20u);
        }
        CHECK(svs_seq_busy());
        ra_ms(460u);
        CHECK(dead == 0 || svs_seq_busy());   /* 500ms for the CLEAR */
        ra_ms(140u);
        ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
        ra_ms(20u);
        CHECK_EQ(ra.clears, 1u);
        CHECK_EQ(ra.refused, 0u);
        if (!dead) {
            CHECK(servo_screen_rauto_running());
            CHECK(prog5v_box_on());
            CHECK_EQ(ra.p5_ons, 1u);
            CHECK_EQ(ra.arms, 1u);
            CHECK(svs_seq_failure() == NULL);
            rauto_end_now(SERVO_TEST_AB_OPERATOR);
        } else {
            CHECK(!servo_screen_rauto_running());
            CHECK(!svs_seq_busy());
            CHECK_EQ(ra.p5_ons, 0u);
            CHECK_EQ(ra.arms, 0u);
            snprintf(cause, sizeof(cause), TR(SQ_STILL_LATCHED), 500u);
            snprintf(want, sizeof(want), TR(SQ_FAILED), TR(SQ_CLEAR), cause);
            CHECK_STR_EQ(rauto_line(0, b, sizeof(b)), want);
            CHECK_STR_EQ(ui_router_alert(), want);
        }
        ra_ms(2000u);
        ra.latched = ra.clear_dead = false;
        ra_faults();
        ra.p5_task = false;
        carrier_done();
    }
}

/* The run's read finds no servo on the port: a FAIL in the read session,
 * which gives the port back with a LEAVE.  An error leaves no output on:
 * the PD mini goes off once the LEAVE is over, and the bench is SAFE. */
TEST_CASE(a_read_that_finds_no_servo_ends_with_the_pd_mini_off)
{
    ra_setup(true, true);
    CHECK(rauto_hold_blocked(NULL) == NULL);
    sim_add_fault(&g_rig.sim, SIM_ANY, -1, 0, 100000u, SIM_F_DEAF, 0);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    CHECK(g_rig.sup_on);
    const unsigned offs = ra.offs;
    ra_run_out();
    CHECK(!servo_screen_rauto_running());
    CHECK(ra.offs > offs);
    CHECK(!g_rig.sup_on);
    CHECK(!ra.armed);
    g_rig.sim.n_faults = 0u;
}

/* The run goes SAFE for the programmer's read session: the band reads
 * SAFE, and so does the ARM row, without the red dot of an output live
 * through it. */
TEST_CASE(the_arm_row_reads_safe_while_the_read_session_holds_the_bench_safe)
{
    ra_setup(true, true);
    ra_hold_start();
    CHECK(servo_screen_rauto_running());
    for (int i = 0; i < 400 && servo_screen_rauto_running()
                    && !(servo_screen_rauto_phase()
                             == (int)SERVO_RAUTO_PH_READ
                         && !ra.armed);
         ++i) {
        ra_ms(20u);
    }
    CHECK_EQ(servo_screen_rauto_phase(), (int)SERVO_RAUTO_PH_READ);
    ra_ms(40u);
    CHECK(!ra.armed);
    const rdy_row_t r = row_now(RAR_ARM);
    CHECK_STR_EQ(r.value, TR(BAND_SAFE));
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    rauto_end_now(SERVO_TEST_AB_OPERATOR);
    ra_ms(2000u);
    CHECK(!servo_screen_rauto_running());
}

int main(void)
{
    RUN(on_the_carrier_one_hold_switches_port_pwm_and_5v_arms_and_starts);
    RUN(the_5v_read_back_has_1000ms_and_a_late_one_ends_all_off);
    RUN(armed_has_500ms_and_a_refused_arm_ends_all_off);
    RUN(a_pd_mini_off_is_switched_on_by_the_hold);
    RUN(a_tap_on_the_hold_during_the_sequence_ends_it_all_off);
    RUN(a_finger_on_arm_start_locks_port_target_and_pwm);
    RUN(settings_edited_after_entry_are_the_ones_the_run_takes);
    RUN(a_second_finger_opens_nothing_under_arm_start);
    RUN(a_row_during_the_steps_opens_no_keypad_and_the_hold_still_stops);
    RUN(the_run_takes_the_settings_the_hold_fired_on);
    RUN(a_window_measured_before_the_5v_refuses_nothing);
    RUN(a_rail_dead_after_the_5v_fails_naming_prog_5v);
    RUN(a_pd_mini_off_on_a_rail_fed_from_elsewhere_refuses_the_hold);
    RUN(a_pd_mini_set_low_or_past_6v_runs_at_5v_and_stays_there);
    RUN(a_pd_mini_that_does_not_answer_5v_fails_the_read_back);
    RUN(a_pd_mini_capped_under_5v_refuses_the_hold);
    RUN(a_tap_on_a_red_zero_row_takes_the_zero_while_the_encoder_reads);
    RUN(a_tap_on_a_met_row_opens_and_names_nothing);
    RUN(port_pwm_on_with_the_5v_off_reads_amber_with_no_text_of_its_own);
    RUN(a_run_under_way_keeps_the_angle_row_on_its_source_without_a_reading);
    RUN(a_latched_failsafe_is_cleared_and_the_hold_reaches_the_run);
    RUN(a_read_that_finds_no_servo_ends_with_the_pd_mini_off);
    RUN(the_arm_row_reads_safe_while_the_read_session_holds_the_bench_safe);
    return test_summary("servo_rauto_seq");
}
