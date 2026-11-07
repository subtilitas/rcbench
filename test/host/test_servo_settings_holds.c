/**
 * @file test_servo_settings_holds.c
 * @brief SERVO's SETTINGS while a programmer session holds its port: the
 *        session owns its settings until LEAVE, so the footer's SETTINGS
 *        opens no page and names the hold.
 *
 * Cases:
 * - a_session_holding_its_port_opens_no_settings
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"

/* KST's EXPERT MODE page opens on a session that holds no port; once
 * CONNECT has the port in programming, SETTINGS opens nothing, on a SAFE
 * bench with no finger down. */
TEST_CASE(a_session_holding_its_port_opens_no_settings)
{
    fresh_screen();
    program_open();
    screen_tap(SETB_X, BTN_Y);
    CHECK(servo_screen_run_settings() != 0);
    CHECK(scr->back());
    CHECK_EQ(servo_screen_run_settings(), 0);

    connected();
    CHECK(kst_panel_holds_port());
    screen_tap(SETB_X, BTN_Y);
    CHECK_EQ(servo_screen_run_settings(), 0);
}

int main(void)
{
    RUN(a_session_holding_its_port_opens_no_settings);
    return test_summary("servo_settings_holds");
}
