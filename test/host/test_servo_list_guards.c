/**
 * @file test_servo_list_guards.c
 * @brief When SERVO's lists, pages of rows and PORT list open and stay
 *        open: the run card's SETTINGS by the footer's rule, a page closed
 *        as the bench goes live, and no list or PORT pick while a finger
 *        holds a press elsewhere on the screen.
 *
 * Cases:
 * - the_run_cards_settings_open_nothing_while_armed
 * - a_page_of_rows_closes_as_the_bench_arms
 * - a_press_held_on_rudder_keeps_settings_shut
 * - a_press_held_on_rudder_auto_keeps_settings_shut
 * - a_finger_on_a_kst_control_keeps_the_port_list_shut
 * - a_finger_on_output_takes_no_port_pick
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

#include "kst_panel.h"

/* RUDDER's ZERO, the left of its row (servo_rudder_int.h): x 18 to 178. */
#define ZERO_X 98

/** @brief The alert band's text, "" for none. */
static const char *alert(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/* TEST's SETTINGS in the footer on an armed bench opens no list; on a
 * SAFE bench it opens the automatic test's.  LIMIT RUN's card SETTINGS
 * the same with its page of rows. */
TEST_CASE(the_run_cards_settings_open_nothing_while_armed)
{
    fresh_armed();
    go(SERVO_FN_TEST);
    tap_settings();
    CHECK(!servo_screen_list()->open);
    servo_screen_set_armed(false);
    tap_settings();
    CHECK(servo_screen_list()->open);

    fresh_armed();
    go(SERVO_FN_LIMRUN);
    tap(SV_RUN_SET_X, SV_RUN_SET_Y(1));
    CHECK_EQ(servo_screen_run_settings(), 0);
    servo_screen_set_armed(false);
    tap(SV_RUN_SET_X, SV_RUN_SET_Y(1));
    CHECK_EQ(servo_screen_run_settings(), 2);
}

/* A list or a page of rows open on a SAFE bench closes in the frame after
 * the bench arms: TEST's list, LIMIT RUN's and KST's EXPERT MODE page
 * alike. */
TEST_CASE(a_page_of_rows_closes_as_the_bench_arms)
{
    static const servo_fn_t k_fn[] = {
        SERVO_FN_TEST, SERVO_FN_LIMRUN, SERVO_FN_KST,
    };
    for (size_t i = 0; i < sizeof(k_fn) / sizeof(k_fn[0]); ++i) {
        fresh();
        go(k_fn[i]);
        tap_settings();
        CHECK(servo_screen_run_settings() != 0 || servo_screen_list()->open);
        servo_screen_set_armed(true);
        scr->tick(0.02f);
        CHECK_EQ(servo_screen_run_settings(), 0);
        CHECK(!servo_screen_list()->open);
    }
}

/* A finger on RUDDER's ZERO keeps SETTINGS shut, so ZERO's release stays
 * RUDDER's: the list opens once the finger lifts. */
TEST_CASE(a_press_held_on_rudder_keeps_settings_shut)
{
    fresh();
    go(SERVO_FN_RUDDER);
    ev(ZERO_X, SV_RUD_ZERO_Y, TOUCH_EVENT_DOWN, 1);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_UP, 2);
    CHECK(!servo_screen_list()->open);
    ev(ZERO_X, SV_RUD_ZERO_Y, TOUCH_EVENT_UP, 1);
    tap_settings();
    CHECK(servo_screen_list()->open);
}

/* The same for a finger on one of RUDDER AUTO's rows. */
TEST_CASE(a_press_held_on_rudder_auto_keeps_settings_shut)
{
    fresh();
    go(SERVO_FN_RAUTO);
    ev(SV_RA_L_X, SV_RA_ROW_Y(1), TOUCH_EVENT_DOWN, 1);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_UP, 2);
    CHECK(!servo_screen_list()->open);
    ev(SV_RA_L_X, SV_RA_ROW_Y(1), TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_list()->open);
}

/* A finger on a control of KST's session keeps the PORT list shut: no port
 * picked under the finger reaches the session (CONNECT's press alike). */
TEST_CASE(a_finger_on_a_kst_control_keeps_the_port_list_shut)
{
    fresh();
    go(SERVO_FN_KST);
    CHECK(kst_panel_is_open());
    const gfx_rect_t r = kst_panel_close_rect();
    ev(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
    CHECK(kst_panel_pressed());
    ui_router_set_alert(NULL);
    ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_UP, 2);
    CHECK_EQ(servo_screen_port_choices(), -1);
    CHECK_STR_EQ(alert(), "");
    ev(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 1);
}

/* The PORT list open, a finger on the supply's OUTPUT button: a tap on a
 * port picks nothing.  Lifted, the pick is taken. */
TEST_CASE(a_finger_on_output_takes_no_port_pick)
{
    fresh();
    go(SERVO_FN_LIVE);
    tap(SV_PORT_X, SV_PORT_Y);
    CHECK(servo_screen_port_choices() > 0);
    ev(SV_SUP_OUT_X, SV_SUP_Y, TOUCH_EVENT_DOWN, 1);
    const gfx_rect_t c = servo_screen_port_choice_rect(2);
    ev(c.x + c.w / 2, c.y + c.h / 2, TOUCH_EVENT_DOWN, 2);
    ev(c.x + c.w / 2, c.y + c.h / 2, TOUCH_EVENT_UP, 2);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    ev(SV_SUP_OUT_X, SV_SUP_Y, TOUCH_EVENT_UP, 1);
    if (servo_screen_port_choices() < 0) {
        tap(SV_PORT_X, SV_PORT_Y);
    }
    const gfx_rect_t d = servo_screen_port_choice_rect(2);
    tap(d.x + d.w / 2, d.y + d.h / 2);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 2);
}

int main(void)
{
    RUN(the_run_cards_settings_open_nothing_while_armed);
    RUN(a_page_of_rows_closes_as_the_bench_arms);
    RUN(a_press_held_on_rudder_keeps_settings_shut);
    RUN(a_press_held_on_rudder_auto_keeps_settings_shut);
    RUN(a_finger_on_a_kst_control_keeps_the_port_list_shut);
    RUN(a_finger_on_output_takes_no_port_pick);
    return test_summary("servo_list_guards");
}
