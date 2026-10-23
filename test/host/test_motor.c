/**
 * @file test_motor.c
 * @brief What the bench screen decides: hit regions, the commands it produces, and
 * the two rules that keep a tap from spinning something.
 *
 * This suite holds the ARM hold and its gestures, the throttle track, the knob
 * and the peaks.  The redraws, the throttle at an arm and the TARGET row are
 * in test_motor_*.c.
 *
 * Not what it looks like; that is the golden image's job.
 *
 * Cases:
 * - a_finger_that_leaves_arm_arms_nothing
 * - a_finger_that_comes_back_has_to_be_lifted_first
 * - a_hold_that_stays_put_still_arms_exactly_once
 * - a_press_that_disarmed_the_bench_does_not_then_arm_it
 * - a_press_that_wandered_off_does_not_arm_when_the_bench_disarms
 * - a_throttle_drag_is_not_an_arming_gesture
 * - arming_and_disarming_come_from_the_same_button
 * - a_press_that_slides_off_arm_does_nothing
 * - a_second_contact_cannot_steal_the_disarm_release
 * - a_stop_abandons_a_hold_that_is_under_way
 * - a_cancelled_hold_leaves_no_arm_to_be_read_later
 * - leaving_under_a_held_arm_does_not_strand_the_button
 * - a_second_contact_cannot_take_over_the_arm_hold
 * - a_short_press_on_arm_does_nothing
 * - arming_flashes_the_whole_button
 * - a_pending_disarm_cannot_be_overwritten_by_an_arm
 * - the_disarm_latch_clears_when_it_is_read
 * - the_nudges_step_the_throttle_by_one_point
 * - the_rated_kv_prefers_the_esc_over_the_entered_value
 * - the_throttle_track_moves_by_how_far_it_is_dragged
 * - the_knob_moves_the_throttle_by_how_far_it_turned
 * - the_knob_stops_at_the_ends_of_the_throttle
 * - a_knob_that_does_not_turn_commands_nothing
 * - the_knob_never_arms
 * - a_finger_on_the_throttle_track_owns_it_against_the_knob
 * - a_finger_that_lifted_in_the_frame_still_owned_the_slider
 * - a_touch_off_the_slider_leaves_it_to_the_knob
 * - a_withdrawn_knob_command_restores_the_throttle
 * - a_withdrawn_knob_command_leaves_a_throttle_from_a_touch
 * - a_withdrawal_does_not_touch_a_command_posted_after_the_knob
 * - a_posted_disarm_is_not_overwritten_by_the_knob
 * - a_completed_arm_is_not_overwritten_by_the_knob
 * - a_pending_peak_reset_is_not_overwritten_by_the_knob
 * - the_knob_goes_on_from_zero_after_a_disarm
 * - reset_peaks_posts_its_own_command
 * - a_peak_outlasts_the_reading_that_went
 * - leaving_the_screen_disarms
 * - dragging_the_throttle_leaves_no_stale_pixels
 *
 * SPDX-License-Identifier: MIT
 */

#include "motor_rig.h"
#include "ui_widgets.h"

/* The same on a bench that reports itself armed: the throttle's controls
 * move it only there. */
static void fresh_armed(void)
{
    fresh();
    motor_screen_set_armed(true);
}

#define RESET_X  MO_RESET_X

/* RESET PEAKS, over ARM on the CONTROL card (motor_spots.h). */
#define RESET_Y  MO_RESET_Y

#define TRACK_W  MO_TRACK_W

/* Somewhere well clear of the ARM button: the plot, half the screen away. */
#define OFF_X 300

#define OFF_Y 120

TEST_CASE(a_finger_that_leaves_arm_arms_nothing)
{
    /*
     * The gesture is contact with the control, not with the panel. A press
     * that lands on ARM and slides onto the plot would otherwise arm the
     * bench two seconds later, with the finger nowhere near the button.
     */
    fresh();
    motor_screen_set_armed(false);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 4);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_MOVE, 1);
    tick_for(HOLD_TICKS * 2);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

TEST_CASE(a_finger_that_comes_back_has_to_be_lifted_first)
{
    /*
     * Coming back is not resuming and not restarting: the press is over as
     * far as ARM is concerned. Held here for a full ARM_HOLD_S after
     * returning, which is what tells the two apart -- a shorter hold would
     * pass whether the timer restarted or the press was dead.
     */
    fresh();
    motor_screen_set_armed(false);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 4);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_MOVE, 1);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_MOVE, 1);
    tick_for(HOLD_TICKS * 2);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    /* And a fresh press does arm, so nothing is left stuck. */
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    hold_arm();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
}

TEST_CASE(a_hold_that_stays_put_still_arms_exactly_once)
{
    fresh();
    motor_screen_set_armed(false);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    /* A move that does not leave the button is not a departure. */
    ev(ARM_X - 10, ARM_Y + 2, TOUCH_EVENT_MOVE, 1);
    tick_for(HOLD_TICKS);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
    tick_for(HOLD_TICKS);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
}

TEST_CASE(a_press_that_disarmed_the_bench_does_not_then_arm_it)
{
    /*
     * The bench disarms under a finger that is still down: a failsafe, a
     * STOP, or the IOmcu dropping the outputs. tick() asks only
     * whether a press is held, so a press left standing would start its two
     * seconds the moment armed went false and arm the bench again -- from
     * the very contact the operator made to stop it.
     */
    fresh();
    motor_screen_set_armed(true);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);   /* reads DISARM while armed */
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);   /* on the press */
    tick_for(HOLD_TICKS / 4);

    motor_screen_set_armed(false);           /* the far end disarmed */
    tick_for(HOLD_TICKS * 2);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    /* And the button is not left stuck: the next hold still arms. */
    hold_arm();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
}

TEST_CASE(a_press_that_wandered_off_does_not_arm_when_the_bench_disarms)
{
    /* The same edge with the finger already gone from the button. */
    fresh();
    motor_screen_set_armed(true);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);   /* on the press */
    ev(OFF_X, OFF_Y, TOUCH_EVENT_MOVE, 1);
    motor_screen_set_armed(false);
    tick_for(HOLD_TICKS * 2);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

TEST_CASE(a_throttle_drag_is_not_an_arming_gesture)
{
    /* A drag on the slider never touches the button's press state. */
    fresh();
    motor_screen_set_armed(false);
    ev(120, 300, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 8; ++i) {
        ev(120 + i * 40, 300, TOUCH_EVENT_MOVE, 1);
    }
    tick_for(HOLD_TICKS * 2);
    ev(440, 300, TOUCH_EVENT_UP, 1);
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f };
    bool armed_seen = false;
    while (motor_screen_poll_cmd(&c)) {
        if (c.kind == MOTOR_CMD_ARM) { armed_seen = true; }
    }
    CHECK(!armed_seen);
}

TEST_CASE(arming_and_disarming_come_from_the_same_button)
{
    fresh();
    motor_screen_set_armed(false);
    hold_arm();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);

    /* Disarming stays a press: stopping must not need a hold. */
    motor_screen_set_armed(true);
    tap(ARM_X, ARM_Y);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
}

/* A press that slid off the button is not a tap on it. */
TEST_CASE(a_press_that_slides_off_arm_does_nothing)
{
    fresh();
    motor_screen_set_armed(false);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ev(60, 120, TOUCH_EVENT_MOVE, 1);
    ev(60, 120, TOUCH_EVENT_UP, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

/* DISARM acts on its press, once: no release, the finger's own or a
 * second contact's, disarms again or arms. */
TEST_CASE(a_disarm_acts_on_its_press_once)
{
    fresh();
    motor_screen_set_armed(true);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 2);        /* not ours */
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);        /* ours */
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

/*
 * Arming takes ARM_HOLD_S of contact. A press that lets go early arms
 * nothing, and it does not arm later either: a bench that spins a propeller
 * should not do it on a touch that could have been an elbow.
 */
TEST_CASE(a_stop_abandons_a_hold_that_is_under_way)
{
    /* The bench need not have been armed for a stop to latch, so set_armed()
     * sees no change and the hold would run on and arm from a contact made
     * before the stop. */
    fresh();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    motor_screen_cancel_arm();
    tick_for(HOLD_TICKS + 4);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS + 4);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
}

TEST_CASE(a_cancelled_hold_leaves_no_arm_to_be_read_later)
{
    /* See the servo screen's own: the arm can already be waiting when the
     * stop arrives, and cancelling the hold has to take it with it. */
    fresh();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS + 4);
    motor_screen_cancel_arm();
    motor_cmd_t got;
    CHECK(!motor_screen_poll_cmd(&got));

    /* And after the release, which clears the gesture but not its command. */
    fresh();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS + 4);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    motor_screen_cancel_arm();
    CHECK(!motor_screen_poll_cmd(&got));
}

TEST_CASE(leaving_under_a_held_arm_does_not_strand_the_button)
{
    /*
     * A second contact can navigate away while the first is holding ARM, and
     * no release arrives for that first contact -- the UP belongs to whatever
     * screen is up by then.  A press left recorded would own the button for
     * ever, because the hold stays with the contact that began it.
     */
    fresh();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    scr->leave();

    /* Back on the screen, a fresh hold still arms. */
    (void)motor_screen_poll_cmd(NULL);      /* the disarm leave() posted */
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 2);
    tick_for(HOLD_TICKS + 4);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
}

TEST_CASE(a_second_contact_cannot_take_over_the_arm_hold)
{
    fresh();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    ev(ARM_X + 20, ARM_Y, TOUCH_EVENT_DOWN, 2);   /* a second finger lands */
    /* The contact that began it leaves the button, ending the gesture. */
    ev(ARM_X, ARM_Y - 200, TOUCH_EVENT_MOVE, 1);
    tick_for(HOLD_TICKS + 4);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

TEST_CASE(a_short_press_on_arm_does_nothing)
{
    fresh();
    motor_screen_set_armed(false);

    tap(ARM_X, ARM_Y);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    /* Half of the hold, then let go. */
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    /* Time passing afterwards does not complete it. */
    tick_for(HOLD_TICKS * 2);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    /* And the hold arms exactly once, however long it is held. */
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS * 4);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f };
    int arms = 0;
    while (motor_screen_poll_cmd(&c)) {
        if (c.kind == MOTOR_CMD_ARM) {
            ++arms;
        }
    }
    CHECK_EQ(arms, 1);
}

/*
 * Arming flashes the whole button twice: white, black, the danger red, and
 * again, 25 ms each. A fill that merely decayed towards the settled colour
 * would not read as a flash.
 */
TEST_CASE(arming_flashes_the_whole_button)
{
    fresh();
    scr->render(&cv, 0);
    motor_screen_set_armed(true);

    /* As the panel runs a frame: the tick, then the render.  The tick
     * after the bench's answer spends nothing, so the first frame is white. */
    gfx_color_t seen[7];
    for (int i = 0; i < 7; ++i) {
        scr->tick((float)UI_HOLD_FLASH_PHASE_MS / 1000.0f);
        scr->render(&cv, 0);
        seen[i] = arm_px();
    }

    /* Twice through white, black, red, and then it stays on the red. */
    for (int cycle = 0; cycle < 2; ++cycle) {
        CHECK_EQ(seen[cycle * 3 + 0], GFX_WHITE);
        CHECK_EQ(seen[cycle * 3 + 1], GFX_BLACK);
        CHECK(red_of(seen[cycle * 3 + 2]) > green_of(seen[cycle * 3 + 2]));
    }
    const gfx_color_t settled = seen[6];
    CHECK_EQ(settled, seen[5]);
    CHECK(settled != GFX_WHITE && settled != GFX_BLACK);
    /* Settled is the danger red: more red than green. */
    CHECK(red_of(settled) > green_of(settled));
}

/*
 * Two taps inside a single drain: an ARM landing on top of a DISARM must not
 * re-arm a bench the operator has just watched stop.
 */
TEST_CASE(a_pending_disarm_cannot_be_overwritten_by_an_arm)
{
    fresh();
    motor_screen_set_armed(true);
    tap(ARM_X, ARM_Y);                 /* posts DISARM */
    motor_screen_set_armed(false);
    tap(ARM_X, ARM_Y);                 /* would post ARM */

    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f };
    CHECK(motor_screen_poll_cmd(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_DISARM);
}

/* And it does not become a permanent block: once drained, ARM works. */
TEST_CASE(the_disarm_latch_clears_when_it_is_read)
{
    fresh();
    motor_screen_set_armed(true);
    tap(ARM_X, ARM_Y);
    (void)last_cmd();

    motor_screen_set_armed(false);
    hold_arm();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
}

/*
 * The percentage point at each end of the track.  They step the throttle and
 * post it, and they are the reason ui_slider_set() has to re-anchor a drag:
 * they move the value while a finger may be on the track.
 */
TEST_CASE(the_nudges_step_the_throttle_by_one_point)
{
    fresh_armed();
    motor_screen_set_throttle(50.0f);
    (void)last_cmd();

    tap(UP_X, TRACK_Y);
    CHECK_NEAR(motor_screen_throttle(), 51.0f, 0.01f);
    motor_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
    CHECK_NEAR(c.value, 51.0f, 0.01f);

    tap(DOWN_X, TRACK_Y);
    CHECK_NEAR(motor_screen_throttle(), 50.0f, 0.01f);
    c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
    CHECK_NEAR(c.value, 50.0f, 0.01f);

    /* They clamp rather than wrap at the ends of travel. */
    motor_screen_set_throttle(0.0f);
    (void)last_cmd();
    tap(DOWN_X, TRACK_Y);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);
    motor_screen_set_throttle(100.0f);
    (void)last_cmd();
    tap(UP_X, TRACK_Y);
    CHECK_NEAR(motor_screen_throttle(), 100.0f, 0.01f);
}

/*
 * A drag moves the throttle and nothing else, which is its own repaint path:
 * the readout's box and the slider, without the row's buttons.  The thumb
 * and its shadow stand proud of the track, so a path that clears the track
 * and not ui_slider_painted_rect() leaves a thumb behind at every position
 * the finger passed through.  The other stale-pixel case changes the armed
 * state, which takes the whole row and never exercises this.
 */
TEST_CASE(dragging_the_throttle_leaves_no_stale_pixels)
{
    const size_t bytes = (size_t)W * H * sizeof(gfx_color_t);
    fresh();
    bench_state_t b;
    telemetry_sim_t sim;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);
    for (int i = 0; i < 60; ++i) {
        telemetry_sim_step(&sim, 40.0f, 0.05f, &b);
        motor_screen_push(&b);
    }

    /* Both framebuffers settled, the way the panel leaves them. */
    motor_screen_set_throttle(0.0f);
    scr->render(&cv, 0);
    scr->render(&cv, 1);

    /* The finger travels the whole track, a frame per step. */
    for (int v = 0; v <= 100; v += 5) {
        motor_screen_set_throttle((float)v);
        scr->render(&cv, (v / 5) & 1);
    }
    const int last_buf = (100 / 5) & 1;
    gfx_color_t *dragged = malloc(bytes);
    CHECK(dragged != NULL);
    if (dragged == NULL) {
        return;
    }
    memcpy(dragged, fb, bytes);

    /* The same value, onto a buffer that never saw the drag. */
    memset(fb, 0, bytes);
    motor_invalidate();
    scr->render(&cv, last_buf);

    if (memcmp(dragged, fb, bytes) != 0) {
        long differ = 0;
        for (long i = 0; i < (long)W * H; ++i) {
            if (dragged[i] != fb[i]) {
                ++differ;
            }
        }
        T_FAIL("a drag left %ld px that a fresh render does not draw", differ);
    }
    free(dragged);
}

/*
 * The rated kV comes from the connected ESC when it reports one, and from the
 * setting when it does not.  Neither leaves the field empty: a guessed kV
 * gives a plausible-looking efficiency that is wrong, and the schema
 * therefore defaults to zero rather than to a typical motor.
 */
TEST_CASE(the_rated_kv_prefers_the_esc_over_the_entered_value)
{
    const size_t bytes = (size_t)W * H * sizeof(gfx_color_t);
    gfx_color_t *shot = malloc(bytes);
    CHECK(shot != NULL);
    if (shot == NULL) {
        return;
    }

    fresh();
    settings_reset_all();
    bench_state_t b;
    telemetry_sim_t sim;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);
    for (int i = 0; i < 60; ++i) {
        telemetry_sim_step(&sim, 60.0f, 0.05f, &b);
        motor_screen_push(&b);
    }
    CHECK_EQ(settings_get_int(SET_MOTOR_KV), 0);   /* nothing assumed */
    scr->render(&cv, 0);
    memcpy(shot, fb, bytes);

    /* A value the operator entered fills the field. */
    settings_set(SET_MOTOR_KV, 1000.0f);
    motor_invalidate();
    scr->render(&cv, 0);
    CHECK(memcmp(shot, fb, bytes) != 0);
    memcpy(shot, fb, bytes);

    /* And what the ESC reports takes it back off them. */
    motor_screen_set_esc_kv(2000);
    motor_invalidate();
    scr->render(&cv, 0);
    CHECK(memcmp(shot, fb, bytes) != 0);

    /*
     * Zero from the ESC is "it did not say", not "zero": the field must fall
     * back to the entered value, which means the frame has to become the one
     * the entered value drew and not merely a different frame.
     */
    gfx_color_t *entered = malloc(bytes);
    CHECK(entered != NULL);
    if (entered != NULL) {
        settings_set(SET_MOTOR_KV, 1000.0f);
        motor_screen_set_esc_kv(0);
        motor_invalidate();
        scr->render(&cv, 0);
        memcpy(entered, fb, bytes);

        motor_screen_set_esc_kv(2000);
        motor_invalidate();
        scr->render(&cv, 0);
        CHECK(memcmp(entered, fb, bytes) != 0);

        motor_screen_set_esc_kv(0);
        motor_invalidate();
        scr->render(&cv, 0);
        CHECK_EQ(memcmp(entered, fb, bytes), 0);
        free(entered);
    }

    settings_reset_all();
    free(shot);
}

/*
 * The throttle moves by how far a finger travels, not to where it lands.
 *
 * A press used to command the value under it, so touching the right-hand end
 * of the track asked for full travel in one contact.  Nothing bounds how fast
 * the motor follows that: a throttle channel bound on the IOmcu carries
 * no slew, so the command went to 100 and the next pass rendered it, with
 * nothing to confirm.
 */
TEST_CASE(the_throttle_track_moves_by_how_far_it_is_dragged)
{
    fresh_armed();

    /* A press commands nothing, wherever on the track it lands. */
    ev(TRACK_X + TRACK_W - 4, TRACK_Y, TOUCH_EVENT_DOWN, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);
    ev(TRACK_X + TRACK_W - 4, TRACK_Y, TOUCH_EVENT_UP, 1);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);

    /* Half the track's width of drag is half the travel, wherever it began. */
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 10 + TRACK_W / 2, TRACK_Y, TOUCH_EVENT_MOVE, 1);
    const motor_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
    CHECK(c.value > 45.0f && c.value < 55.0f);
    CHECK_NEAR(motor_screen_throttle(), c.value, 0.01f);

    /* And dragging back the same distance returns it. */
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_MOVE, 1);
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_UP, 1);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);
}

/*
 * The rotary knob moves the throttle by how far it turns, with the rules
 * the touch slider has: it never arms, it stops at the ends, and it does
 * not fight a finger.
 */
TEST_CASE(the_knob_moves_the_throttle_by_how_far_it_turned)
{
    fresh_armed();
    motor_screen_knob(0.25f);
    CHECK_NEAR(motor_screen_throttle(), 25.0f, 0.01f);
    motor_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
    CHECK_NEAR(c.value, 25.0f, 0.01f);

    motor_screen_knob(0.25f);
    CHECK_NEAR(motor_screen_throttle(), 50.0f, 0.01f);
    motor_screen_knob(-0.1f);
    CHECK_NEAR(motor_screen_throttle(), 40.0f, 0.01f);
    c = last_cmd();
    CHECK_NEAR(c.value, 40.0f, 0.01f);

    /* A step finer than a percentage point still counts. */
    motor_screen_knob(0.001f);
    CHECK_NEAR(motor_screen_throttle(), 40.1f, 0.01f);
}

TEST_CASE(the_knob_stops_at_the_ends_of_the_throttle)
{
    fresh_armed();
    motor_screen_knob(5.0f);
    CHECK_NEAR(motor_screen_throttle(), 100.0f, 0.01f);
    CHECK_NEAR(last_cmd().value, 100.0f, 0.01f);
    motor_screen_knob(-5.0f);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);
    CHECK_NEAR(last_cmd().value, 0.0f, 0.01f);
    /* Already at the end: nothing to say. */
    motor_screen_knob(-0.2f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

TEST_CASE(a_knob_that_does_not_turn_commands_nothing)
{
    fresh();
    motor_screen_knob(0.0f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.0f);
}

/* On a disarmed bench the knob posts nothing at all, an arm least of all,
 * and on an armed one nothing but throttles. */
TEST_CASE(the_knob_never_arms)
{
    fresh();
    for (int i = 0; i < 40; ++i) {
        motor_screen_knob(0.05f);
        CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    }
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    motor_screen_set_armed(true);
    for (int i = 0; i < 40; ++i) {
        motor_screen_knob(0.05f);
        const motor_cmd_t c = last_cmd();
        CHECK(c.kind == MOTOR_CMD_THROTTLE || c.kind == MOTOR_CMD_NONE);
    }
    CHECK(motor_screen_throttle() > 99.0f);
}

TEST_CASE(a_finger_on_the_throttle_track_owns_it_against_the_knob)
{
    fresh_armed();
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_DOWN, 1);
    motor_screen_knob(0.5f);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_UP, 1);
    motor_screen_knob_frame();          /* the next frame */
    motor_screen_knob(0.5f);
    CHECK_NEAR(motor_screen_throttle(), 50.0f, 0.01f);
}

/* A finger that went down and came up inside the frame owned the slider in
 * it: the delta the knob collected in that frame is dropped, whether or not
 * the finger is still down when it is applied. */
TEST_CASE(a_finger_that_lifted_in_the_frame_still_owned_the_slider)
{
    fresh_armed();
    motor_screen_knob_frame();
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_UP, 1);
    (void)last_cmd();
    const float held = motor_screen_throttle();
    motor_screen_knob(0.5f);
    CHECK_NEAR(motor_screen_throttle(), held, 0.01f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    /* A drag that began in an earlier frame and ends in this one. */
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_DOWN, 1);
    motor_screen_knob_frame();
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_UP, 1);
    (void)last_cmd();
    const float held2 = motor_screen_throttle();
    motor_screen_knob(0.5f);
    CHECK_NEAR(motor_screen_throttle(), held2, 0.01f);

    /* The next frame is free again. */
    motor_screen_knob_frame();
    motor_screen_knob(0.1f);
    CHECK_NEAR(motor_screen_throttle(), held2 + 10.0f, 0.01f);
}

/* A touch elsewhere in the frame does not own the slider. */
TEST_CASE(a_touch_off_the_slider_leaves_it_to_the_knob)
{
    fresh_armed();
    motor_screen_knob_frame();
    ev(5, 5, TOUCH_EVENT_DOWN, 1);
    ev(5, 5, TOUCH_EVENT_UP, 1);
    (void)last_cmd();
    motor_screen_knob(0.1f);
    CHECK_NEAR(motor_screen_throttle(), 10.0f, 0.01f);
}

/* Withdrawing the knob's waiting command puts the slider back and sends
 * nothing; a throttle pending before the knob moved it stays pending. */
TEST_CASE(a_withdrawn_knob_command_restores_the_throttle)
{
    fresh_armed();
    motor_screen_knob(0.25f);
    motor_screen_knob(0.25f);
    motor_screen_knob_cancel();
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    /* Nothing waiting: nothing changes. */
    motor_screen_knob(0.25f);
    (void)last_cmd();                   /* taken */
    motor_screen_knob_cancel();
    CHECK_NEAR(motor_screen_throttle(), 25.0f, 0.01f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

TEST_CASE(a_withdrawn_knob_command_leaves_a_throttle_from_a_touch)
{
    fresh_armed();
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 10 + TRACK_W / 4, TRACK_Y, TOUCH_EVENT_MOVE, 1);
    ev(TRACK_X + 10 + TRACK_W / 4, TRACK_Y, TOUCH_EVENT_UP, 1);
    const float touched = motor_screen_throttle();
    CHECK(touched > 20.0f);
    motor_screen_knob_frame();
    motor_screen_knob(0.1f);
    motor_screen_knob_cancel();
    CHECK_NEAR(motor_screen_throttle(), touched, 0.01f);
    const motor_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
    CHECK_NEAR(c.value, touched, 0.01f);
}

TEST_CASE(a_withdrawal_does_not_touch_a_command_posted_after_the_knob)
{
    fresh_armed();
    motor_screen_knob(0.25f);
    scr->leave();                       /* posts the DISARM over it */
    motor_screen_knob_cancel();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
}

TEST_CASE(a_posted_disarm_is_not_overwritten_by_the_knob)
{
    fresh_armed();
    scr->leave();                       /* posts the DISARM */
    motor_screen_knob(0.3f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
}

/* An arm that completed in the frame's tick waits for the application's next
 * flush; a knob delta in between drops and leaves the arm in the slot. */
TEST_CASE(a_completed_arm_is_not_overwritten_by_the_knob)
{
    fresh();
    hold_arm();
    motor_screen_knob(0.3f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);
    /* Nor is the delta kept for the armed bench: the next turn is its own. */
    motor_screen_set_armed(true);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);
    motor_screen_knob(0.3f);
    CHECK_NEAR(motor_screen_throttle(), 30.0f, 0.01f);
}

TEST_CASE(a_pending_peak_reset_is_not_overwritten_by_the_knob)
{
    fresh_armed();
    tap(RESET_X, RESET_Y);
    motor_screen_knob(0.3f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_RESET_PEAKS);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);
}

/* A disarm returns the throttle to zero, and the knob goes on from there. */
TEST_CASE(the_knob_goes_on_from_zero_after_a_disarm)
{
    fresh_armed();
    motor_screen_knob(0.6f);
    (void)last_cmd();
    motor_screen_set_armed(false);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    motor_screen_knob(0.1f);            /* refused: not armed */
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    motor_screen_set_armed(true);
    motor_screen_knob(0.1f);
    CHECK_NEAR(motor_screen_throttle(), 10.0f, 0.01f);
}

TEST_CASE(reset_peaks_posts_its_own_command)
{
    fresh();
    tap(RESET_X, RESET_Y);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_RESET_PEAKS);
}

/*
 * A source that stops answering mid-run empties the live readings and
 * keeps the run's peaks it measured: the peaks stay drawn.  They go with
 * the next arm and with RESET PEAKS, until a valid reading comes again.
 */
TEST_CASE(a_peak_outlasts_the_reading_that_went)
{
    fresh();
    motor_screen_set_armed(true);
    for (int i = 0; i < 4; ++i) {
        CHECK(!motor_screen_peak_shown(i));
    }
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.valid = true;
    b.flags = (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                         | LINK_BN_SENSED);
    b.voltage = 24.0f;
    b.current = 30.0f;
    b.power = 720.0f;
    b.voltage_min = 23.1f;
    b.current_max = 41.0f;
    b.power_max = 960.0f;
    motor_screen_push(&b);
    CHECK(motor_screen_peak_shown(0));
    CHECK(motor_screen_peak_shown(1));
    CHECK(motor_screen_peak_shown(2));
    CHECK(!motor_screen_peak_shown(3));      /* no rpm answered */

    /* The INA228 stops answering: the live flags go, the peaks stay. */
    b.flags = (uint16_t)LINK_BN_SENSED;
    b.voltage = 0.0f;
    b.current = 0.0f;
    b.power = 0.0f;
    motor_screen_push(&b);
    CHECK(motor_screen_peak_shown(0));
    CHECK(motor_screen_peak_shown(1));
    CHECK(motor_screen_peak_shown(2));
    scr->render(&cv, 0);                       /* the PLOT pane */
    tap(6 + 93, 36 + 9);                       /* TABLE */
    scr->render(&cv, 1);

    tap(RESET_X, RESET_Y);
    CHECK(!motor_screen_peak_shown(1));
    motor_screen_push(&b);
    CHECK(!motor_screen_peak_shown(1));
    b.flags |= (uint16_t)LINK_BN_CURRENT_OK;
    motor_screen_push(&b);
    CHECK(motor_screen_peak_shown(1));

    /* A new run starts without them. */
    motor_screen_set_armed(false);
    CHECK(motor_screen_peak_shown(1));         /* the last run's, held */
    motor_screen_set_armed(true);
    CHECK(!motor_screen_peak_shown(1));
    CHECK(!motor_screen_peak_shown(-1));
    CHECK(!motor_screen_peak_shown(4));
}

/*
 * Leaving disarms.  Navigating away from an armed bench must not leave a
 * propeller spinning behind a screen that does not show it, and the command
 * carries the disarm rather than the application inferring it from the
 * navigation.
 */
TEST_CASE(leaving_the_screen_disarms)
{
    fresh();
    motor_screen_set_armed(true);
    scr->leave();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
}

int main(void)
{
    RUN(a_finger_that_leaves_arm_arms_nothing);
    RUN(a_finger_that_comes_back_has_to_be_lifted_first);
    RUN(a_hold_that_stays_put_still_arms_exactly_once);
    RUN(a_press_that_disarmed_the_bench_does_not_then_arm_it);
    RUN(a_press_that_wandered_off_does_not_arm_when_the_bench_disarms);
    RUN(a_throttle_drag_is_not_an_arming_gesture);
    RUN(arming_and_disarming_come_from_the_same_button);
    RUN(a_press_that_slides_off_arm_does_nothing);
    RUN(a_disarm_acts_on_its_press_once);
    RUN(a_stop_abandons_a_hold_that_is_under_way);
    RUN(a_cancelled_hold_leaves_no_arm_to_be_read_later);
    RUN(leaving_under_a_held_arm_does_not_strand_the_button);
    RUN(a_second_contact_cannot_take_over_the_arm_hold);
    RUN(a_short_press_on_arm_does_nothing);
    RUN(arming_flashes_the_whole_button);
    RUN(a_pending_disarm_cannot_be_overwritten_by_an_arm);
    RUN(the_disarm_latch_clears_when_it_is_read);
    RUN(the_nudges_step_the_throttle_by_one_point);
    RUN(the_rated_kv_prefers_the_esc_over_the_entered_value);
    RUN(the_throttle_track_moves_by_how_far_it_is_dragged);
    RUN(the_knob_moves_the_throttle_by_how_far_it_turned);
    RUN(the_knob_stops_at_the_ends_of_the_throttle);
    RUN(a_knob_that_does_not_turn_commands_nothing);
    RUN(the_knob_never_arms);
    RUN(a_finger_on_the_throttle_track_owns_it_against_the_knob);
    RUN(a_finger_that_lifted_in_the_frame_still_owned_the_slider);
    RUN(a_touch_off_the_slider_leaves_it_to_the_knob);
    RUN(a_withdrawn_knob_command_restores_the_throttle);
    RUN(a_withdrawn_knob_command_leaves_a_throttle_from_a_touch);
    RUN(a_withdrawal_does_not_touch_a_command_posted_after_the_knob);
    RUN(a_posted_disarm_is_not_overwritten_by_the_knob);
    RUN(a_completed_arm_is_not_overwritten_by_the_knob);
    RUN(a_pending_peak_reset_is_not_overwritten_by_the_knob);
    RUN(the_knob_goes_on_from_zero_after_a_disarm);
    RUN(reset_peaks_posts_its_own_command);
    RUN(a_peak_outlasts_the_reading_that_went);
    RUN(leaving_the_screen_disarms);
    RUN(dragging_the_throttle_leaves_no_stale_pixels);
    return test_summary("motor");
}
