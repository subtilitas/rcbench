/*
 * LIMIT RUN's HOLD 2s: ARM + START on the SERVO screen and the programmer
 * (servo_limit_panel.c, servo_limit_ready.c, servo_ready.c,
 * servo_start_seq.c): on the rbmod carrier one hold switches PORT PWM and
 * PROG 5V, reads them back, arms and starts the limit run; the read-back's
 * 1000ms and ARMED's 500ms, each failure with every output off and its
 * part named; the PD mini switched on where it feeds a dead rail, refused
 * where the rail is fed from elsewhere or its set point is past 6.0V; a
 * press on the hold that stops the steps; a finger on the hold that opens
 * nothing; the settings the hold fired on; the run's end SAFE with the
 * ports' 5V kept.  The rig is servo_rauto_kst_rig.h.
 *
 * Cases:
 * - on_the_carrier_one_hold_switches_port_pwm_and_5v_arms_and_starts
 * - the_5v_read_back_has_1000ms_and_a_late_one_ends_all_off
 * - armed_has_500ms_and_a_refused_arm_ends_all_off
 * - a_pd_mini_off_on_a_dead_rail_is_switched_on_by_the_hold
 * - a_pd_mini_off_on_a_rail_fed_from_elsewhere_refuses_the_hold
 * - a_pd_mini_off_past_6v_goes_on_only_through_the_hv_warning
 * - a_press_on_the_hold_during_the_steps_ends_them_all_off
 * - a_finger_on_arm_start_opens_nothing
 * - the_run_takes_the_settings_the_hold_fired_on
 * - a_rail_dead_after_the_5v_fails_naming_prog_5v
 * - the_runs_end_leaves_the_bench_safe_and_the_5v_on
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_kst_rig.h"
#include "ui_setlist.h"

/** @brief LIMIT RUN's row @p i, its state and value. */
static rdy_row_t lim_row(int i)
{
    rdy_row_t r;
    CHECK(lim_ready_row(i, &r));
    return r;
}

/** @brief LIMIT RUN on the rbmod carrier from SAFE, PORT PWM and PROG 5V
 *         off, the control task answering PROG 5V after @p delay_ms. */
static void lim_carrier(uint32_t delay_ms)
{
    carrier_safe_off(delay_ms);
    go(SERVO_FN_LIMRUN);
    ra_ms(100u);
}

/** @brief LIMIT RUN on the KST rig's PROG PORT behind a PD mini, SAFE,
 *         the PD mini answering with its output @p on. */
static void lim_pd(bool on)
{
    ra_setup(true, false);
    go(SERVO_FN_LIMRUN);
    ra.armed = false;
    servo_screen_set_armed(false);
    g_rig.sup_on = on;
    ra_pd_reading(on);
    ra_ms(200u);
}

/** @brief The hold pressed, @p ms of frames, the finger lifted. */
static void lim_hold_for(uint32_t ms)
{
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(ms);
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_UP, 1);
}

/** @brief A press on the hold: STOP RUN, or the steps stopped. */
static void lim_press(uint8_t id)
{
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_DOWN, id);
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_UP, id);
}

/* The owner's case on the rbmod carrier: SAFE, PORT PWM off, PROG 5V off,
 * the rail dead.  READY reads PROG 5V, RAIL and PORT PWM START SWITCHES IT
 * ON and ARM START ARMS in amber, and the hold is offered.  One hold: PORT
 * PWM on and saved, PROG 5V ON asked and read back, ARM, ARMED, the limit
 * run.  STOP RUN ends it with the bench SAFE and the PROG 5V off. */
TEST_CASE(on_the_carrier_one_hold_switches_port_pwm_and_5v_arms_and_starts)
{
    lim_carrier(40u);
    CHECK(lim_hold_blocked(NULL) == NULL);
    CHECK_STR_EQ(servo_screen_limit_blocked(),
                 ui_servo_str(SERVO_STR_START_PROG_5V));
    rdy_row_t r = lim_row(LRR_SUPPLY);
    CHECK_STR_EQ(r.label, "PROG 5V");
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(RDY_SEQ_ON));
    CHECK_EQ(lim_row(LRR_RAIL).st, (uint8_t)RDY_SEQ);
    CHECK_EQ(lim_row(LRR_PWM).st, (uint8_t)RDY_SEQ);
    r = lim_row(LRR_ARM);
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(RDY_SEQ_ARM));
    CHECK_EQ(lim_row(LRR_PORT).st, (uint8_t)RDY_OK);
    CHECK_EQ(lim_row(LRR_METER).st, (uint8_t)RDY_OK);
    (void)settings_save();
    lim_hold_for(2300u);
    ra_ms(20u);
    CHECK(servo_screen_limit_running());
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    CHECK(settings_save_asked());
    CHECK(prog5v_box_on());
    CHECK_EQ(ra.p5_ons, 1u);
    CHECK_EQ(ra.arms, 1u);
    CHECK(svs_seq_failure() == NULL);
    CHECK_EQ(lim_row(LRR_SUPPLY).st, (uint8_t)RDY_LIVE);
    CHECK_EQ(lim_row(LRR_ARM).st, (uint8_t)RDY_LIVE);
    CHECK(scr->safe_at(SV_HOLD_X, SV_HOLD_Y));
    scr->render(&cv, 0);
    lim_press(1);
    CHECK(!servo_screen_limit_running());
    ra_ms(2000u);
    CHECK(!ra.armed);
    CHECK(!prog5v_box_on());
    CHECK(ra.p5_offs >= 1u);
    ra.p5_task = false;
    carrier_done();
}

/* The PROG 5V's read-back has 1000ms: read back at 800ms the run starts;
 * at 1100ms the steps fail as READ-BACK names PROG 5V, on the card's first
 * line and in the alert band, with no ARM asked, the bench SAFE and the
 * PROG 5V off. */
TEST_CASE(the_5v_read_back_has_1000ms_and_a_late_one_ends_all_off)
{
    char b[128], want[128], cause[64];
    for (int late = 0; late < 2; ++late) {
        /* The ON is taken on the frame after the hold fires: 20ms of it
         * come off the delay. */
        lim_carrier(late ? 1100u - 20u : 800u - 20u);
        lim_hold_for(2100u);
        CHECK(svs_seq_busy());
        CHECK_EQ(ra.arms, 0u);
        ra_ms(1200u);
        if (!late) {
            CHECK(servo_screen_limit_running());
            CHECK_EQ(ra.arms, 1u);
            lim_press(2);
        } else {
            CHECK(!servo_screen_limit_running());
            CHECK(!svs_seq_busy());
            CHECK_EQ(ra.arms, 0u);
            snprintf(cause, sizeof(cause), TR(SQ_NO_READBACK), 1000u);
            snprintf(want, sizeof(want), TR(SQ_FAILED), "PROG 5V", cause);
            CHECK_STR_EQ(svs_seq_failure(), want);
            CHECK_STR_EQ(servo_screen_limit_line(0, b, sizeof(b)), want);
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

/* ARMED has 500ms: an ARM the bench drops keeps the steps at ARM at 400ms
 * and fails them at 600ms, naming ARM, every output off, the bench SAFE. */
TEST_CASE(armed_has_500ms_and_a_refused_arm_ends_all_off)
{
    char want[128], cause[64];
    lim_pd(true);
    ra.no_arm = true;
    CHECK(lim_hold_blocked(NULL) == NULL);
    const unsigned offs = ra.offs;
    lim_hold_for(2060u);
    CHECK(svs_seq_busy());
    ra_ms(400u);
    CHECK(svs_seq_busy());
    ra_ms(200u);
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_limit_running());
    snprintf(cause, sizeof(cause), TR(SQ_NOT_ARMED), 500u);
    snprintf(want, sizeof(want), TR(SQ_FAILED), "ARM", cause);
    CHECK_STR_EQ(svs_seq_failure(), want);
    ra_ms(100u);
    CHECK(ra.offs > offs);
    CHECK(!g_rig.sup_on);
    CHECK(ra.disarms >= 1u);
    ra.no_arm = false;
}

/* The PD mini answers with its output off on a rail the INA3221 reads
 * dead: PD MINI and RAIL read START SWITCHES IT ON, and the hold switches
 * the PD mini on before ARM. */
TEST_CASE(a_pd_mini_off_on_a_dead_rail_is_switched_on_by_the_hold)
{
    lim_pd(false);
    rdy_row_t r = lim_row(LRR_SUPPLY);
    CHECK_STR_EQ(r.label, "PD MINI");
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_EQ(lim_row(LRR_RAIL).st, (uint8_t)RDY_SEQ);
    CHECK(lim_hold_blocked(NULL) == NULL);
    lim_hold_for(2300u);
    ra_ms(20u);
    CHECK_EQ(ra.pd_ons, 1u);
    CHECK(servo_screen_limit_running());
    lim_press(2);
    ra_ms(2000u);
    CHECK(!servo_screen_limit_running());
    CHECK(!g_rig.sup_on);
}

/* The PD mini answers with its output off while the rail reads 6.0V: fed
 * from elsewhere, PD MINI's row is red with the PD-off text, the hold
 * refuses and switches nothing. */
TEST_CASE(a_pd_mini_off_on_a_rail_fed_from_elsewhere_refuses_the_hold)
{
    lim_pd(false);
    ra.rail_fed = true;
    ra_ms(200u);
    rdy_row_t r = lim_row(LRR_SUPPLY);
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK_STR_EQ(r.why, TR(SV_LIM_PROG_PD_OFF));
    limrun_row_t row = LRR_COUNT;
    CHECK_STR_EQ(lim_hold_blocked(&row), TR(SV_LIM_PROG_PD_OFF));
    CHECK_EQ(row, LRR_SUPPLY);
    lim_hold_for(2300u);
    ra_ms(20u);
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_limit_running());
    CHECK_EQ(ra.pd_ons, 0u);
    CHECK_EQ(ra.arms, 0u);
    ra.rail_fed = false;
}

/* The PD mini off with SUPPLY's set point at 8.4V: PD MINI's row is red
 * with TAP: HV WARNING, the hold refuses and sends no ON, the bench stays
 * SAFE.  A tap on the row opens OUTPUT ON's HV warning, the one way that
 * ON is taken. */
TEST_CASE(a_pd_mini_off_past_6v_goes_on_only_through_the_hv_warning)
{
    char want[128], got[128];
    lim_pd(false);
    settings_set(SET_SUPPLY_V_START, 8.4f);
    supply_screen_settings_loaded();
    CHECK(supply_screen_set_v() > 8.3f);
    ra_pd_reading(false);
    ra_ms(200u);
    rdy_row_t r = lim_row(LRR_SUPPLY);
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK_STR_EQ(r.why, TR(SV_TAP_HV));
    CHECK(r.fix);
    CHECK_STR_EQ(lim_hold_blocked(NULL), TR(SV_TAP_HV));
    snprintf(want, sizeof(want), TR(RDY_BAND), "PD MINI", TR(SV_TAP_HV));
    CHECK_STR_EQ(ready_refusal(got, sizeof(got)), want);
    lim_hold_for(2300u);
    ra_ms(20u);
    CHECK(!svs_seq_busy());
    CHECK_EQ(ra.pd_ons, 0u);
    CHECK_EQ(ra.arms, 0u);
    CHECK(!ra.armed);
    CHECK(!servo_screen_output_held());
    ev(SV_RDY_X, SV_RDY_ROW_Y(LRR_SUPPLY), TOUCH_EVENT_DOWN, 2);
    ev(SV_RDY_X, SV_RDY_ROW_Y(LRR_SUPPLY), TOUCH_EVENT_UP, 2);
    ra_ms(20u);
    CHECK(servo_screen_output_held());
    CHECK_EQ(ra.pd_ons, 0u);
    settings_set(SET_SUPPLY_V_START, settings_def(SET_SUPPLY_V_START)->def);
    supply_screen_settings_loaded();
}

/* A press on the hold while the steps wait for the read-back ends them as
 * STOP, every output off, no ARM, no run. */
TEST_CASE(a_press_on_the_hold_during_the_steps_ends_them_all_off)
{
    lim_carrier(800u);
    lim_hold_for(2100u);
    CHECK(svs_seq_busy());
    CHECK(scr->safe_at(SV_HOLD_X, SV_HOLD_Y));
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_DOWN, 2);
    CHECK(!svs_seq_busy());
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_UP, 2);
    char want[128];
    snprintf(want, sizeof(want), TR(SQ_FAILED), "PROG 5V", "STOP");
    CHECK_STR_EQ(svs_seq_failure(), want);
    ra_ms(1500u);
    CHECK(!servo_screen_limit_running());
    CHECK(!prog5v_box_on());
    CHECK_EQ(ra.arms, 0u);
    ra.p5_task = false;
    carrier_done();
}

/* A second finger while ARM + START's 2s hold fills: SETTINGS opens no
 * list, PORT no choice, the card takes no tap, TARGET and PORT PWM read
 * locked; the finger lifted at 1s switches nothing, arms nothing and
 * starts nothing. */
TEST_CASE(a_finger_on_arm_start_opens_nothing)
{
    lim_carrier(40u);
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(500u);
    CHECK(servo_screen_arm_held());
    CHECK(servo_screen_target_locked());
    CHECK(servo_screen_port_pwm_locked());
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_UP, 2);
    CHECK(!servo_screen_list()->open);
    ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_UP, 2);
    CHECK_EQ(servo_screen_port_choices(), -1);
    ui_router_set_alert(NULL);
    for (int r = 0; r < (int)LRR_COUNT; ++r) {
        ev(SV_RDY_X, SV_RDY_ROW_Y(r), TOUCH_EVENT_DOWN, 2);
        ev(SV_RDY_X, SV_RDY_ROW_Y(r), TOUCH_EVENT_UP, 2);
        CHECK(!servo_screen_list()->open);
        CHECK_EQ(servo_screen_port_choices(), -1);
        CHECK(ui_router_alert() == NULL);
    }
    ra_ms(500u);
    ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_UP, 1);
    ra_ms(2000u);
    CHECK(!servo_screen_arm_held());
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_limit_running());
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    CHECK(!prog5v_box_on());
    CHECK_EQ(ra.arms, 0u);
    ra.p5_task = false;
    carrier_done();
}

/* The device's name changed after the hold fired is not the run's: the
 * report names the one the hold fired on. */
TEST_CASE(the_run_takes_the_settings_the_hold_fired_on)
{
    lim_carrier(300u);
    settings_set_text(SET_TEXT_DUT_NAME, "FIRED");
    lim_hold_for(2100u);
    CHECK(svs_seq_busy());
    settings_set_text(SET_TEXT_DUT_NAME, "LATER");
    ra_ms(1000u);
    CHECK(servo_screen_limit_running());
    lim_press(2);
    ra_ms(2000u);
    CHECK(!servo_screen_limit_running());
    CHECK(strstr(ra.report, "FIRED") != NULL);
    CHECK(strstr(ra.report, "LATER") == NULL);
    ra.p5_task = false;
    carrier_done();
}

/* PROG 5V reads back ON and the rail stays at 0V: the window begun after
 * it refuses, and the step that failed is PROG 5V's, the rail's line in
 * the band, all off, no ARM. */
TEST_CASE(a_rail_dead_after_the_5v_fails_naming_prog_5v)
{
    char want[128];
    lim_carrier(40u);
    ra.rail_dead = true;
    lim_hold_for(2300u);
    ra_ms(20u);
    CHECK(!servo_screen_limit_running());
    CHECK_EQ(ra.arms, 0u);
    snprintf(want, sizeof(want), TR(SQ_FAILED), "PROG 5V",
             TR(SV_LIM_RAIL_LOW));
    CHECK_STR_EQ(svs_seq_failure(), want);
    ra_ms(500u);
    CHECK(!prog5v_box_on());
    ra.rail_dead = false;
    ra.p5_task = false;
    carrier_done();
}

/* The run's own end, here after the read finds the servo's pulses outside
 * the bench's range: the bench goes SAFE, so the footer reads ARM + START
 * again, and the ports' 5V stays on for the next SERVO screen. */
TEST_CASE(the_runs_end_leaves_the_bench_safe_and_the_5v_on)
{
    lim_carrier(40u);
    lim_hold_for(2300u);
    ra_ms(20u);
    CHECK(servo_screen_limit_running());
    for (int i = 0; i < 240 && servo_screen_limit_running(); ++i) {
        ra_ms(500u);
    }
    CHECK(!servo_screen_limit_running());
    ra_ms(500u);
    CHECK(!ra.armed);
    CHECK(ra.disarms >= 1u);
    CHECK(prog5v_box_on());
    CHECK(!scr->safe_at(SV_HOLD_X, SV_HOLD_Y));
    char b[96];
    bool verdict = false;
    for (int i = 0; i < 8; ++i) {
        verdict = verdict
                  || strncmp(servo_screen_limit_line(i, b, sizeof(b)),
                             "LIMITS ", 7) == 0;
    }
    CHECK(verdict);
    scr->render(&cv, 0);
    ra.p5_task = false;
    carrier_done();
}

int main(void)
{
    RUN(on_the_carrier_one_hold_switches_port_pwm_and_5v_arms_and_starts);
    RUN(the_5v_read_back_has_1000ms_and_a_late_one_ends_all_off);
    RUN(armed_has_500ms_and_a_refused_arm_ends_all_off);
    RUN(a_pd_mini_off_on_a_dead_rail_is_switched_on_by_the_hold);
    RUN(a_pd_mini_off_on_a_rail_fed_from_elsewhere_refuses_the_hold);
    RUN(a_pd_mini_off_past_6v_goes_on_only_through_the_hv_warning);
    RUN(a_press_on_the_hold_during_the_steps_ends_them_all_off);
    RUN(a_finger_on_arm_start_opens_nothing);
    RUN(the_run_takes_the_settings_the_hold_fired_on);
    RUN(a_rail_dead_after_the_5v_fails_naming_prog_5v);
    RUN(the_runs_end_leaves_the_bench_safe_and_the_5v_on);
    return test_summary("servo_limrun_seq");
}
