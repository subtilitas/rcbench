/*
 * What the servo screen does with the knob and with a drag that loses its
 * release: the knob's turn, its withdrawal, and the drive restored after an
 * arm or a stop.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

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

int main(void)
{
    RUN(the_knob_moves_the_horn_by_how_far_it_turned);
    RUN(the_knob_stops_at_the_servos_travel);
    RUN(the_knob_never_arms_the_servo_bench);
    RUN(a_knob_that_does_not_turn_commands_nothing_on_the_servo);
    RUN(a_finger_on_the_dial_owns_the_horn_against_the_knob);
    RUN(a_finger_that_lifted_in_the_frame_still_owned_the_dial);
    RUN(a_withdrawn_knob_command_restores_the_horn);
    RUN(a_withdrawn_knob_command_leaves_the_servo_idle);
    RUN(a_turn_in_a_frame_where_the_settings_were_open_is_dropped);
    RUN(a_withdrawn_knob_command_leaves_a_position_from_a_touch);
    RUN(a_withdrawal_does_not_touch_a_command_posted_after_the_knob);
    RUN(the_knob_leaves_the_horn_to_the_settings_panel);
    RUN(a_pending_release_is_not_overwritten_by_the_knob);
    RUN(a_completed_arm_is_not_overwritten_by_the_servo_knob);
    RUN(leaving_under_a_dial_drag_ends_the_drag);
    RUN(a_press_ends_a_dial_drag_that_lost_its_release);
    RUN(a_second_press_ends_a_dial_drag);
    RUN(the_knob_stays_out_of_the_frame_a_second_press_ended_a_drag_in);
    RUN(a_withdrawal_after_an_arm_restores_no_drive_from_before_it);
    RUN(a_press_on_the_dial_ends_a_speed_drag_that_lost_its_release);
    RUN(a_withdrawal_on_a_disarmed_bench_restores_no_drive);
    RUN(a_withdrawal_after_a_stop_restores_no_drive_before_the_disarm);
    RUN(a_withdrawal_on_an_armed_bench_restores_the_drive);
    return test_summary("servo_knob");
}
