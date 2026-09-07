/*
 * What the servo screen decides: where a touch on the dial points the horn,
 * what the travel limit refuses, and the commands it produces.
 *
 * Not what it looks like; that is the golden image's job.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "servo_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H 480

/* Mirrored from servo_screen.c; if the layout moves these move with it. */
#define BODY_H  (480 - UI_BAND_H)
#define SHAFT_X 300
#define SHAFT_Y (BODY_H / 2)
#define ARC_R   140

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;

static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    scr = servo_screen();
    scr->reset();
    servo_cmd_t junk;
    while (servo_screen_take(&junk)) { }
}

static void ev(int x, int y, touch_event_type_t t, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
}
static void tap(int x, int y) { ev(x, y, TOUCH_EVENT_DOWN, 1);
                                ev(x, y, TOUCH_EVENT_UP, 1); }

/* A point on the dial at the given servo angle. */
static void dial_at(float deg, int r, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    *x = SHAFT_X + (int)((float)r * cosf(deg * k) + 0.5f);
    *y = SHAFT_Y - (int)((float)r * sinf(deg * k) + 0.5f);
}

static servo_cmd_t last_cmd(void)
{
    servo_cmd_t c = { SERVO_CMD_NONE, 0 };
    servo_screen_take(&c);
    return c;
}

/* -------------------------------------------------------------------- */

/*
 * The gesture: a touch at 45° on the dial commands 45°, not the pulse width
 * under the finger.
 */
TEST_CASE(a_touch_on_the_dial_points_the_horn_there)
{
    for (int want = -90; want <= 90; want += 45) {
        fresh();
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
    fresh();
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
    fresh();
    for (int i = 0; i < 8; ++i) {
        tap(701, 194 + 13);             /* TRAVEL down, five degrees a tap */
    }
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
    fresh();
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

/* The ARM button, mirrored from servo_screen.c: full width of the right
 * card's inner column, under CENTRE and RELEASE. */
#define ARM_X (508 + 12)
#define ARM_W (800 - 508 - 6 - 24)
#define ARM_Y 388
#define ARM_H 32
static void arm_press(void)   { ev(ARM_X + 40, ARM_Y + 16, TOUCH_EVENT_DOWN, 1); }
static void arm_release(void) { ev(ARM_X + 40, ARM_Y + 16, TOUCH_EVENT_UP, 1); }
static void held(float s_total)
{
    /* The frame rate the panel runs at, so the hold is fed the way it is fed
     * on the bench rather than in one jump. */
    for (int i = 0; i < (int)(s_total * 39.0f + 0.5f); ++i) {
        scr->tick(1.0f / 39.0f);
    }
}

/*
 * The gesture is the same two seconds as MOTOR & ESC's, because it is the
 * same control: a press that is held arms, and a press that is not does
 * nothing.  A servo cannot be driven without it -- the coprocessor writes a
 * pulse of length zero to every PWM pin while the bench is not armed.
 */
TEST_CASE(a_hold_on_arm_asks_to_arm_exactly_once)
{
    fresh();
    arm_press();
    held(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);

    /* It does not repeat under the same finger. */
    servo_cmd_t junk;
    while (servo_screen_take(&junk)) { }
    held(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(a_short_press_on_arm_asks_for_nothing)
{
    fresh();
    arm_press();
    held(UI_HOLD_S / 2.0f);
    arm_release();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(a_finger_that_leaves_arm_arms_nothing)
{
    fresh();
    arm_press();
    held(UI_HOLD_S / 2.0f);
    ev(ARM_X + 40, ARM_Y - 120, TOUCH_EVENT_MOVE, 1);   /* slid off it */
    held(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* Sliding back on does not resume it: the press is over. */
    ev(ARM_X + 40, ARM_Y + 16, TOUCH_EVENT_MOVE, 1);
    held(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(arming_and_disarming_come_from_the_same_button)
{
    fresh();
    arm_press();
    held(UI_HOLD_S + 0.2f);
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
    held(UI_HOLD_S + 0.5f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* And the button is not stranded: the next hold still arms. */
    arm_release();
    arm_press();
    held(UI_HOLD_S + 0.2f);
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

    tap(766, 156 + 13);                 /* trim up, five microseconds */
    CHECK_EQ(servo_screen_commanded(), 1505);
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
    memcpy(first, fb, (size_t)W * H * sizeof(gfx_color_t));
    scr->tick(0.05f);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(first, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(first);
}

int main(void)
{
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
    RUN(leaving_disarms_and_lets_go_of_the_output);
    RUN(trim_shifts_the_pulse_and_not_the_angle);
    RUN(feedback_is_shown_rather_than_travelled_to);
    return test_summary("servo");
}
