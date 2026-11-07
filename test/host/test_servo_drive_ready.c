/*
 * LIVE, SWEEP and RUDDER on the function screens' frame
 * (servo_drive_ready.c): their READY rows over the output card, ARM that
 * switches the servo's supply on first through the shared steps
 * (servo_start_seq.c), DISARM that switches every output off, and the PD
 * mini's HV warning and a rail fed from elsewhere, which ARM never
 * switches past.  The rig is servo_rauto_kst_rig.h.
 *
 * Cases:
 * - each_screen_names_its_rows_and_shows_no_supply_box
 * - on_the_carrier_arm_switches_port_pwm_and_5v_then_arms
 * - the_5v_read_back_has_1000ms_and_a_late_one_ends_all_off
 * - a_pd_mini_off_is_switched_on_by_arm_and_disarm_switches_it_off
 * - a_rail_fed_from_elsewhere_keeps_the_pd_mini_off_and_arms_at_once
 * - a_window_the_ina3221_left_behind_is_no_fed_rail
 * - a_fed_rail_arms_whatever_the_pd_minis_set_point
 * - a_rail_fed_after_the_hold_refuses_the_pd_minis_on
 * - a_tap_on_a_drive_row_names_why_or_opens_its_list
 * - past_6v_arm_is_refused_and_the_row_opens_the_hv_warning
 * - a_tap_on_arm_or_a_stop_during_the_steps_ends_them_all_off
 * - the_5v_arm_switched_stays_across_servo_and_goes_with_the_last
 * - a_leave_under_arms_hold_switches_nothing_on
 * - a_finger_on_arm_opens_no_row_fix
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_kst_rig.h"
#include "ui_setlist.h"

/* LIVE's and SWEEP's rows, and RUDDER's, as the card orders them. */
enum { LV_PORT = 0, LV_SUPPLY, LV_ARM };
enum { RD_PORT = 0, RD_ANGLE, RD_ZERO, RD_SUPPLY, RD_ARM };

/** @brief @p s, or "(null)": a check that fails prints, not crashes. */
static const char *nn(const char *s)
{
    return s != NULL ? s : "(null)";
}

/** @brief Card row @p i as drawn now. */
static rdy_row_t drow(int i)
{
    rdy_row_t r;
    CHECK(ready_row(i, &r));
    return r;
}

/** @brief ARM in the footer held for @p ms, then lifted. */
static void arm_hold(uint32_t ms)
{
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(ms);
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_UP, 1);
}

/** @brief A tap on ARM or DISARM. */
static void arm_tap(uint8_t id)
{
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, id);
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_UP, id);
}

/** @brief The rig's PD mini behind the servo, its output off, the bench
 *         SAFE, on LIVE. */
static void pd_off_on_live(void)
{
    ra_setup(true, true);
    ra.armed = false;
    servo_screen_set_armed(false);
    g_rig.sup_on = false;
    ra_pd_reading(false);
    go(SERVO_FN_LIVE);
    ra_ms(200u);
}

/* LIVE and SWEEP show PORT, SUPPLY and ARM, RUDDER PORT, ANGLE, ZERO,
 * SUPPLY and ARM, over the output card; the SET row and OUTPUT ON are not
 * there.  Off on a SAFE bench, the PD mini's row reads ARM SWITCHES IT ON
 * in amber and ARM HOLD 2s; KST shows none of these rows and has no ARM
 * that switches the supply. */
TEST_CASE(each_screen_names_its_rows_and_shows_no_supply_box)
{
    pd_off_on_live();
    CHECK(drive_shown());
    CHECK_EQ(ready_count(), 3);
    CHECK_STR_EQ(drow(LV_PORT).label, TR(SVH_PORT));
    rdy_row_t r = drow(LV_SUPPLY);
    CHECK_STR_EQ(r.label, "PD MINI");
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(RDY_ARM_ON));
    r = drow(LV_ARM);
    CHECK_STR_EQ(r.label, "ARM");
    CHECK_STR_EQ(r.value, TR(RDY_ARM_HOLD));
    CHECK(drive_hold_blocked(NULL) == NULL);
    CHECK(servo_screen_arm_switches());
    /* Where the SET row's OUTPUT ON was, the SPEED row's panel: no green
     * button. */
    scr->render(&cv, 0);
    CHECK(gfx_pixel_get(&cv, SV_SUP_OUT_X, SV_SUP_Y)
          != ui_theme_color(UI_C_OK));
    /* SUPPLY's dot, amber: the rows are drawn, not the output card. */
    CHECK(gfx_pixel_get(&cv, SV_RC_X + 5, SV_DRV_ROW_Y(LV_SUPPLY))
          == ui_theme_color(UI_C_WARN));
    go(SERVO_FN_SWEEP);
    CHECK_EQ(ready_count(), 3);
    go(SERVO_FN_RUDDER);
    CHECK_EQ(ready_count(), 5);
    CHECK_STR_EQ(drow(RD_ANGLE).label, TR(RDY_ANGLE));
    CHECK_STR_EQ(drow(RD_ZERO).label, TR(RDY_ZERO));
    CHECK_STR_EQ(drow(RD_SUPPLY).label, "PD MINI");
    /* ZERO taken by the rig, the AS5600 reading: both met. */
    CHECK_EQ(drow(RD_ANGLE).st, (uint8_t)RDY_OK);
    CHECK_STR_EQ(drow(RD_ANGLE).value, "AS5600");
    CHECK_EQ(drow(RD_ZERO).st, (uint8_t)RDY_OK);
    go(SERVO_FN_KST);
    CHECK(!drive_shown());
    CHECK_EQ(drive_ready_count(), 0);
    CHECK(!servo_screen_arm_switches());
}

/* The rbmod carrier's port from SAFE, PORT PWM and PROG 5V off: PORT and
 * PROG 5V read amber, and ARM's one hold switches PORT PWM on and saves
 * it, asks PROG 5V ON, reads both back, arms.  DISARM then switches the
 * 5V off with the bench. */
TEST_CASE(on_the_carrier_arm_switches_port_pwm_and_5v_then_arms)
{
    carrier_safe_off(40u);
    go(SERVO_FN_LIVE);
    ra_ms(100u);
    CHECK_EQ(drow(LV_PORT).st, (uint8_t)RDY_SEQ);
    rdy_row_t r = drow(LV_SUPPLY);
    CHECK_STR_EQ(r.label, "PROG 5V");
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(RDY_ARM_ON));
    CHECK(servo_screen_arm_switches());
    (void)settings_save();
    arm_hold(2300u);
    ra_ms(200u);
    CHECK(!svs_seq_busy());
    CHECK(svs_seq_failure() == NULL);
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    CHECK(settings_save_asked());
    CHECK(prog5v_box_on());
    CHECK_EQ(ra.p5_ons, 1u);
    CHECK_EQ(ra.arms, 1u);
    CHECK(ra.armed);
    CHECK_EQ(drow(LV_SUPPLY).st, (uint8_t)RDY_LIVE);
    CHECK_EQ(drow(LV_ARM).st, (uint8_t)RDY_LIVE);
    /* DISARM: SAFE, and the 5V off with it. */
    arm_tap(2);
    ra_ms(200u);
    CHECK(!ra.armed);
    CHECK(ra.p5_offs >= 1u);
    CHECK(!prog5v_box_on());
    ra.p5_task = false;
    carrier_done();
}

/* The 5V's read-back has 1000ms: read back at 800ms ARM follows; at
 * 1100ms the steps fail naming PROG 5V in the alert band, no ARM is
 * asked, the 5V is off and the bench SAFE. */
TEST_CASE(the_5v_read_back_has_1000ms_and_a_late_one_ends_all_off)
{
    char want[128], cause[64];
    for (int late = 0; late < 2; ++late) {
        carrier_safe_off(late ? 1100u - 20u : 800u - 20u);
        go(SERVO_FN_LIVE);
        ra_ms(100u);
        ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 1);
        ra_ms(2100u);
        ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_UP, 1);
        CHECK(svs_seq_busy());
        CHECK_EQ(ra.arms, 0u);
        ra_ms(1300u);
        CHECK(!svs_seq_busy());
        if (!late) {
            CHECK_EQ(ra.arms, 1u);
            CHECK(ra.armed);
            arm_tap(2);
        } else {
            CHECK_EQ(ra.arms, 0u);
            snprintf(cause, sizeof(cause), TR(SQ_NO_READBACK), 1000u);
            snprintf(want, sizeof(want), TR(SQ_FAILED), "PROG 5V", cause);
            CHECK_STR_EQ(nn(svs_seq_failure()), want);
            CHECK_STR_EQ(nn(ui_router_alert()), want);
            CHECK(ra.p5_offs >= 1u);
            CHECK(!prog5v_box_on());
            CHECK(!ra.armed);
        }
        ra_ms(2000u);
        ra.p5_task = false;
        carrier_done();
    }
}

/* The PD mini behind the servo with its output off: ARM switches it on,
 * reads it back and arms.  DISARM switches it off with the bench. */
TEST_CASE(a_pd_mini_off_is_switched_on_by_arm_and_disarm_switches_it_off)
{
    pd_off_on_live();
    arm_hold(2300u);
    ra_ms(200u);
    CHECK_EQ(ra.pd_ons, 1u);
    CHECK(g_rig.sup_on);
    CHECK_EQ(ra.arms, 1u);
    CHECK(ra.armed);
    CHECK(svs_seq_failure() == NULL);
    const unsigned offs = ra.offs;
    arm_tap(2);
    ra_ms(200u);
    CHECK(!ra.armed);
    CHECK(ra.offs > offs);
    CHECK(!g_rig.sup_on);
}

/* A rail the INA3221 reads at 3.0V or more with the PD mini off is fed
 * from elsewhere: the row says so in the warning colour, refuses nothing,
 * and ARM arms at once without switching the PD mini on. */
TEST_CASE(a_rail_fed_from_elsewhere_keeps_the_pd_mini_off_and_arms_at_once)
{
    pd_off_on_live();
    ra.rail_fed = true;
    ra_ms(200u);
    const rdy_row_t r = drow(LV_SUPPLY);
    CHECK_EQ(r.st, (uint8_t)RDY_WARN);
    CHECK_STR_EQ(r.value, TR(RDY_FED));
    CHECK(drive_hold_blocked(NULL) == NULL);
    CHECK(!servo_screen_arm_switches());
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(2100u);
    CHECK(!svs_seq_busy());
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_UP, 1);
    ra_ms(100u);
    CHECK_EQ(ra.pd_ons, 0u);
    CHECK(!g_rig.sup_on);
    CHECK_EQ(ra.arms, 1u);
    ra.rail_fed = false;
}

/* The INA3221 reads the rail fed, then stops being the rail's meter: the
 * window it left behind reads nothing now, so ARM switches the PD mini on
 * as on a bench without the INA3221. */
TEST_CASE(a_window_the_ina3221_left_behind_is_no_fed_rail)
{
    pd_off_on_live();
    ra.rail_fed = true;
    ra_ms(200u);
    CHECK_STR_EQ(drow(LV_SUPPLY).value, TR(RDY_FED));
    /* The PD mini becomes the meter; the INA3221's windows of the older
     * change count are not taken any more. */
    servo_screen_source(SERVO_SOURCE_PDMINI, 3u);
    ra.rail_fed = false;
    ra_ms(100u);
    CHECK(servo_screen_arm_switches());
    arm_hold(2300u);
    ra_ms(200u);
    CHECK_EQ(ra.pd_ons, 1u);
    CHECK(g_rig.sup_on);
    CHECK(ra.armed);
}

/* A tap on a row that is not met names why in the band, as ARM's refusal
 * does.  Before ARM's steps no supply is asked for, so SUPPLY's reason
 * names no part, nor does a text no row reads.  A tap on PORT opens the
 * port list. */
TEST_CASE(a_tap_on_a_drive_row_names_why_or_opens_its_list)
{
    char want[128];
    pd_off_on_live();
    ui_router_set_alert(NULL);
    const rdy_row_t r = drow(LV_SUPPLY);
    CHECK(r.why != NULL);
    ev(SV_RDY_X, SV_DRV_ROW_Y(LV_SUPPLY), TOUCH_EVENT_DOWN, 2);
    ev(SV_RDY_X, SV_DRV_ROW_Y(LV_SUPPLY), TOUCH_EVENT_UP, 2);
    snprintf(want, sizeof(want), TR(RDY_BAND), r.label, nn(r.why));
    CHECK_STR_EQ(nn(ui_router_alert()), want);
    CHECK_EQ(drive_part_of(r.why), SERVO_SEQ_PART_NONE);
    CHECK_EQ(drive_part_of("no row reads this"), SERVO_SEQ_PART_NONE);
    CHECK_EQ(drive_part_of(NULL), SERVO_SEQ_PART_NONE);
    CHECK_EQ(servo_screen_port_choices(), -1);
    ev(SV_RDY_X, SV_DRV_ROW_Y(LV_PORT), TOUCH_EVENT_DOWN, 2);
    ev(SV_RDY_X, SV_DRV_ROW_Y(LV_PORT), TOUCH_EVENT_UP, 2);
    CHECK(servo_screen_port_choices() > 0);
}

/* The PD mini judged off-rail at the hold, then a window reads the rail
 * fed from elsewhere before the steps ask its ON: the steps end refused,
 * the PD mini never switched on and the bench never armed. */
TEST_CASE(a_rail_fed_after_the_hold_refuses_the_pd_minis_on)
{
    pd_off_on_live();
    CHECK(svs_seq_begin());
    const sense_link_win_t w = {
        .number = (uint16_t)++ra.win_no, .current = true, .voltage = true,
        .mean_mv = 6000u, .min_mv = 6000u, .taken_ms = g_rig.ms,
        .stamped = true,
    };
    servo_screen_window(&w, SERVO_SOURCE_INA3221, 2u);
    ra.rail_fed = true;
    ra_ms(300u);
    CHECK(!svs_seq_busy());
    CHECK_EQ(ra.pd_ons, 0u);
    CHECK_EQ(ra.arms, 0u);
    CHECK(!g_rig.sup_on);
    ra.rail_fed = false;
}

/* A rail fed from elsewhere with the PD mini's set point past 6.0V: ARM
 * leaves the PD mini off, so the set point is never applied; the row reads
 * FED FROM ELSEWHERE in the warning colour and ARM arms at once, sending no
 * ON. */
TEST_CASE(a_fed_rail_arms_whatever_the_pd_minis_set_point)
{
    pd_off_on_live();
    settings_set(SET_SUPPLY_V_START, 8.4f);
    supply_screen_settings_loaded();
    ra.rail_fed = true;
    ra_ms(200u);
    const rdy_row_t r = drow(LV_SUPPLY);
    CHECK_EQ(r.st, (uint8_t)RDY_WARN);
    CHECK_STR_EQ(r.value, TR(RDY_FED));
    CHECK(drive_hold_blocked(NULL) == NULL);
    arm_hold(2300u);
    ra_ms(100u);
    CHECK_EQ(ra.pd_ons, 0u);
    CHECK(!g_rig.sup_on);
    CHECK_EQ(ra.arms, 1u);
    ra.rail_fed = false;
}

/* The PD mini off with a set point past 6.0V: SUPPLY reads TAP: HV
 * WARNING in red, ARM takes no hold and names the row, and nothing is
 * switched on; a tap on the row opens OUTPUT ON's HV warning, whose own
 * hold is the only way on. */
TEST_CASE(past_6v_arm_is_refused_and_the_row_opens_the_hv_warning)
{
    char want[128], got[128];
    pd_off_on_live();
    settings_set(SET_SUPPLY_V_START, 8.4f);
    supply_screen_settings_loaded();
    CHECK(supply_screen_set_v() > 8.3f);
    ra_ms(200u);
    const rdy_row_t r = drow(LV_SUPPLY);
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK_STR_EQ(nn(r.why), TR(SV_TAP_HV));
    CHECK(r.fix);
    snprintf(want, sizeof(want), TR(RDY_BAND), "PD MINI", TR(SV_TAP_HV));
    CHECK_STR_EQ(nn(ready_refusal(got, sizeof(got))), want);
    arm_hold(2300u);
    ra_ms(200u);
    CHECK(!svs_seq_busy());
    CHECK_EQ(ra.pd_ons, 0u);
    CHECK_EQ(ra.arms, 0u);
    CHECK(!g_rig.sup_on);
    CHECK(!servo_screen_output_held());
    ev(SV_RDY_X, SV_DRV_ROW_Y(LV_SUPPLY), TOUCH_EVENT_DOWN, 2);
    ev(SV_RDY_X, SV_DRV_ROW_Y(LV_SUPPLY), TOUCH_EVENT_UP, 2);
    ra_ms(20u);
    CHECK(servo_screen_output_held());
    CHECK_EQ(ra.pd_ons, 0u);
    settings_set(SET_SUPPLY_V_START, settings_def(SET_SUPPLY_V_START)->def);
    supply_screen_settings_loaded();
}

/* While the steps wait for PROG 5V's read-back, a tap on the button (it
 * reads the step in amber) ends them as STOP, every output off; a STOP
 * from the band ends them the same way. */
TEST_CASE(a_tap_on_arm_or_a_stop_during_the_steps_ends_them_all_off)
{
    char want[128];
    for (int stop = 0; stop < 2; ++stop) {
        carrier_safe_off(800u);
        go(SERVO_FN_LIVE);
        ra_ms(100u);
        arm_hold(2100u);
        CHECK(svs_seq_busy());
        if (stop) {
            servo_screen_cancel_arm();
        } else {
            arm_tap(2);
        }
        CHECK(!svs_seq_busy());
        snprintf(want, sizeof(want), TR(SQ_FAILED), "PROG 5V", "STOP");
        CHECK_STR_EQ(nn(svs_seq_failure()), want);
        ra_ms(1500u);
        CHECK(!prog5v_box_on());
        CHECK_EQ(ra.arms, 0u);
        CHECK(!ra.armed);
        ra.p5_task = false;
        carrier_done();
    }
}

/* The 5V ARM switched on stays from LIVE to another SERVO screen, the
 * bench SAFE on each leave, and goes off when SERVO is left. */
TEST_CASE(the_5v_arm_switched_stays_across_servo_and_goes_with_the_last)
{
    carrier_safe_off(40u);
    ui_router_goto(SCREEN_SERVO_LIVE);
    scr = servo_fn_screen(SERVO_FN_LIVE);
    ra_ms(100u);
    arm_hold(2300u);
    ra_ms(200u);
    CHECK(ra.armed);
    CHECK(prog5v_box_on());
    const unsigned offs = ra.p5_offs;
    ui_router_goto(SCREEN_SERVO_RUDDER);
    scr = servo_fn_screen(SERVO_FN_RUDDER);
    ra_ms(200u);
    CHECK(!ra.armed);
    CHECK(prog5v_box_on());
    CHECK_EQ(ra.p5_offs, offs);
    ui_router_goto(SCREEN_SERVO);
    ui_router_goto(SCREEN_OVERVIEW);
    ra_ms(200u);
    CHECK(ra.p5_offs > offs);
    CHECK(!prog5v_box_on());
    ra.p5_task = false;
    carrier_done();
}

/* ARM's hold under way as SERVO is left: the leave drops it before its
 * 2s, so neither PORT PWM, PROG 5V nor ARM goes on. */
TEST_CASE(a_leave_under_arms_hold_switches_nothing_on)
{
    carrier_safe_off(40u);
    ui_router_goto(SCREEN_SERVO_LIVE);
    scr = servo_fn_screen(SERVO_FN_LIVE);
    ra_ms(100u);
    CHECK(servo_screen_arm_switches());
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(1000u);
    CHECK(servo_screen_arm_held());
    ui_router_goto(SCREEN_OVERVIEW);
    ra_ms(2000u);
    CHECK(!svs_seq_busy());
    CHECK_EQ(ra.p5_ons, 0u);
    CHECK_EQ(ra.arms, 0u);
    CHECK(!prog5v_box_on());
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    ra.p5_task = false;
    carrier_done();
}

/* A finger on ARM: a second finger's tap on PORT's row opens no list, and
 * on SETTINGS no list either; the hold still fires on its own finger. */
TEST_CASE(a_finger_on_arm_opens_no_row_fix)
{
    pd_off_on_live();
    go(SERVO_FN_SWEEP);
    ra_ms(100u);
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(500u);
    ev(SV_RDY_X, SV_DRV_ROW_Y(LV_PORT), TOUCH_EVENT_DOWN, 2);
    ev(SV_RDY_X, SV_DRV_ROW_Y(LV_PORT), TOUCH_EVENT_UP, 2);
    CHECK_EQ(servo_screen_port_choices(), -1);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 3);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_UP, 3);
    CHECK(!servo_screen_list()->open);
    ra_ms(1800u);
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_UP, 1);
    ra_ms(200u);
    CHECK_EQ(ra.arms, 1u);
    CHECK_EQ(ra.pd_ons, 1u);
    /* Lifted and SAFE again, the row opens PORT's list. */
    arm_tap(2);
    ra_ms(200u);
    ev(SV_RDY_X, SV_DRV_ROW_Y(LV_PORT), TOUCH_EVENT_DOWN, 2);
    ev(SV_RDY_X, SV_DRV_ROW_Y(LV_PORT), TOUCH_EVENT_UP, 2);
    CHECK(servo_screen_port_choices() > 0);
}

/* RUDDER's ZERO row takes the zero on a tap; with a finger on ARM it
 * takes nothing, and the hold still fires. */
TEST_CASE(a_finger_on_arm_takes_no_zero)
{
    ra_setup(true, false);
    ra.armed = false;
    servo_screen_set_armed(false);
    g_rig.sup_on = false;
    ra_pd_reading(false);
    go(SERVO_FN_RUDDER);
    ra_ms(200u);
    CHECK_EQ(drow(RD_ZERO).st, (uint8_t)RDY_WARN);
    CHECK(drow(RD_ZERO).fix);
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(500u);
    ev(SV_RDY_X, SV_DRV_ROW_Y(RD_ZERO), TOUCH_EVENT_DOWN, 2);
    ev(SV_RDY_X, SV_DRV_ROW_Y(RD_ZERO), TOUCH_EVENT_UP, 2);
    ra_ms(20u);
    CHECK_EQ(drow(RD_ZERO).st, (uint8_t)RDY_WARN);
    ra_ms(1800u);
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_UP, 1);
    ra_ms(200u);
    CHECK_EQ(ra.arms, 1u);
    arm_tap(2);
    ra_ms(200u);
    CHECK(!ra.armed);
    /* Lifted and SAFE, the row takes the zero. */
    ev(SV_RDY_X, SV_DRV_ROW_Y(RD_ZERO), TOUCH_EVENT_DOWN, 2);
    ev(SV_RDY_X, SV_DRV_ROW_Y(RD_ZERO), TOUCH_EVENT_UP, 2);
    ra_ms(20u);
    CHECK_EQ(drow(RD_ZERO).st, (uint8_t)RDY_OK);
}

int main(void)
{
    RUN(each_screen_names_its_rows_and_shows_no_supply_box);
    RUN(on_the_carrier_arm_switches_port_pwm_and_5v_then_arms);
    RUN(the_5v_read_back_has_1000ms_and_a_late_one_ends_all_off);
    RUN(a_pd_mini_off_is_switched_on_by_arm_and_disarm_switches_it_off);
    RUN(a_rail_fed_from_elsewhere_keeps_the_pd_mini_off_and_arms_at_once);
    RUN(a_window_the_ina3221_left_behind_is_no_fed_rail);
    RUN(a_fed_rail_arms_whatever_the_pd_minis_set_point);
    RUN(a_rail_fed_after_the_hold_refuses_the_pd_minis_on);
    RUN(a_tap_on_a_drive_row_names_why_or_opens_its_list);
    RUN(past_6v_arm_is_refused_and_the_row_opens_the_hv_warning);
    RUN(a_tap_on_arm_or_a_stop_during_the_steps_ends_them_all_off);
    RUN(the_5v_arm_switched_stays_across_servo_and_goes_with_the_last);
    RUN(a_leave_under_arms_hold_switches_nothing_on);
    RUN(a_finger_on_arm_opens_no_row_fix);
    RUN(a_finger_on_arm_takes_no_zero);
    return test_summary("servo_drive_ready");
}
