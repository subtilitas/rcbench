/*
 * The SERVO screen's commanded value against the bench's armed state: no
 * input changes it while the bench is disarmed, and at every arm it is the
 * rest the pins drive.
 *
 * Touch is fed as the panel produces it: frames of contacts through the
 * tracker and the router (touch_feed.h).  The bench's answers -- armed,
 * disarmed, a stop, the link -- are given the way the panel's frame loop
 * gives them.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "gfx.h"
#include "outputs.h"
#include "servo_screen.h"
#include "settings.h"
#include "splash_screen.h"
#include "supply_screen.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H 480

/*
 * Geometry in panel coordinates, mirrored from servo_screen.c with the
 * band's height added: the shaft, the three buttons of the right card with
 * ARM under them, SPEED's track, and the SETTINGS overlay's rows.
 */
#define P(y)        (UI_BAND_H + (y))
#define SHAFT_X     300
#define SHAFT_Y     P((H - UI_BAND_H) / 2)
#define CENTRE_X    556
#define SWEEP_X     647
#define RELEASE_X   739
#define BTN_Y       P(366)
#define ARM_X       648
#define ARM_Y       P(404)
#define SPEED_Y     P(307)
#define SETB_X      734
#define SETB_Y      P(24)
#define CLOSE_X     439
#define CLOSE_Y     P(27)
#define ROW_L_X     100
#define ROW_Y(r)    P(71 + 42 * (r))
#define TRIM_UP_X   469
#define CH_X(i)     (130 + 239 * ((i) / 5))
#define CH_Y(i)     P(74 + 46 * ((i) % 5))

static gfx_color_t *fb;
static gfx_color_t *fb_was;
static gfx_canvas_t cv;

static servo_cmd_t took(void)
{
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
    servo_screen_take(&c);
    return c;
}

static void drain(void)
{
    while (took().kind != SERVO_CMD_NONE) { }
}

/* The SERVO screen up on a disarmed bench, nothing on the glass. */
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
    ui_router_goto(SCREEN_SERVO);
    feed_reset();
    /* Nothing measures the horn: it is drawn from the command. */
    servo_screen_feedback(0u, 0.0f, false);
    servo_screen_set_sweep(true);
    drain();
}

static void frames(int n)
{
    for (int i = 0; i < n; ++i) {
        ui_router_tick(0.026f);
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

static void dial_at(float deg, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    *x = SHAFT_X + (int)(110.0f * cosf(deg * k) + 0.5f);
    *y = SHAFT_Y - (int)(110.0f * sinf(deg * k) + 0.5f);
}

/* A press on the dial at @p deg, dragged to @p to and lifted. */
static void drag(float deg, float to)
{
    int x, y, x2, y2;
    dial_at(deg, &x, &y);
    dial_at(to, &x2, &y2);
    finger(FEED_LONE, x, y);
    glide(FEED_LONE, x2, y2, 8);
    lift(FEED_LONE);
}

/*
 * ARM held until it posts, and the bench's answer.  The range the arm
 * carries is kept: the panel configures the channels from it and centres
 * them before it arms.
 */
static uint16_t g_min_us, g_max_us;

static bool arm(void)
{
    finger(FEED_LONE, ARM_X, ARM_Y);
    frames((int)(2.3f / 0.026f));
    const servo_cmd_t c = took();
    lift(FEED_LONE);
    if (c.kind != SERVO_CMD_ARM) {
        return false;
    }
    g_min_us = c.min_us;
    g_max_us = c.max_us;
    servo_screen_set_armed(true);
    return true;
}

/* DISARM tapped, and the bench's answer. */
static bool disarm(void)
{
    feed_tap(FEED_LONE, ARM_X, ARM_Y);
    const bool asked = took().kind == SERVO_CMD_DISARM;
    servo_screen_set_armed(false);
    return asked;
}

/* The band's STOP tapped, then what the panel's frame loop does with a
 * stop: both the gesture's end and the bench's answer. */
static bool stop(void)
{
    const gfx_rect_t r = ui_band_stop_rect();
    feed_tap(FEED_LONE, r.x + r.w / 2, r.y + r.h / 2);
    const bool pressed = ui_router_take_stop();
    servo_screen_cancel_arm();
    servo_screen_set_armed(false);
    return pressed;
}

/* One turn of the knob as the panel applies it: a frame, then the turn. */
static void knob(float span)
{
    servo_screen_knob_frame();
    servo_screen_knob(span);
}

/*
 * The pulse the outputs layer renders on an armed surface nobody has
 * commanded, across the endpoints the arm carried: the channel's rest.
 */
static uint16_t rest_us(void)
{
    outputs_t o;
    outputs_init(&o, 0u);
    (void)outputs_set_role(&o, 0u, OUT_ROLE_SURFACE);
    (void)outputs_set_endpoints(&o, 0u, g_min_us, g_max_us);
    outputs_arm(&o, true, 0u);
    outputs_step(&o, OUT_DEFAULT_TIMEOUT_MS + 1u);
    return outputs_pulse_us(&o, 0u);
}

static void open_settings(void)  { feed_tap(FEED_LONE, SETB_X, SETB_Y); }
static void close_settings(void) { feed_tap(FEED_LONE, CLOSE_X, CLOSE_Y); }

/* NARROW 760 chosen on the OUTPUT page: 660 to 860 us. */
static void choose_narrow(void)
{
    open_settings();
    feed_tap(FEED_LONE, ROW_L_X, ROW_Y(0));
    feed_tap(FEED_LONE, CH_X(1), CH_Y(1));
    close_settings();
}

/* Every position control at once, each refused: the dial pressed and
 * dragged, the knob turned both ways, CENTRE, SWEEP and the hook the
 * application has.  True when none posted or moved anything. */
static bool every_input_is_refused(void)
{
    const uint16_t value = servo_screen_commanded();
    const uint16_t drawn = servo_screen_drawn();
    picture(fb_was);
    bool quiet = true;

    drag(-60.0f, 60.0f);
    quiet = quiet && took().kind == SERVO_CMD_NONE;
    knob(0.25f);
    quiet = quiet && took().kind == SERVO_CMD_NONE;
    knob(-0.5f);
    quiet = quiet && took().kind == SERVO_CMD_NONE;
    feed_tap(FEED_LONE, CENTRE_X, BTN_Y);
    quiet = quiet && took().kind == SERVO_CMD_NONE;
    feed_tap(FEED_LONE, SWEEP_X, BTN_Y);
    quiet = quiet && took().kind == SERVO_CMD_NONE;
    servo_screen_set_commanded(25.0f);
    quiet = quiet && took().kind == SERVO_CMD_NONE;
    frames(40);
    quiet = quiet && took().kind == SERVO_CMD_NONE;

    return quiet && servo_screen_commanded() == value
           && servo_screen_drawn() == drawn && !servo_screen_sweeping()
           && picture_unchanged();
}

/* ------------------------------------------------------------- refusals */

/* A bench never armed: the value is the centre it starts at, and stays. */
TEST_CASE(no_position_control_moves_the_value_before_the_first_arm)
{
    fresh();
    CHECK_EQ(servo_screen_commanded(), 1500);
    CHECK(every_input_is_refused());
    CHECK_EQ(servo_screen_commanded(), 1500);
}

/* Disarmed after a move: the value stays the one last driven, 40 degrees,
 * whatever is pressed, turned or tapped. */
TEST_CASE(no_position_control_moves_the_value_after_a_disarm)
{
    fresh();
    CHECK(arm());
    drag(10.0f, 40.0f);
    const uint16_t driven = took().value_us;
    CHECK(driven > 1700);
    frames(40);
    CHECK(disarm());
    CHECK_EQ(servo_screen_commanded(), driven);
    CHECK(every_input_is_refused());
    CHECK_EQ(servo_screen_commanded(), driven);
}

/* A press that began on the disarmed dial is not a drag once the bench
 * arms under it: the first input after an arm is a new press. */
TEST_CASE(a_press_on_the_disarmed_dial_is_no_drag_after_the_arm)
{
    fresh();
    int x, y, x2, y2;
    dial_at(30.0f, &x, &y);
    dial_at(50.0f, &x2, &y2);
    finger(1, x, y);                    /* refused */
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    finger(2, ARM_X, ARM_Y);            /* a second finger holds ARM */
    frames((int)(2.3f / 0.026f));
    CHECK_EQ(took().kind, SERVO_CMD_ARM);
    servo_screen_set_armed(true);
    glide(1, x2, y2, 8);
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), 1500);
    lift(1);
    lift(2);
    drain();
    /* And the next press is taken. */
    finger(FEED_LONE, x, y);
    CHECK_EQ(took().kind, SERVO_CMD_POSITION);
    lift(FEED_LONE);
}

/* A drag under way when the bench disarms ends there: armed again under
 * the same finger, its moves command nothing. */
TEST_CASE(a_drag_does_not_cross_a_disarm_and_the_next_arm)
{
    fresh();
    CHECK(arm());
    int x, y, x2, y2;
    dial_at(30.0f, &x, &y);
    dial_at(50.0f, &x2, &y2);
    finger(1, x, y);
    CHECK_EQ(took().kind, SERVO_CMD_POSITION);
    servo_screen_set_armed(false);      /* the far end disarmed */
    servo_screen_set_armed(true);
    drain();
    glide(1, x2, y2, 8);
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), 1500);
    lift(1);
}

/* Under a disarm waiting to be taken every position is dropped, so the
 * value does not move either. */
TEST_CASE(nothing_moves_the_value_under_a_disarm_not_yet_taken)
{
    fresh();
    CHECK(arm());
    drag(10.0f, 40.0f);
    const uint16_t driven = took().value_us;
    feed_tap(FEED_LONE, ARM_X, ARM_Y);  /* DISARM, not yet taken */
    drag(-60.0f, -30.0f);
    knob(0.2f);
    feed_tap(FEED_LONE, CENTRE_X, BTN_Y);
    CHECK_EQ(servo_screen_commanded(), driven);
    CHECK_EQ(took().kind, SERVO_CMD_DISARM);
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
}

/*
 * A DISARM taken and not yet answered: the bench still reads armed here for
 * the frames its answer takes.  Every position stays refused through them,
 * and RELEASE is sent and moves no value.  The bench's report of the disarm
 * leaves the value last driven, and the arm after it takes input again.
 */
TEST_CASE(nothing_moves_the_value_between_a_disarm_and_its_answer)
{
    fresh();
    CHECK(arm());
    drag(10.0f, 40.0f);
    const uint16_t driven = took().value_us;
    CHECK(driven > 1700);
    frames(40);
    feed_tap(FEED_LONE, ARM_X, ARM_Y);
    CHECK_EQ(took().kind, SERVO_CMD_DISARM);
    for (int frame = 0; frame < 5; ++frame) {
        servo_screen_set_armed(true);           /* not yet answered */
        drag(-60.0f, -30.0f);
        knob(0.2f);
        feed_tap(FEED_LONE, CENTRE_X, BTN_Y);
        feed_tap(FEED_LONE, SWEEP_X, BTN_Y);
        CHECK_EQ(took().kind, SERVO_CMD_NONE);
        CHECK_EQ(servo_screen_commanded(), driven);
        feed_tap(FEED_LONE, RELEASE_X, BTN_Y);
        CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
        CHECK_EQ(servo_screen_commanded(), driven);
    }
    servo_screen_set_armed(false);
    CHECK_EQ(servo_screen_commanded(), driven);
    CHECK(every_input_is_refused());
    CHECK(arm());
    CHECK_EQ(servo_screen_commanded(), 1500);
    knob(0.01f);
    CHECK_EQ(took().value_us, 1510);
}

/* ---------------------------------------------------------- the arm edge */

/* STANDARD PWM: the arm sets the value to the rest the outputs layer
 * renders, 1500 us, from wherever the last run left it. */
TEST_CASE(an_arm_sets_the_value_to_the_channels_rest)
{
    fresh();
    CHECK(arm());
    CHECK_EQ(rest_us(), 1500);
    CHECK_EQ(servo_screen_commanded(), rest_us());
    drag(10.0f, 40.0f);
    drain();
    frames(40);
    CHECK(disarm());
    CHECK(servo_screen_commanded() > 1700);     /* the last value driven */
    CHECK(arm());
    CHECK_EQ(servo_screen_commanded(), rest_us());
    /* And the horn is drawn there at once: the pin does not slew to a
     * rest it is at. */
    CHECK_EQ(servo_screen_drawn(), rest_us());
}

/* NARROW 760, 660 to 860 us: the rest is that range's midpoint, 760 us. */
TEST_CASE(an_arm_on_a_narrow_servo_sets_the_value_to_760_us)
{
    fresh();
    choose_narrow();
    CHECK(arm());
    CHECK_EQ(g_min_us, 660);
    CHECK_EQ(g_max_us, 860);
    CHECK_EQ(rest_us(), 760);
    CHECK_EQ(servo_screen_commanded(), 760);
    drag(10.0f, 40.0f);
    const uint16_t driven = took().value_us;
    CHECK(driven > 790 && driven <= 860);
    frames(40);
    CHECK(disarm());
    CHECK_EQ(servo_screen_commanded(), driven);
    CHECK(arm());
    CHECK_EQ(servo_screen_commanded(), 760);
    CHECK_EQ(servo_screen_drawn(), 760);
    /* The first 1 % of the knob from there: 1.8 degrees, 2 us. */
    knob(0.01f);
    const servo_cmd_t c = took();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, 762);
}

/* The rest is the midpoint of the endpoints, which TRIM does not move: a
 * trimmed servo arms at 1500 us, and a position at 0 degrees is 1520 us. */
TEST_CASE(an_arm_sets_the_rest_and_not_the_trimmed_centre)
{
    fresh();
    open_settings();
    for (int i = 0; i < 4; ++i) {
        feed_tap(FEED_LONE, TRIM_UP_X, ROW_Y(3));       /* +20 us */
    }
    close_settings();
    CHECK(arm());
    CHECK_EQ(rest_us(), 1500);
    CHECK_EQ(servo_screen_commanded(), 1500);
    int x, y;
    dial_at(0.0f, &x, &y);
    feed_tap(FEED_LONE, x, y);
    CHECK_EQ(took().value_us, 1520);
}

/* A profile changed on a resting armed bench moves the rest, and the
 * value with it: the release it posts puts the pin there. */
TEST_CASE(the_value_follows_the_rest_through_a_change_of_type)
{
    fresh();
    open_settings();
    for (int i = 0; i < 4; ++i) {
        feed_tap(FEED_LONE, TRIM_UP_X, ROW_Y(3));       /* +20 us */
    }
    close_settings();
    CHECK(arm());
    CHECK_EQ(servo_screen_commanded(), 1500);
    choose_narrow();
    CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
    CHECK_EQ(servo_screen_commanded(), 760);
    /* Once something is commanded the value is that command's. */
    int x, y;
    dial_at(0.0f, &x, &y);
    feed_tap(FEED_LONE, x, y);
    CHECK_EQ(took().value_us, 780);
    open_settings();
    feed_tap(FEED_LONE, TRIM_UP_X, ROW_Y(3));           /* +25 us */
    close_settings();
    CHECK_EQ(took().value_us, 785);
    CHECK_EQ(servo_screen_commanded(), 785);
}

/* The first 1 % turn of the knob after an arm posts the rest plus 1 %:
 * 1.8 degrees of 90, 10 us of 500.  Not the value before the disarm. */
TEST_CASE(the_first_knob_turn_after_an_arm_starts_from_the_rest)
{
    fresh();
    CHECK(arm());
    drag(60.0f, 70.0f);                 /* 1889 us */
    CHECK(took().value_us > 1880);
    frames(40);
    CHECK(disarm());
    CHECK(arm());
    knob(0.01f);
    const servo_cmd_t c = took();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, 1510);
    CHECK_EQ(servo_screen_commanded(), 1510);
}

/* The first press on the dial after an arm: the horn is drawn from the
 * rest towards it, at SPEED, not from where the last run ended. */
TEST_CASE(the_first_dial_drag_after_an_arm_starts_from_the_rest)
{
    fresh();
    CHECK(arm());
    drag(-70.0f, -60.0f);               /* 1167 us */
    drain();
    frames(40);
    CHECK(disarm());
    feed_tap(FEED_LONE, 521, SPEED_Y);  /* SPEED near its 10 % end */
    CHECK(arm());
    CHECK_EQ(servo_screen_drawn(), 1500);
    int x, y;
    dial_at(20.0f, &x, &y);
    finger(FEED_LONE, x, y);
    const servo_cmd_t c = took();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK(c.value_us > 1600 && c.value_us < 1620);
    ui_router_tick(0.026f);
    const uint16_t early = servo_screen_drawn();
    CHECK(early >= 1500 && early < c.value_us);
    lift(FEED_LONE);
}

/* Turns made while disarmed are not kept: 50 of them, then an arm, and
 * the value is the rest; the next 1 % is 1 % from it. */
TEST_CASE(knob_turns_on_a_disarmed_bench_are_not_accumulated)
{
    fresh();
    for (int i = 0; i < 50; ++i) {
        knob(0.01f);
        ui_router_tick(0.026f);
    }
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), 1500);
    CHECK(arm());
    CHECK_EQ(servo_screen_commanded(), 1500);
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    knob(0.01f);
    CHECK_EQ(took().value_us, 1510);

    /* And across a disarm between two runs. */
    CHECK(disarm());
    for (int i = 0; i < 50; ++i) {
        knob(-0.01f);
    }
    CHECK_EQ(servo_screen_commanded(), 1510);
    CHECK(arm());
    knob(-0.01f);
    CHECK_EQ(took().value_us, 1490);
}

/* The knob's turn is posted at the end of a frame and sent with the next.
 * A disarm or a stop seen in between withdraws it: nothing is sent to the
 * disarmed bench, and the value is the one driven before the turn. */
TEST_CASE(a_knob_turn_not_yet_sent_is_withdrawn_by_a_disarm_and_by_a_stop)
{
    for (int by_stop = 0; by_stop < 2; ++by_stop) {
        fresh();
        CHECK(arm());
        drag(10.0f, 40.0f);
        const uint16_t driven = took().value_us;
        knob(0.05f);
        CHECK(servo_screen_commanded() != driven);
        if (by_stop) {
            servo_screen_cancel_arm();
            CHECK_EQ(took().kind, SERVO_CMD_NONE);
            CHECK_EQ(servo_screen_commanded(), driven);
        }
        servo_screen_set_armed(false);
        CHECK_EQ(took().kind, SERVO_CMD_NONE);
        CHECK_EQ(servo_screen_commanded(), driven);
    }
}

/* ------------------------------------------------- every way out, in turn */

/*
 * A run each way it ends -- DISARM, STOP, the link going, another screen --
 * and the arm after it.  Disarmed, the value shown is the one last driven
 * and nothing moves it; armed again, it is the rest.
 */
TEST_CASE(disarm_stop_link_loss_and_leaving_each_end_at_the_rest_on_rearm)
{
    fresh();
    const float at[4] = { 40.0f, -35.0f, 65.0f, -80.0f };
    for (int way = 0; way < 4; ++way) {
        CHECK(arm());
        CHECK_EQ(servo_screen_commanded(), 1500);
        CHECK_EQ(took().kind, SERVO_CMD_NONE);
        drag(at[way] - 10.0f, at[way]);
        const uint16_t driven = took().value_us;
        CHECK(driven != 1500);
        frames(40);
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
            servo_screen_set_link(false);
            servo_screen_cancel_arm();
            servo_screen_set_armed(false);
            break;
        default:
            /* Leaving disarms; the bench reports armed for one more frame
             * and then disarmed. */
            ui_router_goto(SCREEN_OVERVIEW);
            CHECK_EQ(took().kind, SERVO_CMD_DISARM);
            servo_screen_set_armed(true);
            servo_screen_set_armed(false);
            ui_router_goto(SCREEN_SERVO);
            break;
        }
        CHECK_EQ(took().kind, SERVO_CMD_NONE);
        CHECK_EQ(servo_screen_commanded(), driven);
        CHECK(every_input_is_refused());
        CHECK_EQ(servo_screen_commanded(), driven);
        servo_screen_set_link(true);
    }
    CHECK(arm());
    CHECK_EQ(servo_screen_commanded(), 1500);
    CHECK_EQ(servo_screen_drawn(), 1500);
}

/* ------------------------------------------------------------- the sweep */

static uint16_t start_sweep(void)
{
    feed_tap(FEED_LONE, SWEEP_X, BTN_Y);
    const servo_cmd_t c = took();
    servo_screen_sweep_started(c.start_seq, 0u, SERVO_SWEEP_FROM_REST, 0u);
    return (uint16_t)c.kind;
}

/* A sweep ended by a disarm: nothing waits to be sent, the value stops
 * where the curve had it, and the arm after it is at the rest. */
TEST_CASE(a_sweep_ended_by_a_disarm_leaves_nothing_to_send)
{
    fresh();
    CHECK(arm());
    CHECK_EQ(start_sweep(), SERVO_CMD_SWEEP);
    frames(12);
    CHECK(servo_screen_sweeping());
    servo_screen_set_armed(false);
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    const uint16_t left = servo_screen_commanded();
    frames(80);
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), left);
    CHECK(every_input_is_refused());
    CHECK(arm());
    CHECK_EQ(servo_screen_commanded(), 1500);
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    CHECK(!servo_screen_sweeping());
}

/* PAUSE tapped and the bench disarmed before its HOLD is taken: the HOLD
 * is not sent, and a second tap neither undoes the pause into a running
 * sweep nor starts one. */
TEST_CASE(a_pause_not_yet_sent_is_dropped_by_a_disarm)
{
    fresh();
    CHECK(arm());
    CHECK_EQ(start_sweep(), SERVO_CMD_SWEEP);
    frames(12);
    feed_tap(FEED_LONE, SWEEP_X, BTN_Y);        /* PAUSE: a HOLD waits */
    CHECK(servo_screen_paused());
    servo_screen_set_armed(false);
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    feed_tap(FEED_LONE, SWEEP_X, BTN_Y);
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
}

/* A SWEEP tapped and the bench stopped before it is taken: not sent. */
TEST_CASE(a_sweep_not_yet_sent_is_dropped_by_a_stop)
{
    fresh();
    CHECK(arm());
    feed_tap(FEED_LONE, SWEEP_X, BTN_Y);
    CHECK(servo_screen_sweeping());
    CHECK(stop());
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
}

/* ---------------------------------------------------------- the picture */

static gfx_color_t px(int x, int y) { return fb[(size_t)y * W + x]; }

/* Pixels of the warning colour where the left card says ARM FIRST, under
 * the angle. */
static int arm_first_px(void)
{
    int n = 0;
    for (int y = P(396); y < P(412); ++y) {
        for (int x = 52; x < 200; ++x) {
            if (px(x, y) == ui_theme_color(UI_C_WARN)) {
                ++n;
            }
        }
    }
    return n;
}

/* CENTRE is dimmed with SWEEP while the bench is disarmed, the horn is
 * drawn dimmed and the left card says ARM FIRST; armed, CENTRE and the horn
 * are the accent and the line is gone. */
TEST_CASE(centre_and_the_horn_are_dimmed_while_disarmed)
{
    fresh();
    picture(fb);
    const gfx_color_t accent = ui_theme_color(UI_C_ACCENT);
    /* Inside CENTRE, clear of its label; and on the horn's boss. */
    CHECK(px(CENTRE_X - 36, BTN_Y - 10) != accent);
    CHECK(px(SHAFT_X + 22, SHAFT_Y) != accent);
    CHECK(arm_first_px() > 0);
    CHECK(arm());
    frames(2 * UI_HOLD_FLASH_FRAMES);
    picture(fb);
    CHECK_EQ(px(CENTRE_X - 36, BTN_Y - 10), accent);
    CHECK_EQ(px(SHAFT_X + 22, SHAFT_Y), accent);
    CHECK_EQ(arm_first_px(), 0);
    CHECK(disarm());
    picture(fb);
    CHECK(px(CENTRE_X - 36, BTN_Y - 10) != accent);
    CHECK(px(SHAFT_X + 22, SHAFT_Y) != accent);
    CHECK(arm_first_px() > 0);
}

/* A bench disarmed while ARM's flash runs shows ARM in its own green, not
 * the colour the flash was to settle on. */
TEST_CASE(a_disarm_during_the_arm_flash_leaves_the_button_green)
{
    fresh();
    CHECK(arm());
    ui_router_tick(0.026f);
    ui_router_render(&cv, 0);                   /* one frame of the flash */
    servo_screen_set_armed(false);
    for (int i = 0; i < 2 * UI_HOLD_FLASH_FRAMES; ++i) {
        ui_router_tick(0.026f);
        ui_router_render(&cv, 0);
    }
    const gfx_color_t drawn = px(ARM_X - 120, ARM_Y - 8);
    picture(fb);
    CHECK_EQ(px(ARM_X - 120, ARM_Y - 8), ui_theme_color(UI_C_OK));
    CHECK_EQ(drawn, ui_theme_color(UI_C_OK));
}

/* ------------------------------------------------ what stays as it was */

/* RELEASE is not a position: it is taken on a disarmed bench too, and
 * moves no value.  SPEED and the settings stay open to a disarmed bench. */
TEST_CASE(release_speed_and_the_settings_work_while_disarmed)
{
    fresh();
    feed_tap(FEED_LONE, RELEASE_X, BTN_Y);
    CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
    CHECK_EQ(servo_screen_commanded(), 1500);
    choose_narrow();
    CHECK_STR_EQ(servo_screen_type_name(), "NARROW 760");
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
}

/* ------------------------------------------------------------- RELEASE */

/* The three profiles RELEASE is held to: STANDARD PWM, NARROW 760, and
 * STANDARD PWM trimmed by +20 us, whose rest is not its 0 degrees. */
enum { PROFILE_STANDARD = 0, PROFILE_NARROW, PROFILE_TRIMMED, PROFILE_COUNT };

static void choose_profile(int profile)
{
    if (profile == PROFILE_NARROW) {
        choose_narrow();
    } else if (profile == PROFILE_TRIMMED) {
        open_settings();
        for (int i = 0; i < 4; ++i) {
            feed_tap(FEED_LONE, TRIM_UP_X, ROW_Y(3));   /* +20 us */
        }
        close_settings();
    }
    drain();
}

/* The profile's rest, and the first 1 % of the knob from it: 1.8 degrees,
 * 10 us of a 500 us half-travel and 2 us of a 100 us one. */
static const uint16_t k_rest[PROFILE_COUNT]     = { 1500, 760, 1500 };
static const uint16_t k_rest_1pct[PROFILE_COUNT] = { 1510, 762, 1510 };

/* An armed bench of @p profile, driven 30 degrees off its centre. */
static uint16_t armed_and_driven(int profile)
{
    fresh();
    choose_profile(profile);
    if (!arm()) {
        return 0u;
    }
    drag(10.0f, 30.0f);
    const uint16_t driven = took().value_us;
    frames(40);
    return driven;
}

/* RELEASE on an armed bench: the value is the rest the pins go to, as at
 * an arm, and the horn is drawn there. */
TEST_CASE(release_on_an_armed_bench_sets_the_value_to_the_rest)
{
    for (int profile = 0; profile < PROFILE_COUNT; ++profile) {
        const uint16_t driven = armed_and_driven(profile);
        CHECK(driven > k_rest[profile] + 20);
        CHECK_EQ(servo_screen_commanded(), driven);
        feed_tap(FEED_LONE, RELEASE_X, BTN_Y);
        CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
        CHECK_EQ(took().kind, SERVO_CMD_NONE);
        CHECK_EQ(rest_us(), k_rest[profile]);
        CHECK_EQ(servo_screen_commanded(), k_rest[profile]);
        frames(2);
        CHECK_EQ(servo_screen_drawn(), k_rest[profile]);
        CHECK_EQ(took().kind, SERVO_CMD_NONE);
    }
}

/* The first 1 % turn of the knob after a RELEASE posts the rest plus 1 %,
 * not the value before it plus 1 %. */
TEST_CASE(the_first_knob_turn_after_a_release_starts_from_the_rest)
{
    for (int profile = 0; profile < PROFILE_COUNT; ++profile) {
        CHECK(armed_and_driven(profile) != 0u);
        feed_tap(FEED_LONE, RELEASE_X, BTN_Y);
        CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
        knob(0.01f);
        const servo_cmd_t c = took();
        CHECK_EQ(c.kind, SERVO_CMD_POSITION);
        CHECK_EQ(c.value_us, k_rest_1pct[profile]);
        CHECK_EQ(servo_screen_commanded(), k_rest_1pct[profile]);
    }
}

/* The first press on the dial after a RELEASE: the position under the
 * finger, and the horn drawn from the rest towards it at SPEED. */
TEST_CASE(the_first_dial_drag_after_a_release_starts_from_the_rest)
{
    for (int profile = 0; profile < PROFILE_COUNT; ++profile) {
        const uint16_t driven = armed_and_driven(profile);
        CHECK(driven != 0u);
        feed_tap(FEED_LONE, 521, SPEED_Y);  /* SPEED near its 10 % end */
        drain();
        feed_tap(FEED_LONE, RELEASE_X, BTN_Y);
        CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
        frames(200);
        CHECK_EQ(servo_screen_drawn(), k_rest[profile]);
        int x, y, x2, y2;
        dial_at(60.0f, &x, &y);
        dial_at(70.0f, &x2, &y2);
        finger(FEED_LONE, x, y);
        const servo_cmd_t c = took();
        CHECK_EQ(c.kind, SERVO_CMD_POSITION);
        CHECK(c.value_us > driven);
        ui_router_tick(0.026f);
        const uint16_t early = servo_screen_drawn();
        CHECK(early >= k_rest[profile] && early < c.value_us);
        glide(FEED_LONE, x2, y2, 8);
        CHECK(took().value_us > c.value_us);
        lift(FEED_LONE);
    }
}

/* RELEASE tapped by a second finger while the first drags the dial: the
 * drag ends, and its moves after it command nothing. */
TEST_CASE(a_release_ends_a_drag_under_way)
{
    fresh();
    CHECK(arm());
    int x, y, x2, y2;
    dial_at(20.0f, &x, &y);
    dial_at(50.0f, &x2, &y2);
    finger(1, x, y);
    CHECK_EQ(took().kind, SERVO_CMD_POSITION);
    feed_tap(2, RELEASE_X, BTN_Y);
    CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
    CHECK_EQ(servo_screen_commanded(), 1500);
    glide(1, x2, y2, 8);
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), 1500);
    lift(1);
    /* And the next press is taken. */
    finger(FEED_LONE, x2, y2);
    CHECK_EQ(took().kind, SERVO_CMD_POSITION);
    lift(FEED_LONE);
}

/*
 * One command waits at a time, so within a frame the later of a position
 * and a RELEASE is the one sent.  A position and then RELEASE: the release
 * goes, the position does not, and the value is the rest.
 */
TEST_CASE(a_release_after_a_position_in_one_frame_sends_the_release)
{
    for (int by_knob = 0; by_knob < 2; ++by_knob) {
        fresh();
        CHECK(arm());
        if (by_knob) {
            knob(0.2f);
        } else {
            int x, y;
            dial_at(40.0f, &x, &y);
            feed_tap(FEED_LONE, x, y);
        }
        CHECK(servo_screen_commanded() > 1600);
        feed_tap(FEED_LONE, RELEASE_X, BTN_Y);
        /* A withdrawal of the knob's turn puts nothing back. */
        servo_screen_knob_cancel();
        CHECK_EQ(servo_screen_commanded(), 1500);
        CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
        CHECK_EQ(took().kind, SERVO_CMD_NONE);
    }
}

/* RELEASE and then a press on the dial: the position goes, the release
 * does not, and the value is the position.  The knob does not post over a
 * RELEASE: its turn is dropped and the release goes. */
TEST_CASE(a_position_after_a_release_in_one_frame_sends_the_position)
{
    fresh();
    CHECK(arm());
    drag(10.0f, 30.0f);
    drain();
    feed_tap(FEED_LONE, RELEASE_X, BTN_Y);
    int x, y;
    dial_at(-40.0f, &x, &y);
    feed_tap(FEED_LONE, x, y);
    const servo_cmd_t c = took();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK(c.value_us < 1400);
    CHECK_EQ(servo_screen_commanded(), c.value_us);
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    /* Commanded, so no longer the rest: a trim says the position again. */
    open_settings();
    feed_tap(FEED_LONE, TRIM_UP_X, ROW_Y(3));           /* +5 us */
    close_settings();
    CHECK_EQ(took().value_us, c.value_us + 5);

    feed_tap(FEED_LONE, RELEASE_X, BTN_Y);
    knob(0.2f);
    CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), 1500);
}

/* After a RELEASE the value follows the rest through a change of type, as
 * after an arm: the release the change posts puts the pin there. */
TEST_CASE(the_value_follows_the_rest_through_a_change_of_type_after_a_release)
{
    CHECK(armed_and_driven(PROFILE_STANDARD) != 0u);
    feed_tap(FEED_LONE, RELEASE_X, BTN_Y);
    CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
    choose_narrow();
    CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
    CHECK_EQ(servo_screen_commanded(), 760);
    knob(0.01f);
    CHECK_EQ(took().value_us, 762);
}

/* RELEASE moves no value where the pins do not follow: on a bench disarmed
 * after a move, and behind a disarm waiting to be taken, which it does not
 * replace. */
TEST_CASE(release_moves_no_value_on_a_bench_that_is_not_armed)
{
    const uint16_t driven = armed_and_driven(PROFILE_STANDARD);
    CHECK(driven > 1600);
    feed_tap(FEED_LONE, ARM_X, ARM_Y);          /* DISARM, not yet taken */
    feed_tap(FEED_LONE, RELEASE_X, BTN_Y);
    CHECK_EQ(servo_screen_commanded(), driven);
    CHECK_EQ(took().kind, SERVO_CMD_DISARM);
    CHECK_EQ(took().kind, SERVO_CMD_NONE);
    servo_screen_set_armed(false);
    feed_tap(FEED_LONE, RELEASE_X, BTN_Y);
    CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
    CHECK_EQ(servo_screen_commanded(), driven);
}

/* Released on an armed bench the value is the rest, and a press on the
 * dial drives the servo again. */
TEST_CASE(a_press_on_the_dial_drives_a_released_servo_again)
{
    fresh();
    CHECK(arm());
    drag(10.0f, 40.0f);
    const uint16_t driven = took().value_us;
    CHECK(driven > 1700);
    feed_tap(FEED_LONE, RELEASE_X, BTN_Y);
    CHECK_EQ(took().kind, SERVO_CMD_RELEASE);
    CHECK_EQ(servo_screen_commanded(), 1500);
    int x, y;
    dial_at(-20.0f, &x, &y);
    feed_tap(FEED_LONE, x, y);
    const servo_cmd_t c = took();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK(c.value_us < 1500);
}

int main(void)
{
    RUN(no_position_control_moves_the_value_before_the_first_arm);
    RUN(no_position_control_moves_the_value_after_a_disarm);
    RUN(a_press_on_the_disarmed_dial_is_no_drag_after_the_arm);
    RUN(a_drag_does_not_cross_a_disarm_and_the_next_arm);
    RUN(nothing_moves_the_value_under_a_disarm_not_yet_taken);
    RUN(nothing_moves_the_value_between_a_disarm_and_its_answer);
    RUN(an_arm_sets_the_value_to_the_channels_rest);
    RUN(an_arm_on_a_narrow_servo_sets_the_value_to_760_us);
    RUN(an_arm_sets_the_rest_and_not_the_trimmed_centre);
    RUN(the_value_follows_the_rest_through_a_change_of_type);
    RUN(the_first_knob_turn_after_an_arm_starts_from_the_rest);
    RUN(the_first_dial_drag_after_an_arm_starts_from_the_rest);
    RUN(knob_turns_on_a_disarmed_bench_are_not_accumulated);
    RUN(a_knob_turn_not_yet_sent_is_withdrawn_by_a_disarm_and_by_a_stop);
    RUN(disarm_stop_link_loss_and_leaving_each_end_at_the_rest_on_rearm);
    RUN(a_sweep_ended_by_a_disarm_leaves_nothing_to_send);
    RUN(a_pause_not_yet_sent_is_dropped_by_a_disarm);
    RUN(a_sweep_not_yet_sent_is_dropped_by_a_stop);
    RUN(centre_and_the_horn_are_dimmed_while_disarmed);
    RUN(a_disarm_during_the_arm_flash_leaves_the_button_green);
    RUN(release_speed_and_the_settings_work_while_disarmed);
    RUN(release_on_an_armed_bench_sets_the_value_to_the_rest);
    RUN(the_first_knob_turn_after_a_release_starts_from_the_rest);
    RUN(the_first_dial_drag_after_a_release_starts_from_the_rest);
    RUN(a_release_ends_a_drag_under_way);
    RUN(a_release_after_a_position_in_one_frame_sends_the_release);
    RUN(a_position_after_a_release_in_one_frame_sends_the_position);
    RUN(the_value_follows_the_rest_through_a_change_of_type_after_a_release);
    RUN(release_moves_no_value_on_a_bench_that_is_not_armed);
    RUN(a_press_on_the_dial_drives_a_released_servo_again);
    return test_summary("servo_arm");
}
