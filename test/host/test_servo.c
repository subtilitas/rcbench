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
#include "outputs.h"

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
/* The SPEED track, mirrored from the screen: x, w and y of the slider. */
#define SPEED_X_HALF (508 + 12 + (800 - 508 - 6 - 24) / 2)
#define SPEED_Y (296 + 11)
#define TRIM_UP_X (508 + 12 + (800 - 508 - 6 - 24) - 15)
#define TRIM_UP_Y (156 + 13)
#define TYPE_X (508 + 12 + (800 - 508 - 6 - 24) - 75)
#define TYPE_Y (232 + 13)
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
    held(UI_HOLD_S / 4.0f);

    scr->cancel();
    held(UI_HOLD_S * 2.0f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* The contact that was down is gone as far as this screen is concerned,
     * so its release asks for nothing either. */
    arm_release();
    held(UI_HOLD_S);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* And a fresh press still arms. */
    arm_press();
    held(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);
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
    held(UI_HOLD_S / 2.0f);
    scr->render(&cv, 0);
    const gfx_color_t half = arm_px();
    CHECK(red_of(half) > red_of(idle));         /* on its way to red */

    held(UI_HOLD_S);
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
    /* A pixel of the card above the button, on the RANGE row. */
    const size_t probe = (size_t)(ARM_Y - 60) * W + (ARM_X + 12);
    fb[probe] = 0x1234;

    arm_press();
    held(UI_HOLD_S / 2.0f);
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
    fresh();
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
    fresh();
    int x, y;
    dial_at(45.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const servo_cmd_t first = last_cmd();
    CHECK_EQ(first.kind, SERVO_CMD_POSITION);
    CHECK_EQ(first.max_us, 2000);

    tap(TYPE_X, TYPE_Y);                       /* STANDARD -> NARROW 760 */
    const servo_cmd_t again = last_cmd();
    CHECK_EQ(again.kind, SERVO_CMD_POSITION);
    CHECK_EQ(again.max_us, 860);
    CHECK(again.value_us >= again.min_us && again.value_us <= again.max_us);
}

TEST_CASE(the_trim_says_the_position_again_while_it_is_held)
{
    fresh();
    int x, y;
    dial_at(0.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const uint16_t was = last_cmd().value_us;

    tap(TRIM_UP_X, TRIM_UP_Y);
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
    held(UI_HOLD_S / 2.0f);
    servo_screen_cancel_arm();
    held(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

    /* And the button is not stranded: lifting and pressing again arms. */
    arm_release();
    arm_press();
    held(UI_HOLD_S + 0.2f);
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
    held(UI_HOLD_S + 0.2f);
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
    held(UI_HOLD_S + 0.2f);
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
    held(UI_HOLD_S / 2.0f);
    ev(ARM_X + 80, ARM_Y + 16, TOUCH_EVENT_DOWN, 2);   /* contact 2 lands */
    /* The first contact leaves the button, which ends the gesture. */
    ev(ARM_X + 40, ARM_Y - 120, TOUCH_EVENT_MOVE, 1);
    held(UI_HOLD_S + 0.2f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(arming_drops_a_position_held_before_it)
{
    /*
     * Dragging while disarmed commands a position, and arming discards it:
     * the panel drops the held command and the slot so that an arm starts
     * from nothing.  A screen still believing it was driving would say that
     * discarded position again on the next change of type, trim or travel --
     * onto a bench that is armed by then.
     */
    fresh();
    int x, y;
    dial_at(60.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);

    servo_screen_set_armed(true);
    tap(TYPE_X, TYPE_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(a_stop_on_a_bench_that_was_not_armed_still_lets_go)
{
    /*
     * Dragging while disarmed commands a position, and a STOP then changes
     * nothing about the armed state -- there is nothing to disarm -- while
     * the panel releases the slot all the same.  A screen still believing it
     * was driving would say the released position again on the next change
     * of type, trim or travel.
     */
    fresh();
    int x, y;
    dial_at(-40.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);

    servo_screen_cancel_arm();       /* what a stop calls */
    tap(TYPE_X, TYPE_Y);
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
    tap(TYPE_X, TYPE_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(nothing_is_said_again_when_nothing_is_being_held)
{
    /* A screen that is not driving anything commands nothing by having its
     * settings changed. */
    fresh();
    tap(TYPE_X, TYPE_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

/* ------------------------------------------------------- the servo's range */

/*
 * The range travels with the pulse.  A narrow servo runs 660 to 860 us and
 * its centre is below a standard servo's floor, so a command clamped against
 * the wrong pair sends the whole travel to one end.
 */
TEST_CASE(a_command_carries_the_endpoints_of_the_type_it_was_made_for)
{
    fresh();
    int x, y;
    dial_at(0.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    servo_cmd_t got = last_cmd();
    CHECK_EQ(got.min_us, 1000);
    CHECK_EQ(got.max_us, 2000);

    /* TYPE steps to the next servo, and the endpoints follow it. */
    tap(TYPE_X, TYPE_Y);
    tap(x, y);
    got = last_cmd();
    CHECK_EQ(got.min_us, 660);
    CHECK_EQ(got.max_us, 860);
    /* And the pulse it asks for is inside them. */
    CHECK(got.value_us >= got.min_us && got.value_us <= got.max_us);
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
    RUN(the_arm_button_fades_across_the_hold_and_flashes_when_it_lands);
    RUN(the_hold_repaints_the_button_and_leaves_the_card_alone);
    RUN(a_command_carries_the_endpoints_of_the_type_it_was_made_for);
    RUN(the_speed_travels_with_the_command_as_a_rate);
    RUN(changing_the_type_says_the_position_again);
    RUN(the_trim_says_the_position_again_while_it_is_held);
    RUN(nothing_is_said_again_when_nothing_is_being_held);
    RUN(a_stop_abandons_a_hold_that_is_under_way);
    RUN(a_cancelled_hold_leaves_no_arm_to_be_read_later);
    RUN(a_second_contact_cannot_take_over_the_arm_hold);
    RUN(arming_drops_a_position_held_before_it);
    RUN(a_stop_on_a_bench_that_was_not_armed_still_lets_go);
    RUN(a_stop_stops_the_screen_holding_anything);
    RUN(leaving_disarms_and_lets_go_of_the_output);
    RUN(trim_shifts_the_pulse_and_not_the_angle);
    RUN(feedback_is_shown_rather_than_travelled_to);
    RUN(a_cancelled_gesture_does_not_arm);
    return test_summary("servo");
}
