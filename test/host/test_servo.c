/*
 * What the servo screen decides: where a touch on the dial points the horn,
 * what the travel limit refuses, and the commands it produces.  The
 * SETTINGS overlay, the automatic test and the limit run have their own
 * suites: test_servo_settings.c, test_servo_test_panel.c and
 * test_servo_limit_panel.c.
 *
 * Not what it looks like; that is the golden image's job.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "outputs_screen.h"
#include "servo_screen.h"
#include "servo_sim.h"
#include "servo_sweep.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_target_row.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "outputs.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/* -------------------------------------------------------------------- */

/*
 * The gesture: a touch at 45° on the dial commands 45°, not the pulse width
 * under the finger.
 */
TEST_CASE(a_touch_on_the_dial_points_the_horn_there)
{
    for (int want = -90; want <= 90; want += 45) {
        fresh_armed();
        int x, y;
        dial_at((float)want, ARC_R - 30, &x, &y);
        ev(x, y, TOUCH_EVENT_DOWN, 1);

        const servo_cmd_t c = last_cmd();
        CHECK_EQ(c.kind, SERVO_CMD_POSITION);
        /* Centre 1500, half-span 500, so a degree is 500/90 microseconds. */
        const int expect = 1500 + (int)((float)want * 500.0f / 90.0f + 0.5f);
        if (abs((int)c.value_us - expect) > 12) {
            T_FAIL("%d deg gave %u us, wanted about %d",
                   want, (unsigned)c.value_us, expect);
        }
    }
}

/* A drag keeps commanding, because the servo is meant to follow the finger
 * rather than jump when it lifts. */
TEST_CASE(a_drag_keeps_commanding)
{
    fresh_armed();
    int x, y;
    dial_at(0.0f, ARC_R - 30, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    const uint16_t at_zero = servo_screen_commanded();

    dial_at(60.0f, ARC_R - 30, &x, &y);
    ev(x, y, TOUCH_EVENT_MOVE, 1);
    const uint16_t at_sixty = servo_screen_commanded();
    CHECK(at_sixty > at_zero);

    ev(x, y, TOUCH_EVENT_UP, 1);
    /* And the release does not move it again. */
    CHECK_EQ(servo_screen_commanded(), at_sixty);
}

/*
 * A touch that is not on the dial is not a command.  The case is the biggest
 * thing on the card and sits right beside the sweep; a finger landing on it
 * must not fling the horn to whatever angle the case happens to be at.
 */
TEST_CASE(the_case_is_not_the_dial)
{
    fresh();
    const uint16_t before = servo_screen_commanded();
    tap(SHAFT_X - 180, SHAFT_Y);        /* well inside the case */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), before);

    /* Nor is the middle of the boss, which is not a direction. */
    tap(SHAFT_X, SHAFT_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

/* Dragging past the travel limit stops at the limit rather than being
 * ignored, which is what a mechanical stop does. */
TEST_CASE(the_travel_limit_clamps_rather_than_refuses)
{
    fresh_armed();
    open_settings();
    tap(ROW_L_X, ROW_Y(4));             /* TRAVEL */
    keys("50");
    close_settings();
    while (last_cmd().kind != SERVO_CMD_NONE) { }

    int x, y;
    dial_at(85.0f, ARC_R - 30, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);

    /* Fifty degrees of travel left, so about 1500 + 50*500/90. */
    const int expect = 1500 + (int)(50.0f * 500.0f / 90.0f + 0.5f);
    if (abs((int)c.value_us - expect) > 12) {
        T_FAIL("clamped to %u us, wanted about %d", (unsigned)c.value_us,
               expect);
    }
}

TEST_CASE(centre_and_release_post_their_own_commands)
{
    fresh_armed();
    int x, y;
    dial_at(60.0f, ARC_R - 30, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y, TOUCH_EVENT_UP, 1);
    (void)last_cmd();

    tap(578, 350 + 16);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_CENTRE);
    CHECK_EQ(c.value_us, 1500);
    CHECK_EQ(servo_screen_commanded(), 1500);

    tap(716, 350 + 16);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
}

/* ---------------------------------------------------------------- arming */

/*
 * The gesture is the same two seconds as MOTOR & ESC's, because it is the
 * same control: a press that is held arms, and a press that is not does
 * nothing.  A servo cannot be driven without it -- the IOmcu writes a
 * pulse of length zero to every PWM pin while the bench is not armed.
 */
TEST_CASE(a_hold_on_arm_asks_to_arm_exactly_once)
{
    fresh();
    arm_press();
    hold_for(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);

    /* It does not repeat under the same finger. */
    servo_cmd_t junk;
    while (servo_screen_take(&junk)) { }
    hold_for(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

/*
 * The panel could not hand over every touch event, so this screen's record
 * of what is on the glass is stale.  The arming hold completes on the frame
 * timer, so without the cancel it would arm the bench on a finger that has
 * already gone -- the release it is waiting for is one of the events that
 * went missing.  Cancelling asks for nothing, which is what letting go early
 * already does, and it must leave nothing stuck.
 */
TEST_CASE(a_cancelled_gesture_does_not_arm)
{
    fresh();
    arm_press();
    hold_for(UI_HOLD_S / 4.0f);

    scr->cancel();
    hold_for(UI_HOLD_S * 2.0f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* The contact that was down is gone as far as this screen is concerned,
     * so its release asks for nothing either. */
    arm_release();
    hold_for(UI_HOLD_S);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* And a fresh press still arms. */
    arm_press();
    hold_for(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);
}

TEST_CASE(a_short_press_on_arm_asks_for_nothing)
{
    fresh();
    arm_press();
    hold_for(UI_HOLD_S / 2.0f);
    arm_release();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(a_finger_that_leaves_arm_arms_nothing)
{
    fresh();
    arm_press();
    hold_for(UI_HOLD_S / 2.0f);
    ev(ARM_X + 40, ARM_Y - 120, TOUCH_EVENT_MOVE, 1);   /* slid off it */
    hold_for(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* Sliding back on does not resume it: the press is over. */
    ev(ARM_X + 40, ARM_Y + 16, TOUCH_EVENT_MOVE, 1);
    hold_for(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(arming_and_disarming_come_from_the_same_button)
{
    fresh();
    arm_press();
    hold_for(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);
    /* The bench answers, and the release that armed is not also a press. */
    servo_screen_set_armed(true);
    arm_release();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* A fresh press on it, while armed, is the disarm. */
    tap(ARM_X + 40, ARM_Y + 16);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);
}

TEST_CASE(a_press_that_disarmed_the_bench_does_not_then_arm_it)
{
    /*
     * The bench goes away under the finger -- a stop, a failsafe, or the far
     * end -- and the press that is still down must not start a fresh hold
     * and arm it again two seconds later.
     */
    fresh();
    servo_screen_set_armed(true);
    arm_press();
    servo_screen_set_armed(false);
    hold_for(UI_HOLD_S + 0.5f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* And the button is not stranded: the next hold still arms. */
    arm_release();
    arm_press();
    hold_for(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);
}

TEST_CASE(a_pending_disarm_is_not_overwritten_by_a_position)
{
    /* One command is held at a time, and a drag landing on top of a disarm
     * would drive a bench somebody has just asked to stop. */
    fresh();
    servo_screen_set_armed(true);
    tap(ARM_X + 40, ARM_Y + 16);
    int x, y;
    dial_at(30.0f, ARC_R - 20, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 2);
    servo_cmd_t got;
    CHECK(servo_screen_take(&got));
    CHECK_EQ(got.kind, SERVO_CMD_DISARM);
}

static int red_of(gfx_color_t c)   { return (int)(((c >> 11) & 0x1f) << 3); }
static int green_of(gfx_color_t c) { return (int)(((c >> 5) & 0x3f) << 2); }

/* Inside the ARM button, clear of its rounded corner and its centred
 * label. */
static gfx_color_t arm_px(void)
{
    return fb[(size_t)(ARM_Y + 8) * W + (ARM_X + 12)];
}

TEST_CASE(the_arm_button_fades_across_the_hold_and_flashes_when_it_lands)
{
    fresh();
    scr->render(&cv, 0);
    const gfx_color_t idle = arm_px();
    CHECK(green_of(idle) > red_of(idle));       /* the OK green */

    arm_press();
    hold_for(UI_HOLD_S / 2.0f);
    scr->render(&cv, 0);
    const gfx_color_t half = arm_px();
    CHECK(red_of(half) > red_of(idle));         /* on its way to red */

    hold_for(UI_HOLD_S);
    scr->render(&cv, 0);
    const gfx_color_t full = arm_px();
    if (!(red_of(full) > red_of(half) && red_of(full) > green_of(full))) {
        T_FAIL("the hold went %04x -> %04x -> %04x, which is not a fade to "
               "red", idle, half, full);
    }
    CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);

    /* Arriving flashes the whole button, one colour per drawn frame, and it
     * settles on the danger red.  The finger comes off first: a press lerps
     * the fill towards white, and what is under test here is the flash. */
    servo_screen_set_armed(true);
    arm_release();
    bool saw_white = false, saw_black = false;
    for (int i = 0; i < UI_HOLD_FLASH_FRAMES; ++i) {
        scr->render(&cv, 0);
        const gfx_color_t px = arm_px();
        if (px == GFX_WHITE) { saw_white = true; }
        if (px == GFX_BLACK) { saw_black = true; }
        scr->tick(1.0f / 39.0f);
    }
    CHECK(saw_white);
    CHECK(saw_black);
    scr->render(&cv, 0);
    const gfx_color_t settled = arm_px();
    CHECK(red_of(settled) > green_of(settled));
}

TEST_CASE(the_hold_repaints_the_button_and_leaves_the_card_alone)
{
    /* The fade runs for two seconds at the frame rate, and the right card is
     * 292 x 420: a fade that asked for the card would spend most of the
     * panel's bandwidth on one button. */
    fresh();
    scr->render(&cv, 0);
    /* A pixel of the card above the button, on the TARGET row. */
    const size_t probe = (size_t)(ARM_Y - 60) * W + (ARM_X + 12);
    fb[probe] = 0x1234;

    arm_press();
    hold_for(UI_HOLD_S / 2.0f);
    scr->render(&cv, 0);
    CHECK_EQ(fb[probe], 0x1234);                /* untouched */
    CHECK(arm_px() != 0x1234);                  /* and the button did repaint */
}

/*
 * A held output follows a change to the mapping it was made under.  The
 * pulse is the angle put through the type, the trim and the travel, so
 * changing one of them while something is held leaves the pin on the old
 * mapping while the screen shows the new one.
 */
/*
 * SPEED is the rate the bench may move the output, not a number that only
 * changes the drawing: a servo goes at its own rate unless the command in
 * front of it is ramped.
 */
TEST_CASE(the_speed_travels_with_the_command_as_a_rate)
{
    fresh_armed();
    int x, y;
    dial_at(20.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    /* The screen starts at 100%, which is immediate: anybody who never
     * touches the slider gets the servo at its own rate. */
    CHECK_EQ(last_cmd().slew_per_s, 0);

    /* Half speed is half of the two spans a second the drawing uses. */
    tap(SPEED_X_HALF, SPEED_Y);
    const servo_cmd_t after = last_cmd();
    CHECK_EQ(after.kind, SERVO_CMD_POSITION);   /* said again at the new rate */
    CHECK(after.slew_per_s > 0);
    CHECK(after.slew_per_s <= 2u * OUT_SPAN);
}

TEST_CASE(changing_the_type_says_the_position_again)
{
    fresh_armed();
    int x, y;
    dial_at(45.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const servo_cmd_t first = last_cmd();
    CHECK_EQ(first.kind, SERVO_CMD_POSITION);
    CHECK_EQ(first.max_us, 2000);

    choose_type(1);                            /* STANDARD -> NARROW 760 */
    const servo_cmd_t again = last_cmd();
    CHECK_EQ(again.kind, SERVO_CMD_POSITION);
    CHECK_EQ(again.max_us, 860);
    CHECK(again.value_us >= again.min_us && again.value_us <= again.max_us);
}

TEST_CASE(the_trim_says_the_position_again_while_it_is_held)
{
    fresh_armed();
    int x, y;
    dial_at(0.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const uint16_t was = last_cmd().value_us;

    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));
    const servo_cmd_t after = last_cmd();
    CHECK_EQ(after.kind, SERVO_CMD_POSITION);
    CHECK_EQ(after.value_us, was + 5);
}

TEST_CASE(a_stop_abandons_a_hold_that_is_under_way)
{
    /*
     * A stop can latch while the bench is not armed -- a STOP press, a dead
     * touch, the far end -- so nothing about the armed state changes and the
     * hold would otherwise finish and ask to arm, clearing the latch that had
     * just been set.
     */
    fresh();
    arm_press();
    hold_for(UI_HOLD_S / 2.0f);
    servo_screen_cancel_arm();
    hold_for(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* And the button is not stranded: lifting and pressing again arms. */
    arm_release();
    arm_press();
    hold_for(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);
}

TEST_CASE(a_cancelled_hold_leaves_no_arm_to_be_read_later)
{
    /*
     * The hold can complete in the same frame the stop arrives, so the arm
     * is already waiting to be read.  Cancelling the gesture and leaving its
     * command behind would forward it a frame later and clear the latch.
     */
    fresh();
    arm_press();
    hold_for(UI_HOLD_S + 0.2f);
    servo_screen_cancel_arm();
    servo_cmd_t got;
    CHECK(!servo_screen_take(&got));

    /*
     * And after the finger has lifted, which is the harder case: the release
     * leaves nothing held and nothing counting, so a cancel that asks only
     * about the gesture finds nothing to do and walks past the command.
     */
    fresh();
    arm_press();
    hold_for(UI_HOLD_S + 0.2f);
    arm_release();
    servo_screen_cancel_arm();
    CHECK(!servo_screen_take(&got));
}

TEST_CASE(a_second_contact_cannot_take_over_the_arm_hold)
{
    /*
     * The gesture belongs to the contact that began it.  A second finger, or
     * a palm, taking it over would leave the first one's release ignored and
     * arm the bench from a contact nobody made deliberately.
     */
    fresh();
    arm_press();                                   /* contact 1 */
    hold_for(UI_HOLD_S / 2.0f);
    ev(ARM_X + 80, ARM_Y + 16, TOUCH_EVENT_DOWN, 2);   /* contact 2 lands */
    /* The first contact leaves the button, which ends the gesture. */
    ev(ARM_X + 40, ARM_Y - 120, TOUCH_EVENT_MOVE, 1);
    hold_for(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(a_dial_press_before_the_arm_is_not_held_after_it)
{
    /*
     * A press on the dial of a disarmed bench commands nothing, and the arm
     * starts from nothing.  A screen believing it was driving would say a
     * position on the next change of type, trim or travel -- onto a bench
     * that is armed by then.  What the change says is the rest, under the
     * new type.
     */
    fresh();
    int x, y;
    dial_at(60.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    servo_screen_set_armed(true);
    choose_type(1);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
}

TEST_CASE(a_stop_on_a_bench_that_was_not_armed_still_lets_go)
{
    /*
     * A press on the dial of a disarmed bench commands nothing, and a STOP
     * then changes nothing about the armed state -- there is nothing to
     * disarm -- while the panel releases the slot all the same.  A screen
     * believing it was driving would say a position on the next change of
     * type, trim or travel.
     */
    fresh();
    int x, y;
    dial_at(-40.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    servo_screen_cancel_arm();       /* what a stop calls */
    choose_type(1);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(a_stop_stops_the_screen_holding_anything)
{
    /*
     * The bench can be disarmed by something the screen did not do -- a
     * STOP, a dead touch, the far end.  What was being held is not being
     * held any more, so a change to the settings must not say a position
     * again and rebuild the command the stop released.
     */
    fresh();
    servo_screen_set_armed(true);
    int x, y;
    dial_at(30.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);

    servo_screen_set_armed(false);
    choose_type(1);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(nothing_is_said_again_when_nothing_is_being_held)
{
    /* A screen that is not driving anything commands nothing by having its
     * settings changed. */
    fresh();
    choose_type(1);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

/*
 * Resting on an armed bench is different: the rest is restated under the
 * new profile, so its range and its rate follow the type chosen rather than
 * the one before it.
 */
TEST_CASE(a_resting_armed_servo_takes_the_new_profile)
{
    fresh();
    servo_screen_set_armed(true);
    choose_type(1);                            /* NARROW 760 */
    servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_RELEASE);
    CHECK_EQ(c.min_us, 660);
    CHECK_EQ(c.max_us, 860);
    CHECK_EQ(c.frame_hz, 50);
    choose_type(4);                            /* HELI TAIL 760, held */
    c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_RELEASE);
    CHECK_EQ(c.frame_hz, 560);
    choose_type(0);                            /* back to STANDARD PWM */
    c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_RELEASE);
    CHECK_EQ(c.min_us, 1000);
    CHECK_EQ(c.frame_hz, 50);
}

/*
 * A profile changed between the ARM hold and the arm landing is the one the
 * pins get: an arm not yet collected carries it, and one already collected
 * is followed at once by the rest restated under it, which the panel writes
 * before it arms.
 */
TEST_CASE(a_profile_changed_while_an_arm_is_pending_is_the_one_armed)
{
    fresh();
    arm_press();
    hold_for(UI_HOLD_S + 0.1f);
    arm_release();
    choose_type(1);                            /* NARROW 760 */
    servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_ARM);
    CHECK_EQ(c.min_us, 660);
    CHECK_EQ(c.max_us, 860);
    servo_screen_set_armed(true);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);   /* nothing changed since */

    fresh();
    arm_press();
    hold_for(UI_HOLD_S + 0.1f);
    arm_release();
    c = last_cmd();                            /* collected: STANDARD PWM */
    CHECK_EQ(c.kind, SERVO_CMD_ARM);
    CHECK_EQ(c.min_us, 1000);
    choose_type(4);                            /* HELI TAIL 760, held */
    c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_RELEASE);
    CHECK_EQ(c.min_us, 410);
    CHECK_EQ(c.frame_hz, 560);
    servo_screen_set_armed(true);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);   /* already answered */
}

/* ------------------------------------------------------- the servo's range */

/*
 * The range travels with the pulse.  A narrow servo runs 660 to 860 us and
 * its centre is below a standard servo's floor, so a command clamped against
 * the wrong pair sends the whole travel to one end.
 */
TEST_CASE(a_command_carries_the_endpoints_of_the_type_it_was_made_for)
{
    fresh_armed();
    int x, y;
    dial_at(0.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    servo_cmd_t got = last_cmd();
    CHECK_EQ(got.min_us, 1000);
    CHECK_EQ(got.max_us, 2000);

    /* Another type, and the endpoints follow it. */
    choose_type(1);
    tap(x, y);
    got = last_cmd();
    CHECK_EQ(got.min_us, 660);
    CHECK_EQ(got.max_us, 860);
    /* And the pulse it asks for is inside them. */
    CHECK(got.value_us >= got.min_us && got.value_us <= got.max_us);
}

/* ------------------------------------------------------------------- knob */

/* The rotary knob moves the horn by a fraction of its travel, relative to
 * where it is, and leaves the horn to a finger, a sweep and the settings. */
TEST_CASE(the_knob_moves_the_horn_by_how_far_it_turned)
{
    fresh_armed();
    servo_screen_knob(0.25f);
    const servo_cmd_t a = last_cmd();
    CHECK_EQ(a.kind, SERVO_CMD_POSITION);
    CHECK(a.value_us > 1500);
    const int first = (int)a.value_us - 1500;

    servo_screen_knob(0.25f);
    const servo_cmd_t b = last_cmd();
    CHECK(abs(((int)b.value_us - 1500) - 2 * first) <= 2);

    servo_screen_knob(-0.5f);
    const servo_cmd_t c = last_cmd();
    CHECK(abs((int)c.value_us - 1500) <= 2);
    CHECK_EQ(servo_screen_commanded(), c.value_us);
}

TEST_CASE(the_knob_stops_at_the_servos_travel)
{
    fresh_armed();
    servo_screen_knob(9.0f);
    const servo_cmd_t hi = last_cmd();
    servo_screen_knob(0.1f);
    CHECK_EQ(servo_screen_commanded(), hi.value_us);
    servo_screen_knob(-9.0f);
    const servo_cmd_t lo = last_cmd();
    CHECK(lo.value_us < 1500 && hi.value_us > 1500);
    CHECK(abs((int)(hi.value_us - 1500) - (int)(1500 - lo.value_us)) <= 2);
}

TEST_CASE(the_knob_never_arms_the_servo_bench)
{
    fresh();
    for (int i = 0; i < 30; ++i) {
        servo_screen_knob(0.07f);
        const servo_cmd_t c = last_cmd();
        CHECK(c.kind == SERVO_CMD_POSITION || c.kind == SERVO_CMD_NONE);
    }
}

TEST_CASE(a_knob_that_does_not_turn_commands_nothing_on_the_servo)
{
    fresh();
    servo_screen_knob(0.0f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(a_finger_on_the_dial_owns_the_horn_against_the_knob)
{
    fresh_armed();
    int x, y;
    dial_at(0.0f, ARC_R - 30, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    (void)last_cmd();
    servo_screen_knob(0.4f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    ev(x, y, TOUCH_EVENT_UP, 1);
    servo_screen_knob_frame();          /* the next frame */
    servo_screen_knob(0.4f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
}

/* A finger that went down and came up inside the frame owned the dial in it:
 * the delta collected in that frame is dropped although dragging is clear
 * again when it is applied. */
TEST_CASE(a_finger_that_lifted_in_the_frame_still_owned_the_dial)
{
    fresh_armed();
    int x, y;
    dial_at(0.0f, ARC_R - 30, &x, &y);
    servo_screen_knob_frame();
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y, TOUCH_EVENT_UP, 1);
    (void)last_cmd();
    const uint16_t held = servo_screen_commanded();
    servo_screen_knob(0.4f);
    CHECK_EQ(servo_screen_commanded(), held);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* A drag begun in an earlier frame and ended in this one. */
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    servo_screen_knob_frame();
    ev(x, y, TOUCH_EVENT_UP, 1);
    (void)last_cmd();
    const uint16_t held2 = servo_screen_commanded();
    servo_screen_knob(0.4f);
    CHECK_EQ(servo_screen_commanded(), held2);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    servo_screen_knob_frame();
    servo_screen_knob(0.4f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
}

/* Withdrawing the knob's waiting command puts the horn back and sends
 * nothing; a position pending before the knob moved it stays pending. */
TEST_CASE(a_withdrawn_knob_command_restores_the_horn)
{
    fresh_armed();
    servo_screen_knob(0.25f);
    servo_screen_knob(0.25f);
    servo_screen_knob_cancel();
    CHECK(abs((int)servo_screen_commanded() - 1500) <= 2);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    servo_screen_knob(0.25f);
    const servo_cmd_t taken = last_cmd();          /* taken */
    servo_screen_knob_cancel();
    CHECK_EQ(servo_screen_commanded(), taken.value_us);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(a_withdrawn_knob_command_leaves_a_position_from_a_touch)
{
    fresh_armed();
    int x, y;
    dial_at(20.0f, ARC_R - 30, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y, TOUCH_EVENT_UP, 1);
    const uint16_t touched = servo_screen_commanded();
    servo_screen_knob_frame();
    servo_screen_knob(0.1f);
    CHECK(servo_screen_commanded() != touched);
    servo_screen_knob_cancel();
    CHECK_EQ(servo_screen_commanded(), touched);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, touched);
}

TEST_CASE(a_withdrawal_does_not_touch_a_command_posted_after_the_knob)
{
    fresh();
    servo_screen_knob(0.25f);
    tap(716, 350 + 16);                 /* RELEASE, over the position */
    servo_screen_knob_cancel();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
}

TEST_CASE(the_knob_leaves_the_horn_to_the_settings_panel)
{
    fresh_armed();
    open_settings();
    servo_screen_knob(0.4f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    close_settings();
    servo_screen_knob_frame();
    servo_screen_knob(0.4f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
}

/* The settings open and closed again inside the frame still own it. */
TEST_CASE(a_turn_in_a_frame_where_the_settings_were_open_is_dropped)
{
    fresh_armed();
    servo_screen_knob_frame();
    open_settings();
    close_settings();
    servo_screen_knob(0.4f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    servo_screen_knob_frame();
    servo_screen_knob(0.4f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
}

/* A withdrawn first command leaves the servo idle: a later change of SPEED
 * restates nothing. */
TEST_CASE(a_withdrawn_knob_command_leaves_the_servo_idle)
{
    fresh();
    servo_screen_knob(0.25f);
    servo_screen_knob_cancel();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    tap(SPEED_X_HALF, SPEED_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

/* The knob posts only over a position or into an empty slot: a release or a
 * completed arm waiting to be taken stays, and the delta is dropped. */
TEST_CASE(a_pending_release_is_not_overwritten_by_the_knob)
{
    fresh();
    servo_screen_knob(0.25f);
    (void)last_cmd();
    tap(716, 350 + 16);                 /* RELEASE */
    const uint16_t before = servo_screen_commanded();
    servo_screen_knob(0.25f);
    CHECK_EQ(servo_screen_commanded(), before);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
}

TEST_CASE(a_completed_arm_is_not_overwritten_by_the_servo_knob)
{
    fresh();
    arm_press();
    hold_for(UI_HOLD_S + 0.2f);
    servo_screen_knob(0.25f);
    CHECK_EQ(servo_screen_commanded(), 1500);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);
}

/* ------------------------------------------------------------------ sweep */

#define SWEEP_X (ARM_X + ARM_W / 2)
#define BTN_Y   (350 + 16)

static void frames(float secs)
{
    for (int i = 0; i < (int)(secs * 39.0f + 0.5f); ++i) {
        scr->tick(1.0f / 39.0f);
    }
}

/* SWEEP as the panel takes it, and the far end's start acknowledged now:
 * until then the curve is not drawn. */
static void sweep_go(void)
{
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
}

/* PAUSE as the panel takes it: the HOLD, for its acknowledgement. */
static servo_cmd_t pause_go(void)
{
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_HOLD);
    return c;
}

/*
 * The panel's acknowledgement of @p hold, now: the phase the far end keeps
 * is the curve's now, the screen's own timing of the curve standing in for
 * the panel's, which the tests start in step with it.
 */
static void ack_hold(const servo_cmd_t *hold)
{
    servo_screen_sweep_held(hold->pause_seq, servo_screen_curve_ms());
}

/* The panel's acknowledgement of a RESUME, @p ago_ms ago, of a far end that
 * kept @p kept_ms of its curve: the age of its phase 0, and of the resume. */
static void ack_resume(uint32_t kept_ms, uint32_t ago_ms)
{
    acked(kept_ms + ago_ms, SERVO_SWEEP_RESUMED, ago_ms);
}

/* SWEEP asks the IOmcu for the TEST page's curve, and only on an armed
 * bench and an IOmcu that sweeps: anything else would refuse it. */
TEST_CASE(a_sweep_needs_an_armed_bench_and_a_coprocessor_that_sweeps)
{
    fresh();
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    servo_screen_set_armed(true);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(!servo_screen_sweeping());

    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.sweep_kind, SWEEP_SINE);         /* CURVE: SINE */
    CHECK_EQ(c.sweep_mhz, 500u);                /* SPEED: 0.5 Hz */
    CHECK_EQ(c.sweep_span, 400u);               /* RANGE: 80 % of +/-500 */
    CHECK_EQ(c.sweep_dwell_ms, 200u);
    CHECK_EQ(c.min_us, 1000u);
    CHECK_EQ(c.max_us, 2000u);
    CHECK(servo_screen_sweeping());
}

/* The horn follows the curve the IOmcu runs; PAUSE asks the
 * IOmcu to hold the output where it is, and the drawing stops there. */
TEST_CASE(the_horn_follows_the_sweep_and_hold_keeps_it_where_it_is)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    two_buffers();
    sweep_go();
    for (int i = 0; i < 20; ++i) {             /* half a second, 0.5 Hz sine */
        scr->tick(1.0f / 39.0f);
        scr->render((i % 2) ? &cv1 : &cv, i % 2);
    }
    CHECK(both_whole());
    const uint16_t at = servo_screen_commanded();
    CHECK(at > 1880u && at <= 1900u);          /* 1500 + 400 at the peak */
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_HOLD);
    const uint16_t held_at = servo_screen_commanded();
    CHECK(held_at > 1880u && held_at <= 1900u);
}

/* A sweep, running or paused, owns the horn: the knob does not take it. */
TEST_CASE(the_knob_does_not_take_the_horn_from_a_sweep)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    servo_screen_knob(0.3f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(servo_screen_sweeping());

    (void)pause_go();
    CHECK(servo_screen_paused());
    servo_screen_knob(0.3f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

/* A sweep that ended before the turn is applied still owned the frame. */
TEST_CASE(a_turn_in_a_frame_where_a_sweep_ran_is_dropped)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    servo_screen_knob_frame();          /* the frame starts mid-sweep */
    servo_screen_set_sweep(false);      /* ends it, with a RELEASE */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
    servo_screen_knob(0.3f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    servo_screen_knob_frame();
    servo_screen_knob(0.3f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
}

/* A finger on the dial, CENTRE, RELEASE, a disarm and leaving each end the
 * sweep, with the command that ends it. */
TEST_CASE(a_sweep_ends_on_the_dial_centre_release_disarm_and_leave)
{
    int x, y;
    dial_at(30.0f, ARC_R - 20, &x, &y);
    const struct { int px, py; servo_cmd_kind_t kind; } by[] = {
        { 0, 0, SERVO_CMD_POSITION },          /* the dial, below */
        { ARM_X + 40, BTN_Y, SERVO_CMD_CENTRE },
        { ARM_X + ARM_W - 40, BTN_Y, SERVO_CMD_RELEASE },
    };
    for (int k = 0; k < 3; ++k) {
        fresh();
        servo_screen_set_armed(true);
        servo_screen_set_sweep(true);
        tap(SWEEP_X, BTN_Y);
        frames(0.2f);
        (void)last_cmd();
        tap(k == 0 ? x : by[k].px, k == 0 ? y : by[k].py);
        CHECK(!servo_screen_sweeping());
        const servo_cmd_t c = last_cmd();
        CHECK_EQ(c.kind, by[k].kind);
        CHECK(c.ends_sweep);                   /* it gives way at once */
        tap(k == 0 ? x : by[k].px, k == 0 ? y : by[k].py);
        CHECK(!last_cmd().ends_sweep);         /* only the one that ended it */
    }

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    servo_screen_set_armed(false);
    CHECK(!servo_screen_sweeping());

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    scr->leave();
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);

    /* A touch stream that lost events, the PAUSE perhaps among them: the
     * sweep stops and the output is held where it is. */
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    frames(0.2f);
    (void)last_cmd();
    scr->cancel();
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_HOLD);

    /* A hold the panel had to let go of: the horn goes to the centre the
     * surfaces were released to, and nothing is held. */
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.5f);
    (void)pause_go();
    CHECK(servo_screen_commanded() > 1800u);
    servo_screen_released(g_hold_seq);
    CHECK_EQ(servo_screen_commanded(), 1500u);
    (void)last_cmd();
    choose_type(1);                            /* not driving: a rest */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);

    /* And an IOmcu that cannot sweep any more: what the panel holds is
     * ended with a release. */
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    (void)last_cmd();
    servo_screen_set_sweep(false);
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
}

/* A setting changed while it runs starts the sweep over with it, as the
 * IOmcu's does when it is written. */
TEST_CASE(a_changed_setting_starts_the_sweep_over)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    frames(0.3f);
    (void)last_cmd();
    settings_set(SET_SERVO_TEST_RANGE, 50.0f);
    frames(0.05f);
    servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.sweep_span, 250u);
    /* TRAVEL limits it too: half the travel is half the sweep. */
    open_settings();
    tap(ROW_L_X, ROW_Y(4));                    /* TRAVEL */
    keys("45");
    close_settings();
    frames(0.05f);
    c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.sweep_span, 125u);
}

/* A profile changed while it sweeps is said again with the sweep: NARROW
 * 760's range and rate, though its curve in command units is the same. */
TEST_CASE(a_profile_changed_while_it_sweeps_goes_with_the_sweep)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    frames(0.2f);
    (void)last_cmd();
    choose_type(1);                            /* NARROW 760 */
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.min_us, 660u);
    CHECK_EQ(c.max_us, 860u);
    CHECK_EQ(c.sweep_span, 400u);
    CHECK(servo_screen_sweeping());
}

/* A range changed while it sweeps goes out with the amplitude it gives,
 * not the one before it: half the travel is half the sweep at once. */
TEST_CASE(a_range_changed_while_it_sweeps_goes_with_its_own_amplitude)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    frames(0.2f);
    (void)last_cmd();
    open_settings();
    tap(ROW_L_X, ROW_Y(4));                    /* TRAVEL */
    keys("45");
    const servo_cmd_t c = last_cmd();          /* before any tick */
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.sweep_span, 200u);
    CHECK(servo_screen_sweeping());
}

/* PAUSE keeps the horn where the output has got to, which a slow SPEED
 * leaves well behind the curve. */
TEST_CASE(hold_keeps_the_output_where_it_has_got_to)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED 12 %: 43 deg/s */
    sweep_go();
    frames(0.3f);                              /* the curve is near 58 deg */
    const servo_cmd_t hold = pause_go();
    ack_hold(&hold);                           /* acknowledged at once */
    const uint16_t at = servo_screen_commanded();   /* drawn where it got to */
    CHECK(at > 1500u && at < 1600u);           /* about 13 deg */
}

/* Pixels of @p col on SPEED's row of the right card. */
static int speed_row_pixels(gfx_color_t col)
{
    int n = 0;
    for (int y = 264 + 5; y < 264 + 21; ++y) {
        for (int x = ARM_X; x < ARM_X + ARM_W; ++x) {
            n += (fb[(size_t)y * W + x] == col) ? 1 : 0;
        }
    }
    return n;
}

/*
 * SPEED's row says when SPEED rather than the curve shapes the sweep, in the
 * warning colour, from the settings: before SWEEP is pressed and on a bench
 * that is not armed.  The TEST page's sine, 0.5 Hz over 400 units, asks for
 * 1257 units a second; SPEED at the left end of its track allows at most
 * 240.  The rate typed on the TEST page clears it while the page is open:
 * 0.05 Hz asks for 126.
 */
TEST_CASE(speed_says_when_it_limits_the_sweep)
{
    fresh();
    scr->render(&cv, 0);
    CHECK_EQ(speed_row_pixels(ui_theme_color(UI_C_WARN)), 0);   /* 100 % */

    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    scr->render(&cv, 0);
    CHECK(speed_row_pixels(ui_theme_color(UI_C_WARN)) > 100);
    CHECK_EQ(speed_row_pixels(ui_theme_color(UI_C_DANGER)), 0);

    open_settings();
    tap(TAB_X(PAGE_SWEEP), TAB_Y);             /* SWEEP */
    tap(ROW_L_X, ROW_Y(1));                    /* SPEED, the rate */
    keys("0.05");
    scr->render(&cv, 0);
    CHECK_EQ(speed_row_pixels(ui_theme_color(UI_C_WARN)), 0);

    tap(ROW_L_X, ROW_Y(1));
    keys("0.5");
    scr->render(&cv, 0);
    CHECK(speed_row_pixels(ui_theme_color(UI_C_WARN)) > 100);
    close_settings();

    tap(ARM_X + ARM_W - 1, SPEED_Y);           /* SPEED 100 %: no slew */
    scr->render(&cv, 0);
    CHECK_EQ(speed_row_pixels(ui_theme_color(UI_C_WARN)), 0);
}

/* Pixels of @p col across the middle of the sweep button. */
static int sweep_btn_pixels(gfx_color_t col)
{
    int n = 0;
    for (int y = BTN_Y - 12; y < BTN_Y + 12; ++y) {
        for (int x = SWEEP_X - 40; x < SWEEP_X + 40; ++x) {
            n += (fb[(size_t)y * W + x] == col) ? 1 : 0;
        }
    }
    return n;
}

/*
 * PAUSE pauses a running sweep with the link's HOLD and reads PAUSED, filled
 * in the warning colour rather than the accent; a second tap starts the
 * curve again, from its beginning, as the IOmcu does.  In both themes.
 */
TEST_CASE(pause_holds_the_sweep_and_a_second_tap_resumes_it)
{
    const ui_theme_id_t themes[] = { UI_THEME_DARK, UI_THEME_LIGHT };
    for (unsigned k = 0; k < 2u; ++k) {
        fresh();
        ui_theme_set(themes[k]);
        servo_screen_set_armed(true);
        servo_screen_set_sweep(true);
        scr->render(&cv, 0);
        CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_ACCENT)), 0);
        CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_WARN)), 0);

        tap(SWEEP_X, BTN_Y);                   /* SWEEP */
        CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
        frames(0.3f);
        scr->render(&cv, 0);
        CHECK(sweep_btn_pixels(ui_theme_color(UI_C_ACCENT)) > 1000);
        CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_WARN)), 0);

        tap(SWEEP_X, BTN_Y);                   /* PAUSE */
        CHECK_EQ(last_cmd().kind, SERVO_CMD_HOLD);
        CHECK(servo_screen_paused());
        CHECK(!servo_screen_sweeping());
        const uint16_t at = servo_screen_commanded();
        frames(1.0f);                          /* the drawing stays there */
        CHECK_EQ(servo_screen_commanded(), at);
        scr->render(&cv, 0);
        CHECK(sweep_btn_pixels(ui_theme_color(UI_C_WARN)) > 1000);
        CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_ACCENT)), 0);

        /* SPEED changed while paused keeps the pause, and the resume
         * carries the new rate. */
        tap(SPEED_X_HALF, SPEED_Y);
        CHECK(servo_screen_paused());
        CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

        tap(SWEEP_X, BTN_Y);                   /* PAUSE again: on */
        const servo_cmd_t c = last_cmd();
        CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
        CHECK(c.slew_per_s > 0);
        CHECK_EQ(c.sweep_kind, SWEEP_SINE);
        CHECK(c.resume);
        CHECK(!c.ends_sweep);
        CHECK(servo_screen_sweeping());
        CHECK(!servo_screen_paused());
        scr->render(&cv, 0);
        CHECK(sweep_btn_pixels(ui_theme_color(UI_C_ACCENT)) > 1000);
        CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_WARN)), 0);
    }
    ui_theme_set(UI_THEME_DARK);
}

/*
 * The horn goes on from the phase it was paused at, however long the pause:
 * the 0.5 Hz sine paused a quarter of a second in is at its peak a quarter
 * of a second after PAUSED.  An IOmcu that started the curve over
 * instead (older than 4.6, or refusing) is followed: drawn from the curve's
 * beginning.  One that resumed later than the tap is followed too.
 */
TEST_CASE(paused_carries_the_sweep_on_from_its_phase)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);                       /* SWEEP */
    CHECK(!last_cmd().resume);
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.25f);
    const servo_cmd_t hold = pause_go();
    const uint32_t kept = servo_screen_curve_ms();
    ack_hold(&hold);
    frames(2.0f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: on */
    CHECK(last_cmd().resume);
    const uint16_t held = servo_screen_commanded();
    frames(0.1f);                              /* not acknowledged yet */
    CHECK_EQ(servo_screen_commanded(), held);
    ack_resume(kept, 0u);
    frames(0.25f);
    CHECK(servo_screen_commanded() > 1880u);   /* the peak, 0.5 s in */

    /* Started over at the far end: drawn from the beginning. */
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.25f);
    CHECK(servo_screen_commanded() > 1760u && servo_screen_commanded() < 1800u);
    (void)pause_go();

    /* Resumed 1450 ms before the panel heard: about 1960 ms into the
     * cycle, in the 200 ms dwell at the low end, 1100 us. */
    tap(SWEEP_X, BTN_Y);
    (void)last_cmd();
    ack_resume(servo_screen_curve_ms(), 1450u);
    frames(0.25f);
    CHECK(servo_screen_commanded() >= 1100u && servo_screen_commanded() < 1120u);

    /* A repeat before the panel took it is still a resume. */
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    (void)last_cmd();
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    choose_type(0);                            /* says the sweep again */
    CHECK(last_cmd().resume);
}

/*
 * The far end runs its curve on until the HOLD reaches it.  With 300 ms
 * between the tap and the acknowledgement, the phase it keeps is 400 ms
 * in, not the tap's 100 ms, and the resumed horn is drawn from there: 50 ms
 * later 450 ms into the 0.5 Hz sine, near its peak, where the tap's phase
 * would draw it 150 ms in, below 1700 us.
 *
 * Without feedback the drawn output goes on along the curve while the HOLD
 * is on its way, as the far end does, and the acknowledgement puts it where
 * the far end held it: from about 1620 us at the tap to the curve's
 * 1880 us, which SPEED 100 % follows at once, in both buffers.  The resume
 * goes on from there, not from the tap's angle.
 */
TEST_CASE(the_pause_is_drawn_from_when_the_hold_was_acknowledged)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    two_buffers();
    for (int i = 0; i < 40; ++i) {             /* ARM's flash runs out */
        scr->tick(1.0f / 39.0f);
        scr->render((i % 2) ? &cv1 : &cv, i % 2);
    }
    sweep_go();
    frames(0.1f);
    const servo_cmd_t hold = pause_go();
    const uint16_t at_tap = servo_screen_drawn();
    CHECK(at_tap > 1590u && at_tap < 1650u);
    frames(0.3f);                              /* the HOLD on its way */
    CHECK(servo_screen_drawn() > at_tap + 200u);   /* drawn on, as it runs */
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    const uint32_t kept = servo_screen_curve_ms();
    ack_hold(&hold);                           /* acknowledged now */
    CHECK(both_whole());
    const uint16_t at_ack = servo_screen_drawn();
    CHECK(at_ack > 1860u && at_ack < 1900u);
    CHECK_EQ(servo_screen_commanded(), at_ack);
    frames(1.0f);
    CHECK_EQ(servo_screen_drawn(), at_ack);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: on */
    (void)last_cmd();
    ack_resume(kept, 0u);
    frames(0.05f);
    CHECK(servo_screen_commanded() > 1880u);
    CHECK(servo_screen_drawn() >= at_ack);     /* on from there */

    /* An acknowledgement with nothing paused here changes nothing, nor
     * does an earlier pause's during a later one: paused about 450 ms in,
     * near the peak, it resumes there and reaches the dwell at the peak. */
    servo_screen_sweep_held(hold.pause_seq, 0u);
    CHECK(servo_screen_sweeping());
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    const servo_cmd_t again = last_cmd();
    CHECK(again.pause_seq != hold.pause_seq);
    const uint16_t paused_at = servo_screen_drawn();
    servo_screen_sweep_held(hold.pause_seq, 100000u);
    CHECK_EQ(servo_screen_drawn(), paused_at);
    frames(0.5f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: on */
    (void)last_cmd();
    ack_resume(servo_screen_curve_ms(), 0u);
    frames(0.05f);
    CHECK(servo_screen_commanded() > 1880u);
}

/* A sweep at SPEED's slowest, paused 300 ms in and acknowledged 307 ms
 * later, SPEED raised to 100 % in between when @p raise; the drawn output
 * at the tap into @p at_tap, and returned as it is at the acknowledgement. */
static uint16_t slow_pause_acknowledged(bool raise, uint16_t *at_tap)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    sweep_go();
    frames(0.3f);
    const servo_cmd_t hold = pause_go();
    *at_tap = servo_screen_drawn();
    frames(0.1f);
    if (raise) {
        tap(ARM_X + ARM_W - 1, SPEED_Y);       /* 100 %: nothing sent */
        CHECK(servo_screen_paused());
        CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    }
    frames(0.2f);
    ack_hold(&hold);
    return servo_screen_drawn();
}

/*
 * With feedback, the servo goes on moving after the tap -- while the HOLD
 * is on its way and while it settles -- and the paused angle follows what
 * it reports: a trim changed while paused says the angle it is at, 5 us
 * up, not the one it had at the tap.
 */
TEST_CASE(a_pause_with_feedback_holds_the_angle_the_servo_reports)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    servo_screen_feedback(1500u, 0.2f, true);
    sweep_go();
    frames(0.2f);
    servo_screen_feedback(1600u, 0.2f, true);
    frames(0.05f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    const servo_cmd_t hold = last_cmd();
    CHECK_EQ(hold.kind, SERVO_CMD_HOLD);
    CHECK_EQ(servo_screen_drawn(), 1600u);
    servo_screen_feedback(1700u, 0.2f, true);  /* moved on after the tap */
    frames(0.1f);
    ack_hold(&hold);
    CHECK_EQ(servo_screen_commanded(), 1700u);
    CHECK_EQ(servo_screen_drawn(), 1700u);
    servo_screen_feedback(1720u, 0.2f, true);  /* and settled */
    CHECK_EQ(servo_screen_commanded(), 1720u);
    CHECK(servo_screen_paused());

    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, 1725u);
    CHECK(!servo_screen_paused());
}

/*
 * Feedback lost between the tap and the acknowledgement: the replay of the
 * interval starts from where the output was drawn at the tap, 1600 us, and
 * moves at most SPEED's 74 us, not from a reading that came after it.
 */
TEST_CASE(a_pause_replays_from_the_tap_when_feedback_goes)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    servo_screen_feedback(1500u, 0.2f, true);
    sweep_go();
    frames(0.25f);
    servo_screen_feedback(1600u, 0.2f, true);
    /* tapped before a frame has passed: the reading is the start */
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    const servo_cmd_t hold = last_cmd();
    servo_screen_feedback(1700u, 0.2f, true);
    frames(0.05f);
    servo_screen_feedback(0u, 0.0f, false);    /* gone */
    frames(0.25f);
    ack_hold(&hold);
    const uint16_t at_ack = servo_screen_drawn();
    CHECK(at_ack > 1600u && at_ack <= 1674u);
    CHECK_EQ(servo_screen_commanded(), at_ack);
    frames(0.5f);                              /* held, and drawn held */
    CHECK_EQ(servo_screen_drawn(), at_ack);
}

/*
 * At SPEED's slowest the drawn output lags the curve, and moves on by no
 * more than SPEED allows over the 307 ms (12 frames) the HOLD took: at most
 * 240 units a second, 74 us on a 1000 to 2000 us range.
 */
TEST_CASE(the_acknowledged_pause_moves_the_output_at_speed)
{
    uint16_t at_tap = 0u;
    const uint16_t at_ack = slow_pause_acknowledged(false, &at_tap);
    CHECK(at_ack > at_tap);
    CHECK(at_ack <= at_tap + 74u);
    CHECK(at_ack < 1800u);                     /* behind the curve's 1880 */

    /*
     * SPEED raised after the tap sends nothing while paused, so the far end
     * slews at the old rate until the HOLD reaches it: the output is worked
     * on at the SPEED of the tap, to the same place.
     */
    uint16_t raised_tap = 0u;
    CHECK_EQ(slow_pause_acknowledged(true, &raised_tap), at_ack);
    CHECK_EQ(raised_tap, at_tap);
}

/*
 * PAUSED tapped before the HOLD's acknowledgement reaches the screen: the
 * screen resumes from the tap's phase, 100 ms in, but the far end kept the
 * phase at its acknowledgement, 410 ms in.  The acknowledgement then finds
 * no pause and changes nothing; the resume's acknowledgement, which the
 * panel times from the far end's kept phase, rebases the curve: 410 ms on
 * it is 820 ms in, 1872 us, where a rebase on the tap's phase plus the
 * kept one would put it 922 ms in, near 1806 us, and one on the tap's alone
 * 512 ms in, at the peak.  The same with both acknowledgements in one
 * frame, the HOLD's first.
 */
TEST_CASE(an_early_resume_is_rebased_on_the_phase_the_far_end_kept)
{
    for (int same_frame = 0; same_frame < 2; ++same_frame) {
        fresh();
        servo_screen_set_armed(true);
        servo_screen_set_sweep(true);
        sweep_go();
        frames(0.1f);
        const servo_cmd_t hold = pause_go();
        frames(0.3f);
        const uint32_t kept = servo_screen_curve_ms();  /* the far end's */
        if (!same_frame) {
            tap(SWEEP_X, BTN_Y);               /* PAUSED, before the ack */
            CHECK(last_cmd().resume);
            ack_hold(&hold);                   /* too late: no pause */
            CHECK(servo_screen_sweeping());
        } else {
            tap(SWEEP_X, BTN_Y);
            CHECK(last_cmd().resume);
            servo_screen_sweep_held(hold.pause_seq, kept);
        }
        ack_resume(kept, 0u);
        frames(0.4f);                          /* 820 ms in: 1872 us */
        CHECK(servo_screen_commanded() > 1850u && servo_screen_commanded() < 1890u);
    }
}

/*
 * A disarm, STOP or leaving between PAUSE and its acknowledgement ends the
 * pause; the acknowledgement that follows changes nothing.
 */
TEST_CASE(an_acknowledgement_after_the_pause_ended_changes_nothing)
{
    for (int how = 0; how < 3; ++how) {
        fresh();
        servo_screen_set_armed(true);
        servo_screen_set_sweep(true);
        sweep_go();
        frames(0.2f);
        const servo_cmd_t hold = pause_go();
        if (how == 0) {
            servo_screen_set_armed(false);
        } else if (how == 1) {
            servo_screen_cancel_arm();         /* STOP */
        } else {
            scr->leave();
        }
        CHECK(!servo_screen_paused());
        const uint16_t was = servo_screen_commanded();
        ack_hold(&hold);
        CHECK(!servo_screen_paused());
        CHECK_EQ(servo_screen_commanded(), was);
    }
}

/*
 * A HOLD never acknowledged: the curve is drawn on for OUT_DEFAULT_TIMEOUT_MS
 * after the tap, the longest the far end can run unwritten, and the horn
 * then stands where it got to.
 */
TEST_CASE(an_unacknowledged_pause_is_drawn_on_for_500_ms_at_most)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.1f);
    (void)pause_go();
    const uint16_t at_tap = servo_screen_drawn();
    frames(0.3f);
    CHECK(servo_screen_drawn() > at_tap + 200u);   /* on, as the far end */
    frames(0.3f);
    const uint16_t stood = servo_screen_drawn();
    CHECK_EQ(servo_screen_commanded(), stood);
    frames(1.0f);
    CHECK_EQ(servo_screen_drawn(), stood);
    CHECK(servo_screen_paused());
}

/*
 * A curve changed while the sweep runs: the far end runs the old one until
 * the new start reaches it, so the old curve is drawn on until that start
 * is acknowledged -- near the old 0.5 Hz sine's peak 0.45 s in -- and the
 * new one from there, from its beginning.
 */
TEST_CASE(a_changed_curve_is_drawn_from_its_acknowledged_start)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.25f);
    settings_set(SET_SERVO_TEST_HZ, 1.0f);
    frames(0.025f);                            /* tick() sees it */
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.sweep_mhz, 1000u);
    frames(0.2f);
    CHECK(servo_screen_commanded() > 1880u);   /* still the old curve */
    acked(0u, SERVO_SWEEP_FROM_HERE, 0u);
    scr->tick(0.001f);
    CHECK(servo_screen_commanded() < 1520u);   /* the new one, at 0 */
}

/*
 * A trim changed while a PAUSE is drawn on -- its HOLD not yet acknowledged
 * -- says the angle the horn is at, plus the trim, never a point the curve
 * was still heading for, and the curve is no longer drawn on: a second
 * change says the same angle again.  Without feedback the angle is the
 * drawn estimate, with it the reading.
 */
TEST_CASE(a_change_during_a_pause_drain_holds_the_drawn_angle)
{
    for (int fb_pass = 0; fb_pass < 2; ++fb_pass) {
        fresh();
        servo_screen_set_armed(true);
        servo_screen_set_sweep(true);
        tap(ARM_X + 1, SPEED_Y);               /* SPEED's slowest */
        if (fb_pass) {
            servo_screen_feedback(1500u, 0.2f, true);
        }
        sweep_go();
        frames(0.3f);
        if (fb_pass) {
            servo_screen_feedback(1600u, 0.2f, true);
        }
        (void)pause_go();
        frames(0.15f);                         /* drawn on, not acked */
        if (fb_pass) {
            servo_screen_feedback(1620u, 0.2f, true);
        }
        const uint16_t at = servo_screen_drawn();
        if (fb_pass) {
            CHECK_EQ(at, 1620u);
        }
        open_settings();
        tap(TRIM_UP_X, ROW_Y(3));
        servo_cmd_t c = last_cmd();
        CHECK_EQ(c.kind, SERVO_CMD_POSITION);
        CHECK_EQ(c.value_us, at + 5u);
        frames(0.3f);
        tap(TRIM_UP_X, ROW_Y(3));
        c = last_cmd();
        CHECK_EQ(c.kind, SERVO_CMD_POSITION);
        CHECK_EQ(c.value_us, at + 10u);
        close_settings();
    }
}

/*
 * PAUSED tapped 0.1 s after PAUSE, the HOLD acknowledged 0.2 s after that:
 * the far end ran on to the acknowledgement, 307 ms past the tap, and holds
 * there until the RESUME arrives.  The late acknowledgement moves the drawn
 * output to that held point -- near the 0.5 Hz sine's 1884 us, not the
 * 1740 us the drawing had stopped at -- and the resume goes on from it.
 */
TEST_CASE(an_early_resume_keeps_the_output_the_far_end_held)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.1f);
    const uint32_t tap_phase = servo_screen_curve_ms();
    const servo_cmd_t hold = pause_go();
    frames(0.1f);                              /* drawn on */
    tap(SWEEP_X, BTN_Y);                       /* PAUSED, before the ack */
    CHECK(last_cmd().resume);
    frames(0.2f);
    CHECK(servo_screen_drawn() < 1780u);       /* stopped at the tap */
    servo_screen_sweep_held(hold.pause_seq, tap_phase + 307u);
    const uint16_t held = servo_screen_drawn();
    CHECK(held > 1870u && held < 1900u);
    ack_resume(tap_phase + 307u, 0u);
    CHECK(servo_screen_drawn() >= held - 2u);
    frames(0.05f);
    CHECK(servo_screen_drawn() > 1880u);
}

/*
 * The link going ends a sweep and a pause: the far end lets them go 500 ms
 * after its last word and rests the surfaces, and so does the drawing.  The
 * next tap starts a sweep; nothing is resumed.
 */
TEST_CASE(the_link_going_ends_a_pause_at_rest)
{
    fresh();
    servo_screen_set_link(true);
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.4f);
    const servo_cmd_t hold = pause_go();
    ack_hold(&hold);
    CHECK(servo_screen_paused());
    CHECK(servo_screen_drawn() > 1800u);
    servo_screen_set_link(false);
    CHECK(!servo_screen_paused());
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(servo_screen_drawn(), 1500u);
    frames(0.5f);
    CHECK_EQ(servo_screen_drawn(), 1500u);
    servo_screen_set_link(true);
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK(!c.resume);
}

/*
 * No surface bound: SWEEP is greyed and sends nothing, since a sweep of
 * nothing never starts.  A sweep command the panel finds nothing to send to
 * ends the wait for its start.
 */
TEST_CASE(sweep_needs_a_surface_bound)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    servo_screen_set_surfaces(false);
    scr->render(&cv, 0);
    CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_ACCENT)), 0);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(!servo_screen_sweeping());

    servo_screen_set_surfaces(true);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    CHECK(servo_screen_sweeping());
    servo_screen_sweep_refused();
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
}

/*
 * A sweep, the link down, the link back: nothing is sent until a new tap,
 * the sweep's own command not taken before the link went included, and
 * the control task drops a kept sweep or hold at the same edge
 * (servo_cmd_survives_link_loss()).
 */
TEST_CASE(a_sweep_does_not_come_back_with_the_link)
{
    fresh();
    servo_screen_set_link(true);
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.3f);
    servo_screen_set_link(false);
    servo_screen_set_link(true);
    frames(1.0f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(!servo_screen_sweeping());

    tap(SWEEP_X, BTN_Y);                       /* a new tap ... */
    servo_screen_set_link(false);              /* ... the link goes first */
    servo_screen_set_link(true);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);

    const servo_cmd_t kinds[] = {
        { .kind = SERVO_CMD_SWEEP }, { .kind = SERVO_CMD_HOLD },
        { .kind = SERVO_CMD_POSITION }, { .kind = SERVO_CMD_CENTRE },
        { .kind = SERVO_CMD_RELEASE },
    };
    CHECK(!servo_cmd_survives_link_loss(&kinds[0]));
    CHECK(!servo_cmd_survives_link_loss(&kinds[1]));
    CHECK(servo_cmd_survives_link_loss(&kinds[2]));
    CHECK(servo_cmd_survives_link_loss(&kinds[3]));
    CHECK(servo_cmd_survives_link_loss(&kinds[4]));
    CHECK(!servo_cmd_survives_link_loss(NULL));
}

/* With the link down SWEEP is greyed and starts nothing: the panel would
 * drop the command, and the screen would wait for a start that never
 * comes.  Back up, it works. */
TEST_CASE(sweep_needs_the_link)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    servo_screen_set_link(false);
    scr->render(&cv, 0);
    CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_ACCENT)), 0);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(!servo_screen_sweeping());
    servo_screen_set_link(true);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    CHECK(servo_screen_sweeping());
}

/*
 * PAUSED tapped while the HOLD is still on its way, and that HOLD then let
 * go of (acknowledged late): the resume carries the pause it resumes, which
 * is how the panel drops it as stale, and the screen ends the sweep it had
 * asked to resume, so nothing waits for a start that will not come.
 */
TEST_CASE(a_resume_of_a_let_go_pause_is_dropped)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.2f);
    const servo_cmd_t hold = pause_go();
    tap(SWEEP_X, BTN_Y);                       /* PAUSED, HOLD in flight */
    const servo_cmd_t resume = last_cmd();
    CHECK(resume.resume);
    CHECK_EQ(resume.from_pause, hold.pause_seq);
    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(servo_cmd_stale(&ended, &resume));
    CHECK(servo_screen_sweeping());            /* waiting for the resume */
    servo_screen_released(g_hold_seq);                   /* the late HOLD let go */
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
    frames(0.5f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    tap(SWEEP_X, BTN_Y);                       /* a new tap starts afresh */
    const servo_cmd_t again = last_cmd();
    CHECK_EQ(again.kind, SERVO_CMD_SWEEP);
    CHECK(!again.resume);
}

/*
 * A profile changed while paused says the held angle as a POSITION asked
 * during that pause; once the panel has let the pause go, that POSITION is
 * stale and dropped, so the surfaces stay at the rest the screen shows.
 * The first command from after the pause retires the record: a pause
 * number reused later is not mistaken for it.  A disarm is never stale.
 */
TEST_CASE(commands_from_a_let_go_pause_are_stale)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.2f);
    const servo_cmd_t hold = pause_go();
    CHECK(hold.pause_seq != 0u);
    CHECK_EQ(hold.from_pause, 0u);
    ack_hold(&hold);
    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));                  /* while paused */
    const servo_cmd_t pos = last_cmd();
    CHECK_EQ(pos.kind, SERVO_CMD_POSITION);
    CHECK_EQ(pos.from_pause, hold.pause_seq);
    close_settings();

    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(servo_cmd_stale(&ended, &pos));      /* queued behind the HOLD */
    CHECK(ended.on);
    const servo_cmd_t disarm = { .kind = SERVO_CMD_DISARM,
                                 .from_pause = hold.pause_seq };
    CHECK(!servo_cmd_stale(&ended, &disarm));
    CHECK(ended.on);                           /* not a drive command */
    servo_screen_released(g_hold_seq);
    tap(ARM_X + 40, BTN_Y);                    /* CENTRE, after the pause */
    const servo_cmd_t centre = last_cmd();
    CHECK_EQ(centre.kind, SERVO_CMD_CENTRE);
    CHECK_EQ(centre.from_pause, 0u);
    CHECK(!servo_cmd_stale(&ended, &centre));
    CHECK(!ended.on);                          /* retired */
    CHECK(!servo_cmd_stale(&ended, &pos));     /* a reused number later */
    CHECK(!servo_cmd_stale(NULL, &pos));
    CHECK(!servo_cmd_stale(&ended, NULL));
}

/*
 * The let-go of pause N, with the queue behind its late HOLD in each order
 * the screen can leave: the panel drops exactly the commands that still
 * derive from N, and the screen takes the let-go only while it is still on
 * N -- so once the queue drains, the surfaces do what the screen shows.
 */
static void paused_on_n(servo_cmd_t *hold)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.3f);
    *hold = pause_go();                        /* its ack is late */
}

/* 1: PAUSED and then SPEED in one pass: the resume, said again at the new
 * SPEED, still derives from N and is dropped with it; the screen stops. */
TEST_CASE(a_resume_said_again_keeps_its_pause)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED ... */
    tap(ARM_X + 1, SPEED_Y);                   /* ... SPEED, same pass */
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK(c.resume);
    CHECK_EQ(c.from_pause, hold.pause_seq);
    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(servo_cmd_stale(&ended, &c));
    servo_screen_released(hold.pause_seq);
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
}

/* 2a: a drag after PAUSE leaves N: its POSITION is sent and holds the
 * servo there, and the screen keeps showing it driven there. */
TEST_CASE(a_drag_after_pause_outlives_the_let_go)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    int x, y;
    dial_at(30.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const servo_cmd_t pos = last_cmd();
    CHECK_EQ(pos.kind, SERVO_CMD_POSITION);
    CHECK_EQ(pos.from_pause, 0u);
    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(!servo_cmd_stale(&ended, &pos));     /* sent, and retires it */
    const uint16_t at = servo_screen_commanded();
    servo_screen_released(hold.pause_seq);
    CHECK_EQ(servo_screen_commanded(), at);    /* still driven there */
    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));
    const servo_cmd_t again = last_cmd();
    CHECK_EQ(again.kind, SERVO_CMD_POSITION);  /* the drag, said again */
    CHECK_EQ(again.value_us, at + 5u);
    close_settings();

    /* CENTRE the same way. */
    paused_on_n(&hold);
    tap(ARM_X + 40, BTN_Y);
    const servo_cmd_t centre = last_cmd();
    CHECK_EQ(centre.kind, SERVO_CMD_CENTRE);
    CHECK_EQ(centre.from_pause, 0u);
    servo_screen_released(hold.pause_seq);
    CHECK_EQ(servo_screen_commanded(), 1500u);
}

/* 2b: SPEED moves while a taken resume waits: both the resume and the
 * SWEEP said again derive from N and are dropped; the screen stops. */
TEST_CASE(a_resume_and_its_repeat_go_with_the_let_go)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    const servo_cmd_t resume = last_cmd();
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED, after it was taken */
    const servo_cmd_t repeat = last_cmd();
    CHECK_EQ(repeat.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(resume.from_pause, hold.pause_seq);
    CHECK_EQ(repeat.from_pause, hold.pause_seq);
    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(servo_cmd_stale(&ended, &resume));
    CHECK(servo_cmd_stale(&ended, &repeat));
    servo_screen_released(hold.pause_seq);
    CHECK(!servo_screen_sweeping());
}

/*
 * 2c, and a PAUSE after an unacknowledged resume: PAUSED, PAUSE, PAUSED
 * while HOLD N is out.  The second HOLD holds what pause N left, so it and
 * its resume derive from N and go with it; the screen, still on N, takes
 * the let-go and stops, and nothing is left held or sweeping.
 */
TEST_CASE(a_later_pause_goes_with_the_let_go)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    const servo_cmd_t r1 = last_cmd();
    const servo_cmd_t h2 = pause_go();         /* PAUSE: N+1 */
    CHECK_EQ(h2.from_pause, hold.pause_seq);
    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(servo_cmd_stale(&ended, &r1));
    CHECK(servo_cmd_stale(&ended, &h2));       /* dropped: no hold of rest */
    servo_screen_released(hold.pause_seq);
    CHECK(!servo_screen_paused());
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(servo_screen_commanded(), 1500u);

    /* And with a second PAUSED after it, before the let-go arrives. */
    paused_on_n(&hold);
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t a = last_cmd();
    const servo_cmd_t b = pause_go();
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t c = last_cmd();
    servo_pause_end_t e2 = { true, hold.pause_seq };
    CHECK(servo_cmd_stale(&e2, &a));
    CHECK(servo_cmd_stale(&e2, &b));
    CHECK(servo_cmd_stale(&e2, &c));
    servo_screen_released(hold.pause_seq);
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
}

/*
 * A chained HOLD -- PAUSE before the resume of pause N is acknowledged --
 * answered late: the panel lets go of the chain's root, N, so it drops the
 * commands tagged N and the screen, still on N, takes the let-go.  A HOLD
 * of a sweep running on its own ends its own pause.
 */
TEST_CASE(a_late_chained_hold_ends_its_root)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    CHECK_EQ(servo_cmd_pause_root(&hold), hold.pause_seq);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    const servo_cmd_t r1 = last_cmd();
    const servo_cmd_t h2 = pause_go();         /* PAUSE, chained */
    CHECK(h2.pause_seq != hold.pause_seq);
    const uint16_t root = servo_cmd_pause_root(&h2);
    CHECK_EQ(root, hold.pause_seq);
    servo_pause_end_t ended = { true, root };
    CHECK(servo_cmd_stale(&ended, &r1));
    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));                  /* a position from the chain */
    const servo_cmd_t pos = last_cmd();
    close_settings();
    CHECK(servo_cmd_stale(&ended, &pos));
    servo_screen_released(root);
    CHECK(!servo_screen_paused());
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(servo_cmd_pause_root(NULL), 0u);
}

/*
 * A PAUSED that starts a changed curve -- the TEST page changed while
 * paused -- is a new origin, not a resume: it carries no pause, survives a
 * let-go of the old one, and the screen keeps waiting for its start.
 */
TEST_CASE(a_changed_curve_from_paused_is_a_new_start)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    settings_set(SET_SERVO_TEST_HZ, 1.0f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: a new curve */
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK(!c.resume);
    CHECK_EQ(c.from_pause, 0u);
    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(!servo_cmd_stale(&ended, &c));
    servo_screen_released(hold.pause_seq);
    CHECK(servo_screen_sweeping());            /* still waiting for it */
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.125f);
    CHECK(servo_screen_commanded() > 1760u);
}

/*
 * A sweep-button tap whose command has not left, then the let-go of the
 * pause, then a second tap in the next frame's first drain: the second tap
 * is a tap of its own, not an undo across the notification.  Variant A:
 * PAUSED pending, the screen stops, and the next tap starts a sweep from
 * no pause.  Variant A2: a taken resume, PAUSE pending, the screen stops,
 * and the next tap does not restore a wait for the dropped resume.
 */
TEST_CASE(no_undo_across_a_let_go)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED, pending */
    servo_screen_released(hold.pause_seq);
    CHECK(!servo_screen_sweeping());
    tap(SWEEP_X, BTN_Y);                       /* a new SWEEP */
    servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK(!c.resume);
    CHECK_EQ(c.from_pause, 0u);
    CHECK(servo_screen_sweeping());
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.25f);
    CHECK(servo_screen_commanded() > 1760u);

    paused_on_n(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: taken, dropped */
    (void)last_cmd();
    tap(SWEEP_X, BTN_Y);                       /* PAUSE, pending */
    servo_screen_released(hold.pause_seq);
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
    tap(SWEEP_X, BTN_Y);                       /* not an undo */
    c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK(!c.resume);
    CHECK_EQ(c.from_pause, 0u);
}

/*
 * A resume waiting for its acknowledgement, a PAUSE and PAUSED drained in
 * one pass on top of it: the pair undoes itself and leaves the resume as it
 * was, its pause and origin included, so the late HOLD acknowledgement of
 * the first pause still moves the horn to the output held.
 */
TEST_CASE(an_undone_pair_keeps_the_resume_waiting)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.1f);
    const uint32_t tap_phase = servo_screen_curve_ms();
    const servo_cmd_t hold = pause_go();
    frames(0.1f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED, before the ack */
    CHECK(last_cmd().resume);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE ... */
    tap(SWEEP_X, BTN_Y);                       /* ... PAUSED, same pass */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    frames(0.2f);
    servo_screen_sweep_held(hold.pause_seq, tap_phase + 307u);
    const uint16_t held = servo_screen_drawn();
    CHECK(held > 1870u && held < 1900u);
}

/*
 * A resume acknowledged while feedback is valid uses its origin up: the
 * repeat of the acknowledgement after feedback has gone works on from the
 * reading, 1700 us, not from the 1600 saved at the tap.
 */
TEST_CASE(a_resume_origin_is_used_once)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    servo_screen_feedback(1500u, 0.2f, true);
    sweep_go();
    frames(0.2f);
    servo_screen_feedback(1600u, 0.2f, true);
    frames(0.05f);
    const servo_cmd_t hold = pause_go();
    const uint32_t kept = servo_screen_curve_ms();
    ack_hold(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: origin 1600 */
    CHECK(last_cmd().resume);
    servo_screen_feedback(1700u, 0.2f, true);
    frames(0.05f);
    ack_resume(kept, 0u);                      /* with feedback */
    frames(0.1f);
    servo_screen_feedback(0u, 0.0f, false);    /* gone */
    ack_resume(kept, 0u);                      /* said again */
    CHECK(abs((int)servo_screen_drawn() - 1700) <= 2);
}

/* A refused sweep drives nothing: a change of profile after it says no
 * position. */
TEST_CASE(a_refused_sweep_leaves_nothing_driven)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    servo_screen_sweep_refused();
    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));
    CHECK(last_cmd().kind != SERVO_CMD_POSITION);
    close_settings();
}

/*
 * With feedback, a HOLD acknowledged after an early PAUSED keeps the
 * reading for the resume: feedback gone before the resume's
 * acknowledgement, the resume goes on from 1700 us, not the tap's 1600.
 */
TEST_CASE(an_early_resume_keeps_the_reading_at_the_hold)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    servo_screen_feedback(1500u, 0.2f, true);
    sweep_go();
    frames(0.2f);
    servo_screen_feedback(1600u, 0.2f, true);
    frames(0.05f);
    const uint32_t tap_phase = servo_screen_curve_ms();
    const servo_cmd_t hold = pause_go();
    tap(SWEEP_X, BTN_Y);                       /* PAUSED, early */
    CHECK(last_cmd().resume);
    servo_screen_feedback(1700u, 0.2f, true);
    frames(0.05f);
    servo_screen_sweep_held(hold.pause_seq, tap_phase + 100u);
    servo_screen_feedback(0u, 0.0f, false);    /* gone */
    ack_resume(tap_phase + 100u, 0u);
    CHECK(abs((int)servo_screen_drawn() - 1700) <= 2);
}

/*
 * A HOLD the panel had to let go of -- answered only at a retry, or after
 * longer than a hold lives -- while the curve was drawn on: the pause ends,
 * and a profile change after it says no position from the drawing, only
 * the rest the surfaces were released to.
 */
TEST_CASE(a_released_pause_says_no_drawn_angle)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    sweep_go();
    frames(0.3f);
    (void)pause_go();
    frames(0.2f);                              /* drawn on, not acked */
    servo_screen_released(g_hold_seq);
    CHECK(!servo_screen_paused());
    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
    close_settings();
    frames(0.6f);                              /* no drain left */
    open_settings();
    tap(TRIM_DN_X, ROW_Y(3));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
}

/*
 * Leaving while a PAUSE is drawn on, and opening SERVO again: no curve,
 * drain or acknowledgement of the old sweep is drawn, and a new sweep
 * starts from its own beginning.
 */
TEST_CASE(leaving_forgets_the_sweep_model)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    const uint16_t old_seq = g_sweep_seq;
    frames(0.2f);
    const servo_cmd_t hold = pause_go();       /* drawn on, not acked */
    scr->leave();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);
    servo_invalidate();                        /* opened again */
    const uint16_t at = servo_screen_drawn();
    frames(0.3f);
    CHECK_EQ(servo_screen_drawn(), at);        /* nothing stale drawn */
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
    ack_hold(&hold);                           /* late: changes nothing */
    servo_screen_sweep_started(old_seq, 0u, SERVO_SWEEP_FROM_REST, 0u);
    CHECK_EQ(servo_screen_drawn(), at);

    servo_screen_set_armed(false);             /* the disarm, answered */
    servo_screen_set_armed(true);
    sweep_go();
    frames(0.25f);
    CHECK(servo_screen_commanded() > 1760u && servo_screen_commanded() < 1800u);
}

/*
 * While a start waits for its acknowledgement, a SPEED change and a curve
 * change replace what the screen would draw, not what was sent.  A SPEED
 * change is a repeat of the same curve: the start's own acknowledgement,
 * 307 ms late, is worked through at the start's SPEED, 12 %, not the new
 * 100 %.  A curve change is a new start: the first start's acknowledgement
 * does not end the wait, the new one's does, with its own curve.
 */
TEST_CASE(a_start_acknowledgement_applies_to_its_own_command)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t start = last_cmd();
    CHECK_EQ(start.kind, SERVO_CMD_SWEEP);
    tap(ARM_X + ARM_W - 1, SPEED_Y);           /* SPEED 100 % */
    const servo_cmd_t repeat = last_cmd();
    CHECK_EQ(repeat.kind, SERVO_CMD_SWEEP);
    CHECK(repeat.start_seq != start.start_seq);
    frames(0.3f);
    CHECK_EQ(servo_screen_drawn(), 1500u);     /* not before it is known */
    servo_screen_sweep_started(start.start_seq, 307u, SERVO_SWEEP_FROM_REST,
                               0u);
    const uint16_t at = servo_screen_drawn();
    CHECK(at > 1500u && at <= 1575u);          /* 307 ms at 240 a second */

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t first = last_cmd();
    settings_set(SET_SERVO_TEST_HZ, 1.0f);
    frames(0.025f);                            /* tick() restarts it */
    const servo_cmd_t second = last_cmd();
    CHECK_EQ(second.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(second.sweep_mhz, 1000u);
    servo_screen_sweep_started(first.start_seq, 0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.2f);
    CHECK_EQ(servo_screen_commanded(), 1500u); /* still waiting */
    servo_screen_sweep_started(second.start_seq, 0u, SERVO_SWEEP_FROM_REST,
                               0u);
    frames(0.125f);                            /* 1 Hz, 128 ms in */
    CHECK(servo_screen_commanded() > 1760u);
}

/*
 * The start's write failed and a repeat -- SPEED moved -- started the sweep
 * at the far end instead: only the repeat is acknowledged.  The wait ends
 * with it, at the repeat's SPEED, 100 %: 307 ms into the 0.5 Hz sine,
 * near 1829 us.  The same for a resume whose RESUME failed: the repeat
 * that followed it started the curve from rest.  An acknowledgement of a
 * command older than the start, or newer than any posted, is not taken.
 */
TEST_CASE(a_later_repeat_acknowledged_ends_the_wait)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t start = last_cmd();
    tap(ARM_X + ARM_W - 1, SPEED_Y);           /* SPEED 100 % */
    const servo_cmd_t repeat = last_cmd();
    CHECK_EQ(repeat.kind, SERVO_CMD_SWEEP);
    frames(0.3f);
    servo_screen_sweep_started((uint16_t)(start.start_seq - 1u), 0u,
                               SERVO_SWEEP_FROM_REST, 0u);
    servo_screen_sweep_started((uint16_t)(repeat.start_seq + 1u), 0u,
                               SERVO_SWEEP_FROM_REST, 0u);
    CHECK_EQ(servo_screen_drawn(), 1500u);     /* neither is this wait's */
    servo_screen_sweep_started(repeat.start_seq, 307u, SERVO_SWEEP_FROM_REST,
                               0u);
    CHECK(servo_screen_drawn() > 1800u && servo_screen_drawn() < 1860u);

    /* A resume, SPEED moved after it was taken, and only the repeat
     * acknowledged: a start from rest, out of the wait. */
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.25f);
    const servo_cmd_t hold = pause_go();
    ack_hold(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    const servo_cmd_t resume = last_cmd();
    CHECK(resume.resume);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED moved */
    const servo_cmd_t again = last_cmd();
    CHECK_EQ(again.kind, SERVO_CMD_SWEEP);
    CHECK(!again.resume);
    const uint16_t held = servo_screen_drawn();
    frames(0.2f);
    CHECK_EQ(servo_screen_drawn(), held);
    servo_screen_sweep_started(again.start_seq, 0u, SERVO_SWEEP_FROM_REST,
                               0u);
    frames(0.25f);
    CHECK(servo_screen_commanded() > 1760u && servo_screen_commanded() < 1800u);
}

/*
 * TRAVEL 90 to 45 deg while a sweep runs: the far end runs the old curve
 * under the old mapping until the restart is acknowledged, so the drawing
 * goes on to the old curve's peak, 1900 us, not clamped at 45 deg (1750 us).
 * After the acknowledgement the new curve keeps within 45 deg.
 */
TEST_CASE(a_restart_drains_the_old_curve_in_its_old_mapping)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.4f);
    open_settings();
    tap(ROW_L_X, ROW_Y(4));                    /* TRAVEL */
    keys("45");
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.sweep_span, 200u);
    frames(0.15f);                             /* the dwell at the peak */
    CHECK(servo_screen_drawn() > 1880u);
    acked(0u, SERVO_SWEEP_FROM_HERE, 0u);
    frames(0.6f);
    CHECK(servo_screen_drawn() <= 1750u);
    CHECK(servo_screen_commanded() <= 1750u);
}

/*
 * PAUSE and PAUSED drained in one pass, after a late frame: the HOLD never
 * left, so neither does a RESUME.  Nothing is sent, the sweep runs on, and
 * the curve's clock is untouched.  SWEEP and PAUSE in one pass likewise
 * leave nothing sent and no sweep, and a command queued before them is
 * still queued.
 */
TEST_CASE(two_sweep_taps_in_one_pass_send_nothing)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.3f);
    const uint32_t phase = servo_screen_curve_ms();
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    tap(SWEEP_X, BTN_Y);                       /* PAUSED, same pass */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(servo_screen_sweeping());
    CHECK(!servo_screen_paused());
    CHECK_EQ(servo_screen_curve_ms(), phase);
    frames(0.2f);                              /* on, as at the far end */
    CHECK(servo_screen_commanded() > 1880u);
    /* And the pair after it is a pair again. */
    (void)pause_go();

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    int x, y;
    dial_at(30.0f, ARC_R - 20, &x, &y);
    tap(x, y);                                 /* a position, queued */
    const uint16_t pos = servo_screen_commanded();
    tap(SWEEP_X, BTN_Y);                       /* SWEEP */
    tap(SWEEP_X, BTN_Y);                       /* PAUSE, same pass */
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, pos);
}

/*
 * At SPEED 100 % the far end takes a command at once, and so does the
 * drawing: a square's edge inside the HOLD's acknowledgement interval puts
 * the paused output at the far end, 1100 us, not part way down a ramp, and
 * a trim changed while paused says that.  A drag at 100 % is drawn at the
 * command on the next frame.
 */
TEST_CASE(speed_100_is_immediate_in_the_drawing_and_the_replay)
{
    fresh();
    settings_set(SET_SERVO_CURVE, 0.0f);       /* SQUARE */
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(1.1f);                              /* +A until 1200 ms */
    CHECK_EQ(servo_screen_drawn(), 1900u);
    const servo_cmd_t hold = pause_go();
    frames(0.2f);                              /* the edge passes */
    ack_hold(&hold);
    CHECK_EQ(servo_screen_drawn(), 1100u);
    CHECK_EQ(servo_screen_commanded(), 1100u);
    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, 1105u);
    close_settings();

    fresh();
    int x, y;
    dial_at(60.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    scr->tick(1.0f / 39.0f);
    CHECK_EQ(servo_screen_drawn(), servo_screen_commanded());
}

/*
 * PAUSED does not move the horn: the far end stays held until the RESUME
 * reaches it.  Acknowledged 300 ms after it did, the drawn output is worked
 * on from the held point for those 300 ms at SPEED, as the far end's
 * output slews -- the bank the IOmcu runs, fed the same curve.
 */
TEST_CASE(a_resume_is_drawn_from_its_acknowledgement)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    sweep_go();
    frames(0.3f);
    const servo_cmd_t hold = pause_go();
    const uint32_t kept = servo_screen_curve_ms();
    ack_hold(&hold);
    const uint16_t held = servo_screen_drawn();
    frames(1.0f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    const servo_cmd_t c = last_cmd();
    CHECK(c.resume);
    frames(0.3f);
    CHECK_EQ(servo_screen_drawn(), held);      /* not before it is known */
    ack_resume(kept, 307u);

    outputs_t far;
    outputs_init(&far, 0u);
    CHECK(outputs_set_role(&far, 0, OUT_ROLE_SURFACE));
    CHECK(outputs_set_endpoints(&far, 0, c.min_us, c.max_us));
    outputs_arm(&far, true, 0u);
    CHECK(outputs_set(&far, 0, (uint16_t)(held - c.min_us), 0u));
    outputs_step(&far, 0u);
    CHECK(outputs_set_slew(&far, 0, c.slew_per_s));
    sweep_t w;
    const sweep_cfg_t cfg = { (sweep_kind_t)c.sweep_kind, c.sweep_mhz,
                              c.sweep_span, c.sweep_dwell_ms, 0u };
    CHECK(sweep_start(&w, &cfg, 0u));
    for (uint32_t t = 1u; t <= 307u; ++t) {
        uint16_t cmd = 0;
        (void)sweep_step(&w, kept + t, &cmd);
        (void)outputs_set(&far, 0, cmd, t);
        outputs_step(&far, t);
    }
    const int far_us = (int)outputs_pulse_us(&far, 0);
    CHECK(far_us > (int)held + 60);
    CHECK(abs((int)servo_screen_drawn() - far_us) <= 3);
}

/* A curve changed on the TEST page while paused is a new sweep, from its
 * beginning, not a resume. */
TEST_CASE(a_curve_changed_while_paused_starts_over)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    frames(0.25f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    settings_set(SET_SERVO_TEST_HZ, 1.0f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK(!c.resume);
    CHECK_EQ(c.sweep_mhz, 1000u);
}

/* Pause, then each thing that ends a pause, and the command it sends. */
static void paused_sweep(void)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.2f);
    (void)pause_go();
    CHECK(servo_screen_paused());
}

/*
 * A pause ends as a hold did: CENTRE, RELEASE, a finger on the dial, a
 * disarm, STOP, leaving the screen, an IOmcu that stops sweeping, a
 * hold the panel had to let go of, and a changed profile, which says the
 * position again.  The button reads SWEEP after each.
 */
TEST_CASE(a_pause_ends_where_a_hold_ended)
{
    paused_sweep();
    tap(ARM_X + 40, BTN_Y);                    /* CENTRE */
    CHECK(!servo_screen_paused());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_CENTRE);

    paused_sweep();
    tap(ARM_X + ARM_W - 40, BTN_Y);            /* RELEASE */
    CHECK(!servo_screen_paused());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);

    paused_sweep();
    int x, y;
    dial_at(30.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    CHECK(!servo_screen_paused());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);

    paused_sweep();
    servo_screen_set_armed(false);
    CHECK(!servo_screen_paused());

    paused_sweep();
    servo_screen_cancel_arm();                 /* STOP */
    CHECK(!servo_screen_paused());

    paused_sweep();
    scr->leave();
    CHECK(!servo_screen_paused());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);

    paused_sweep();
    servo_screen_set_sweep(false);
    CHECK(!servo_screen_paused());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);

    paused_sweep();
    servo_screen_released(g_hold_seq);                   /* unrepeated for 500 ms */
    CHECK(!servo_screen_paused());

    paused_sweep();
    choose_type(1);
    CHECK(!servo_screen_paused());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);

    /* The button has left the paused fill. */
    scr->render(&cv, 0);
    CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_WARN)), 0);
}

/*
 * Without feedback the horn is drawn where the far end's output is, slewing
 * as the IOmcu slews -- in its command units -- so the horn PAUSE
 * leaves drawn is where the IOmcu holds the servo, with CENTRE off
 * the middle as well.  The far end
 * here is the bank the IOmcu runs, fed the same sweep.
 */
TEST_CASE(hold_without_feedback_matches_the_far_ends_slew)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    open_settings();
    tap(ROW_R_X, ROW_Y(2));                    /* PULSE CENTRE */
    keys("1520");
    tap(ROW_L_X, ROW_Y(2));                    /* PULSE MIN */
    keys("900");
    close_settings();
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED 10 % */
    (void)last_cmd();
    tap(SWEEP_X, BTN_Y);
    servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);

    outputs_t far;
    outputs_init(&far, 0u);
    CHECK(outputs_set_role(&far, 0, OUT_ROLE_SURFACE));
    CHECK(outputs_set_endpoints(&far, 0, c.min_us, c.max_us));
    CHECK(outputs_set_slew(&far, 0, c.slew_per_s));
    outputs_arm(&far, true, 0u);
    sweep_t w;
    const sweep_cfg_t cfg = { (sweep_kind_t)c.sweep_kind, c.sweep_mhz,
                              c.sweep_span, c.sweep_dwell_ms, 0u };
    CHECK(sweep_start(&w, &cfg, 0u));
    for (int i = 1; i <= 20; ++i) {            /* half a second */
        scr->tick(1.0f / 39.0f);
        const uint32_t t = (uint32_t)((float)i * 1000.0f / 39.0f);
        uint16_t cmd = 0;
        (void)sweep_step(&w, t, &cmd);
        (void)outputs_set(&far, 0, cmd, t);
        outputs_step(&far, t);
    }
    const servo_cmd_t hold = pause_go();
    ack_hold(&hold);                           /* where the far end is */
    const int far_us = (int)outputs_pulse_us(&far, 0);
    CHECK(far_us > 1560);                      /* well on its way */
    CHECK(abs((int)servo_screen_commanded() - far_us) <= 6);
}

/*
 * Without feedback the horn follows only what the far end is known to do.
 * A sweep asked for is not drawn until its start is acknowledged; then the
 * output is worked on from where it was drawn when the far end began, at
 * SPEED: acknowledged 300 ms after it began, it is where one acknowledged
 * at once is 300 ms in.  A sweep that froze unrepeated starts from where it
 * froze.
 */
TEST_CASE(the_drawn_output_starts_where_the_far_ends_output_starts)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED 12 % */
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    frames(0.3f);                              /* not acknowledged */
    CHECK_EQ(servo_screen_drawn(), 1500u);
    CHECK_EQ(servo_screen_commanded(), 1500u);
    acked(307u, SERVO_SWEEP_FROM_REST, 0u);
    const uint16_t late = servo_screen_drawn();

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);
    sweep_go();
    frames(0.3f);
    const uint16_t at_once = servo_screen_drawn();
    CHECK(at_once > 1520u);
    CHECK(abs((int)late - (int)at_once) <= 3);

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);
    sweep_go();
    frames(0.2f);
    const uint16_t at_02 = servo_screen_drawn();
    frames(0.3f);
    acked(0u, SERVO_SWEEP_FROM_FROZEN, 307u);
    CHECK(at_02 > 1520u);
    CHECK(abs((int)servo_screen_drawn() - (int)at_02) <= 3);
}

/* The horn is drawn along the far end's curve from when it started there. */
TEST_CASE(the_horn_follows_the_curve_from_where_the_far_end_started_it)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.5f);                              /* the peak, as drawn */
    CHECK(servo_screen_commanded() > 1880u);
    acked(0u, SERVO_SWEEP_FROM_HERE, 0u);  /* just now */
    scr->tick(0.001f);
    const uint16_t at = servo_screen_commanded();
    CHECK(at > 1490u && at < 1520u);
}

/*
 * Leaving disarms, the same rule as the motor bench's: a screen that does
 * not show the horn must not be holding it somewhere, and it must not leave
 * the bench armed behind itself either.  The disarm lets go of the pin on
 * its way, so one command covers both.
 */
TEST_CASE(leaving_disarms_and_lets_go_of_the_output)
{
    fresh();
    scr->leave();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);
}

/*
 * The trim moves the pulse the same angle maps to; the angle itself does not
 * change, because the horn has not moved, the linkage under it has.
 */
TEST_CASE(trim_shifts_the_pulse_and_not_the_angle)
{
    fresh();
    int x, y;
    dial_at(0.0f, ARC_R - 30, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(servo_screen_commanded(), 1500);

    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));           /* trim up, five microseconds */
    CHECK_EQ(servo_screen_commanded(), 1505);
    tap(TRIM_DN_X, ROW_Y(3));
    CHECK_EQ(servo_screen_commanded(), 1500);
}

/* Feedback places the arm rather than animating towards it: a servo already
 * at 40° was never at zero, and the screen reports what the hardware did. */
TEST_CASE(feedback_is_shown_rather_than_travelled_to)
{
    fresh();
    servo_screen_feedback(1750, 0.8f, true);
    scr->render(&cv, 0);

    /* Rendering twice with no change must paint nothing, so the second pass
     * proves the first one settled rather than still easing somewhere. */
    gfx_color_t *first = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(first);
    memcpy(first, fb, (size_t)W * H * sizeof(gfx_color_t));
    scr->tick(0.05f);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(first, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(first);
}

/* ------------------------------------------- the review's cases, as tests */

/* As bench_frames(), with ONs from the screen let through only while
 * @p accept_on, and the highest set point an ON was asked at kept. */
static void bench_frames_on(uint32_t ms, bool accept_on, float *on_v_max)
{
    for (uint32_t k = 0; k < ms; k += 20u) {
        bench.now += 20u;
        servo_screen_clock(bench.now);
        supply_cmd_t c;
        while (supply_screen_poll_cmd(&c)) {
            if (c.off) {
                bench.out = false;
            } else if (c.on) {
                if (supply_screen_set_v() > *on_v_max) {
                    *on_v_max = supply_screen_set_v();
                }
                if (accept_on) {
                    bench.out = true;
                }
            }
        }
        supply_screen_set_output(bench.out);
        servo_cmd_t sc;
        while (servo_screen_take(&sc)) { }
        if ((int32_t)(bench.now - bench.next) >= 0) {
            bench.next += 100u;
            supply_state_t st;
            memset(&st, 0, sizeof(st));
            st.online = true;
            st.ok     = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
            st.output = bench.out;
            st.set_v  = supply_screen_set_v();
            st.set_i  = supply_screen_set_i();
            st.v      = bench.out ? st.set_v : 0.0f;
            st.mode   = bench.out ? SUPPLY_MODE_CV : SUPPLY_MODE_OFF;
            st.samples  = ++bench.samples;
            st.taken_ms = bench.now;
            st.stamped = true;
            servo_screen_supply(&st);
        }
        servo_screen_service();
        scr->tick(0.02f);
        bench_drain();
    }
}

/*
 * 8.4 V set on SUPPLY with the output off; a run without HV steps starts
 * and asks 4.8 V and its ON, which does not land.  OUTPUT ON held on SERVO
 * at 4.8 V while the run ends (no ON in 3 s): the set points do not go back
 * under the hold, and no ON is ever asked above 6.0 V.
 */
TEST_CASE(a_run_ending_under_output_ons_hold_switches_nothing_on_past_6_v)
{
    bench_fresh();
    short_runs();
    supply_screen_put(8.4f, 1.0f);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);
    float on_v = 0.0f;
    bench_frames_on(1500u, false, &on_v);          /* the ON does not land */
    on_v = 0.0f;
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    bench_frames_on(2800u, true, &on_v);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_testing());
    CHECK(on_v <= 6.0f + 1e-4f);
    CHECK(!(bench.out && supply_screen_set_v() > 6.0f + 1e-4f));
    bench_frames_on(2000u, true, &on_v);
    CHECK(on_v <= 6.0f + 1e-4f);
    /* The output came on at 4.8 V under the hold: the set points stay as
     * they are while it is on. */
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);
}

/*
 * The ON a run asks for, and the ON the HV warning's hold sends, belong to
 * SERVO as the switch's own hold does: leaving SERVO switches each off.
 */
TEST_CASE(leaving_servo_switches_off_an_on_a_run_or_the_hv_hold_asked)
{
    supply_cmd_t c;

    /* A run's ON. */
    bench_fresh();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    CHECK(supply_cmd(&c) && c.on);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SERVO);
    supply_screen_set_output(true);
    scr->leave();
    bool off = false;
    while (supply_cmd(&c)) {
        off = off || c.off;
    }
    CHECK(off);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);

    /* The HV warning's ON. */
    fresh();
    supply_screen_put(7.4f, 1.0f);
    scr->tick(0.025f);
    tap(SUP_OUT_X, SUP_ROW_Y);
    scr->tick(0.025f);
    hold_apply(2.3f);
    CHECK(supply_cmd(&c) && c.on);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SERVO);
    supply_screen_set_output(true);
    scr->leave();
    CHECK(supply_cmd(&c));
    CHECK(c.off && !c.on);
}

/* A set point raised past 6.0 V during OUTPUT ON's ordinary hold -- on
 * SUPPLY, or a run's end putting it back -- switches nothing on when the
 * hold completes: HV SERVOS ONLY opens instead. */
TEST_CASE(output_ons_hold_looks_at_the_set_point_again_as_it_completes)
{
    bench_fresh();
    supply_screen_put(5.0f, 1.0f);
    scr->tick(0.02f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(0.025f);
    }
    supply_screen_put(8.4f, 1.0f);
    for (int i = 0; i < 80; ++i) {
        scr->tick(0.025f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));
    scr->render(&cv, 0);
    CHECK_EQ(fb[6 * W + 6], ui_theme_color(UI_C_DANGER));   /* the warning */
    hold_apply(2.3f);
    supply_cmd_t c;
    CHECK(supply_cmd(&c));
    CHECK(c.on);
}

/* A run ended by leaving SERVO puts the set points back from the per-frame
 * service, with SERVO not ticked; one set on SUPPLY after the run ended is
 * left as the operator set it. */
TEST_CASE(the_set_points_go_back_whichever_screen_is_up)
{
    for (int changed = 0; changed < 2; ++changed) {
        bench_fresh();
        short_runs();
        supply_screen_put(5.5f, 1.5f);
        hold_start(2.3f);
        bench_frames(3000u);
        CHECK(bench.out);
        scr->leave();
        CHECK(!servo_screen_testing());
        if (changed) {
            supply_screen_put(5.2f, 1.0f);              /* on SUPPLY */
        }
        float on_v = 0.0f;
        for (uint32_t k = 0; k < 2000u; k += 20u) {    /* SERVO not ticked */
            bench.now += 20u;
            servo_screen_clock(bench.now);
            supply_cmd_t c;
            while (supply_screen_poll_cmd(&c)) {
                bench.out = c.off ? false : (c.on ? true : bench.out);
            }
            supply_screen_set_output(bench.out);
            if ((int32_t)(bench.now - bench.next) >= 0) {
                bench.next += 100u;
                supply_state_t st;
                memset(&st, 0, sizeof(st));
                st.online = true;
                st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
                st.output = bench.out;
                st.set_v = supply_screen_set_v();
                st.samples = ++bench.samples;
                st.taken_ms = bench.now;
                st.stamped = true;
                servo_screen_supply(&st);
            }
            servo_screen_service();
            bench_drain();
        }
        CHECK(!bench.out);
        if (changed) {
            CHECK(fabsf(supply_screen_set_v() - 5.2f) < 1e-4f);
            CHECK(fabsf(supply_screen_set_i() - 1.0f) < 1e-4f);
        } else {
            CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
            CHECK(fabsf(supply_screen_set_i() - 1.5f) < 1e-4f);
        }
        /* Back on SERVO, nothing is put back over it later. */
        supply_screen_put(5.1f, 1.0f);
        bench_frames_on(1000u, true, &on_v);
        CHECK(fabsf(supply_screen_set_v() - 5.1f) < 1e-4f);
    }
}

/* The restore waits for a sample taken after the OFF went: one taken
 * before it, showing the output off while an ON is on its way, is not
 * enough. */
TEST_CASE(the_set_points_wait_for_a_sample_after_the_off)
{
    bench_fresh();
    short_runs();
    supply_screen_put(8.4f, 1.0f);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    servo_screen_cancel_arm();                  /* STOP: OFF posted */
    CHECK(!servo_screen_testing());
    supply_cmd_t c;
    while (supply_screen_poll_cmd(&c)) { }       /* the OFF taken */
    servo_screen_service();
    /* A sample stamped before the next frame, showing off. */
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.set_v = supply_screen_set_v();
    st.samples = ++bench.samples;
    st.taken_ms = bench.now;
    st.stamped = true;
    servo_screen_supply(&st);
    servo_screen_service();
    servo_screen_service();
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);   /* not yet */
    /* The next frame and a sample of its own, but the module still reads
     * its output on: the panel asked it off and the PD mini has not yet
     * answered.  Not yet either. */
    bench.now += 20u;
    servo_screen_clock(bench.now);
    servo_screen_service();
    st.samples = ++bench.samples;
    st.taken_ms = bench.now;
    st.stamped = true;
    st.mode = SUPPLY_MODE_CV;
    servo_screen_supply(&st);
    servo_screen_service();
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);
    /* Nor from a supply that has stopped answering. */
    st.samples = ++bench.samples;
    st.mode = SUPPLY_MODE_OFF;
    st.online = false;
    servo_screen_supply(&st);
    servo_screen_service();
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);
    /* The module's own off: back. */
    st.samples = ++bench.samples;
    st.online = true;
    servo_screen_supply(&st);
    servo_screen_service();
    CHECK(fabsf(supply_screen_set_v() - 8.4f) < 1e-4f);
}

/* A reading stamped on tick 0 is stamped.  The restore compares the
 * stamp with the clock at the stage's start: a stamp of 0 older than that
 * is a sample from before the OFF and is not enough, and one newer, across
 * the 32-bit wrap, is. */
TEST_CASE(a_supply_reading_stamped_on_tick_zero_keeps_its_stamp)
{
    static const struct { uint32_t base; bool restores; } k_case[] = {
        { 0u,           false },    /* restore_ms 20: stamp 0 is older     */
        { 0xFFFFFFF0u,  false },    /* restore_ms 4, past the wrap         */
        { 0xFFFFFFE0u,  true },     /* restore_ms -12: stamp 0 is newer    */
    };
    for (size_t k = 0; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        bench_fresh();
        short_runs();
        supply_screen_put(8.4f, 1.0f);
        hold_start(2.3f);
        CHECK(servo_screen_testing());
        servo_screen_cancel_arm();
        supply_cmd_t c;
        while (supply_screen_poll_cmd(&c)) { }
        servo_screen_clock(k_case[k].base);
        servo_screen_service();                 /* OFF sent                */
        servo_screen_clock(k_case[k].base + 20u);
        servo_screen_service();                 /* waiting for a sample    */
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.online   = true;
        st.ok       = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        st.set_v    = supply_screen_set_v();
        st.mode     = SUPPLY_MODE_OFF;
        st.samples  = ++bench.samples;
        st.taken_ms = 0u;
        st.stamped  = true;
        servo_screen_supply(&st);
        servo_screen_service();
        servo_screen_service();
        const bool back = fabsf(supply_screen_set_v() - 8.4f) < 1e-4f;
        CHECK_EQ(back, k_case[k].restores);
    }
}

/* START TEST twice without a fresh screen: the second hold starts a second
 * run, and the button is drawn as a full redraw would between them. */
TEST_CASE(start_test_starts_a_second_run)
{
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    settle_drawn();
    two_buffers();
    open_test();
    CHECK(both_whole());
    CHECK_EQ(fb[(START_Y + UI_BAND_H * 0) * W + START_X - 60],
             fb1[(START_Y + UI_BAND_H * 0) * W + START_X - 60]);
    close_settings();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK_EQ(bench.ends, 2u);
}

/* The page redraws START TEST and its line when the last report has been
 * taken: no stale LAST REPORT STILL WRITING, no button left disabled. */
TEST_CASE(the_start_line_follows_the_report_being_taken)
{
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    servo_screen_test_files(12, true);
    open_test();
    two_buffers();
    /* Frames that keep the report back from the card. */
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        for (uint32_t k = 0; k < 500u; k += 20u) {
            bench.now += 20u;
            servo_screen_clock(bench.now);
            supply_cmd_t c;
            while (supply_screen_poll_cmd(&c)) {
                bench.out = c.off ? false : (c.on ? true : bench.out);
            }
            supply_screen_set_output(bench.out);
            servo_cmd_t sc;
            while (servo_screen_take(&sc)) {
                if (sc.kind == SERVO_CMD_POSITION) {
                    bench.cmd = sc.value_us;
                }
            }
            const float amps = bench.out ? servo_sim_step(&bench.sv, bench.cmd, bench.now)
                                     : 0.0f;
            if ((int32_t)(bench.now - bench.next) >= 0) {
                bench.next += 100u;
                supply_state_t st;
                memset(&st, 0, sizeof(st));
                st.online = true;
                st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
                st.output = bench.out;
                st.set_v = supply_screen_set_v();
                st.v = bench.out ? st.set_v : 0.0f;
                st.i = amps;
                st.samples = ++bench.samples;
                st.taken_ms = bench.now;
                st.stamped = true;
                servo_screen_supply(&st);
            }
            scr->tick(0.02f);
        }
        scr->render(&cv, 0);
        scr->render(&cv1, 1);
    }
    CHECK(!servo_screen_testing());
    CHECK(both_whole());                         /* the report waits */
    bench_drain();                               /* taken */
    for (int i = 0; i < 4; ++i) {
        scr->tick(0.02f);
        scr->render((i & 1) ? &cv1 : &cv, i & 1);
    }
    CHECK(both_whole());
}

/* ARM FIRST goes once the bench is armed, and a page drawn then is the one
 * a full redraw draws. */
TEST_CASE(arm_first_goes_once_the_bench_is_armed)
{
    fresh();
    memset(&bench, 0, sizeof(bench));
    open_test();
    tap(START_X, START_Y);                       /* ARM FIRST */
    two_buffers();
    gfx_color_t *was = malloc(FB_BYTES);
    REQUIRE_ALLOC(was);
    memcpy(was, fb, FB_BYTES);
    servo_screen_set_armed(true);
    settle_drawn();                              /* ARM's flash is over */
    CHECK(both_whole());
    CHECK(memcmp(fb, was, FB_BYTES) != 0);
    free(was);
}

/* A run whose steps the caps do not reach is refused before any warning:
 * with VOLTAGE MAX 8.0 V and HV SERVO on, START TEST opens no HV warning
 * naming 8.0 V, and no run starts. */
TEST_CASE(a_run_outside_the_caps_is_refused_before_the_warning)
{
    bench_fresh();
    short_runs();
    settings_set(SET_SUPPLY_V_MAX, 8.0f);
    supply_screen_limits_changed();
    open_run_settings(0);
    tap(ROW_R_X, HV_ROW_Y);                      /* HV SERVO on */
    CHECK(home_tag());                           /* back to the cards */
    tap(START_X, START_Y);
    scr->tick(0.02f);
    scr->render(&cv, 0);
    CHECK(fb[6 * W + 6] != ui_theme_color(UI_C_DANGER));
    close_settings();
    hold_start(2.3f);
    CHECK(!servo_screen_testing());

    /* The warning open at 21 V, and the cap brought under its top step
     * while it stands: the hold starts nothing. */
    bench_fresh();
    short_runs();
    open_run_settings(0);
    tap(ROW_R_X, HV_ROW_Y);
    CHECK(home_tag());
    tap(START_X, START_Y);
    scr->tick(0.02f);
    scr->render(&cv, 0);
    CHECK_EQ(fb[6 * W + 6], ui_theme_color(UI_C_DANGER));
    gfx_color_t *named = malloc(FB_BYTES);
    REQUIRE_ALLOC(named);
    memcpy(named, fb, FB_BYTES);
    settings_set(SET_SUPPLY_V_MAX, 8.0f);
    supply_screen_limits_changed();
    scr->tick(0.02f);
    servo_invalidate();
    scr->render(&cv, 0);
    /* Still the run's 8.40 V, not a value snapped under the cap. */
    for (int y = 60; y < 100; ++y) {
        CHECK_EQ(memcmp(&fb[(size_t)y * W], &named[(size_t)y * W],
                        490 * sizeof(gfx_color_t)), 0);
    }
    free(named);
    hold_apply(2.3f);
    CHECK(!servo_screen_testing());
}

/* The left card names the report only when the card took it whole. */
TEST_CASE(the_result_names_the_report_only_when_it_was_written)
{
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    servo_screen_cancel_arm();
    scr->tick(0.02f);
    servo_screen_test_files(12, false);
    settle_drawn();
    two_buffers();
    CHECK(both_whole());
    gfx_color_t *without = malloc(FB_BYTES);
    REQUIRE_ALLOC(without);
    memcpy(without, fb, FB_BYTES);
    servo_screen_test_files(12, true);
    CHECK(both_whole());
    CHECK(memcmp(fb, without, FB_BYTES) != 0);
    free(without);
}

/* The overlay's pixels, the left card, against @p want's. */
static bool overlay_same(const gfx_color_t *want)
{
    for (int y = 6; y < 426; ++y) {
        if (memcmp(&fb[(size_t)y * W + 6], &want[(size_t)y * W + 6],
                   488 * sizeof(gfx_color_t)) != 0) {
            return false;
        }
    }
    return true;
}

/* A refusal goes with the change that answers it: NO STEP CHOSEN, then
 * STEP 4.8 V turned on; SUPPLY NOT ANSWERING, then the supply answering.
 * The page is then the one drawn with nothing refused. */
TEST_CASE(a_refusal_goes_with_the_edit_that_answers_it)
{
    gfx_color_t *want = malloc(FB_BYTES);
    REQUIRE_ALLOC(want);
    for (int k = 0; k < 2; ++k) {
        /* The page with nothing refused. */
        bench_fresh();
        settings_set(SET_SERVO_STEP_60, 0.0f);
        settings_set(SET_SERVO_BROWNOUT, 0.0f);
        open_test();
        scr->tick(0.02f);
        servo_invalidate();
        scr->render(&cv, 0);
        memcpy(want, fb, FB_BYTES);

        /* Refused, then answered. */
        bench_fresh();
        settings_set(SET_SERVO_STEP_60, 0.0f);
        settings_set(SET_SERVO_BROWNOUT, 0.0f);
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        st.set_v = supply_screen_set_v();
        if (k == 0) {
            settings_set(SET_SERVO_STEP_48, 0.0f);
        } else {
            st.samples = ++bench.samples;            /* not answering */
            servo_screen_supply(&st);
        }
        hold_start(2.3f);
        CHECK(!servo_screen_testing());
        if (k == 0) {
            settings_set(SET_SERVO_STEP_48, 1.0f);
        } else {
            st.online = true;
            st.samples = ++bench.samples;
            servo_screen_supply(&st);
        }
        for (int i = 0; i < 10; ++i) {
            scr->tick(0.02f);
        }
        servo_invalidate();
        scr->render(&cv, 0);
        CHECK(overlay_same(want));
    }
    free(want);
}

/* OUTPUT ON held on SUPPLY holds the restore back as a hold on SERVO does:
 * the ON lands at the set point shown at the press. */
TEST_CASE(a_hold_on_supplys_output_on_holds_the_restore_back)
{
    bench_fresh();
    short_runs();
    supply_screen_put(5.5f, 1.5f);
    hold_start(2.3f);
    bench_frames(3000u);
    scr->leave();                                /* ends the run */
    CHECK(!servo_screen_testing());
    const float v_end = supply_screen_set_v();
    /* The OFF goes; then, on SUPPLY, OUTPUT ON pressed (its right rail,
     * 676, 318) before a sample shows the output off. */
    supply_cmd_t c;
    while (supply_screen_poll_cmd(&c)) {
        bench.out = c.off ? false : (c.on ? true : bench.out);
    }
    supply_screen_set_output(bench.out);
    const ui_screen_t *sup = supply_screen();
    const touch_event_t down = { .type = TOUCH_EVENT_DOWN,
                                 .point = { .id = 1, .x = 676, .y = 318,
                                            .strength = 40 } };
    sup->event(&down);
    CHECK(supply_screen_output_held());
    float on_v = 0.0f;
    for (uint32_t k = 0; k < 600u; k += 20u) {   /* samples, all off */
        bench.now += 20u;
        servo_screen_clock(bench.now);
        while (supply_screen_poll_cmd(&c)) {
            bench.out = c.off ? false : (c.on ? true : bench.out);
        }
        supply_screen_set_output(bench.out);
        if ((int32_t)(bench.now - bench.next) >= 0) {
            bench.next += 100u;
            supply_state_t st;
            memset(&st, 0, sizeof(st));
            st.online = true;
            st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
            st.output = bench.out;
            st.set_v = supply_screen_set_v();
            st.samples = ++bench.samples;
            st.taken_ms = bench.now;
            st.stamped = true;
            servo_screen_supply(&st);
        }
        servo_screen_service();
        bench_drain();
    }
    CHECK(fabsf(supply_screen_set_v() - v_end) < 1e-4f);    /* held back */
    const touch_event_t up = { .type = TOUCH_EVENT_UP,
                               .point = { .id = 1, .x = 676, .y = 318,
                                          .strength = 40 } };
    sup->event(&up);                             /* let go early: no ON */
    CHECK(!supply_screen_output_held());
    bench_frames_on(600u, true, &on_v);
    CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
    CHECK_EQ(on_v, 0.0f);
}

/* ------------------------------------------------- the output encoder */

/* The DUT page's rows: AS5600 on row 2 and ENC CENTRE on row 3 of the left
 * column. */
TEST_CASE(the_encoders_angle_replaces_the_dashes_in_the_measured_row)
{
    fresh();
    scr->render(&cv, 0);
    gfx_color_t *none = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(none);
    memcpy(none, fb, (size_t)W * H * sizeof(gfx_color_t));
    const servo_test_enc_t e = { true, 3100u, 0u, 1000u, false, false, false, false };

    /* AS5600 off in SETUP: the reading is ignored. */
    CHECK(!settings_get_bool(SET_ENC_EN));
    servo_screen_encoder(&e);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(none, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* On: the MEASURED row shows the angle from the centre count. */
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_invalidate();
    servo_screen_encoder(&e);
    scr->render(&cv, 0);
    CHECK(memcmp(none, fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
    gfx_color_t *shown = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(shown);
    memcpy(shown, fb, (size_t)W * H * sizeof(gfx_color_t));

    /* Another count, another angle; the same count, the same picture. */
    const servo_test_enc_t e2 = { true, 3200u, 0u, 1020u, false, false, false, false };
    servo_screen_encoder(&e2);
    scr->render(&cv, 0);
    CHECK(memcmp(shown, fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
    servo_screen_encoder(&e);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(shown, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* A reading that is not valid is the dashes again. */
    const servo_test_enc_t gone = { false, 0u, 0u, 1040u, false, false, false, false };
    servo_screen_encoder(&gone);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(none, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    servo_screen_encoder(NULL);
    free(none);
    free(shown);
}

/* The MEASURED row of the right card, copied out of the frame. */
static void measured_row(gfx_color_t *out)
{
    for (int y = 0; y < 20; ++y) {
        memcpy(out + y * 292, fb + (size_t)(64 + y) * W + 502,
               292 * sizeof(gfx_color_t));
    }
}

/* With the encoder on in SETUP and no valid reading, the MEASURED row shows
 * the dashes, not the angle of the pulse position. */
TEST_CASE(an_enabled_encoder_without_a_reading_does_not_fall_back_to_the_feedback)
{
    fresh();
    gfx_color_t *a = malloc(20 * 292 * sizeof(gfx_color_t));
    REQUIRE_ALLOC(a);
    gfx_color_t *row_b = malloc(20 * 292 * sizeof(gfx_color_t));
    REQUIRE_ALLOC(row_b);

    /* Encoder off: the feedback's angle is on show and follows it. */
    servo_screen_feedback(1700u, 0.2f, true);
    scr->render(&cv, 0);
    measured_row(a);
    servo_screen_feedback(1300u, 0.2f, true);
    scr->render(&cv, 0);
    measured_row(row_b);
    CHECK(memcmp(a, row_b, 20 * 292 * sizeof(gfx_color_t)) != 0);

    /* Encoder on, no reading: dashes, whatever the feedback says. */
    settings_set(SET_ENC_EN, 1.0f);
    servo_invalidate();
    servo_screen_feedback(1700u, 0.2f, true);
    scr->render(&cv, 0);
    measured_row(a);
    servo_screen_feedback(1300u, 0.2f, true);
    scr->render(&cv, 0);
    measured_row(row_b);
    CHECK_EQ(memcmp(a, row_b, 20 * 292 * sizeof(gfx_color_t)), 0);

    /* A reading puts the encoder's angle there. */
    const servo_test_enc_t e = { true, 3100u, 0u, 1000u, false, false, false, false };
    servo_screen_encoder(&e);
    scr->render(&cv, 0);
    measured_row(row_b);
    CHECK(memcmp(a, row_b, 20 * 292 * sizeof(gfx_color_t)) != 0);
    free(a);
    free(row_b);
}

/*
 * SETUP > OUTPUTS' PROG PORT entry, which the TARGET row opens: its SERVO
 * TESTER segment (OUTPUTS | PROG PORT, left column x 19 to 279, y 204 to
 * 240) and its PORT PWM segment (ON | OFF, y 274 to 310), two tabs of
 * 128 px 4 px apart.  Mirrored from outputs_screen.c.
 */
#define OSEG_X(i)      (19 + 64 + (i) * 132)
#define OSEG_TESTER_Y  (242 + 20 + 18)
#define OSEG_PWM_Y     (306 + 20 + 18)

static void out_ev(int x, int y, touch_event_type_t type)
{
    const touch_event_t e = { .type = type,
                              .point = { .id = 2, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    outputs_screen()->event(&e);
}

static void out_tap(int x, int y)
{
    out_ev(x, y, TOUCH_EVENT_DOWN);
    out_ev(x, y, TOUCH_EVENT_UP);
}

/* OUTPUTS on its PROG PORT entry, as the TARGET row opens it. */
static void out_open_port(void)
{
    outputs_screen()->reset();
    outputs_screen_set_prog_ports(1u, (const uint8_t[]){ 13u, 0xFFu, 0xFFu,
                                                         0xFFu });
    outputs_screen_open_on_port(0u);
    outputs_screen()->enter();
    CHECK_EQ(outputs_screen_port_shown(), 0);
}

/* What the tester drives is set on OUTPUTS' PROG PORT entry, on a bench at
 * rest: SERVO TESTER, the TARGET setting, and PORT PWM, which has no port
 * to switch while the tester drives the outputs.  SETTINGS' DUT page holds
 * no PORT PWM row: its right column's row 1 takes no tap. */
TEST_CASE(outputs_prog_port_entry_sets_what_the_tester_drives_at_rest)
{
    fresh();
    out_open_port();
    open_settings();
    tap(TAB_X(PAGE_DUT), TAB_Y);               /* DUT */
    tap(ROW_R_X, ROW_Y(1));                    /* where PORT PWM was */
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    /* With the outputs as target PORT PWM has no port to switch. */
    CHECK(servo_screen_port_pwm_locked());
    out_tap(OSEG_X(1), OSEG_PWM_Y);            /* OFF */
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));

    CHECK(!servo_screen_tester_target_locked());
    out_tap(OSEG_X(1), OSEG_TESTER_Y);         /* PORT 1 */
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    CHECK(settings_save_asked());
    out_tap(OSEG_X(1), OSEG_PWM_Y);            /* OFF */
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    out_tap(OSEG_X(0), OSEG_PWM_Y);            /* ON */
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));

    /* Armed: the target stays. */
    servo_screen_set_armed(true);
    CHECK(servo_screen_target_locked());
    CHECK(servo_screen_tester_target_locked());
    out_tap(OSEG_X(0), OSEG_TESTER_Y);         /* OUTPUTS */
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    /* The pulse goes off under an armed bench and does not come back. */
    out_tap(OSEG_X(1), OSEG_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    out_tap(OSEG_X(0), OSEG_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    /* Disarmed it does, and the target goes back. */
    servo_screen_set_armed(false);
    CHECK(!servo_screen_tester_target_locked());
    out_tap(OSEG_X(0), OSEG_PWM_Y);
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));

    /* The bench ARMED for the motor, which this screen does not arm: the
     * target stays, the pulse goes off and stays off. */
    servo_screen_set_bench_armed(true);
    CHECK(!servo_screen_target_locked());
    CHECK(servo_screen_tester_target_locked());
    out_tap(OSEG_X(0), OSEG_TESTER_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    out_tap(OSEG_X(1), OSEG_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    out_tap(OSEG_X(0), OSEG_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    servo_screen_set_bench_armed(false);
    out_tap(OSEG_X(0), OSEG_PWM_Y);
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    out_tap(OSEG_X(0), OSEG_TESTER_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    /* OUTPUTS again: PORT PWM is locked and keeps its value. */
    CHECK(servo_screen_port_pwm_locked());
    out_tap(OSEG_X(1), OSEG_PWM_Y);
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
}

/*
 * The TARGET row of the right card, as the pixels of its box: the row at
 * y 326 to 348, between the SPEED slider, whose thumb and its shadow paint
 * down to y 324, and CENTRE (from y 350); the label at x 514 and the value
 * box from x 578.
 */
#define TGT_X 514
#define TGT_Y 326
#define TGT_W 268
#define TGT_H 22
#define TGT_ROW_Y 322
#define TGT_VAL_DX UI_TARGET_LABEL_W

/* The target line's box at the foot of the left card before the TARGET
 * row: left 6 + 200, top (H - UI_BAND_H) - 6 - 36, 278 x 26 px. */
#define OLD_TGT_X 206
#define OLD_TGT_Y ((H - UI_BAND_H) - 6 - 36)

static void target_box(gfx_color_t *out)
{
    for (int y = 0; y < TGT_H; ++y) {
        memcpy(out + y * TGT_W, fb + (TGT_Y + y) * W + TGT_X,
               TGT_W * sizeof(gfx_color_t));
    }
}

/* One state of the target row. */
typedef struct {
    int      target;        /* SET_SERVO_TARGET */
    uint8_t  drive;         /* the drive's target: 0, 1 + n for port n,
                               SERVO_SCREEN_TARGET_UNKNOWN or _NO_LINK */
    uint64_t pins;          /* GPIOs bound as SERVO PWM */
    bool     known;
    uint8_t  prog_pin;
    bool     pwm;           /* the port renders its pulse */
    bool     warn;          /* the warning colour */
} tgt_state_t;

#define PINS_40_47 (0xFFull << 40)
#define T_UNKNOWN SERVO_SCREEN_TARGET_UNKNOWN
#define T_NO_LINK SERVO_SCREEN_TARGET_NO_LINK

static const tgt_state_t k_tgt[] = {
    { 0, 0, PIN(2),                              true,  0xFFu, false, false },
    { 0, 0, PIN(4),                              true,  0xFFu, false, false },
    { 0, 0, PIN(2) | PIN(4),                     true,  0xFFu, false, false },
    { 0, 0, PIN(2) | PIN(4) | PIN(5),            true,  0xFFu, false, false },
    { 0, 0, PIN(12) | PIN(13) | PIN(14) | PIN(15), true, 0xFFu, false, false },
    { 0, 0, PIN(2) | PIN(4) | PIN(5) | PIN(6) | PIN(7) | PIN(8), true,
      0xFFu, false, false },
    { 0, 0, PINS_40_47,                          true,  0xFFu, false, false },
    { 0, 0, 0,                                   true,  0xFFu, false, true  },
    { 0, 0, 0,                                   false, 0xFFu, false, true  },
    { 1, 1, PIN(2),                              true,  0xFFu, true,  true  },
    { 1, 1, PIN(2),                              true,  13u,   true,  true  },
    /* The port's pulse off: the longest values, with a two-digit pin. */
    { 1, 1, PIN(2),                              true,  63u,   false, true  },
    { 1, 1, PIN(2),                              true,  0xFFu, false, true  },
    /* Not known and no link, with GP2 bound: the tester sends nothing. */
    { 1, T_UNKNOWN, PIN(2),                      true,  0xFFu, false, true  },
    { 1, T_NO_LINK, PIN(2),                      true,  0xFFu, false, true  },
};
#define TGT_N ((int)(sizeof(k_tgt) / sizeof(k_tgt[0])))
#define TGT_NONE   7        /* k_tgt: no SERVO PWM pin */
#define TGT_PORT   9        /* k_tgt: the port, its pin not known */
#define TGT_PORT_PIN 10     /* k_tgt: the port on GP13, its pulse on */
#define TGT_PORT_OFF 11     /* k_tgt: the port on GP63, its pulse off */
#define TGT_UNKNOWN 13      /* k_tgt: the target not known */
#define TGT_NO_LINK 14      /* k_tgt: no link */

/* The exact text of each k_tgt state's value, in English and in German,
 * written out here and not taken from servo_target_line(). */
static const char *const k_tgt_txt[2][TGT_N] = {
    { "SERVO PWM GP2", "SERVO PWM GP4", "SERVO PWM GP2 GP4",
      "SERVO PWM GP2 GP4 GP5", "SERVO PWM GP12 GP13 +2",
      "SERVO PWM GP2 GP4 +4", "SERVO PWM GP40 GP41 +6",
      "NO SERVO PWM PIN",
      "PINS NOT READ", "PROG PORT 1", "PROG PORT 1 GP13",
      "PORT 1 GP63 PWM OFF", "PROG PORT 1 PWM OFF", "UNKNOWN", "NO LINK" },
    { "SERVO PWM GP2", "SERVO PWM GP4", "SERVO PWM GP2 GP4",
      "SERVO PWM GP2 GP4 GP5", "SERVO PWM GP12 GP13 +2",
      "SERVO PWM GP2 GP4 +4", "SERVO PWM GP40 GP41 +6",
      "KEIN SERVO-PWM-PIN",
      "PINS NICHT AUSGELESEN", "PROG-PORT 1", "PROG-PORT 1 GP13",
      "PORT 1 GP63 PWM AUS", "PROG-PORT 1 PWM AUS", "UNBEKANNT",
      "KEINE VERBINDUNG" },
};

/* The row a label and a value give, drawn over the screen's by the row
 * widget (ui_target_row.h, whose own pixels test_widgets holds) at the
 * row's place: the label at x 514, the value in the box from x 578, both in
 * the warning colour for @p warn, the box drawn locked for @p locked. */
static void target_reference_as(gfx_color_t *out, const char *label,
                                const char *text, bool warn, bool locked)
{
    gfx_fill_rect(&cv, TGT_X, TGT_Y, TGT_W, TGT_H,
                  ui_theme_color(UI_C_PANEL));
    ui_target_row_t row;
    ui_target_row_init(&row, (gfx_rect_t){ TGT_X, TGT_Y, TGT_W, TGT_H });
    ui_target_row_render(&row, &cv, label, text, warn, locked);
    target_box(out);
}

static void target_reference(gfx_color_t *out, const char *label,
                             const char *text, bool warn)
{
    target_reference_as(out, label, text, warn, false);
}

static int warn_pixels(const gfx_color_t *bx)
{
    int n = 0;
    for (int i = 0; i < TGT_W * TGT_H; ++i) {
        n += (bx[i] == ui_theme_color(UI_C_WARN)) ? 1 : 0;
    }
    return n;
}

/* The active PWM target is on the SERVO screen's right card in both
 * languages: the pins bound as SERVO PWM for OUTPUTS, the programming port
 * with its pin, and the warning colour for the port, for no pin, for a
 * binding not read, for a target not known and for no link.  Every state
 * reads differently, and no value runs out of its box: the longest are the
 * protocol with two two-digit pins and a count, 22 of the 23 cells, and
 * the port on a two-digit pin with its pulse off, 22 as well. */
TEST_CASE(the_servo_screen_names_the_pins_the_tester_drives)
{
    gfx_color_t *box[2 * TGT_N];
    for (int i = 0; i < 2 * TGT_N; ++i) {
        box[i] = malloc(TGT_W * TGT_H * sizeof(gfx_color_t));
        REQUIRE_ALLOC(box[i]);
    }
    for (int lang = 0; lang < 2; ++lang) {
        for (int i = 0; i < TGT_N; ++i) {
            const tgt_state_t *st = &k_tgt[i];
            fresh();
            ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
            settings_set(SET_SERVO_TARGET, (float)st->target);
            servo_screen_set_pins(st->pins, st->known);
            servo_screen_set_drive(st->drive, st->prog_pin, st->pwm);
            scr->render(&cv, 0);
            gfx_color_t *bx = box[lang * TGT_N + i];
            target_box(bx);
            CHECK_EQ(warn_pixels(bx) > 0, st->warn);
            /* The pixels of the label and the value the state must give. */
            static gfx_color_t want_box[TGT_W * TGT_H];
            target_reference(want_box, lang == 0 ? "TARGET" : "ZIEL",
                             k_tgt_txt[lang][i], st->warn);
            CHECK_EQ(memcmp(want_box, bx, sizeof(want_box)), 0);
            /* The value fits the 23 cells of its box. */
            CHECK(gfx_text_width(UI_FONT_LABEL, k_tgt_txt[lang][i], 1)
                  <= 23 * 8);
            /* The foot of the left card carries no second target line. */
            CHECK_EQ(fb[(OLD_TGT_Y + 2) * W + OLD_TGT_X + 4],
                     ui_theme_color(UI_C_PANEL));
        }
    }
    ui_text_set_language(UI_LANG_EN);
    for (int i = 0; i < 2 * TGT_N; ++i) {
        for (int j = i + 1; j < 2 * TGT_N; ++j) {
            /* The pin lists read the same in both languages; only the
             * label tells them apart. */
            CHECK(memcmp(box[i], box[j], TGT_W * TGT_H
                                         * sizeof(gfx_color_t)) != 0);
        }
    }

    /* Rebinding GP2 to GP4 changes the row, without a tap; and back. */
    fresh();
    servo_screen_set_pins(PIN(2), true);
    scr->render(&cv, 0);
    static gfx_color_t a[TGT_W * TGT_H];
    target_box(a);
    servo_screen_set_pins(PIN(4), true);
    scr->render(&cv, 0);
    target_box(box[0]);
    CHECK(memcmp(a, box[0], TGT_W * TGT_H * sizeof(gfx_color_t)) != 0);
    servo_screen_set_pins(PIN(2), true);
    scr->render(&cv, 0);
    target_box(box[0]);
    CHECK_EQ(memcmp(a, box[0], TGT_W * TGT_H * sizeof(gfx_color_t)), 0);

    /* The row follows the drive, not the setting.  PROG PORT chosen while
     * the drive still uses the outputs shows the pins the outputs carry, as
     * GP2 does; with no such pin it is the warning, as no pin does. */
    static gfx_color_t d[TGT_W * TGT_H];
    fresh();
    settings_set(SET_SERVO_TARGET, 1.0f);
    servo_screen_set_pins(PIN(2), true);
    servo_screen_set_drive(0u, 13u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[0], TGT_W * TGT_H * sizeof(gfx_color_t)), 0);
    servo_screen_set_pins(0u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_NONE], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    /* The port driven while the setting still reads OUTPUTS. */
    fresh();
    settings_set(SET_SERVO_TARGET, 0.0f);
    servo_screen_set_pins(PIN(2), true);
    servo_screen_set_drive(1u, 13u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_PORT_PIN], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    /* Only the port's pulse turned off: the row follows, without a tap. */
    servo_screen_set_drive(1u, 63u, true);
    scr->render(&cv, 0);
    servo_screen_set_drive(1u, 63u, false);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_PORT_OFF], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    /* The port driven with no pin known (none, refused, or a port the page
     * does not have): still the port, not the GP2 the outputs carry. */
    servo_screen_set_drive(1u, SERVO_SCREEN_NO_PIN, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_PORT], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    CHECK(memcmp(d, box[0], TGT_W * TGT_H * sizeof(gfx_color_t)) != 0);
    /* Only the pin changed, port and pulse kept: the row follows. */
    servo_screen_set_drive(1u, 13u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_PORT_PIN], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    servo_screen_set_drive(1u, 14u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK(memcmp(d, box[TGT_PORT_PIN], TGT_W * TGT_H * sizeof(gfx_color_t))
          != 0);
    /* The link lost with the binding still held: no link, not the GP2 the
     * outputs carry; then the page not known after the link comes back;
     * then the outputs once it is read. */
    servo_screen_set_drive(SERVO_SCREEN_TARGET_NO_LINK, 13u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_NO_LINK], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    servo_screen_set_drive(SERVO_SCREEN_TARGET_UNKNOWN, 13u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_UNKNOWN], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[0], TGT_W * TGT_H * sizeof(gfx_color_t)), 0);
    for (int i = 0; i < 2 * TGT_N; ++i) {
        free(box[i]);
    }
}

/*
 * The row is on the right card, which the settings, the keypads they open
 * and the servo programmer leave uncovered: while they cover the left card
 * the row still names the pin, and a change of the drive under them still
 * reaches it.
 */
TEST_CASE(the_target_row_stays_in_sight_under_the_settings_and_a_keypad)
{
    static gfx_color_t want[TGT_W * TGT_H], got[TGT_W * TGT_H];
    fresh();
    servo_screen_set_pins(PIN(4), true);
    scr->render(&cv, 0);
    target_box(want);
    target_reference(got, "TARGET", "SERVO PWM GP4", false);
    CHECK_EQ(memcmp(want, got, sizeof(want)), 0);

    open_settings();
    scr->render(&cv, 0);
    target_box(got);
    CHECK_EQ(memcmp(want, got, sizeof(want)), 0);
    /* The drive moves to the port while the settings are open. */
    servo_screen_set_drive(1u, 13u, true);
    scr->render(&cv, 0);
    target_box(got);
    static gfx_color_t port[TGT_W * TGT_H];
    target_reference(port, "TARGET", "PROG PORT 1 GP13", true);
    scr->render(&cv, 0);
    target_box(got);
    CHECK_EQ(memcmp(port, got, sizeof(port)), 0);
    servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);

    /* PULSE MIN's keypad over the left card. */
    tap(ROW_L_X, ROW_Y(2));
    scr->render(&cv, 0);
    target_box(got);
    CHECK_EQ(memcmp(want, got, sizeof(want)), 0);
    key(UI_KEY_CANCEL);
    close_settings();
}

/* One value of the target row and its colour for a drive and a set of
 * pins. */
static void check_line(uint8_t target, uint8_t gpio, bool pwm, uint64_t pins,
                       bool known, const char *want, bool warn)
{
    char line[40];
    memset(line, 'x', sizeof(line));
    CHECK_EQ(servo_target_line(line, sizeof(line), target, gpio, pwm, pins,
                               known),
             warn);
    CHECK_STR_EQ(line, want);
}

/* The target row's value: as many pins as the 23 cells hold, then the
 * count of the rest, the port with its pin or without and with its pulse on
 * or off, the target not known and no link, in both languages. */
TEST_CASE(the_target_line_lists_the_pins_that_fit_and_counts_the_rest)
{
    ui_text_set_language(UI_LANG_EN);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PIN(2), true, "SERVO PWM GP2",
               false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PIN(2) | PIN(4) | PIN(5), true,
               "SERVO PWM GP2 GP4 GP5", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false,
               PIN(12) | PIN(13) | PIN(14) | PIN(15), true,
               "SERVO PWM GP12 GP13 +2", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false,
               PIN(2) | PIN(4) | PIN(5) | PIN(6) | PIN(7), true,
               "SERVO PWM GP2 GP4 +3", false);
    /* The list ends exactly at the 23 cells, the protocol's 10 and 13 of
     * pins: no count. */
    check_line(0u, SERVO_SCREEN_NO_PIN, false,
               PIN(2) | PIN(10) | PIN(11), true,
               "SERVO PWM GP2 GP10 GP11", false);
    /* One pin more overflows it: 2 pins and " +2". */
    check_line(0u, SERVO_SCREEN_NO_PIN, false,
               PIN(2) | PIN(10) | PIN(11) | PIN(12), true,
               "SERVO PWM GP2 GP10 +2", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false,
               PIN(10) | PIN(11) | PIN(12) | PIN(13) | PIN(14), true,
               "SERVO PWM GP10 GP11 +3", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PINS_40_47, true,
               "SERVO PWM GP40 GP41 +6", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, ~0ull, true,
               "SERVO PWM GP0 GP1 +62", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PIN(63), true,
               "SERVO PWM GP63", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, 0u, true,
               "NO SERVO PWM PIN", true);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PIN(2), false,
               "PINS NOT READ", true);
    /* With the outputs as target a port pin names nothing. */
    check_line(0u, 13u, false, PIN(2), true, "SERVO PWM GP2", false);
    /* A port target is the port, its pin known or not, pins bound or not. */
    check_line(1u, 13u, true, PIN(2), true, "PROG PORT 1 GP13", true);
    check_line(1u, SERVO_SCREEN_NO_PIN, true, PIN(2), true, "PROG PORT 1",
               true);
    check_line(2u, SERVO_SCREEN_NO_PIN, true, 0u, false, "PROG PORT 2", true);
    check_line(4u, 47u, true, 0u, false, "PROG PORT 4 GP47", true);
    /* The pulse off: the port is still the target, and renders nothing.
     * Port 4 on GP63 is the longest, 19 of the 23 cells the value
     * holds. */
    check_line(1u, 13u, false, PIN(2), true, "PORT 1 GP13 PWM OFF", true);
    check_line(1u, 63u, false, PIN(2), true, "PORT 1 GP63 PWM OFF", true);
    check_line(4u, 63u, false, PIN(2), true, "PORT 4 GP63 PWM OFF", true);
    check_line(1u, SERVO_SCREEN_NO_PIN, false, PIN(2), true,
               "PROG PORT 1 PWM OFF", true);
    /* Not known and no link name no pin and no port, pins bound or not;
     * a pin and a pulse passed with them are ignored. */
    check_line(SERVO_SCREEN_TARGET_UNKNOWN, 13u, true, PIN(2), true,
               "UNKNOWN", true);
    check_line(SERVO_SCREEN_TARGET_UNKNOWN, SERVO_SCREEN_NO_PIN, false, 0u,
               false, "UNKNOWN", true);
    check_line(SERVO_SCREEN_TARGET_NO_LINK, 13u, true, PIN(2), true,
               "NO LINK", true);
    check_line(SERVO_SCREEN_TARGET_NO_LINK, SERVO_SCREEN_NO_PIN, false, 0u,
               false, "NO LINK", true);
    ui_text_set_language(UI_LANG_DE);
    /* The protocol is a name: the same in German. */
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PIN(2) | PIN(4) | PIN(5), true,
               "SERVO PWM GP2 GP4 GP5", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PINS_40_47, true,
               "SERVO PWM GP40 GP41 +6", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, 0u, true,
               "KEIN SERVO-PWM-PIN", true);
    check_line(1u, 13u, true, PIN(2), true, "PROG-PORT 1 GP13", true);
    check_line(1u, SERVO_SCREEN_NO_PIN, true, PIN(2), true, "PROG-PORT 1",
               true);
    check_line(1u, 63u, false, PIN(2), true, "PORT 1 GP63 PWM AUS", true);
    check_line(3u, SERVO_SCREEN_NO_PIN, false, PIN(2), true,
               "PROG-PORT 3 PWM AUS", true);
    check_line(SERVO_SCREEN_TARGET_UNKNOWN, SERVO_SCREEN_NO_PIN, false,
               PIN(2), true, "UNBEKANNT", true);
    check_line(SERVO_SCREEN_TARGET_NO_LINK, SERVO_SCREEN_NO_PIN, false,
               PIN(2), true, "KEINE VERBINDUNG", true);
    ui_text_set_language(UI_LANG_EN);
    /* A short buffer is cut, not overrun; none is no line. */
    char small[8];
    memset(small, 'x', sizeof(small));
    CHECK(servo_target_line(small, sizeof(small), 1u, 13u, true, 0u, true));
    CHECK_STR_EQ(small, "PROG PO");
    memset(small, 'x', sizeof(small));
    CHECK(!servo_target_line(small, sizeof(small), 0u, SERVO_SCREEN_NO_PIN,
                             false, PINS_40_47, true));
    CHECK_STR_EQ(small, "SERVO P");
    CHECK(!servo_target_line(NULL, 8u, 1u, 13u, true, 0u, true));
    CHECK(!servo_target_line(small, 0u, 1u, 13u, true, 0u, true));
    CHECK_STR_EQ(small, "SERVO P");
}

/* A test or a sweep under way keeps the port's pulse: switched off, the run
 * would go on stepping and recording a servo that gets no pulse. */
TEST_CASE(the_port_pulse_is_not_switched_off_under_a_test_or_a_sweep)
{
    /* A sweep. */
    fresh();
    out_open_port();
    settings_set(SET_SERVO_TARGET, 1.0f);
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    CHECK(servo_screen_sweeping());
    CHECK(servo_screen_port_pwm_locked());
    out_tap(OSEG_X(1), OSEG_PWM_Y);            /* PORT PWM OFF */
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    CHECK(servo_screen_sweeping());

    /* A test. */
    bench_fresh();
    out_open_port();
    settings_set(SET_SERVO_TARGET, 1.0f);
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    out_tap(OSEG_X(1), OSEG_PWM_Y);
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    CHECK(servo_screen_testing());
    /* The run over, armed still: the pulse goes off. */
    open_test();
    tap(START_X, START_Y);                     /* STOP */
    CHECK(!servo_screen_testing());
    out_tap(OSEG_X(1), OSEG_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    settings_set(SET_SERVO_TARGET, 0.0f);
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
}

static bool s_port_held;

static bool port_hold(setting_id_t id)
{
    return s_port_held
        && (id == SET_SERVO_TARGET || id == SET_SERVO_PORT_PWM);
}

/* While the programmer has the port, neither OUTPUTS' SERVO TESTER nor its
 * PORT PWM changes what the tester drives or the port's pulse -- the pulse
 * is not switched off either -- and a press begun before the hold takes no
 * choice on its release.  The TARGET row opens nothing. */
TEST_CASE(the_tester_target_and_pulse_stay_while_the_port_is_held)
{
    for (int pwm = 0; pwm < 2; ++pwm) {
        for (int target = 0; target < 2; ++target) {
            fresh();
            out_open_port();
            settings_set_hold(port_hold);
            s_port_held = false;
            settings_set(SET_SERVO_TARGET, (float)target);
            settings_set(SET_SERVO_PORT_PWM, (float)pwm);
            s_port_held = true;
            CHECK(servo_screen_target_locked());
            CHECK(servo_screen_tester_target_locked());
            CHECK(servo_screen_port_pwm_locked());
            char why[96];
            CHECK(outputs_screen_port_reason(why, sizeof(why)));
            CHECK_STR_EQ(why, TR(OUT_PORT_HELD));
            out_tap(OSEG_X(1 - target), OSEG_TESTER_Y);
            out_tap(OSEG_X(pwm), OSEG_PWM_Y);  /* the other value */
            CHECK_EQ(settings_get_int(SET_SERVO_TARGET), target);
            CHECK_EQ((int)settings_get_bool(SET_SERVO_PORT_PWM), pwm);
            /* A press begun before the port was taken. */
            s_port_held = false;
            out_ev(OSEG_X(1 - target), OSEG_TESTER_Y, TOUCH_EVENT_DOWN);
            s_port_held = true;
            out_ev(OSEG_X(1 - target), OSEG_TESTER_Y, TOUCH_EVENT_UP);
            CHECK_EQ(settings_get_int(SET_SERVO_TARGET), target);
            /* The hold over, both take their taps. */
            s_port_held = false;
            CHECK(!servo_screen_target_locked());
            /* The hold's line goes; another may stand in its place. */
            CHECK(!outputs_screen_port_reason(why, sizeof(why))
                  || strcmp(why, TR(OUT_PORT_HELD)) != 0);
            out_tap(OSEG_X(1), OSEG_TESTER_Y);
            CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
            out_tap(OSEG_X(pwm), OSEG_PWM_Y);
            CHECK_EQ((int)settings_get_bool(SET_SERVO_PORT_PWM), 1 - pwm);
            settings_set_hold(NULL);
        }
    }
}

/* ENC CENTRE on SETTINGS' DUT page, row 2: the AS5600 is switched on
 * SETUP > INTERFACES, its one home, and the DUT page holds no AS5600 row. */
TEST_CASE(the_dut_page_sets_the_encoders_centre_from_the_live_count)
{
    fresh();
    open_settings();
    tap(TAB_X(PAGE_DUT), TAB_Y);               /* DUT */
    /* Where AS5600 was, nothing: the encoder stays off. */
    tap(ROW_L_X, ROW_Y(3));
    CHECK(!settings_get_bool(SET_ENC_EN));
    settings_set(SET_ENC_EN, 1.0f);            /* SETUP > INTERFACES */

    /* No reading yet: the tap sets nothing. */
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 0);
    tap(ROW_L_X, ROW_Y(2));                    /* ENC CENTRE */
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 0);

    /* A live count is the centre. */
    const servo_test_enc_t e = { true, 2871u, 0u, 5000u, false, false, false, false };
    servo_screen_encoder(&e);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 2871);

    /* A sensor that answers and sees no magnet has no live count. */
    const servo_test_enc_t dark = { .valid = false, .raw = 777u,
                                    .taken_ms = 5020u, .no_magnet = true };
    servo_screen_encoder(&dark);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 2871);

    /* A reading that has gone sets nothing; with the encoder off in SETUP
     * the row is faint and sets nothing either. */
    const servo_test_enc_t e2 = { true, 100u, 0u, 5040u, false, false, false, false };
    servo_screen_encoder(&e2);
    settings_set(SET_ENC_EN, 0.0f);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 2871);
}

TEST_CASE(a_link_that_goes_down_takes_the_encoders_angle_with_it)
{
    fresh();
    open_settings();
    tap(TAB_X(PAGE_DUT), TAB_Y);               /* DUT */
    settings_set(SET_ENC_EN, 1.0f);            /* SETUP > INTERFACES */
    settings_set(SET_ENC_CENTRE, 1000.0f);
    const servo_test_enc_t e = { true, 2871u, 0u, 5000u, false, false, false, false };
    servo_screen_encoder(&e);

    /* The link drops: ENC CENTRE takes nothing from the last count. */
    servo_screen_set_link(false);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 1000);

    /* A reading queued before the drop and drained after it is not used
     * either. */
    servo_screen_encoder(&e);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 1000);

    /* The link back and no reading yet: still nothing; a reading that
     * arrives with the link is the live count. */
    servo_screen_set_link(true);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 1000);
    const servo_test_enc_t f = { true, 1500u, 0u, 6000u, false, false, false, false };
    servo_screen_encoder(&f);
    tap(ROW_L_X, ROW_Y(2));
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 1500);
}

TEST_CASE(a_run_with_the_encoder_on_writes_the_angle_columns)
{
    bench_fresh();
    short_runs();
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_screen_set_enc_held(true);
    bench.enc = true;
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 240 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK(strstr(bench.csv_head, ";travel (ms);angle (deg);travel angle (ms)") != NULL);
    CHECK(strstr(bench.report, "ENCODER (angles in degrees from the centre count") != NULL);
    CHECK(strstr(bench.report, "centre count 3000,") != NULL);
    CHECK(strstr(bench.report, "Commanded: ") != NULL);
    CHECK(bench.ends >= 1u);
}

/* "No magnet" reaches the screen once, when it begins.  A run started
 * after that is told the state it starts in, and its report gives the
 * reason for its missing angles. */
TEST_CASE(a_run_started_without_a_magnet_says_so_in_its_report)
{
    bench_fresh();
    short_runs();
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_screen_set_enc_held(true);
    const servo_test_enc_t dark = { .valid = false, .no_magnet = true };
    servo_screen_encoder(&dark);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 240 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK(strstr(bench.report, "No angle reading reached the run.") != NULL);
    CHECK(strstr(bench.report, "No magnet: the AS5600 reported none 1 time(s).")
          != NULL);

    /* A sensor that stopped answering before the run is not "no magnet". */
    bench_fresh();
    short_runs();
    settings_set(SET_ENC_EN, 1.0f);
    servo_screen_set_enc_held(true);
    servo_screen_encoder(&dark);
    const servo_test_enc_t gone = { .valid = false };
    servo_screen_encoder(&gone);
    hold_start(2.3f);
    for (int i = 0; i < 240 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    bench_frames(1000u);
    CHECK(strstr(bench.report, "No angle reading reached the run.") != NULL);
    CHECK(strstr(bench.report, "No magnet:") == NULL);
}

TEST_CASE(a_run_with_the_encoder_off_writes_the_old_columns)
{
    bench_fresh();
    short_runs();
    bench.enc = true;                               /* readings, AS5600 off */
    hold_start(2.3f);
    for (int i = 0; i < 240 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    bench_frames(1000u);
    CHECK(strstr(bench.csv_head, "angle") == NULL);
    CHECK(strstr(bench.report, "ENCODER") == NULL);
}

/* The SENSE set-up is written only while the bank is disarmed: an AS5600
 * switched on while armed is not in the IOmcu, and a run started then
 * has no angles to log. */
TEST_CASE(an_encoder_the_coprocessor_does_not_hold_gives_the_run_no_angle_columns)
{
    bench_fresh();
    short_runs();
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_screen_set_enc_held(false);
    bench.enc = true;
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 240 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    bench_frames(1000u);
    CHECK(strstr(bench.csv_head, "angle") == NULL);
    CHECK(strstr(bench.report, "ENCODER") == NULL);
}

/* ------------------------------------------- a drag that lost its release */

/*
 * The cases below feed the screen frames of contacts through the tracker
 * (touch_feed.h), with no router in front of it: what the screen does by
 * itself about a drag whose release it never sees.  The feed works in panel
 * coordinates and removes the band's height, as the router does.
 */
#define P(y) ((y) + UI_BAND_H)
/* Beside the dial, 190 px right of the shaft: the arc ends at 174 px. */
#define BESIDE_X (SHAFT_X + 190)
#define ONTO_X   (SHAFT_X + 150)

static void fed_armed(void)
{
    fresh();
    feed_reset();
    feed_to_screen(scr);
    servo_screen_set_armed(true);
    while (last_cmd().kind != SERVO_CMD_NONE) { }
}

/* Whether a contact that presses beside the dial and slides onto it
 * commands a position.  With no drag latched it does not. */
static bool a_slide_onto_the_dial_commands(void)
{
    finger(FEED_LONE, BESIDE_X, P(SHAFT_Y));
    const servo_cmd_t d = last_cmd();
    glide(FEED_LONE, ONTO_X, P(SHAFT_Y), 8);
    const servo_cmd_t c = last_cmd();
    lift(FEED_LONE);
    return d.kind != SERVO_CMD_NONE || c.kind != SERVO_CMD_NONE;
}

static bool the_knob_moves_the_horn(void)
{
    servo_screen_knob_frame();
    servo_screen_knob(0.1f);
    return last_cmd().kind == SERVO_CMD_POSITION;
}

/*
 * The screen is left under a finger on the dial: a second finger on the
 * home tag, which the band handles without asking this screen.  The
 * release goes to the screen then on top.  Back on SERVO and armed, the
 * knob has the horn and a contact with the same id commands nothing by
 * sliding onto the dial.
 */
TEST_CASE(leaving_under_a_dial_drag_ends_the_drag)
{
    fed_armed();
    int x, y;
    dial_at(40.0f, 110, &x, &y);
    finger(FEED_LONE, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);

    scr->leave();
    feed_lose_next(1);                  /* the release is another screen's */
    lift(FEED_LONE);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);
    servo_screen_set_armed(false);
    servo_screen_set_armed(true);
    while (last_cmd().kind != SERVO_CMD_NONE) { }

    CHECK(the_knob_moves_the_horn());
    CHECK(!a_slide_onto_the_dial_commands());
}

/*
 * A release that never arrives, and no loss told.  The next press ends the
 * drag wherever it lands, so the contact that made it commands nothing by
 * sliding onto the dial, and the knob has the horn after it.
 */
TEST_CASE(a_press_ends_a_dial_drag_that_lost_its_release)
{
    fed_armed();
    int x, y;
    dial_at(40.0f, 110, &x, &y);
    finger(FEED_LONE, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    feed_lose_next(1);
    lift(FEED_LONE);
    const uint16_t held = servo_screen_commanded();

    CHECK(!a_slide_onto_the_dial_commands());
    CHECK_EQ(servo_screen_commanded(), held);
    CHECK(the_knob_moves_the_horn());
}

/*
 * Any press, a second finger's included, as on the sliders: the finger on
 * the dial commands nothing more until it presses again.
 */
TEST_CASE(a_second_press_ends_a_dial_drag)
{
    fed_armed();
    int x, y, x2, y2;
    dial_at(40.0f, 110, &x, &y);
    dial_at(60.0f, 110, &x2, &y2);
    finger(0, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    const uint16_t held = servo_screen_commanded();

    finger(1, BESIDE_X, P(SHAFT_Y));
    glide(0, x2, P(y2), 8);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), held);
    lift(1);
    lift(0);

    /* Pressed again, the dial has the horn. */
    finger(0, x2, P(y2));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    CHECK(servo_screen_commanded() != held);
    lift(0);
}

/*
 * The second press ends the drag, and the finger that was dragging still
 * owned the dial in that frame: a knob turn applied at the end of the frame
 * moves nothing.  The next frame is the knob's.
 */
TEST_CASE(the_knob_stays_out_of_the_frame_a_second_press_ended_a_drag_in)
{
    fed_armed();
    int x, y;
    dial_at(40.0f, 110, &x, &y);
    finger(0, x, P(y));                 /* frame N */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    const uint16_t held = servo_screen_commanded();

    servo_screen_knob_frame();          /* frame N+1 */
    finger(1, BESIDE_X, P(SHAFT_Y));
    servo_screen_knob(0.1f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), held);

    servo_screen_knob_frame();          /* frame N+2 */
    servo_screen_knob(0.1f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    lift(1);
    lift(0);
}

/*
 * A press on the dial and a turn of the knob on a disarmed bench command
 * nothing.  The arm holds nothing, and a withdrawal after it restores no
 * drive from before it: a change of SPEED on the armed bench says no
 * position.
 */
TEST_CASE(a_withdrawal_after_an_arm_restores_no_drive_from_before_it)
{
    fresh();
    feed_reset();
    feed_to_screen(scr);
    int x, y;
    dial_at(40.0f, 110, &x, &y);
    feed_tap(FEED_LONE, x, P(y));       /* disarmed: refused */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    servo_screen_knob_frame();
    servo_screen_knob(0.1f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    servo_screen_set_armed(true);
    servo_screen_knob_cancel();
    scr->cancel();
    while (last_cmd().kind != SERVO_CMD_NONE) { }

    finger(FEED_LONE, 560, P(306));
    glide(FEED_LONE, 620, P(306), 8);
    lift(FEED_LONE);
    CHECK(last_cmd().kind != SERVO_CMD_POSITION);
}

/*
 * SPEED's drag loses its release, and the next press is on the dial, which
 * takes the press before the slider is asked.  The rate stays the one the
 * drag set through that gesture and the one after it.
 */
TEST_CASE(a_press_on_the_dial_ends_a_speed_drag_that_lost_its_release)
{
    fed_armed();
    int x, y, x2, y2;
    dial_at(40.0f, 110, &x, &y);
    dial_at(60.0f, 110, &x2, &y2);
    feed_tap(FEED_LONE, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);

    finger(FEED_LONE, 560, P(306));
    glide(FEED_LONE, 620, P(306), 8);
    const servo_cmd_t set = last_cmd();
    CHECK_EQ(set.kind, SERVO_CMD_POSITION);
    feed_lose_next(1);
    lift(FEED_LONE);

    finger(FEED_LONE, x, P(y));
    CHECK_EQ(last_cmd().slew_per_s, set.slew_per_s);
    glide(FEED_LONE, x2, P(y2), 8);
    CHECK_EQ(last_cmd().slew_per_s, set.slew_per_s);
    lift(FEED_LONE);

    finger(FEED_LONE, BESIDE_X, P(SHAFT_Y));
    glide(FEED_LONE, BESIDE_X + 60, P(SHAFT_Y), 8);
    lift(FEED_LONE);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    feed_tap(FEED_LONE, x, P(y));
    CHECK_EQ(last_cmd().slew_per_s, set.slew_per_s);
}

/*
 * The knob's command is withdrawn after a stop and a disarm, in the panel's
 * order: the knob turned at the end of one frame; the next frame takes the
 * stop and the disarm and then finds touch events lost.  The withdrawal
 * restores no drive on a disarmed bench, so a change of SPEED says no
 * position.
 */
TEST_CASE(a_withdrawal_on_a_disarmed_bench_restores_no_drive)
{
    fed_armed();
    int x, y;
    dial_at(40.0f, 110, &x, &y);
    feed_tap(FEED_LONE, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);

    servo_screen_knob_frame();
    servo_screen_knob(0.1f);

    servo_screen_knob_frame();
    servo_screen_cancel_arm();
    servo_screen_set_armed(false);
    servo_screen_knob_cancel();
    scr->cancel();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    finger(FEED_LONE, 560, P(306));
    glide(FEED_LONE, 620, P(306), 8);
    lift(FEED_LONE);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

/*
 * The stop is seen a frame before the bench reports disarmed.  The
 * withdrawal in that frame finds the screen still reading the bench as
 * armed, and restores no drive all the same: the stop held nothing.
 */
TEST_CASE(a_withdrawal_after_a_stop_restores_no_drive_before_the_disarm)
{
    fed_armed();
    int x, y;
    dial_at(40.0f, 110, &x, &y);
    feed_tap(FEED_LONE, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);

    servo_screen_knob_frame();
    servo_screen_knob(0.1f);

    servo_screen_knob_frame();
    servo_screen_cancel_arm();          /* and no disarm reported yet */
    servo_screen_knob_cancel();
    scr->cancel();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    finger(FEED_LONE, 560, P(306));
    glide(FEED_LONE, 620, P(306), 8);
    lift(FEED_LONE);
    CHECK(last_cmd().kind != SERVO_CMD_POSITION);
}

/* On an armed bench the withdrawal restores the drive the knob found, so a
 * change of SPEED says the held position again. */
TEST_CASE(a_withdrawal_on_an_armed_bench_restores_the_drive)
{
    fed_armed();
    int x, y;
    dial_at(40.0f, 110, &x, &y);
    feed_tap(FEED_LONE, x, P(y));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
    const uint16_t held = servo_screen_commanded();

    servo_screen_knob_frame();
    servo_screen_knob(0.1f);
    servo_screen_knob_cancel();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), held);

    finger(FEED_LONE, 560, P(306));
    glide(FEED_LONE, 620, P(306), 8);
    lift(FEED_LONE);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, held);
}

/* ------------------------------------------- the servo rail's meter */

/*
 * Mirrored from servo_screen.c and servo_screen_int.h: the right card's
 * column, the CURRENT row, the line over the power plot with its 64 px name
 * (PD MINI) or 88 px name (INA3221 CH1), and the line and plot together
 * (power_rect()).
 */
#define RAIL_X     514
#define RAIL_W     268
#define CUR_ROW_Y  92
#define CUR_ROW_H  24
#define LINE_Y     150
#define LINE_H     16
#define PWR_BOX_Y  148
#define PWR_BOX_H  70
#define LAB_SUPPLY 64
#define LAB_INA    96

/* Buffer 0 drawn, and whether its CURRENT row reads @p text in @p col. */
static bool row_reads(const char *text, ui_color_id_t col)
{
    scr->render(&cv, 0);
    want_fresh();
    gfx_fill_rect(&want_cv, RAIL_X, CUR_ROW_Y, RAIL_W, CUR_ROW_H,
                  ui_theme_color(UI_C_PANEL));
    gfx_text(&want_cv, RAIL_X, CUR_ROW_Y + 5, "CURRENT", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text_in(&want_cv, (gfx_rect_t){ RAIL_X + 80, CUR_ROW_Y + 5,
                                        RAIL_W - 80, 16 },
                text, UI_FONT_LABEL, ui_theme_color(col), 1, GFX_ALIGN_RIGHT);
    return same_in(fb, want_fb, RAIL_X, CUR_ROW_Y, RAIL_W, CUR_ROW_H);
}

/* Buffer 0 drawn, and whether the line over the plot reads the name
 * @p label, @p lab_w px wide, and the three readings, the current in
 * @p a_col. */
static bool line_reads(const char *label, int lab_w, const char *v,
                       const char *a, ui_color_id_t a_col, const char *w)
{
    scr->render(&cv, 0);
    want_fresh();
    gfx_fill_rect(&want_cv, RAIL_X, LINE_Y, RAIL_W, LINE_H,
                  ui_theme_color(UI_C_PANEL));
    gfx_text(&want_cv, RAIL_X, LINE_Y, label, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    const char *txt[3] = { v, a, w };
    const ui_color_id_t col[3] = { UI_C_VOLT, a_col, UI_C_POWER };
    /* 68 px each beside PD MINI; 52, 64 and 56 px beside a channel. */
    static const int k_fw[2][3] = { { 68, 68, 68 }, { 52, 64, 56 } };
    const int *fw = k_fw[lab_w == LAB_INA ? 1 : 0];
    int x = RAIL_X + lab_w;
    bool fits = true;
    for (int k = 0; k < 3; ++k) {
        /* A reading wider than its field is cut at the left, and a cut
         * minus sign is a wrong reading. */
        fits = fits && (int)strlen(txt[k]) * 8 <= fw[k];
        gfx_text_in(&want_cv, (gfx_rect_t){ (int16_t)x, LINE_Y,
                                            (int16_t)fw[k], 16 },
                    txt[k], UI_FONT_LABEL, ui_theme_color(col[k]), 1,
                    GFX_ALIGN_RIGHT);
        x += fw[k];
    }
    return fits && x == RAIL_X + RAIL_W
           && same_in(fb, want_fb, RAIL_X, LINE_Y, RAIL_W, LINE_H);
}

static bool line_supply(const char *v, const char *a, const char *w)
{
    return line_reads("PD MINI", LAB_SUPPLY, v, a, UI_C_CURR, w);
}

/* The meter's name over the plot: the channel watching the tester's
 * target, "INA 0x40 CH1", or INA3221 with none fitted. */
static const char *ina_label(void)
{
    static char name[16];
    const uint8_t f = settings_sensors_fast();
    if (f >= SET_INA_CHANNELS) {
        return "INA3221";
    }
    snprintf(name, sizeof(name), "INA 0x%02X CH%u", 0x40u + f / 3u,
             f % 3u + 1u);
    return name;
}

static bool line_ina(const char *v, const char *a, const char *w)
{
    return line_reads(ina_label(), LAB_INA, v, a, UI_C_CURR, w);
}


/* A supply sample that answers with both readings. */
static void rail_supply(float v, float i)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.v = v;
    st.i = i;
    st.p = v * i;
    servo_screen_supply(&st);
}

static void rail_window(int ma, unsigned mv)
{
    const sense_link_win_t w = rail_win(ma, mv);
    window(&w);
}

/*
 * The TAG row carries the pulse range after the type and the frame rate:
 * "STANDARD PWM  50 Hz  1000-2000", two spaces between the three parts, the
 * range in microseconds without its unit.  The longest a profile and its
 * keypads allow, a 13-character type, a 3-digit rate and two 4-digit ends,
 * is 32 of the 33 cells the 268 px row holds.  A typed end reaches the row
 * at once.
 */
#define TAG_BOX_Y 118
#define TAG_BOX_H 24

static bool tag_reads(const char *text, bool danger)
{
    scr->render(&cv, 0);
    want_fresh();
    const gfx_rect_t r = { TGT_X, TAG_BOX_Y, TGT_W, TAG_BOX_H };
    gfx_fill_rect(&want_cv, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    gfx_fill_round_rect(&want_cv, r.x, r.y, r.w, r.h, 4,
                        danger ? ui_theme_color(UI_C_DANGER)
                               : ui_theme_color(UI_C_PANEL_SUNK));
    gfx_text_in(&want_cv, r, text, UI_FONT_LABEL,
                danger ? GFX_WHITE : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_CENTER);
    for (int y = r.y; y < r.y + r.h; ++y) {
        if (memcmp(fb + (size_t)y * W + r.x, want_fb + (size_t)y * W + r.x,
                   (size_t)r.w * sizeof(gfx_color_t)) != 0) {
            return false;
        }
    }
    return true;
}

TEST_CASE(the_tag_row_carries_the_pulse_range)
{
    fresh();
    CHECK(tag_reads("STANDARD PWM  50 Hz  1000-2000", false));
    choose_type(1);
    CHECK(tag_reads("NARROW 760  50 Hz  660-860", false));
    choose_type(4);
    CHECK(tag_reads("HELI TAIL 760  560 Hz  410-1110", true));
    /* PULSE MAX typed: the row follows without another tap. */
    choose_type(0);
    open_settings();
    tap(ROW_L_X, ROW_Y(3));                    /* PULSE MAX */
    keys("2100");
    close_settings();
    CHECK(tag_reads("STANDARD PWM  50 Hz  1000-2100", false));
    /* The widest the row can be asked to hold fits with 1 cell spare. */
    CHECK(gfx_text_width(UI_FONT_LABEL, "HELI TAIL 760  560 Hz  2500-2500", 1)
          <= TGT_W - 8);
    /* The separate RANGE row is gone: its label is not on the card. */
    want_fresh();
    gfx_fill_rect(&want_cv, TGT_X, TGT_ROW_Y, 80, 24,
                  ui_theme_color(UI_C_PANEL));
    gfx_text(&want_cv, TGT_X, TGT_ROW_Y + 5, "RANGE", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    scr->render(&cv, 0);
    CHECK(!same_in(fb, want_fb, TGT_X, TGT_ROW_Y, 80, 24));
}

/*
 * The supply the rail's line names is the PD mini, or the panel's model of
 * it: then the SIMULATED (SIMULIERT) pill sits on the plot's top-left corner,
 * under the name, 88 px or as wide as a longer word and 20 px high, as the
 * overview's is.  A change of the model reaches it without a tap; with
 * INA3221 CH1 as the meter it is not shown.
 */
#define PILL_X (RAIL_X + 2)
#define PILL_Y (170 + 2)

static int pill_w(const char *word)
{
    const int w = 24 + gfx_text_width(UI_FONT_LABEL, word, 1);
    return w < 88 ? 88 : w;
}

/* Whether buffer 0, drawn, carries the pill reading @p word: the area around
 * it, one pixel wider on every side, compared with the plot as drawn
 * without the model and the overview's pill drawn on it.  The fill is the
 * plot's own colour, so the pill's width and height show where it covers a
 * grid line or the plot's edge. */
static bool pill_reads(const char *word)
{
    scr->render(&cv, 0);
    const bool model = supply_screen_model();
    static gfx_color_t seen[(size_t)W * H];
    memcpy(seen, fb, sizeof(seen));
    supply_screen_set_model(false);
    scr->tick(1.0f / 39.0f);
    scr->render(&cv, 0);
    want_fresh();
    memcpy(want_fb, fb, (size_t)W * H * sizeof(gfx_color_t));
    memcpy(fb, seen, sizeof(seen));
    supply_screen_set_model(model);
    scr->tick(1.0f / 39.0f);
    const int w = pill_w(word);
    ui_pill(&want_cv, (gfx_rect_t){ PILL_X, PILL_Y, (int16_t)w, 20 }, word,
            ui_theme_color(UI_C_WARN), ui_theme_color(UI_C_PANEL_SUNK));
    return same_in(fb, want_fb, PILL_X - 1, PILL_Y - 1, w + 2, 22);
}

TEST_CASE(the_rail_line_names_the_pd_mini_and_a_modelled_supply)
{
    fresh();
    meter(SERVO_SOURCE_PDMINI);
    rail_supply(6.0f, 0.4f);
    CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
    CHECK(!pill_reads("SIMULATED"));
    /* The model switched on: the pill, the name kept, no tap needed. */
    supply_screen_set_model(true);
    scr->tick(1.0f / 39.0f);
    CHECK(pill_reads("SIMULATED"));
    CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
    /* A new sample repaints the plot, and the pill with it. */
    rail_supply(6.0f, 0.5f);
    CHECK(pill_reads("SIMULATED"));
    /* No IOmcu: the supply's sample, still under PD MINI. */
    meter(SERVO_SOURCE_MODEL);
    scr->tick(1.0f / 39.0f);
    CHECK(pill_reads("SIMULATED"));
    CHECK(line_supply("6.00 V", "0.50 A", "3.0 W"));
    /* In German the word is SIMULIERT; the name stays PD MINI. */
    ui_text_set_language(UI_LANG_DE);
    servo_invalidate();
    CHECK(pill_reads("SIMULIERT"));
    CHECK(line_supply("6.00 V", "0.50 A", "3.0 W"));
    ui_text_set_language(UI_LANG_EN);
    servo_invalidate();
    /* INA3221 CH1 as the meter: its own name and no pill. */
    meter(SERVO_SOURCE_INA3221);
    scr->tick(1.0f / 39.0f);
    CHECK(!pill_reads("SIMULATED"));
    meter(SERVO_SOURCE_PDMINI);
    scr->tick(1.0f / 39.0f);
    CHECK(pill_reads("SIMULATED"));
    /* The model switched off: the pill goes, without a tap. */
    supply_screen_set_model(false);
    scr->tick(1.0f / 39.0f);
    CHECK(!pill_reads("SIMULATED"));
    CHECK(line_supply("6.00 V", "0.50 A", "3.0 W"));
}

/* A pixel no part of the screen draws: one that is still there after a
 * render was not painted over. */
#define MARK_COLOUR ((gfx_color_t)0xF81F)
static void mark(int x, int y) { fb[(size_t)y * W + x] = MARK_COLOUR; }
static bool marked(int x, int y)
{
    return fb[(size_t)y * W + x] == MARK_COLOUR;
}

/* Marks in the CURRENT row, the line, the plot, and around them on both
 * cards: the MEASURED row, the tag, the SET line, SPEED's row and the left
 * card away from the grip. */
enum { MK_ROW = 0, MK_LINE, MK_PLOT, MK_MEASURED, MK_TAG, MK_SET, MK_SPEED,
       MK_LEFT, MK_COUNT };
static const struct { int x, y; } k_mark[MK_COUNT] = {
    { 600, 100 }, { 600, 152 }, { 600, 190 }, { 600, 75 }, { 520, 125 },
    { 518, 236 }, { 600, 270 }, { 40, 40 },
};
static void mark_all(void)
{
    for (int k = 0; k < MK_COUNT; ++k) {
        mark(k_mark[k].x, k_mark[k].y);
    }
}
static bool mk(int k) { return marked(k_mark[k].x, k_mark[k].y); }
/* Every mark outside the row, the line and the plot is still there. */
static bool card_kept(void)
{
    return mk(MK_MEASURED) && mk(MK_TAG) && mk(MK_SET) && mk(MK_SPEED)
           && mk(MK_LEFT);
}

/* The supply is the meter until the panel names another, and as the PD
 * mini: the row, the line and the plot are its samples.  The feedback's
 * current is not shown. */
TEST_CASE(the_current_row_shows_the_supply_while_it_is_the_meter)
{
    for (int pass = 0; pass < 3; ++pass) {
        fresh();
        if (pass == 1) {
            meter(SERVO_SOURCE_PDMINI);
        } else if (pass == 2) {
            meter(SERVO_SOURCE_MODEL);
        }
        /* No sample: the dashes. */
        CHECK(row_reads("---", UI_C_TEXT));
        CHECK(line_supply("-- V", "-- A", "-- W"));
        CHECK_EQ(servo_screen_power_points(), 0);
        /* A feedback with a current puts nothing in the row. */
        servo_screen_feedback(1500u, 1.23f, true);
        CHECK(row_reads("---", UI_C_TEXT));

        rail_supply(6.0f, 0.4f);
        CHECK(row_reads("0.40 A", UI_C_TEXT));
        CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
        CHECK_EQ(servo_screen_power_points(), 1);
        CHECK_NEAR(servo_screen_power_sample(1, 0), 0.4f, 1e-6f);
        servo_screen_feedback(1500u, 1.23f, true);
        CHECK(row_reads("0.40 A", UI_C_TEXT));
        servo_screen_feedback(0u, 0.0f, false);
        CHECK(row_reads("0.40 A", UI_C_TEXT));

        /* A window of the INA3221 is not this meter's reading. */
        rail_window(1200, 5000u);
        CHECK(row_reads("0.40 A", UI_C_TEXT));
        CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
        CHECK_EQ(servo_screen_power_points(), 1);

        /* A sample without a current reading, and a supply that does not
         * answer: the dashes, and a gap on the plot. */
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.online = true;
        st.ok = SUPPLY_OK_VOLTAGE;
        st.v = 6.0f;
        servo_screen_supply(&st);
        CHECK(row_reads("---", UI_C_TEXT));
        CHECK(line_supply("6.00 V", "-- A", "-- W"));
        CHECK_EQ(servo_screen_power_points(), 2);
        CHECK(isnan(servo_screen_power_sample(1, 0)));
        rail_supply(6.0f, 0.4f);
        CHECK(row_reads("0.40 A", UI_C_TEXT));
        st.online = false;
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        servo_screen_supply(&st);
        CHECK(row_reads("---", UI_C_TEXT));
        CHECK(line_supply("-- V", "-- A", "-- W"));
        servo_screen_supply(NULL);
        CHECK(row_reads("---", UI_C_TEXT));

        /* A reading that is not a number is no reading, and one beyond any
         * supply is drawn at the 10^8 hundredths the digits are held to. */
        st.online = true;
        st.v = 6.0f;
        st.i = NAN;
        st.p = NAN;
        servo_screen_supply(&st);
        CHECK(row_reads("---", UI_C_TEXT));
        CHECK(line_supply("6.00 V", "-- A", "-- W"));
        st.i = INFINITY;
        servo_screen_supply(&st);
        CHECK(row_reads("---", UI_C_TEXT));
        st.v = NAN;
        st.i = 0.4f;
        servo_screen_supply(&st);
        CHECK(row_reads("0.40 A", UI_C_TEXT));
        CHECK(line_supply("-- V", "0.40 A", "-- W"));
        st.v = 6.0f;
        st.i = 3.0e9f;
        st.p = -3.0e12f;
        servo_screen_supply(&st);
        CHECK(row_reads("1000000.00 A", UI_C_TEXT));
        st.i = -3.0e9f;
        servo_screen_supply(&st);
        CHECK(row_reads("-1000000.00 A", UI_C_TEXT));
    }
}

/* With the INA3221 as the meter the row, the line and the plot are CH1's
 * windows: mean current, mean bus voltage, their product, under its name. */
TEST_CASE(the_current_row_shows_ch1s_window_while_the_ina3221_is_the_meter)
{
    fresh();
    rail_supply(6.0f, 0.4f);
    meter(SERVO_SOURCE_INA3221);
    /* No window yet: the dashes, whatever the supply reads. */
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK(line_ina("-- V", "-- A", "-- W"));
    CHECK_EQ(servo_screen_power_points(), 0);

    rail_window(412, 5980u);
    CHECK(row_reads("0.41 A", UI_C_TEXT));
    CHECK(line_ina("5.98 V", "0.41 A", "2.5 W"));        /* 2.46 W */
    CHECK_EQ(servo_screen_power_points(), 1);
    CHECK_NEAR(servo_screen_power_sample(0, 0), 5.98f, 1e-5f);
    CHECK_NEAR(servo_screen_power_sample(1, 0), 0.412f, 1e-6f);
    CHECK_NEAR(servo_screen_power_sample(2, 0), 5.98f * 0.412f, 1e-5f);

    /* The supply's samples and the feedback change none of it. */
    rail_supply(6.0f, 0.9f);
    servo_screen_feedback(1500u, 1.23f, true);
    CHECK(row_reads("0.41 A", UI_C_TEXT));
    CHECK(line_ina("5.98 V", "0.41 A", "2.5 W"));
    CHECK_EQ(servo_screen_power_points(), 1);

    /* The range of the registers: every reading whole on the line, with
     * its sign, the voltage in tenths and the power in whole watts where
     * hundredths or tenths do not fit. */
    rail_window(32767, 26000u);
    CHECK(row_reads("32.77 A", UI_C_TEXT));
    CHECK(line_ina("26.0 V", "32.77 A", "851.9 W"));
    rail_window(-32768, 26000u);
    CHECK(row_reads("-32.77 A", UI_C_TEXT));
    CHECK(line_ina("26.0 V", "-32.77 A", "-852 W"));
    rail_window(-32768, 65535u);
    CHECK(line_ina("65.5 V", "-32.77 A", "-2147 W"));
    rail_window(32767, 65535u);
    CHECK(line_ina("65.5 V", "32.77 A", "2147 W"));
    /* Where the tenths stop fitting: 7 cells. */
    /* Hundredths of a volt to 9.99 V. */
    rail_window(1000, 9994u);
    CHECK(line_ina("9.99 V", "1.00 A", "10.0 W"));
    rail_window(-9990, 10000u);
    CHECK(line_ina("10.0 V", "-9.99 A", "-99.9 W"));
    rail_window(-10000, 10000u);
    CHECK(line_ina("10.0 V", "-10.00 A", "-100 W"));
    rail_window(9999, 10000u);
    CHECK(line_ina("10.0 V", "10.00 A", "100.0 W"));
    rail_window(10000, 10005u);
    CHECK(line_ina("10.0 V", "10.00 A", "100.1 W"));
    rail_window(-10005, 10000u);                  /* -100.05 W */
    CHECK(line_ina("10.0 V", "-10.01 A", "-100 W"));
    rail_window(1637, 5000u);
    CHECK(row_reads("1.64 A", UI_C_TEXT));
    rail_window(0, 0u);
    CHECK(row_reads("0.00 A", UI_C_TEXT));
    CHECK(line_ina("0.00 V", "0.00 A", "0.0 W"));

    /* A window without samples of one quantity: that one is dashes and a
     * gap, and the power with it. */
    sense_link_win_t w = rail_win(300, 5000u);
    w.current = false;
    window(&w);
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK(line_ina("5.00 V", "-- A", "-- W"));
    CHECK(isnan(servo_screen_power_sample(1, 0)));
    CHECK(isnan(servo_screen_power_sample(2, 0)));
    CHECK_NEAR(servo_screen_power_sample(0, 0), 5.0f, 1e-6f);
    w = rail_win(300, 5000u);
    w.voltage = false;
    window(&w);
    CHECK(row_reads("0.30 A", UI_C_TEXT));
    CHECK(line_ina("-- V", "0.30 A", "-- W"));
    CHECK(isnan(servo_screen_power_sample(0, 0)));
    servo_screen_window(NULL, meter_id, meter_changes);
    CHECK(row_reads("0.30 A", UI_C_TEXT));
}

/* A negative current is a reading: signed, in hundredths, rounded half away
 * from zero, and never drawn as zero while its digits are not. */
TEST_CASE(a_negative_current_is_shown_signed)
{
    static const struct { int ma; const char *text; } k[] = {
        { -20, "-0.02 A" }, { -1640, "-1.64 A" }, { -4, "0.00 A" },
        { -5, "-0.01 A" }, { -6, "-0.01 A" }, { 4, "0.00 A" },
        { 5, "0.01 A" }, { -14, "-0.01 A" }, { -15, "-0.02 A" },
        { -32768, "-32.77 A" }, { -1, "0.00 A" }, { 1, "0.00 A" },
    };
    fresh();
    meter(SERVO_SOURCE_INA3221);
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        rail_window(k[i].ma, 5000u);
        CHECK(row_reads(k[i].text, UI_C_TEXT));
        /* The plot's point is the reading, not a zero. */
        CHECK_NEAR(servo_screen_power_sample(1, 0), (float)k[i].ma / 1000.0f,
                   1e-6f);
    }
    rail_window(-20, 5000u);
    CHECK(line_ina("5.00 V", "-0.02 A", "-0.1 W"));
    rail_window(-1640, 5000u);
    CHECK(line_ina("5.00 V", "-1.64 A", "-8.2 W"));
    rail_window(-4, 5000u);
    CHECK(line_ina("5.00 V", "0.00 A", "0.0 W"));       /* -0.02 W */

    /* The supply's reading keeps its sign as well. */
    meter(SERVO_SOURCE_PDMINI);
    rail_supply(5.0f, -0.02f);
    CHECK(row_reads("-0.02 A", UI_C_TEXT));
    CHECK(line_supply("5.00 V", "-0.02 A", "-0.1 W"));
    rail_supply(5.0f, -1.64f);
    CHECK(row_reads("-1.64 A", UI_C_TEXT));
    rail_supply(5.0f, -0.004f);
    CHECK(row_reads("0.00 A", UI_C_TEXT));
}

/* A window with a sample at an end of the range shows the value it holds,
 * in the warning colour, in the row and on the line. */
TEST_CASE(a_clipped_window_draws_its_current_in_the_warning_colour)
{
    fresh();
    meter(SERVO_SOURCE_INA3221);
    rail_window(1590, 5900u);
    CHECK(row_reads("1.59 A", UI_C_TEXT));

    sense_link_win_t w = rail_win(1590, 5900u);
    w.clip_hi = true;
    w.clipped = 7u;
    w.max_ma  = 1638;
    window(&w);
    /* The same digits: the colour alone repaints the row. */
    CHECK(row_reads("1.59 A", UI_C_WARN));
    CHECK(line_reads(ina_label(), LAB_INA, "5.90 V", "1.59 A", UI_C_WARN,
                     "9.4 W"));
    CHECK_NEAR(servo_screen_power_sample(1, 0), 1.59f, 1e-6f);

    rail_window(1590, 5900u);
    CHECK(row_reads("1.59 A", UI_C_TEXT));
    CHECK(line_ina("5.90 V", "1.59 A", "9.4 W"));

    /* The bottom of the range, with a negative mean. */
    w = rail_win(-1600, 5900u);
    w.clip_lo = true;
    w.clipped = 255u;
    window(&w);
    CHECK(row_reads("-1.60 A", UI_C_WARN));
    CHECK(line_reads(ina_label(), LAB_INA, "5.90 V", "-1.60 A", UI_C_WARN,
                     "-9.4 W"));

    /* The count of clipped samples alone marks nothing, and a clipped
     * window without current samples has no current to colour. */
    w = rail_win(700, 5900u);
    w.clipped = 3u;
    window(&w);
    CHECK(row_reads("0.70 A", UI_C_TEXT));
    w = rail_win(700, 5900u);
    w.clip_hi = true;
    w.current = false;
    window(&w);
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK(line_ina("5.90 V", "-- A", "-- W"));

    /* The supply is never drawn clipped: the mark is the window's. */
    w = rail_win(1590, 5900u);
    w.clip_hi = true;
    window(&w);
    CHECK(row_reads("1.59 A", UI_C_WARN));
    meter(SERVO_SOURCE_PDMINI);
    rail_supply(6.0f, 1.59f);
    CHECK(row_reads("1.59 A", UI_C_TEXT));
    CHECK(line_supply("6.00 V", "1.59 A", "9.5 W"));
}

/* Every change of meter, with the screen open: the row, the line, its name
 * and the plot are the new meter's in both buffers, as a full redraw draws
 * them.  The trace starts again when the INA3221 becomes or stops being the
 * meter; between the PD mini and the model it is the supply's and stays. */
TEST_CASE(a_change_of_meter_repaints_the_row_the_line_and_the_plot)
{
    static const servo_source_id_t k_src[3] = {
        SERVO_SOURCE_PDMINI, SERVO_SOURCE_INA3221, SERVO_SOURCE_MODEL,
    };
    for (int from = 0; from < 3; ++from) {
        for (int to = 0; to < 3; ++to) {
            if (from == to) {
                continue;
            }
            fresh();
            two_buffers();
            meter(k_src[from]);
            const bool from_ina = (k_src[from] == SERVO_SOURCE_INA3221);
            const bool to_ina   = (k_src[to] == SERVO_SOURCE_INA3221);
            /* Three readings of each kind: three points of the meter's. */
            for (int n = 0; n < 3; ++n) {
                rail_supply(6.0f, 0.4f);
                rail_window(1200, 5000u);
            }
            CHECK_EQ(servo_screen_power_points(), 3);
            CHECK(both_whole());
            if (from_ina) {
                CHECK(row_reads("1.20 A", UI_C_TEXT));
                CHECK(line_ina("5.00 V", "1.20 A", "6.0 W"));
            } else {
                CHECK(row_reads("0.40 A", UI_C_TEXT));
                CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
            }

            meter(k_src[to]);
            CHECK(both_whole());
            /* At once, from the reading the new meter last gave. */
            if (to_ina) {
                CHECK(row_reads("1.20 A", UI_C_TEXT));
                CHECK(line_ina("5.00 V", "1.20 A", "6.0 W"));
            } else {
                CHECK(row_reads("0.40 A", UI_C_TEXT));
                CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
            }
            CHECK_EQ(servo_screen_power_points(),
                     (from_ina != to_ina) ? 0 : 3);

            /* And from then on the new meter's readings only. */
            rail_supply(6.0f, 0.5f);
            rail_window(1300, 5000u);
            CHECK_EQ(servo_screen_power_points(),
                     (from_ina != to_ina) ? 1 : 4);
            CHECK(both_whole());
            if (to_ina) {
                CHECK(row_reads("1.30 A", UI_C_TEXT));
                CHECK_NEAR(servo_screen_power_sample(1, 0), 1.3f, 1e-6f);
            } else {
                CHECK(row_reads("0.50 A", UI_C_TEXT));
                CHECK_NEAR(servo_screen_power_sample(1, 0), 0.5f, 1e-6f);
            }
            /* The same meter said again changes nothing. */
            meter(k_src[to]);
            CHECK_EQ(servo_screen_power_points(),
                     (from_ina != to_ina) ? 1 : 4);
        }
    }
    /* With the settings open over the left card the right card follows the
     * meter all the same. */
    fresh();
    two_buffers();
    open_settings();
    rail_supply(6.0f, 0.4f);
    CHECK(both_whole());
    meter(SERVO_SOURCE_INA3221);
    rail_window(1200, 5000u);
    CHECK(both_whole());
    CHECK(row_reads("1.20 A", UI_C_TEXT));
    CHECK(line_ina("5.00 V", "1.20 A", "6.0 W"));
    meter(SERVO_SOURCE_PDMINI);
    CHECK(both_whole());
    CHECK(row_reads("0.40 A", UI_C_TEXT));
}

/*
 * The meter comes from the snapshot and the windows from a queue, so either
 * can be the older.  A window taken under a meter that has been replaced is
 * dropped; one taken under a later meter brings that meter with it; an
 * answer older than the meter in force changes nothing.
 */
TEST_CASE(a_window_is_shown_under_the_meter_of_its_poll)
{
    const sense_link_win_t w1 = rail_win(1100, 5000u);
    const sense_link_win_t w2 = rail_win(1200, 5000u);
    const sense_link_win_t w3 = rail_win(1300, 5000u);

    /* The snapshot ahead of the queue, PD mini to INA3221: the windows
     * from before the change are not the INA3221's points. */
    fresh();
    servo_screen_source(SERVO_SOURCE_PDMINI, 1u);
    rail_supply(6.0f, 0.4f);
    servo_screen_source(SERVO_SOURCE_INA3221, 2u);
    servo_screen_window(&w1, SERVO_SOURCE_PDMINI, 1u);
    CHECK_EQ(servo_screen_power_points(), 0);
    CHECK(row_reads("---", UI_C_TEXT));
    servo_screen_window(&w2, SERVO_SOURCE_INA3221, 2u);
    CHECK_EQ(servo_screen_power_points(), 1);
    CHECK(row_reads("1.20 A", UI_C_TEXT));

    /* The queue ahead of the snapshot: the window brings its meter, and
     * the snapshot that catches up changes nothing. */
    fresh();
    servo_screen_source(SERVO_SOURCE_PDMINI, 1u);
    rail_supply(6.0f, 0.4f);
    servo_screen_window(&w1, SERVO_SOURCE_PDMINI, 1u);
    CHECK_EQ(servo_screen_power_points(), 1);       /* the supply's */
    servo_screen_window(&w2, SERVO_SOURCE_INA3221, 2u);
    servo_screen_window(&w3, SERVO_SOURCE_INA3221, 2u);
    CHECK_EQ(servo_screen_power_points(), 2);
    CHECK_NEAR(servo_screen_power_sample(1, 1), 1.2f, 1e-6f);
    CHECK_NEAR(servo_screen_power_sample(1, 0), 1.3f, 1e-6f);
    CHECK(line_ina("5.00 V", "1.30 A", "6.5 W"));
    /* The next frame's snapshot, read before that window's poll. */
    servo_screen_source(SERVO_SOURCE_PDMINI, 1u);
    CHECK_EQ(servo_screen_power_points(), 2);
    CHECK(row_reads("1.30 A", UI_C_TEXT));
    servo_screen_source(SERVO_SOURCE_INA3221, 2u);
    CHECK_EQ(servo_screen_power_points(), 2);

    /* INA3221 to PD mini with windows still queued: they are dropped, the
     * meter stays the PD mini and the plot the supply's. */
    servo_screen_source(SERVO_SOURCE_PDMINI, 3u);
    CHECK_EQ(servo_screen_power_points(), 0);
    servo_screen_window(&w1, SERVO_SOURCE_INA3221, 2u);
    CHECK_EQ(servo_screen_power_points(), 0);
    CHECK(row_reads("0.40 A", UI_C_TEXT));
    CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
    /* Back at count 4: the window dropped at count 2 is not the reading. */
    servo_screen_source(SERVO_SOURCE_INA3221, 4u);
    CHECK(row_reads("1.30 A", UI_C_TEXT));          /* w3, taken at 2 */
    servo_screen_window(&w2, SERVO_SOURCE_INA3221, 4u);
    CHECK(row_reads("1.20 A", UI_C_TEXT));
    CHECK_EQ(servo_screen_power_points(), 1);

    /* The count across 2^32: 0 is after 0xFFFFFFFF, and 0xFFFFFFFF
     * before 0; 2^31 - 1 ahead is the last count that is later. */
    static const uint32_t k_at[] = { 0xFFFFFFFFu, 0x7FFFFFFFu, 0u, 5u };
    for (size_t i = 0u; i < sizeof(k_at) / sizeof(k_at[0]); ++i) {
        const uint32_t at = k_at[i];
        fresh();
        servo_screen_source(SERVO_SOURCE_PDMINI, at);
        rail_supply(6.0f, 0.4f);
        servo_screen_source(SERVO_SOURCE_INA3221, at + 1u);
        servo_screen_window(&w1, SERVO_SOURCE_INA3221, at + 1u);
        CHECK(row_reads("1.10 A", UI_C_TEXT));
        servo_screen_source(SERVO_SOURCE_PDMINI, at);          /* older */
        CHECK(row_reads("1.10 A", UI_C_TEXT));
        servo_screen_window(&w2, SERVO_SOURCE_PDMINI, at);     /* older */
        CHECK(row_reads("1.10 A", UI_C_TEXT));
        CHECK_EQ(servo_screen_power_points(), 1);
        /* 2^31 - 1 ahead of at + 1 is the last count that is later; so far
         * ahead, the window from before it is forgotten. */
        servo_screen_source(SERVO_SOURCE_PDMINI, at + 0x80000000u);
        CHECK(row_reads("0.40 A", UI_C_TEXT));
        servo_screen_source(SERVO_SOURCE_INA3221, at + 0x80000001u);
        CHECK(row_reads("---", UI_C_TEXT));
        servo_screen_window(&w1, SERVO_SOURCE_INA3221, at + 0x80000001u);
        CHECK(row_reads("1.10 A", UI_C_TEXT));
        /* 2^31 ahead reads as behind. */
        servo_screen_source(SERVO_SOURCE_PDMINI, at + 1u);
        CHECK(row_reads("1.10 A", UI_C_TEXT));
    }

    /* Changes that went by unseen: a count two or more ahead empties the
     * plot whatever the meter is, since another may have been the rail's
     * in between, and the INA3221's window from before them is not shown. */
    static const uint32_t k_from[] = { 2u, 0xFFFFFFFEu, 0xFFFFFFFFu };
    for (size_t i = 0u; i < sizeof(k_from) / sizeof(k_from[0]); ++i) {
        const uint32_t n = k_from[i];
        fresh();
        servo_screen_source(SERVO_SOURCE_INA3221, n);
        servo_screen_window(&w1, SERVO_SOURCE_INA3221, n);
        servo_screen_window(&w2, SERVO_SOURCE_INA3221, n);
        CHECK_EQ(servo_screen_power_points(), 2);
        servo_screen_source(SERVO_SOURCE_INA3221, n);       /* the same */
        CHECK_EQ(servo_screen_power_points(), 2);
        /* INA3221, PD mini, INA3221 between two snapshots. */
        servo_screen_source(SERVO_SOURCE_INA3221, n + 2u);
        CHECK_EQ(servo_screen_power_points(), 0);
        CHECK(row_reads("---", UI_C_TEXT));
        CHECK(line_ina("-- V", "-- A", "-- W"));
        /* The PD mini's interval, arriving behind it, shows nothing. */
        servo_screen_window(&w3, SERVO_SOURCE_PDMINI, n + 1u);
        CHECK(row_reads("---", UI_C_TEXT));
        servo_screen_window(&w3, SERVO_SOURCE_INA3221, n + 2u);
        CHECK_EQ(servo_screen_power_points(), 1);
        CHECK(row_reads("1.30 A", UI_C_TEXT));
        /* A window that is itself two ahead: the same, then its point. */
        servo_screen_window(&w1, SERVO_SOURCE_INA3221, n + 4u);
        CHECK_EQ(servo_screen_power_points(), 1);
        CHECK(row_reads("1.10 A", UI_C_TEXT));

        /* The supply's trace likewise: PD mini, INA3221, PD mini; and PD
         * mini, INA3221, model, where the name does not change either. */
        for (int to = 0; to < 2; ++to) {
            fresh();
            servo_screen_source(SERVO_SOURCE_PDMINI, n);
            rail_supply(6.0f, 0.4f);
            rail_supply(6.0f, 0.4f);
            CHECK_EQ(servo_screen_power_points(), 2);
            servo_screen_source(to == 0 ? SERVO_SOURCE_PDMINI
                                        : SERVO_SOURCE_MODEL, n + 2u);
            CHECK_EQ(servo_screen_power_points(), 0);
            CHECK(row_reads("0.40 A", UI_C_TEXT));   /* its last reading */
            rail_supply(6.0f, 0.5f);
            CHECK_EQ(servo_screen_power_points(), 1);
        }
        /* One ahead between the PD mini and the model: the supply's trace
         * goes on. */
        fresh();
        servo_screen_source(SERVO_SOURCE_PDMINI, n);
        rail_supply(6.0f, 0.4f);
        servo_screen_source(SERVO_SOURCE_MODEL, n + 1u);
        rail_supply(6.0f, 0.4f);
        CHECK_EQ(servo_screen_power_points(), 2);
    }

    /* The supply's samples wait on a queue as well.  One taken under a
     * meter replaced since is the supply's last reading and no point; one
     * ahead of the snapshot brings its meter. */
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.v = 6.0f;
    fresh();
    servo_screen_source(SERVO_SOURCE_INA3221, 2u);
    servo_screen_window(&w1, SERVO_SOURCE_INA3221, 2u);
    servo_screen_source(SERVO_SOURCE_PDMINI, 3u);       /* the snapshot */
    CHECK_EQ(servo_screen_power_points(), 0);
    st.i = 0.5f; st.p = 3.0f;
    servo_screen_supply_at(&st, SERVO_SOURCE_INA3221, 2u);
    CHECK_EQ(servo_screen_power_points(), 0);
    CHECK(row_reads("0.50 A", UI_C_TEXT));
    CHECK(line_supply("6.00 V", "0.50 A", "3.0 W"));
    st.i = 0.6f; st.p = 3.6f;
    servo_screen_supply_at(&st, SERVO_SOURCE_PDMINI, 3u);
    CHECK_EQ(servo_screen_power_points(), 1);
    CHECK_NEAR(servo_screen_power_sample(1, 0), 0.6f, 1e-6f);
    CHECK(row_reads("0.60 A", UI_C_TEXT));
    /* Ahead of the snapshot, to the INA3221 and back. */
    st.i = 0.7f; st.p = 4.2f;
    servo_screen_supply_at(&st, SERVO_SOURCE_INA3221, 4u);
    CHECK_EQ(servo_screen_power_points(), 0);
    CHECK(line_ina("5.00 V", "1.10 A", "5.5 W"));       /* w1, kept */
    servo_screen_source(SERVO_SOURCE_PDMINI, 3u);        /* older */
    CHECK(line_ina("5.00 V", "1.10 A", "5.5 W"));
    st.i = 0.8f; st.p = 4.8f;
    servo_screen_supply_at(&st, SERVO_SOURCE_PDMINI, 5u);
    CHECK_EQ(servo_screen_power_points(), 1);
    CHECK(row_reads("0.80 A", UI_C_TEXT));
    /* A window of count 4 behind it is dropped; the supply's trace stays. */
    servo_screen_window(&w2, SERVO_SOURCE_INA3221, 4u);
    CHECK_EQ(servo_screen_power_points(), 1);
    CHECK(row_reads("0.80 A", UI_C_TEXT));
    /* A sample with no tag is one of the meter in force. */
    st.i = 0.9f; st.p = 5.4f;
    servo_screen_supply(&st);
    CHECK_EQ(servo_screen_power_points(), 2);
    servo_screen_supply_at(NULL, SERVO_SOURCE_INA3221, 9u);
    CHECK_EQ(servo_screen_power_points(), 2);
    CHECK(row_reads("0.90 A", UI_C_TEXT));
    /* Across 2^32. */
    fresh();
    servo_screen_source(SERVO_SOURCE_PDMINI, 0u);
    servo_screen_supply_at(&st, SERVO_SOURCE_INA3221, 0xFFFFFFFFu);
    CHECK_EQ(servo_screen_power_points(), 0);            /* behind 0 */
    CHECK(line_supply("6.00 V", "0.90 A", "5.4 W"));
    servo_screen_supply_at(&st, SERVO_SOURCE_PDMINI, 0u);
    CHECK_EQ(servo_screen_power_points(), 1);

    /* A window while the link is down is dropped with its meter. */
    fresh();
    servo_screen_source(SERVO_SOURCE_PDMINI, 1u);
    servo_screen_set_link(false);
    servo_screen_window(&w1, SERVO_SOURCE_INA3221, 2u);
    servo_screen_set_link(true);
    CHECK(line_supply("-- V", "-- A", "-- W"));
    servo_screen_window(NULL, SERVO_SOURCE_INA3221, 2u);
    CHECK(line_supply("-- V", "-- A", "-- W"));
}

/* A link that goes takes the INA3221's last window with it: the dashes,
 * until a window arrives over a link that is up. */
TEST_CASE(link_loss_clears_the_current_row)
{
    fresh();
    two_buffers();
    meter(SERVO_SOURCE_INA3221);
    rail_window(1200, 5000u);
    CHECK(row_reads("1.20 A", UI_C_TEXT));
    servo_screen_set_link(false);
    CHECK(both_whole());
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK(line_ina("-- V", "-- A", "-- W"));
    /* A window that was on its way is dropped: no reading, no point. */
    const int points = servo_screen_power_points();
    rail_window(1300, 5000u);
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK_EQ(servo_screen_power_points(), points);
    /* Said again, nothing changes. */
    servo_screen_set_link(false);
    CHECK(row_reads("---", UI_C_TEXT));
    /* The link back: still no reading until one arrives. */
    servo_screen_set_link(true);
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK(line_ina("-- V", "-- A", "-- W"));
    rail_window(1300, 5000u);
    CHECK(row_reads("1.30 A", UI_C_TEXT));
    CHECK(both_whole());

    /* The meter drops to the model with the link, in either order: the
     * row is the supply's then, and the window is gone when the INA3221
     * is the meter again. */
    for (int order = 0; order < 2; ++order) {
        fresh();
        meter(SERVO_SOURCE_INA3221);
        rail_supply(6.0f, 0.4f);
        rail_window(1200, 5000u);
        if (order == 0) {
            servo_screen_set_link(false);
            meter(SERVO_SOURCE_MODEL);
        } else {
            meter(SERVO_SOURCE_MODEL);
            servo_screen_set_link(false);
        }
        CHECK(row_reads("0.40 A", UI_C_TEXT));
        CHECK(line_supply("6.00 V", "0.40 A", "2.4 W"));
        servo_screen_set_link(true);
        meter(SERVO_SOURCE_INA3221);
        CHECK(row_reads("---", UI_C_TEXT));
    }

    /* The supply's reading goes when its sample says it does not answer,
     * which is what the PD mini's samples say once the link is down. */
    fresh();
    meter(SERVO_SOURCE_PDMINI);
    rail_supply(6.0f, 0.4f);
    servo_screen_set_link(false);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    servo_screen_supply(&st);
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK(line_supply("-- V", "-- A", "-- W"));
}

/* The row is repainted when the digits it shows change, and then the row
 * alone: not the card around it.  The same digits paint nothing there. */
TEST_CASE(the_current_row_repaints_on_a_changed_digit_and_alone)
{
    fresh_armed();
    meter(SERVO_SOURCE_INA3221);
    rail_window(412, 5980u);
    scr->render(&cv, 0);

    /* 0.412 A to 0.414 A: the plot takes its point, the row is left. */
    mark_all();
    rail_window(414, 5980u);
    scr->render(&cv, 0);
    CHECK(mk(MK_ROW));
    CHECK(!mk(MK_LINE));
    CHECK(!mk(MK_PLOT));
    CHECK(card_kept());

    /* 0.414 A to 0.415 A is 0.41 to 0.42: the row, and nothing around it
     * but the line and the plot the window also feeds. */
    mark_all();
    rail_window(415, 5980u);
    scr->render(&cv, 0);
    CHECK(!mk(MK_ROW));
    CHECK(card_kept());
    CHECK(row_reads("0.42 A", UI_C_TEXT));

    /* No window in a frame: nothing is painted at all. */
    mark_all();
    scr->render(&cv, 0);
    CHECK(mk(MK_ROW));
    CHECK(mk(MK_LINE));
    CHECK(mk(MK_PLOT));
    CHECK(card_kept());

    /* The other buffer owes the row once and takes it once. */
    settle_drawn();                 /* ARM's flash after the arm is over */
    rail_window(500, 5980u);
    CHECK(both_whole_in(502, 6, 292, 400));
    CHECK(row_reads("0.50 A", UI_C_TEXT));
    scr->render(&cv1, 1);
    mark_all();
    scr->render(&cv, 0);
    CHECK(mk(MK_ROW));
    CHECK(mk(MK_PLOT));
    CHECK(card_kept());

    /* The supply as the meter: the same rule. */
    fresh_armed();
    meter(SERVO_SOURCE_PDMINI);
    rail_supply(6.0f, 0.402f);
    scr->render(&cv, 0);
    mark_all();
    rail_supply(6.0f, 0.404f);
    scr->render(&cv, 0);
    CHECK(mk(MK_ROW));
    CHECK(!mk(MK_PLOT));
    CHECK(card_kept());
    mark_all();
    rail_supply(6.0f, 0.43f);
    scr->render(&cv, 0);
    CHECK(!mk(MK_ROW));
    CHECK(card_kept());

    /* A feedback whose current alone moved paints nothing: neither the
     * row nor the card. */
    servo_screen_feedback(1500u, 0.2f, true);
    scr->render(&cv, 0);
    mark_all();
    servo_screen_feedback(1500u, 0.9f, true);
    scr->render(&cv, 0);
    CHECK(mk(MK_ROW));
    CHECK(mk(MK_PLOT));
    CHECK(card_kept());
}

/* The plot keeps one point for each window, however many a frame hands
 * over, in their order; a frame with none adds none and repaints nothing. */
TEST_CASE(the_plot_keeps_one_point_for_each_window)
{
    for (int n = 0; n <= 4; ++n) {
        fresh();
        meter(SERVO_SOURCE_INA3221);
        rail_window(50, 4000u);
        scr->render(&cv, 0);
        CHECK_EQ(servo_screen_power_points(), 1);
        mark_all();
        for (int k = 0; k < n; ++k) {
            rail_window(100 * (k + 1), 5000u + (unsigned)k);
        }
        scr->render(&cv, 0);
        CHECK_EQ(servo_screen_power_points(), 1 + n);
        for (int k = 0; k < n; ++k) {
            const int back = n - 1 - k;
            const float a = 0.1f * (float)(k + 1);
            const float v = (float)(5000 + k) / 1000.0f;
            CHECK_NEAR(servo_screen_power_sample(0, back), v, 1e-5f);
            CHECK_NEAR(servo_screen_power_sample(1, back), a, 1e-6f);
            CHECK_NEAR(servo_screen_power_sample(2, back), v * a, 1e-5f);
        }
        CHECK_NEAR(servo_screen_power_sample(1, n), 0.05f, 1e-6f);
        CHECK_EQ(mk(MK_PLOT), n == 0);
        CHECK_EQ(mk(MK_LINE), n == 0);
        CHECK_EQ(mk(MK_ROW), n == 0);
        CHECK(card_kept());
        if (n > 0) {
            char want[16];
            snprintf(want, sizeof(want), "0.%d0 A", n);
            CHECK(row_reads(want, UI_C_TEXT));
        }
    }
    /* The supply's samples are the points while it is the meter, one each,
     * and the windows none. */
    fresh();
    meter(SERVO_SOURCE_PDMINI);
    for (int k = 0; k < 4; ++k) {
        rail_supply(6.0f, 0.1f * (float)(k + 1));
        rail_window(900, 5000u);
        rail_window(900, 5000u);
    }
    CHECK_EQ(servo_screen_power_points(), 4);
    CHECK_NEAR(servo_screen_power_sample(1, 0), 0.4f, 1e-6f);
    CHECK_NEAR(servo_screen_power_sample(1, 3), 0.1f, 1e-6f);
}

/* One run of the automatic test with @p changing: the meter changed every
 * 300 ms and two windows handed over every 100 ms while it runs.  With
 * @p behind every supply sample of the run was taken under a meter
 * replaced since. */
static void a_short_run(bool changing, bool behind)
{
    static const servo_source_id_t k_src[4] = {
        SERVO_SOURCE_INA3221, SERVO_SOURCE_PDMINI, SERVO_SOURCE_INA3221,
        SERVO_SOURCE_MODEL,
    };
    bench_fresh();
    short_runs();
    supply_screen_put(5.5f, 1.5f);
    if (behind) {
        meter(SERVO_SOURCE_INA3221);
        meter(SERVO_SOURCE_PDMINI);
        bench.behind = true;
    }
    hold_start(2.3f);
    for (int i = 0; i < 600 && servo_screen_testing(); ++i) {
        if (changing) {
            meter(k_src[(i / 3) % 4]);
            rail_window(2000 + i, 9000u);
            rail_window(-2000 - i, 9000u);
        }
        bench_frames(100u);
        if (changing && (i % 7) == 0) {
            scr->render(&cv, 0);
        }
    }
    bench_frames(1000u);
}

/* A run reads the PD mini whatever the rail's meter is and however it
 * changes: its report and its files are those of a run under one meter,
 * and the screen draws the meter's reading as a full redraw would. */
TEST_CASE(a_run_reads_the_supply_while_the_meter_changes)
{
    a_short_run(false, false);
    CHECK(!servo_screen_testing());
    CHECK(strstr(bench.report, "Result:         PASS") != NULL);
    static char report[sizeof(bench.report)];
    memcpy(report, bench.report, sizeof(report));
    const unsigned csv = bench.csv, txt = bench.txt;
    CHECK(csv > 50u);

    a_short_run(true, false);
    CHECK(!servo_screen_testing());
    CHECK_EQ(bench.opens, 1u);
    CHECK_EQ(bench.ends, 1u);
    CHECK_EQ(bench.csv, csv);
    CHECK_EQ(bench.txt, txt);
    CHECK_STR_EQ(bench.report, report);
    CHECK(strstr(bench.report, "Supply:         PD mini") != NULL);
    CHECK(!bench.out);

    /* Samples taken under a meter replaced since are no points of the
     * plot, and the run reads every one of them. */
    a_short_run(false, true);
    CHECK(!servo_screen_testing());
    CHECK_EQ(bench.csv, csv);
    CHECK_EQ(bench.txt, txt);
    CHECK_STR_EQ(bench.report, report);
    CHECK_EQ(servo_screen_power_points(), 0);

    /* Mid-run, on each meter: the right card as a full redraw draws it,
     * and the row the meter's. */
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    bench_frames(2000u);
    settle_drawn();
    meter(SERVO_SOURCE_INA3221);
    rail_window(-1640, 5000u);
    bench_frames(100u);
    CHECK(servo_screen_testing());
    CHECK(both_whole_in(502, 6, 292, 400));
    CHECK(row_reads("-1.64 A", UI_C_TEXT));
    meter(SERVO_SOURCE_PDMINI);
    bench_frames(200u);
    CHECK(servo_screen_testing());
    CHECK(both_whole_in(502, 6, 292, 400));
    CHECK(!row_reads("-1.64 A", UI_C_TEXT));
    meter(SERVO_SOURCE_MODEL);
    bench_frames(200u);
    CHECK(servo_screen_testing());
    CHECK(both_whole_in(502, 6, 292, 400));
    /* The link going ends the run and clears the INA3221's row. */
    meter(SERVO_SOURCE_INA3221);
    rail_window(700, 5000u);
    servo_screen_set_link(false);
    CHECK(!servo_screen_testing());
    CHECK(row_reads("---", UI_C_TEXT));
}

int main(void)
{
    RUN(an_encoder_the_coprocessor_does_not_hold_gives_the_run_no_angle_columns);
    RUN(the_encoders_angle_replaces_the_dashes_in_the_measured_row);
    RUN(an_enabled_encoder_without_a_reading_does_not_fall_back_to_the_feedback);
    RUN(outputs_prog_port_entry_sets_what_the_tester_drives_at_rest);
    RUN(the_servo_screen_names_the_pins_the_tester_drives);
    RUN(the_target_line_lists_the_pins_that_fit_and_counts_the_rest);
    RUN(the_target_row_stays_in_sight_under_the_settings_and_a_keypad);
    RUN(the_tag_row_carries_the_pulse_range);
    RUN(the_rail_line_names_the_pd_mini_and_a_modelled_supply);
    RUN(the_port_pulse_is_not_switched_off_under_a_test_or_a_sweep);
    RUN(the_tester_target_and_pulse_stay_while_the_port_is_held);
    RUN(the_dut_page_sets_the_encoders_centre_from_the_live_count);
    RUN(a_link_that_goes_down_takes_the_encoders_angle_with_it);
    RUN(a_run_with_the_encoder_on_writes_the_angle_columns);
    RUN(a_run_started_without_a_magnet_says_so_in_its_report);
    RUN(a_run_with_the_encoder_off_writes_the_old_columns);
    RUN(a_touch_on_the_dial_points_the_horn_there);
    RUN(a_drag_keeps_commanding);
    RUN(the_case_is_not_the_dial);
    RUN(the_travel_limit_clamps_rather_than_refuses);
    RUN(centre_and_release_post_their_own_commands);
    RUN(a_hold_on_arm_asks_to_arm_exactly_once);
    RUN(a_short_press_on_arm_asks_for_nothing);
    RUN(a_finger_that_leaves_arm_arms_nothing);
    RUN(arming_and_disarming_come_from_the_same_button);
    RUN(a_press_that_disarmed_the_bench_does_not_then_arm_it);
    RUN(a_pending_disarm_is_not_overwritten_by_a_position);
    RUN(the_arm_button_fades_across_the_hold_and_flashes_when_it_lands);
    RUN(the_hold_repaints_the_button_and_leaves_the_card_alone);
    RUN(a_command_carries_the_endpoints_of_the_type_it_was_made_for);
    RUN(the_speed_travels_with_the_command_as_a_rate);
    RUN(changing_the_type_says_the_position_again);
    RUN(the_trim_says_the_position_again_while_it_is_held);
    RUN(nothing_is_said_again_when_nothing_is_being_held);
    RUN(a_resting_armed_servo_takes_the_new_profile);
    RUN(a_profile_changed_while_an_arm_is_pending_is_the_one_armed);
    RUN(a_stop_abandons_a_hold_that_is_under_way);
    RUN(a_cancelled_hold_leaves_no_arm_to_be_read_later);
    RUN(a_second_contact_cannot_take_over_the_arm_hold);
    RUN(a_dial_press_before_the_arm_is_not_held_after_it);
    RUN(a_stop_on_a_bench_that_was_not_armed_still_lets_go);
    RUN(a_stop_stops_the_screen_holding_anything);
    RUN(leaving_disarms_and_lets_go_of_the_output);
    RUN(trim_shifts_the_pulse_and_not_the_angle);
    RUN(feedback_is_shown_rather_than_travelled_to);
    RUN(a_sweep_needs_an_armed_bench_and_a_coprocessor_that_sweeps);
    RUN(the_horn_follows_the_sweep_and_hold_keeps_it_where_it_is);
    RUN(a_sweep_ends_on_the_dial_centre_release_disarm_and_leave);
    RUN(a_changed_setting_starts_the_sweep_over);
    RUN(a_profile_changed_while_it_sweeps_goes_with_the_sweep);
    RUN(hold_keeps_the_output_where_it_has_got_to);
    RUN(speed_says_when_it_limits_the_sweep);
    RUN(pause_holds_the_sweep_and_a_second_tap_resumes_it);
    RUN(paused_carries_the_sweep_on_from_its_phase);
    RUN(the_pause_is_drawn_from_when_the_hold_was_acknowledged);
    RUN(a_pause_with_feedback_holds_the_angle_the_servo_reports);
    RUN(a_pause_replays_from_the_tap_when_feedback_goes);
    RUN(the_acknowledged_pause_moves_the_output_at_speed);
    RUN(an_early_resume_is_rebased_on_the_phase_the_far_end_kept);
    RUN(a_changed_curve_is_drawn_from_its_acknowledged_start);
    RUN(a_change_during_a_pause_drain_holds_the_drawn_angle);
    RUN(an_early_resume_keeps_the_output_the_far_end_held);
    RUN(the_link_going_ends_a_pause_at_rest);
    RUN(sweep_needs_a_surface_bound);
    RUN(a_sweep_does_not_come_back_with_the_link);
    RUN(sweep_needs_the_link);
    RUN(a_resume_of_a_let_go_pause_is_dropped);
    RUN(a_resume_origin_is_used_once);
    RUN(commands_from_a_let_go_pause_are_stale);
    RUN(a_resume_said_again_keeps_its_pause);
    RUN(a_drag_after_pause_outlives_the_let_go);
    RUN(a_resume_and_its_repeat_go_with_the_let_go);
    RUN(a_later_pause_goes_with_the_let_go);
    RUN(a_late_chained_hold_ends_its_root);
    RUN(a_changed_curve_from_paused_is_a_new_start);
    RUN(no_undo_across_a_let_go);
    RUN(an_undone_pair_keeps_the_resume_waiting);
    RUN(a_refused_sweep_leaves_nothing_driven);
    RUN(an_early_resume_keeps_the_reading_at_the_hold);
    RUN(a_released_pause_says_no_drawn_angle);
    RUN(leaving_forgets_the_sweep_model);
    RUN(a_start_acknowledgement_applies_to_its_own_command);
    RUN(a_later_repeat_acknowledged_ends_the_wait);
    RUN(a_restart_drains_the_old_curve_in_its_old_mapping);
    RUN(an_unacknowledged_pause_is_drawn_on_for_500_ms_at_most);
    RUN(two_sweep_taps_in_one_pass_send_nothing);
    RUN(speed_100_is_immediate_in_the_drawing_and_the_replay);
    RUN(a_resume_is_drawn_from_its_acknowledgement);
    RUN(an_acknowledgement_after_the_pause_ended_changes_nothing);
    RUN(a_curve_changed_while_paused_starts_over);
    RUN(a_pause_ends_where_a_hold_ended);
    RUN(hold_without_feedback_matches_the_far_ends_slew);
    RUN(the_drawn_output_starts_where_the_far_ends_output_starts);
    RUN(a_range_changed_while_it_sweeps_goes_with_its_own_amplitude);
    RUN(the_horn_follows_the_curve_from_where_the_far_end_started_it);
    RUN(a_cancelled_gesture_does_not_arm);
    RUN(leaving_servo_switches_off_an_on_a_run_or_the_hv_hold_asked);
    RUN(a_run_ending_under_output_ons_hold_switches_nothing_on_past_6_v);
    RUN(output_ons_hold_looks_at_the_set_point_again_as_it_completes);
    RUN(the_set_points_go_back_whichever_screen_is_up);
    RUN(the_set_points_wait_for_a_sample_after_the_off);
    RUN(a_supply_reading_stamped_on_tick_zero_keeps_its_stamp);
    RUN(start_test_starts_a_second_run);
    RUN(the_knob_moves_the_horn_by_how_far_it_turned);
    RUN(the_knob_stops_at_the_servos_travel);
    RUN(the_knob_never_arms_the_servo_bench);
    RUN(a_knob_that_does_not_turn_commands_nothing_on_the_servo);
    RUN(a_finger_on_the_dial_owns_the_horn_against_the_knob);
    RUN(a_finger_that_lifted_in_the_frame_still_owned_the_dial);
    RUN(a_withdrawn_knob_command_restores_the_horn);
    RUN(a_withdrawn_knob_command_leaves_the_servo_idle);
    RUN(a_turn_in_a_frame_where_the_settings_were_open_is_dropped);
    RUN(a_turn_in_a_frame_where_a_sweep_ran_is_dropped);
    RUN(a_withdrawn_knob_command_leaves_a_position_from_a_touch);
    RUN(a_withdrawal_does_not_touch_a_command_posted_after_the_knob);
    RUN(the_knob_leaves_the_horn_to_the_settings_panel);
    RUN(a_pending_release_is_not_overwritten_by_the_knob);
    RUN(a_completed_arm_is_not_overwritten_by_the_servo_knob);
    RUN(the_knob_does_not_take_the_horn_from_a_sweep);
    RUN(the_start_line_follows_the_report_being_taken);
    RUN(arm_first_goes_once_the_bench_is_armed);
    RUN(a_run_outside_the_caps_is_refused_before_the_warning);
    RUN(the_result_names_the_report_only_when_it_was_written);
    RUN(a_refusal_goes_with_the_edit_that_answers_it);
    RUN(a_hold_on_supplys_output_on_holds_the_restore_back);
    RUN(leaving_under_a_dial_drag_ends_the_drag);
    RUN(a_press_ends_a_dial_drag_that_lost_its_release);
    RUN(a_second_press_ends_a_dial_drag);
    RUN(the_knob_stays_out_of_the_frame_a_second_press_ended_a_drag_in);
    RUN(a_withdrawal_after_an_arm_restores_no_drive_from_before_it);
    RUN(a_press_on_the_dial_ends_a_speed_drag_that_lost_its_release);
    RUN(a_withdrawal_on_a_disarmed_bench_restores_no_drive);
    RUN(a_withdrawal_after_a_stop_restores_no_drive_before_the_disarm);
    RUN(a_withdrawal_on_an_armed_bench_restores_the_drive);
    RUN(the_current_row_shows_the_supply_while_it_is_the_meter);
    RUN(the_current_row_shows_ch1s_window_while_the_ina3221_is_the_meter);
    RUN(a_negative_current_is_shown_signed);
    RUN(a_clipped_window_draws_its_current_in_the_warning_colour);
    RUN(a_change_of_meter_repaints_the_row_the_line_and_the_plot);
    RUN(a_window_is_shown_under_the_meter_of_its_poll);
    RUN(link_loss_clears_the_current_row);
    RUN(the_current_row_repaints_on_a_changed_digit_and_alone);
    RUN(the_plot_keeps_one_point_for_each_window);
    RUN(a_run_reads_the_supply_while_the_meter_changes);
    return test_summary("servo");
}
