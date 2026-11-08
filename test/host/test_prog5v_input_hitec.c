/*
 * The Hitec programmer's CONNECT on the rbmod carrier with its input
 * supply low (prog5v_box_input_low_mv()): refused with the input named and
 * the session left in PWM, at 5.1V; taken at 15.0V.  CONNECT as a 2s hold:
 * a tap shows the hint, a finger that leaves or a touch loss connects
 * nothing, a press refused at a low input takes no hold, an input gone low
 * under the hold ends it with the reason.  The KST programmer's is
 * test_prog5v_input's.  The rig is hitec_panel_rig.h.
 *
 * Cases:
 * - connect_is_refused_while_the_input_is_low
 * - connect_is_a_2s_hold
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
    press_port();
    prun(200u);
    CHECK_EQ(pstate(), HITEC_PROG_PWM);
    CHECK(ui_router_alert() != NULL);
    if (ui_router_alert() != NULL) {
        CHECK_STR_EQ(ui_router_alert(),
                     "INPUT 5.1V: NEEDS 15V. CHECK THE SUPPLY VOLTAGE");
    }
    prog5v_box_publish_input(15000u);
    ui_router_set_alert(NULL);
    press_port();
    CHECK(pstate() != HITEC_PROG_PWM);
    prog5v_box_reset();
}

/** @brief The overlay's frames for @p secs, 20ms each.  @param secs s */
static void frames(float secs)
{
    for (int i = 0; i < (int)(secs * 50.0f + 0.5f); ++i) {
        hitec_panel_tick(0.02f);
    }
}

TEST_CASE(connect_is_a_2s_hold)
{
    const gfx_rect_t r = val(HITEC_PANEL_SERVO, 0, 0);
    const char *why = "INPUT 5.1V: NEEDS 15V. CHECK THE SUPPLY VOLTAGE";

    band_live();
    open_en();
    prog5v_box_reset();
    prog5v_box_publish(2u, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_publish_input(15000u);
    /* A tap: the hint, no session. */
    ui_router_set_alert(NULL);
    tap(r);
    frames(0.1f);
    CHECK_EQ(pstate(), HITEC_PROG_PWM);
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), TR(HOLD_HINT_CONNECT)) == 0);
    /* 1s, then the finger leaves: nothing, no hint. */
    ui_router_set_alert(NULL);
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    frames(1.0f);
    ev(r.x - 20, cy(r), TOUCH_EVENT_MOVE);
    frames(1.5f);
    ev(r.x - 20, cy(r), TOUCH_EVENT_UP);
    CHECK_EQ(pstate(), HITEC_PROG_PWM);
    CHECK(ui_router_alert() == NULL);
    /* 1s, then the touch is lost. */
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    frames(1.0f);
    hitec_panel_cancel();
    frames(1.5f);
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    CHECK_EQ(pstate(), HITEC_PROG_PWM);
    /* Pressed at 5.1V: refused, and no hold runs after the input is back. */
    prog5v_box_publish_input(5100u);
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), why) == 0);
    prog5v_box_publish_input(15000u);
    frames(2.5f);
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    CHECK_EQ(pstate(), HITEC_PROG_PWM);
    /* Gone low at 1s: the hold ends with the reason and stays ended. */
    ui_router_set_alert(NULL);
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    frames(1.0f);
    prog5v_box_publish_input(5100u);
    frames(0.1f);
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), why) == 0);
    prog5v_box_publish_input(15000u);
    frames(1.5f);
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    CHECK_EQ(pstate(), HITEC_PROG_PWM);
    /* Held 2s: the session starts while the finger is still down. */
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    frames(2.1f);
    CHECK(pstate() != HITEC_PROG_PWM);
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    CHECK(pstate() != HITEC_PROG_PWM);
    prog5v_box_reset();
}

int main(void)
{
    RUN(connect_is_refused_while_the_input_is_low);
    RUN(connect_is_a_2s_hold);
    return test_summary("prog5v_input_hitec");
}
