/*
 * The Hitec programmer's overlay stops the servo on the press, whatever
 * other fingers hold (shared/ui/hitec_panel_touch.c): MOVE SERVO while it
 * reads ON, and the FIND STOP key whose side searches, act as the contact
 * comes down, the only contact on the glass with track id 1, 2 or 3, or a
 * further one while one contact holds the overlay's press and others rest
 * on the glass.  A slide off the control and a touch loss start nothing
 * again.  The overlay's bench is hitec_panel_rig.h, its servo
 * follows the target and stops at d_block_lo.
 *
 * Cases:
 * - move_servo_on_ends_on_the_press_of_a_lone_contact_1_2_or_3
 * - move_servo_pressed_then_slid_off_or_lost_stays_ended
 * - the_searching_find_key_ends_on_the_press_of_a_lone_contact_1_2_or_3
 * - the_searching_key_pressed_then_slid_off_or_lost_stays_ended
 * - the_overlay_names_its_stops_to_the_router
 * - the_servo_screen_hands_the_press_on_a_stop_to_the_overlay
 * - abort_ends_a_read_on_the_press_of_a_lone_contact_1_2_or_3
 * - abort_ends_a_read_under_two_other_contacts
 * - move_servo_on_ends_under_two_other_contacts
 * - move_servo_on_ends_under_three_other_contacts
 * - the_searching_find_key_ends_under_two_and_three_other_contacts
 * - a_held_tap_on_move_servo_starts_nothing_after_the_stop
 * - a_held_tap_on_the_searching_key_starts_no_search_after_the_stop
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel_rig.h"

#include "hitec_live.h"
#include "hitec_move.h"
#include "servo_screen.h"

/* The rows on TRAVEL: LEFT END, FIND STOP, MOVE SERVO. */
#define LEFT_ROW 1
#define MOVE_ROW 4

/* The block of station step S-L6: position 4840. */
#define BLOCK_L 4840u

/* A touch of contact @p id at (@p x, @p y). */
static void evi(int x, int y, touch_event_type_t t, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };

    hitec_panel_event(&e);
}

/* The overlay open, a D485HW whose horn follows its target connected,
 * TRAVEL shown, MOVE SERVO's loop running. */
static void moving(void)
{
    open_at(SUPPLY_ON, UI_LANG_EN);
    servo_d485();
    g_drv.d_follow = true;
    g_drv.d_block_lo = BLOCK_L;
    g_drv.dreg[HITEC_D_REG_TARGET] = HITEC_LIVE_D_T_MID;
    sim_d_follow();
    tap_connect();
    tap(hitec_panel_tab_rect(HITEC_PANEL_TRAVEL));
    tap(val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0));
    CHECK_EQ(hitec_panel_editor(), 3);
    hold(2.1f);
    for (unsigned i = 0; i < 20000u
                         && hitec_prog_move_phase(model()) != HITEC_MOVE_RUN;
         ++i) {
        pms();
    }
    CHECK_EQ(hitec_prog_move_phase(model()), HITEC_MOVE_RUN);
    show();
}

/* Runs the model until MOVE SERVO's loop has ended, then draws. */
static void move_ended(void)
{
    for (unsigned i = 0; i < 60000u
                         && hitec_prog_move_phase(model()) != HITEC_MOVE_OFF;
         ++i) {
        pms();
    }
    until(HITEC_PROG_HELD);
    show();
}

/* Runs the model until the search has halted. */
static void find_halted(void)
{
    for (unsigned i = 0; i < 2000u && msg() != HITEC_PROG_MSG_FIND_HALT;
         ++i) {
        pms();
    }
    CHECK_EQ(msg(), HITEC_PROG_MSG_FIND_HALT);
    CHECK_EQ(hitec_prog_find_side(model()), -1);
}

/* The only contact on the glass, with track id 1, 2 or 3: MOVE SERVO's ON
 * ends the loop on its DOWN.  Its lift on the row, after the loop's end,
 * opens no page and starts nothing. */
TEST_CASE(move_servo_on_ends_on_the_press_of_a_lone_contact_1_2_or_3)
{
    for (uint8_t id = 1; id <= 3; ++id) {
        moving();
        const gfx_rect_t mv = val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0);

        evi(cx(mv), cy(mv), TOUCH_EVENT_DOWN, id);
        CHECK(!hitec_prog_move_on(model()));
        move_ended();
        evi(cx(mv), cy(mv), TOUCH_EVENT_UP, id);
        show();
        CHECK_EQ(hitec_panel_editor(), 0);
        prun(500u);
        CHECK_EQ(hitec_prog_move_phase(model()), HITEC_MOVE_OFF);
        CHECK(!hitec_panel_pressed());
    }
}

/* The finger slides off MOVE SERVO's ON and lifts outside it, or the touch
 * stream breaks under the press: the loop ended on the DOWN and stays
 * ended. */
TEST_CASE(move_servo_pressed_then_slid_off_or_lost_stays_ended)
{
    for (int lost = 0; lost < 2; ++lost) {
        for (uint8_t id = 1; id <= 3; ++id) {
            moving();
            const gfx_rect_t mv = val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0);
            const gfx_rect_t tab = hitec_panel_tab_rect(HITEC_PANEL_TRAVEL);

            evi(cx(mv), cy(mv), TOUCH_EVENT_DOWN, id);
            CHECK(!hitec_prog_move_on(model()));
            if (lost) {
                hitec_panel_cancel();
            } else {
                evi(cx(tab), cy(tab), TOUCH_EVENT_MOVE, id);
                evi(cx(tab), cy(tab), TOUCH_EVENT_UP, id);
            }
            CHECK(!hitec_panel_pressed());
            move_ended();
            CHECK_EQ(hitec_panel_editor(), 0);
            prun(500u);
            CHECK_EQ(hitec_prog_move_phase(model()), HITEC_MOVE_OFF);
        }
    }
}

/* FIND STOP searches on the left.  The only contact, id 1, 2 or 3, on the
 * left key ends the search on its DOWN; its lift on the key after the halt
 * starts no new search, and MOVE SERVO runs on. */
TEST_CASE(the_searching_find_key_ends_on_the_press_of_a_lone_contact_1_2_or_3)
{
    for (uint8_t id = 1; id <= 3; ++id) {
        moving();
        const gfx_rect_t l = hitec_panel_find_rect(false);

        tap(l);
        CHECK_EQ(hitec_prog_find_side(model()), (int)HITEC_FIND_LEFT);
        prun(200u);
        evi(cx(l), cy(l), TOUCH_EVENT_DOWN, id);
        find_halted();
        evi(cx(l), cy(l), TOUCH_EVENT_UP, id);
        prun(500u);
        CHECK_EQ(hitec_prog_find_side(model()), -1);
        CHECK(hitec_prog_move_on(model()));
    }
}

/* The finger slides off the searching key and lifts outside it, or the
 * touch stream breaks under the press: the search ended on the DOWN and
 * none starts again. */
TEST_CASE(the_searching_key_pressed_then_slid_off_or_lost_stays_ended)
{
    for (int lost = 0; lost < 2; ++lost) {
        for (uint8_t id = 1; id <= 3; ++id) {
            moving();
            const gfx_rect_t l = hitec_panel_find_rect(false);
            const gfx_rect_t tab = hitec_panel_tab_rect(HITEC_PANEL_TRAVEL);

            tap(l);
            CHECK_EQ(hitec_prog_find_side(model()), (int)HITEC_FIND_LEFT);
            prun(200u);
            evi(cx(l), cy(l), TOUCH_EVENT_DOWN, id);
            if (lost) {
                hitec_panel_cancel();
            } else {
                evi(cx(tab), cy(tab), TOUCH_EVENT_MOVE, id);
                evi(cx(tab), cy(tab), TOUCH_EVENT_UP, id);
            }
            find_halted();
            prun(500u);
            CHECK_EQ(hitec_prog_find_side(model()), -1);
            CHECK(hitec_prog_move_on(model()));
        }
    }
}

/* The router hands a press through the alert band where the overlay names
 * a stop: MOVE SERVO's value while the loop is on and the key whose side
 * searches, and neither once it has ended. */
TEST_CASE(the_overlay_names_its_stops_to_the_router)
{
    moving();
    const gfx_rect_t mv = val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0);
    const gfx_rect_t l = hitec_panel_find_rect(false);
    const gfx_rect_t r = hitec_panel_find_rect(true);

    CHECK(hitec_panel_safe_at(cx(mv), cy(mv)));
    CHECK(!hitec_panel_safe_at(cx(l), cy(l)));
    tap(l);
    prun(200u);
    CHECK(hitec_panel_safe_at(cx(l), cy(l)));
    CHECK(!hitec_panel_safe_at(cx(r), cy(r)));
    evi(cx(l), cy(l), TOUCH_EVENT_DOWN, 1);
    find_halted();
    evi(cx(l), cy(l), TOUCH_EVENT_UP, 1);
    CHECK(!hitec_panel_safe_at(cx(l), cy(l)));
    evi(cx(mv), cy(mv), TOUCH_EVENT_DOWN, 1);
    evi(cx(mv), cy(mv), TOUCH_EVENT_UP, 1);
    CHECK(!hitec_panel_safe_at(cx(mv), cy(mv)));
}

/* SERVO over the session: the screen names MOVE SERVO's ON as a stop
 * (ui_screen_t's safe_at) while the loop runs, and a DOWN handed to the
 * screen ends the loop on the press.  With the loop off the screen names
 * none there. */
TEST_CASE(the_servo_screen_hands_the_press_on_a_stop_to_the_overlay)
{
    const ui_screen_t *sv = servo_fn_screen(SERVO_FN_HITEC);

    sv->reset();
    moving();
    servo_screen_show_fn(SERVO_FN_HITEC);
    const gfx_rect_t mv = val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0);
    const gfx_rect_t lv = val(HITEC_PANEL_TRAVEL, LEFT_ROW, 0);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)cx(mv),
                                   .y = (int16_t)cy(mv), .strength = 40 } };

    CHECK(sv->safe_at(cx(mv), cy(mv)));
    CHECK(!sv->safe_at(cx(lv), cy(lv)));
    sv->event(&e);
    CHECK(!hitec_prog_move_on(model()));
    e.type = TOUCH_EVENT_UP;
    sv->event(&e);
    move_ended();
    CHECK(!sv->safe_at(cx(mv), cy(mv)));
    CHECK_EQ(hitec_panel_editor(), 0);
}

/* A read under way, the PORT row's button reading ABORT. */
static void reading(void)
{
    open_at(SUPPLY_ON, UI_LANG_EN);
    servo_d485();
    g_drv.dreg[0x32] = 3u;
    tap_connect();
    tap(val(HITEC_PANEL_SERVO, 1, 1));            /* READ ALL */
    CHECK_EQ(hitec_prog_state(model()), HITEC_PROG_READING);
    prun(200u);
    show();
}

/* ABORT acts on the press: the read ends on the DOWN of contact 1, 2 or 3,
 * lifted on the button, slid off it or lost.  The overlay names ABORT to
 * the router while it shows. */
TEST_CASE(abort_ends_a_read_on_the_press_of_a_lone_contact_1_2_or_3)
{
    for (int end = 0; end < 3; ++end) {
        for (uint8_t id = 1; id <= 3; ++id) {
            reading();
            const gfx_rect_t ab = val(HITEC_PANEL_SERVO, 0, 0);
            const gfx_rect_t tab = hitec_panel_tab_rect(HITEC_PANEL_TRAVEL);

            CHECK(hitec_panel_safe_at(cx(ab), cy(ab)));
            evi(cx(ab), cy(ab), TOUCH_EVENT_DOWN, id);
            if (end == 1) {
                evi(cx(tab), cy(tab), TOUCH_EVENT_MOVE, id);
                evi(cx(tab), cy(tab), TOUCH_EVENT_UP, id);
            } else if (end == 2) {
                hitec_panel_cancel();
            }
            until(HITEC_PROG_HELD);
            show();
            CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
            CHECK(!hitec_panel_safe_at(cx(ab), cy(ab)));
            if (end == 0) {
                /* The lift on the button, now LEAVE, opens no page. */
                evi(cx(ab), cy(ab), TOUCH_EVENT_UP, id);
                show();
                CHECK_EQ(hitec_panel_editor(), 0);
            }
            CHECK_EQ(hitec_prog_state(model()), HITEC_PROG_HELD);
        }
    }
}

/* Contact 1 holds the TRAVEL tab, contact 2 rests on a value; a third on
 * ABORT ends the read on its press. */
TEST_CASE(abort_ends_a_read_under_two_other_contacts)
{
    reading();
    const gfx_rect_t ab = val(HITEC_PANEL_SERVO, 0, 0);
    const gfx_rect_t tab = hitec_panel_tab_rect(HITEC_PANEL_SERVO);
    const gfx_rect_t rd = val(HITEC_PANEL_SERVO, 1, 1);

    evi(cx(tab), cy(tab), TOUCH_EVENT_DOWN, 1);
    evi(cx(rd), cy(rd), TOUCH_EVENT_DOWN, 2);
    CHECK_EQ(hitec_prog_state(model()), HITEC_PROG_READING);
    evi(cx(ab), cy(ab), TOUCH_EVENT_DOWN, 3);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_ABORTED);
    for (uint8_t id = 3; id >= 1; --id) {
        evi(cx(ab), cy(ab), TOUCH_EVENT_UP, id);
    }
}

/* Contact 1 holds LEFT END's + key, whose repeat moves the horn; contact 2
 * rests on LEFT END's value.  A third on MOVE SERVO's ON ends the loop on
 * its press. */
TEST_CASE(move_servo_on_ends_under_two_other_contacts)
{
    moving();
    const gfx_rect_t plus =
        hitec_panel_step_rect(HITEC_PANEL_TRAVEL, LEFT_ROW, true);
    const gfx_rect_t mv = val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0);
    const gfx_rect_t lv = val(HITEC_PANEL_TRAVEL, LEFT_ROW, 0);

    evi(cx(plus), cy(plus), TOUCH_EVENT_DOWN, 1);
    evi(cx(lv), cy(lv), TOUCH_EVENT_DOWN, 2);
    CHECK(hitec_prog_move_on(model()));
    evi(cx(mv), cy(mv), TOUCH_EVENT_DOWN, 3);
    CHECK(!hitec_prog_move_on(model()));
    for (uint8_t id = 3; id >= 1; --id) {
        evi(cx(mv), cy(mv), TOUCH_EVENT_UP, id);
    }
}

/* With contacts 1 to 3 on the + key, the value and the tab strip, a fourth
 * on MOVE SERVO's ON ends the loop on its press. */
TEST_CASE(move_servo_on_ends_under_three_other_contacts)
{
    moving();
    const gfx_rect_t plus =
        hitec_panel_step_rect(HITEC_PANEL_TRAVEL, LEFT_ROW, true);
    const gfx_rect_t mv = val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0);
    const gfx_rect_t lv = val(HITEC_PANEL_TRAVEL, LEFT_ROW, 0);
    const gfx_rect_t tab = hitec_panel_tab_rect(HITEC_PANEL_TRAVEL);

    evi(cx(plus), cy(plus), TOUCH_EVENT_DOWN, 1);
    evi(cx(lv), cy(lv), TOUCH_EVENT_DOWN, 2);
    evi(cx(tab), cy(tab), TOUCH_EVENT_DOWN, 3);
    CHECK(hitec_prog_move_on(model()));
    evi(cx(mv), cy(mv), TOUCH_EVENT_DOWN, 4);
    CHECK(!hitec_prog_move_on(model()));
    for (uint8_t id = 4; id >= 1; --id) {
        evi(cx(mv), cy(mv), TOUCH_EVENT_UP, id);
    }
}

/* FIND STOP searches on the left.  Contact 1 holds the TRAVEL tab, contact
 * 2 rests on the right key; a third on LEFT END ends the search on its
 * press, and MOVE SERVO runs on.  Again with a third resting on the value
 * of MOVE SERVO, and a fourth on the key. */
TEST_CASE(the_searching_find_key_ends_under_two_and_three_other_contacts)
{
    const gfx_rect_t tab = hitec_panel_tab_rect(HITEC_PANEL_TRAVEL);

    for (int others = 2; others <= 3; ++others) {
        moving();
        const gfx_rect_t l = hitec_panel_find_rect(false);
        const gfx_rect_t r = hitec_panel_find_rect(true);
        const gfx_rect_t lv = val(HITEC_PANEL_TRAVEL, LEFT_ROW, 0);

        tap(l);
        CHECK_EQ(hitec_prog_find_side(model()), (int)HITEC_FIND_LEFT);
        prun(200u);
        evi(cx(tab), cy(tab), TOUCH_EVENT_DOWN, 1);
        evi(cx(r), cy(r), TOUCH_EVENT_DOWN, 2);
        if (others == 3) {
            evi(cx(lv), cy(lv), TOUCH_EVENT_DOWN, 3);
        }
        CHECK_EQ(hitec_prog_find_side(model()), (int)HITEC_FIND_LEFT);
        evi(cx(l), cy(l), TOUCH_EVENT_DOWN, (uint8_t)(others + 1));
        for (unsigned i = 0; i < 2000u
                             && msg() != HITEC_PROG_MSG_FIND_HALT;
             ++i) {
            pms();
        }
        CHECK_EQ(msg(), HITEC_PROG_MSG_FIND_HALT);
        CHECK_EQ(hitec_prog_find_side(model()), -1);
        CHECK(hitec_prog_move_on(model()));
        for (int id = others + 1; id >= 1; --id) {
            evi(cx(l), cy(l), TOUCH_EVENT_UP, (uint8_t)id);
        }
    }
}

/* Contact 1 holds MOVE SERVO's ON, contact 2 presses it too: the loop ends
 * on contact 2's press.  Contact 1's release after the end opens no page
 * and starts nothing: its tap went with the stop. */
TEST_CASE(a_held_tap_on_move_servo_starts_nothing_after_the_stop)
{
    moving();
    const gfx_rect_t mv = val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0);

    evi(cx(mv), cy(mv), TOUCH_EVENT_DOWN, 1);
    evi(cx(mv), cy(mv), TOUCH_EVENT_DOWN, 2);
    CHECK(!hitec_prog_move_on(model()));
    evi(cx(mv), cy(mv), TOUCH_EVENT_UP, 2);
    for (unsigned i = 0; i < 60000u
                         && hitec_prog_move_phase(model()) != HITEC_MOVE_OFF;
         ++i) {
        pms();
    }
    until(HITEC_PROG_HELD);
    show();
    CHECK(hitec_panel_row_live(HITEC_PANEL_TRAVEL, MOVE_ROW, 0));
    evi(cx(mv), cy(mv), TOUCH_EVENT_UP, 1);
    show();
    CHECK_EQ(hitec_panel_editor(), 0);
    prun(500u);
    CHECK_EQ(hitec_prog_move_phase(model()), HITEC_MOVE_OFF);
}

/* Contact 1 holds the FIND STOP key that searches, contact 2 presses it
 * too: the search ends on contact 2's press.  Neither release after the
 * halt starts a new search, in either order: contact 1's tap went with the
 * stop. */
TEST_CASE(a_held_tap_on_the_searching_key_starts_no_search_after_the_stop)
{
    for (int order = 0; order < 2; ++order) {
        moving();
        const gfx_rect_t l = hitec_panel_find_rect(false);

        tap(l);
        CHECK_EQ(hitec_prog_find_side(model()), (int)HITEC_FIND_LEFT);
        prun(200u);
        evi(cx(l), cy(l), TOUCH_EVENT_DOWN, 1);
        evi(cx(l), cy(l), TOUCH_EVENT_DOWN, 2);
        for (unsigned i = 0; i < 2000u && msg() != HITEC_PROG_MSG_FIND_HALT;
             ++i) {
            pms();
        }
        CHECK_EQ(msg(), HITEC_PROG_MSG_FIND_HALT);
        evi(cx(l), cy(l), TOUCH_EVENT_UP, order == 0 ? 2 : 1);
        prun(200u);
        CHECK_EQ(hitec_prog_find_side(model()), -1);
        evi(cx(l), cy(l), TOUCH_EVENT_UP, order == 0 ? 1 : 2);
        prun(500u);
        CHECK_EQ(hitec_prog_find_side(model()), -1);
        CHECK(hitec_prog_move_on(model()));
    }
}

int main(void)
{
    RUN(move_servo_on_ends_on_the_press_of_a_lone_contact_1_2_or_3);
    RUN(move_servo_pressed_then_slid_off_or_lost_stays_ended);
    RUN(the_searching_find_key_ends_on_the_press_of_a_lone_contact_1_2_or_3);
    RUN(the_searching_key_pressed_then_slid_off_or_lost_stays_ended);
    RUN(the_overlay_names_its_stops_to_the_router);
    RUN(the_servo_screen_hands_the_press_on_a_stop_to_the_overlay);
    RUN(abort_ends_a_read_on_the_press_of_a_lone_contact_1_2_or_3);
    RUN(abort_ends_a_read_under_two_other_contacts);
    RUN(move_servo_on_ends_under_two_other_contacts);
    RUN(move_servo_on_ends_under_three_other_contacts);
    RUN(the_searching_find_key_ends_under_two_and_three_other_contacts);
    RUN(a_held_tap_on_move_servo_starts_nothing_after_the_stop);
    RUN(a_held_tap_on_the_searching_key_starts_no_search_after_the_stop);
    return test_summary("hitec_panel_stop_fingers");
}
