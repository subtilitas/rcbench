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
#include "servo_sim.h"
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

/* The horn follows the curve the coprocessor runs; PAUSE asks the
 * coprocessor to hold the output where it is, and the drawing stops there. */
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
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_HOLD);
    const uint16_t held_at = servo_screen_commanded();
    CHECK(held_at > 1880u && held_at <= 1900u);
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
    tap(SWEEP_X, BTN_Y);
    frames(0.5f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    CHECK(servo_screen_commanded() > 1800u);
    servo_screen_released();
    CHECK_EQ(servo_screen_commanded(), 1500u);
    (void)last_cmd();
    choose_type(1);                            /* not driving: a rest */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);

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

/* PAUSE keeps the horn where the output has got to, which a slow SPEED
 * leaves well behind the curve. */
TEST_CASE(hold_keeps_the_output_where_it_has_got_to)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED 10 %: 36 deg/s */
    tap(SWEEP_X, BTN_Y);
    frames(0.3f);                              /* the curve is near 58 deg */
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_HOLD);
    const uint16_t at = servo_screen_commanded();   /* drawn where it got to */
    CHECK(at > 1500u && at < 1600u);           /* about 11 deg */
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
    tap(TAB_X(1), TAB_Y);                      /* TEST */
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
 * curve again, from its beginning, as the coprocessor does.  In both themes.
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
 * of a second after PAUSED.  A coprocessor that started the curve over
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
    frames(0.25f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    frames(2.0f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: on */
    CHECK(last_cmd().resume);
    frames(0.25f);
    CHECK(servo_screen_commanded() > 1880u);   /* the peak, 0.5 s in */

    /* Started over at the far end: drawn from the beginning. */
    servo_screen_sweep_started(0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.25f);
    CHECK(servo_screen_commanded() > 1760u && servo_screen_commanded() < 1800u);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_HOLD);

    /* Resumed 1450 ms before the panel heard: about 1960 ms into the
     * cycle, in the 200 ms dwell at the low end, 1100 us. */
    tap(SWEEP_X, BTN_Y);
    servo_screen_sweep_started(1450u, SERVO_SWEEP_RESUMED, 0u);
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
 * Without feedback the drawn output moves on to the acknowledged phase as
 * well: from about 1620 us at the tap to the curve's 1880 us, which SPEED
 * 100 % (2000 units a second) follows, in both buffers.  The resume slews
 * on from there, not from the tap's angle.
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
    tap(SWEEP_X, BTN_Y);                       /* SWEEP */
    servo_screen_sweep_started(0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.1f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    const uint16_t at_tap = servo_screen_drawn();
    CHECK(at_tap > 1590u && at_tap < 1650u);
    frames(0.3f);                              /* the HOLD on its way */
    CHECK_EQ(servo_screen_drawn(), at_tap);
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    servo_screen_sweep_held(0u);               /* acknowledged now */
    CHECK(both_whole());
    const uint16_t at_ack = servo_screen_drawn();
    CHECK(at_ack > 1860u && at_ack < 1900u);
    CHECK_EQ(servo_screen_commanded(), at_ack);
    frames(1.0f);
    CHECK_EQ(servo_screen_drawn(), at_ack);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: on */
    servo_screen_sweep_started(0u, SERVO_SWEEP_RESUMED, 0u);
    frames(0.05f);
    CHECK(servo_screen_commanded() > 1880u);
    CHECK(servo_screen_drawn() >= at_ack);     /* on from there */

    /* Held with nothing paused here changes nothing, and a time from before
     * the curve began is not this sweep's: paused about 450 ms in, near the
     * peak, it resumes there and reaches the dwell at the peak. */
    servo_screen_sweep_held(0u);
    CHECK(servo_screen_sweeping());
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    servo_screen_sweep_held(100000u);
    frames(0.5f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: on */
    servo_screen_sweep_started(0u, SERVO_SWEEP_RESUMED, 0u);
    frames(0.05f);
    CHECK(servo_screen_commanded() > 1880u);
}

/*
 * At SPEED's slowest the drawn output lags the curve, and moves on by no
 * more than SPEED allows over the 307 ms (12 frames) the HOLD took: at most
 * 240 units a second, 74 us on a 1000 to 2000 us range.
 */
/* A sweep at SPEED's slowest, paused 300 ms in and acknowledged 307 ms
 * later, SPEED raised to 100 % in between when @p raise; the drawn output
 * at the tap into @p at_tap, and returned as it is at the acknowledgement. */
static uint16_t slow_pause_acknowledged(bool raise, uint16_t *at_tap)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    tap(SWEEP_X, BTN_Y);
    servo_screen_sweep_started(0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.3f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    (void)last_cmd();
    *at_tap = servo_screen_drawn();
    frames(0.1f);
    if (raise) {
        tap(ARM_X + ARM_W - 1, SPEED_Y);       /* 100 %: nothing sent */
        CHECK(servo_screen_paused());
        CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    }
    frames(0.2f);
    servo_screen_sweep_held(0u);
    return servo_screen_drawn();
}

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
    tap(SWEEP_X, BTN_Y);
    frames(0.2f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    CHECK(servo_screen_paused());
    (void)last_cmd();
}

/*
 * A pause ends as a hold did: CENTRE, RELEASE, a finger on the dial, a
 * disarm, STOP, leaving the screen, a coprocessor that stops sweeping, a
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
    servo_screen_released();                   /* unrepeated for 500 ms */
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
 * as the coprocessor slews -- in its command units -- so the horn PAUSE
 * leaves drawn is where the coprocessor holds the servo, with CENTRE off
 * the middle as well.  The far end
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
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_HOLD);
    const int far_us = (int)outputs_pulse_us(&far, 0);
    CHECK(far_us > 1560);                      /* well on its way */
    CHECK(abs((int)servo_screen_commanded() - far_us) <= 6);
}

/*
 * Without feedback the horn starts from where the far end's output starts:
 * still where it was held when the sweep only now began there, or where it
 * froze when the sweep went unrepeated.
 */
TEST_CASE(the_drawn_output_starts_where_the_far_ends_output_starts)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED 10 % */
    tap(SWEEP_X, BTN_Y);
    frames(0.3f);                              /* drawn on its way */
    servo_screen_sweep_started(0u, SERVO_SWEEP_FROM_REST, 0u);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE at once */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_HOLD);
    uint16_t c = servo_screen_commanded();
    CHECK(c >= 1499u && c <= 1501u);

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);
    tap(SWEEP_X, BTN_Y);
    frames(0.2f);
    tap(SWEEP_X, BTN_Y);                       /* where it was at 0.2 s */
    const uint16_t at_02 = servo_screen_commanded();

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);
    tap(SWEEP_X, BTN_Y);
    frames(0.5f);
    servo_screen_sweep_started(0u, SERVO_SWEEP_FROM_FROZEN, 300u);
    tap(SWEEP_X, BTN_Y);
    c = servo_screen_commanded();
    CHECK(at_02 > 1520u);
    CHECK(abs((int)c - (int)at_02) <= 3);
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
    servo_screen_sweep_started(0u, SERVO_SWEEP_FROM_HERE, 0u);  /* just now */
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

/* ------------------------------------------------- the supply, from SERVO */

/* The SET row on the right card: the voltage, the current limit, and the
 * output switch, in the body's coordinates. */
#define SUP_ROW_Y   237
#define SUP_V_X     579
#define SUP_I_X     649
#define SUP_OUT_X   733

static bool supply_cmd(supply_cmd_t *out)
{
    supply_cmd_t junk;
    return supply_screen_poll_cmd(out != NULL ? out : &junk);
}

/* A set point typed on SERVO is SUPPLY's set point, and the keypad that
 * opened the overlay for it closes it again: the dial works at once. */
TEST_CASE(a_set_point_typed_on_servo_is_the_supplys)
{
    fresh();
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");
    CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
    tap(SUP_I_X, SUP_ROW_Y);
    keys("0.5");
    CHECK(fabsf(supply_screen_set_i() - 0.5f) < 1e-4f);
    CHECK(!supply_cmd(NULL));               /* a level, not a command */

    int x, y;
    dial_at(40.0f, ARC_R - 30, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y, TOUCH_EVENT_UP, 1);
    CHECK(servo_screen_commanded() != 1500);   /* no overlay in the way */

    /* CANCEL leaves the set point as it was. */
    tap(SUP_V_X, SUP_ROW_Y);
    key(UI_KEY_CANCEL);
    CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
}

/* Typed outside the caps, it is snapped into them, as on SUPPLY. */
TEST_CASE(a_set_point_typed_on_servo_keeps_to_the_caps)
{
    fresh();
    settings_set(SET_SUPPLY_V_MAX, 8.4f);
    supply_screen_limits_changed();
    tap(SUP_V_X, SUP_ROW_Y);
    keys("12");                             /* past the keypad's range */
    CHECK(supply_screen_set_v() <= 8.4f + 1e-4f);
}

/* OUTPUT ON is SUPPLY's two-second hold; a short press asks for nothing. */
TEST_CASE(output_on_from_servo_is_a_two_second_hold)
{
    fresh();
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 20; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));

    supply_cmd_t c;
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(supply_cmd(&c));
    CHECK(c.on);
    CHECK(!c.off);
}

/* On, the same switch is OUTPUT OFF and a tap: and a finger that slides
 * off it before lifting asks for nothing. */
TEST_CASE(output_off_from_servo_is_a_tap)
{
    fresh();
    supply_screen_set_output(true);
    scr->tick(0.025f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    ev(SUP_OUT_X, 100, TOUCH_EVENT_MOVE, 1);
    ev(SUP_OUT_X, 100, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));

    supply_cmd_t c;
    tap(SUP_OUT_X, SUP_ROW_Y);
    CHECK(supply_cmd(&c));
    CHECK(c.off);
    CHECK(!c.on);
}

/* STOP ends OUTPUT ON's hold here too: the rest of the two seconds switches
 * nothing on. */
TEST_CASE(stop_ends_the_output_hold_on_servo)
{
    fresh();
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    servo_screen_cancel_arm();
    for (int i = 0; i < 80; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));
}

/* With the output live, a typed set point waits for SUPPLY's question:
 * APPLY gives it to the supply, CANCEL drops it. */
TEST_CASE(a_set_point_for_a_live_output_waits_for_apply)
{
    fresh();
    supply_screen_set_output(true);
    scr->tick(0.025f);
    const float was = supply_screen_set_v();
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5");
    CHECK_EQ(supply_screen_set_v(), was);
    tap(WARN_CANCEL_X, WARN_Y);
    CHECK_EQ(supply_screen_set_v(), was);

    tap(SUP_V_X, SUP_ROW_Y);
    keys("5");
    CHECK_EQ(supply_screen_set_v(), was);
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);

    /* Asked not to ask: it goes straight through. */
    settings_set(SET_SUPPLY_CONFIRM_KEYS, 0.0f);
    tap(SUP_I_X, SUP_ROW_Y);
    keys("1");
    CHECK(fabsf(supply_screen_set_i() - 1.0f) < 1e-4f);
}

/* The question is about a live output: once the output is off it goes,
 * unanswered, and the change it held with it. */
TEST_CASE(the_question_goes_with_the_live_output)
{
    fresh();
    supply_screen_set_output(true);
    scr->tick(0.025f);
    const float was = supply_screen_set_v();
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5");
    supply_screen_set_output(false);
    scr->tick(0.025f);
    tap(WARN_APPLY_X, WARN_Y);              /* nothing there to press */
    CHECK_EQ(supply_screen_set_v(), was);
}

/* A set point changed on SUPPLY, or a cap that moved it, is drawn here on
 * the next frame, without a full repaint. */
TEST_CASE(a_set_point_changed_on_supply_is_redrawn_here)
{
    fresh();
    scr->render(&cv, 0);
    gfx_color_t *before = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(before, fb, (size_t)W * H * sizeof(gfx_color_t));
    supply_screen_put(9.0f, 1.0f);
    scr->tick(0.025f);
    scr->render(&cv, 0);
    int changed = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[(size_t)y * W + x] != before[(size_t)y * W + x]) {
                CHECK(y >= SUP_ROW_Y - 13 && y < SUP_ROW_Y + 13);
                ++changed;
            }
        }
    }
    CHECK(changed > 0);
    free(before);
}

/* A voltage raised past 6.0 V, a standard servo's rating, waits for the HV
 * warning's two-second hold: a tap does nothing, CANCEL drops it. */
TEST_CASE(a_voltage_raised_past_6_v_takes_the_hv_hold)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("6");                              /* at the rating: no warning */
    CHECK(fabsf(supply_screen_set_v() - 6.0f) < 1e-4f);

    tap(SUP_V_X, SUP_ROW_Y);
    keys("7.4");
    CHECK(fabsf(supply_screen_set_v() - 6.0f) < 1e-4f);
    tap(WARN_APPLY_X, WARN_Y);              /* a tap is not the hold */
    CHECK(fabsf(supply_screen_set_v() - 6.0f) < 1e-4f);
    tap(WARN_CANCEL_X, WARN_Y);
    CHECK(fabsf(supply_screen_set_v() - 6.0f) < 1e-4f);

    tap(SUP_V_X, SUP_ROW_Y);
    keys("7.4");
    hold_apply(2.3f);
    CHECK(fabsf(supply_screen_set_v() - 7.4f) < 1e-4f);

    /* Already past it: the warning was given, and a further change is a
     * plain set point again. */
    tap(SUP_V_X, SUP_ROW_Y);
    keys("8");
    CHECK(fabsf(supply_screen_set_v() - 8.0f) < 1e-4f);
}

/* With the output on, the HV hold stands in for SUPPLY's question, and the
 * output going off does not drop it: it is about the servo. */
TEST_CASE(the_hv_hold_stands_for_the_live_question)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("8.4");
    supply_screen_set_output(false);
    scr->tick(0.025f);
    hold_apply(2.3f);
    CHECK(fabsf(supply_screen_set_v() - 8.4f) < 1e-4f);
}

/* OUTPUT ON with a set point past 6.0 V already in force -- made on SUPPLY,
 * or before this screen was opened -- goes through the HV warning: the
 * switch's own hold switches nothing on, a tap on APPLY does nothing, STOP
 * ends the hold, and only the two-second hold on APPLY sends the ON. */
TEST_CASE(output_on_past_6_v_takes_the_hv_hold)
{
    fresh();
    supply_screen_put(7.4f, 1.0f);              /* as if set on SUPPLY */
    scr->tick(0.025f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));                   /* the switch's hold: no */

    tap(WARN_APPLY_X, WARN_Y);                  /* a tap is not the hold */
    CHECK(!supply_cmd(NULL));

    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    servo_screen_cancel_arm();                  /* STOP mid-hold */
    for (int i = 0; i < 80; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));

    hold_apply(2.3f);
    supply_cmd_t c;
    CHECK(supply_cmd(&c));
    CHECK(c.on);
    CHECK(!c.off);
    CHECK(fabsf(supply_screen_set_v() - 7.4f) < 1e-4f);

    /* CANCEL switches nothing on; the warning drawn says what is asked. */
    fresh();
    supply_screen_put(8.4f, 1.0f);
    scr->tick(0.025f);
    tap(SUP_OUT_X, SUP_ROW_Y);
    scr->tick(0.025f);
    scr->render(&cv, 0);
    tap(WARN_CANCEL_X, WARN_Y);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    CHECK(!supply_cmd(NULL));

    /* At 6.0 V it is the ordinary hold. */
    fresh();
    supply_screen_put(6.0f, 1.0f);
    scr->tick(0.025f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(supply_cmd(&c));
    CHECK(c.on);
}

/* A second hold on OUTPUT ON while the first ON is on its way: the output
 * reporting on during it does not make its release an OFF. */
TEST_CASE(an_on_reported_during_a_second_servo_hold_is_not_an_off)
{
    fresh();
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(supply_cmd(NULL));                    /* the first ON */
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    scr->tick(1.0f / 40.0f);
    supply_screen_set_output(true);
    scr->tick(1.0f / 40.0f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    supply_cmd_t c;
    CHECK(!supply_cmd(&c) || !c.off);
}

/* A question asked while an ON was on its way goes when STOP drops that
 * ON, though the output never reported on. */
TEST_CASE(a_servo_question_for_a_dropped_on_goes)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);   /* the question */
    supply_screen_cancel_on();
    servo_screen_cancel_arm();
    scr->tick(1.0f / 40.0f);
    tap(WARN_APPLY_X, WARN_Y);                  /* nothing there now */
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
}

/* APPLY pressed on the question, then the output goes off before the finger
 * lifts: the release applies nothing, whether the tick that drops the
 * question comes first or not. */
TEST_CASE(an_apply_held_as_the_output_goes_off_applies_nothing)
{
    for (int tick_first = 0; tick_first < 2; ++tick_first) {
        fresh();
        supply_screen_put(5.0f, 1.0f);
        supply_screen_set_output(true);
        scr->tick(0.025f);
        tap(SUP_V_X, SUP_ROW_Y);
        keys("5.5");
        ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
        supply_screen_set_output(false);
        if (tick_first) {
            scr->tick(0.025f);
        }
        ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
        CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
    }
}

/* APPLY gives the supply the set point that was typed, and leaves the
 * other as it is now: a cap that moved it meanwhile is not undone. */
TEST_CASE(apply_sets_only_the_set_point_that_was_typed)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_I_X, SUP_ROW_Y);
    keys("1.5");
    supply_screen_put(4.0f, 1.0f);              /* the voltage moved */
    scr->tick(0.025f);
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(fabsf(supply_screen_set_i() - 1.5f) < 1e-4f);
    CHECK(fabsf(supply_screen_set_v() - 4.0f) < 1e-4f);
}

/* A press on OUTPUT OFF in the frame the output was reported on: the
 * screen's drawn state has not caught up, and a lost release still sends
 * the OFF. */
TEST_CASE(an_off_press_before_the_tick_is_still_an_off)
{
    fresh();
    supply_screen_set_output(true);             /* no tick yet */
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    supply_cmd_t c;
    CHECK(supply_cmd(&c));
    CHECK(c.off);
}

/* The HV warning says whether the output is on, and says it again when
 * that changes. */
TEST_CASE(the_hv_warning_redraws_when_the_output_changes)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("7.4");
    scr->tick(0.025f);
    scr->render(&cv, 0);
    gfx_color_t *before = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(before, fb, (size_t)W * H * sizeof(gfx_color_t));
    supply_screen_set_output(true);
    scr->tick(0.025f);
    scr->render(&cv, 0);
    int changed = 0;
    for (int y = 0; y < 300; ++y) {             /* the warning's text */
        for (int x = 0; x < 490; ++x) {
            changed += (fb[(size_t)y * W + x] != before[(size_t)y * W + x]);
        }
    }
    CHECK(changed > 0);
    free(before);
}

/* A set point's keypad shut by SETTINGS leaves nothing behind: SETTINGS
 * opened afterwards stays open when a keypad from its own rows is done. */
TEST_CASE(settings_shuts_a_set_points_keypad_cleanly)
{
    fresh();
    tap(SUP_V_X, SUP_ROW_Y);                    /* the keypad, on its own */
    open_settings();                            /* shuts it */
    open_settings();                            /* the settings */
    tap(SUP_V_X, SUP_ROW_Y);                    /* a set point over them */
    key(UI_KEY_CANCEL);                         /* back to the settings */
    int x, y;
    dial_at(40.0f, ARC_R - 30, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(servo_screen_commanded(), 1500);   /* the overlay is over it */
}

/* A cap that comes down while the question stands takes the waiting value
 * with it; the cap recovering before APPLY does not bring it back. */
TEST_CASE(a_cap_lowered_under_the_question_lowers_its_value)
{
    fresh();
    supply_screen_put(9.0f, 1.0f);              /* past 6 V already */
    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("12");
    settings_set(SET_SUPPLY_V_MAX, 10.0f);
    supply_screen_limits_changed();
    scr->tick(0.025f);
    settings_set(SET_SUPPLY_V_MAX, 21.0f);
    supply_screen_limits_changed();
    scr->tick(0.025f);
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(supply_screen_set_v() <= 10.0f + 1e-4f);
    CHECK(supply_screen_set_v() > 9.0f);
}

/* The rest of the SET line's gestures, each drawn as it happens: a value
 * typed as it was, the question drawn, the HV hold slid off and drawn as it
 * fills, an ON hold under a finger as the output comes and goes, and an
 * OFF press the screen is left on. */
TEST_CASE(the_set_lines_gestures_draw_and_end_as_they_should)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5");                                  /* the value it has */
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);

    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");                                /* the question */
    scr->render(&cv, 0);
    CHECK_EQ(fb[(6 + UI_BAND_H * 0) * W + 6], ui_theme_color(UI_C_WARN));
    tap(WARN_CANCEL_X, WARN_Y);

    tap(SUP_V_X, SUP_ROW_Y);
    supply_screen_set_output(false);
    scr->tick(0.025f);
    keys("7.4");                                /* the HV warning */
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 20; ++i) {
        scr->tick(1.0f / 40.0f);
        scr->render(&cv, 0);
    }
    ev(WARN_APPLY_X, 100, TOUCH_EVENT_MOVE, 1);  /* slid off: abandoned */
    ev(WARN_APPLY_X, 100, TOUCH_EVENT_UP, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
    tap(WARN_CANCEL_X, WARN_Y);

    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    scr->tick(1.0f / 40.0f);
    supply_screen_set_output(true);
    scr->tick(1.0f / 40.0f);
    supply_screen_set_output(false);
    scr->tick(1.0f / 40.0f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));

    supply_screen_set_output(true);
    scr->tick(1.0f / 40.0f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    scr->leave();
    supply_cmd_t c;
    CHECK(supply_cmd(&c));
    CHECK(c.off);
}

/* STOP ends the warnings' holds as it ends ARM's: the rest of the two
 * seconds applies neither the voltage nor the profile. */
TEST_CASE(stop_ends_the_warning_holds)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("7.4");
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    servo_screen_cancel_arm();
    for (int i = 0; i < 80; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
    tap(WARN_CANCEL_X, WARN_Y);

    open_settings();
    tap(ROW_L_X, ROW_Y(0));
    tap(CH_X(3), CH_Y(3));                      /* HELI CYCLIC: the warning */
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    servo_screen_cancel_arm();
    for (int i = 0; i < 80; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
}

/* APPLY tapped on the question while the output is on, and STOP before the
 * finger lifts: the release applies nothing. */
TEST_CASE(stop_lets_go_of_an_apply_press)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    servo_screen_cancel_arm();
    supply_screen_cancel_on();
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
}

/* STOP and a whole APPLY tap in one frame: the stop is seen first, as the
 * render loop does, while the supply has not yet reported the output off.
 * The tap applies nothing, and the question has gone by the frame's tick. */
TEST_CASE(stop_and_an_apply_tap_in_one_frame_apply_nothing)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");
    servo_screen_cancel_arm();
    supply_screen_cancel_on();
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
    scr->tick(0.025f);
    tap(WARN_APPLY_X, WARN_Y);                  /* nothing there now */
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
}

/* A question asked after a stop, on an output still on, is not the one the
 * stop ended: APPLY applies it. */
TEST_CASE(a_question_asked_after_a_stop_stands)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    scr->tick(0.025f);
    servo_screen_cancel_arm();
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
}

/* The output reported off and on again between two frames: the question
 * was about the run that ended, and APPLY gives the new run nothing. */
TEST_CASE(a_question_does_not_outlive_its_run)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");
    supply_screen_set_output(false);           /* no tick between */
    supply_screen_set_output(true);
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
    scr->tick(0.025f);                          /* and the question is gone */
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
}

/* ------------------------------------------------ the automatic test */

/*
 * START TEST, mirrored from servo_screen.c: under the TEST page's right
 * column, row 6; the box on the left card and its button; HV SERVO at the
 * right column's row 2.
 */
#define START_X  369
#define START_Y  323
#define BOXBTN_X 91
#define BOXBTN_Y 104
#define HV_ROW_Y ROW_Y(2)

/* A bench for the screen's run: the servo model on the output, SUPPLY's
 * switch and set points as the panel would carry them, and a reading every
 * 100 ms stamped on the panel's clock. */
static struct {
    servo_sim_t sv;
    bool     out;
    bool     online;
    uint16_t samples;
    uint32_t now, next;
    uint16_t cmd;
    bool     released;
    bool     stepped;          /* a position went out with no slew   */
    float    set_v_max;        /* the highest set point while it ran */
    unsigned opens, csv, txt, ends;
    char     report[4096];
    size_t   report_len;
} b;

static void bench_fresh(void)
{
    fresh();
    memset(&b, 0, sizeof(b));
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 1000u;
    sc.stop_hi_us = 2000u;
    servo_sim_init(&b.sv, &sc);
    b.sv.position_us = 1500.0f;
    b.online = true;
    b.now  = 5000u;
    b.next = b.now;
    b.cmd  = 1500u;
    servo_screen_clock(b.now);
    servo_screen_set_armed(true);
    /* One reading before the run, as the panel always has one. */
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.set_v = supply_screen_set_v();
    st.samples = ++b.samples;
    st.taken_ms = b.now;
    servo_screen_supply(&st);
    scr->tick(0.02f);
}

static void bench_drain(void)
{
    const char *text = NULL;
    for (;;) {
        const servo_test_out_t o = servo_screen_test_peek(&text);
        if (o == SERVO_TEST_OUT_NONE) {
            return;
        }
        if (o == SERVO_TEST_OUT_OPEN) {
            ++b.opens;
        } else if (o == SERVO_TEST_OUT_CSV) {
            ++b.csv;
        } else if (o == SERVO_TEST_OUT_TXT) {
            ++b.txt;
            const size_t n = strlen(text);
            if (b.report_len + n + 2u < sizeof(b.report)) {
                memcpy(b.report + b.report_len, text, n);
                b.report_len += n;
                b.report[b.report_len++] = '\n';
                b.report[b.report_len] = '\0';
            }
        } else {
            ++b.ends;
        }
        servo_screen_test_pop();
    }
}

static void bench_frames(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; k += 20u) {
        b.now += 20u;
        servo_screen_clock(b.now);
        supply_cmd_t c;
        while (supply_screen_poll_cmd(&c)) {
            if (c.off) {
                b.out = false;
            } else if (c.on) {
                b.out = true;
            }
        }
        supply_screen_set_output(b.out);
        servo_cmd_t sc;
        while (servo_screen_take(&sc)) {
            if (sc.kind == SERVO_CMD_POSITION) {
                b.cmd = sc.value_us;
                b.stepped = b.stepped || sc.slew_per_s == 0u;
            } else if (sc.kind == SERVO_CMD_RELEASE
                       || sc.kind == SERVO_CMD_DISARM) {
                b.cmd = 1500u;
                b.released = true;
            }
        }
        if (servo_screen_testing() && supply_screen_set_v() > b.set_v_max) {
            b.set_v_max = supply_screen_set_v();
        }
        const float amps = b.out ? servo_sim_step(&b.sv, b.cmd, b.now) : 0.0f;
        if ((int32_t)(b.now - b.next) >= 0) {
            b.next += 100u;
            supply_state_t st;
            memset(&st, 0, sizeof(st));
            st.online = b.online;
            st.ok     = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
            st.output = b.out;
            st.set_v  = supply_screen_set_v();
            st.set_i  = supply_screen_set_i();
            st.v      = b.out ? st.set_v : 0.0f;
            st.i      = amps;
            st.p      = st.v * st.i;
            st.mode   = b.out ? SUPPLY_MODE_CV : SUPPLY_MODE_OFF;
            st.samples  = ++b.samples;
            st.taken_ms = b.now;
            servo_screen_supply(&st);
        }
        /* After the OFF was taken and the samples, as the panel runs it. */
        servo_screen_service();
        scr->tick(0.02f);
        bench_drain();
    }
}

/* Frames drawn as the panel draws them, so the flashes a hold or an
 * output switched on starts -- spent one a drawn frame -- are over. */
static void settle_drawn(void)
{
    two_buffers();
    for (int i = 0; i < 2 * UI_HOLD_FLASH_FRAMES; ++i) {
        scr->render((i & 1) ? &cv1 : &cv, i & 1);
        scr->tick(0.02f);
    }
}

/* As both_whole(), inside @p r only: away from the grip, whose breathing is
 * repainted at the step it has reached rather than every frame. */
static bool both_whole_in(int x0, int y0, int w, int h)
{
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    servo_invalidate();
    scr->render(&cref, 0);
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            const size_t i = (size_t)y * W + (size_t)x;
            if (fb[i] != fb1[i] || fb[i] != ref[i]) {
                return false;
            }
        }
    }
    return true;
}

/* Hold START TEST for @p secs on the TEST page, which is opened first. */
static void hold_start(float secs)
{
    open_settings();
    tap(TAB_X(1), TAB_Y);
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < (int)(secs * 50.0f + 0.5f); ++i) {
        b.now += 20u;
        servo_screen_clock(b.now);
        scr->tick(0.02f);
    }
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);
}

/* Short runs: two movements a step. */
static void short_runs(void)
{
    settings_set(SET_SERVO_LEN_BY, 1.0f);
    settings_set(SET_SERVO_LEN_MOVES, 2.0f);
}

/* START TEST runs only from a two-second hold on an armed bench: the run
 * closes the settings, asks the first set point and the output on, and
 * steps the servo to its centre with no slew. */
TEST_CASE(start_test_is_a_two_second_hold_on_an_armed_bench)
{
    fresh();
    memset(&b, 0, sizeof(b));
    hold_start(2.3f);
    CHECK(!servo_screen_testing());               /* not armed */
    scr->render(&cv, 0);

    bench_fresh();
    open_settings();
    tap(TAB_X(1), TAB_Y);
    tap(START_X, START_Y);                        /* a tap is not the hold */
    CHECK(!servo_screen_testing());
    close_settings();
    hold_start(1.0f);
    CHECK(!servo_screen_testing());
    close_settings();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    supply_cmd_t c;
    CHECK(supply_cmd(&c));
    CHECK(c.on);
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);
    const servo_cmd_t sc = last_cmd();
    CHECK_EQ(sc.kind, SERVO_CMD_POSITION);
    CHECK_EQ(sc.value_us, 1500u);
    CHECK_EQ(sc.slew_per_s, 0u);
    /* The settings closed: the run shows on the left card. */
    scr->render(&cv, 0);
    CHECK(fb[6 * W + 6] != ui_theme_color(UI_C_ACCENT));
}

/* A run through the screen: it passes, its files are handed over and
 * ended, the output goes off and the set points come back to what they
 * were. */
TEST_CASE(a_run_through_the_screen_ends_and_restores_the_set_points)
{
    bench_fresh();
    short_runs();
    supply_screen_put(5.5f, 1.5f);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK(b.stepped);
    CHECK(b.released);
    CHECK(!b.out);
    CHECK_EQ(b.opens, 1u);
    CHECK_EQ(b.ends, 1u);
    CHECK(b.csv > 50u);
    CHECK(strstr(b.report, "Result:         PASS") != NULL);
    CHECK(strstr(b.report, "Supply:         PD mini") != NULL);
    CHECK(b.set_v_max <= 6.0f + 1e-4f);
    CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
    CHECK(fabsf(supply_screen_set_i() - 1.5f) < 1e-4f);
    /* The result on the left card, and the files once the card says. */
    servo_screen_test_files(12, true);
    settle_drawn();
    two_buffers();
    CHECK(both_whole());
    servo_screen_test_files(-1, false);
    CHECK(both_whole());
    /* CLOSE puts it away. */
    tap(BOXBTN_X, BOXBTN_Y);
    CHECK(both_whole());
}

/* HV SERVO adds 7.4 and 8.4 V, and a run that has them starts only through
 * HV SERVOS ONLY held for two seconds: START TEST is then a tap that opens
 * it, and a tap on APPLY does nothing. */
TEST_CASE(a_run_above_6_v_starts_through_the_hv_hold)
{
    bench_fresh();
    short_runs();
    open_settings();
    tap(TAB_X(1), TAB_Y);
    tap(ROW_R_X, HV_ROW_Y);                       /* HV SERVO on */
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 120; ++i) {
        scr->tick(0.02f);
    }
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);      /* the warning, not a run */
    CHECK(!servo_screen_testing());
    scr->render(&cv, 0);
    CHECK_EQ(fb[6 * W + 6], ui_theme_color(UI_C_DANGER));
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(!servo_screen_testing());
    hold_apply(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 200 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(b.set_v_max > 8.3f);
    CHECK(strstr(b.report, "HV servo:       ON") != NULL);
    CHECK(strstr(b.report, " 8.40 V") != NULL);
}

/* Every way out of a run ends it: the output asked off, the servo let go,
 * and the report says ABORTED. */
TEST_CASE(every_way_out_of_a_run_switches_off_and_lets_go)
{
    for (int way = 0; way < 11; ++way) {
        bench_fresh();
        servo_screen_set_link(true);
        hold_start(2.3f);
        CHECK(servo_screen_testing());
        bench_frames(3000u);
        CHECK(b.out);
        b.released = false;
        switch (way) {
        case 0: servo_screen_cancel_arm(); break;              /* STOP */
        case 1: servo_screen_set_armed(false); break;
        case 2: servo_screen_set_link(false); break;
        case 3: scr->leave(); break;
        case 4: scr->cancel(); break;                          /* touch */
        case 5: {                                              /* the dial */
            int x, y;
            dial_at(30.0f, ARC_R - 20, &x, &y);
            tap(x, y);
            break;
        }
        case 6: tap(BOXBTN_X, BOXBTN_Y); break;                /* STOP TEST */
        case 7:                                     /* the page's STOP TEST */
            open_settings();
            tap(TAB_X(1), TAB_Y);
            tap(START_X, START_Y);
            break;
        case 8:                                     /* the profile changed */
            open_settings();
            tap(TAB_X(0), TAB_Y);
            tap(ROW_R_X, ROW_Y(4));                 /* REVERSE */
            break;
        case 9: b.online = false; break;            /* the supply is gone */
        default:                                    /* OUTPUT OFF */
            tap(SUP_OUT_X, SUP_ROW_Y);
            break;
        }
        bench_frames(2000u);
        CHECK(!servo_screen_testing());
        CHECK(!b.out);
        /* Let go, or -- a finger on the dial -- where the finger put it. */
        CHECK(b.released || (way == 5 && b.cmd != 1100u && b.cmd != 1900u));
        CHECK_EQ(b.ends, 1u);
        CHECK(strstr(b.report, "Result:         ABORTED") != NULL);
    }
}

/* START TEST's hold repaints only its button, a finger that leaves it or a
 * STOP mid-hold starts nothing, and a run's faces draw as a full redraw
 * would. */
TEST_CASE(the_runs_faces_draw_as_a_full_redraw_would)
{
    bench_fresh();
    short_runs();
    settle_drawn();
    two_buffers();
    open_settings();
    tap(TAB_X(1), TAB_Y);
    CHECK(both_whole());
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(0.02f);
    }
    CHECK(both_whole());
    ev(START_X, 10, TOUCH_EVENT_MOVE, 1);             /* off the button */
    for (int i = 0; i < 100; ++i) {
        scr->tick(0.02f);
    }
    ev(START_X, 10, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_testing());
    CHECK(both_whole());
    close_settings();

    /* Running, phase by phase. */
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 12; ++i) {
        bench_frames(700u);
        settle_drawn();
        CHECK(both_whole_in(16, 16, 270, 112));           /* the box */
        CHECK(both_whole_in(496, 0, W - 496, H));          /* the right card */
    }
    open_settings();
    tap(TAB_X(1), TAB_Y);
    CHECK(both_whole());                              /* RUNNING, STOP TEST */

    /* A STOP mid-hold starts nothing. */
    bench_fresh();
    open_settings();
    tap(TAB_X(1), TAB_Y);
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 50; ++i) {
        scr->tick(0.02f);
    }
    servo_screen_cancel_arm();
    for (int i = 0; i < 100; ++i) {
        scr->tick(0.02f);
    }
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_testing());
}

/* A run that cannot start says why on the TEST page; one whose report is
 * still on its way to the card waits for it. */
TEST_CASE(a_refused_start_says_why)
{
    bench_fresh();
    settings_set(SET_SERVO_STEP_48, 0.0f);
    settings_set(SET_SERVO_STEP_60, 0.0f);
    settings_set(SET_SERVO_BROWNOUT, 0.0f);
    hold_start(2.3f);
    CHECK(!servo_screen_testing());
    settle_drawn();
    two_buffers();
    CHECK(both_whole());

    bench_fresh();
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.samples = 99u;                 /* the supply not answering */
    servo_screen_supply(&st);
    hold_start(2.3f);
    CHECK(!servo_screen_testing());

    /* A run whose report has not been taken: the next waits. */
    bench_fresh();
    short_runs();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    servo_screen_cancel_arm();
    scr->tick(0.02f);
    hold_start(2.3f);
    CHECK(!servo_screen_testing());
    bench_drain();
    close_settings();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
}

/* ------------------------------------------- the review's cases, as tests */

/* As bench_frames(), with ONs from the screen let through only while
 * @p accept_on, and the highest set point an ON was asked at kept. */
static void bench_frames_on(uint32_t ms, bool accept_on, float *on_v_max)
{
    for (uint32_t k = 0; k < ms; k += 20u) {
        b.now += 20u;
        servo_screen_clock(b.now);
        supply_cmd_t c;
        while (supply_screen_poll_cmd(&c)) {
            if (c.off) {
                b.out = false;
            } else if (c.on) {
                if (supply_screen_set_v() > *on_v_max) {
                    *on_v_max = supply_screen_set_v();
                }
                if (accept_on) {
                    b.out = true;
                }
            }
        }
        supply_screen_set_output(b.out);
        servo_cmd_t sc;
        while (servo_screen_take(&sc)) { }
        if ((int32_t)(b.now - b.next) >= 0) {
            b.next += 100u;
            supply_state_t st;
            memset(&st, 0, sizeof(st));
            st.online = true;
            st.ok     = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
            st.output = b.out;
            st.set_v  = supply_screen_set_v();
            st.set_i  = supply_screen_set_i();
            st.v      = b.out ? st.set_v : 0.0f;
            st.mode   = b.out ? SUPPLY_MODE_CV : SUPPLY_MODE_OFF;
            st.samples  = ++b.samples;
            st.taken_ms = b.now;
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
    CHECK(!(b.out && supply_screen_set_v() > 6.0f + 1e-4f));
    bench_frames_on(2000u, true, &on_v);
    CHECK(on_v <= 6.0f + 1e-4f);
    /* The output came on at 4.8 V under the hold: the set points stay as
     * they are while it is on. */
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);
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
        CHECK(b.out);
        scr->leave();
        CHECK(!servo_screen_testing());
        if (changed) {
            supply_screen_put(5.2f, 1.0f);              /* on SUPPLY */
        }
        float on_v = 0.0f;
        for (uint32_t k = 0; k < 2000u; k += 20u) {    /* SERVO not ticked */
            b.now += 20u;
            servo_screen_clock(b.now);
            supply_cmd_t c;
            while (supply_screen_poll_cmd(&c)) {
                b.out = c.off ? false : (c.on ? true : b.out);
            }
            supply_screen_set_output(b.out);
            if ((int32_t)(b.now - b.next) >= 0) {
                b.next += 100u;
                supply_state_t st;
                memset(&st, 0, sizeof(st));
                st.online = true;
                st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
                st.output = b.out;
                st.set_v = supply_screen_set_v();
                st.samples = ++b.samples;
                st.taken_ms = b.now;
                servo_screen_supply(&st);
            }
            servo_screen_service();
            bench_drain();
        }
        CHECK(!b.out);
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
    st.samples = ++b.samples;
    st.taken_ms = b.now;
    servo_screen_supply(&st);
    servo_screen_service();
    servo_screen_service();
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);   /* not yet */
    /* The next frame and a sample of its own, but the module still reads
     * its output on: the panel asked it off and the PD mini has not yet
     * answered.  Not yet either. */
    b.now += 20u;
    servo_screen_clock(b.now);
    servo_screen_service();
    st.samples = ++b.samples;
    st.taken_ms = b.now;
    st.mode = SUPPLY_MODE_CV;
    servo_screen_supply(&st);
    servo_screen_service();
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);
    /* Nor from a supply that has stopped answering. */
    st.samples = ++b.samples;
    st.mode = SUPPLY_MODE_OFF;
    st.online = false;
    servo_screen_supply(&st);
    servo_screen_service();
    CHECK(fabsf(supply_screen_set_v() - 4.8f) < 1e-4f);
    /* The module's own off: back. */
    st.samples = ++b.samples;
    st.online = true;
    servo_screen_supply(&st);
    servo_screen_service();
    CHECK(fabsf(supply_screen_set_v() - 8.4f) < 1e-4f);
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
    open_settings();
    tap(TAB_X(1), TAB_Y);
    CHECK(both_whole());
    CHECK_EQ(fb[(START_Y + UI_BAND_H * 0) * W + START_X - 60],
             fb1[(START_Y + UI_BAND_H * 0) * W + START_X - 60]);
    close_settings();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK_EQ(b.ends, 2u);
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
    open_settings();
    tap(TAB_X(1), TAB_Y);
    two_buffers();
    /* Frames that keep the report back from the card. */
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        for (uint32_t k = 0; k < 500u; k += 20u) {
            b.now += 20u;
            servo_screen_clock(b.now);
            supply_cmd_t c;
            while (supply_screen_poll_cmd(&c)) {
                b.out = c.off ? false : (c.on ? true : b.out);
            }
            supply_screen_set_output(b.out);
            servo_cmd_t sc;
            while (servo_screen_take(&sc)) {
                if (sc.kind == SERVO_CMD_POSITION) {
                    b.cmd = sc.value_us;
                }
            }
            const float amps = b.out ? servo_sim_step(&b.sv, b.cmd, b.now)
                                     : 0.0f;
            if ((int32_t)(b.now - b.next) >= 0) {
                b.next += 100u;
                supply_state_t st;
                memset(&st, 0, sizeof(st));
                st.online = true;
                st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
                st.output = b.out;
                st.set_v = supply_screen_set_v();
                st.v = b.out ? st.set_v : 0.0f;
                st.i = amps;
                st.samples = ++b.samples;
                st.taken_ms = b.now;
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
    memset(&b, 0, sizeof(b));
    open_settings();
    tap(TAB_X(1), TAB_Y);
    tap(START_X, START_Y);                       /* ARM FIRST */
    two_buffers();
    gfx_color_t *was = malloc(FB_BYTES);
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
    open_settings();
    tap(TAB_X(1), TAB_Y);
    tap(ROW_R_X, HV_ROW_Y);                      /* HV SERVO on */
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
    open_settings();
    tap(TAB_X(1), TAB_Y);
    tap(ROW_R_X, HV_ROW_Y);
    tap(START_X, START_Y);
    scr->tick(0.02f);
    scr->render(&cv, 0);
    CHECK_EQ(fb[6 * W + 6], ui_theme_color(UI_C_DANGER));
    gfx_color_t *named = malloc(FB_BYTES);
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
    for (int k = 0; k < 2; ++k) {
        /* The page with nothing refused. */
        bench_fresh();
        settings_set(SET_SERVO_STEP_60, 0.0f);
        settings_set(SET_SERVO_BROWNOUT, 0.0f);
        open_settings();
        tap(TAB_X(1), TAB_Y);
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
            st.samples = ++b.samples;            /* not answering */
            servo_screen_supply(&st);
        }
        hold_start(2.3f);
        CHECK(!servo_screen_testing());
        if (k == 0) {
            settings_set(SET_SERVO_STEP_48, 1.0f);
        } else {
            st.online = true;
            st.samples = ++b.samples;
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
        b.out = c.off ? false : (c.on ? true : b.out);
    }
    supply_screen_set_output(b.out);
    const ui_screen_t *sup = supply_screen();
    const touch_event_t down = { .type = TOUCH_EVENT_DOWN,
                                 .point = { .id = 1, .x = 676, .y = 318,
                                            .strength = 40 } };
    sup->event(&down);
    CHECK(supply_screen_output_held());
    float on_v = 0.0f;
    for (uint32_t k = 0; k < 600u; k += 20u) {   /* samples, all off */
        b.now += 20u;
        servo_screen_clock(b.now);
        while (supply_screen_poll_cmd(&c)) {
            b.out = c.off ? false : (c.on ? true : b.out);
        }
        supply_screen_set_output(b.out);
        if ((int32_t)(b.now - b.next) >= 0) {
            b.next += 100u;
            supply_state_t st;
            memset(&st, 0, sizeof(st));
            st.online = true;
            st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
            st.output = b.out;
            st.set_v = supply_screen_set_v();
            st.samples = ++b.samples;
            st.taken_ms = b.now;
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
    RUN(speed_says_when_it_limits_the_sweep);
    RUN(pause_holds_the_sweep_and_a_second_tap_resumes_it);
    RUN(paused_carries_the_sweep_on_from_its_phase);
    RUN(the_pause_is_drawn_from_when_the_hold_was_acknowledged);
    RUN(the_acknowledged_pause_moves_the_output_at_speed);
    RUN(a_curve_changed_while_paused_starts_over);
    RUN(a_pause_ends_where_a_hold_ended);
    RUN(hold_without_feedback_matches_the_far_ends_slew);
    RUN(the_drawn_output_starts_where_the_far_ends_output_starts);
    RUN(a_range_changed_while_it_sweeps_goes_with_its_own_amplitude);
    RUN(the_horn_follows_the_curve_from_where_the_far_end_started_it);
    RUN(drags_and_samples_leave_both_buffers_as_a_full_redraw_would);
    RUN(a_cancelled_gesture_does_not_arm);
    RUN(a_set_point_typed_on_servo_is_the_supplys);
    RUN(a_set_point_typed_on_servo_keeps_to_the_caps);
    RUN(output_on_from_servo_is_a_two_second_hold);
    RUN(output_off_from_servo_is_a_tap);
    RUN(stop_ends_the_output_hold_on_servo);
    RUN(a_set_point_for_a_live_output_waits_for_apply);
    RUN(the_question_goes_with_the_live_output);
    RUN(a_set_point_changed_on_supply_is_redrawn_here);
    RUN(a_voltage_raised_past_6_v_takes_the_hv_hold);
    RUN(the_hv_hold_stands_for_the_live_question);
    RUN(output_on_past_6_v_takes_the_hv_hold);
    RUN(an_on_reported_during_a_second_servo_hold_is_not_an_off);
    RUN(a_servo_question_for_a_dropped_on_goes);
    RUN(an_apply_held_as_the_output_goes_off_applies_nothing);
    RUN(apply_sets_only_the_set_point_that_was_typed);
    RUN(an_off_press_before_the_tick_is_still_an_off);
    RUN(the_hv_warning_redraws_when_the_output_changes);
    RUN(settings_shuts_a_set_points_keypad_cleanly);
    RUN(a_cap_lowered_under_the_question_lowers_its_value);
    RUN(the_set_lines_gestures_draw_and_end_as_they_should);
    RUN(stop_ends_the_warning_holds);
    RUN(stop_lets_go_of_an_apply_press);
    RUN(stop_and_an_apply_tap_in_one_frame_apply_nothing);
    RUN(a_question_asked_after_a_stop_stands);
    RUN(a_question_does_not_outlive_its_run);
    RUN(start_test_is_a_two_second_hold_on_an_armed_bench);
    RUN(a_run_through_the_screen_ends_and_restores_the_set_points);
    RUN(a_run_above_6_v_starts_through_the_hv_hold);
    RUN(every_way_out_of_a_run_switches_off_and_lets_go);
    RUN(the_runs_faces_draw_as_a_full_redraw_would);
    RUN(a_refused_start_says_why);
    RUN(a_run_ending_under_output_ons_hold_switches_nothing_on_past_6_v);
    RUN(output_ons_hold_looks_at_the_set_point_again_as_it_completes);
    RUN(the_set_points_go_back_whichever_screen_is_up);
    RUN(the_set_points_wait_for_a_sample_after_the_off);
    RUN(start_test_starts_a_second_run);
    RUN(the_start_line_follows_the_report_being_taken);
    RUN(arm_first_goes_once_the_bench_is_armed);
    RUN(a_run_outside_the_caps_is_refused_before_the_warning);
    RUN(the_result_names_the_report_only_when_it_was_written);
    RUN(a_refusal_goes_with_the_edit_that_answers_it);
    RUN(a_hold_on_supplys_output_on_holds_the_restore_back);
    return test_summary("servo");
}
