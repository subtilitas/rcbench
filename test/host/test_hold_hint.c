/*
 * The hint a 2s hold button gives when it lifts before 2s.
 *
 * A release before UI_HOLD_S shows the button's hint on the alert band and
 * sends nothing.  A press that slid off the button, a touch that was lost, a
 * release that is not a tap, a second finger and a screen without the band
 * show nothing.  A hold that completes shows no hint.  A press on STOP, a
 * fault bit that comes up and a switch from ARMED to SAFE clear the hint at
 * once; an alert stays.
 *
 * The first cases drive ui_hold_t on its own with the router on a screen
 * with the band; the others drive the real buttons through the tracker and
 * the router, as test_motor_arm.c does.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "link_pages.h"

#include "gfx.h"
#include "motor_screen.h"
#include "motor_spots.h"
#include "settings.h"
#include "splash_screen.h"
#include "supply_screen.h"
#include "supply_spots.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* A frame of 0.25s is the most a hold credits (UI_HOLD_MAX_CREDIT_S), and
 * a multiple of 0.25 adds without rounding: 7 of them are 1.75s exactly. */
#define Q 0.25f

#define ARM_X MO_ARM_X
#define ARM_Y (UI_BAND_H + MO_ARM_Y)
#define OUT_X SU_OUT_X
#define OUT_Y (UI_BAND_H + SU_OUT_Y)

/* The router on @p screen, the splash done, nothing on the glass. */
static void router_on(ui_screen_id_t screen)
{
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    ui_text_set_language(UI_LANG_EN);
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(screen);
    feed_reset();
    ui_router_set_alert(NULL);
}

static motor_cmd_t took(void)
{
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f };
    (void)motor_screen_poll_cmd(&c);
    return c;
}

/* @p n frames of @p dt_s. */
static void frames(int n, float dt_s)
{
    for (int i = 0; i < n; ++i) {
        ui_router_tick(dt_s);
    }
}

/* A hold held for 7 frames of 0.25s and 0.249s more: 1,999ms. */
static void hold_1999(void)
{
    frames(7, Q);
    ui_router_tick(0.249f);
}

static touch_event_t up_at(int x, int y, uint8_t flags)
{
    touch_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = TOUCH_EVENT_UP;
    e.point.x = (int16_t)x;
    e.point.y = (int16_t)y;
    e.flags = flags;
    return e;
}

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

static bool hint_shows(int id)
{
    const char *a = ui_router_alert();
    return a != NULL && strcmp(a, ui_tr((ui_text_id_t)id)) == 0;
}

/* ------------------------------------------------------------- the widget */

static const gfx_rect_t BTN = { 100, 100, 200, 60 };

TEST_CASE(a_release_at_1999ms_shows_the_hint_and_fires_nothing)
{
    router_on(SCREEN_MOTOR);
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    for (int i = 0; i < 7; ++i) {
        CHECK(!ui_hold_tick(&h, Q));
    }
    CHECK(!ui_hold_tick(&h, 0.249f));
    CHECK(ui_router_alert() == NULL);
    const touch_event_t up = up_at(150, 130, 0);
    CHECK(!ui_hold_end(&h, &up, BTN));
    CHECK(hint_shows(TX_HOLD_HINT_ARM));
}

TEST_CASE(a_release_at_2000ms_fires_and_shows_no_hint)
{
    router_on(SCREEN_MOTOR);
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    for (int i = 0; i < 7; ++i) {
        CHECK(!ui_hold_tick(&h, Q));
    }
    CHECK(ui_hold_tick(&h, Q));
    const touch_event_t up = up_at(150, 130, 0);
    CHECK(ui_hold_end(&h, &up, BTN));
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(a_tap_with_no_time_held_shows_the_hint)
{
    router_on(SCREEN_MOTOR);
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_TEST);
    const touch_event_t up = up_at(150, 130, 0);
    CHECK(!ui_hold_end(&h, &up, BTN));
    CHECK(hint_shows(TX_HOLD_HINT_TEST));
}

TEST_CASE(a_press_that_left_the_control_shows_no_hint)
{
    router_on(SCREEN_MOTOR);
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    CHECK(!ui_hold_tick(&h, Q));
    CHECK(ui_hold_leave(&h));
    const touch_event_t up = up_at(150, 130, 0);
    CHECK(!ui_hold_end(&h, &up, BTN));
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(a_release_off_the_control_shows_no_hint)
{
    router_on(SCREEN_MOTOR);
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    const touch_event_t up = up_at(BTN.x + BTN.w, BTN.y, 0);
    CHECK(!ui_hold_end(&h, &up, BTN));
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(a_release_that_is_no_tap_shows_no_hint)
{
    router_on(SCREEN_MOTOR);
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    const touch_event_t up = up_at(150, 130, TOUCH_FLAG_NO_TAP);
    CHECK(!ui_hold_end(&h, &up, BTN));
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(a_press_dropped_without_a_release_shows_no_hint)
{
    router_on(SCREEN_MOTOR);
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    CHECK(!ui_hold_end(&h, NULL, BTN));
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(a_state_that_went_away_under_the_press_shows_no_hint)
{
    router_on(SCREEN_MOTOR);
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    CHECK(ui_hold_left(&h));
    const touch_event_t up = up_at(150, 130, 0);
    CHECK(!ui_hold_end(&h, &up, BTN));
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(no_hint_shows_where_the_screen_has_no_band)
{
    router_on(SCREEN_BUSFAULT);
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    const touch_event_t up = up_at(150, 130, 0);
    CHECK(!ui_hold_end(&h, &up, BTN));
    CHECK(ui_router_alert() == NULL);
    /* and none waits for the next screen with a band */
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(the_hint_shows_in_the_language_showing)
{
    router_on(SCREEN_MOTOR);
    ui_hold_t h;
    ui_hold_reset(&h);
    const touch_event_t up = up_at(150, 130, 0);
    ui_text_set_language(UI_LANG_DE);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    (void)ui_hold_end(&h, &up, BTN);
    const char *de = ui_router_alert();
    CHECK(de != NULL);
    CHECK(de != NULL
          && strcmp(de, "Halte 2s, um auf ARMED zu schalten.") == 0);
    ui_text_set_language(UI_LANG_EN);
}

/* ------------------------------------------------------ the notice's style */

/* A hint released at once, on the motor screen. */
static void release_early(void)
{
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    const touch_event_t up = up_at(150, 130, 0);
    (void)ui_hold_end(&h, &up, BTN);
}

/* Binary fractions, so the sums are exact: 3.75 + 255/1024 is 4 - 1/1024s. */
#define T_3999 0.2490234375f
#define T_REST 0.0009765625f

TEST_CASE(the_hint_is_there_at_3999ms_and_gone_at_4000ms)
{
    router_on(SCREEN_MOTOR);
    release_early();
    CHECK(hint_shows(TX_HOLD_HINT_ARM));
    ui_router_tick(0.1f);   /* the frame a notice arrives in does not count */
    ui_router_tick(3.75f);
    ui_router_tick(T_3999);
    CHECK(hint_shows(TX_HOLD_HINT_ARM));
    ui_router_tick(T_REST);
    CHECK(ui_router_alert() == NULL);
}

static gfx_color_t band_pixel(void)
{
    static gfx_color_t fb[UI_PANEL_W * UI_PANEL_H];
    gfx_canvas_t cv;
    gfx_canvas_init(&cv, fb, UI_PANEL_W, UI_PANEL_H, UI_PANEL_W);
    ui_router_invalidate();
    ui_router_render(&cv, 0);
    return fb[(UI_PANEL_H - UI_ALERT_H + 2) * UI_PANEL_W + 4];
}

TEST_CASE(the_hint_is_amber_and_an_alert_is_the_danger_colour)
{
    router_on(SCREEN_MOTOR);
    release_early();
    CHECK(ui_router_alert_is_notice());
    CHECK_EQ(band_pixel(), ui_theme_color(UI_C_WARN));
    ui_router_set_alert("An alert.");
    CHECK(!ui_router_alert_is_notice());
    CHECK_EQ(band_pixel(), ui_theme_color(UI_C_DANGER));
}

TEST_CASE(an_alert_that_arrives_replaces_the_hint)
{
    router_on(SCREEN_MOTOR);
    release_early();
    ui_router_set_alert("Touch signal lost.");
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), "Touch signal lost.") == 0);
    CHECK(!ui_router_alert_is_notice());
    /* and it keeps the alert's 30s, not the hint's 4s */
    ui_router_tick(0.1f);
    ui_router_tick(UI_NOTICE_SHOW_S + 1.0f);
    CHECK(ui_router_alert() != NULL);

    /* a held alert replaces it too */
    router_on(SCREEN_MOTOR);
    release_early();
    ui_router_hold_alert("Touchscreen not responding.");
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), "Touchscreen not responding.") == 0);
    CHECK(!ui_router_alert_is_notice());
    ui_router_hold_alert(NULL);
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(a_hint_does_not_show_while_an_alert_is_up)
{
    router_on(SCREEN_MOTOR);
    ui_router_set_alert("Touch signal lost.");
    release_early();
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), "Touch signal lost.") == 0);
    CHECK(!ui_router_alert_is_notice());

    router_on(SCREEN_MOTOR);
    ui_router_hold_alert("Touchscreen not responding.");
    release_early();
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), "Touchscreen not responding.") == 0);
    CHECK(!ui_router_alert_is_notice());
    ui_router_hold_alert(NULL);
}

TEST_CASE(a_tap_on_the_band_clears_the_hint)
{
    router_on(SCREEN_MOTOR);
    release_early();
    CHECK(ui_router_alert() != NULL);
    feed_tap(FEED_LONE, 300, UI_PANEL_H - 10);
    CHECK(ui_router_alert() == NULL);
}

/* A status with @p faults, ARMED when @p armed, the link up. */
static void status_set(uint16_t faults, bool armed)
{
    ui_bench_status_t st;
    memset(&st, 0, sizeof(st));
    st.link_up = true;
    st.armed   = armed;
    st.faults  = faults;
    ui_router_set_status(&st);
}

TEST_CASE(stop_clears_the_hint_in_the_next_frame)
{
    router_on(SCREEN_MOTOR);
    release_early();
    REQUIRE(ui_router_alert_is_notice());
    const gfx_rect_t stop = ui_band_stop_rect();
    finger(FEED_LONE, stop.x + stop.w / 2, stop.y + stop.h / 2);
    ui_router_tick(1.0f / 60.0f);
    CHECK(ui_router_alert() == NULL);
    CHECK(!ui_router_alert_is_notice());
    lift(FEED_LONE);
    CHECK(ui_router_take_stop());

    /* An alert is not a notice: STOP leaves it for its 30s. */
    router_on(SCREEN_MOTOR);
    ui_router_set_alert("Touch signal lost.");
    finger(FEED_LONE, stop.x + stop.w / 2, stop.y + stop.h / 2);
    ui_router_tick(1.0f / 60.0f);
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), "Touch signal lost.") == 0);
    lift(FEED_LONE);
    (void)ui_router_take_stop();
}

TEST_CASE(a_switch_to_safe_or_a_new_fault_clears_the_hint)
{
    /* ARMED to SAFE, as STOP or an error leaves the bench. */
    router_on(SCREEN_MOTOR);
    status_set(0u, true);
    release_early();
    REQUIRE(ui_router_alert_is_notice());
    status_set(0u, true);           /* the same status keeps it */
    CHECK(ui_router_alert_is_notice());
    status_set(0u, false);
    CHECK(ui_router_alert() == NULL);

    /* A fault bit that comes up, the bench SAFE throughout. */
    router_on(SCREEN_MOTOR);
    status_set(LINK_FAULT_OVERCURRENT, false);
    release_early();
    REQUIRE(ui_router_alert_is_notice());
    status_set(LINK_FAULT_OVERCURRENT, false);  /* one that stands keeps it */
    CHECK(ui_router_alert_is_notice());
    status_set(0u, false);                      /* one that clears keeps it */
    CHECK(ui_router_alert_is_notice());
    status_set(LINK_FAULT_HEARTBEAT, false);
    CHECK(ui_router_alert() == NULL);

    /* An alert stays through both. */
    router_on(SCREEN_MOTOR);
    status_set(0u, true);
    ui_router_set_alert("Touch signal lost.");
    status_set(LINK_FAULT_HEARTBEAT, false);
    CHECK(ui_router_alert() != NULL && !ui_router_alert_is_notice());
    status_set(0u, false);
}

/* ----------------------------------------------------------------- MOTOR */

TEST_CASE(arm_released_at_1999ms_shows_the_hint_and_sends_nothing)
{
    router_on(SCREEN_MOTOR);
    motor_screen_set_armed(false);
    finger(FEED_LONE, ARM_X, ARM_Y);
    hold_1999();
    lift(FEED_LONE);
    CHECK(hint_shows(TX_HOLD_HINT_ARM));
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
}

TEST_CASE(arm_released_at_2000ms_arms_and_shows_no_hint)
{
    router_on(SCREEN_MOTOR);
    motor_screen_set_armed(false);
    finger(FEED_LONE, ARM_X, ARM_Y);
    frames(8, Q);
    lift(FEED_LONE);
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(took().kind, MOTOR_CMD_ARM);
}

TEST_CASE(arm_slid_off_before_the_release_shows_no_hint)
{
    router_on(SCREEN_MOTOR);
    motor_screen_set_armed(false);
    finger(FEED_LONE, ARM_X, ARM_Y);
    frames(2, Q);
    glide(FEED_LONE, MO_TRACK_X + 100, UI_BAND_H + MO_TRACK_Y, 8);
    frames(2, Q);
    lift(FEED_LONE);
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
}

TEST_CASE(arm_with_a_lost_touch_shows_no_hint)
{
    router_on(SCREEN_MOTOR);
    motor_screen_set_armed(false);
    finger(FEED_LONE, ARM_X, ARM_Y);
    frames(4, Q);
    /* the controller stops answering: the panel cancels every gesture */
    ui_router_cancel_gestures();
    lift(FEED_LONE);
    CHECK(ui_router_alert() == NULL);

    /* a release that never arrived */
    router_on(SCREEN_MOTOR);
    finger(FEED_LONE, ARM_X, ARM_Y);
    frames(4, Q);
    feed_lose_next(1);
    lift(FEED_LONE);
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
}

TEST_CASE(arm_whose_contact_jumps_shows_no_hint)
{
    router_on(SCREEN_MOTOR);
    motor_screen_set_armed(false);
    finger(FEED_LONE, ARM_X, ARM_Y);
    frames(4, Q);
    /* a jump of more than TOUCH_JUMP_PX in one report: the tracker ends
     * the contact where it was, flagged NO_TAP, and starts another */
    finger(FEED_LONE, ARM_X - 3 * TOUCH_JUMP_PX, ARM_Y);
    CHECK(ui_router_alert() == NULL);
    lift(FEED_LONE);
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
}

TEST_CASE(arm_with_a_second_finger_shows_no_hint_for_the_second)
{
    router_on(SCREEN_MOTOR);
    motor_screen_set_armed(false);
    finger(FEED_LONE, ARM_X, ARM_Y);
    frames(2, Q);
    finger(2, ARM_X, ARM_Y);
    frames(1, Q);
    lift(2);
    CHECK(ui_router_alert() == NULL);
    /* the first finger's hold goes on and completes */
    frames(8, Q);
    lift(FEED_LONE);
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(took().kind, MOTOR_CMD_ARM);
}

/* ---------------------------------------------------------------- SUPPLY */

static void supply_up(void)
{
    router_on(SCREEN_SUPPLY);
    supply_screen_set_output(false);
    supply_cmd_t junk;
    while (supply_screen_poll_cmd(&junk)) { }
}

TEST_CASE(output_on_released_at_1999ms_shows_the_hint_and_sends_nothing)
{
    supply_up();
    finger(FEED_LONE, OUT_X, OUT_Y);
    hold_1999();
    lift(FEED_LONE);
    CHECK(hint_shows(TX_HOLD_HINT_OUTPUT_ON));
    supply_cmd_t c;
    CHECK(!supply_screen_poll_cmd(&c));
}

TEST_CASE(output_on_released_at_2000ms_switches_on_and_shows_no_hint)
{
    supply_up();
    finger(FEED_LONE, OUT_X, OUT_Y);
    frames(8, Q);
    lift(FEED_LONE);
    CHECK(ui_router_alert() == NULL);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.on);
}

TEST_CASE(output_on_slid_off_shows_no_hint)
{
    supply_up();
    finger(FEED_LONE, OUT_X, OUT_Y);
    frames(2, Q);
    glide(FEED_LONE, SU_PLOT_X, UI_BAND_H + SU_PLOT_Y, 8);
    lift(FEED_LONE);
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(a_tap_that_switches_the_output_off_shows_no_hint)
{
    supply_up();
    supply_screen_set_output(true);
    feed_tap(FEED_LONE, OUT_X, OUT_Y);
    CHECK(ui_router_alert() == NULL);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
}

/* ----------------------------------------------------------------- texts */

TEST_CASE(every_hint_names_the_2s_hold_in_both_languages)
{
    for (int id = TX_HOLD_HINT_ARM; id <= TX_HOLD_HINT_UNBIND; ++id) {
        const char *en = ui_tr_in(UI_LANG_EN, (ui_text_id_t)id);
        const char *de = ui_tr_in(UI_LANG_DE, (ui_text_id_t)id);
        CHECK(strncmp(en, "Hold 2s to ", 11) == 0);
        CHECK(strncmp(de, "Halte 2s, um ", 13) == 0);
        CHECK(en[strlen(en) - 1] == '.');
        CHECK(de[strlen(de) - 1] == '.');
    }
}

int main(void)
{
    RUN(a_release_at_1999ms_shows_the_hint_and_fires_nothing);
    RUN(a_release_at_2000ms_fires_and_shows_no_hint);
    RUN(a_tap_with_no_time_held_shows_the_hint);
    RUN(a_press_that_left_the_control_shows_no_hint);
    RUN(a_release_off_the_control_shows_no_hint);
    RUN(a_release_that_is_no_tap_shows_no_hint);
    RUN(a_press_dropped_without_a_release_shows_no_hint);
    RUN(a_state_that_went_away_under_the_press_shows_no_hint);
    RUN(no_hint_shows_where_the_screen_has_no_band);
    RUN(the_hint_shows_in_the_language_showing);
    RUN(the_hint_is_there_at_3999ms_and_gone_at_4000ms);
    RUN(the_hint_is_amber_and_an_alert_is_the_danger_colour);
    RUN(an_alert_that_arrives_replaces_the_hint);
    RUN(a_hint_does_not_show_while_an_alert_is_up);
    RUN(a_tap_on_the_band_clears_the_hint);
    RUN(stop_clears_the_hint_in_the_next_frame);
    RUN(a_switch_to_safe_or_a_new_fault_clears_the_hint);
    RUN(arm_released_at_1999ms_shows_the_hint_and_sends_nothing);
    RUN(arm_released_at_2000ms_arms_and_shows_no_hint);
    RUN(arm_slid_off_before_the_release_shows_no_hint);
    RUN(arm_with_a_lost_touch_shows_no_hint);
    RUN(arm_whose_contact_jumps_shows_no_hint);
    RUN(arm_with_a_second_finger_shows_no_hint_for_the_second);
    RUN(output_on_released_at_1999ms_shows_the_hint_and_sends_nothing);
    RUN(output_on_released_at_2000ms_switches_on_and_shows_no_hint);
    RUN(output_on_slid_off_shows_no_hint);
    RUN(a_tap_that_switches_the_output_off_shows_no_hint);
    RUN(every_hint_names_the_2s_hold_in_both_languages);
    return test_summary("hold_hint");
}
