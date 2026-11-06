/*
 * MOVE SERVO on the Hitec programmer's overlay (shared/ui/hitec_panel_move.c):
 * the row on the D series TRAVEL tab, its page with the risks and the hold,
 * the band's hint of a short release, the refusal said where the gate
 * refuses, the -/+ keys of the positions with their repeat, the keypad
 * between them, the live lines that name the port the loop drives, and the
 * ways off: a tap on ON, a tab change, CLOSE and the home tag, a STOP.  The
 * overlay's bench is hitec_panel_rig.h, its servo follows the target.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel_rig.h"

#include <stdio.h>

#include "hitec_move.h"

/* The MOVE SERVO row and LEFT END's row on TRAVEL. */
#define MOVE_ROW 4
#define LEFT_ROW 1

/* The overlay open in @p lang, a D485HW whose horn follows its target
 * connected, TRAVEL shown. */
static void travel_at(supply_kind_t supply, ui_lang_t lang)
{
    open_at(supply, lang);
    servo_d485();
    g_drv.d_follow = true;
    g_drv.dreg[HITEC_D_REG_TARGET] = HITEC_LIVE_D_T_MID;
    sim_d_follow();
    tap_connect();
    tap(hitec_panel_tab_rect(HITEC_PANEL_TRAVEL));
}

static void travel(void)
{
    travel_at(SUPPLY_ON, UI_LANG_EN);
}

/* MOVE SERVO tapped and its hold done: the loop runs. */
static void move_on(void)
{
    tap(val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0));
    CHECK_EQ(hitec_panel_editor(), 3);
    hold(2.1f);
    for (unsigned i = 0; i < 20000u
                         && hitec_prog_move_phase(model()) != HITEC_MOVE_RUN;
         ++i) {
        pms();
    }
    CHECK_EQ(hitec_prog_move_phase(model()), HITEC_MOVE_RUN);
    show();
}

/* Run until the loop is off and the port held again. */
static void move_ended(void)
{
    for (unsigned i = 0; i < 60000u
                         && hitec_prog_move_phase(model()) != HITEC_MOVE_OFF;
         ++i) {
        pms();
    }
    until(HITEC_PROG_HELD);
    show();
}

/* The draft's value of a D position. */
static int32_t draft(uint8_t id)
{
    hitec_value_t v;

    CHECK_EQ(hitec_param_get(hitec_prog_draft(model()), HITEC_LAYOUT_D, id,
                             &v), HITEC_GET_OK);
    return v.value;
}

/* Whether a note line holds @p s. */
static bool note_has(const char *s)
{
    for (int i = 0; hitec_panel_note(i) != NULL; ++i) {
        if (strstr(hitec_panel_note(i), s) != NULL) {
            return true;
        }
    }
    return false;
}

TEST_CASE(the_move_row_sits_under_direction_and_reads_off)
{
    travel();
    CHECK_STR_EQ(hitec_panel_label(HITEC_PANEL_TRAVEL, MOVE_ROW, 0),
                 TR(HP_MOVE));
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_TRAVEL, MOVE_ROW, 0), TR(OFF));
    CHECK(hitec_panel_row_live(HITEC_PANEL_TRAVEL, MOVE_ROW, 0));
    CHECK(val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0).h >= UI_TAP_MIN);
    /* No keys while it is off. */
    CHECK_EQ(hitec_panel_step_rect(HITEC_PANEL_TRAVEL, LEFT_ROW, false).w, 0);
}

TEST_CASE(the_page_names_the_risks_the_stall_level_and_the_holds_consent)
{
    bool stall = false;
    char want[HITEC_PANEL_LINE];

    travel();
    tap(val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0));
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_STR_EQ(hitec_panel_page_line(0), TR(HP_MOVE));
    CHECK_STR_EQ(hitec_panel_apply_label(), TR(HP_MOVE_HOLD));
    CHECK_EQ(hitec_panel_page_hint(), TX_HOLD_HINT_MOVE);
    snprintf(want, sizeof(want), TR(HP_MOVE_5), 800u);
    for (int i = 1; hitec_panel_page_line(i) != NULL; ++i) {
        stall = stall || strcmp(hitec_panel_page_line(i), want) == 0;
    }
    CHECK(stall);
    CHECK_STR_EQ(hitec_panel_page_line(2), TR(HP_MOVE_2));
    CHECK_STR_EQ(hitec_panel_page_line(3), TR(HP_MOVE_3));
    CHECK_STR_EQ(hitec_panel_page_line(7), TR(KP_W_END_MOVED));
    CHECK_STR_EQ(hitec_panel_page_line(8), TR(KP_W_NEUTRAL_MOVED));
    /* CANCEL starts nothing. */
    tap(hitec_panel_cancel_rect());
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(hitec_prog_move_phase(model()), HITEC_MOVE_OFF);
}

TEST_CASE(a_release_before_2s_starts_nothing)
{
    unsigned moves;

    travel();
    moves = g_drv.d_moves;
    tap(val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0));
    hold(1.9f);
    prun(500u);
    CHECK_EQ(hitec_prog_move_phase(model()), HITEC_MOVE_OFF);
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_EQ(g_drv.d_moves, moves);
}

TEST_CASE(the_hold_switches_it_on_and_a_tap_on_on_switches_it_off)
{
    travel();
    move_on();
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_TRAVEL, MOVE_ROW, 0), TR(ON));
    CHECK_EQ(pstate(), HITEC_PROG_MOVING);
    tap(val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0));
    CHECK(!hitec_prog_move_on(model()));
    show();
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_TRAVEL, MOVE_ROW, 0),
                 TR(KP_WAIT));
    CHECK(!hitec_panel_row_live(HITEC_PANEL_TRAVEL, MOVE_ROW, 0));
    move_ended();
    CHECK_STR_EQ(said(), TR(HP_MOVE_DONE));
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_TRAVEL, MOVE_ROW, 0), TR(OFF));
}

TEST_CASE(a_refusal_is_said_and_opens_no_page)
{
    travel_at(SUPPLY_NONE, UI_LANG_EN);
    tap(val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0));
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_STR_EQ(said(), TR(HP_MOVE_NEED_PD));
    CHECK_EQ(hitec_prog_move_phase(model()), HITEC_MOVE_OFF);
}

TEST_CASE(minus_steps_left_end_by_8_counts_and_the_horn_follows_it)
{
    gfx_rect_t minus;

    travel();
    move_on();
    minus = hitec_panel_step_rect(HITEC_PANEL_TRAVEL, LEFT_ROW, false);
    CHECK_EQ(minus.w, 56);
    CHECK(minus.h >= UI_TAP_MIN);
    tap(minus);
    CHECK_EQ(draft(HITEC_P_D_LEFT_END), 3351 - (int32_t)HITEC_MOVE_STEP);
    CHECK_EQ(model()->mv.follow, HITEC_P_D_LEFT_END);
    /* No CHECK THIS CHANGE: the hold took the rule. */
    CHECK_EQ(hitec_panel_editor(), 0);
    for (unsigned i = 0; i < 60000u
                         && g_drv.dreg[HITEC_D_REG_POSITION] > 3360u;
         ++i) {
        pms();
    }
    CHECK(g_drv.dreg[HITEC_D_REG_POSITION] <= 3360u);
    tap(hitec_panel_step_rect(HITEC_PANEL_TRAVEL, LEFT_ROW, true));
    CHECK_EQ(draft(HITEC_P_D_LEFT_END), 3351);
}

TEST_CASE(a_held_key_repeats_after_045s_and_a_finger_off_it_ends_the_repeat)
{
    gfx_rect_t plus;
    int32_t at_press;

    travel();
    move_on();
    plus = hitec_panel_step_rect(HITEC_PANEL_TRAVEL, LEFT_ROW, true);
    ev(cx(plus), cy(plus), TOUCH_EVENT_DOWN);
    at_press = draft(HITEC_P_D_LEFT_END);
    CHECK_EQ(at_press, 3351 + (int32_t)HITEC_MOVE_STEP);
    /* 0.40s: no repeat yet. */
    for (int i = 0; i < 20; ++i) {
        hitec_panel_tick(0.02f);
        prun(20u);
    }
    CHECK_EQ(draft(HITEC_P_D_LEFT_END), at_press);
    /* 1.0s in all: the first repeats at 8Hz. */
    for (int i = 0; i < 30; ++i) {
        hitec_panel_tick(0.02f);
        prun(20u);
    }
    CHECK(draft(HITEC_P_D_LEFT_END) > at_press);
    CHECK(draft(HITEC_P_D_LEFT_END)
          <= at_press + 6 * (int32_t)HITEC_MOVE_STEP);
    /* The finger slides off the key. */
    ev(cx(plus) - 100, cy(plus), TOUCH_EVENT_MOVE);
    at_press = draft(HITEC_P_D_LEFT_END);
    for (int i = 0; i < 50; ++i) {
        hitec_panel_tick(0.02f);
        prun(20u);
    }
    CHECK_EQ(draft(HITEC_P_D_LEFT_END), at_press);
    ev(cx(plus) - 100, cy(plus), TOUCH_EVENT_UP);
}

TEST_CASE(a_step_the_order_refuses_says_why_and_keeps_the_draft)
{
    travel();
    move_on();
    /* RIGHT END down to NEUTRAL + 8, then a step below it. */
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_RIGHT_END, 8200),
             HITEC_PROG_EDIT_OK);
    tap(hitec_panel_step_rect(HITEC_PANEL_TRAVEL, 2, false));
    CHECK_EQ(draft(HITEC_P_D_RIGHT_END), 8200);
    CHECK_STR_EQ(said(), TR(HP_E_ORDER));
}

TEST_CASE(the_value_between_the_keys_opens_the_keypad)
{
    travel();
    move_on();
    tap(val(HITEC_PANEL_TRAVEL, LEFT_ROW, 0));
    CHECK_EQ(hitec_panel_editor(), 1);
    /* The loop runs on under the keypad. */
    prun(500u);
    CHECK_EQ(hitec_prog_move_phase(model()), HITEC_MOVE_RUN);
    CHECK_EQ(hitec_panel_editor(), 1);
}

TEST_CASE(the_live_lines_name_the_port_the_loop_drives_and_the_position)
{
    char port[48];

    travel();
    move_on();
    prun(300u);
    show();
    /* The port and its GPIO as the drive path holds them. */
    snprintf(port, sizeof(port), "GPIO %u", (unsigned)hitec_prog_pin(model()));
    CHECK_EQ(hitec_prog_pin(model()), PIN);
    CHECK_EQ(hitec_prog_port(model()), CH);
    CHECK_STR_EQ(hitec_panel_note(0), hitec_panel_port_line(0));
    CHECK(strstr(hitec_panel_note(0), port) != NULL);
    CHECK(strncmp(hitec_panel_note(1), "POSITION 8192 (+0.0", 19) == 0);
    CHECK(strstr(hitec_panel_note(1), "0.04A") != NULL);
    CHECK(note_has(TR(HP_LIVE_NOTE)));
    /* LEFT END followed: where 850us puts the horn, 77% at 1000us. */
    tap(hitec_panel_step_rect(HITEC_PANEL_TRAVEL, LEFT_ROW, false));
    CHECK(note_has("850us"));
    CHECK(note_has("1000us"));
    CHECK(note_has("77%"));
    /* Beyond the reach: the reach is said. */
    CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_LEFT_END, 2000),
             HITEC_PROG_EDIT_OK);
    show();
    CHECK(note_has("2606"));
    CHECK(note_has("13743"));
}

TEST_CASE(the_as5600_angle_shows_in_the_live_line)
{
    travel();
    g_angle_on = true;
    g_angle = 1024u;
    move_on();
    prun(200u);
    show();
    CHECK(strstr(hitec_panel_note(1), "AS5600 +90.0") != NULL);
    g_angle = 3072u;
    prun(200u);
    show();
    CHECK(strstr(hitec_panel_note(1), "AS5600 -90.0") != NULL);
}

/* The live line writes the PD mini's current in 10mA: a step of 10mA with
 * the horn standing still moves the page's revision and the line. */
TEST_CASE(the_current_in_the_live_line_repaints_with_the_horn_at_rest)
{
    travel();
    move_on();
    prun(300u);
    show();
    CHECK(strstr(hitec_panel_note(1), "0.04A") != NULL);
    const uint32_t rev = hitec_panel_revision();
    g_out_ma = 51u;
    prun(200u);
    CHECK(hitec_panel_revision() != rev);
    show();
    CHECK(strstr(hitec_panel_note(1), "0.05A") != NULL);
    g_out_ma = 41u;
}

TEST_CASE(a_tab_change_switches_it_off_and_the_end_runs)
{
    unsigned reboots;

    travel();
    reboots = g_drv.d_reboots;
    move_on();
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    CHECK(!hitec_prog_move_on(model()));
    show();
    CHECK_STR_EQ(hitec_panel_note(0), TR(HP_LIVE_ENDING));
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_SERVO, 0, 0), TR(KP_WAIT));
    move_ended();
    CHECK_EQ(g_drv.d_reboots, reboots + 1u);
    CHECK_STR_EQ(said(), TR(HP_MOVE_DONE));
}

TEST_CASE(close_switches_it_off_first_and_leaves_after_the_end)
{
    travel();
    move_on();
    tap(hitec_panel_close_rect());
    CHECK(!hitec_prog_move_on(model()));
    CHECK(hitec_panel_is_open());
    CHECK_EQ(hitec_panel_editor(), 0);
    move_ended();
    hitec_panel_back();
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_STR_EQ(hitec_panel_apply_label(), TR(KP_HOLD_LEAVE));
}

TEST_CASE(a_screen_change_switches_it_off)
{
    travel();
    move_on();
    hitec_panel_leave();
    CHECK(!hitec_prog_move_on(model()));
    move_ended();
}

TEST_CASE(a_stop_ends_it_and_the_keys_go)
{
    travel();
    move_on();
    press_stop();
    move_ended();
    CHECK_STR_EQ(hitec_panel_value(HITEC_PANEL_TRAVEL, MOVE_ROW, 0), TR(OFF));
    CHECK_EQ(hitec_panel_step_rect(HITEC_PANEL_TRAVEL, LEFT_ROW, true).w, 0);
    CHECK(model()->must_read);
    CHECK(!hitec_panel_row_live(HITEC_PANEL_TRAVEL, LEFT_ROW, 0));
    /* SERVO says what is owed, and no write is named. */
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    CHECK(note_has(TR(HP_LIVE_OWED)));
    CHECK(!note_has(TR(HP_MUST_READ)));
}

TEST_CASE(a_bind_is_said_in_both_languages)
{
    char want[HITEC_PANEL_LINE];

    for (int lang = 0; lang < 2; ++lang) {
        travel_at(SUPPLY_ON, lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        g_drv.d_block_lo = 6000u;
        move_on();
        CHECK_EQ(hitec_prog_move_edit(model(), HITEC_P_D_LEFT_END, 3351),
                 HITEC_PROG_EDIT_OK);
        move_ended();
        CHECK_EQ(msg(), HITEC_PROG_MSG_MOVE_BLOCKED);
        snprintf(want, sizeof(want), TR(HP_LIVE_BLOCKED),
                 (unsigned)hitec_prog_message(model())->arg,
                 (unsigned)hitec_prog_message(model())->reg);
        CHECK_STR_EQ(said(), want);
    }
}

int main(void)
{
    RUN(the_move_row_sits_under_direction_and_reads_off);
    RUN(the_page_names_the_risks_the_stall_level_and_the_holds_consent);
    RUN(a_release_before_2s_starts_nothing);
    RUN(the_hold_switches_it_on_and_a_tap_on_on_switches_it_off);
    RUN(a_refusal_is_said_and_opens_no_page);
    RUN(minus_steps_left_end_by_8_counts_and_the_horn_follows_it);
    RUN(a_held_key_repeats_after_045s_and_a_finger_off_it_ends_the_repeat);
    RUN(a_step_the_order_refuses_says_why_and_keeps_the_draft);
    RUN(the_value_between_the_keys_opens_the_keypad);
    RUN(the_live_lines_name_the_port_the_loop_drives_and_the_position);
    RUN(the_as5600_angle_shows_in_the_live_line);
    RUN(the_current_in_the_live_line_repaints_with_the_horn_at_rest);
    RUN(a_tab_change_switches_it_off_and_the_end_runs);
    RUN(close_switches_it_off_first_and_leaves_after_the_end);
    RUN(a_screen_change_switches_it_off);
    RUN(a_stop_ends_it_and_the_keys_go);
    RUN(a_bind_is_said_in_both_languages);
    return test_summary("hitec_panel_move");
}
