/**
 * @file test_nav_stop.c
 * @brief STOP and DISARM act on the press, and a press the router keeps ends
 * a latched drag, with the second contact on the glass and injected.
 *
 * The station's check, through the tracker and the router (touch_feed.h):
 * contact a drags the SERVO dial or the MOTOR & ESC throttle, contact b
 * presses STOP, DISARM or the band and is held 100ms or 600ms.  The pairs of
 * track ids are the glass's (0, 1) and the touch injector's (0x80, 0x81,
 * TOUCH_MERGE_INJECT_ID | id).  Nothing answers for the bench in the cases
 * that ask whether a command follows the press: the screen alone must post
 * none.
 *
 * Cases:
 * - a_stop_press_stops_at_the_press_with_no_drag
 * - a_stop_press_ends_a_dial_drag_at_the_press
 * - a_band_press_ends_a_dial_drag_at_the_press
 * - a_dial_drag_carried_onto_the_band_stops_the_filter_at_the_edge
 * - a_servo_disarm_press_switches_to_safe_at_the_press
 * - a_motor_disarm_press_switches_to_safe_at_the_press
 * - a_stop_press_ends_a_throttle_drag_at_the_press
 * - arm_is_still_a_hold_of_2s
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"

/** @brief The shaft in panel coordinates. */
#define SHAFT_P_X SV_SHAFT_X
#define SHAFT_P_Y (UI_BAND_H + SV_SHAFT_Y)

/** @brief SERVO's ARM / DISARM in panel coordinates. */
#define SV_ARM_P_X (SV_RC_X + SV_RC_W / 2)
#define SV_ARM_P_Y (UI_BAND_H + SV_ARM_Y + SV_ARM_H / 2)

/** @brief MOTOR & ESC's ARM / DISARM in panel coordinates. */
#define MO_ARM_P_X MO_ARM_X
#define MO_ARM_P_Y (UI_BAND_H + MO_ARM_Y)

/** @brief One frame at 60 frames a second, s. */
#define FRAME_S (1.0f / 60.0f)

/** @brief The pairs of track ids: the glass's, then the injector's. */
static const uint8_t k_ids[2][2] = { { 0u, 1u }, { 0x80u, 0x81u } };

/** @brief How long contact b is held, s: the station's 100ms and 600ms. */
static const float k_hold_s[2] = { 0.1f, 0.6f };

/** @brief A point on the dial's arc at @p deg, 110px from the shaft. */
static void dial_at(float deg, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    *x = SHAFT_P_X + (int)(110.0f * cosf(deg * k) + 0.5f);
    *y = SHAFT_P_Y - (int)(110.0f * sinf(deg * k) + 0.5f);
}

/** @brief @p n frames with nothing on the glass changing. */
static void ticks(int n)
{
    for (int i = 0; i < n; ++i) {
        ui_router_tick(FRAME_S);
    }
}

/** @brief The middle of STOP. */
static void stop_at(int *x, int *y)
{
    const gfx_rect_t r = ui_band_stop_rect();
    *x = r.x + r.w / 2;
    *y = r.y + r.h / 2;
}

/** @brief Frames for @p s seconds, each with a report of contact @p id
 *         moved 1px along the dial's arc, as a finger that never stops. */
static void frames_moving(float s, uint8_t id, float *deg)
{
    for (float t = 0.0f; t < s; t += FRAME_S) {
        *deg -= 0.5f;
        int x, y;
        dial_at(*deg, &x, &y);
        finger(id, x, y);
        ui_router_tick(FRAME_S);
    }
}

/** @brief SERVO up, armed, a dial drag of contact @p a under way from 30deg
 *         to 0deg with a frame after every report, and the commands taken.
 *         Returns the angle contact a is at. */
static float servo_drag_under_way(uint8_t a)
{
    on_screen(SCREEN_SERVO);
    servo_screen_set_armed(true);
    drain_servo();
    float deg = 30.0f;
    int x, y;
    dial_at(deg, &x, &y);
    finger(a, x, y);
    feed_tick_per_report(FRAME_S);
    for (; deg > 0.0f; deg -= 2.0f) {
        dial_at(deg, &x, &y);
        finger(a, x, y);
    }
    feed_tick_per_report(0.0f);
    /* Two reports with no frame between them: the filter's target is
     * behind the finger's, so a filter left running would command. */
    deg -= 6.0f;
    dial_at(deg, &x, &y);
    finger(a, x, y);
    drain_servo();
    return deg;
}

/** @brief Whether nothing is commanded for @p s seconds while contact @p a
 *         moves on, and the value stays at @p held. */
static bool servo_quiet_for(float s, uint8_t a, float *deg, uint16_t held)
{
    frames_moving(s, a, deg);
    return servo_took().kind == SERVO_CMD_NONE
           && servo_screen_commanded() == held;
}

TEST_CASE(a_stop_press_stops_at_the_press_with_no_drag)
{
    for (int p = 0; p < 2; ++p) {
        for (int h = 0; h < 2; ++h) {
            on_screen(SCREEN_SERVO);
            servo_screen_set_armed(true);
            int sx, sy;
            stop_at(&sx, &sy);
            finger(k_ids[p][1], sx, sy);
            CHECK(ui_router_take_stop());     /* before any frame or lift */
            for (float t = 0.0f; t < k_hold_s[h]; t += FRAME_S) {
                ui_router_tick(FRAME_S);
                CHECK(!ui_router_take_stop());
            }
            lift(k_ids[p][1]);
            CHECK(!ui_router_take_stop());    /* and once */
        }
    }
}

TEST_CASE(a_stop_press_ends_a_dial_drag_at_the_press)
{
    for (int p = 0; p < 2; ++p) {
        for (int h = 0; h < 2; ++h) {
            const uint8_t a = k_ids[p][0], b = k_ids[p][1];
            float deg = servo_drag_under_way(a);
            const uint16_t held = servo_screen_commanded();
            int sx, sy;
            stop_at(&sx, &sy);
            finger(b, sx, sy);
            CHECK(ui_router_take_stop());
            CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);
            CHECK(servo_quiet_for(k_hold_s[h], a, &deg, held));
            lift(b);
            CHECK(!ui_router_take_stop());
            CHECK(servo_quiet_for(0.3f, a, &deg, held));
            lift(a);
            ticks(20);
            CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);
            CHECK_EQ(servo_screen_commanded(), held);
        }
    }
}

TEST_CASE(a_band_press_ends_a_dial_drag_at_the_press)
{
    for (int p = 0; p < 2; ++p) {
        const uint8_t a = k_ids[p][0], b = k_ids[p][1];
        /* A point of the band that is no control. */
        float deg = servo_drag_under_way(a);
        uint16_t held = servo_screen_commanded();
        check_plain_band(700, UNDER_STOP_Y);
        finger(b, 700, UNDER_STOP_Y);
        CHECK(servo_quiet_for(0.6f, a, &deg, held));
        lift(b);
        CHECK(servo_quiet_for(0.3f, a, &deg, held));
        lift(a);

        /* The home tag, pressed and held: the drag ends at the press, and
         * the tap leaves the screen at the lift. */
        deg = servo_drag_under_way(a);
        held = servo_screen_commanded();
        const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_SERVO));
        finger(b, home.x + home.w / 2, home.y + home.h / 2);
        CHECK(servo_quiet_for(0.6f, a, &deg, held));
        CHECK_EQ(ui_router_current(), SCREEN_SERVO);
        lift(b);
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
        lift(a);
    }
}

TEST_CASE(a_dial_drag_carried_onto_the_band_stops_the_filter_at_the_edge)
{
    for (int p = 0; p < 2; ++p) {
        const uint8_t a = k_ids[p][0];
        (void)servo_drag_under_way(a);
        const uint16_t held = servo_screen_commanded();
        int x, y;
        dial_at(0.0f, &x, &y);
        finger(a, x, UI_BAND_H - 4);      /* one report: onto the band */
        ticks(20);
        CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);
        CHECK_EQ(servo_screen_commanded(), held);
        lift(a);
    }
}

TEST_CASE(a_servo_disarm_press_switches_to_safe_at_the_press)
{
    for (int p = 0; p < 2; ++p) {
        for (int h = 0; h < 2; ++h) {
            const uint8_t a = k_ids[p][0], b = k_ids[p][1];
            float deg = servo_drag_under_way(a);
            const uint16_t held = servo_screen_commanded();
            finger(b, SV_ARM_P_X, SV_ARM_P_Y);
            CHECK_EQ(servo_took().kind, SERVO_CMD_DISARM);   /* the press */
            CHECK(servo_quiet_for(k_hold_s[h], a, &deg, held));
            lift(b);
            CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);
            lift(a);
            ticks(20);
            CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);
        }
    }
    /* A finger left on the button after the disarm arms nothing when it
     * reads ARM again: the press took no hold. */
    on_screen(SCREEN_SERVO);
    servo_screen_set_armed(true);
    drain_servo();
    finger(FEED_LONE, SV_ARM_P_X, SV_ARM_P_Y);
    CHECK_EQ(servo_took().kind, SERVO_CMD_DISARM);
    ticks(2);
    servo_screen_set_armed(false);
    for (float t = 0.0f; t < UI_HOLD_S + 0.5f; t += FRAME_S) {
        ui_router_tick(FRAME_S);
        CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);
    }
    lift(FEED_LONE);
    CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);
}

/** @brief MOTOR & ESC up, armed, a throttle drag of contact @p a 40px along
 *         the track, and the commands taken. */
static void motor_drag_under_way(uint8_t a)
{
    on_screen(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    drain_motor();
    finger(a, 300, M_TRACK_Y);
    glide(a, 340, M_TRACK_Y, 8);
    drain_motor();
}

/** @brief Whether nothing but a DISARM is waiting: no throttle, no arm. */
static bool motor_quiet(void)
{
    motor_cmd_t c;
    while (motor_posted(&c)) {
        if (c.kind != MOTOR_CMD_DISARM) {
            return false;
        }
    }
    return true;
}

TEST_CASE(a_motor_disarm_press_switches_to_safe_at_the_press)
{
    for (int p = 0; p < 2; ++p) {
        for (int h = 0; h < 2; ++h) {
            const uint8_t a = k_ids[p][0], b = k_ids[p][1];
            motor_drag_under_way(a);
            finger(b, MO_ARM_P_X, MO_ARM_P_Y);
            motor_cmd_t c;
            CHECK(motor_posted(&c));
            CHECK_EQ(c.kind, MOTOR_CMD_DISARM);              /* the press */
            /* The press ended the drag: contact a moves no throttle. */
            feed_tick_per_report(FRAME_S);
            glide(a, 340 + (int)(k_hold_s[h] * 60.0f), M_TRACK_Y, 1);
            feed_tick_per_report(0.0f);
            lift(b);
            lift(a);
            CHECK(motor_quiet());
        }
    }
    /* A finger left on the button after the disarm arms nothing. */
    on_screen(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    drain_motor();
    finger(FEED_LONE, MO_ARM_P_X, MO_ARM_P_Y);
    motor_cmd_t c;
    CHECK(motor_posted(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_DISARM);
    motor_screen_set_armed(false);
    drain_motor();
    for (float t = 0.0f; t < UI_HOLD_S + 0.5f; t += FRAME_S) {
        ui_router_tick(FRAME_S);
        CHECK(!motor_posted(NULL));
    }
    lift(FEED_LONE);
    CHECK(!motor_posted(NULL));
}

TEST_CASE(a_stop_press_ends_a_throttle_drag_at_the_press)
{
    for (int p = 0; p < 2; ++p) {
        const uint8_t a = k_ids[p][0], b = k_ids[p][1];
        motor_drag_under_way(a);
        const float held = motor_screen_throttle();
        int sx, sy;
        stop_at(&sx, &sy);
        finger(b, sx, sy);
        CHECK(ui_router_take_stop());
        glide(a, 400, M_TRACK_Y, 8);
        CHECK(!motor_posted(NULL));
        CHECK_NEAR(motor_screen_throttle(), held, 0.001f);
        lift(b);
        lift(a);
        CHECK(!motor_posted(NULL));
        CHECK(!ui_router_take_stop());
    }
}

/** @brief Whether a hold of @p s seconds on ARM at (@p x, @p y) posts an
 *         ARM on the screen on top. */
static bool arm_posts_after(float s, int x, int y, bool servo)
{
    finger(FEED_LONE, x, y);
    bool armed = false;
    for (float t = 0.0f; t < s; t += FRAME_S) {
        ui_router_tick(FRAME_S);
        if (servo) {
            armed = armed || servo_took().kind == SERVO_CMD_ARM;
        } else {
            motor_cmd_t c;
            armed = armed || (motor_posted(&c) && c.kind == MOTOR_CMD_ARM);
        }
    }
    lift(FEED_LONE);
    return armed;
}

TEST_CASE(arm_is_still_a_hold_of_2s)
{
    on_screen(SCREEN_SERVO);
    CHECK(!arm_posts_after(0.0f, SV_ARM_P_X, SV_ARM_P_Y, true));
    CHECK(!arm_posts_after(UI_HOLD_S - 0.1f, SV_ARM_P_X, SV_ARM_P_Y, true));
    CHECK(arm_posts_after(UI_HOLD_S + 0.2f, SV_ARM_P_X, SV_ARM_P_Y, true));

    on_screen(SCREEN_MOTOR);
    CHECK(!arm_posts_after(0.0f, MO_ARM_P_X, MO_ARM_P_Y, false));
    CHECK(!arm_posts_after(UI_HOLD_S - 0.1f, MO_ARM_P_X, MO_ARM_P_Y, false));
    CHECK(arm_posts_after(UI_HOLD_S + 0.2f, MO_ARM_P_X, MO_ARM_P_Y, false));
}

int main(void)
{
    RUN(a_stop_press_stops_at_the_press_with_no_drag);
    RUN(a_stop_press_ends_a_dial_drag_at_the_press);
    RUN(a_band_press_ends_a_dial_drag_at_the_press);
    RUN(a_dial_drag_carried_onto_the_band_stops_the_filter_at_the_edge);
    RUN(a_servo_disarm_press_switches_to_safe_at_the_press);
    RUN(a_motor_disarm_press_switches_to_safe_at_the_press);
    RUN(a_stop_press_ends_a_throttle_drag_at_the_press);
    RUN(arm_is_still_a_hold_of_2s);
    return test_summary("nav_stop");
}
