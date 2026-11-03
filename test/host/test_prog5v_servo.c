/**
 * @file test_prog5v_servo.c
 * @brief The PROG 5V box on SERVO's output card: shown in the power plot's
 *        place, its hold, its lock, a stop and leaving the screen.
 *
 * Cases:
 * - the_box_takes_the_plots_place_and_its_hold_asks_on_at_2000ms
 * - a_press_while_a_port_holds_the_5v_says_why
 * - a_slide_off_a_stop_or_a_port_taking_it_ends_the_hold
 * - leaving_servo_switches_off_what_its_box_switched_on
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

#include "prog5v_box.h"

/* PROG 5V's button, mirrored from servo_screen_int.h: the box spans the
 * output card's inner column from P5_Y = UI_MARGIN + 90 (the type pill's
 * 12 + 44, 8, the readings' 24 and 2), the button its right end. */
#define P5_BTN_X (SV_RC_X + SV_RC_W - PROG5V_BOX_BTN_W / 2)
#define P5_BTN_Y (UI_MARGIN + 90 + PROG5V_BOX_H / 2)

enum { ST_OFF = 2, ST_ON = 3 };

/* SERVO fresh on a board that switches the ports' 5V, which reads
 * @p state with no port holding it. */
static void fresh_p5(uint8_t state)
{
    prog5v_box_reset();
    ui_text_set_language(UI_LANG_EN);
    prog5v_box_publish(state, 21u, (uint8_t)PROG5V_HELD_NONE);
    fresh();
    ui_router_set_alert(NULL);
}

/* The button's centre. */
static int p5x(void) { return P5_BTN_X; }
static int p5y(void) { return P5_BTN_Y; }

/* @p n frames of @p dt_s. */
static void ticks(int n, float dt_s)
{
    for (int i = 0; i < n; ++i) {
        scr->tick(dt_s);
    }
}

static bool asked_on(void)
{
    prog5v_cmd_t c;
    return prog5v_box_poll_cmd(&c) && c.on && !c.off && c.by == PROG5V_BY_SERVO;
}

static bool asked_off(void)
{
    prog5v_cmd_t c;
    return prog5v_box_poll_cmd(&c) && c.off && !c.on;
}

TEST_CASE(the_box_takes_the_plots_place_and_its_hold_asks_on_at_2000ms)
{
    fresh_p5(ST_OFF);
    scr->render(&cv, 0);
    /* The button's ON fill lands where the plot was. */
    CHECK_EQ(gfx_pixel_get(&cv, p5x() - 50, p5y() - 20),
             ui_theme_color(UI_C_OK));
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 1);
    ticks(7, 0.25f);
    scr->tick(0.249f);                        /* 1,999ms */
    CHECK(!prog5v_box_poll_cmd(NULL));
    scr->tick(0.001f);                        /* 2,000ms */
    CHECK(asked_on());
    ev(p5x(), p5y(), TOUCH_EVENT_UP, 1);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* Read ON: a tap asks OFF. */
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    scr->tick(0.02f);
    tap(p5x(), p5y());
    CHECK(asked_off());
    /* Without the switch: the plot is back, the spot is no button. */
    prog5v_box_publish(0u, 0u, (uint8_t)PROG5V_HELD_NONE);
    scr->tick(0.02f);
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 2);
    ticks(10, 0.25f);
    ev(p5x(), p5y(), TOUCH_EVENT_UP, 2);
    CHECK(!prog5v_box_poll_cmd(NULL));
}

TEST_CASE(a_press_while_a_port_holds_the_5v_says_why)
{
    fresh_p5(ST_ON);
    prog5v_box_publish(ST_ON, 21u, 0u);       /* PROG PORT 1 holds it */
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 1);
    CHECK(ui_router_alert() != NULL);
    CHECK_STR_EQ(ui_router_alert(), ui_tr(TX_P5_LOCKED));
    ev(p5x(), p5y(), TOUCH_EVENT_UP, 1);
    ticks(10, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* A second contact while the first owns the button: its UP ends
     * nothing, and the first's hold completes. */
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    scr->tick(0.02f);
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 1);
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 2);
    ev(p5x(), p5y(), TOUCH_EVENT_UP, 2);
    ticks(8, 0.25f);
    CHECK(asked_on());
    ev(p5x(), p5y(), TOUCH_EVENT_UP, 1);
}

TEST_CASE(a_slide_off_a_stop_or_a_port_taking_it_ends_the_hold)
{
    fresh_p5(ST_OFF);
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 1);
    ticks(4, 0.25f);
    ev(p5x(), p5y() - PROG5V_BOX_H, TOUCH_EVENT_MOVE, 1);
    ticks(8, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
    ev(p5x(), p5y() - PROG5V_BOX_H, TOUCH_EVENT_UP, 1);
    /* A stop under the hold. */
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 1);
    ticks(4, 0.25f);
    servo_screen_cancel_arm();
    ticks(8, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
    ev(p5x(), p5y(), TOUCH_EVENT_UP, 1);
    /* A port takes the 5V under the hold. */
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 1);
    ticks(4, 0.25f);
    prog5v_box_publish(ST_OFF, 21u, 2u);
    scr->tick(0.02f);
    ticks(8, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
    ev(p5x(), p5y(), TOUCH_EVENT_UP, 1);
}

TEST_CASE(leaving_servo_switches_off_what_its_box_switched_on)
{
    fresh_p5(ST_OFF);
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 1);
    ticks(8, 0.25f);
    CHECK(asked_on());
    ev(p5x(), p5y(), TOUCH_EVENT_UP, 1);
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    scr->tick(0.02f);
    scr->leave();
    CHECK(asked_off());
    /* A hold under way at the leave asks nothing. */
    fresh_p5(ST_OFF);
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 1);
    ticks(4, 0.25f);
    scr->leave();
    ticks(8, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
}

int main(void)
{
    RUN(the_box_takes_the_plots_place_and_its_hold_asks_on_at_2000ms);
    RUN(a_press_while_a_port_holds_the_5v_says_why);
    RUN(a_slide_off_a_stop_or_a_port_taking_it_ends_the_hold);
    RUN(leaving_servo_switches_off_what_its_box_switched_on);
    return test_summary("prog5v_servo");
}
