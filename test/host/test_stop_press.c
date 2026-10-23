/**
 * @file test_stop_press.c
 * @brief STOP and DISARM act on the press, and a press the router keeps ends
 * a latched drag, with the second contact on the glass and injected.
 *
 * Contact a drags the SERVO dial or the MOTOR & ESC throttle, contact b
 * presses STOP, DISARM or the band and is held 100ms or 600ms.  The pairs of
 * track ids are the glass's (0, 1) and the touch injector's (0x80, 0x81).
 * Touch is fed through the tracker and the router (touch_feed.h).  Nothing
 * answers for the bench in the cases that ask whether a command follows the
 * press: the screen alone must post none.
 *
 * Cases:
 * - a_stop_press_stops_at_the_press_with_no_drag
 * - a_stop_press_ends_a_dial_drag_at_the_press
 * - a_band_press_ends_a_dial_drag_at_the_press
 * - a_servo_disarm_press_switches_to_safe_at_the_press
 * - a_motor_disarm_press_switches_to_safe_at_the_press
 * - a_stop_press_ends_a_throttle_drag_at_the_press
 * - arm_is_still_a_hold_of_2s
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

/** @brief A body row in panel coordinates. */
#define P(y) (UI_BAND_H + (y))

/** @brief SERVO's shaft and ARM / DISARM, as test_servo_arm.c has them. */
#define SHAFT_X 300
#define SHAFT_Y P((H - UI_BAND_H) / 2)
#define SV_ARM_PX 648
#define SV_ARM_PY P(404)

/** @brief MOTOR & ESC's ARM / DISARM and throttle track, as
 *         test_safe_press.c and test_nav.c have them. */
#define MO_ARM_PX 676
#define MO_ARM_PY P(342)
#define M_TRACK_Y (336 + UI_BAND_H + 20)

/** @brief A point on the band that is no control, as test_nav.c has it. */
#define PLAIN_BAND_X 720
#define PLAIN_BAND_Y 44

/** @brief One frame at 60 frames a second, s. */
#define FRAME_S (1.0f / 60.0f)

/** @brief The pairs of track ids: the glass's, then the injector's. */
static const uint8_t k_ids[2][2] = { { 0u, 1u }, { 0x80u, 0x81u } };

/** @brief How long contact b is held, s: 100ms and 600ms. */
static const float k_hold_s[2] = { 0.1f, 0.6f };

static gfx_color_t *fb;
static gfx_canvas_t cv;

/** @brief The next servo command; SERVO_CMD_NONE when none waits. */
static servo_cmd_kind_t sv_took(void)
{
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
    servo_screen_take(&c);
    return c.kind;
}

/** @brief Every servo command taken. */
static void sv_drain(void)
{
    while (sv_took() != SERVO_CMD_NONE) { }
}

/** @brief The next motor command into @p c when given; whether one waited. */
static bool mo_took(motor_cmd_t *c)
{
    motor_cmd_t got = { .kind = MOTOR_CMD_NONE };
    const bool any = motor_screen_poll_cmd(&got);
    if (c != NULL) {
        *c = got;
    }
    return any;
}

/** @brief Every motor command taken. */
static void mo_drain(void)
{
    while (mo_took(NULL)) { }
}

/** @brief Whether nothing but a DISARM waits: no throttle, no arm. */
static bool mo_quiet(void)
{
    motor_cmd_t c;
    while (mo_took(&c)) {
        if (c.kind != MOTOR_CMD_DISARM) {
            return false;
        }
    }
    return true;
}

/** @brief Screen @p id up, nothing on the glass, nothing waiting. */
static void up_on(ui_screen_id_t id)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
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
    ui_router_goto(id);
    feed_reset();
    servo_screen_feedback(0u, 0.0f, false);
    sv_drain();
    mo_drain();
    (void)ui_router_take_stop();
}

/** @brief @p n frames with nothing on the glass changing. */
static void ticks(int n)
{
    for (int i = 0; i < n; ++i) {
        ui_router_tick(FRAME_S);
    }
}

/** @brief A point on the dial's arc at @p deg, 110px from the shaft. */
static void dial_at(float deg, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    *x = SHAFT_X + (int)(110.0f * cosf(deg * k) + 0.5f);
    *y = SHAFT_Y - (int)(110.0f * sinf(deg * k) + 0.5f);
}

/** @brief The middle of STOP. */
static void stop_at(int *x, int *y)
{
    const gfx_rect_t r = ui_band_stop_rect();
    *x = r.x + r.w / 2;
    *y = r.y + r.h / 2;
}

/** @brief SERVO armed, a dial drag of contact @p a from 30deg to 0deg, and
 *         the commands taken.  Returns the angle contact a is at. */
static float servo_drag_under_way(uint8_t a)
{
    up_on(SCREEN_SERVO);
    servo_screen_set_armed(true);
    sv_drain();
    float deg = 30.0f;
    int x, y;
    for (; deg > 0.0f; deg -= 2.0f) {
        dial_at(deg, &x, &y);
        finger(a, x, y);
        ui_router_tick(FRAME_S);
    }
    sv_drain();
    return deg;
}

/** @brief Whether nothing is commanded for @p s seconds while contact @p a
 *         moves on along the arc, and the value stays at @p held. */
static bool servo_quiet_for(float s, uint8_t a, float *deg, uint16_t held)
{
    for (float t = 0.0f; t < s; t += FRAME_S) {
        *deg -= 0.5f;
        int x, y;
        dial_at(*deg, &x, &y);
        finger(a, x, y);
        ui_router_tick(FRAME_S);
    }
    return sv_took() == SERVO_CMD_NONE && servo_screen_commanded() == held;
}

TEST_CASE(a_stop_press_stops_at_the_press_with_no_drag)
{
    for (int p = 0; p < 2; ++p) {
        for (int h = 0; h < 2; ++h) {
            up_on(SCREEN_SERVO);
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
            CHECK_EQ(sv_took(), SERVO_CMD_NONE);
            CHECK(servo_quiet_for(k_hold_s[h], a, &deg, held));
            lift(b);
            CHECK(!ui_router_take_stop());
            CHECK(servo_quiet_for(0.3f, a, &deg, held));
            lift(a);
            ticks(20);
            CHECK_EQ(sv_took(), SERVO_CMD_NONE);
        }
    }
}

TEST_CASE(a_band_press_ends_a_dial_drag_at_the_press)
{
    for (int p = 0; p < 2; ++p) {
        const uint8_t a = k_ids[p][0], b = k_ids[p][1];
        float deg = servo_drag_under_way(a);
        uint16_t held = servo_screen_commanded();
        CHECK(!gfx_rect_contains(ui_band_stop_rect(), PLAIN_BAND_X,
                                 PLAIN_BAND_Y));
        finger(b, PLAIN_BAND_X, PLAIN_BAND_Y);
        CHECK(servo_quiet_for(0.6f, a, &deg, held));
        lift(b);
        CHECK(servo_quiet_for(0.3f, a, &deg, held));
        lift(a);

        /* The home tag, pressed and held: the drag ends at the press, and
         * the tap leaves the screen at the lift. */
        deg = servo_drag_under_way(a);
        held = servo_screen_commanded();
        const gfx_rect_t home =
            ui_home_tag_rect(ui_router_title(SCREEN_SERVO));
        finger(b, home.x + home.w / 2, home.y + home.h / 2);
        CHECK(servo_quiet_for(0.6f, a, &deg, held));
        CHECK_EQ(ui_router_current(), SCREEN_SERVO);
        lift(b);
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
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
            finger(b, SV_ARM_PX, SV_ARM_PY);
            CHECK_EQ(sv_took(), SERVO_CMD_DISARM);           /* the press */
            CHECK(servo_quiet_for(k_hold_s[h], a, &deg, held));
            lift(b);
            CHECK_EQ(sv_took(), SERVO_CMD_NONE);
            lift(a);
            ticks(20);
            CHECK_EQ(sv_took(), SERVO_CMD_NONE);
        }
    }
    /* A finger left on the button after the disarm arms nothing when it
     * reads ARM again: the press took no hold. */
    up_on(SCREEN_SERVO);
    servo_screen_set_armed(true);
    sv_drain();
    finger(FEED_LONE, SV_ARM_PX, SV_ARM_PY);
    CHECK_EQ(sv_took(), SERVO_CMD_DISARM);
    ticks(2);
    servo_screen_set_armed(false);
    for (float t = 0.0f; t < UI_HOLD_S + 0.5f; t += FRAME_S) {
        ui_router_tick(FRAME_S);
        CHECK_EQ(sv_took(), SERVO_CMD_NONE);
    }
    lift(FEED_LONE);
    CHECK_EQ(sv_took(), SERVO_CMD_NONE);
}

/** @brief MOTOR & ESC armed, a throttle drag of contact @p a 40px along the
 *         track, and the commands taken. */
static void motor_drag_under_way(uint8_t a)
{
    up_on(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    mo_drain();
    finger(a, 300, M_TRACK_Y);
    glide(a, 340, M_TRACK_Y, 8);
    mo_drain();
}

TEST_CASE(a_motor_disarm_press_switches_to_safe_at_the_press)
{
    for (int p = 0; p < 2; ++p) {
        for (int h = 0; h < 2; ++h) {
            const uint8_t a = k_ids[p][0], b = k_ids[p][1];
            motor_drag_under_way(a);
            finger(b, MO_ARM_PX, MO_ARM_PY);
            motor_cmd_t c;
            CHECK(mo_took(&c));
            CHECK_EQ(c.kind, MOTOR_CMD_DISARM);              /* the press */
            feed_tick_per_report(FRAME_S);
            glide(a, 340 + (int)(k_hold_s[h] * 60.0f), M_TRACK_Y, 1);
            feed_tick_per_report(0.0f);
            lift(b);
            lift(a);
            CHECK(mo_quiet());
        }
    }
    up_on(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    mo_drain();
    finger(FEED_LONE, MO_ARM_PX, MO_ARM_PY);
    motor_cmd_t c;
    CHECK(mo_took(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_DISARM);
    motor_screen_set_armed(false);
    mo_drain();
    for (float t = 0.0f; t < UI_HOLD_S + 0.5f; t += FRAME_S) {
        ui_router_tick(FRAME_S);
        CHECK(!mo_took(NULL));
    }
    lift(FEED_LONE);
    CHECK(!mo_took(NULL));
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
        CHECK(!mo_took(NULL));
        CHECK_NEAR(motor_screen_throttle(), held, 0.001f);
        lift(b);
        lift(a);
        CHECK(!mo_took(NULL));
        CHECK(!ui_router_take_stop());
    }
}

/** @brief Whether a hold of @p s seconds at (@p x, @p y) posts an ARM on
 *         SERVO (@p servo) or on MOTOR & ESC. */
static bool arm_posts_after(float s, int x, int y, bool servo)
{
    finger(FEED_LONE, x, y);
    bool armed = false;
    for (float t = 0.0f; t < s; t += FRAME_S) {
        ui_router_tick(FRAME_S);
        if (servo) {
            armed = armed || sv_took() == SERVO_CMD_ARM;
        } else {
            motor_cmd_t c;
            armed = armed || (mo_took(&c) && c.kind == MOTOR_CMD_ARM);
        }
    }
    lift(FEED_LONE);
    return armed;
}

TEST_CASE(arm_is_still_a_hold_of_2s)
{
    up_on(SCREEN_SERVO);
    CHECK(!arm_posts_after(0.0f, SV_ARM_PX, SV_ARM_PY, true));
    CHECK(!arm_posts_after(UI_HOLD_S - 0.1f, SV_ARM_PX, SV_ARM_PY, true));
    CHECK(arm_posts_after(UI_HOLD_S + 0.2f, SV_ARM_PX, SV_ARM_PY, true));

    up_on(SCREEN_MOTOR);
    CHECK(!arm_posts_after(0.0f, MO_ARM_PX, MO_ARM_PY, false));
    CHECK(!arm_posts_after(UI_HOLD_S - 0.1f, MO_ARM_PX, MO_ARM_PY, false));
    CHECK(arm_posts_after(UI_HOLD_S + 0.2f, MO_ARM_PX, MO_ARM_PY, false));
}

int main(void)
{
    RUN(a_stop_press_stops_at_the_press_with_no_drag);
    RUN(a_stop_press_ends_a_dial_drag_at_the_press);
    RUN(a_band_press_ends_a_dial_drag_at_the_press);
    RUN(a_servo_disarm_press_switches_to_safe_at_the_press);
    RUN(a_motor_disarm_press_switches_to_safe_at_the_press);
    RUN(a_stop_press_ends_a_throttle_drag_at_the_press);
    RUN(arm_is_still_a_hold_of_2s);
    free(fb);
    return test_summary("stop_press");
}
