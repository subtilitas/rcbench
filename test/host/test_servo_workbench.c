/*
 * SERVO's function screens as a workbench: what each shows, the home
 * tag's levels, the run cards' lines, the caps button and the knob on each
 * screen.  The hub: test_servo_hub.c; the PORT button:
 * test_servo_port_button.c.
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
#include "shot_facts_rig.h"

#include "esc_wire_panel.h"
#include "kst_panel.h"
#include "link_pages.h"
#include "outputs_screen.h"
#include "prog5v_box.h"
#include "prog_choice.h"
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
#include "ui_board.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/* ------------------------------------------------- the function screens */

/*
 * Each function's screen shows its task: LIVE and SWEEP the dial, TEST and
 * LIMIT RUN their card, RUDDER and RUDDER AUTO the rudder's card, KST and
 * HITEC the programmer's session.  A leave closes a run's settings; the
 * screen entered again is the same function's.
 */
TEST_CASE(each_function_screen_shows_its_task)
{
    static const struct { servo_fn_t fn; ui_wb_task_t task; } k[] = {
        { SERVO_FN_LIVE, UI_WB_LIVE },     { SERVO_FN_SWEEP, UI_WB_LIVE },
        { SERVO_FN_TEST, UI_WB_TEST },     { SERVO_FN_LIMRUN, UI_WB_TEST },
        { SERVO_FN_RUDDER, UI_WB_RUDDER }, { SERVO_FN_RAUTO, UI_WB_RUDDER },
        { SERVO_FN_KST, UI_WB_PROGRAM },   { SERVO_FN_HITEC, UI_WB_PROGRAM },
    };
    fresh();
    scr->enter();
    CHECK_EQ(servo_screen_fn(), SERVO_FN_LIVE);
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        go(k[i].fn);
        CHECK_EQ(servo_screen_fn(), k[i].fn);
        CHECK_EQ(servo_screen_task(), (int)k[i].task);
        CHECK_EQ(servo_fn_of_screen(servo_fn_screen_id(k[i].fn)), k[i].fn);
        CHECK(servo_screen_owns(servo_fn_screen_id(k[i].fn)));
        scr->render(&cv, 0);
    }
    CHECK(servo_screen_owns(SCREEN_SERVO));
    CHECK(!servo_screen_owns(SCREEN_OUTPUTS));

    open_limrun_list();
    CHECK_EQ(servo_screen_fn(), SERVO_FN_LIMRUN);
    CHECK(servo_screen_list()->open);
    scr->leave();
    scr->enter();
    CHECK_EQ(servo_screen_fn(), SERVO_FN_LIMRUN);
    CHECK(!servo_screen_list()->open);

    scr->reset();
    CHECK_EQ(servo_screen_fn(), SERVO_FN_LIVE);
}

/* Over a panel the home tag closes the panel and the page under it stays;
 * from TEST's list and from the limit run's settings it climbs to the
 * run's card; on a function screen with no panel it lets the screen go,
 * which switches to SAFE. */
TEST_CASE(the_home_tag_closes_a_panel_climbs_from_a_runs_settings_and_leaves)
{
    fresh();
    scr->enter();
    CHECK(!home_tag());                                 /* LIVE */
    open_test();
    CHECK(!home_tag());
    CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);

    open_run_settings(0);
    CHECK(servo_screen_list()->open);
    CHECK(home_tag());
    CHECK(!servo_screen_list()->open);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);
    CHECK(!home_tag());
    /* LIMIT RUN's list: the tag closes it, the next lets the screen go. */
    open_limrun_list();
    CHECK(servo_screen_list()->open);
    CHECK(home_tag());
    CHECK(!servo_screen_list()->open);
    CHECK(!home_tag());

    /* IDLE CURRENT's keypad over TEST's list: the tag closes the keypad
     * without a value, and the list stays; the next tag climbs to TEST. */
    const float idle = settings_get(SET_SERVO_IDLE_MAX);
    open_run_settings(0);
    sl_part(TL_IDLE, UI_SL_PART_VALUE);
    CHECK(servo_screen_list()->kp.open);
    CHECK(home_tag());
    CHECK(!servo_screen_list()->kp.open);
    CHECK(servo_screen_list()->open);
    CHECK_NEAR(settings_get(SET_SERVO_IDLE_MAX), idle, 1e-6f);
    CHECK(home_tag());
    CHECK(!servo_screen_list()->open);
    CHECK(!home_tag());

    /* TYPE's list over the hub's list: one tag closes it, nothing is
     * chosen, and the list stays; the next closes the list; the next is
     * the hub's own, which goes to the menu. */
    open_settings();
    sl_tap(HL_TYPE);
    CHECK(home_tag());
    CHECK(servo_screen_list()->open);
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    CHECK(home_tag());
    CHECK(!servo_screen_list()->open);
    CHECK(!home_tag());
    close_settings();
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    CHECK(!home_tag());

    /* A set point's keypad on an armed bench: closed, the bench stays
     * ARMED; the tag after it leaves, and leaving switches to SAFE. */
    fresh_armed();
    go(SERVO_FN_KST);                          /* its SET row */
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

/* A leave under START TEST's hold drops it, and the hold does not go on
 * when TEST comes back. */
TEST_CASE(a_leave_drops_a_start_hold)
{
    bench_fresh();
    open_test();
    ev(START_X, START_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 50; ++i) {
        scr->tick(0.02f);                               /* 1s of the 2s */
    }
    scr->leave();
    go(SERVO_FN_LIVE);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    scr->enter();
    open_test();
    for (int i = 0; i < 100; ++i) {
        scr->tick(0.02f);
    }
    ev(START_X, START_Y, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_testing());
    servo_screen_set_armed(false);             /* the leave disarmed */
    bench_frames(100u);
    servo_screen_set_armed(true);
    bench_frames(100u);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
}

/* ----------------------------------------------------------- the run cards */

/* TEST's state line says HOLD 2s TO START on a SAFE bench, whose ARM its
 * hold does, and on an armed one.  An HV step makes ARM + START a tap that
 * opens the warning and the line says so. */
TEST_CASE(the_run_cards_say_what_their_start_needs)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        bench_fresh();
        servo_screen_set_armed(false);
        bench_frames(100u);
        open_test();
        CHECK(card_line_reads(0, TR(SV_HOLD_START), UI_C_TEXT_FAINT));
        servo_screen_set_armed(true);
        bench_frames(100u);
        CHECK(card_line_reads(0, TR(SV_HOLD_START), UI_C_TEXT_FAINT));
        settings_set(SET_SERVO_STEP_84, 1.0f);
        test_hv_on();                                    /* HV SERVO on */
        CHECK(card_line_reads(0, TR(SV_TAP_HV), UI_C_TEXT_FAINT));
    }
    ui_text_set_language(UI_LANG_EN);
}

/* On the rbmod carrier with a programming port chosen the AUTOMATIC TEST
 * card says the run cannot take it, with its PROG 5V off or on: the
 * voltage steps reach the servo rails and not the port. */
TEST_CASE(the_test_card_on_a_carrier_port_names_the_steps)
{
    bench_fresh();
    servo_screen_set_armed(true);
    ui_board_set(OUTBIND_BOARD_RBMOD);
    settings_set(SET_SERVO_TARGET, 2.0f);
    prog5v_box_publish(2u, 21u, PROG5V_HELD_NONE);
    bench_frames(100u);
    open_test();
    CHECK(card_line_reads(0, ui_servo_str(SERVO_STR_START_PROG_STEPS),
                          UI_C_WARN));
    prog5v_box_publish(3u, 21u, PROG5V_HELD_NONE);
    bench_frames(100u);
    CHECK(card_line_reads(0, ui_servo_str(SERVO_STR_START_PROG_STEPS),
                          UI_C_WARN));
    prog5v_box_reset();
    settings_set(SET_SERVO_TARGET, 0.0f);
    ui_board_set(OUTBIND_BOARD_PICO_HEADER);
}

/* servo_limit_panel.c's write note, declared in servo_screen_int.h, which
 * this suite does not take in whole. */
const char *lim_write_why(void);

/* On the carrier's programming port the LIMIT RUN's write note names the
 * PROG 5V box, never a PD mini: the box off asks it on, the box on leaves
 * nothing to say.  Off the carrier the rig's PD mini answers, and the note
 * is empty there too; the port without one keeps SV_LIM_PROG_NO_PD's line
 * (test_kst_panel_limrun). */
TEST_CASE(the_limit_runs_write_note_on_a_carrier_port_names_the_prog_5v)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        bench_fresh();
        ui_board_set(OUTBIND_BOARD_RBMOD);
        prog_choice_set_far_minor(LINK_MINOR_CARRIER_PWM);
        settings_set(SET_SERVO_TARGET, 3.0f);
        servo_screen_set_drive(3u, 15u, true);   /* PROG PORT 3, GPIO 15 */
        prog5v_box_publish(2u, 21u, PROG5V_HELD_NONE);
        bench_frames(100u);
        CHECK(lim_write_why() != NULL);
        CHECK_STR_EQ(lim_write_why(), ui_servo_str(SERVO_STR_START_PROG_5V));
        prog5v_box_publish(3u, 21u, PROG5V_HELD_NONE);
        bench_frames(100u);
        CHECK(lim_write_why() == NULL);
        prog5v_box_reset();
        prog_choice_reset();
        ui_board_set(OUTBIND_BOARD_PICO_HEADER);
        bench_frames(100u);
        CHECK(lim_write_why() == NULL);
        settings_set(SET_SERVO_TARGET, 0.0f);
        servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* servo_limit_panel.c's start gate and its pulse line, declared in
 * servo_screen_int.h. */
const char *lim_blocked(void);
const char *lim_pwm_why(void);

/* On the carrier's programming port the LIMIT RUN asks for the PROG 5V
 * before ARM: the port's pulse comes on only with it and only while SAFE.
 * The pulse line names the PROG 5V while PORT PWM ON waits for it, PORT
 * PWM ON waiting for SAFE while the panel asks for the pulse, PORT PWM OFF
 * where the setting is off. */
TEST_CASE(the_limit_run_on_a_carrier_port_asks_the_prog_5v_before_arm)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        bench_fresh();
        servo_screen_set_armed(false);
        ui_board_set(OUTBIND_BOARD_RBMOD);
        prog_choice_set_far_minor(LINK_MINOR_CARRIER_PWM);
        settings_set(SET_SERVO_TARGET, 1.0f);
        settings_set(SET_SERVO_PORT_PWM, 1.0f);
        servo_screen_set_drive(1u, 13u, false);
        prog5v_box_publish(2u, 21u, PROG5V_HELD_NONE);
        bench_frames(100u);
        CHECK_STR_EQ(lim_blocked(), ui_servo_str(SERVO_STR_START_PROG_5V));
        CHECK_STR_EQ(lim_pwm_why(), ui_servo_str(SERVO_STR_START_PROG_5V));
        prog5v_box_publish(3u, 21u, PROG5V_HELD_NONE);
        bench_frames(100u);
        CHECK_STR_EQ(lim_blocked(),
                     ui_servo_str(SERVO_STR_START_NOT_ARMED));
        CHECK_STR_EQ(lim_pwm_why(), TR(SV_LIM_PWM_SAFE));
        settings_set(SET_SERVO_PORT_PWM, 0.0f);
        CHECK_STR_EQ(lim_pwm_why(), TR(SV_LIM_PWM_OFF));
        settings_set(SET_SERVO_PORT_PWM, 1.0f);
        prog5v_box_reset();
        prog_choice_reset();
        ui_board_set(OUTBIND_BOARD_PICO_HEADER);
        settings_set(SET_SERVO_TARGET, 0.0f);
        servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
        /* Off the carrier the PROG 5V asks for nothing: ARM FIRST. */
        bench_frames(100u);
        CHECK_STR_EQ(lim_blocked(),
                     ui_servo_str(SERVO_STR_START_NOT_ARMED));
    }
    ui_text_set_language(UI_LANG_EN);
}

/* PORT PWM off: the line says what to do, switch it on in OUTPUTS while
 * SAFE, and DISARM first while ARMED, where OUTPUTS takes no change of
 * it.  The limit run's gate names it on an ARMED bench with the port's
 * pulse off, in EN and DE. */
TEST_CASE(port_pwm_off_says_how_to_switch_it_on_safe_and_armed)
{
    static const char *const k_safe[UI_LANG_COUNT] = {
        "PORT PWM OFF: SWITCH IT ON IN OUTPUTS",
        "PORT-PWM AUS: SCHALTE ES IN OUTPUTS EIN",
    };
    static const char *const k_armed[UI_LANG_COUNT] = {
        "PORT PWM OFF: DISARM, SWITCH IT ON IN OUTPUTS",
        "PORT-PWM AUS: DISARM, SCHALTE ES IN OUTPUTS EIN",
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        bench_fresh();
        servo_screen_set_armed(false);
        settings_set(SET_SERVO_TARGET, 1.0f);
        settings_set(SET_SERVO_PORT_PWM, 0.0f);
        servo_screen_set_drive(1u, 13u, false);
        bench_frames(100u);
        CHECK_STR_EQ(lim_pwm_why(), k_safe[lang]);
        CHECK(!servo_screen_tester_target_locked());
        servo_screen_set_armed(true);
        bench_frames(100u);
        CHECK_STR_EQ(lim_pwm_why(), k_armed[lang]);
        CHECK(servo_screen_tester_target_locked());
        /* The motor's ARM locks TARGET as well. */
        servo_screen_set_armed(false);
        servo_screen_set_bench_armed(true);
        bench_frames(100u);
        CHECK_STR_EQ(lim_pwm_why(), k_armed[lang]);
        servo_screen_set_bench_armed(false);
        settings_set(SET_SERVO_TARGET, 0.0f);
        settings_set(SET_SERVO_PORT_PWM, 1.0f);
        servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
        bench_frames(100u);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* ------------------------------------------------------- the read-only caps */

/** @brief The caps button's top on TEST, under the card. */
#define CAPS_TOP (SV_CAPS_TEST_Y - UI_TAP_MIN / 2)

/* Whether the caps button with its top at (@p x, @p y) reads SUPPLY's caps in
 * force. */
static bool caps_read(int x, int y)
{
    const supply_caps_t caps = supply_screen_caps();
    char line[64];
    snprintf(line, sizeof(line), TR(SV_CAPS), (double)caps.v_max,
             (double)caps.i_max);
    scr->render(&cv, 0);
    want_fresh();
    const gfx_rect_t r = gfx_rect_make(x, y, 600, UI_TAP_MIN);
    const gfx_color_t bg = fb[(size_t)(y - 2) * W + (size_t)(x + 8)];
    gfx_fill_rect(&want_cv, r.x - 2, r.y - 1, r.w + 4, r.h + 2, bg);
    ui_button_font(&want_cv, r, line, ui_theme_color(UI_C_PANEL_HI), false,
                   true, &gfx_font_10x20);
    return same_in(fb, want_fb, r.x, r.y, r.w, r.h);
}

/* The caps button under TEST's card reads SUPPLY's caps in force and follows them when they change, in both languages.  What
 * a tap on it does: test_menu_openers. */
TEST_CASE(the_supply_caps_show_on_their_button)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        fresh();
        scr->enter();
        open_test();
        CHECK(caps_read(UI_MARGIN + 10, CAPS_TOP));
        supply_caps_t caps = supply_screen_caps();
        CHECK_NEAR(caps.v_max, 21.0f, 1e-4f);
        settings_set(SET_SUPPLY_V_MAX, 12.0f);
        settings_set(SET_SUPPLY_I_MAX, 3.0f);
        supply_screen_settings_loaded();
        servo_invalidate();
        caps = supply_screen_caps();
        CHECK_NEAR(caps.v_max, 12.0f, 1e-4f);
        CHECK(caps_read(UI_MARGIN + 10, CAPS_TOP));
    }
    ui_text_set_language(UI_LANG_EN);
}

/* AS5600 has one home, SETUP > SENSORS; RUDDER's list keeps ENC CENTRE,
 * the action on the servo. */
TEST_CASE(the_as5600_lives_on_sensors)
{
    const setting_def_t *d = settings_def(SET_ENC_EN);
    CHECK(d != NULL);
    CHECK(d != NULL && d->cat == SET_CAT_SENSORS);
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
 * the hub's list, under a panel or the programmer; a tap on LIVE in the frame
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
    sl_tap(HL_TRAVEL);                              /* TRAVEL's keypad */
    CHECK(!knob_moves());
    key(UI_KEY_CANCEL);
    close_settings();
    servo_screen_set_armed(true);       /* the run's page asked for SAFE */
    CHECK(knob_moves());

    /* The turn of the frame in which LIVE came back. */
    open_test();
    servo_screen_knob_frame();
    close_settings();
    servo_screen_knob(0.05f);
    CHECK(last_cmd().kind != SERVO_CMD_POSITION);
    CHECK(knob_moves());

    /* KST's screen and its session. */
    open_kst();
    CHECK(kst_panel_is_open());
    CHECK(scr->covered());
    CHECK(!shot_facts_now().no_session);
    CHECK(!knob_moves());
    close_kst();
    CHECK(!kst_panel_is_open());
    CHECK(!knob_moves());
    close_settings();
    CHECK(knob_moves());
}

/* ------------------------------------------------------- KST and HITEC */

/* KST's screen is the session: the home tag closes it, and the router
 * then takes the screen to the hub (test_servo_hub.c).  A leave closes it;
 * the screen entered again opens it again. */
TEST_CASE(the_home_tag_closes_the_session_and_leaves)
{
    fresh();
    scr->enter();
    open_kst();
    CHECK(kst_panel_is_open());
    CHECK(!home_tag());
    CHECK(!kst_panel_is_open());
    open_kst();
    CHECK(kst_panel_is_open());
    scr->leave();
    CHECK(!kst_panel_is_open());
    scr->enter();
    CHECK_EQ(servo_screen_fn(), SERVO_FN_KST);
    CHECK(kst_panel_is_open());
    close_kst();
}

/* ------------------------------------------------- finger-sized controls */

/* Whether the keypad is open over the left column, closed again by the
 * home tag. */
static bool keypad_opened(void)
{
    const bool open = servo_screen_keypad()->open;
    if (open) {
        CHECK(home_tag());
    }
    return open;
}

/*
 * The rows a finger taps are UI_TAP_MIN tall: the SET row's values on KST's
 * output card take a press 1px inside each edge.  The lists' rows are ui_setlist.h's.
 */
TEST_CASE(a_set_value_is_a_finger_tall)
{
    fresh();
    go(SERVO_FN_KST);                          /* its SET row */
    tap(SUP_V_X, SUP_ROW_Y - 23);              /* SET's voltage */
    CHECK(keypad_opened());
    tap(SUP_V_X, SUP_ROW_Y + 22);
    CHECK(keypad_opened());
}

/*
 * Two taps where a run card's SETTINGS sat on TEST land on no row: TEST's
 * settings are the footer's list.  No setting changes, no keypad opens.
 */
TEST_CASE(a_second_tap_on_a_run_cards_settings_changes_no_setting)
{
    static float before[SETTING_COUNT];
    fresh();
    go(SERVO_FN_TEST);
    for (int id = 0; id < SETTING_COUNT; ++id) {
        before[id] = settings_get((setting_id_t)id);
    }
    tap(SV_RUN_SET_X, SV_RUN_SET_Y);
    tap(SV_RUN_SET_X, SV_RUN_SET_Y);
    int changed = 0;
    for (int id = 0; id < SETTING_COUNT; ++id) {
        changed += settings_get((setting_id_t)id) != before[id];
    }
    CHECK_EQ(changed, 0);
    CHECK(!servo_screen_keypad()->open);
    CHECK(!servo_screen_list()->open);
    CHECK_EQ(servo_screen_run_settings(), 0);
}

/* The rows of the body a stop at column @p x covers around row @p y:
 * the height of the control safe_at() names there, in pixels. */
static int stop_height(int x, int y)
{
    int top = y, bottom = y;
    while (top > 0 && scr->safe_at(x, top - 1)) {
        --top;
    }
    while (bottom < UI_BODY_H - 1 && scr->safe_at(x, bottom + 1)) {
        ++bottom;
    }
    return bottom - top + 1;
}

/* The points of the body under the alert band the screen names as a stop,
 * every 2px. */
static int stops_under_the_alert_band(void)
{
    int n = 0;
    for (int y = UI_ALERT_Y - UI_BAND_H; y < UI_BODY_H; y += 2) {
        for (int x = 0; x < UI_PANEL_W; x += 2) {
            n += scr->safe_at(x, y) ? 1 : 0;
        }
    }
    return n;
}

/* The left card's last 2 rows over its floor at body y 436: no text of
 * the card reaches them.  The stall note's 20px line ends at y 436, its
 * lowest ink at y 433. */
#define TEXT_FLOOR (436 - 2)

/*
 * Buffer 0 drawn whole: the pixels of the left card from TEXT_FLOOR down to
 * its bottom edge that are not the page's fill.
 */
static int ink_under_the_alert_band(void)
{
    servo_invalidate();
    scr->render(&cv, 0);
    const gfx_color_t fill = ui_theme_color(UI_C_PANEL);
    int n = 0;
    for (int y = TEXT_FLOOR; y < 436 - 1; ++y) {
        for (int x = UI_MARGIN + 1; x < UI_MARGIN + 620 - 1; ++x) {
            n += fb[(size_t)y * W + x] != fill;
        }
    }
    return n;
}

/*
 * In both languages the text at the foot of the left card ends above the
 * card's floor, so above the footer: TEST's card, its caps line and the
 * rail's plot, the LIMIT RUN's settings, the OUTPUT page's notes at their
 * longest, a refused or unsupported rate, and the save line at its
 * longest, SETUP's changes not saved.
 */
/*
 * LIVE's three readings end above the alert band in both languages, and the
 * band's own rows draw on the card's foot: "Hold 2s to switch to ARMED." leaves
 * COMMANDED, MEASURED and CURRENT whole.
 */
TEST_CASE(lives_readings_end_above_the_alert_band)
{
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        bench_fresh();
        CHECK_EQ(ink_under_the_alert_band(), 0);
        /* The numerals are there: ink in their row, 4px over the floor. */
        int ink = 0;
        const gfx_color_t fill = ui_theme_color(UI_C_PANEL);
        for (int y = TEXT_FLOOR - 44; y < TEXT_FLOOR; ++y) {
            for (int x = UI_MARGIN + 1; x < UI_MARGIN + 620 - 1; ++x) {
                ink += fb[(size_t)y * W + x] != fill;
            }
        }
        CHECK(ink > 100);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(no_text_lies_under_the_alert_band)
{
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        bench_fresh();
        settings_set(SET_SERVO_DWELL_MS, 210.0f);   /* not saved */
        CHECK(settings_dirty());
        open_test();
        CHECK_EQ(ink_under_the_alert_band(), 0);
        open_limrun();
        CHECK_EQ(ink_under_the_alert_band(), 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * While a run is under way its stops -- START TEST's place on TEST and STOP
 * TEST in the box under the card -- are UI_TAP_MIN tall or taller and end above the
 * alert band, as DISARM and OUTPUT OFF do: on TEST, on LIVE, and on the
 * run's settings.
 */
TEST_CASE(a_runs_stops_are_a_finger_tall_and_clear_of_the_alert_band)
{
    bench_fresh();
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    CHECK(scr->safe_at(START_X, START_Y));
    CHECK(stop_height(START_X, START_Y) >= UI_TAP_MIN);
    CHECK_EQ(stops_under_the_alert_band(), 0);
    /* The result box under the card: STOP TEST. */
    CHECK(scr->safe_at(BOXBTN_X, BOXBTN_Y));
    CHECK(stop_height(BOXBTN_X, BOXBTN_Y) >= UI_TAP_MIN);
    CHECK_EQ(stops_under_the_alert_band(), 0);
    open_run_settings(0);
    CHECK_EQ(stops_under_the_alert_band(), 0);
    servo_screen_set_armed(false);
    CHECK(!servo_screen_testing());
}

/* ARM with a signal-wire session holding a pin: no ask, and the alert names it. */
TEST_CASE(arm_under_a_wire_session_says_the_esc_programmer_holds_it)
{
    esc_prog_feed_t f;
    servo_cmd_t c;

    fresh();
    memset(&f, 0, sizeof(f));
    f.rd.known = true;
    f.rd.held = true;
    f.rd.state = LINK_ESC_READY;
    esc_wire_panel_feed(&f);
    ui_router_set_alert(NULL);
    arm_press();
    hold_for(2.5f);
    CHECK_STR_EQ(ui_router_alert(), TR(ALERT_ARM_ESC));
    arm_release();
    bool asked = false;

    while (servo_screen_take(&c)) {
        asked = asked || c.kind == SERVO_CMD_ARM;
    }
    CHECK(!asked);
    f.rd.held = false;
    f.rd.state = 0u;
    esc_wire_panel_feed(&f);
    ui_router_set_alert(NULL);
}

int main(void)
{
    RUN(a_set_value_is_a_finger_tall);
    RUN(a_runs_stops_are_a_finger_tall_and_clear_of_the_alert_band);
    RUN(a_second_tap_on_a_run_cards_settings_changes_no_setting);
    RUN(lives_readings_end_above_the_alert_band);
    RUN(no_text_lies_under_the_alert_band);
    RUN(each_function_screen_shows_its_task);
    RUN(the_home_tag_closes_the_session_and_leaves);
    RUN(the_home_tag_closes_a_panel_climbs_from_a_runs_settings_and_leaves);
    RUN(a_leave_drops_a_start_hold);
    RUN(the_run_cards_say_what_their_start_needs);
    RUN(the_test_card_on_a_carrier_port_names_the_steps);
    RUN(the_limit_runs_write_note_on_a_carrier_port_names_the_prog_5v);
    RUN(the_limit_run_on_a_carrier_port_asks_the_prog_5v_before_arm);
    RUN(port_pwm_off_says_how_to_switch_it_on_safe_and_armed);
    RUN(the_supply_caps_show_on_their_button);
    RUN(the_as5600_lives_on_sensors);
    RUN(the_knob_moves_the_horn_on_live_alone);
    RUN(arm_under_a_wire_session_says_the_esc_programmer_holds_it);
    return test_summary("servo_workbench");
}
