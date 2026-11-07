/*
 * The hold flash walk: every 2s hold this rig reaches, driven through its
 * screen, flashes as it fires.
 *
 * The flash starts in ui_hold_tick() on the frame the hold completes, and
 * ui_hold_button() draws it (shared/ui/ui_widgets.c), so a button flashes
 * whatever its screen does with the firing: a label that changes, a
 * warning that closes on it, an answer that comes later or never.  Each
 * case presses the real button, runs the frames at 60Hz, draws each one,
 * and asserts the button's body went white, black, white, black
 * (hold_flash.h).  ARMED, the output and the run are never confirmed
 * here: the flash does not wait for them.
 *
 * Here: ARM, OUTPUT ON, START TEST, START LIMIT RUN, HOLD TO APPLY on the
 * profile warning and KST EXPERT MODE on SERVO; ARM on MOTOR & ESC and
 * OUTPUT ON on SUPPLY through the router.  The holds of the other screens
 * have their *_flashes_as_it_fires case in their own suite, on their own
 * rig: test_servo_rauto_kst.c (START on AUTO), test_kst_panel_write.c
 * (a warning page's hold), test_hitec_panel_write.c (a hold page),
 * test_esc_wire_press.c (CONNECT), test_esc_stick_panel_hold.c (HOLD 2s
 * TO RUN), test_outputs_view.c (UNBIND ALL PINS), test_files_changed.c
 * (OVERWRITE); test_prog5v_box.c and test_stop_hold.c hold PROG 5V ON and
 * STOP's reset to their flash, test_busfault_screen.c ACKNOWLEDGE.  Not
 * driven on a screen: the HV warning's APPLY, the updates' and Wi-Fi's
 * holds, the IOmcu update's START, IMU CALIBRATE and RECONNECT;
 * tools/check_holds.py holds every ui_hold_begin() site to
 * ui_hold_button() and ui_hold_flash_step() in the source.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "link_pages.h"
#include "motor_screen.h"
#include "motor_spots.h"
#include "servo_screen.h"
#include "servo_sim.h"
#include "settings.h"
#include "splash_screen.h"
#include "supply_screen.h"
#include "supply_spots.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"
#include "hold_flash.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------ SERVO */

/* One frame of the bench: its clock, the supply's answers, the screen. */
static void servo_frame(float dt_s)
{
    (void)dt_s;
    bench_frames(20u);
}

static void servo_draw(void) { scr->render(&cv, 0); }

static const hold_flash_rig_t k_servo = { servo_frame, servo_draw, NULL, W };

/* The rig with this case's canvas. */
static hold_flash_rig_t servo_rig(void)
{
    hold_flash_rig_t r = k_servo;
    r.fb = fb;
    return r;
}

/* A press at (@p x, @p y), the walk over @p r, the release; the classes
 * seen named on a failure. */
static bool servo_walk(int x, int y, gfx_rect_t r, const char *what)
{
    const hold_flash_rig_t rig = servo_rig();
    char seen[64];
    scr->render(&cv, 0);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    const bool ok = hold_flash_walk(&rig, r, 2.6f, seen, sizeof(seen));
    ev(x, y, TOUCH_EVENT_UP, 1);
    if (!ok) {
        T_FAIL("%s drew %s, no flash", what, seen);
    }
    return ok;
}

/* ARM on LIVE: the bench never answers ARMED. */
TEST_CASE(servo_arm_flashes_as_it_fires)
{
    bench_fresh();
    servo_screen_set_armed(false);
    scr->tick(0.02f);
    const gfx_rect_t r = { (int16_t)(SV_ARM_L + 12), (int16_t)(SV_ARM_Y + 4),
                           (int16_t)(SV_ARM_W - 24), 8 };
    CHECK(servo_walk(SV_ARM_X, SV_ARM_Y + 16, r, "ARM"));
}

/* ARM on LIVE with the PD mini's output off: the hold fires the steps
 * that switch the supply on first, and flashes as it fires. */
TEST_CASE(servo_arm_that_switches_the_supply_flashes_as_it_fires)
{
    bench_fresh();
    bench.out = false;
    bench_frames(200u);
    servo_screen_set_armed(false);
    CHECK(servo_screen_arm_switches());
    const gfx_rect_t r = { (int16_t)(SV_ARM_L + 12), (int16_t)(SV_ARM_Y + 4),
                           (int16_t)(SV_ARM_W - 24), 8 };
    CHECK(servo_walk(SV_ARM_X, SV_ARM_Y + 16, r, "ARM"));
}

/* OUTPUT ON in the supply box on TEST's output card: the bench switches
 * the output on. */
TEST_CASE(servo_output_on_flashes_as_it_fires)
{
    bench_fresh();
    go(SERVO_FN_TEST);
    bench.out = false;
    bench_frames(200u);
    const gfx_rect_t r = { (int16_t)(SV_RC_X + 244), (int16_t)(SV_SUP_Y - 20),
                           (int16_t)(SV_RC_W - 256), 6 };
    CHECK(servo_walk(SV_SUP_OUT_X, SV_SUP_Y, r, "OUTPUT ON"));
    CHECK(bench.out);
}

/* START TEST on TEST: the run starts as the hold fires. */
TEST_CASE(start_test_flashes_as_it_fires)
{
    bench_fresh();
    open_test();
    scr->tick(0.02f);
    const gfx_rect_t r = { (int16_t)(START_X - 138), (int16_t)(START_Y - 20),
                           276, 8 };
    CHECK(servo_walk(START_X, START_Y, r, "START TEST"));
    CHECK(servo_screen_testing());
}

/* START LIMIT RUN on LIMIT RUN: on the INA3221, a SERVO PWM output bound. */
TEST_CASE(start_limit_run_flashes_as_it_fires)
{
    bench_on_ina();
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    servo_sim_init(&bench.sv, &sc);
    bench.sv.position_us = 1500.0f;
    bench.out = true;
    servo_screen_set_pins(PIN(2), true);
    bench_frames(200u);
    open_limrun();
    const gfx_rect_t r = { (int16_t)(START_X - 138),
                           (int16_t)(LIM_START_Y - 20), 276, 8 };
    CHECK(servo_walk(START_X, LIM_START_Y, r, "START LIMIT RUN"));
    CHECK(servo_screen_limit_running());
}

/* HOLD TO APPLY on the warning a heli type opens: the warning closes as
 * the hold fires, and the button flashes where it was. */
TEST_CASE(profile_apply_flashes_where_its_warning_was)
{
    fresh();
    open_settings();
    sl_tap(HL_TYPE);
    tap(CH_X(3), CH_Y(3));
    const gfx_rect_t r = { (int16_t)(WARN_APPLY_X - 108),
                           (int16_t)(WARN_Y - 24), 216, 8 };
    CHECK(servo_walk(WARN_APPLY_X, WARN_Y, r, "HOLD TO APPLY"));
    /* Over, the place is repainted whole: no white or black left. */
    scr->render(&cv, 0);
    const hold_flash_rig_t rig = servo_rig();
    CHECK_EQ(hold_flash_body(&rig, r), '.');
}

/* KST EXPERT MODE on KST > SETTINGS: its value box, OFF to ON. */
TEST_CASE(kst_expert_flashes_as_it_fires)
{
    fresh();
    settings_set(SET_KST_EXPERT, 0.0f);
    open_kst();
    tap_settings();
    scr->tick(0.02f);
    /* The row's value box, x 18 to 211 under its label; the top of its
     * body. */
    const gfx_rect_t r = { 34, (int16_t)(SV_LIST_ROW_Y(0) - 22), 160, 6 };
    CHECK(servo_walk(SV_LIST_X(0), SV_LIST_ROW_Y(0), r, "KST EXPERT MODE"));
    CHECK(settings_get_bool(SET_KST_EXPERT));
}

/* After the hold fired, the press dropped and a tap turned the mode OFF:
 * the value box is the sunk OFF again, with no fade left from the hold
 * that fired (the hold ended with the press, ui_hold_spend()). */
TEST_CASE(kst_expert_off_after_its_hold_shows_no_fade)
{
    fresh();
    settings_set(SET_KST_EXPERT, 0.0f);
    open_kst();
    tap_settings();
    scr->tick(0.02f);
    const gfx_rect_t r = { 34, (int16_t)(SV_LIST_ROW_Y(0) - 22), 160, 6 };
    CHECK(servo_walk(SV_LIST_X(0), SV_LIST_ROW_Y(0), r, "KST EXPERT MODE"));
    REQUIRE(settings_get_bool(SET_KST_EXPERT));
    tap(SV_LIST_X(0), SV_LIST_ROW_Y(0));
    for (int i = 0; i < 4; ++i) {
        scr->tick(0.02f);
    }
    CHECK(!settings_get_bool(SET_KST_EXPERT));
    scr->render(&cv, 0);
    const gfx_color_t px = fb[(size_t)(r.y + 2) * W + (size_t)(r.x + 50)];
    if (px != ui_theme_color(UI_C_PANEL_SUNK)) {
        T_FAIL("OFF drawn in %04x, not the sunk %04x", px,
               ui_theme_color(UI_C_PANEL_SUNK));
    }
}

/* ------------------------------------------- a tap on a refused button */

/* START TEST and START LIMIT RUN grey on a bench that is not ARMED: a tap
 * puts the reason in the band as a notice, and the line under the button
 * stays. */
TEST_CASE(a_tap_on_a_grey_start_puts_its_reason_in_the_band)
{
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_SERVO_LIVE);
    fresh();
    servo_screen_set_armed(false);
    open_test();
    ui_router_set_alert(NULL);
    tap(START_X, START_Y);
    CHECK(!servo_screen_testing());
    if (ui_router_alert() == NULL) {
        T_FAIL("a tap on the grey START TEST left the band empty");
        return;
    }
    CHECK(ui_router_alert_is_notice());
    CHECK_STR_EQ(ui_router_alert(),
                 ui_servo_str(SERVO_STR_START_NOT_ARMED));
    open_limrun();
    ui_router_set_alert(NULL);
    tap(START_X, LIM_START_Y);
    CHECK(!servo_screen_limit_running());
    CHECK(ui_router_alert() != NULL && ui_router_alert_is_notice());
    ui_router_goto(SCREEN_OVERVIEW);
}

/* ------------------------------------------------- through the router */

static gfx_color_t *pfb;
static gfx_canvas_t pcv;

static void router_frame(float dt_s) { ui_router_tick(dt_s); }

static void router_draw(void) { ui_router_render(&pcv, 0); }

/* The router on @p screen, the splash done, nothing on the glass. */
static void router_on(ui_screen_id_t screen)
{
    if (pfb == NULL) {
        pfb = calloc((size_t)UI_PANEL_W * UI_PANEL_H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(pfb);
    }
    gfx_canvas_init(&pcv, pfb, UI_PANEL_W, UI_PANEL_H, UI_PANEL_W);
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
    ui_router_invalidate();
    ui_router_render(&pcv, 0);
}

/* A finger at (@p x, @p y) of the panel, the walk over @p r, the lift. */
static bool router_walk(int x, int y, gfx_rect_t r, const char *what)
{
    const hold_flash_rig_t rig = { router_frame, router_draw, pfb,
                                   UI_PANEL_W };
    char seen[64];
    finger(FEED_LONE, x, y);
    const bool ok = hold_flash_walk(&rig, r, 2.6f, seen, sizeof(seen));
    lift(FEED_LONE);
    if (!ok) {
        T_FAIL("%s drew %s, no flash", what, seen);
    }
    return ok;
}

/* ARM on MOTOR & ESC: ARMED never answers. */
TEST_CASE(motor_arm_flashes_as_it_fires)
{
    router_on(SCREEN_MOTOR);
    motor_screen_set_armed(false);
    const gfx_rect_t r = { (int16_t)(MO_ARM_X - 60),
                           (int16_t)(UI_BAND_H + MO_ARM_T + 4), 120, 8 };
    CHECK(router_walk(MO_ARM_X, UI_BAND_H + MO_ARM_Y, r, "MOTOR ARM"));
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f, 0u };
    CHECK(motor_screen_poll_cmd(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_ARM);
}

/* OUTPUT ON on SUPPLY: the output never answers. */
TEST_CASE(supply_output_on_flashes_as_it_fires)
{
    router_on(SCREEN_SUPPLY);
    const gfx_rect_t r = { (int16_t)(SU_OUT_X - 60),
                           (int16_t)(UI_BAND_H + SU_OUT_T + 4), 120, 8 };
    CHECK(router_walk(SU_OUT_X, UI_BAND_H + SU_OUT_Y, r, "SUPPLY OUTPUT ON"));
}

int main(void)
{
    RUN(servo_arm_flashes_as_it_fires);
    RUN(servo_arm_that_switches_the_supply_flashes_as_it_fires);
    RUN(servo_output_on_flashes_as_it_fires);
    RUN(start_test_flashes_as_it_fires);
    RUN(start_limit_run_flashes_as_it_fires);
    RUN(profile_apply_flashes_where_its_warning_was);
    RUN(kst_expert_flashes_as_it_fires);
    RUN(kst_expert_off_after_its_hold_shows_no_fade);
    RUN(a_tap_on_a_grey_start_puts_its_reason_in_the_band);
    RUN(motor_arm_flashes_as_it_fires);
    RUN(supply_output_on_flashes_as_it_fires);
    return test_summary("hold_flash");
}
