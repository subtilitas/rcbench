/**
 * @file test_supply_stop_fingers.c
 * @brief SUPPLY's stops act on the press, whatever other fingers hold:
 *        OUTPUT OFF and PROG 5V OFF switch off as the contact comes down,
 *        the only contact on the glass with track id 1, 2 or 3, or a
 *        further one while one contact holds the screen's press and
 *        others rest on the glass.  A slide off the button and a touch
 *        loss ask nothing more and take nothing back.  The press held
 *        stays its contact's; a drag on a set point's track under OUTPUT
 *        OFF ends there.
 *
 * Cases:
 * - output_off_acts_on_the_press_of_a_lone_contact_1_2_or_3
 * - output_off_pressed_then_slid_off_has_switched_off
 * - output_off_pressed_then_touch_lost_has_switched_off
 * - prog5v_off_acts_on_the_press_of_a_lone_contact_1_2_or_3
 * - prog5v_off_pressed_then_slid_off_has_switched_off
 * - prog5v_off_pressed_then_touch_lost_has_switched_off
 * - output_pressed_while_on_is_asked_takes_the_on_back
 * - a_hold_on_output_on_ends_with_the_stop_of_an_on_asked_elsewhere
 * - a_completed_hold_under_a_stop_asks_no_second_on
 * - prog5v_pressed_while_on_is_asked_takes_the_on_back
 * - a_hold_on_prog5v_on_ends_with_the_stop_of_an_asked_on
 * - output_off_under_two_other_contacts_switches_off
 * - output_off_under_three_other_contacts_switches_off
 * - prog5v_off_under_two_and_three_other_contacts_switches_off
 * - a_held_output_on_switches_nothing_on_after_the_stop
 * - a_held_prog5v_on_switches_nothing_on_after_the_stop
 * - a_drag_under_output_off_ends_and_its_lift_sets_nothing
 *
 * SPDX-License-Identifier: MIT
 */

#include "supply_screen_rig.h"

#include "prog5v_box.h"

/* PROG 5V's button, as test_prog5v_supply.c mirrors it from
 * supply_screen_int.h: the right end of the box over the legend. */
#define P5_BTN_X (618 - PROG5V_BOX_BTN_W / 2)
#define P5_BTN_Y (126 - PROG5V_BOX_H / 2)

enum { ST_OFF = 2, ST_ON = 3 };

/** @brief SUPPLY fresh with its output reported on. */
static void fresh_on(void)
{
    fresh();
    supply_screen_set_output(true);
    scr->tick(TICK_S);
    drain();
}

/** @brief Whether an OFF is waiting, and no ON with it. */
static bool asked_off(void)
{
    supply_cmd_t c = { 0 };
    return supply_screen_poll_cmd(&c) && c.off && !c.on;
}

/** @brief Whether PROG 5V's OFF is waiting, and no ON with it. */
static bool p5_asked_off(void)
{
    prog5v_cmd_t c;
    return prog5v_box_poll_cmd(&c) && c.off && !c.on;
}

/** @brief SUPPLY fresh with PROG 5V read ON and no command waiting. */
static void fresh_p5_on(void)
{
    prog5v_box_reset();
    ui_text_set_language(UI_LANG_EN);
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    fresh();
    scr->tick(TICK_S);
    while (prog5v_box_poll_cmd(NULL)) { }
}

/* The only contact on the glass, with track id 1, 2 or 3: OUTPUT OFF is
 * asked on its DOWN, before any tick, move or lift; its lift on the button
 * asks nothing more. */
TEST_CASE(output_off_acts_on_the_press_of_a_lone_contact_1_2_or_3)
{
    for (uint8_t id = 1; id <= 3; ++id) {
        fresh_on();
        ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, id);
        CHECK(asked_off());
        tick_for(HOLD_TICKS);
        ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, id);
        tick_for(HOLD_TICKS);
        CHECK(!supply_screen_poll_cmd(NULL));
    }
}

/* The finger slides off OUTPUT OFF and lifts outside it: the OFF of the
 * press stands, with the output still reported on throughout, and neither
 * the move nor the lift asks an ON. */
TEST_CASE(output_off_pressed_then_slid_off_has_switched_off)
{
    for (uint8_t id = 1; id <= 3; ++id) {
        fresh_on();
        ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, id);
        ev(OFF_X, OFF_Y, TOUCH_EVENT_MOVE, id);
        ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, id);
        CHECK(asked_off());
        tick_for(2 * HOLD_TICKS);
        CHECK(!supply_screen_poll_cmd(NULL));
    }
}

/* The touch stream breaks under the press: the OFF was asked on the DOWN,
 * the loss asks no second one, and the screen takes the next press. */
TEST_CASE(output_off_pressed_then_touch_lost_has_switched_off)
{
    for (uint8_t id = 1; id <= 3; ++id) {
        fresh_on();
        ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, id);
        scr->cancel();
        CHECK(asked_off());
        tick_for(2 * HOLD_TICKS);
        CHECK(!supply_screen_poll_cmd(NULL));
        ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, id);
        CHECK(asked_off());
        ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, id);
    }
}

/* PROG 5V reads ON: the only contact, id 1, 2 or 3, asks OFF on its DOWN;
 * its lift on the button asks nothing more. */
TEST_CASE(prog5v_off_acts_on_the_press_of_a_lone_contact_1_2_or_3)
{
    for (uint8_t id = 1; id <= 3; ++id) {
        fresh_p5_on();
        ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_DOWN, id);
        CHECK(p5_asked_off());
        tick_for(HOLD_TICKS);
        ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_UP, id);
        tick_for(HOLD_TICKS);
        CHECK(!prog5v_box_poll_cmd(NULL));
    }
}

/* The finger slides off PROG 5V OFF and lifts outside it: the OFF of the
 * press stands and no ON follows. */
TEST_CASE(prog5v_off_pressed_then_slid_off_has_switched_off)
{
    for (uint8_t id = 1; id <= 3; ++id) {
        fresh_p5_on();
        ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_DOWN, id);
        ev(OFF_X, OFF_Y, TOUCH_EVENT_MOVE, id);
        ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, id);
        CHECK(p5_asked_off());
        tick_for(2 * HOLD_TICKS);
        CHECK(!prog5v_box_poll_cmd(NULL));
    }
}

/* The touch stream breaks under the press on PROG 5V OFF: the OFF asked on
 * the DOWN stays asked, and no ON follows. */
TEST_CASE(prog5v_off_pressed_then_touch_lost_has_switched_off)
{
    for (uint8_t id = 1; id <= 3; ++id) {
        fresh_p5_on();
        ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_DOWN, id);
        scr->cancel();
        CHECK(p5_asked_off());
        tick_for(2 * HOLD_TICKS);
        CHECK(!prog5v_box_poll_cmd(NULL));
    }
}

/* OUTPUT ON's hold has asked ON and the supply has not reported it: the
 * button is OUTPUT OFF.  A press there, by contact 1, 2 or 3, takes the ON
 * back on the DOWN, whether the ON still waits to be collected or went
 * out, and a finger left on it asks no ON. */
TEST_CASE(output_pressed_while_on_is_asked_takes_the_on_back)
{
    for (int collected = 0; collected < 2; ++collected) {
        for (uint8_t id = 1; id <= 3; ++id) {
            supply_cmd_t c = { 0 };

            fresh();
            hold_on();
            if (collected) {
                CHECK(supply_screen_poll_cmd(&c) && c.on);
            }
            CHECK(supply_screen_output_live());
            ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, id);
            CHECK(asked_off());
            CHECK(!supply_screen_output_live());
            tick_for(2 * HOLD_TICKS);
            ev(OFF_X, OFF_Y, TOUCH_EVENT_MOVE, id);
            ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, id);
            tick_for(HOLD_TICKS);
            CHECK(!supply_screen_poll_cmd(NULL));
        }
    }
}

/* One contact holds OUTPUT ON, 1s in; SERVO asks ON meanwhile, so the
 * button reads OUTPUT OFF, and the other contact presses it.  The OFF is
 * asked and the first contact's hold ends there: held on for 4s more and
 * then lifted, it asks no ON.  With the track ids either way round. */
TEST_CASE(a_hold_on_output_on_ends_with_the_stop_of_an_on_asked_elsewhere)
{
    for (int swap = 0; swap < 2; ++swap) {
        const uint8_t a = swap ? 2 : 1, b = swap ? 1 : 2;

        fresh();
        ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, a);
        tick_for(HOLD_TICKS / 2);
        supply_screen_ask_on(SUPPLY_BY_SERVO);
        CHECK(supply_screen_output_live());
        ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, b);
        CHECK(asked_off());
        tick_for(2 * HOLD_TICKS);
        CHECK(!supply_screen_poll_cmd(NULL));
        ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, b);
        ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, a);
        tick_for(HOLD_TICKS);
        CHECK(!supply_screen_poll_cmd(NULL));
    }
}

/* One contact's hold on OUTPUT ON has completed and asked ON; still down,
 * the other contact presses the button, now OUTPUT OFF.  The OFF is asked
 * and the first contact, held on and then lifted, asks no second ON. */
TEST_CASE(a_completed_hold_under_a_stop_asks_no_second_on)
{
    for (int swap = 0; swap < 2; ++swap) {
        const uint8_t a = swap ? 2 : 1, b = swap ? 1 : 2;
        supply_cmd_t c = { 0 };

        fresh();
        ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, a);
        tick_for(HOLD_TICKS);
        CHECK(supply_screen_poll_cmd(&c) && c.on);
        ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, b);
        CHECK(asked_off());
        tick_for(2 * HOLD_TICKS);
        CHECK(!supply_screen_poll_cmd(NULL));
        ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, a);
        ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, b);
        tick_for(HOLD_TICKS);
        CHECK(!supply_screen_poll_cmd(NULL));
    }
}

/** @brief SUPPLY fresh with PROG 5V read OFF and no command waiting. */
static void fresh_p5_off(void)
{
    prog5v_box_reset();
    ui_text_set_language(UI_LANG_EN);
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    fresh();
    scr->tick(TICK_S);
    while (prog5v_box_poll_cmd(NULL)) { }
}

/* PROG 5V ON's hold has asked ON and the port has not reported it: the
 * button is PROG 5V OFF (prog5v_box_stops()).  A press there, by contact
 * 1, 2 or 3, takes the ON back on the DOWN, whether the ON still waits to
 * be collected or went out and the application says it is coming, and a
 * finger left on it asks no ON. */
TEST_CASE(prog5v_pressed_while_on_is_asked_takes_the_on_back)
{
    for (int collected = 0; collected < 2; ++collected) {
        for (uint8_t id = 1; id <= 3; ++id) {
            prog5v_cmd_t c;

            fresh_p5_off();
            CHECK(!prog5v_box_stops());
            ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_DOWN, 1);
            tick_for(HOLD_TICKS);
            ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_UP, 1);
            if (collected) {
                CHECK(prog5v_box_poll_cmd(&c) && c.on);
                prog5v_box_set_on_coming(true);
            }
            CHECK(prog5v_box_stops());
            ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_DOWN, id);
            CHECK(p5_asked_off());
            CHECK(!prog5v_box_stops());
            tick_for(2 * HOLD_TICKS);
            ev(OFF_X, OFF_Y, TOUCH_EVENT_MOVE, id);
            ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, id);
            tick_for(HOLD_TICKS);
            CHECK(!prog5v_box_poll_cmd(NULL));
        }
    }
}

/* One contact holds PROG 5V ON, 1s in; SERVO asks ON meanwhile, so the
 * button reads PROG 5V OFF, and the other contact presses it.  The OFF is
 * asked and the first contact's hold ends there: held on for 4s more and
 * then lifted, it asks no ON.  And with the first contact's own hold
 * completed and still down.  With the track ids either way round. */
TEST_CASE(a_hold_on_prog5v_on_ends_with_the_stop_of_an_asked_on)
{
    for (int own = 0; own < 2; ++own) {
        for (int swap = 0; swap < 2; ++swap) {
            const uint8_t a = swap ? 2 : 1, b = swap ? 1 : 2;

            fresh_p5_off();
            ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_DOWN, a);
            if (own) {
                tick_for(HOLD_TICKS);
            } else {
                tick_for(HOLD_TICKS / 2);
                prog5v_box_ask_on(PROG5V_BY_SERVO);
            }
            CHECK(prog5v_box_stops());
            ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_DOWN, b);
            CHECK(p5_asked_off());
            tick_for(2 * HOLD_TICKS);
            CHECK(!prog5v_box_poll_cmd(NULL));
            ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_UP, b);
            ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_UP, a);
            tick_for(HOLD_TICKS);
            CHECK(!prog5v_box_poll_cmd(NULL));
        }
    }
}

/* Contact 1 holds RESET PEAKS, contact 2 rests on the voltage's track; a
 * third on OUTPUT OFF switches it off on its press.  RESET PEAKS keeps its
 * release: its tap resets the peaks. */
TEST_CASE(output_off_under_two_other_contacts_switches_off)
{
    fresh_on();
    ev(RESET_X, RESET_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 60, V_ROW_Y, TOUCH_EVENT_DOWN, 2);
    CHECK(!supply_screen_poll_cmd(NULL));
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 3);
    CHECK(asked_off());
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 3);
    ev(TRACK_X + 60, V_ROW_Y, TOUCH_EVENT_UP, 2);
    ev(RESET_X, RESET_Y, TOUCH_EVENT_UP, 1);
    supply_cmd_t c = { 0 };
    CHECK(supply_screen_poll_cmd(&c) && c.reset);
}

/* Contact 1 drags the voltage's track, contacts 2 and 3 rest on RESET
 * PEAKS and the plot; a fourth on OUTPUT OFF switches it off on its press,
 * and a fifth there asks it again. */
TEST_CASE(output_off_under_three_other_contacts_switches_off)
{
    fresh_on();
    ev(TRACK_X + 60, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 64, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    ev(RESET_X, RESET_Y, TOUCH_EVENT_DOWN, 2);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_DOWN, 3);
    drain();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 4);
    CHECK(asked_off());
    ev(OUT_X - 10, OUT_Y, TOUCH_EVENT_DOWN, 5);
    CHECK(asked_off());
    for (uint8_t id = 5; id >= 1; --id) {
        ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, id);
    }
}

/* PROG 5V reads ON: with contact 1 on RESET PEAKS and contact 2 on the
 * plot, a third on PROG 5V OFF asks OFF on its press; with a third resting
 * on the voltage's track, a fourth does. */
TEST_CASE(prog5v_off_under_two_and_three_other_contacts_switches_off)
{
    prog5v_box_reset();
    ui_text_set_language(UI_LANG_EN);
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    fresh();
    scr->tick(TICK_S);
    while (prog5v_box_poll_cmd(NULL)) { }

    ev(RESET_X, RESET_Y, TOUCH_EVENT_DOWN, 1);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_DOWN, 2);
    ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_DOWN, 3);
    CHECK(p5_asked_off());
    ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_UP, 3);

    ev(TRACK_X + 60, V_ROW_Y, TOUCH_EVENT_DOWN, 3);
    ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_DOWN, 4);
    CHECK(p5_asked_off());
    for (uint8_t id = 4; id >= 1; --id) {
        ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, id);
    }
}

/* Contact 1 holds OUTPUT ON; the output goes on from elsewhere before the
 * hold completes, and contact 2 presses OUTPUT OFF.  The OFF is asked and
 * reported, and contact 1, still down and then lifted, switches nothing
 * back on. */
TEST_CASE(a_held_output_on_switches_nothing_on_after_the_stop)
{
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    supply_screen_set_output(true);
    scr->tick(TICK_S);
    drain();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 2);
    CHECK(asked_off());
    supply_screen_set_output(false);
    tick_for(2 * HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(NULL));
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 2);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(NULL));
}

/* The same for PROG 5V: contact 1 holds PROG 5V ON, the 5V goes on from
 * elsewhere, contact 2 presses PROG 5V OFF.  After the OFF reads back,
 * contact 1 asks no ON, held or lifted. */
TEST_CASE(a_held_prog5v_on_switches_nothing_on_after_the_stop)
{
    prog5v_box_reset();
    ui_text_set_language(UI_LANG_EN);
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    fresh();
    scr->tick(TICK_S);
    ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    scr->tick(TICK_S);
    while (prog5v_box_poll_cmd(NULL)) { }
    ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_DOWN, 2);
    CHECK(p5_asked_off());
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    tick_for(2 * HOLD_TICKS);
    CHECK(!prog5v_box_poll_cmd(NULL));
    ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_UP, 2);
    ev(P5_BTN_X, P5_BTN_Y, TOUCH_EVENT_UP, 1);
    tick_for(HOLD_TICKS);
    CHECK(!prog5v_box_poll_cmd(NULL));
}

/* Contact 1 drags the voltage's track; contact 2 presses OUTPUT OFF, which
 * is asked on its press and reported.  The drag ends there: contact 1's
 * later moves and its lift leave the set point where the drag had put it,
 * and ask nothing of the supply. */
TEST_CASE(a_drag_under_output_off_ends_and_its_lift_sets_nothing)
{
    fresh_on();
    ev(TRACK_X + 60, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 64, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    scr->tick(TICK_S);
    drain();
    const float v0 = supply_screen_set_v();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 2);
    CHECK(asked_off());
    supply_screen_set_output(false);
    scr->tick(TICK_S);
    drain();
    ev(TRACK_X + TRACK_W - 20, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    ev(TRACK_X + TRACK_W - 20, V_ROW_Y, TOUCH_EVENT_UP, 1);
    scr->tick(TICK_S);
    CHECK_NEAR(supply_screen_set_v(), v0, 0.0001f);
    CHECK(!supply_screen_poll_cmd(NULL));
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 2);
    scr->tick(TICK_S);
    CHECK(!supply_screen_poll_cmd(NULL));
    CHECK_NEAR(supply_screen_set_v(), v0, 0.0001f);
}

int main(void)
{
    RUN(output_off_acts_on_the_press_of_a_lone_contact_1_2_or_3);
    RUN(output_off_pressed_then_slid_off_has_switched_off);
    RUN(output_off_pressed_then_touch_lost_has_switched_off);
    RUN(prog5v_off_acts_on_the_press_of_a_lone_contact_1_2_or_3);
    RUN(prog5v_off_pressed_then_slid_off_has_switched_off);
    RUN(prog5v_off_pressed_then_touch_lost_has_switched_off);
    RUN(output_pressed_while_on_is_asked_takes_the_on_back);
    RUN(a_hold_on_output_on_ends_with_the_stop_of_an_on_asked_elsewhere);
    RUN(a_completed_hold_under_a_stop_asks_no_second_on);
    RUN(prog5v_pressed_while_on_is_asked_takes_the_on_back);
    RUN(a_hold_on_prog5v_on_ends_with_the_stop_of_an_asked_on);
    RUN(output_off_under_two_other_contacts_switches_off);
    RUN(output_off_under_three_other_contacts_switches_off);
    RUN(prog5v_off_under_two_and_three_other_contacts_switches_off);
    RUN(a_held_output_on_switches_nothing_on_after_the_stop);
    RUN(a_held_prog5v_on_switches_nothing_on_after_the_stop);
    RUN(a_drag_under_output_off_ends_and_its_lift_sets_nothing);
    return test_summary("supply_stop_fingers");
}
