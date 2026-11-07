/*
 * RUDDER AUTO's HOLD 2s: ARM + START on the SERVO screen and the
 * programmer (servo_rudder_auto_panel.c, servo_ready.c,
 * servo_start_seq.c): on the rbmod carrier one hold switches PORT PWM and
 * PROG 5V, reads them back, arms and starts; the read-back's 1000ms and
 * ARMED's 500ms on the screen, each failure with every output off and its
 * step named; the PD mini switched on where it feeds the servo; a tap
 * that stops the sequence.  The rig is servo_rauto_kst_rig.h.
 *
 * Cases:
 * - on_the_carrier_one_hold_switches_port_pwm_and_5v_arms_and_starts
 * - the_5v_read_back_has_1000ms_and_a_late_one_ends_all_off
 * - armed_has_500ms_and_a_refused_arm_ends_all_off
 * - a_pd_mini_off_is_switched_on_by_the_hold
 * - a_tap_on_the_hold_during_the_sequence_ends_it_all_off
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_kst_rig.h"

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

/* The PROG 5V's read-back has 1000ms: read back at 900ms the run starts;
 * at 1100ms the sequence fails as READ-BACK names PROG 5V, in the first
 * line and in the alert band, with no ARM asked, the bench SAFE and the
 * PROG 5V off. */
TEST_CASE(the_5v_read_back_has_1000ms_and_a_late_one_ends_all_off)
{
    char b[128], want[128], cause[64];
    for (int late = 0; late < 2; ++late) {
        /* The ON is taken on the frame after the hold fires: 20ms of it
         * come off the delay. */
        carrier_safe_off(late ? 1100u - 20u : 900u - 20u);
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

int main(void)
{
    RUN(on_the_carrier_one_hold_switches_port_pwm_and_5v_arms_and_starts);
    RUN(the_5v_read_back_has_1000ms_and_a_late_one_ends_all_off);
    RUN(armed_has_500ms_and_a_refused_arm_ends_all_off);
    RUN(a_pd_mini_off_is_switched_on_by_the_hold);
    RUN(a_tap_on_the_hold_during_the_sequence_ends_it_all_off);
    return test_summary("servo_rauto_seq");
}
