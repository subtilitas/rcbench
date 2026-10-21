/**
 * @file test_nav_drag.c
 * @brief A drag that leaves by the band or ends under STOP, ARM, HOME or a touch
 * loss, for the throttle, the dial, the speed and the set point.
 *
 * Cases:
 * - a_stop_ends_a_throttle_drag
 * - a_disarm_ends_a_throttle_drag
 * - an_arm_ends_a_throttle_drag
 * - leaving_ends_a_throttle_drag
 * - a_touch_loss_ends_a_throttle_drag
 * - a_dial_drag_that_leaves_by_the_band_ends_at_the_edge
 * - a_slide_onto_the_dial_commands_nothing
 * - a_speed_drag_that_leaves_by_the_band_ends_at_the_edge
 * - home_under_a_dial_drag_ends_the_drag
 * - a_touch_loss_ends_a_dial_drag
 * - a_set_point_drag_that_leaves_by_the_band_ends_at_the_edge
 * - output_off_answers_the_first_tap_after_a_drag_left_by_the_band
 * - an_output_off_press_that_leaves_by_the_band_asks_for_nothing
 * - an_output_on_hold_that_leaves_by_the_band_switches_nothing_on
 * - leaving_or_a_touch_loss_ends_a_set_point_drag
 * - a_release_made_for_the_finger_activates_nothing_but_stop
 * - reset_peaks_needs_the_fingers_release_and_disarm_does_not
 * - servo_strip_tabs_carried_into_the_band_do_nothing
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"

#define SV_SHAFT_X 300

#define SV_SHAFT_Y (UI_BAND_H + (H - UI_BAND_H) / 2)

#define SV_SPEED_Y (UI_BAND_H + 306)

#define SU_TRACK_X 72

#define SU_V_Y     (335 + UI_BAND_H)

#define SU_I_Y     (397 + UI_BAND_H)

#define SU_OUT_X   676

#define SU_OUT_Y   (318 + UI_BAND_H)

static void dial_at(float deg, int r, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    *x = SV_SHAFT_X + (int)((float)r * cosf(deg * k) + 0.5f);
    *y = SV_SHAFT_Y - (int)((float)r * sinf(deg * k) + 0.5f);
}

/* What ends a throttle drag while the finger stays on the track. */
typedef enum { END_STOP, END_DISARM, END_ARM, END_HOME, END_LOSS } ender_t;

/*
 * A drag on the throttle track, then one of the things that end a run or a
 * gesture, in the order the panel's frame does them.  From there the finger
 * still on the track moves nothing, and a later contact with its id moves
 * nothing on its way from the band into the body.  On a bench the ender
 * left disarmed the knob and a fresh drag move nothing either; armed, the
 * knob has the throttle again, and a fresh drag moves it by its own travel.
 * The press before an arm is on a disarmed bench, and takes no drag.
 */
static void a_throttle_drag_then(ender_t e)
{
    on_screen(SCREEN_MOTOR);
    if (e != END_ARM) {
        motor_screen_set_armed(true);
    }
    drain_motor();
    const gfx_rect_t stop = ui_band_stop_rect();
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_MOTOR));

    finger(0, 300, M_TRACK_Y);
    glide(0, 340, M_TRACK_Y, 8);
    const float dragged = motor_screen_throttle();
    CHECK_NEAR(dragged, (e == END_ARM) ? 0.0f : 40.0f * M_PX_PCT, 0.01f);
    float want = dragged;
    bool disarmed = false;

    switch (e) {
    case END_STOP:
        feed_tap(1, stop.x + stop.w / 2, stop.y + stop.h / 2);
        CHECK(ui_router_take_stop());
        motor_screen_cancel_arm();
        /* the bench disarms on a stop: the next case's checks apply */
        /* fall through */
    case END_DISARM:
        motor_screen_set_throttle(0.0f);
        drain_motor();
        motor_screen_set_armed(false);
        want = 0.0f;
        disarmed = true;
        break;
    case END_ARM:
        motor_screen_set_armed(true);
        want = 0.0f;
        break;
    case END_HOME:
        feed_tap(1, home.x + home.w / 2, home.y + home.h / 2);
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
        ui_router_goto(SCREEN_MOTOR);
        disarmed = true;        /* asked for; the bench has not answered */
        break;
    case END_LOSS:
        motor_screen_knob_cancel();
        ui_router_cancel_gestures();
        break;
    }
    drain_motor();
    CHECK_NEAR(motor_screen_throttle(), want, 0.001f);

    glide(0, 420, M_TRACK_Y, 8);
    CHECK_NEAR(motor_screen_throttle(), want, 0.001f);
    lift(0);
    CHECK_NEAR(motor_screen_throttle(), want, 0.001f);

    finger(0, 560, UNDER_STOP_Y);
    glide(0, 560, UI_BAND_H + 1, 3);
    glide(0, 640, UI_BAND_H + 1, 8);
    lift(0);
    CHECK_NEAR(motor_screen_throttle(), want, 0.001f);
    CHECK(!motor_posted(NULL));

    if (disarmed) {
        /* Not armed: no control moves the throttle.  The bench's answer to
         * a disarm asked for by leaving returns it to zero. */
        CHECK(!knob_moves_the_throttle());
        finger(0, 200, M_TRACK_Y);
        glide(0, 240, M_TRACK_Y, 8);
        lift(0);
        CHECK_NEAR(motor_screen_throttle(), want, 0.001f);
        CHECK(!motor_posted(NULL));
        motor_screen_set_armed(false);
        want = 0.0f;
        CHECK_NEAR(motor_screen_throttle(), want, 0.001f);
        motor_screen_set_armed(true);
        drain_motor();
    }
    CHECK(knob_moves_the_throttle());

    finger(0, 200, M_TRACK_Y);
    glide(0, 240, M_TRACK_Y, 8);
    lift(0);
    CHECK_NEAR(motor_screen_throttle(), want + 40.0f * M_PX_PCT, 0.01f);
}

TEST_CASE(a_stop_ends_a_throttle_drag)      { a_throttle_drag_then(END_STOP); }

TEST_CASE(a_disarm_ends_a_throttle_drag)    { a_throttle_drag_then(END_DISARM); }

TEST_CASE(an_arm_ends_a_throttle_drag)      { a_throttle_drag_then(END_ARM); }

TEST_CASE(leaving_ends_a_throttle_drag)     { a_throttle_drag_then(END_HOME); }

TEST_CASE(a_touch_loss_ends_a_throttle_drag) { a_throttle_drag_then(END_LOSS); }

/*
 * SERVO.  A dial drag slides up the glass and leaves it on the band.  The
 * knob has the horn again, and the next lone finger, which presses the body
 * beside the dial and slides onto it, commands nothing: it did not press
 * the dial.
 */
TEST_CASE(a_dial_drag_that_leaves_by_the_band_ends_at_the_edge)
{
    on_screen(SCREEN_SERVO);
    servo_screen_set_armed(true);
    drain_servo();

    int x, y;
    dial_at(80.0f, 110, &x, &y);
    finger(FEED_LONE, x, y);
    CHECK_EQ(servo_took().kind, SERVO_CMD_POSITION);
    glide(FEED_LONE, x, 30, 8);
    lift(FEED_LONE);
    const uint16_t held = servo_screen_commanded();
    drain_servo();

    servo_screen_knob_frame();
    servo_screen_knob(0.1f);
    const servo_cmd_t k = servo_took();
    CHECK_EQ(k.kind, SERVO_CMD_POSITION);
    CHECK(k.value_us != held);
    const uint16_t turned = servo_screen_commanded();

    /* 190 px right of the shaft: outside the arc, which ends at 174 px. */
    finger(FEED_LONE, SV_SHAFT_X + 190, SV_SHAFT_Y);
    const servo_cmd_t d = servo_took();
    glide(FEED_LONE, SV_SHAFT_X + 150, SV_SHAFT_Y, 8);
    const servo_cmd_t c = servo_took();
    lift(FEED_LONE);
    CHECK_EQ(d.kind, SERVO_CMD_NONE);
    CHECK_EQ(c.kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), turned);
}

/* The same press and slide with no drag before it, for comparison. */
TEST_CASE(a_slide_onto_the_dial_commands_nothing)
{
    on_screen(SCREEN_SERVO);
    servo_screen_set_armed(true);
    drain_servo();
    finger(FEED_LONE, SV_SHAFT_X + 190, SV_SHAFT_Y);
    glide(FEED_LONE, SV_SHAFT_X + 150, SV_SHAFT_Y, 8);
    const servo_cmd_t c = servo_took();
    lift(FEED_LONE);
    CHECK_EQ(c.kind, SERVO_CMD_NONE);
}

/*
 * SPEED is part of the command, so on a held output a change of it says the
 * position again at the new rate.  A drag on it that leaves by the band
 * ends at the edge: a contact from the band into the body changes no rate.
 */
TEST_CASE(a_speed_drag_that_leaves_by_the_band_ends_at_the_edge)
{
    on_screen(SCREEN_SERVO);
    servo_screen_set_armed(true);
    drain_servo();
    int x, y;
    dial_at(40.0f, 110, &x, &y);
    feed_tap(FEED_LONE, x, y);
    CHECK_EQ(servo_took().kind, SERVO_CMD_POSITION);

    finger(FEED_LONE, 560, SV_SPEED_Y);
    glide(FEED_LONE, 620, SV_SPEED_Y, 8);
    const servo_cmd_t set = servo_took();
    CHECK_EQ(set.kind, SERVO_CMD_POSITION);
    glide(FEED_LONE, 620, 30, 8);
    lift(FEED_LONE);
    CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);

    check_plain_band(700, UNDER_STOP_Y);
    finger(FEED_LONE, 700, UNDER_STOP_Y);
    glide(FEED_LONE, 700, UI_BAND_H + 1, 3);
    glide(FEED_LONE, 760, UI_BAND_H + 1, 8);
    lift(FEED_LONE);
    CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);

    /* The rate is the one the drag set: a tap on the dial carries it. */
    feed_tap(FEED_LONE, x, y);
    const servo_cmd_t after = servo_took();
    CHECK_EQ(after.kind, SERVO_CMD_POSITION);
    CHECK_EQ(after.slew_per_s, set.slew_per_s);
}

/* ARM by touch: the button's place is found by holding each candidate. */
static bool servo_arm_by_touch(uint8_t id)
{
    for (int yy = UI_BAND_H + 392; yy < UI_BAND_H + 418; yy += 8) {
        finger(id, 650, yy);
        for (int i = 0; i < 50; ++i) {
            ui_router_tick(0.05f);
        }
        const servo_cmd_t c = servo_took();
        lift(id);
        if (c.kind == SERVO_CMD_ARM) {
            return true;
        }
    }
    return false;
}

/*
 * Finger 0 on the dial, finger 1 taps the home tag, finger 0 lifts on the
 * overview.  SERVO again, armed by a hold on ARM with a lone finger, every
 * event of which reaches the screen; then a press beside the dial slides
 * onto it.  The drag ended when the screen was left.
 */
TEST_CASE(home_under_a_dial_drag_ends_the_drag)
{
    on_screen(SCREEN_SERVO);
    servo_screen_set_armed(true);
    drain_servo();

    int x, y;
    dial_at(40.0f, 110, &x, &y);
    finger(0, x, y);
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_SERVO));
    feed_tap(1, home.x + 10, home.y + 10);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    lift(0);
    drain_servo();
    servo_screen_set_armed(false);

    ui_router_goto(SCREEN_SERVO);
    drain_servo();
    CHECK(servo_arm_by_touch(FEED_LONE));
    servo_screen_set_armed(true);
    drain_servo();

    /* The knob has the horn. */
    servo_screen_knob_frame();
    servo_screen_knob(0.1f);
    CHECK_EQ(servo_took().kind, SERVO_CMD_POSITION);

    finger(FEED_LONE, SV_SHAFT_X + 190, SV_SHAFT_Y);
    glide(FEED_LONE, SV_SHAFT_X + 150, SV_SHAFT_Y, 8);
    const servo_cmd_t c = servo_took();
    lift(FEED_LONE);
    CHECK_EQ(c.kind, SERVO_CMD_NONE);
}

/* A touch loss under a dial drag: the finger still on the dial commands
 * nothing more, and the knob has the horn. */
TEST_CASE(a_touch_loss_ends_a_dial_drag)
{
    on_screen(SCREEN_SERVO);
    servo_screen_set_armed(true);
    drain_servo();
    int x, y, x2, y2;
    dial_at(40.0f, 110, &x, &y);
    dial_at(70.0f, 110, &x2, &y2);
    finger(FEED_LONE, x, y);
    CHECK_EQ(servo_took().kind, SERVO_CMD_POSITION);

    servo_screen_knob_cancel();
    ui_router_cancel_gestures();
    drain_servo();
    const uint16_t held = servo_screen_commanded();
    glide(FEED_LONE, x2, y2, 8);
    lift(FEED_LONE);
    CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), held);

    servo_screen_knob_frame();
    servo_screen_knob(0.1f);
    CHECK_EQ(servo_took().kind, SERVO_CMD_POSITION);
}

/*
 * SUPPLY, output off.  The voltage track is dragged, the finger slides up
 * and leaves on the band.  The next lone finger holds OUTPUT ON without
 * moving: the hold switches on, at the set point the drag left.
 */
TEST_CASE(a_set_point_drag_that_leaves_by_the_band_ends_at_the_edge)
{
    static const int rows[] = { SU_V_Y, SU_I_Y };
    for (int r = 0; r < 2; ++r) {
        on_screen(SCREEN_SUPPLY);
        finger(FEED_LONE, SU_TRACK_X + 60, rows[r]);
        glide(FEED_LONE, SU_TRACK_X + 80, rows[r], 8);
        const float set_v = supply_screen_set_v();
        const float set_i = supply_screen_set_i();
        glide(FEED_LONE, SU_TRACK_X + 80, 30, 8);
        lift(FEED_LONE);
        CHECK_NEAR(supply_screen_set_v(), set_v, 0.001f);
        CHECK_NEAR(supply_screen_set_i(), set_i, 0.001f);

        finger(FEED_LONE, SU_OUT_X, SU_OUT_Y);
        for (int i = 0; i < 50; ++i) {
            ui_router_tick(0.05f);
        }
        supply_cmd_t c1 = { 0 };
        const bool p1 = supply_screen_poll_cmd(&c1);
        lift(FEED_LONE);
        CHECK(p1 && c1.on);
        CHECK_NEAR(supply_screen_set_v(), set_v, 0.001f);
        CHECK_NEAR(supply_screen_set_i(), set_i, 0.001f);
    }
}

/* Output on: the same drag, and the next tap is OUTPUT OFF.  The first tap
 * switches off. */
TEST_CASE(output_off_answers_the_first_tap_after_a_drag_left_by_the_band)
{
    on_screen(SCREEN_SUPPLY);
    supply_screen_set_output(true);
    const float set = supply_screen_set_v();

    finger(FEED_LONE, SU_TRACK_X + 60, SU_V_Y);
    glide(FEED_LONE, SU_TRACK_X + 80, SU_V_Y, 8);
    glide(FEED_LONE, SU_TRACK_X + 80, 30, 8);
    lift(FEED_LONE);

    feed_tap(FEED_LONE, SU_OUT_X, SU_OUT_Y);
    supply_cmd_t c1 = { 0 };
    const bool p1 = supply_screen_poll_cmd(&c1);
    CHECK(p1 && c1.off);
    CHECK_NEAR(supply_screen_set_v(), set, 0.001f);
}

/* A press on OUTPUT OFF that slides up and leaves by the band is a press
 * that left its button: no OFF, and the next tap switches off. */
TEST_CASE(an_output_off_press_that_leaves_by_the_band_asks_for_nothing)
{
    on_screen(SCREEN_SUPPLY);
    supply_screen_set_output(true);

    finger(FEED_LONE, SU_OUT_X, SU_OUT_Y);
    glide(FEED_LONE, SU_OUT_X, 30, 8);
    lift(FEED_LONE);
    CHECK(!supply_screen_poll_cmd(NULL));

    feed_tap(FEED_LONE, SU_OUT_X, SU_OUT_Y);
    supply_cmd_t c1 = { 0 };
    const bool p1 = supply_screen_poll_cmd(&c1);
    CHECK(p1 && c1.off);
}

/* An OUTPUT ON hold that slides up to the band is abandoned where it left
 * the button, and switches nothing on, however long the finger stays. */
TEST_CASE(an_output_on_hold_that_leaves_by_the_band_switches_nothing_on)
{
    on_screen(SCREEN_SUPPLY);
    finger(FEED_LONE, SU_OUT_X, SU_OUT_Y);
    glide(FEED_LONE, SU_OUT_X, 30, 8);
    for (int i = 0; i < 80; ++i) {
        ui_router_tick(0.05f);
    }
    CHECK(!supply_screen_poll_cmd(NULL));
    lift(FEED_LONE);
    CHECK(!supply_screen_poll_cmd(NULL));
}

/* A second finger on the home tag or a touch loss under a set-point drag:
 * the finger still on the track changes nothing more. */
TEST_CASE(leaving_or_a_touch_loss_ends_a_set_point_drag)
{
    for (int how = 0; how < 2; ++how) {
        on_screen(SCREEN_SUPPLY);
        finger(0, SU_TRACK_X + 60, SU_V_Y);
        glide(0, SU_TRACK_X + 80, SU_V_Y, 8);
        if (how == 0) {
            const gfx_rect_t home =
                ui_home_tag_rect(ui_router_title(SCREEN_SUPPLY));
            feed_tap(1, home.x + 10, home.y + 10);
            CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
            ui_router_goto(SCREEN_SUPPLY);
        } else {
            ui_router_cancel_gestures();
        }
        const float set = supply_screen_set_v();
        glide(0, SU_TRACK_X + 200, SU_V_Y, 8);
        lift(0);
        CHECK_NEAR(supply_screen_set_v(), set, 0.001f);

        /* And the screen answers the next press. */
        finger(0, SU_OUT_X, SU_OUT_Y);
        for (int i = 0; i < 50; ++i) {
            ui_router_tick(0.05f);
        }
        supply_cmd_t c1 = { 0 };
        const bool p1 = supply_screen_poll_cmd(&c1);
        lift(0);
        CHECK(p1 && c1.on);
    }
}

/*
 * A release the finger did not make: the router's, for a contact the screen
 * owns that reaches the band, and the tracker's, for a contact that jumps.
 * Both are where the contact was last seen, which can be on the control it
 * pressed.  They end the press and activate nothing: not a tile, not the
 * home tag, not the alert strip, not the splash's skip.  STOP takes either.
 */
TEST_CASE(a_release_made_for_the_finger_activates_nothing_but_stop)
{
    fresh();
    to_overview();
    feed_reset();

    /* A tile, 90 px under the band, carried into the band in one report. */
    finger(FEED_LONE, 110, UI_BAND_H + 90);
    finger(FEED_LONE, 110, UI_BAND_H - 8);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    lift(FEED_LONE);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* The same px by px: the last point in the body is on the tile. */
    finger(FEED_LONE, 110, UI_BAND_H + 30);
    glide(FEED_LONE, 110, UI_BAND_H - 8, 4);
    lift(FEED_LONE);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* A tile, and the contact next reported off every tile. */
    finger(FEED_LONE, 110, UI_BAND_H + 90);
    finger(FEED_LONE, 110, UI_BAND_H + 90 + 121);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    lift(FEED_LONE);

    /* The tap after it is a tap. */
    feed_tap(FEED_LONE, 110, UI_BAND_H + 90);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);

    /* The home tag, and the contact next reported in the body. */
    finger(FEED_LONE, UI_TAG_X + 20, UI_TAG_Y + UI_TAG_H / 2);
    finger(FEED_LONE, 400, 300);
    lift(FEED_LONE);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    feed_tap(FEED_LONE, UI_TAG_X + 20, UI_TAG_Y + UI_TAG_H / 2);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* The alert strip. */
    ui_router_set_alert("supply not answering -- output off");
    finger(FEED_LONE, 400, H - 10);
    finger(FEED_LONE, 400, 200);
    lift(FEED_LONE);
    CHECK(ui_router_alert() != NULL);
    feed_tap(FEED_LONE, 400, H - 10);
    CHECK(ui_router_alert() == NULL);

    /* STOP: the release the tracker makes over it is a stop. */
    const gfx_rect_t stop = ui_band_stop_rect();
    CHECK(!ui_router_take_stop());
    finger(FEED_LONE, stop.x + stop.w / 2, stop.y + stop.h / 2);
    finger(FEED_LONE, 200, 300);
    CHECK(ui_router_take_stop());
    lift(FEED_LONE);

    /* The splash's skip. */
    fresh();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_WARN, "");
    }
    feed_reset();
    finger(FEED_LONE, 100, 100);
    finger(FEED_LONE, 600, 400);
    CHECK(!splash_screen_done());
    lift(FEED_LONE);
    CHECK(splash_screen_done());
}

/* Everything the panel shows, as one number. */
static uint32_t panel_look(void)
{
    uint32_t h = 2166136261u;
    ui_router_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    for (int i = 0; i < W * H; ++i) {
        h = (h ^ (uint32_t)fb[i]) * 16777619u;
    }
    return h;
}

/* MOTOR & ESC: RESET PEAKS takes a release the finger made.  DISARM takes
 * any: disarming is not held back by how the release came about. */
TEST_CASE(reset_peaks_needs_the_fingers_release_and_disarm_does_not)
{
    /* Mirrored from motor_screen.c: the right rail's two buttons. */
    const int rail_x = 676, arm_y = UI_BAND_H + 342, reset_y = UI_BAND_H + 402;
    fresh();
    to_overview();
    ui_router_goto(SCREEN_MOTOR);
    feed_reset();
    motor_cmd_t c;
    while (motor_screen_poll_cmd(&c)) { }

    finger(FEED_LONE, rail_x, reset_y);
    finger(FEED_LONE, 300, UI_BAND_H + 120);  /* the plot */
    CHECK_EQ(feed_ups, 1);                    /* the tracker's release */
    lift(FEED_LONE);
    CHECK(!motor_screen_poll_cmd(&c));
    feed_tap(FEED_LONE, rail_x, reset_y);
    CHECK(motor_screen_poll_cmd(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_RESET_PEAKS);

    motor_screen_set_armed(true);
    while (motor_screen_poll_cmd(&c)) { }
    finger(FEED_LONE, rail_x, arm_y);
    finger(FEED_LONE, 300, UI_BAND_H + 120);
    CHECK(motor_screen_poll_cmd(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_DISARM);
    lift(FEED_LONE);
    motor_screen_set_armed(false);
}

/* SERVO: the strip's SETTINGS and LIVE tabs lie 18 px under the band.
 * Carried into it, they are released for the screen where they were
 * pressed, and neither switches the task. */
TEST_CASE(servo_strip_tabs_carried_into_the_band_do_nothing)
{
    /* Mirrored from ui_workbench.h (servo_spots.h). */
    const int set_x = SV_TAB_SETTINGS_X, set_y = UI_BAND_H + SV_TAB_Y;
    const int close_x = SV_TAB_LIVE_X, close_y = UI_BAND_H + SV_TAB_Y;
    fresh();
    to_overview();
    ui_router_goto(SCREEN_SERVO);
    feed_reset();
    const uint32_t shut = panel_look();

    finger(FEED_LONE, set_x, set_y);
    finger(FEED_LONE, set_x, UI_BAND_H - 8);
    lift(FEED_LONE);
    CHECK_EQ(panel_look(), shut);
    feed_tap(FEED_LONE, set_x, set_y);
    const uint32_t open = panel_look();
    CHECK(open != shut);

    finger(FEED_LONE, close_x, close_y);
    finger(FEED_LONE, close_x, UI_BAND_H - 8);
    lift(FEED_LONE);
    CHECK_EQ(panel_look(), open);
    feed_tap(FEED_LONE, close_x, close_y);
    CHECK_EQ(panel_look(), shut);
}

int main(void)
{
    RUN(a_stop_ends_a_throttle_drag);
    RUN(a_disarm_ends_a_throttle_drag);
    RUN(an_arm_ends_a_throttle_drag);
    RUN(leaving_ends_a_throttle_drag);
    RUN(a_touch_loss_ends_a_throttle_drag);
    RUN(a_dial_drag_that_leaves_by_the_band_ends_at_the_edge);
    RUN(a_slide_onto_the_dial_commands_nothing);
    RUN(a_speed_drag_that_leaves_by_the_band_ends_at_the_edge);
    RUN(home_under_a_dial_drag_ends_the_drag);
    RUN(a_touch_loss_ends_a_dial_drag);
    RUN(a_set_point_drag_that_leaves_by_the_band_ends_at_the_edge);
    RUN(output_off_answers_the_first_tap_after_a_drag_left_by_the_band);
    RUN(an_output_off_press_that_leaves_by_the_band_asks_for_nothing);
    RUN(an_output_on_hold_that_leaves_by_the_band_switches_nothing_on);
    RUN(leaving_or_a_touch_loss_ends_a_set_point_drag);
    RUN(a_release_made_for_the_finger_activates_nothing_but_stop);
    RUN(reset_peaks_needs_the_fingers_release_and_disarm_does_not);
    RUN(servo_strip_tabs_carried_into_the_band_do_nothing);
    return test_summary("nav_drag");
}
