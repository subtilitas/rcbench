/**
 * @file test_prog5v_supply.c
 * @brief The PROG 5V box on SUPPLY's LIVE task: drawn in the output card's
 *        head, its hold, its lock, a stop, a port taking the 5V and
 *        leaving the screen.
 *
 * Cases:
 * - the_box_draws_on_live_and_its_hold_asks_on_at_2000ms
 * - a_press_while_a_port_holds_the_5v_says_why
 * - a_slide_off_a_stop_or_a_port_taking_it_ends_the_hold
 * - leaving_supply_switches_off_what_its_box_switched_on
 *
 * SPDX-License-Identifier: MIT
 */

#include "supply_screen_rig.h"

#include "prog5v_box.h"

/* PROG 5V's button, mirrored from supply_screen_int.h: the box ends at the
 * left card's inner edge, x 618, and stands PROG5V_BOX_H over the legend
 * at y 126; the button is its right end. */
#define P5_BTN_X (618 - PROG5V_BOX_BTN_W / 2)
#define P5_BTN_Y (126 - PROG5V_BOX_H / 2)

enum { ST_OFF = 2, ST_ON = 3 };

/* SUPPLY fresh on a board that switches the ports' 5V, which reads
 * @p state with no port holding it. */
static void fresh_p5(uint8_t state)
{
    prog5v_box_reset();
    ui_text_set_language(UI_LANG_EN);
    prog5v_box_publish(state, 21u, (uint8_t)PROG5V_HELD_NONE);
    fresh();
    ui_router_set_alert(NULL);
    scr->tick(0.02f);
}

static int p5x(void) { return P5_BTN_X; }
static int p5y(void) { return P5_BTN_Y; }

static void ticks(int n, float dt_s)
{
    for (int i = 0; i < n; ++i) {
        scr->tick(dt_s);
    }
}

static bool asked_on(void)
{
    prog5v_cmd_t c;
    return prog5v_box_poll_cmd(&c) && c.on && !c.off
           && c.by == PROG5V_BY_SUPPLY;
}

static bool asked_off(void)
{
    prog5v_cmd_t c;
    return prog5v_box_poll_cmd(&c) && c.off && !c.on;
}

TEST_CASE(the_box_draws_on_live_and_its_hold_asks_on_at_2000ms)
{
    fresh_p5(ST_OFF);
    scr->render(&cv, 0);
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
    /* The switch gone: the card is drawn again without the box. */
    prog5v_box_publish(0u, 0u, (uint8_t)PROG5V_HELD_NONE);
    scr->tick(0.02f);
    scr->render(&cv, 0);
    CHECK(gfx_pixel_get(&cv, p5x() - 50, p5y() - 20)
          != ui_theme_color(UI_C_OK));
}

TEST_CASE(a_press_while_a_port_holds_the_5v_says_why)
{
    fresh_p5(ST_ON);
    prog5v_box_publish(ST_ON, 21u, 3u);       /* PROG PORT 4 holds it */
    scr->tick(0.02f);
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 1);
    CHECK(ui_router_alert() != NULL);
    CHECK_STR_EQ(ui_router_alert(), ui_tr(TX_P5_LOCKED));
    ev(p5x(), p5y(), TOUCH_EVENT_UP, 1);
    ticks(10, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
}

TEST_CASE(a_slide_off_a_stop_or_a_port_taking_it_ends_the_hold)
{
    fresh_p5(ST_OFF);
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 1);
    ticks(4, 0.25f);
    ev(p5x(), p5y() + PROG5V_BOX_H, TOUCH_EVENT_MOVE, 1);
    ticks(8, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
    ev(p5x(), p5y() + PROG5V_BOX_H, TOUCH_EVENT_UP, 1);
    /* A stop under the hold. */
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 1);
    ticks(4, 0.25f);
    supply_screen_cancel_on();
    ticks(8, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
    ev(p5x(), p5y(), TOUCH_EVENT_UP, 1);
    /* A port takes the 5V under the hold. */
    ev(p5x(), p5y(), TOUCH_EVENT_DOWN, 1);
    ticks(4, 0.25f);
    prog5v_box_publish(ST_OFF, 21u, 1u);
    scr->tick(0.02f);
    ticks(8, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
    ev(p5x(), p5y(), TOUCH_EVENT_UP, 1);
}

TEST_CASE(leaving_supply_switches_off_what_its_box_switched_on)
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
}

int main(void)
{
    RUN(the_box_draws_on_live_and_its_hold_asks_on_at_2000ms);
    RUN(a_press_while_a_port_holds_the_5v_says_why);
    RUN(a_slide_off_a_stop_or_a_port_taking_it_ends_the_hold);
    RUN(leaving_supply_switches_off_what_its_box_switched_on);
    return test_summary("prog5v_supply");
}
