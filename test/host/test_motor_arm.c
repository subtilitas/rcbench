/*
 * The MOTOR & ESC screen's throttle against the bench's armed state: no
 * control changes it while the bench is not armed, a disarm returns it to
 * 0 %, and every arm starts from there.
 *
 * Touch is fed as the panel produces it: frames of contacts through the
 * tracker and the router (touch_feed.h).  The bench's answers -- armed,
 * disarmed, a stop -- are given the way the panel's frame loop gives them.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "gfx.h"
#include "motor_screen.h"
#include "settings.h"
#include "splash_screen.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H 480

/*
 * Geometry in panel coordinates, mirrored from motor_screen.c with the
 * band's height added: the throttle track x 72..485, y 384..423, with -1 at
 * x 12..65 and +1 at x 492..545; ARM at x 564..787, y 348..383; RESET PEAKS
 * under it; the hint line under the track.
 */
#define P(y)      (UI_BAND_H + (y))
#define TRACK_X   72
#define TRACK_Y   P(356)
#define DOWN_X    33
#define UP_X      519
#define ARM_X     676
#define ARM_Y     P(318)
#define RESET_X   676
#define RESET_Y   P(402)
#define HINT_Y    P(390)
/* Throttle per px of travel on the track: 100 % over its 413 px. */
#define PX_PCT    (100.0f / 413.0f)

static gfx_color_t *fb;
static gfx_color_t *fb_was;
static gfx_canvas_t cv;

static motor_cmd_t took(void)
{
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f };
    (void)motor_screen_poll_cmd(&c);
    return c;
}

static void drain(void)
{
    while (took().kind != MOTOR_CMD_NONE) { }
}

/* MOTOR & ESC up on a disarmed bench, nothing on the glass. */
static void fresh(void)
{
    if (fb == NULL) {
        fb     = calloc((size_t)W * H, sizeof(gfx_color_t));
        fb_was = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_MOTOR);
    feed_reset();
    motor_screen_set_armed(false);
    drain();
}

static void frames(int n)
{
    for (int i = 0; i < n; ++i) {
        ui_router_tick(0.026f);
    }
}

/* Frames drawn into both buffers, so ARM's flash, spent one a drawn frame,
 * is over. */
static void settle(void)
{
    for (int i = 0; i < 2 * UI_HOLD_FLASH_FRAMES + 2; ++i) {
        ui_router_tick(0.026f);
        ui_router_render(&cv, i & 1);
    }
}

/* The whole panel as a full redraw draws it. */
static void picture(gfx_color_t *into)
{
    ui_router_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    if (into != fb) {
        memcpy(into, fb, (size_t)W * H * sizeof(gfx_color_t));
    }
}

static bool picture_unchanged(void)
{
    picture(fb);
    return memcmp(fb, fb_was, (size_t)W * H * sizeof(gfx_color_t)) == 0;
}

/* A press on the track at @p from px of its travel, dragged by @p px and
 * lifted. */
static void drag(int from, int px)
{
    finger(FEED_LONE, TRACK_X + from, TRACK_Y);
    glide(FEED_LONE, TRACK_X + from + px, TRACK_Y, 8);
    lift(FEED_LONE);
}

/* ARM held until it posts, and the bench's answer. */
static bool arm(void)
{
    finger(FEED_LONE, ARM_X, ARM_Y);
    frames((int)(2.3f / 0.026f));
    const motor_cmd_t c = took();
    lift(FEED_LONE);
    if (c.kind != MOTOR_CMD_ARM) {
        return false;
    }
    motor_screen_set_armed(true);
    return true;
}

/* DISARM tapped, and the bench's answer. */
static bool disarm(void)
{
    feed_tap(FEED_LONE, ARM_X, ARM_Y);
    const bool asked = took().kind == MOTOR_CMD_DISARM;
    motor_screen_set_armed(false);
    return asked;
}

/* The band's STOP tapped, then what the panel's frame loop does with a
 * stop: both the gesture's end and the bench's answer. */
static bool stop(void)
{
    const gfx_rect_t r = ui_band_stop_rect();
    feed_tap(FEED_LONE, r.x + r.w / 2, r.y + r.h / 2);
    const bool pressed = ui_router_take_stop();
    motor_screen_cancel_arm();
    motor_screen_set_armed(false);
    return pressed;
}

/* One turn of the knob as the panel applies it: a frame, then the turn. */
static void knob(float span)
{
    motor_screen_knob_frame();
    motor_screen_knob(span);
}

/* Every throttle control at once, each refused: the track pressed and
 * dragged both ways, - and + pressed and held, the knob turned both ways.
 * True when none posted, moved or drew anything. */
static bool every_control_is_refused(void)
{
    const float value = motor_screen_throttle();
    settle();
    picture(fb_was);
    bool quiet = true;

    drag(28, 250);
    quiet = quiet && took().kind == MOTOR_CMD_NONE;
    drag(300, -200);
    quiet = quiet && took().kind == MOTOR_CMD_NONE;
    finger(FEED_LONE, UP_X, TRACK_Y);
    quiet = quiet && took().kind == MOTOR_CMD_NONE && picture_unchanged();
    lift(FEED_LONE);
    quiet = quiet && took().kind == MOTOR_CMD_NONE;
    finger(FEED_LONE, DOWN_X, TRACK_Y);
    quiet = quiet && took().kind == MOTOR_CMD_NONE && picture_unchanged();
    lift(FEED_LONE);
    quiet = quiet && took().kind == MOTOR_CMD_NONE;
    knob(0.25f);
    quiet = quiet && took().kind == MOTOR_CMD_NONE;
    knob(-0.5f);
    quiet = quiet && took().kind == MOTOR_CMD_NONE;
    motor_screen_knob_cancel();
    frames(40);
    quiet = quiet && took().kind == MOTOR_CMD_NONE;

    return quiet && motor_screen_throttle() == value && picture_unchanged();
}

/* ------------------------------------------------------------- refusals */

/* A bench never armed: the throttle is the 0 % it starts at, and stays. */
TEST_CASE(no_throttle_control_moves_the_value_before_the_first_arm)
{
    fresh();
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK(every_control_is_refused());
    CHECK_EQ(motor_screen_throttle(), 0.0f);
}

/* Disarmed after a run at 24 %: the throttle is 0 %, whatever is pressed,
 * dragged or turned. */
TEST_CASE(no_throttle_control_moves_the_value_after_a_disarm)
{
    fresh();
    CHECK(arm());
    drag(28, 100);
    CHECK_NEAR(took().value, 100.0f * PX_PCT, 0.01f);
    CHECK(disarm());
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK(every_control_is_refused());
    CHECK_EQ(motor_screen_throttle(), 0.0f);
}

/* Between the hold's ask and the bench's answer the bench is not armed. */
TEST_CASE(no_throttle_control_moves_the_value_before_the_bench_answers)
{
    fresh();
    finger(FEED_LONE, ARM_X, ARM_Y);
    frames((int)(2.3f / 0.026f));
    lift(FEED_LONE);
    /* The ARM still waiting to be taken, then taken. */
    drag(28, 100);
    feed_tap(FEED_LONE, UP_X, TRACK_Y);
    knob(0.1f);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(took().kind, MOTOR_CMD_ARM);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK(every_control_is_refused());
    motor_screen_set_armed(true);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
}

/* Under a disarm waiting to be taken every throttle is dropped, so the
 * value does not move either; the bench's answer returns it to 0 %. */
TEST_CASE(nothing_moves_the_throttle_under_a_disarm_not_yet_taken)
{
    fresh();
    CHECK(arm());
    drag(28, 100);
    const float driven = took().value;
    CHECK_NEAR(driven, 100.0f * PX_PCT, 0.01f);
    feed_tap(FEED_LONE, ARM_X, ARM_Y);  /* DISARM, not yet taken */
    drag(28, 200);
    feed_tap(FEED_LONE, UP_X, TRACK_Y);
    feed_tap(FEED_LONE, DOWN_X, TRACK_Y);
    knob(0.2f);
    CHECK_EQ(motor_screen_throttle(), driven);
    CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    motor_screen_set_armed(false);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
}

/*
 * A DISARM taken and not yet answered: the bench still reads armed here for
 * the frames its answer takes, and every control stays refused through
 * them.  Reporting the bench armed again in that time changes nothing; its
 * report of the disarm returns the throttle to 0 %, and the arm after it
 * takes input again.
 */
TEST_CASE(nothing_moves_the_throttle_between_a_disarm_and_its_answer)
{
    for (int by_leaving = 0; by_leaving < 2; ++by_leaving) {
        fresh();
        CHECK(arm());
        drag(28, 100);
        const float driven = took().value;
        CHECK_NEAR(driven, 100.0f * PX_PCT, 0.01f);
        if (by_leaving) {
            ui_router_goto(SCREEN_OVERVIEW);
            CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
            ui_router_goto(SCREEN_MOTOR);
        } else {
            feed_tap(FEED_LONE, ARM_X, ARM_Y);
            CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
        }
        for (int frame = 0; frame < 5; ++frame) {
            motor_screen_set_armed(true);       /* not yet answered */
            drag(28, 200);
            feed_tap(FEED_LONE, UP_X, TRACK_Y);
            feed_tap(FEED_LONE, DOWN_X, TRACK_Y);
            knob(0.2f);
            CHECK_EQ(took().kind, MOTOR_CMD_NONE);
            CHECK_EQ(motor_screen_throttle(), driven);
        }
        motor_screen_set_armed(false);
        CHECK_EQ(motor_screen_throttle(), 0.0f);
        CHECK(every_control_is_refused());
        CHECK(arm());
        feed_tap(FEED_LONE, UP_X, TRACK_Y);
        CHECK_NEAR(took().value, 1.0f, 0.001f);
    }
}

/* A drag under way when a second finger taps DISARM ends there: its moves
 * behind the disarm change nothing. */
TEST_CASE(a_drag_ends_where_the_disarm_is_asked)
{
    fresh();
    CHECK(arm());
    finger(1, TRACK_X + 28, TRACK_Y);
    glide(1, TRACK_X + 128, TRACK_Y, 8);
    const float driven = took().value;
    CHECK_NEAR(driven, 100.0f * PX_PCT, 0.01f);
    feed_tap(2, ARM_X, ARM_Y);
    glide(1, TRACK_X + 228, TRACK_Y, 8);
    CHECK_EQ(motor_screen_throttle(), driven);
    CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    /* Armed again under the same finger, the drag is not taken up. */
    motor_screen_set_armed(false);
    motor_screen_set_armed(true);
    glide(1, TRACK_X + 300, TRACK_Y, 8);
    lift(1);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
}

/* A press that began on the disarmed track is not a drag once the bench
 * arms under it: the first input after an arm is a new press. */
TEST_CASE(a_press_on_the_disarmed_track_is_no_drag_after_the_arm)
{
    fresh();
    finger(1, TRACK_X + 28, TRACK_Y);   /* refused */
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    finger(2, ARM_X, ARM_Y);            /* a second finger holds ARM */
    frames((int)(2.3f / 0.026f));
    CHECK_EQ(took().kind, MOTOR_CMD_ARM);
    motor_screen_set_armed(true);
    glide(1, TRACK_X + 228, TRACK_Y, 8);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    lift(1);
    lift(2);
    drain();
    /* And the next press is taken. */
    drag(28, 40);
    CHECK_NEAR(took().value, 40.0f * PX_PCT, 0.01f);
}

/* ---------------------------------------------------------- the arm edge */

/* The first input after an arm starts from 0 %, whatever the run before it
 * ended at: +, the knob's 1 % and a drag of 40 px. */
TEST_CASE(the_first_input_after_an_arm_starts_from_zero)
{
    for (int input = 0; input < 3; ++input) {
        fresh();
        CHECK(arm());
        drag(28, 250);                  /* 60.5 % */
        CHECK(took().value > 60.0f);
        CHECK(disarm());
        CHECK(arm());
        CHECK_EQ(motor_screen_throttle(), 0.0f);
        CHECK_EQ(took().kind, MOTOR_CMD_NONE);
        float want = 0.0f;
        switch (input) {
        case 0:
            feed_tap(FEED_LONE, UP_X, TRACK_Y);
            want = 1.0f;
            break;
        case 1:
            knob(0.01f);
            want = 1.0f;
            break;
        default:
            drag(200, 40);
            want = 40.0f * PX_PCT;
            break;
        }
        const motor_cmd_t c = took();
        CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
        CHECK_NEAR(c.value, want, 0.001f);
        CHECK_NEAR(motor_screen_throttle(), want, 0.001f);
    }
}

/* Turns made while disarmed are not kept: 50 of them, then an arm, and the
 * throttle is 0 %; the next 1 % is 1 %. */
TEST_CASE(knob_turns_on_a_disarmed_bench_are_not_accumulated)
{
    fresh();
    for (int i = 0; i < 50; ++i) {
        knob(0.01f);
        ui_router_tick(0.026f);
    }
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK(arm());
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    knob(0.01f);
    CHECK_NEAR(took().value, 1.0f, 0.001f);

    /* And across a disarm between two runs. */
    CHECK(disarm());
    for (int i = 0; i < 50; ++i) {
        knob(0.01f);
    }
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK(arm());
    knob(0.02f);
    CHECK_NEAR(took().value, 2.0f, 0.001f);
}

/* The disarm returns the throttle to 0 % itself, at the limits of the
 * slider and one step inside each. */
TEST_CASE(a_disarm_returns_every_throttle_to_zero)
{
    static const float from[] = { 0.0f, 1.0f, 60.5f, 99.0f, 100.0f };
    for (size_t i = 0; i < sizeof(from) / sizeof(from[0]); ++i) {
        fresh();
        CHECK(arm());
        motor_screen_set_throttle(from[i]);
        CHECK_EQ(motor_screen_throttle(), from[i]);
        motor_screen_set_armed(false);
        CHECK_EQ(motor_screen_throttle(), 0.0f);
        CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    }
}

/* A throttle posted and not yet taken as the bench disarms or stops is not
 * sent to the disarmed bench: the track's, the step button's, and the
 * knob's, which a withdrawal afterwards does not put back. */
TEST_CASE(a_throttle_not_yet_sent_is_dropped_by_a_disarm_and_by_a_stop)
{
    for (int by_stop = 0; by_stop < 2; ++by_stop) {
        for (int input = 0; input < 3; ++input) {
            fresh();
            CHECK(arm());
            drag(28, 100);
            drain();
            switch (input) {
            case 0:  drag(200, 40); break;
            case 1:  feed_tap(FEED_LONE, UP_X, TRACK_Y); break;
            default: knob(0.05f); break;
            }
            CHECK(motor_screen_throttle() > 100.0f * PX_PCT);
            if (by_stop) {
                motor_screen_cancel_arm();
            }
            motor_screen_set_armed(false);
            motor_screen_knob_cancel();
            CHECK_EQ(took().kind, MOTOR_CMD_NONE);
            CHECK_EQ(motor_screen_throttle(), 0.0f);
        }
    }
}

/* ------------------------------------------------- every way out, in turn */

/* Arm, drive, disarm, drag, arm, STOP, drag, arm: each disarmed drag is
 * refused and each arm starts from 0 %. */
TEST_CASE(arm_drive_disarm_drag_arm_stop_drag_arm)
{
    fresh();
    CHECK(arm());
    drag(28, 100);
    CHECK_NEAR(took().value, 100.0f * PX_PCT, 0.01f);
    CHECK(disarm());
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    drag(28, 250);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK_EQ(motor_screen_throttle(), 0.0f);

    CHECK(arm());
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    drag(28, 60);
    CHECK_NEAR(took().value, 60.0f * PX_PCT, 0.01f);
    CHECK(stop());
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    drag(28, 250);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK_EQ(motor_screen_throttle(), 0.0f);

    CHECK(arm());
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    drag(28, 40);
    CHECK_NEAR(took().value, 40.0f * PX_PCT, 0.01f);
}

/*
 * A run each way it ends -- DISARM, STOP, the link going, another screen --
 * and the arm after it.  Disarmed, the throttle is 0 % and nothing moves
 * it; armed again, the first step is 1 %.
 */
TEST_CASE(disarm_stop_link_loss_and_leaving_each_end_at_zero)
{
    fresh();
    for (int way = 0; way < 4; ++way) {
        CHECK(arm());
        CHECK_EQ(motor_screen_throttle(), 0.0f);
        CHECK_EQ(took().kind, MOTOR_CMD_NONE);
        feed_tap(FEED_LONE, UP_X, TRACK_Y);
        CHECK_NEAR(took().value, 1.0f, 0.001f);
        drag(28, 100 + 40 * way);
        CHECK(took().value > 24.0f);
        switch (way) {
        case 0:
            CHECK(disarm());
            break;
        case 1:
            CHECK(stop());
            break;
        case 2:
            /* The link goes quiet under an armed bench: the panel latches
             * a stop, and the bench reports disarmed. */
            motor_screen_cancel_arm();
            motor_screen_set_armed(false);
            break;
        default:
            /* Leaving disarms; the bench reports armed for one more frame
             * and then disarmed. */
            ui_router_goto(SCREEN_OVERVIEW);
            CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
            motor_screen_set_armed(true);
            motor_screen_set_armed(false);
            ui_router_goto(SCREEN_MOTOR);
            break;
        }
        CHECK_EQ(took().kind, MOTOR_CMD_NONE);
        CHECK_EQ(motor_screen_throttle(), 0.0f);
        CHECK(every_control_is_refused());
        CHECK_EQ(motor_screen_throttle(), 0.0f);
    }
    CHECK(arm());
    CHECK_EQ(motor_screen_throttle(), 0.0f);
}

/* Left with a finger on the track: the drag does not come back with the
 * screen, armed or not. */
TEST_CASE(a_drag_does_not_cross_leaving_the_screen)
{
    fresh();
    CHECK(arm());
    finger(1, TRACK_X + 28, TRACK_Y);
    glide(1, TRACK_X + 128, TRACK_Y, 8);
    drain();
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
    motor_screen_set_armed(false);
    ui_router_goto(SCREEN_MOTOR);
    glide(1, TRACK_X + 228, TRACK_Y, 8);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    motor_screen_set_armed(true);
    glide(1, TRACK_X + 300, TRACK_Y, 8);
    lift(1);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
}

/* ---------------------------------------------------------- the picture */

static gfx_color_t px(int x, int y) { return fb[(size_t)y * W + x]; }

/* Pixels of the warning colour on the line under the track, where the
 * screen says ARM FIRST. */
static int arm_first_px(void)
{
    int n = 0;
    for (int y = HINT_Y; y < HINT_Y + 16; ++y) {
        for (int x = 12; x < 100; ++x) {
            if (px(x, y) == ui_theme_color(UI_C_WARN)) {
                ++n;
            }
        }
    }
    return n;
}

/* Pixels of the hint's colour on the same line, past where ARM FIRST ends. */
static int hint_px(void)
{
    int n = 0;
    for (int y = HINT_Y; y < HINT_Y + 16; ++y) {
        for (int x = 100; x < 420; ++x) {
            if (px(x, y) == ui_theme_color(UI_C_TEXT_FAINT)) {
                ++n;
            }
        }
    }
    return n;
}

/* Disarmed, the step buttons and the thumb are drawn dimmed and the line
 * under the track says ARM FIRST; armed, they are the panel's own colours
 * and the line is the hint. */
TEST_CASE(the_throttle_is_dimmed_and_says_arm_first_while_disarmed)
{
    fresh();
    picture(fb);
    const gfx_color_t sunk  = ui_theme_color(UI_C_PANEL_SUNK);
    const gfx_color_t thumb = ui_theme_color(UI_C_TEXT);
    /* Inside each step button, clear of its label; and on the thumb, which
     * at 0 % is centred 10 px into the track, between its grip lines. */
    const gfx_color_t down_off = px(DOWN_X - 16, TRACK_Y - 12);
    CHECK(down_off != sunk);
    CHECK_EQ(px(UP_X - 16, TRACK_Y - 12), down_off);
    CHECK(px(TRACK_X + 10, TRACK_Y - 15) != thumb);
    CHECK(arm_first_px() > 0);
    CHECK_EQ(hint_px(), 0);

    CHECK(arm());
    settle();
    picture(fb);
    CHECK_EQ(px(DOWN_X - 16, TRACK_Y - 12), sunk);
    CHECK_EQ(px(UP_X - 16, TRACK_Y - 12), sunk);
    CHECK_EQ(px(TRACK_X + 10, TRACK_Y - 15), thumb);
    CHECK_EQ(arm_first_px(), 0);
    CHECK(hint_px() > 0);

    CHECK(disarm());
    settle();
    picture(fb);
    CHECK_EQ(px(DOWN_X - 16, TRACK_Y - 12), down_off);
    CHECK(px(TRACK_X + 10, TRACK_Y - 15) != thumb);
    CHECK(arm_first_px() > 0);
    CHECK_EQ(hint_px(), 0);
}

/* The change shows without a full redraw: both buffers, repainted only by
 * their revision counters, match a full redraw after the arm and after the
 * disarm. */
TEST_CASE(the_dimming_follows_the_arm_in_both_buffers)
{
    static gfx_color_t *other;
    if (other == NULL) {
        other = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    gfx_canvas_t cv1;
    gfx_canvas_init(&cv1, other, W, H, W);
    fresh();
    ui_router_render(&cv, 0);
    ui_router_render(&cv1, 1);
    for (int edge = 0; edge < 2; ++edge) {
        if (edge == 0) {
            CHECK(arm());
        } else {
            CHECK(disarm());
        }
        for (int i = 0; i < 2 * UI_HOLD_FLASH_FRAMES + 2; ++i) {
            ui_router_tick(0.026f);
            ui_router_render((i & 1) ? &cv1 : &cv, i & 1);
        }
        memcpy(fb_was, fb, (size_t)W * H * sizeof(gfx_color_t));
        CHECK(memcmp(fb, other, (size_t)W * H * sizeof(gfx_color_t)) == 0);
        picture(fb);
        CHECK(memcmp(fb, fb_was, (size_t)W * H * sizeof(gfx_color_t)) == 0);
        ui_router_render(&cv1, 1);
    }
}

/* A bench disarmed while ARM's flash runs shows ARM in its own green once
 * the frames the flash had left are drawn, in each buffer. */
TEST_CASE(a_disarm_during_the_arm_flash_leaves_the_button_green)
{
    static gfx_color_t *other;
    if (other == NULL) {
        other = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    gfx_canvas_t cv1;
    gfx_canvas_init(&cv1, other, W, H, W);
    fresh();
    ui_router_render(&cv, 0);
    ui_router_render(&cv1, 1);
    CHECK(arm());
    ui_router_tick(0.026f);
    ui_router_render(&cv, 0);                   /* one frame of the flash */
    motor_screen_set_armed(false);
    for (int i = 1; i < 2 * UI_HOLD_FLASH_FRAMES + 2; ++i) {
        ui_router_tick(0.026f);
        ui_router_render((i & 1) ? &cv1 : &cv, i & 1);
    }
    const gfx_color_t ok = ui_theme_color(UI_C_OK);
    CHECK_EQ(px(ARM_X - 90, ARM_Y + 8), ok);
    CHECK_EQ(other[(size_t)(ARM_Y + 8) * W + (ARM_X - 90)], ok);
    picture(fb);
    CHECK_EQ(px(ARM_X - 90, ARM_Y + 8), ok);
}

/* ------------------------------------------------ what stays as it was */

/* ARM, RESET PEAKS and the tabs are not throttles: a disarmed bench takes
 * them. */
TEST_CASE(arm_reset_peaks_and_the_tabs_work_while_disarmed)
{
    fresh();
    feed_tap(FEED_LONE, RESET_X, RESET_Y);
    CHECK_EQ(took().kind, MOTOR_CMD_RESET_PEAKS);
    picture(fb_was);
    feed_tap(FEED_LONE, 6 + 124 / 2 + 31, P(11));       /* TABLE */
    CHECK(!picture_unchanged());
    CHECK(arm());
    CHECK(disarm());
    CHECK(arm());
}

/* The application's hook sets the slider whatever the bench is: the panel
 * and the tools that draw the screen use it, and it posts nothing. */
TEST_CASE(the_applications_hook_is_not_a_control)
{
    fresh();
    motor_screen_set_throttle(64.0f);
    CHECK_EQ(motor_screen_throttle(), 64.0f);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK(every_control_is_refused());
    CHECK_EQ(motor_screen_throttle(), 64.0f);
}

int main(void)
{
    RUN(no_throttle_control_moves_the_value_before_the_first_arm);
    RUN(no_throttle_control_moves_the_value_after_a_disarm);
    RUN(no_throttle_control_moves_the_value_before_the_bench_answers);
    RUN(nothing_moves_the_throttle_under_a_disarm_not_yet_taken);
    RUN(nothing_moves_the_throttle_between_a_disarm_and_its_answer);
    RUN(a_drag_ends_where_the_disarm_is_asked);
    RUN(a_press_on_the_disarmed_track_is_no_drag_after_the_arm);
    RUN(the_first_input_after_an_arm_starts_from_zero);
    RUN(knob_turns_on_a_disarmed_bench_are_not_accumulated);
    RUN(a_disarm_returns_every_throttle_to_zero);
    RUN(a_throttle_not_yet_sent_is_dropped_by_a_disarm_and_by_a_stop);
    RUN(arm_drive_disarm_drag_arm_stop_drag_arm);
    RUN(disarm_stop_link_loss_and_leaving_each_end_at_zero);
    RUN(a_drag_does_not_cross_leaving_the_screen);
    RUN(the_throttle_is_dimmed_and_says_arm_first_while_disarmed);
    RUN(the_dimming_follows_the_arm_in_both_buffers);
    RUN(a_disarm_during_the_arm_flash_leaves_the_button_green);
    RUN(arm_reset_peaks_and_the_tabs_work_while_disarmed);
    RUN(the_applications_hook_is_not_a_control);
    return test_summary("motor_arm");
}
