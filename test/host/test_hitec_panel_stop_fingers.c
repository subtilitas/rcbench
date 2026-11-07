/*
 * The Hitec programmer's overlay stops the servo whatever other fingers
 * hold (shared/ui/hitec_panel_touch.c): MOVE SERVO while it reads ON, and
 * the FIND STOP key whose side searches, pressed by a further contact while
 * one contact holds the overlay's press and others rest on the glass, act
 * on that press.  The overlay's bench is hitec_panel_rig.h, its servo
 * follows the target and stops at d_block_lo.
 *
 * Cases:
 * - move_servo_on_ends_under_two_other_contacts
 * - move_servo_on_ends_under_three_other_contacts
 * - the_searching_find_key_ends_under_two_and_three_other_contacts
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel_rig.h"

#include "hitec_live.h"
#include "hitec_move.h"

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

int main(void)
{
    RUN(move_servo_on_ends_under_two_other_contacts);
    RUN(move_servo_on_ends_under_three_other_contacts);
    RUN(the_searching_find_key_ends_under_two_and_three_other_contacts);
    return test_summary("hitec_panel_stop_fingers");
}
