/*
 * The Hitec programmer's CONNECT on the rbmod carrier with its input
 * supply low (prog5v_box_input_low_mv()): refused with the input named and
 * the session left in PWM, at 5.1V; taken at 15.0V.  The KST programmer's
 * is test_prog5v_input's.  The rig is hitec_panel_rig.h.
 *
 * Cases:
 * - connect_is_refused_while_the_input_is_low
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel_rig.h"

#include "prog5v_box.h"
#include "splash_screen.h"
#include "ui_screen.h"

/** @brief The router on SUPPLY, a screen with the band: a refusal's notice
 *         shows only there (ui_action_refused()). */
static void band_live(void)
{
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SUPPLY);
}

TEST_CASE(connect_is_refused_while_the_input_is_low)
{
    band_live();
    open_en();
    prog5v_box_reset();
    prog5v_box_publish(2u, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_publish_input(5100u);
    ui_router_set_alert(NULL);
    CHECK_EQ(pstate(), HITEC_PROG_PWM);
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    prun(200u);
    CHECK_EQ(pstate(), HITEC_PROG_PWM);
    CHECK(ui_router_alert() != NULL);
    if (ui_router_alert() != NULL) {
        CHECK_STR_EQ(ui_router_alert(),
                     "INPUT 5.1V: NEEDS 15V. CHECK THE SUPPLY VOLTAGE");
    }
    prog5v_box_publish_input(15000u);
    ui_router_set_alert(NULL);
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    CHECK(pstate() != HITEC_PROG_PWM);
    prog5v_box_reset();
}

int main(void)
{
    RUN(connect_is_refused_while_the_input_is_low);
    return test_summary("prog5v_input_hitec");
}
