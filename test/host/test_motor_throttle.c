/**
 * @file test_motor_throttle.c
 * @brief The bench screen's throttle at an arm and its TARGET row: every arm starts
 * from zero, no command or drag outlives the disarm, and the row names the
 * protocol and the pins, counts them, takes its colour from its state and
 * redraws when they change.
 *
 * Cases:
 * - arming_returns_the_throttle_to_zero
 * - arming_returns_every_throttle_to_zero
 * - the_readout_after_an_arm_is_that_of_an_untouched_screen
 * - the_hold_that_asks_for_the_arm_returns_the_throttle_to_zero
 * - a_throttle_set_after_the_ask_stays_when_the_bench_answers
 * - an_arm_after_a_dropped_ask_returns_the_throttle_to_zero
 * - every_ask_returns_the_throttle_to_zero
 * - a_hold_that_does_not_arm_leaves_the_throttle_alone
 * - a_throttle_waiting_at_the_disarm_is_dropped
 * - a_knob_command_waiting_at_the_disarm_is_not_put_back
 * - an_armed_report_repeated_keeps_the_throttle
 * - a_press_on_the_disarmed_track_is_no_drag_after_the_arm
 * - a_drag_under_way_ends_at_the_disarm
 * - every_arm_starts_from_zero
 * - the_target_row_names_the_protocol_and_the_pins
 * - many_throttle_pins_end_in_a_count_within_the_box
 * - the_target_row_texts_fit_the_box
 * - no_throttle_pin_and_an_unread_binding_are_warnings
 * - the_target_row_is_drawn_in_the_colour_of_its_state
 * - a_new_protocol_or_pin_set_redraws_the_row_and_a_throttle_keeps_it
 * - a_binding_that_reads_with_no_motor_pin_leaves_not_read
 * - a_changed_pin_set_redraws_the_controls_and_an_unchanged_one_does_not
 * - arm_with_no_motor_pin_is_dimmed_and_refused_with_its_reason
 * - a_pin_lost_under_a_held_arm_ends_the_hold
 * - disarm_stays_live_with_no_motor_pin
 *
 * SPDX-License-Identifier: MIT
 */

#include "motor_rig.h"

/* ------------------------------------------------- the throttle at an arm */

/*
 * The cases below feed the screen frames of contacts through the tracker
 * (touch_feed.h).  The feed works in panel coordinates and removes the
 * band's height, as the router does.
 */
#define P(y) ((y) + UI_BAND_H)

static void fed(void)
{
    fresh();
    feed_reset();
    feed_to_screen(scr);
}

/*
 * 60.5% on the slider of a disarmed bench.  No control puts a value there
 * (test_motor_arm.c); the application's hook does, and the cases below hold
 * the arm edge to what it does with one.
 */
static void set_disarmed(void)
{
    fed();
    motor_screen_set_throttle(60.5f);
    (void)last_cmd();
}

/*
 * An arm starts from nothing.  A slider at 60.5% on a disarmed bench is at
 * 0% once the bench is armed, and the step buttons, which move the
 * slider's value by one point, start from there: + posts 1.0%, not 61.5%.
 */
TEST_CASE(arming_returns_the_throttle_to_zero)
{
    set_disarmed();
    CHECK_NEAR(motor_screen_throttle(), 60.5f, 0.05f);

    motor_screen_set_armed(true);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    feed_tap(FEED_LONE, UP_X, P(TRACK_Y));
    motor_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
    CHECK_NEAR(c.value, 1.0f, 0.001f);
    feed_tap(FEED_LONE, DOWN_X, P(TRACK_Y));
    c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
    CHECK_NEAR(c.value, 0.0f, 0.001f);
    feed_tap(FEED_LONE, DOWN_X, P(TRACK_Y));
    CHECK_EQ(motor_screen_throttle(), 0.0f);    /* and no lower */

    /* The knob goes on from zero as well. */
    (void)last_cmd();
    motor_screen_knob_frame();
    motor_screen_knob(0.1f);
    CHECK_NEAR(last_cmd().value, 10.0f, 0.01f);
}

/* At the limits of the slider and one step inside each: every value a
 * disarmed bench can show arms to 0%. */
TEST_CASE(arming_returns_every_throttle_to_zero)
{
    static const float from[] = { 0.0f, 1.0f, 60.5f, 99.0f, 100.0f };
    for (size_t i = 0; i < sizeof(from) / sizeof(from[0]); ++i) {
        fed();
        motor_screen_set_throttle(from[i]);
        CHECK_EQ(motor_screen_throttle(), from[i]);
        motor_screen_set_armed(true);
        CHECK_EQ(motor_screen_throttle(), 0.0f);
    }
}

/*
 * The readout is the slider's value, so it reads 0.0 after the arm.  Held to
 * the picture of a screen that was never touched before its arm: the whole
 * frame is the same.
 */
TEST_CASE(the_readout_after_an_arm_is_that_of_an_untouched_screen)
{
    static gfx_color_t *untouched;
    if (untouched == NULL) {
        untouched = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    fed();
    motor_screen_set_armed(true);
    scr->render(&cv, 0);
    memcpy(untouched, fb, (size_t)W * H * sizeof(gfx_color_t));

    set_disarmed();
    scr->render(&cv, 0);                /* 60.5 on the glass */
    CHECK(memcmp(untouched, fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
    motor_screen_set_armed(true);
    scr->render(&cv, 0);
    CHECK(memcmp(untouched, fb, (size_t)W * H * sizeof(gfx_color_t)) == 0);
}

/*
 * From the ask, not only from the bench's answer: the hold that asks for
 * the arm returns the slider to zero itself.  Until the bench answers it is
 * not armed, and + in a frame that still reads it as disarmed is refused.
 */
TEST_CASE(the_hold_that_asks_for_the_arm_returns_the_throttle_to_zero)
{
    set_disarmed();
    finger(FEED_LONE, ARM_X, P(ARM_Y));
    tick_for(HOLD_TICKS - 10);
    CHECK_NEAR(motor_screen_throttle(), 60.5f, 0.05f);   /* not yet asked */
    tick_for(10);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    lift(FEED_LONE);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);

    feed_tap(FEED_LONE, UP_X, P(TRACK_Y));
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
}

/*
 * A throttle the application sets after the hold has asked is from after
 * the ask, and the bench's answer leaves it on the slider.  A drag in that
 * time is refused like any other on a bench not yet armed.
 */
TEST_CASE(a_throttle_set_after_the_ask_stays_when_the_bench_answers)
{
    set_disarmed();
    finger(FEED_LONE, ARM_X, P(ARM_Y));
    tick_for(HOLD_TICKS);
    lift(FEED_LONE);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
    CHECK_EQ(motor_screen_throttle(), 0.0f);

    finger(FEED_LONE, TRACK_X + 28, P(TRACK_Y));
    glide(FEED_LONE, TRACK_X + 128, P(TRACK_Y), 8);
    lift(FEED_LONE);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    motor_screen_set_throttle(24.0f);
    motor_screen_set_armed(true);
    CHECK_EQ(motor_screen_throttle(), 24.0f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

/*
 * An arm that was asked for and dropped -- by a stop, a touch loss or
 * leaving the screen -- is not the arm that comes later: that one returns
 * the slider to zero like any arm this screen's hold did not ask for.
 */
TEST_CASE(an_arm_after_a_dropped_ask_returns_the_throttle_to_zero)
{
    for (int how = 0; how < 3; ++how) {
        set_disarmed();
        finger(FEED_LONE, ARM_X, P(ARM_Y));
        tick_for(HOLD_TICKS);
        lift(FEED_LONE);
        CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
        switch (how) {
        case 0:  motor_screen_cancel_arm(); break;
        case 1:  scr->cancel(); break;
        default: scr->leave(); break;
        }
        (void)last_cmd();

        motor_screen_set_throttle(24.0f);
        motor_screen_set_armed(true);
        CHECK_EQ(motor_screen_throttle(), 0.0f);
    }
}

/* One run after another from the hold: the second ask starts from zero
 * too, whatever the first run and the disarmed bench left. */
TEST_CASE(every_ask_returns_the_throttle_to_zero)
{
    fed();
    for (int run = 0; run < 2; ++run) {
        motor_screen_set_throttle(48.5f);
        finger(FEED_LONE, ARM_X, P(ARM_Y));
        tick_for(HOLD_TICKS);
        lift(FEED_LONE);
        CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
        CHECK_EQ(motor_screen_throttle(), 0.0f);
        motor_screen_set_armed(true);
        CHECK_EQ(motor_screen_throttle(), 0.0f);
        motor_screen_set_throttle(0.0f);
        motor_screen_set_armed(false);
        (void)last_cmd();
    }
}

/* A hold abandoned before it asks leaves the slider as it was. */
TEST_CASE(a_hold_that_does_not_arm_leaves_the_throttle_alone)
{
    set_disarmed();
    const float before = motor_screen_throttle();
    finger(FEED_LONE, ARM_X, P(ARM_Y));
    tick_for(HOLD_TICKS / 2);
    lift(FEED_LONE);
    tick_for(HOLD_TICKS);
    CHECK_EQ(motor_screen_throttle(), before);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

/* A throttle posted on the armed bench and not yet collected as it disarms
 * is not sent to the disarmed bench, and the slider is at zero. */
TEST_CASE(a_throttle_waiting_at_the_disarm_is_dropped)
{
    fed();
    motor_screen_set_armed(true);
    finger(FEED_LONE, TRACK_X + 28, P(TRACK_Y));
    glide(FEED_LONE, TRACK_X + 128, P(TRACK_Y), 8);
    lift(FEED_LONE);
    CHECK(motor_screen_throttle() > 20.0f);
    motor_screen_set_armed(false);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
}

/* And a knob command waiting at the disarm: withdrawing it afterwards puts
 * no armed value back, on the disarmed bench or at the next arm. */
TEST_CASE(a_knob_command_waiting_at_the_disarm_is_not_put_back)
{
    fed();
    motor_screen_set_armed(true);
    motor_screen_knob_frame();
    motor_screen_knob(0.2f);
    (void)last_cmd();
    motor_screen_knob_frame();
    motor_screen_knob(0.2f);
    CHECK_NEAR(motor_screen_throttle(), 40.0f, 0.01f);
    motor_screen_set_armed(false);
    motor_screen_knob_cancel();
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    motor_screen_set_armed(true);
    motor_screen_knob_cancel();
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

/*
 * The application reports the armed bench on every frame.  Only the edge
 * returns the slider to zero: a throttle set on the armed bench stays, and
 * so does a drag under way.
 */
TEST_CASE(an_armed_report_repeated_keeps_the_throttle)
{
    fed();
    motor_screen_set_armed(true);
    finger(FEED_LONE, TRACK_X + 28, P(TRACK_Y));
    glide(FEED_LONE, TRACK_X + 128, P(TRACK_Y), 8);
    for (int i = 0; i < 3; ++i) {
        motor_screen_set_armed(true);
    }
    CHECK_NEAR(motor_screen_throttle(), 100.0f * 100.0f / (float)(MO_TRACK_W - 1), 0.01f);
    glide(FEED_LONE, TRACK_X + 168, P(TRACK_Y), 8);
    motor_screen_set_armed(true);
    lift(FEED_LONE);
    CHECK_NEAR(motor_screen_throttle(), 140.0f * 100.0f / (float)(MO_TRACK_W - 1), 0.01f);
    CHECK_NEAR(last_cmd().value, 140.0f * 100.0f / (float)(MO_TRACK_W - 1), 0.01f);
}

/*
 * A finger on the track as the bench arms: a second finger held ARM.  The
 * press was on a disarmed bench and took no drag, so its travel before and
 * after the arm moves nothing.  Pressed again, the track moves the throttle
 * by that press's travel.
 */
TEST_CASE(a_press_on_the_disarmed_track_is_no_drag_after_the_arm)
{
    fed();
    finger(0, TRACK_X + 28, P(TRACK_Y));
    glide(0, TRACK_X + 228, P(TRACK_Y), 8);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    motor_screen_set_armed(true);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    glide(0, TRACK_X + 236, P(TRACK_Y), 8);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    lift(0);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    finger(0, TRACK_X + 236, P(TRACK_Y));
    glide(0, TRACK_X + 276, P(TRACK_Y), 8);
    lift(0);
    CHECK_NEAR(motor_screen_throttle(), 40.0f * 100.0f / (float)(MO_TRACK_W - 1), 0.01f);
}

/* And at the disarm, where the application returns the value to zero. */
TEST_CASE(a_drag_under_way_ends_at_the_disarm)
{
    fed();
    motor_screen_set_armed(true);
    finger(FEED_LONE, TRACK_X + 28, P(TRACK_Y));
    glide(FEED_LONE, TRACK_X + 228, P(TRACK_Y), 8);
    CHECK(motor_screen_throttle() > 45.0f);   /* 200 of 421px */

    motor_screen_set_throttle(0.0f);    /* the panel's disarm edge */
    (void)last_cmd();
    motor_screen_set_armed(false);
    glide(FEED_LONE, TRACK_X + 236, P(TRACK_Y), 8);
    lift(FEED_LONE);
    CHECK_EQ(motor_screen_throttle(), 0.0f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

/* Run after run: each arm starts from zero, and the disarmed bench between
 * them takes no value from the track. */
TEST_CASE(every_arm_starts_from_zero)
{
    fed();
    for (int run = 0; run < 3; ++run) {
        motor_screen_set_armed(true);
        CHECK_EQ(motor_screen_throttle(), 0.0f);
        finger(FEED_LONE, TRACK_X + 28, P(TRACK_Y));
        glide(FEED_LONE, TRACK_X + 128, P(TRACK_Y), 8);
        lift(FEED_LONE);
        CHECK(motor_screen_throttle() > 20.0f);
        motor_screen_set_throttle(0.0f);
        (void)last_cmd();
        motor_screen_set_armed(false);
        /* Dragged again on the disarmed bench: refused. */
        finger(FEED_LONE, TRACK_X + 28, P(TRACK_Y));
        glide(FEED_LONE, TRACK_X + 228, P(TRACK_Y), 8);
        lift(FEED_LONE);
        CHECK_EQ(motor_screen_throttle(), 0.0f);
        CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    }
}

/* ---------------------------------------------------- the TARGET row */

/*
 * The CONTROL card's TARGET row, from motor_spots.h: the card's inner
 * column, x 648 to 1004, y 308 to 356, TARGET and its value box.
 */
#define TGT_X0 MO_RC_X

#define TGT_X1 (MO_RC_X + MO_RC_W)

#define TGT_Y0 MO_TARGET_T

#define TGT_Y1 (MO_TARGET_B + 1)

/* How many pixels of one colour the TARGET row's value box holds. */
static int target_pixels(gfx_color_t colour)
{
    int n = 0;
    for (int y = TGT_Y0; y < TGT_Y1; ++y) {
        for (int x = TGT_X0; x < TGT_X1; ++x) {
            n += (fb[y * W + x] == colour) ? 1 : 0;
        }
    }
    return n;
}

static void draw_once(void)
{
    scr->render(&cv, 0);
}

/* DSHOT300 is entry 4 of the outputs catalogue, MOTOR PWM entry 2. */
#define PROTO_MOTOR_PWM 2u

#define PROTO_DSHOT300  4u

#define PROTO_DSHOT600_BIDIR 7u

TEST_CASE(the_target_row_names_the_protocol_and_the_pins)
{
    char line[96];
    /* In the card's 25 cells (motor_target_cells()). */
    for (int l = 0; l < 2; ++l) {
        ui_text_set_language(l == 0 ? UI_LANG_EN : UI_LANG_DE);
        /* A protocol's name is the catalogue's, the same in both. */
        CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300,
                                     (1u << 0) | (1u << 1), true, 25u));
        CHECK_STR_EQ(line, "DSHOT300 GPIO 0 GPIO 1");
        CHECK(!motor_throttle_target(line, sizeof(line), PROTO_MOTOR_PWM,
                                     1u << 2, true, 25u));
        CHECK_STR_EQ(line, "MOTOR PWM GPIO 2");
        /* No protocol known, or an index past the catalogue: the pins. */
        CHECK(!motor_throttle_target(line, sizeof(line), 0u, 3u, true, 25u));
        CHECK_STR_EQ(line, "GPIO 0 GPIO 1");
        CHECK(!motor_throttle_target(line, sizeof(line), 8u, 3u, true, 25u));
        CHECK_STR_EQ(line, "GPIO 0 GPIO 1");
    }
    /* Several protocols: MIXED, in the language shown. */
    ui_text_set_language(UI_LANG_EN);
    CHECK(!motor_throttle_target(line, sizeof(line), MOTOR_PROTO_MIXED,
                                 (1u << 0) | (1u << 2), true, 25u));
    CHECK_STR_EQ(line, "MIXED GPIO 0 GPIO 2");
    ui_text_set_language(UI_LANG_DE);
    CHECK(!motor_throttle_target(line, sizeof(line), MOTOR_PROTO_MIXED,
                                 (1u << 0) | (1u << 2), true, 25u));
    CHECK_STR_EQ(line, "GEMISCHT GPIO 0 GPIO 2");
    ui_text_set_language(UI_LANG_EN);
}

/* The row's value box holds 25 cells of 10px.  In a
 * box of 18 cells the pins give way to a count. */
TEST_CASE(many_throttle_pins_end_in_a_count_within_the_box)
{
    char line[96];
    CHECK_EQ(motor_target_cells(), 25u);
    ui_text_set_language(UI_LANG_EN);
    /* The card's own 25 cells: two pins and the count of the rest. */
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 0xFFu,
                                 true, motor_target_cells()));
    CHECK_STR_EQ(line, "DSHOT300 GPIO 0 GPIO 1 +6");
    CHECK(strlen(line) <= motor_target_cells());
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 0xFFu,
                                 true, 18u));
    CHECK_STR_EQ(line, "DSHOT300 GPIO 0 +7");
    CHECK(strlen(line) <= 18u);
    /* Exactly fitting: "DSHOT300 GPIO 0 GPIO 1" is 22. */
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 3u, true,
                                 22u));
    CHECK_STR_EQ(line, "DSHOT300 GPIO 0 GPIO 1");
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 3u, true,
                                 21u));
    CHECK_STR_EQ(line, "DSHOT300 GPIO 0 +1");
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 3u, true,
                                 17u));
    CHECK_STR_EQ(line, "DSHOT300 +2");
    /* The longest name leaves room for the count alone. */
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT600_BIDIR, 3u,
                                 true, 18u));
    CHECK_STR_EQ(line, "DSHOT600 BIDIR +2");
    /* GPIO 47 is the highest pin the boards have. */
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_MOTOR_PWM,
                                 (uint64_t)1u << 47, true, 18u));
    CHECK_STR_EQ(line, "MOTOR PWM GPIO 47");
    /* A buffer too short cuts the line and ends it. */
    char tiny[6];
    (void)motor_throttle_target(tiny, sizeof(tiny), PROTO_DSHOT300, 3u, true,
                                18u);
    CHECK_STR_EQ(tiny, "DSHOT");
    /* No buffer: nothing written, no warning. */
    CHECK(!motor_throttle_target(NULL, 8u, PROTO_DSHOT300, 3u, true, 18u));
    CHECK(!motor_throttle_target(line, 0u, PROTO_DSHOT300, 3u, true, 18u));
}

/* Every text the value box shows fits its 18 cells in both languages. */
TEST_CASE(the_target_row_texts_fit_the_box)
{
    char line[96];
    for (int l = 0; l < 2; ++l) {
        ui_text_set_language(l == 0 ? UI_LANG_EN : UI_LANG_DE);
        CHECK(motor_throttle_target(line, sizeof(line), 0u, 0u, true, 18u));
        CHECK(strlen(line) <= 18u);
        CHECK(motor_throttle_target(line, sizeof(line), 0u, 0u, false, 18u));
        CHECK(strlen(line) <= 18u);
        CHECK(!motor_throttle_target(line, sizeof(line), MOTOR_PROTO_MIXED,
                                     0xFFFFu, true, 18u));
        CHECK(strlen(line) <= 18u);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(no_throttle_pin_and_an_unread_binding_are_warnings)
{
    char line[96];
    ui_text_set_language(UI_LANG_EN);
    CHECK(motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 0u, true,
                                18u));
    CHECK_STR_EQ(line, "NO MOTOR PIN");
    CHECK(motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 0u, false,
                                18u));
    CHECK_STR_EQ(line, "PINS NOT READ");
    /* A mask with a binding not read is no pin. */
    CHECK(motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 3u, false,
                                18u));
    CHECK_STR_EQ(line, "PINS NOT READ");
    ui_text_set_language(UI_LANG_DE);
    CHECK(motor_throttle_target(line, sizeof(line), 0u, 0u, true, 18u));
    CHECK_STR_EQ(line, "KEIN MOTOR-PIN");
    CHECK(motor_throttle_target(line, sizeof(line), 0u, 0u, false, 18u));
    CHECK_STR_EQ(line, "PINS NICHT GELESEN");
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_target_row_is_drawn_in_the_colour_of_its_state)
{
    const gfx_color_t warn = ui_theme_color(UI_C_WARN);
    const gfx_color_t text = ui_theme_color(UI_C_TEXT);
    for (int l = 0; l < 2; ++l) {
        ui_text_set_language(l == 0 ? UI_LANG_EN : UI_LANG_DE);
        /* Not read until the application hands a binding over. */
        fresh();
        draw_once();
        CHECK(target_pixels(warn) > 0);
        CHECK_EQ(target_pixels(text), 0);
        /* Bound: the text colour, no warning. */
        fresh();
        motor_screen_set_throttle_pins((1u << 2) | (1u << 5), true);
        draw_once();
        CHECK(target_pixels(text) > 0);
        CHECK_EQ(target_pixels(warn), 0);
        /* Read, and no pin bound as a motor. */
        motor_screen_set_throttle_pins(0u, true);
        draw_once();
        CHECK(target_pixels(warn) > 0);
        CHECK_EQ(target_pixels(text), 0);
        /* Rebound: the old warning is cleared away. */
        motor_screen_set_throttle_pins(1u << 4, true);
        draw_once();
        CHECK_EQ(target_pixels(warn), 0);
        CHECK(target_pixels(text) > 0);
        /* The binding goes unread again. */
        motor_screen_set_throttle_pins(1u << 4, false);
        draw_once();
        CHECK(target_pixels(warn) > 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A sum over the TARGET row's value box: two different texts differ. */
static unsigned long target_hash(void)
{
    unsigned long h = 1469598103UL;
    for (int y = TGT_Y0; y < TGT_Y1; ++y) {
        for (int x = TGT_X0; x < TGT_X1; ++x) {
            h = (h * 31UL + (unsigned long)fb[y * W + x]) & 0xffffffffUL;
        }
    }
    return h;
}

TEST_CASE(a_new_protocol_or_pin_set_redraws_the_row_and_a_throttle_keeps_it)
{
    fresh();
    motor_screen_set_throttle_pins(1u << 2, true);
    draw_once();
    const unsigned long bare = target_hash();
    motor_screen_set_throttle_protocol(PROTO_DSHOT300);
    draw_once();
    const unsigned long named = target_hash();
    CHECK(named != bare);
    /* The same protocol again moves nothing. */
    const gfx_color_t mark = (gfx_color_t)~fb[ARM_Y * W + ARM_X];
    fb[ARM_Y * W + ARM_X] = mark;
    motor_screen_set_throttle_protocol(PROTO_DSHOT300);
    draw_once();
    CHECK_EQ(fb[ARM_Y * W + ARM_X], mark);
    motor_screen_set_throttle_pins((1u << 2) | (1u << 3), true);
    draw_once();
    CHECK(target_hash() != named);
    /* A throttle move repaints the readout, not the row. */
    const unsigned long two = target_hash();
    motor_screen_set_throttle(10.0f);
    draw_once();
    CHECK_EQ(target_hash(), two);
}

TEST_CASE(a_binding_that_reads_with_no_motor_pin_leaves_not_read)
{
    fresh();
    draw_once();
    const unsigned long unread = target_hash();
    /* Same mask, 0, but the binding is now read: the text changes. */
    motor_screen_set_throttle_pins(0u, true);
    draw_once();
    CHECK(target_hash() != unread);
    CHECK(target_pixels(ui_theme_color(UI_C_WARN)) > 0);
    /* It is the text a screen set to (0, true) from the start draws. */
    const unsigned long read = target_hash();
    fresh();
    motor_screen_set_throttle_pins(0u, true);
    draw_once();
    CHECK_EQ(target_hash(), read);
}

TEST_CASE(a_changed_pin_set_redraws_the_controls_and_an_unchanged_one_does_not)
{
    fresh();
    draw_once();
    const gfx_color_t mark = (gfx_color_t)~fb[ARM_Y * W + ARM_X];
    fb[ARM_Y * W + ARM_X] = mark;
    /* Same pins again: nothing moved, nothing is painted. */
    motor_screen_set_throttle_pins(0u, false);
    draw_once();
    CHECK_EQ(fb[ARM_Y * W + ARM_X], mark);
    /* A new pin set bumps the control revision: the buttons repaint. */
    motor_screen_set_throttle_pins(1u << 3, true);
    draw_once();
    CHECK(fb[ARM_Y * W + ARM_X] != mark);
}

/*
 * A binding that reads with no motor pin: ARM is drawn dimmed, a tap says
 * why and no hold arms.  A pin brings the green and the arm back.
 */
TEST_CASE(arm_with_no_motor_pin_is_dimmed_and_refused_with_its_reason)
{
    fresh();
    motor_screen_set_throttle_pins(1u << 2, true);
    draw_once();
    const gfx_color_t live = arm_px();
    CHECK(green_of(live) > 120);

    motor_screen_set_throttle_pins(0u, true);
    tick_for(1);
    draw_once();
    CHECK(arm_px() != live);
    CHECK(green_of(arm_px()) < green_of(live));

    ui_router_set_alert(NULL);
    hold_arm();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    CHECK_STR_EQ(ui_router_alert(), TR(ALERT_ARM_NO_PIN));

    /* Unread pins are not a refusal: the row says PINS NOT READ. */
    ui_router_set_alert(NULL);
    motor_screen_set_throttle_pins(0u, false);
    tick_for(1);
    draw_once();
    CHECK_EQ(arm_px(), live);

    motor_screen_set_throttle_pins(1u << 2, true);
    tick_for(1);
    hold_arm();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
}

/* The pin goes while ARM is held: the hold ends and arms nothing. */
TEST_CASE(a_pin_lost_under_a_held_arm_ends_the_hold)
{
    fresh();
    motor_screen_set_throttle_pins(1u << 2, true);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    motor_screen_set_throttle_pins(0u, true);
    tick_for(HOLD_TICKS);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

/* An armed bench whose binding empties can still be switched to SAFE. */
TEST_CASE(disarm_stays_live_with_no_motor_pin)
{
    fresh();
    motor_screen_set_throttle_pins(1u << 2, true);
    motor_screen_set_armed(true);
    motor_screen_set_throttle_pins(0u, true);
    tick_for(1);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
}

int main(void)
{
    RUN(arming_returns_the_throttle_to_zero);
    RUN(arming_returns_every_throttle_to_zero);
    RUN(the_readout_after_an_arm_is_that_of_an_untouched_screen);
    RUN(the_hold_that_asks_for_the_arm_returns_the_throttle_to_zero);
    RUN(a_throttle_set_after_the_ask_stays_when_the_bench_answers);
    RUN(an_arm_after_a_dropped_ask_returns_the_throttle_to_zero);
    RUN(every_ask_returns_the_throttle_to_zero);
    RUN(a_hold_that_does_not_arm_leaves_the_throttle_alone);
    RUN(a_throttle_waiting_at_the_disarm_is_dropped);
    RUN(a_knob_command_waiting_at_the_disarm_is_not_put_back);
    RUN(an_armed_report_repeated_keeps_the_throttle);
    RUN(a_press_on_the_disarmed_track_is_no_drag_after_the_arm);
    RUN(a_drag_under_way_ends_at_the_disarm);
    RUN(every_arm_starts_from_zero);
    RUN(the_target_row_names_the_protocol_and_the_pins);
    RUN(many_throttle_pins_end_in_a_count_within_the_box);
    RUN(the_target_row_texts_fit_the_box);
    RUN(no_throttle_pin_and_an_unread_binding_are_warnings);
    RUN(the_target_row_is_drawn_in_the_colour_of_its_state);
    RUN(a_new_protocol_or_pin_set_redraws_the_row_and_a_throttle_keeps_it);
    RUN(a_binding_that_reads_with_no_motor_pin_leaves_not_read);
    RUN(a_changed_pin_set_redraws_the_controls_and_an_unchanged_one_does_not);
    RUN(arm_with_no_motor_pin_is_dimmed_and_refused_with_its_reason);
    RUN(a_pin_lost_under_a_held_arm_ends_the_hold);
    RUN(disarm_stays_live_with_no_motor_pin);
    return test_summary("motor_throttle");
}
