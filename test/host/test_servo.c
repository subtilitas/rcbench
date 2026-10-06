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
#include "servo_sweep.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "link_pages.h"
#include "outputs.h"
#include "outputs_pages.h"

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
    settings_set_store(NULL);
    settings_init();
    supply_screen()->reset();
    supply_screen_settings_loaded();
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

/*
 * The SETTINGS overlay, mirrored from servo_screen.c: the button at the top
 * of the right card, then over the left card its tabs, CLOSE, the rows of a
 * page (left column at x 16, right at x 255, 42 px apart from y 54), the
 * list a row opens (two columns of five, 46 px apart from y 54) and the
 * warning's HOLD TO APPLY and CANCEL.
 */
#define SETB_X   734
#define SETB_Y   24
#define CLOSE_X  439
#define CLOSE_Y  27
#define TAB_X(i) (56 + 80 * (i))
#define TAB_Y    27
#define ROW_L_X  100
#define ROW_R_X  330
#define ROW_Y(r) (71 + 42 * (r))
#define TRIM_DN_X 399
#define TRIM_UP_X 469
#define CH_X(i)  (130 + 239 * ((i) / 5))
#define CH_Y(i)  (74 + 46 * ((i) % 5))
#define CH_CANCEL_X 429
#define CH_CANCEL_Y 398
#define WARN_APPLY_X  146
#define WARN_CANCEL_X 384
#define WARN_Y        378

static void open_settings(void)  { tap(SETB_X, SETB_Y); }
static void close_settings(void) { tap(CLOSE_X, CLOSE_Y); }

/* Hold the warning's HOLD TO APPLY for @p s seconds and let go. */
static void hold_apply(float secs)
{
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < (int)(secs * 39.0f + 0.5f); ++i) {
        scr->tick(1.0f / 39.0f);
    }
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
}

/* A type chosen from the list on OUTPUT: STANDARD PWM 0, NARROW 760 1,
 * WIDE 2, HELI CYCLIC 3, HELI TAIL 760 4.  A heli type is held through its
 * warning.  The overlay is closed again after. */
static void choose_type(int t)
{
    open_settings();
    tap(ROW_L_X, ROW_Y(0));
    tap(CH_X(t), CH_Y(t));
    if (t >= 3) {
        hold_apply(2.3f);
    }
    close_settings();
}

/* A key of the keypad, where the screen opens it over the left card. */
static void key(ui_key_t k)
{
    ui_keypad_t probe;
    memset(&probe, 0, sizeof(probe));
    ui_keypad_open(&probe, (gfx_rect_t){ 6, 6, 488, 420 }, "", "", 0.0f,
                   0.0f, 1.0f, 2);
    const gfx_rect_t r = ui_keypad_key_rect(&probe, k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

static void keys(const char *text)
{
    static const ui_key_t digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    for (const char *p = text; *p != '\0'; ++p) {
        key((*p == '.') ? UI_KEY_DOT : digit[*p - '0']);
    }
    key(UI_KEY_OK);
}

/* A point on the dial at the given servo angle. */
static void dial_at(float deg, int r, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    *x = SHAFT_X + (int)((float)r * cosf(deg * k) + 0.5f);
    *y = SHAFT_Y - (int)((float)r * sinf(deg * k) + 0.5f);
}

static servo_cmd_t last_cmd(void)
{
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
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

    choose_type(1);                            /* STANDARD -> NARROW 760 */
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
     * onto a bench that is armed by then.  What the change says is the rest,
     * under the new type.
     */
    fresh();
    int x, y;
    dial_at(60.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);

    servo_screen_set_armed(true);
    choose_type(1);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
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
    held(UI_HOLD_S + 0.1f);
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
    held(UI_HOLD_S + 0.1f);
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
    fresh();
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

/* ------------------------------------------------ profiles and the warning */

TEST_CASE(a_heli_type_waits_for_the_warning_held_two_seconds)
{
    fresh();
    open_settings();
    tap(ROW_L_X, ROW_Y(0));                    /* TYPE */
    tap(CH_X(3), CH_Y(3));                     /* HELI CYCLIC */
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    hold_apply(0.5f);                          /* not long enough */
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    hold_apply(2.3f);
    CHECK_STR_EQ(servo_screen_type_name(), "HELI CYCLIC");
    CHECK_EQ(servo_screen_frame_hz(), 333);

    /* The command carries the profile: its endpoints and its rate. */
    close_settings();
    int x, y;
    dial_at(0.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.min_us, 820);
    CHECK_EQ(c.max_us, 2220);
    CHECK_EQ(c.frame_hz, 333);
    CHECK_EQ(c.value_us, 1520);
}

/*
 * Every profile's endpoints are a range the coprocessor takes: one it
 * refused would leave the panel driving a range the profile never named.
 * HELI TAIL 760's end of travel, 410 us, is the narrowest of them.
 */
TEST_CASE(every_profile_is_a_range_the_coprocessor_takes)
{
    for (int t = 0; t < 5; ++t) {
        fresh();
        choose_type(t);
        servo_screen_set_commanded(-90.0f);
        const servo_cmd_t c = last_cmd();
        CHECK(c.min_us >= LINK_CC_FLOOR_US);
        CHECK(c.max_us <= LINK_CC_CEILING_US);
        CHECK_EQ(c.value_us, c.min_us);
        uint16_t regs[LINK_CC_COUNT];
        outputs_chan_cfg_defaults(regs);
        const uint16_t in[LINK_CC_STRIDE] = {
            [LINK_CC_ROLE]   = LINK_CC_ROLE_SURFACE,
            [LINK_CC_SLEW]   = 0u,
            [LINK_CC_MIN_US] = c.min_us,
            [LINK_CC_MAX_US] = c.max_us,
        };
        CHECK_EQ(outputs_chan_cfg_write(regs, 0, LINK_CC_STRIDE, in), 0u);
    }
    CHECK_STR_EQ(servo_screen_type_name(), "HELI TAIL 760");
    servo_screen_set_commanded(-90.0f);
    CHECK_EQ(last_cmd().value_us, 410);
}

/* And a pulse width typed outside that range is refused at the keypad. */
TEST_CASE(a_pulse_width_the_coprocessor_would_refuse_is_not_taken)
{
    fresh();
    open_settings();
    tap(ROW_L_X, ROW_Y(2));                    /* PULSE MIN */
    keys("350");
    key(UI_KEY_CANCEL);
    tap(ROW_L_X, ROW_Y(3));                    /* PULSE MAX */
    keys("2600");
    key(UI_KEY_CANCEL);
    /* A centre further from one end than the ceiling allows the other:
     * 1800 us with MIN at 1000 would carry 1000..2600. */
    tap(ROW_R_X, ROW_Y(2));                    /* PULSE CENTRE */
    keys("1800");
    key(UI_KEY_CANCEL);
    close_settings();
    servo_screen_set_commanded(0.0f);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.value_us, 1500);
    CHECK_EQ(c.min_us, 1000);
    CHECK_EQ(c.max_us, 2000);
}

/*
 * The range a command carries reaches past PULSE MAX when CENTRE is off the
 * middle, and the far end can render its top between two transactions: the
 * frame rate's pause is kept from that top.  At HELI TAIL 760's 560 Hz,
 * 1285 us is the longest pulse with 0.5 ms after it, so CENTRE may not move
 * the range past it; at 50 Hz a centre of 1700 us carries 1000..2400 us and
 * leaves 294 Hz as the fastest rate.
 */
TEST_CASE(the_pause_is_kept_from_the_top_of_the_range_a_command_carries)
{
    fresh();
    choose_type(4);                            /* HELI TAIL 760, 560 Hz */
    open_settings();
    tap(ROW_R_X, ROW_Y(2));                    /* PULSE CENTRE */
    keys("1060");                              /* would carry 410..1710 */
    key(UI_KEY_CANCEL);
    close_settings();
    servo_screen_set_commanded(90.0f);
    servo_cmd_t c = last_cmd();
    CHECK_EQ(c.max_us, 1110);                  /* refused: still 760 */
    open_settings();
    tap(ROW_R_X, ROW_Y(2));
    keys("840");                               /* 410..1270 fits */
    close_settings();
    servo_screen_set_commanded(90.0f);
    c = last_cmd();
    CHECK_EQ(c.min_us, 410);
    CHECK_EQ(c.max_us, 1270);
    CHECK_EQ(c.frame_hz, 560);

    fresh();
    open_settings();
    tap(ROW_R_X, ROW_Y(2));                    /* PULSE CENTRE */
    keys("1700");                              /* 1000..2400 */
    tap(ROW_L_X, ROW_Y(1));                    /* FRAME RATE */
    tap(CH_X(6), CH_Y(6));                     /* CUSTOM, after 250 Hz */
    keys("333");                               /* past 294 Hz: refused */
    key(UI_KEY_CANCEL);
    CHECK_EQ(servo_screen_frame_hz(), 50);
    tap(ROW_L_X, ROW_Y(1));
    tap(CH_X(6), CH_Y(6));
    keys("290");                               /* fits; past 60 Hz: warned */
    hold_apply(2.3f);
    close_settings();
    CHECK_EQ(servo_screen_frame_hz(), 290);
    servo_screen_set_commanded(0.0f);
    c = last_cmd();
    CHECK_EQ(c.max_us, 2400);
    CHECK_EQ(c.value_us, 1700);
}

/*
 * MIN, CENTRE and MAX are set one by one, so each side of centre runs to
 * its own end: -90 deg is MIN and +90 deg is MAX.  The range a command
 * carries is centred on CENTRE, because the far end rests a surface at its
 * midpoint -- on RELEASE and before an arm -- and that rest is the centre.
 */
TEST_CASE(each_side_of_centre_runs_to_its_own_end)
{
    fresh();
    open_settings();
    tap(ROW_R_X, ROW_Y(2));                    /* PULSE CENTRE */
    keys("1520");
    tap(ROW_L_X, ROW_Y(2));                    /* PULSE MIN */
    keys("900");
    close_settings();
    servo_screen_set_commanded(-90.0f);
    CHECK_EQ(last_cmd().value_us, 900);
    servo_screen_set_commanded(-45.0f);
    CHECK_EQ(last_cmd().value_us, 1210);
    servo_screen_set_commanded(45.0f);
    CHECK_EQ(last_cmd().value_us, 1760);
    servo_screen_set_commanded(90.0f);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.value_us, 2000);
    CHECK_EQ(c.min_us, 900);
    CHECK_EQ(c.max_us, 2140);                  /* 1520 +/- 620 */
    tap(ARM_X + ARM_W - 40, 350 + 16);         /* RELEASE */
    const servo_cmd_t r = last_cmd();
    CHECK_EQ(r.kind, SERVO_CMD_RELEASE);
    CHECK_EQ(r.min_us, 900);
    CHECK_EQ(r.max_us, 2140);
}

TEST_CASE(a_warning_cancelled_dropped_or_left_applies_nothing)
{
    fresh();
    open_settings();
    tap(ROW_L_X, ROW_Y(0));
    tap(CH_X(4), CH_Y(4));                     /* HELI TAIL 760 */
    tap(WARN_CANCEL_X, WARN_Y);
    for (int i = 0; i < 120; ++i) {
        scr->tick(1.0f / 39.0f);
    }
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");

    /* A hold the touch stream lost does not complete on its own. */
    tap(ROW_L_X, ROW_Y(0));
    tap(CH_X(4), CH_Y(4));
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(1.0f / 39.0f);
    }
    scr->cancel();
    for (int i = 0; i < 120; ++i) {
        scr->tick(1.0f / 39.0f);
    }
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");

    /* A finger that slides off APPLY abandons it. */
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    ev(WARN_CANCEL_X, WARN_Y, TOUCH_EVENT_MOVE, 1);
    for (int i = 0; i < 120; ++i) {
        scr->tick(1.0f / 39.0f);
    }
    ev(WARN_CANCEL_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");

    /* Leaving the screen with the warning open drops it. */
    scr->leave();
    for (int i = 0; i < 120; ++i) {
        scr->tick(1.0f / 39.0f);
    }
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
}

TEST_CASE(a_rate_above_60_hz_needs_the_warning_and_60_does_not)
{
    fresh();
    open_settings();
    tap(ROW_L_X, ROW_Y(1));                    /* FRAME RATE */
    tap(CH_X(1), CH_Y(1));                     /* 60 Hz */
    CHECK_EQ(servo_screen_frame_hz(), 60);
    tap(ROW_L_X, ROW_Y(1));
    tap(CH_X(2), CH_Y(2));                     /* 100 Hz: asked first */
    CHECK_EQ(servo_screen_frame_hz(), 60);
    hold_apply(2.3f);
    CHECK_EQ(servo_screen_frame_hz(), 100);
}

TEST_CASE(standard_pwm_keeps_a_millisecond_between_pulses)
{
    /* 1 / (2000 us + 1 ms) is 333 Hz: the list stops there, a custom rate
     * above it is refused, and at 333 Hz the longest pulse is 2003 us. */
    fresh();
    open_settings();
    tap(ROW_L_X, ROW_Y(1));
    tap(CH_X(7), CH_Y(7));                     /* 333 Hz, the last */
    hold_apply(2.3f);
    CHECK_EQ(servo_screen_frame_hz(), 333);
    tap(ROW_L_X, ROW_Y(3));                    /* PULSE MAX */
    keys("2100");                              /* refused: no pause left */
    key(UI_KEY_CANCEL);
    tap(ROW_L_X, ROW_Y(3));
    keys("2003");
    int x, y;
    close_settings();
    dial_at(90.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    CHECK_EQ(last_cmd().max_us, 2003);

    /* WIDE's 2200 us leaves 312 Hz: CUSTOM refuses 320, takes 300. */
    fresh();
    choose_type(2);
    open_settings();
    tap(ROW_L_X, ROW_Y(1));
    tap(CH_X(7), CH_Y(7));                     /* CUSTOM: 7 rates fit 312 Hz */
    keys("320");
    key(UI_KEY_CANCEL);
    CHECK_EQ(servo_screen_frame_hz(), 50);
    tap(ROW_L_X, ROW_Y(1));
    tap(CH_X(7), CH_Y(7));                     /* CUSTOM */
    keys("55");                                /* 60 Hz or under: no warning */
    CHECK_EQ(servo_screen_frame_hz(), 55);
}

TEST_CASE(a_restart_is_standard_pwm_at_50_hz)
{
    fresh();
    choose_type(4);
    CHECK_STR_EQ(servo_screen_type_name(), "HELI TAIL 760");
    CHECK_EQ(servo_screen_frame_hz(), 560);
    scr->reset();                              /* what a restart does */
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    CHECK_EQ(servo_screen_frame_hz(), 50);
}

TEST_CASE(reverse_and_the_pulse_widths_reshape_the_command)
{
    fresh();
    int x, y;
    dial_at(45.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const uint16_t ahead = last_cmd().value_us;
    CHECK(ahead > 1500);
    open_settings();
    tap(ROW_R_X, ROW_Y(4));                    /* REVERSE */
    CHECK(last_cmd().value_us < 1500);         /* said again, mirrored */

    fresh();
    open_settings();
    tap(ROW_R_X, ROW_Y(2));                    /* PULSE CENTRE */
    keys("1520");
    tap(ROW_L_X, ROW_Y(2));                    /* PULSE MIN */
    keys("900");
    close_settings();
    dial_at(0.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.value_us, 1520);
    CHECK_EQ(c.min_us, 900);
}

TEST_CASE(the_test_and_limit_settings_are_kept)
{
    fresh();
    open_settings();
    tap(TAB_X(1), TAB_Y);                      /* TEST */
    tap(ROW_R_X, ROW_Y(0));                    /* STEP 4.8 V: a switch */
    CHECK(!settings_get_bool(SET_SERVO_STEP_48));
    tap(ROW_L_X, ROW_Y(0));                    /* CURVE: a list */
    tap(CH_X(2), CH_Y(2));                     /* TRIANGLE */
    CHECK_EQ(settings_get_int(SET_SERVO_CURVE), 2);
    tap(ROW_L_X, ROW_Y(1));                    /* SPEED: the keypad */
    keys("1.25");
    CHECK_NEAR(settings_get(SET_SERVO_TEST_HZ), 1.25f, 1e-4f);
    CHECK(settings_save_asked());

    tap(TAB_X(2), TAB_Y);                      /* LIMITS */
    tap(ROW_L_X, ROW_Y(0));                    /* VOLTAGE MAX: SUPPLY's cap */
    keys("8.41");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 8.40f, 1e-4f);
    CHECK_NEAR(supply_screen_limits().v_max, 8.40f, 1e-4f);
    tap(ROW_R_X, ROW_Y(0));                    /* IDLE CURRENT */
    keys("0.3");
    CHECK_NEAR(settings_get(SET_SERVO_IDLE_MAX), 0.3f, 1e-4f);

    tap(TAB_X(3), TAB_Y);                      /* DUT */
    tap(ROW_L_X, ROW_Y(0));                    /* NAME: the keyboard */
    ui_textkey_t probe;
    memset(&probe, 0, sizeof(probe));
    ui_textkey_open(&probe, (gfx_rect_t){ 6, 6, 488, 420 }, "", "", 23);
    const int seq[] = { UI_TK_CLR, 31 /* X */, 15 /* Y */, 0 /* 1 */,
                        UI_TK_OK };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); ++i) {
        const gfx_rect_t r = ui_textkey_key_rect(&probe, seq[i]);
        tap(r.x + r.w / 2, r.y + r.h / 2);
    }
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "XY1");
    tap(ROW_L_X, ROW_Y(1));                    /* REPORT */
    CHECK(!settings_get_bool(SET_SERVO_REPORT));
}

TEST_CASE(the_overlay_leaves_the_right_card_working)
{
    /* ARM, CENTRE and RELEASE work under the overlay; the dial under it
     * does not. */
    fresh();
    open_settings();
    tap(ARM_X + 40, 350 + 16);                 /* CENTRE */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_CENTRE);
    int x, y;
    dial_at(45.0f, ARC_R - 20, &x, &y);
    tap(x, y);                                 /* on the overlay, not the dial */
    CHECK(servo_screen_commanded() == 1500);
    arm_press();
    held(2.2f);
    arm_release();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);
}

TEST_CASE(the_tag_is_red_while_a_dangerous_profile_is_in_force)
{
    fresh();
    scr->render(&cv, 0);
    const gfx_color_t calm = fb[130 * W + 520];
    CHECK(calm != ui_theme_color(UI_C_DANGER));
    choose_type(3);
    scr->render(&cv, 0);
    CHECK_EQ(fb[130 * W + 520], ui_theme_color(UI_C_DANGER));
}

TEST_CASE(drags_and_samples_leave_both_buffers_as_a_full_redraw_would)
{
    /* The horn's drag repaints the right card without its plot, and the
     * plot repaints itself on its own samples; through any mix of the two,
     * each buffer ends as one drawn whole. */
    fresh();
    gfx_color_t *fb1 = calloc((size_t)W * H, sizeof(gfx_color_t));
    gfx_color_t *ref = calloc((size_t)W * H, sizeof(gfx_color_t));
    gfx_canvas_t cv1, cref;
    gfx_canvas_init(&cv1, fb1, W, H, W);
    gfx_canvas_init(&cref, ref, W, H, W);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.v = 6.0f;
    for (int step = 0; step < 24; ++step) {
        st.i = 0.1f * (float)(step % 7);
        st.p = st.v * st.i;
        if (step % 3 != 1) {
            servo_screen_supply(&st);
        }
        if (step % 2 == 0) {
            int x, y;
            dial_at((float)(step * 5 - 60), ARC_R - 20, &x, &y);
            tap(x, y);
        }
        scr->render((step % 2) ? &cv1 : &cv, step % 2);
    }
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    CHECK_EQ(memcmp(fb, fb1, (size_t)W * H * sizeof(gfx_color_t)), 0);
    servo_invalidate();
    scr->render(&cref, 0);
    CHECK_EQ(memcmp(fb, ref, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(fb1);
    free(ref);
}

TEST_CASE(a_supply_sample_repaints_only_the_power_plot)
{
    fresh();
    scr->render(&cv, 0);
    gfx_color_t *was = malloc((size_t)W * H * sizeof(gfx_color_t));
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    for (int i = 0; i < 30; ++i) {
        st.v = 6.0f; st.i = 0.2f + 0.02f * (float)i; st.p = st.v * st.i;
        servo_screen_supply(&st);
    }
    scr->render(&cv, 0);
    memcpy(was, fb, (size_t)W * H * sizeof(gfx_color_t));
    st.i = 1.5f; st.p = 9.0f;
    servo_screen_supply(&st);
    scr->render(&cv, 0);
    int outside = 0, inside = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[y * W + x] == was[y * W + x]) {
                continue;
            }
            const bool in = x >= 514 && x < 514 + 268 && y >= 148 && y < 244;
            if (in) { ++inside; } else { ++outside; }
        }
    }
    free(was);
    CHECK(inside > 0);
    CHECK_EQ(outside, 0);
    servo_screen_supply(NULL);
}

/* A second buffer, and a full redraw to hold the first against. */
static gfx_color_t *fb1, *ref;
static gfx_canvas_t cv1, cref;
#define FB_BYTES ((size_t)W * H * sizeof(gfx_color_t))

static void two_buffers(void)
{
    if (fb1 == NULL) {
        fb1 = calloc((size_t)W * H, sizeof(gfx_color_t));
        ref = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    gfx_canvas_init(&cv1, fb1, W, H, W);
    gfx_canvas_init(&cref, ref, W, H, W);
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
}

/* Both buffers drawn once more, each then held against one drawn whole. */
static bool both_whole(void)
{
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    const bool same = memcmp(fb, fb1, FB_BYTES) == 0;
    servo_invalidate();
    scr->render(&cref, 0);
    return same && memcmp(fb, ref, FB_BYTES) == 0;
}

/* A key of the text keyboard, where the screen opens it over the left
 * card. */
static void text_key(int k)
{
    ui_textkey_t probe;
    memset(&probe, 0, sizeof(probe));
    ui_textkey_open(&probe, (gfx_rect_t){ 6, 6, 488, 420 }, "", "", 23);
    const gfx_rect_t r = ui_textkey_key_rect(&probe, k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/*
 * Each face of the overlay -- the four pages, a list, the keypad, the
 * keyboard and the warning with a hold let go part way -- leaves both
 * buffers as one drawn whole, and a list or a warning cancelled leaves the
 * page as it was.  Opening and closing it leave nothing behind either: the
 * servo's lead runs out past the left card, where the overlay ends.
 */
TEST_CASE(each_face_of_the_overlay_draws_as_a_full_redraw_would)
{
    fresh();
    two_buffers();
    open_settings();
    CHECK(both_whole());
    CHECK_EQ(fb[6 * W + 6], ui_theme_color(UI_C_ACCENT));
    gfx_color_t *page = malloc(FB_BYTES);
    memcpy(page, fb, FB_BYTES);

    for (int tab = 1; tab < 4; ++tab) {
        tap(TAB_X(tab), TAB_Y);
        CHECK(both_whole());
        CHECK(memcmp(fb, page, FB_BYTES) != 0);
    }
    tap(ROW_L_X, ROW_Y(0));                    /* DUT NAME: the keyboard */
    CHECK(both_whole());
    text_key(UI_TK_CANCEL);
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "SERVO");

    tap(TAB_X(0), TAB_Y);
    tap(ROW_L_X, ROW_Y(4));                    /* TRAVEL: the keypad */
    CHECK(both_whole());
    key(UI_KEY_CANCEL);
    CHECK(both_whole());
    CHECK_EQ(memcmp(fb, page, FB_BYTES), 0);

    tap(ROW_L_X, ROW_Y(0));                    /* TYPE: the list */
    CHECK(both_whole());
    tap(CH_CANCEL_X, CH_CANCEL_Y);
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    CHECK(both_whole());
    CHECK_EQ(memcmp(fb, page, FB_BYTES), 0);

    tap(ROW_L_X, ROW_Y(0));
    tap(CH_X(3), CH_Y(3));                     /* HELI CYCLIC: the warning */
    CHECK(both_whole());
    CHECK_EQ(fb[6 * W + 6], ui_theme_color(UI_C_DANGER));
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 39; ++i) {             /* a second of the two */
        scr->tick(1.0f / 39.0f);
        scr->render((i % 2) ? &cv1 : &cv, i % 2);
    }
    CHECK(both_whole());
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    CHECK(both_whole());
    tap(WARN_CANCEL_X, WARN_Y);
    CHECK(both_whole());
    CHECK_EQ(memcmp(fb, page, FB_BYTES), 0);
    close_settings();
    CHECK(both_whole());
    free(page);
}

/*
 * The OUTPUT page says whether the frame rate reached the pins, and in the
 * warning colour when it did not: refused by the binding, or a coprocessor
 * that takes none.  News about another rate is not news about this one.
 */
TEST_CASE(the_output_page_says_whether_the_rate_reached_the_pins)
{
    fresh();
    two_buffers();
    open_settings();
    CHECK(both_whole());
    gfx_color_t *unsent = malloc(FB_BYTES);
    memcpy(unsent, fb, FB_BYTES);
    int warn = 0;

    servo_screen_rate(SERVO_RATE_IN_FORCE, 50u);
    CHECK(both_whole());
    CHECK(memcmp(fb, unsent, FB_BYTES) != 0);
    servo_screen_rate(SERVO_RATE_IN_FORCE, 333u);   /* not the rate shown */
    CHECK(both_whole());
    CHECK_EQ(memcmp(fb, unsent, FB_BYTES), 0);

    const servo_rate_state_t bad[] = { SERVO_RATE_REFUSED,
                                       SERVO_RATE_UNSUPPORTED };
    for (int k = 0; k < 2; ++k) {
        servo_screen_rate(bad[k], 50u);
        CHECK(both_whole());
        warn = 0;
        for (int y = 260; y < 400; ++y) {
            for (int x = 6; x < 494; ++x) {
                warn += fb[y * W + x] == ui_theme_color(UI_C_WARN);
            }
        }
        CHECK(warn > 0);
    }
    free(unsent);
}

/*
 * The save line follows the store and not the page: a save refused after
 * the change repaints that line alone, in the warning colour.
 */
TEST_CASE(a_refused_save_repaints_only_the_save_line)
{
    fresh();
    two_buffers();
    open_settings();
    tap(TAB_X(1), TAB_Y);
    tap(ROW_R_X, ROW_Y(0));                    /* STEP 4.8 V: asks to save */
    CHECK(settings_save_asked());
    CHECK(both_whole());
    gfx_color_t *was = malloc(FB_BYTES);
    memcpy(was, fb, FB_BYTES);
    CHECK(!settings_save());                   /* no store: refused */
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    int outside = 0, warn = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const bool in = x >= 16 && x < 16 + 244 && y >= 402 && y < 420;
            if (in && fb[y * W + x] == ui_theme_color(UI_C_WARN)) {
                ++warn;
            }
            if (!in && fb[y * W + x] != was[y * W + x]) {
                ++outside;
            }
        }
    }
    free(was);
    CHECK(warn > 0);
    CHECK_EQ(outside, 0);
    CHECK(both_whole());
}

/*
 * With nothing reporting, the horn chases the command at the set speed
 * rather than jumping to it, and its grip breathes while it is driven:
 * every frame of either leaves both buffers as a full redraw would.
 */
TEST_CASE(the_horn_travels_and_breathes_as_a_full_redraw_would)
{
    fresh();
    two_buffers();
    tap(ARM_X + 1, SPEED_Y);                   /* the slowest sweep, 10 % */
    int x, y;
    dial_at(60.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    scr->tick(1.0f / 39.0f);
    CHECK(both_whole());
    gfx_color_t *early = malloc(FB_BYTES);
    memcpy(early, fb, FB_BYTES);
    for (int i = 0; i < 4 * 39; ++i) {         /* 60 deg at 36 deg/s, and on */
        scr->tick(1.0f / 39.0f);
        scr->render((i % 2) ? &cv1 : &cv, i % 2);
    }
    CHECK(both_whole());
    CHECK(memcmp(fb, early, FB_BYTES) != 0);
    free(early);
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

/* SWEEP asks the coprocessor for the TEST page's curve, and only on an armed
 * bench and a coprocessor that sweeps: anything else would refuse it. */
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

/* The horn follows the curve the coprocessor runs; HOLD stops it where it
 * is and holds it there. */
TEST_CASE(the_horn_follows_the_sweep_and_hold_keeps_it_where_it_is)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    two_buffers();
    tap(SWEEP_X, BTN_Y);
    (void)last_cmd();
    for (int i = 0; i < 20; ++i) {             /* half a second, 0.5 Hz sine */
        scr->tick(1.0f / 39.0f);
        scr->render((i % 2) ? &cv1 : &cv, i % 2);
    }
    CHECK(both_whole());
    const uint16_t at = servo_screen_commanded();
    CHECK(at > 1880u && at <= 1900u);          /* 1500 + 400 at the peak */
    tap(SWEEP_X, BTN_Y);                       /* HOLD */
    CHECK(!servo_screen_sweeping());
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK(c.value_us > 1880u && c.value_us <= 1900u);
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
        CHECK_EQ(last_cmd().kind, by[k].kind);
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

    /* And a coprocessor that cannot sweep any more: what the panel holds is
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
 * coprocessor's does when it is written. */
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

/* HOLD keeps the horn where the output has got to, which a slow SPEED
 * leaves well behind the curve. */
TEST_CASE(hold_keeps_the_output_where_it_has_got_to)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED 10 %: 36 deg/s */
    tap(SWEEP_X, BTN_Y);
    frames(0.3f);                              /* the curve is near 58 deg */
    tap(SWEEP_X, BTN_Y);                       /* HOLD */
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK(c.value_us > 1500u && c.value_us < 1600u);   /* about 11 deg */
}

/*
 * Without feedback the horn is drawn where the far end's output is, slewing
 * as the coprocessor slews -- in its command units -- so HOLD keeps the
 * servo where it got to, with CENTRE off the middle as well.  The far end
 * here is the bank the coprocessor runs, fed the same sweep.
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
    tap(SWEEP_X, BTN_Y);                       /* HOLD */
    c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    const int far_us = (int)outputs_pulse_us(&far, 0);
    CHECK(far_us > 1560);                      /* well on its way */
    CHECK(abs((int)c.value_us - far_us) <= 6);
}

/* The horn is drawn along the far end's curve from when it started there. */
TEST_CASE(the_horn_follows_the_curve_from_where_the_far_end_started_it)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    frames(0.5f);                              /* the peak, as drawn */
    CHECK(servo_screen_commanded() > 1880u);
    servo_screen_sweep_started(0u);            /* it began just now */
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
    RUN(a_resting_armed_servo_takes_the_new_profile);
    RUN(a_profile_changed_while_an_arm_is_pending_is_the_one_armed);
    RUN(a_stop_abandons_a_hold_that_is_under_way);
    RUN(a_cancelled_hold_leaves_no_arm_to_be_read_later);
    RUN(a_second_contact_cannot_take_over_the_arm_hold);
    RUN(arming_drops_a_position_held_before_it);
    RUN(a_stop_on_a_bench_that_was_not_armed_still_lets_go);
    RUN(a_stop_stops_the_screen_holding_anything);
    RUN(leaving_disarms_and_lets_go_of_the_output);
    RUN(trim_shifts_the_pulse_and_not_the_angle);
    RUN(feedback_is_shown_rather_than_travelled_to);
    RUN(a_heli_type_waits_for_the_warning_held_two_seconds);
    RUN(every_profile_is_a_range_the_coprocessor_takes);
    RUN(a_pulse_width_the_coprocessor_would_refuse_is_not_taken);
    RUN(each_side_of_centre_runs_to_its_own_end);
    RUN(the_pause_is_kept_from_the_top_of_the_range_a_command_carries);
    RUN(a_warning_cancelled_dropped_or_left_applies_nothing);
    RUN(a_rate_above_60_hz_needs_the_warning_and_60_does_not);
    RUN(standard_pwm_keeps_a_millisecond_between_pulses);
    RUN(a_restart_is_standard_pwm_at_50_hz);
    RUN(reverse_and_the_pulse_widths_reshape_the_command);
    RUN(the_test_and_limit_settings_are_kept);
    RUN(the_overlay_leaves_the_right_card_working);
    RUN(the_tag_is_red_while_a_dangerous_profile_is_in_force);
    RUN(a_supply_sample_repaints_only_the_power_plot);
    RUN(each_face_of_the_overlay_draws_as_a_full_redraw_would);
    RUN(a_refused_save_repaints_only_the_save_line);
    RUN(the_output_page_says_whether_the_rate_reached_the_pins);
    RUN(the_horn_travels_and_breathes_as_a_full_redraw_would);
    RUN(a_sweep_needs_an_armed_bench_and_a_coprocessor_that_sweeps);
    RUN(the_horn_follows_the_sweep_and_hold_keeps_it_where_it_is);
    RUN(a_sweep_ends_on_the_dial_centre_release_disarm_and_leave);
    RUN(a_changed_setting_starts_the_sweep_over);
    RUN(a_profile_changed_while_it_sweeps_goes_with_the_sweep);
    RUN(hold_keeps_the_output_where_it_has_got_to);
    RUN(hold_without_feedback_matches_the_far_ends_slew);
    RUN(a_range_changed_while_it_sweeps_goes_with_its_own_amplitude);
    RUN(the_horn_follows_the_curve_from_where_the_far_end_started_it);
    RUN(drags_and_samples_leave_both_buffers_as_a_full_redraw_would);
    RUN(a_cancelled_gesture_does_not_arm);
    return test_summary("servo");
}
