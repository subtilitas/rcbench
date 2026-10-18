/*
 * SERVO as a workbench: the strip's tasks and what each keeps, the home
 * tag's levels, the TARGET row's lock, the run cards' lines, the read-only
 * supply caps and the knob on each task.
 *
 * The screen and its fixtures are servo_rig.h, the bench
 * servo_bench_rig.h, the spots servo_spots.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "kst_panel.h"
#include "servo_screen.h"
#include "servo_sim.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_workbench.h"
#include "ui_theme.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/* ---------------------------------------------------------------- the strip */

/*
 * A tile opens LIVE; a tab switch keeps SETTINGS' page and TEST's open run
 * settings for the session; leaving closes the run's settings (a level
 * down) and keeps the page; a reset, the panel's start, goes back to
 * OUTPUT.
 */
TEST_CASE(a_tile_opens_live_and_each_task_keeps_its_place)
{
    fresh();
    scr->enter();
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    open_settings();
    CHECK_EQ(servo_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_EQ(servo_screen_settings_page(), PAGE_OUTPUT);
    tap(TAB_X(PAGE_DUT), TAB_Y);
    CHECK_EQ(servo_screen_settings_page(), PAGE_DUT);
    open_run_settings(1);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);
    CHECK_EQ(servo_screen_run_settings(), 2);
    close_settings();
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    open_settings();
    CHECK_EQ(servo_screen_settings_page(), PAGE_DUT);
    open_test();
    CHECK_EQ(servo_screen_run_settings(), 2);

    scr->leave();
    scr->enter();
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    CHECK_EQ(servo_screen_run_settings(), 0);
    open_settings();
    CHECK_EQ(servo_screen_settings_page(), PAGE_DUT);

    scr->reset();
    scr->enter();
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    open_settings();
    CHECK_EQ(servo_screen_settings_page(), PAGE_OUTPUT);
}

/* A tab switch is no level: over a panel the home tag closes the panel and
 * the page under it stays; from a run's settings it climbs to TEST; on a
 * task with no panel it lets the screen go, which switches to SAFE. */
TEST_CASE(the_home_tag_closes_a_panel_climbs_from_a_runs_settings_and_leaves)
{
    fresh();
    scr->enter();
    CHECK(!home_tag());                                 /* LIVE */
    open_test();
    CHECK(!home_tag());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);
    open_settings();
    CHECK(!home_tag());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_SETTINGS);

    for (int k = 0; k < 2; ++k) {
        open_run_settings(k);
        CHECK_EQ(servo_screen_run_settings(), k + 1);
        CHECK(home_tag());
        CHECK_EQ(servo_screen_run_settings(), 0);
        CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);
        CHECK(!home_tag());
    }

    /* IDLE CURRENT's keypad over the list: the tag closes the keypad, the
     * keypad's keys do nothing after it, and the list stays; the next tag
     * climbs to TEST. */
    const float idle = settings_get(SET_SERVO_IDLE_MAX);
    open_run_settings(0);
    tap(ROW_L_X, LROW_Y(6));
    CHECK(home_tag());
    CHECK_EQ(servo_screen_run_settings(), 1);
    keys("0.7");
    CHECK_NEAR(settings_get(SET_SERVO_IDLE_MAX), idle, 1e-6f);
    CHECK(home_tag());
    CHECK_EQ(servo_screen_run_settings(), 0);
    CHECK(!home_tag());

    /* A list on SETTINGS (TYPE) and the TARGET choice on LIVE: one tag
     * closes each, nothing is chosen, and the task stays; a tap where the
     * choice's line was reaches the page under it. */
    open_settings();
    tap(TAB_X(PAGE_OUTPUT), TAB_Y);
    tap(ROW_L_X, ROW_Y(0));
    CHECK(home_tag());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    CHECK(!home_tag());
    close_settings();
    tap(SV_TARGET_X, SV_TARGET_Y);
    CHECK(home_tag());
    tap(CH_X(1), CH_Y(1));                              /* PROG PORT's line */
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    CHECK(!home_tag());

    /* A set point's keypad on an armed bench: closed, the bench stays
     * ARMED; the tag after it leaves, and leaving switches to SAFE. */
    fresh_armed();
    scr->enter();
    tap(SUP_V_X, SUP_ROW_Y);
    CHECK(home_tag());
    servo_cmd_t c;
    bool disarm = false;
    while (servo_screen_take(&c)) {
        disarm = disarm || c.kind == SERVO_CMD_DISARM;
    }
    CHECK(!disarm);
    CHECK(!home_tag());
    scr->leave();
    while (servo_screen_take(&c)) {
        disarm = disarm || c.kind == SERVO_CMD_DISARM;
    }
    CHECK(disarm);
}

/* A second finger switching the task drops START TEST's hold under way,
 * and the hold does not go on when TEST comes back. */
TEST_CASE(a_tab_switch_drops_a_start_hold)
{
    bench_fresh();
    open_test();
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 50; ++i) {
        scr->tick(0.02f);                               /* 1 s of the 2 s */
    }
    ev(SV_TAB_LIVE_X, SV_TAB_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_TAB_LIVE_X, SV_TAB_Y, TOUCH_EVENT_UP, 2);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    ev(SV_TAB_TEST_X, SV_TAB_Y, TOUCH_EVENT_DOWN, 2);
    ev(SV_TAB_TEST_X, SV_TAB_Y, TOUCH_EVENT_UP, 2);
    for (int i = 0; i < 100; ++i) {
        scr->tick(0.02f);
    }
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_testing());
    hold_start(2.3f);
    CHECK(servo_screen_testing());
}

/* ----------------------------------------------------------- the TARGET row */

/* The TARGET row's value box, mirrored from servo_screen_int.h. */
static const gfx_rect_t k_tgt_box = { 578, 326, 204, 22 };

static unsigned edge_ink(void)
{
    scr->render(&cv, 0);
    unsigned n = 0u;
    for (int y = k_tgt_box.y; y < k_tgt_box.y + k_tgt_box.h; ++y) {
        for (int x = k_tgt_box.x; x < k_tgt_box.x + k_tgt_box.w; ++x) {
            n += (fb[(size_t)y * W + (size_t)x] == UI_EDGE) ? 1u : 0u;
        }
    }
    return n;
}

/* Whether a tap on TARGET and one on the choice's second line set PROG
 * PORT; the target goes back to OUTPUTS after, as the bench allows. */
static bool target_tap_sets(void)
{
    const int was = settings_get_int(SET_SERVO_TARGET);
    tap(SV_TARGET_X, SV_TARGET_Y);
    tap(CH_X(1), CH_Y(1));
    const bool set = settings_get_int(SET_SERVO_TARGET) == 1 && was == 0;
    return set;
}

/* The row takes no press, and is drawn so, while ARMED, while a sweep and
 * while a run is under way; at rest a tap opens the choice. */
TEST_CASE(the_target_row_locks_for_each_condition)
{
    fresh();
    scr->enter();
    CHECK(!servo_screen_target_locked());
    CHECK_EQ(edge_ink(), 0u);
    CHECK(target_tap_sets());
    settings_set(SET_SERVO_TARGET, 0.0f);

    /* ARMED. */
    fresh_armed();
    CHECK(servo_screen_target_locked());
    CHECK(edge_ink() > 100u);
    CHECK(!target_tap_sets());
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);

    /* A sweep: locked, and still after its STOP until the bench is SAFE. */
    fresh_armed();
    servo_screen_set_sweep(true);
    tap(ARM_X + ARM_W / 2, 350 + 16);                   /* SWEEP */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
    CHECK(servo_screen_sweeping());
    CHECK(servo_screen_target_locked());
    CHECK(!target_tap_sets());

    /* An automatic test, from its card. */
    bench_fresh();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    CHECK(servo_screen_target_locked());
    CHECK(!target_tap_sets());
    servo_screen_cancel_arm();
    servo_screen_set_armed(false);
    bench_frames(200u);
    CHECK(!servo_screen_testing());
    CHECK(!servo_screen_target_locked());
    CHECK_EQ(edge_ink(), 0u);

    /* A limit run, from its card, on the INA3221. */
    bench_on_ina();
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    servo_sim_init(&bench.sv, &sc);
    bench.sv.position_us = 1500.0f;
    bench.out = true;
    servo_screen_set_pins(PIN(2), true);
    bench_frames(200u);
    hold_start_on(1, 2.3f);
    CHECK(servo_screen_limit_running());
    CHECK(servo_screen_target_locked());
    CHECK(!target_tap_sets());
}

/* ----------------------------------------------------------- the run cards */

/* Buffer 0 drawn, and whether line @p i of card @p k (its state line is
 * 0) reads @p text in @p col on the card's fill, and nothing else. */
static bool card_line_reads(int k, int i, const char *text, ui_color_id_t col)
{
    scr->render(&cv, 0);
    want_fresh();
    const int y = 46 + 178 * k + 98 + 18 * i + (k == 1 ? 18 : 0);
    const gfx_color_t bg = fb[(size_t)y * W + 20u];
    gfx_fill_rect(&want_cv, 18, (int16_t)y, 464, 16, bg);
    gfx_text(&want_cv, 26, y, text, &gfx_font_8x16, ui_theme_color(col), 1);
    return same_in(fb, want_fb, 18, y, 464, 16);
}

/* Each card's state line says ARM FIRST on a SAFE bench and HOLD 2 S TO
 * START on an armed one; an HV step makes START TEST a tap that opens the
 * warning and the line says so. */
TEST_CASE(the_run_cards_say_what_their_start_needs)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        bench_fresh();
        servo_screen_set_armed(false);
        bench_frames(100u);
        open_test();
        CHECK(card_line_reads(0, 0, ui_servo_str(SERVO_STR_START_NOT_ARMED),
                              UI_C_WARN));
        CHECK(card_line_reads(1, 0, ui_servo_str(SERVO_STR_START_NOT_ARMED),
                              UI_C_WARN));
        servo_screen_set_armed(true);
        bench_frames(100u);
        CHECK(card_line_reads(0, 0, TR(SV_HOLD_START), UI_C_TEXT_FAINT));
        settings_set(SET_SERVO_STEP_84, 1.0f);
        open_run_settings(0);
        tap(ROW_R_X, HV_ROW_Y);                         /* HV SERVO on */
        CHECK(home_tag());
        CHECK(card_line_reads(0, 0, TR(SV_TAP_HV), UI_C_TEXT_FAINT));
    }
    ui_text_set_language(UI_LANG_EN);
}

/* ------------------------------------------------------- the read-only caps */

/* Whether the caps line at (@p x, @p y) reads SUPPLY's caps in force. */
static bool caps_read(int x, int y)
{
    const supply_caps_t caps = supply_screen_caps();
    char line[64];
    snprintf(line, sizeof(line), TR(SV_CAPS), (double)caps.v_max,
             (double)caps.i_max);
    scr->render(&cv, 0);
    want_fresh();
    const gfx_color_t bg = fb[(size_t)y * W + (size_t)(x - 2)];
    gfx_fill_rect(&want_cv, (int16_t)(x - 4), (int16_t)y, 470, 16, bg);
    gfx_text(&want_cv, x, y, line, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    return same_in(fb, want_fb, x - 4, y, 470, 16);
}

/* The caps line under TEST's cards and on SETTINGS > DUT reads SUPPLY's
 * caps in force, follows them when they change, names their home in both
 * languages, and a tap on it edits nothing. */
TEST_CASE(the_supply_caps_show_read_only_with_their_home)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        fresh();
        scr->enter();
        open_test();
        CHECK(caps_read(16, 404));
        supply_caps_t caps = supply_screen_caps();
        CHECK_NEAR(caps.v_max, 21.0f, 1e-4f);
        settings_set(SET_SUPPLY_V_MAX, 12.0f);
        settings_set(SET_SUPPLY_I_MAX, 3.0f);
        supply_screen_settings_loaded();
        servo_invalidate();
        caps = supply_screen_caps();
        CHECK_NEAR(caps.v_max, 12.0f, 1e-4f);
        CHECK(caps_read(16, 404));
        tap(60, 412);
        CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 12.0f, 1e-4f);
        CHECK(caps_read(16, 404));                      /* no keypad */

        open_settings();
        tap(TAB_X(PAGE_DUT), TAB_Y);
        CHECK(caps_read(16, 280));
        tap(60, 288);
        CHECK(caps_read(16, 280));
        CHECK_NEAR(settings_get(SET_SUPPLY_I_MAX), 3.0f, 1e-4f);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* AS5600 has one home, SETUP > INTERFACES; DUT keeps ENC CENTRE, the
 * action on the servo. */
TEST_CASE(the_as5600_lives_on_interfaces)
{
    const setting_def_t *d = settings_def(SET_ENC_EN);
    CHECK(d != NULL);
    CHECK(d != NULL && d->cat == SET_CAT_IFACE);
}

/* ------------------------------------------------------------------ the knob */

/* Whether a knob turn this frame moves the horn. */
static bool knob_moves(void)
{
    servo_screen_knob_frame();
    servo_screen_knob(0.05f);
    const servo_cmd_t c = last_cmd();
    servo_screen_knob_cancel();
    servo_cmd_t junk;
    while (servo_screen_take(&junk)) { }
    return c.kind == SERVO_CMD_POSITION;
}

/* The knob moves the horn on LIVE alone: not on TEST, a run's settings,
 * SETTINGS, under a panel or the programmer; a tap on LIVE in the frame
 * of the turn does not let that turn through, the next one is. */
TEST_CASE(the_knob_moves_the_horn_on_live_alone)
{
    fresh_armed();
    scr->enter();
    CHECK(knob_moves());
    open_test();
    CHECK(!knob_moves());
    open_run_settings(0);
    CHECK(!knob_moves());
    open_settings();
    CHECK(!knob_moves());
    tap(TAB_X(PAGE_SWEEP), TAB_Y);
    tap(ROW_L_X, ROW_Y(1));                         /* SPEED's keypad */
    CHECK(!knob_moves());
    key(UI_KEY_CANCEL);
    close_settings();
    CHECK(knob_moves());

    /* The turn of the frame in which LIVE came back. */
    open_test();
    servo_screen_knob_frame();
    close_settings();
    servo_screen_knob(0.05f);
    CHECK(last_cmd().kind != SERVO_CMD_POSITION);
    CHECK(knob_moves());

    /* The programmer over LIVE. */
    tap(SV_PROGRAM_X, SV_TAB_Y);
    CHECK(kst_panel_is_open());
    CHECK(!knob_moves());
    tap(SV_PROGRAM_X, SV_TAB_Y);
    CHECK(!kst_panel_is_open());
    CHECK(knob_moves());
}

int main(void)
{
    RUN(a_tile_opens_live_and_each_task_keeps_its_place);
    RUN(the_home_tag_closes_a_panel_climbs_from_a_runs_settings_and_leaves);
    RUN(a_tab_switch_drops_a_start_hold);
    RUN(the_target_row_locks_for_each_condition);
    RUN(the_run_cards_say_what_their_start_needs);
    RUN(the_supply_caps_show_read_only_with_their_home);
    RUN(the_as5600_lives_on_interfaces);
    RUN(the_knob_moves_the_horn_on_live_alone);
    return test_summary("servo_workbench");
}
