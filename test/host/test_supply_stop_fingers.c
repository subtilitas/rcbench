/**
 * @file test_supply_stop_fingers.c
 * @brief SUPPLY's stops count whatever other fingers hold: OUTPUT OFF and
 *        PROG 5V OFF, pressed by a further contact while one contact holds
 *        the screen's press and others rest on the glass, switch off on
 *        that press.  The press held stays its contact's; a drag on a
 *        set point's track under OUTPUT OFF ends there.
 *
 * Cases:
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
    RUN(output_off_under_two_other_contacts_switches_off);
    RUN(output_off_under_three_other_contacts_switches_off);
    RUN(prog5v_off_under_two_and_three_other_contacts_switches_off);
    RUN(a_held_output_on_switches_nothing_on_after_the_stop);
    RUN(a_held_prog5v_on_switches_nothing_on_after_the_stop);
    RUN(a_drag_under_output_off_ends_and_its_lift_sets_nothing);
    return test_summary("supply_stop_fingers");
}
