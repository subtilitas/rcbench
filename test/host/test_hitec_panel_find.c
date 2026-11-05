/*
 * FIND STOP on the Hitec programmer's overlay (shared/ui/hitec_panel_move.c):
 * the row in DIRECTION's place while MOVE SERVO's loop runs, its two keys
 * of 48px and more, a tap that starts the search on one side and a second
 * that ends it, the other side's key taking no tap meanwhile, the note
 * while it runs, the line of each way it ends in both languages, and a tab
 * change during the search.  The overlay's bench is hitec_panel_rig.h, its
 * servo follows the target and stops at d_block_lo.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel_rig.h"

#include <stdio.h>

#include "hitec_live.h"
#include "hitec_move.h"

/* The rows on TRAVEL: LEFT END, DIRECTION or FIND STOP, MOVE SERVO. */
#define LEFT_ROW 1
#define FIND_ROW 3
#define MOVE_ROW 4

/* The block of station step S-L6: position 4840. */
#define BLOCK_L 4840u

/* The overlay open in @p lang, a D485HW whose horn follows its target
 * connected, TRAVEL shown. */
static void travel_at(ui_lang_t lang)
{
    open_at(SUPPLY_ON, lang);
    servo_d485();
    g_drv.d_follow = true;
    g_drv.dreg[HITEC_D_REG_TARGET] = HITEC_LIVE_D_T_MID;
    sim_d_follow();
    tap_connect();
    tap(hitec_panel_tab_rect(HITEC_PANEL_TRAVEL));
}

/* MOVE SERVO tapped and its hold done: the loop's passes run. */
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

/* Run until message @p id is said; @p ms is the bound. */
static void until_msg(unsigned id, unsigned ms)
{
    for (unsigned i = 0; i < ms && msg() != id; ++i) {
        pms();
    }
    CHECK_EQ(msg(), id);
    show();
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

/* The first line of message @p id with @p arg, in the language in force. */
static const char *line_of(unsigned id, unsigned arg, unsigned reg)
{
    static char out[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];
    hitec_prog_msg_t m;

    memset(&m, 0, sizeof(m));
    m.id = (uint8_t)id;
    m.arg = (uint16_t)arg;
    m.reg = (uint16_t)reg;
    CHECK(hitec_panel_message_lines(&m, out) >= 1);
    return out[0];
}

TEST_CASE(the_find_row_takes_directions_place_while_move_servo_runs)
{
    gfx_rect_t l;
    gfx_rect_t r;
    gfx_rect_t v;

    travel_at(UI_LANG_EN);
    CHECK_STR_EQ(hitec_panel_label(HITEC_PANEL_TRAVEL, FIND_ROW, 0),
                 TR(HP_DIRECTION));
    CHECK_EQ(hitec_panel_find_rect(false).w, 0);
    move_on();
    CHECK_STR_EQ(hitec_panel_label(HITEC_PANEL_TRAVEL, FIND_ROW, 0),
                 TR(HP_FIND));
    CHECK(hitec_panel_row_live(HITEC_PANEL_TRAVEL, FIND_ROW, 0));
    l = hitec_panel_find_rect(false);
    r = hitec_panel_find_rect(true);
    v = val(HITEC_PANEL_TRAVEL, FIND_ROW, 0);
    CHECK(l.w >= UI_TAP_MIN && l.h >= UI_TAP_MIN);
    CHECK(r.w >= UI_TAP_MIN && r.h >= UI_TAP_MIN);
    CHECK(l.x >= v.x && r.x + r.w <= v.x + v.w);
    CHECK(l.x + l.w < r.x);
    /* MOVE OFF: DIRECTION is back once the end ran. */
    tap(val(HITEC_PANEL_TRAVEL, MOVE_ROW, 0));
    for (unsigned i = 0; i < 60000u
                         && hitec_prog_move_phase(model()) != HITEC_MOVE_OFF;
         ++i) {
        pms();
    }
    show();
    CHECK_STR_EQ(hitec_panel_label(HITEC_PANEL_TRAVEL, FIND_ROW, 0),
                 TR(HP_DIRECTION));
}

TEST_CASE(a_tap_starts_the_search_and_a_second_tap_ends_it)
{
    char want[HITEC_PANEL_LINE];

    travel_at(UI_LANG_EN);
    g_drv.d_block_lo = BLOCK_L;
    move_on();
    tap(hitec_panel_find_rect(false));
    CHECK_EQ(hitec_prog_find_side(model()), (int)HITEC_FIND_LEFT);
    prun(500u);
    show();
    snprintf(want, sizeof(want), TR(HP_FIND_RUN), TR(HP_END_L),
             (unsigned)HITEC_LIVE_FIND_STEP, (unsigned)HITEC_LIVE_FIND_PASSES,
             (unsigned)(HITEC_LIVE_FIND_MS / 1000u));
    CHECK(note_has(want));
    CHECK(!note_has(TR(HP_LIVE_NOTE)));
    /* The other side's key takes no tap while one side searches. */
    tap(hitec_panel_find_rect(true));
    CHECK_EQ(hitec_prog_find_side(model()), (int)HITEC_FIND_LEFT);
    /* The step keys are gone while it searches. */
    CHECK_EQ(hitec_panel_step_rect(HITEC_PANEL_TRAVEL, LEFT_ROW, false).w, 0);
    tap(hitec_panel_find_rect(false));
    until_msg(HITEC_PROG_MSG_FIND_HALT, 2000u);
    CHECK_STR_EQ(said(), TR(HP_FIND_HALT));
    CHECK_EQ(hitec_prog_find_side(model()), -1);
    CHECK(hitec_prog_move_on(model()));
}

TEST_CASE(a_found_stop_is_said_with_its_end_in_both_languages)
{
    char want[HITEC_PANEL_LINE];

    for (int lang = 0; lang < 2; ++lang) {
        travel_at(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        g_drv.d_block_lo = BLOCK_L;
        move_on();
        tap(hitec_panel_find_rect(false));
        until_msg(HITEC_PROG_MSG_FIND_FOUND_L, 60000u);
        snprintf(want, sizeof(want), TR(HP_FIND_FOUND),
                 (unsigned)hitec_prog_message(model())->arg, TR(HP_END_L),
                 (unsigned)hitec_prog_message(model())->reg);
        CHECK_STR_EQ(said(), want);
        /* LEFT END shows the edit with its `*`. */
        CHECK(hitec_panel_value(HITEC_PANEL_TRAVEL, LEFT_ROW, 0)[0] == '*');
    }
}

TEST_CASE(each_way_the_search_ends_has_its_line_in_both_languages)
{
    char want[HITEC_PANEL_LINE];

    for (int lang = 0; lang < 2; ++lang) {
        travel_at(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        snprintf(want, sizeof(want), TR(HP_FIND_FOUND), 11540u, TR(HP_END_R),
                 11465u);
        CHECK_STR_EQ(line_of(HITEC_PROG_MSG_FIND_FOUND_R, 11540u, 11465u),
                     want);
        snprintf(want, sizeof(want), TR(HP_FIND_FREE), 2606u);
        CHECK_STR_EQ(line_of(HITEC_PROG_MSG_FIND_FREE, 2606u, 0u), want);
        snprintf(want, sizeof(want), TR(HP_FIND_TIME), 40u, 3100u);
        CHECK_STR_EQ(line_of(HITEC_PROG_MSG_FIND_TIME, 3100u, 0u), want);
        snprintf(want, sizeof(want), TR(HP_FIND_NEAR), 8152u);
        CHECK_STR_EQ(line_of(HITEC_PROG_MSG_FIND_NEAR, 8152u, 0u), want);
        CHECK_STR_EQ(line_of(HITEC_PROG_MSG_FIND_HALT, 0u, 0u),
                     TR(HP_FIND_HALT));
    }
}

TEST_CASE(a_tab_change_during_the_search_switches_move_servo_off)
{
    unsigned reboots;

    travel_at(UI_LANG_EN);
    g_drv.d_block_lo = BLOCK_L;
    reboots = g_drv.d_reboots;
    move_on();
    tap(hitec_panel_find_rect(false));
    prun(2000u);
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    CHECK(!hitec_prog_move_on(model()));
    for (unsigned i = 0; i < 60000u
                         && hitec_prog_move_phase(model()) != HITEC_MOVE_OFF;
         ++i) {
        pms();
    }
    until(HITEC_PROG_HELD);
    show();
    CHECK_EQ(g_drv.d_reboots, reboots + 1u);
    CHECK_STR_EQ(said(), TR(HP_MOVE_DONE));
    CHECK_EQ(hitec_prog_changes(model()), 0u);
}

int main(void)
{
    RUN(the_find_row_takes_directions_place_while_move_servo_runs);
    RUN(a_tap_starts_the_search_and_a_second_tap_ends_it);
    RUN(a_found_stop_is_said_with_its_end_in_both_languages);
    RUN(each_way_the_search_ends_has_its_line_in_both_languages);
    RUN(a_tab_change_during_the_search_switches_move_servo_off);
    return test_summary("hitec_panel_find");
}
