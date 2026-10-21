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
#include "link_pages.h"
#include "outputs_screen.h"
#include "prog_registry.h"
#include "servo_screen.h"
#include "servo_sim.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_workbench.h"
#include "ui_theme.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/*
 * Whether a tap on TARGET opens SETUP > OUTPUTS: one navigation, to
 * OUTPUTS.  The router is on the menu before and after, so every tap can
 * open it again; this screen is driven on its own and is not the router's.
 */
static bool target_tap_opens(void)
{
    if (ui_router_current() != SCREEN_OVERVIEW) {
        ui_router_goto(SCREEN_OVERVIEW);
    }
    const uint32_t n = ui_router_navigations();
    tap(SV_TARGET_X, SV_TARGET_Y);
    const bool opened = ui_router_navigations() == n + 1u
                        && ui_router_current() == SCREEN_OUTPUTS;
    ui_router_goto(SCREEN_OVERVIEW);
    return opened;
}

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

    /* CLOSE on a run's settings climbs to TEST, as the tag does; a press
     * that slides off it closes nothing; the TEST tab opens the cards
     * again after it, and the card's SETTINGS opens the page again. */
    for (int k = 0; k < 2; ++k) {
        open_run_settings(k);
        CHECK_EQ(servo_screen_run_settings(), k + 1);
        tap(SV_RUN_CLOSE_X, SV_RUN_CLOSE_Y);
        CHECK_EQ(servo_screen_run_settings(), 0);
        CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);
        open_test();
        CHECK_EQ(servo_screen_run_settings(), 0);
        open_run_settings(k);
        CHECK_EQ(servo_screen_run_settings(), k + 1);
        ev(SV_RUN_CLOSE_X, SV_RUN_CLOSE_Y, TOUCH_EVENT_DOWN, 1);
        ev(SV_RUN_CLOSE_X, SV_RUN_CLOSE_Y + 120, TOUCH_EVENT_MOVE, 1);
        ev(SV_RUN_CLOSE_X, SV_RUN_CLOSE_Y + 120, TOUCH_EVENT_UP, 1);
        CHECK_EQ(servo_screen_run_settings(), k + 1);
        /* A second tap on the card's SETTINGS spot stays on the page. */
        tap(SV_RUN_SET_X, SV_RUN_SET_Y(0));
        CHECK_EQ(servo_screen_run_settings(), k + 1);
        CHECK(home_tag());
        CHECK_EQ(servo_screen_run_settings(), 0);
    }
    /* On SETTINGS' pages the spot is no CLOSE. */
    open_settings();
    tap(SV_RUN_CLOSE_X, SV_RUN_CLOSE_Y);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_SETTINGS);

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

    /* A list on SETTINGS (TYPE): one tag closes it, nothing is chosen,
     * and the task stays.  TARGET on LIVE opens no list on this screen: it
     * opens OUTPUTS, and the target stays. */
    open_settings();
    tap(TAB_X(PAGE_OUTPUT), TAB_Y);
    tap(ROW_L_X, ROW_Y(0));
    CHECK(home_tag());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    CHECK(!home_tag());
    close_settings();
    CHECK(target_tap_opens());
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

/* The row takes no press, and is drawn so, while ARMED, while a sweep and
 * while a run is under way; at rest a tap opens OUTPUTS.  No tap sets the
 * target here. */
TEST_CASE(the_target_row_locks_for_each_condition)
{
    fresh();
    scr->enter();
    CHECK(!servo_screen_target_locked());
    CHECK_EQ(edge_ink(), 0u);
    CHECK(target_tap_opens());
    settings_set(SET_SERVO_TARGET, 0.0f);

    /* ARMED. */
    fresh_armed();
    CHECK(servo_screen_target_locked());
    CHECK(edge_ink() > 100u);
    CHECK(!target_tap_opens());
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);

    /* A sweep: locked, and still after its STOP until the bench is SAFE. */
    fresh_armed();
    servo_screen_set_sweep(true);
    tap(ARM_X + ARM_W / 2, 350 + 16);                   /* SWEEP */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
    CHECK(servo_screen_sweeping());
    CHECK(servo_screen_target_locked());
    CHECK(!target_tap_opens());

    /* An automatic test, from its card. */
    bench_fresh();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    CHECK(servo_screen_target_locked());
    CHECK(!target_tap_opens());
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
    CHECK(!target_tap_opens());
}

/*
 * Through the router, as the panel runs: a tap on TARGET leaves SERVO for
 * SETUP > OUTPUTS -- on SERVO PWM with the outputs driven, on the PROG
 * PORT entry with the port -- and the home tag there comes back
 * to SERVO on the task the row was tapped on.  A screen between the two
 * makes the next entry a tile's, which opens LIVE; OUTPUTS from SETUP's
 * tile opens on the overview and goes back to SETUP.
 */
TEST_CASE(the_target_row_opens_outputs_on_what_the_tester_drives)
{
    for (int target = 0; target < 2; ++target) {
        for (int task = 0; task < 2; ++task) {
            fresh();
            ui_router_init();
            ui_router_goto(SCREEN_SERVO);
            settings_set(SET_SERVO_TARGET, (float)target);
            servo_screen_set_drive((uint8_t)target, 13u, true);
            if (task == 1) {
                open_settings();
            }
            tap(SV_TARGET_X, SV_TARGET_Y);
            CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
            CHECK_EQ(ui_router_parent(SCREEN_OUTPUTS), SCREEN_SERVO);
            if (target == 1) {
                CHECK_EQ(outputs_screen_port_shown(), 0);
                CHECK_EQ((int)outputs_screen_binding()->proto, 0);
            } else {
                CHECK_EQ(outputs_screen_port_shown(), -1);
                CHECK_STR_EQ(
                    outbind_protos()[outputs_screen_binding()->proto].name,
                    "SERVO PWM");
            }
            /* The tag's way back. */
            ui_router_goto(ui_router_parent(SCREEN_OUTPUTS));
            CHECK_EQ(ui_router_current(), SCREEN_SERVO);
            CHECK_EQ(servo_screen_task(), task == 0 ? (int)UI_WB_LIVE
                                                    : (int)UI_WB_SETTINGS);
        }
    }
    /* A route elsewhere between the two: LIVE. */
    fresh();
    ui_router_init();
    ui_router_goto(SCREEN_SERVO);
    open_settings();
    tap(SV_TARGET_X, SV_TARGET_Y);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SERVO);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    /* OUTPUTS from SETUP's tile: the overview, back to SETUP. */
    settings_set(SET_SERVO_TARGET, 1.0f);
    ui_router_goto(SCREEN_SETUP);
    ui_router_goto(SCREEN_OUTPUTS);
    CHECK_EQ(outputs_screen_port_shown(), -1);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ(ui_router_parent(SCREEN_OUTPUTS), SCREEN_SETUP);
    settings_set(SET_SERVO_TARGET, 0.0f);
    ui_router_goto(SCREEN_OVERVIEW);
}

/*
 * TARGET opens OUTPUTS on what the row shows, where the IOmcu sends the
 * commands now, and not on the TARGET setting: a write still pending, or
 * PROG PORT asked for with no pin on the port (the IOmcu keeps the
 * outputs), open on what is driven.  A port without a pin as the drive
 * opens its entry; a target not known, no link, or a port the list does
 * not hold open the overview.
 */
TEST_CASE(the_target_row_opens_on_the_drive_not_the_setting)
{
    enum { PWM = -2, OVERVIEW = -1 };
    static const struct {
        int     setting;
        uint8_t target, gpio;
        int     opens;            /* a port entry, PWM or OVERVIEW */
    } k[] = {
        { 1, 0u, 13u, PWM },                       /* write pending */
        { 1, 0u, SERVO_SCREEN_NO_PIN, PWM },       /* port without a pin */
        { 0, 1u, 13u, 0 },                         /* write back pending */
        { 1, 1u, SERVO_SCREEN_NO_PIN, 0 },         /* the port, no pin */
        { 1, SERVO_SCREEN_TARGET_UNKNOWN, 13u, OVERVIEW },
        { 0, SERVO_SCREEN_TARGET_NO_LINK, 13u, OVERVIEW },
        { 1, 2u, 26u, OVERVIEW },                  /* port 2, one entry */
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        fresh();
        ui_router_init();
        ui_router_goto(SCREEN_SERVO);
        settings_set(SET_SERVO_TARGET, (float)k[i].setting);
        servo_screen_set_drive(k[i].target, k[i].gpio, true);
        tap(SV_TARGET_X, SV_TARGET_Y);
        if (ui_router_current() != SCREEN_OUTPUTS) {
            T_FAIL("case %u: no OUTPUTS", (unsigned)i);
        }
        const int port = outputs_screen_port_shown();
        const char *name =
            outbind_protos()[outputs_screen_binding()->proto].name;
        if (k[i].opens >= 0) {
            CHECK_EQ(port, k[i].opens);
        } else if (k[i].opens == PWM) {
            CHECK_EQ(port, -1);
            CHECK_STR_EQ(name, "SERVO PWM");
        } else {
            CHECK_EQ(port, -1);
            CHECK_EQ((int)outputs_screen_binding()->proto, 0);
        }
        CHECK_EQ(settings_get_int(SET_SERVO_TARGET), k[i].setting);
    }
    settings_set(SET_SERVO_TARGET, 0.0f);
    ui_router_goto(SCREEN_OVERVIEW);
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

    /* PROGRAM's list, and the programmer opened from it. */
    open_program();
    CHECK(!knob_moves());
    tap(SV_PROG_ROW_X, SV_PROG_ROW_Y(0));
    CHECK(kst_panel_is_open());
    CHECK(!knob_moves());
    close_kst();
    CHECK(!kst_panel_is_open());
    CHECK(!knob_moves());
    close_settings();
    CHECK(knob_moves());
}

/* ----------------------------------------------------------- PROGRAM */

/* The strip reads LIVE, TEST, PROGRAM, SETTINGS from the left, 112 px a
 * tab; a tap on each selects it, and PROGRAM shows its list with no
 * session open. */
TEST_CASE(the_strip_reads_live_test_program_settings)
{
    fresh();
    scr->enter();
    static const struct { int x; ui_wb_task_t task; } k[] = {
        { SV_TAB_TEST_X, UI_WB_TEST },
        { SV_TAB_PROGRAM_X, UI_WB_PROGRAM },
        { SV_TAB_SETTINGS_X, UI_WB_SETTINGS },
        { SV_TAB_LIVE_X, UI_WB_LIVE },
        { SV_TAB_PROGRAM_X, UI_WB_PROGRAM },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        tap(k[i].x, SV_TAB_Y);
        CHECK_EQ(servo_screen_task(), (int)k[i].task);
        CHECK(!kst_panel_is_open());
    }
    /* The tabs in order, 116 px apart from x 9. */
    for (int t = 0; t < (int)UI_WB_TASKS; ++t) {
        CHECK_EQ(SV_TAB_LIVE_X + 116 * t,
                 (t == 0) ? SV_TAB_LIVE_X : (t == 1) ? SV_TAB_TEST_X
                 : (t == 2) ? SV_TAB_PROGRAM_X : SV_TAB_SETTINGS_X);
    }
    /* PROGRAM's page draws in both languages; a tile opens LIVE again. */
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        servo_invalidate();
        scr->render(&cv, 0);
    }
    ui_text_set_language(UI_LANG_EN);
    scr->leave();
    scr->enter();
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
}

/* With no SERVO PWM reported (no IOmcu answering, or one without the
 * bank) the KST row is drawn dimmed with its reason, and a tap on it opens
 * nothing; once reported it opens the session. */
TEST_CASE(a_row_the_iomcu_lacks_is_dimmed_and_opens_nothing)
{
    fresh();
    scr->enter();
    ui_bench_status_t st = *ui_router_status();
    st.capabilities = 0u;
    ui_router_set_status(&st);
    open_program();
    scr->render(&cv, 0);
    /* The reason, in the warning colour, inside the row. */
    const gfx_color_t warn = ui_theme_color(UI_C_WARN);
    int warned = 0;
    for (int y = SV_PROG_ROW_Y(0) - 17; y < SV_PROG_ROW_Y(0) + 17; ++y) {
        for (int x = 16; x < 484; ++x) {
            warned += fb[y * W + x] == warn;
        }
    }
    CHECK(warned > 0);
    tap(SV_PROG_ROW_X, SV_PROG_ROW_Y(0));
    CHECK(!kst_panel_is_open());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);

    /* Reported: the page repaints on its own and the row opens. */
    st.capabilities = LINK_CAP_SERVO_PWM;
    ui_router_set_status(&st);
    scr->tick(0.02f);
    scr->render(&cv, 0);
    tap(SV_PROG_ROW_X, SV_PROG_ROW_Y(0));
    CHECK(kst_panel_is_open());
    close_kst();
    CHECK(!kst_panel_is_open());
}

/* The row's detail names the tester's port: a move of SERVO TESTER from
 * OUTPUTS to PORT 1 repaints it on its own, with the row's state and pin
 * unchanged. */
TEST_CASE(the_program_row_repaints_when_the_testers_port_moves)
{
    fresh();
    scr->enter();
    prog_registry_set_ports(PROG_REG_NO_PORT, 0u);
    open_program();
    scr->tick(0.02f);
    scr->render(&cv, 0);
    const gfx_color_t bg = fb[(SV_PROG_ROW_Y(0)) * W + 470];
    unsigned before = 0u;
    for (int y = SV_PROG_ROW_Y(0) - 17; y < SV_PROG_ROW_Y(0) + 17; ++y) {
        for (int x = 200; x < 484; ++x) {
            before += fb[y * W + x] != bg;
        }
    }
    prog_registry_set_ports(0u, 0u);
    scr->tick(0.02f);
    scr->render(&cv, 0);
    unsigned after = 0u;
    for (int y = SV_PROG_ROW_Y(0) - 17; y < SV_PROG_ROW_Y(0) + 17; ++y) {
        for (int x = 200; x < 484; ++x) {
            after += fb[y * W + x] != bg;
        }
    }
    CHECK(after > before);           /* "PROG PORT 1 ..." over "PROG PORT" */
    prog_registry_set_ports(PROG_REG_NO_PORT, 0u);
}

/* The session is a level below PROGRAM's list: the home tag closes it to
 * the list, the next one leaves the screen.  (An editor in the session and
 * a port in programming: test_kst_panel_screen.c.) */
TEST_CASE(the_home_tag_climbs_from_the_session_to_the_list_and_leaves)
{
    fresh();
    scr->enter();
    open_kst();
    CHECK(kst_panel_is_open());
    CHECK(home_tag());
    CHECK(!kst_panel_is_open());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);
    CHECK(!home_tag());

    /* Leaving with the session open closes it; the tile opens LIVE. */
    tap(SV_PROG_ROW_X, SV_PROG_ROW_Y(0));
    CHECK(kst_panel_is_open());
    scr->leave();
    CHECK(!kst_panel_is_open());
    scr->enter();
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
}

/* A key of the search keyboard, where PROGRAM opens it over the left
 * column. */
static void search_key(int k)
{
    ui_textkey_t probe;
    memset(&probe, 0, sizeof(probe));
    ui_textkey_open_search(&probe, (gfx_rect_t){ 6, 6, 488, 420 }, "", "",
                           16);
    const gfx_rect_t r = ui_textkey_key_rect(&probe, k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/* SEARCH's box, a row under the bar, and a row under the bar and a
 * heading, in the page area. */
#define SEARCH_X 126
#define SEARCH_Y 63
#define BARRED_ROW_Y(r) (103 + 40 * (r))
#define HEADED_ROW_Y(r) (131 + 40 * (r))

/*
 * A list of more than 9 rows on the screen (a table handed in): SEARCH
 * opens the keyboard, a search narrows the rows, a narrowed row opens its
 * programmer, and the home tag over the keyboard closes the keyboard and
 * keeps the search.
 */
TEST_CASE(program_offers_search_over_nine_rows_and_narrows_the_list)
{
    static prog_reg_row_t rows[12];
    for (int i = 0; i < 12; ++i) {
        rows[i] = (prog_reg_row_t){ PROG_DEV_SERVO, UI_WB_PROGRAM,
                                    (i == 11) ? TX_PL_KST : TX_WB_LIVE,
                                    TX_PL_GRP_PROG_PORT, TX_SV_TARGET_PORT,
                                    PROG_REG_NO_PORT, LINK_CAP_SERVO_PWM,
                                    &kst_panel_pages, false };
    }
    prog_registry_use(rows, 12);
    fresh();
    scr->enter();
    open_program();
    scr->render(&cv, 0);
    /* Twelve rows under their heading: a tap on the heading opens
     * nothing, one on a row its programmer. */
    tap(SV_PROG_ROW_X, BARRED_ROW_Y(0) - 5);
    CHECK(!kst_panel_is_open());
    tap(SV_PROG_ROW_X, HEADED_ROW_Y(0));
    CHECK(kst_panel_is_open());
    close_kst();

    /* SEARCH: the keyboard; K, S, T and OK narrow the list to the KST
     * row, now the first under the bar. */
    tap(SEARCH_X, SEARCH_Y);
    scr->render(&cv, 0);
    search_key(27);                                     /* K */
    search_key(21);                                     /* S */
    search_key(14);                                     /* T */
    search_key(UI_TK_OK);
    scr->render(&cv, 0);
    tap(SV_PROG_ROW_X, BARRED_ROW_Y(1));                /* nothing there */
    CHECK(!kst_panel_is_open());
    tap(SV_PROG_ROW_X, BARRED_ROW_Y(0));
    CHECK(kst_panel_is_open());
    close_kst();

    /* The home tag over the keyboard closes it and keeps the search; the
     * next one leaves. */
    tap(SEARCH_X, SEARCH_Y);
    CHECK(home_tag());
    search_key(UI_TK_OK);                               /* no keyboard */
    tap(SV_PROG_ROW_X, BARRED_ROW_Y(0));
    CHECK(kst_panel_is_open());
    close_kst();
    CHECK(!home_tag());

    /* An empty search is no search: all twelve again, under the
     * heading. */
    tap(SEARCH_X, SEARCH_Y);
    search_key(UI_TK_CLR);
    search_key(UI_TK_OK);
    tap(SV_PROG_ROW_X, HEADED_ROW_Y(1));
    CHECK(kst_panel_is_open());
    close_kst();
    scr->leave();
    prog_registry_use(NULL, 0);
    size_t n = 0;
    (void)prog_registry(&n);
    CHECK_EQ(n, 2u);        /* the shipped table: KST and the stick menu */
}

int main(void)
{
    RUN(a_tile_opens_live_and_each_task_keeps_its_place);
    RUN(the_strip_reads_live_test_program_settings);
    RUN(a_row_the_iomcu_lacks_is_dimmed_and_opens_nothing);
    RUN(the_program_row_repaints_when_the_testers_port_moves);
    RUN(the_home_tag_climbs_from_the_session_to_the_list_and_leaves);
    RUN(program_offers_search_over_nine_rows_and_narrows_the_list);
    RUN(the_home_tag_closes_a_panel_climbs_from_a_runs_settings_and_leaves);
    RUN(a_tab_switch_drops_a_start_hold);
    RUN(the_target_row_locks_for_each_condition);
    RUN(the_target_row_opens_outputs_on_what_the_tester_drives);
    RUN(the_target_row_opens_on_the_drive_not_the_setting);
    RUN(the_run_cards_say_what_their_start_needs);
    RUN(the_supply_caps_show_read_only_with_their_home);
    RUN(the_as5600_lives_on_interfaces);
    RUN(the_knob_moves_the_horn_on_live_alone);
    return test_summary("servo_workbench");
}
