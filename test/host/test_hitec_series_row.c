/**
 * @file test_hitec_series_row.c
 * @brief HITEC's READY card SERIES row (servo_prog_ready.c, servo_pick.c):
 *        the second of four rows, D SERIES or CLASSIC as SET_HITEC_SERIES
 *        holds it; a tap opens the two over the left column and a pick
 *        writes the setting, asks a save and reaches the programmer's next
 *        CONNECT; LIMIT RUN's and RUDDER AUTO's SERVO stay; while the page
 *        holds the port the row shows the session's series and a tap names
 *        why it takes no pick.  The bench is hitec_limit_panel_rig.h.
 *
 * Cases:
 * - the_series_row_shows_and_picks_the_series
 * - the_series_row_refuses_while_the_page_holds_the_port
 * - four_rows_sit_above_the_rail_line
 * - a_held_press_refuses_the_series_list_and_pick
 * - a_held_port_refuses_a_series_pick_past_the_row
 * - a_pick_outside_the_choices_changes_nothing
 * - the_programmers_card_has_no_rows_elsewhere
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_limit_panel_rig.h"

#include "servo_ready_int.h"
#include "ui_text.h"

/* servo_screen_int.h's press guard and the SERVO and SERIES picks
 * (servo_pick.c), which this suite does not take in whole. */
bool svs_press_held(void);
void svs_series_open(void);
void svs_series_pick(int value);
void svs_family_pick(setting_id_t id, int value);

/** @brief The HITEC page on a SAFE bench, the D485HW on the port. */
static void on_hitec(void)
{
    hl_fresh(true);
    disarm();
    hl_ms(100u);
    go(SERVO_FN_HITEC);
    hl_ms(100u);
    CHECK(hitec_panel_is_open());
    CHECK(!hl.armed);
    ui_router_set_alert(NULL);
}

/** @brief The card's SERIES row as it reads now. */
static rdy_row_t series_row(void)
{
    rdy_row_t r;
    memset(&r, 0, sizeof(r));
    CHECK(ready_row(prog_ready_at(PRR_SERIES), &r));
    return r;
}

/** @brief A tap on the card's SERIES row. */
static void tap_series(void)
{
    const gfx_rect_t r = ready_row_rect(prog_ready_at(PRR_SERIES));
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/* SERIES reads the setting; its list holds D SERIES and CLASSIC with the
 * servos CLASSIC covers; a pick writes the setting, asks a save and is the
 * series the programmer's next CONNECT enters.  The runs' SERVO stays. */
TEST_CASE(the_series_row_shows_and_picks_the_series)
{
    on_hitec();
    rdy_row_t r = series_row();
    CHECK_STR_EQ(r.label, TR(HP_SERIES));
    CHECK_STR_EQ(r.value, "D SERIES");
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    CHECK(r.fix);

    tap_series();
    CHECK_EQ(servo_screen_family_choices(), 2);
    CHECK_STR_EQ(servo_screen_family_choice(0), "D SERIES");
    CHECK_STR_EQ(servo_screen_family_choice(1),
                 "CLASSIC  HS-5xxx and HS-7xxx servos.");
    settings_cancel_save();
    ov_choose(1);
    CHECK_EQ(servo_screen_family_choices(), -1);
    CHECK_EQ(settings_get_int(SET_HITEC_SERIES), (int)HITEC_CLASSIC);
    CHECK(settings_save_asked());
    CHECK_EQ(hitec_prog_series(model()), (uint8_t)HITEC_CLASSIC);
    CHECK_STR_EQ(series_row().value, "CLASSIC");
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_KST);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_LIMRUN_FAM_KST);

    /* German: the series in German. */
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(series_row().label, "SERIE");
    CHECK_STR_EQ(series_row().value, "KLASSISCH");
    ui_text_set_language(UI_LANG_EN);

    tap_series();
    ov_choose(0);
    CHECK_EQ(settings_get_int(SET_HITEC_SERIES), (int)HITEC_D);
    CHECK_EQ(hitec_prog_series(model()), (uint8_t)HITEC_D);
    settings_cancel_save();
}

/* CONNECT holds the port: SERIES shows the session's series, takes no
 * pick, and a tap names why in the alert band. */
TEST_CASE(the_series_row_refuses_while_the_page_holds_the_port)
{
    char want[160];

    on_hitec();
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_NONE);
    for (int i = 0; i < 400 && !hitec_panel_holds_port(); ++i) {
        hl_ms(10u);
    }
    CHECK(hitec_panel_holds_port());
    rdy_row_t r = series_row();
    CHECK(!r.fix);
    CHECK_STR_EQ(r.value, "D SERIES");
    CHECK_STR_EQ(r.why, TR(PRR_SERIES_HELD));
    tap_series();
    CHECK_EQ(servo_screen_family_choices(), -1);
    snprintf(want, sizeof(want), TR(RDY_BAND), TR(HP_SERIES),
             TR(PRR_SERIES_HELD));
    CHECK(ui_router_alert() != NULL);
    CHECK_STR_EQ(ui_router_alert(), want);
    CHECK_EQ(settings_get_int(SET_HITEC_SERIES), (int)HITEC_D);
}

/* HITEC's four rows sit above the rail line, KST keeps its three. */
TEST_CASE(four_rows_sit_above_the_rail_line)
{
    on_hitec();
    CHECK_EQ(ready_count(), 4);
    CHECK_EQ(prog_ready_at(PRR_PORT), 0);
    CHECK_EQ(prog_ready_at(PRR_SERIES), 1);
    CHECK_EQ(prog_ready_at(PRR_SUPPLY), 2);
    CHECK_EQ(prog_ready_at(PRR_SERVO), 3);
    const gfx_rect_t last = ready_row_rect(3);
    CHECK(last.y + last.h <= ready_rail_rect().y);
    hitec_panel_close();
    hl_ms(100u);
    go(SERVO_FN_KST);
    hl_ms(100u);
    CHECK_EQ(ready_count(), 3);
    CHECK_EQ(prog_ready_at(PRR_SERIES), -1);
    CHECK_EQ(prog_ready_at(PRR_SUPPLY), 1);
}

/* A finger held on another READY row: SERIES opens no list and takes no
 * pick (the router names the held press, FOOT_PRESS_HELD, on a live screen
 * only).  Released, the list opens. */
TEST_CASE(a_held_press_refuses_the_series_list_and_pick)
{
    on_hitec();
    const gfx_rect_t port = ready_row_rect(prog_ready_at(PRR_PORT));
    ev(port.x + port.w / 2, port.y + port.h / 2, TOUCH_EVENT_DOWN, 1);
    CHECK(svs_press_held());
    svs_series_open();
    CHECK_EQ(servo_screen_family_choices(), -1);
    svs_series_pick((int)HITEC_CLASSIC);
    CHECK_EQ(settings_get_int(SET_HITEC_SERIES), (int)HITEC_D);
    /* LIMIT RUN's and RUDDER AUTO's SERVO refuse the same way. */
    svs_family_pick(SET_LIMIT_FAMILY, (int)SERVO_LIMRUN_FAM_HITEC_D);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_KST);
    ev(port.x + port.w / 2, port.y + port.h / 2, TOUCH_EVENT_UP, 1);
    hl_ms(20u);
    CHECK(!svs_press_held());
    svs_series_open();
    CHECK_EQ(servo_screen_family_choices(), 2);
}

/* The page holding the port refuses the list and a pick made past the
 * row, as the row's tap names; the setting stays. */
TEST_CASE(a_held_port_refuses_a_series_pick_past_the_row)
{
    on_hitec();
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_NONE);
    for (int i = 0; i < 400 && !hitec_panel_holds_port(); ++i) {
        hl_ms(10u);
    }
    CHECK(hitec_panel_holds_port());
    ui_router_set_alert(NULL);
    svs_series_open();
    CHECK_EQ(servo_screen_family_choices(), -1);
    svs_series_pick((int)HITEC_CLASSIC);
    CHECK_EQ(settings_get_int(SET_HITEC_SERIES), (int)HITEC_D);
    CHECK_EQ(hitec_prog_series(model()), (uint8_t)HITEC_D);
}

/* A value that is no series, or no family, or a setting that is no
 * screen's SERVO, changes nothing and asks no save. */
TEST_CASE(a_pick_outside_the_choices_changes_nothing)
{
    on_hitec();
    settings_cancel_save();
    svs_series_pick(-1);
    svs_series_pick((int)HITEC_CLASSIC + 1);
    CHECK_EQ(settings_get_int(SET_HITEC_SERIES), (int)HITEC_D);
    svs_family_pick(SET_LIMIT_FAMILY, (int)SERVO_LIMRUN_FAM_COUNT);
    svs_family_pick(SET_LIMIT_FAMILY, -1);
    svs_family_pick(SET_HITEC_SERIES, (int)SERVO_LIMRUN_FAM_HITEC_C);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_KST);
    CHECK_EQ(settings_get_int(SET_HITEC_SERIES), (int)HITEC_D);
    CHECK(!settings_save_asked());
}

/* Off KST and HITEC the programmers' card has no rows. */
TEST_CASE(the_programmers_card_has_no_rows_elsewhere)
{
    on_hitec();
    hitec_panel_close();
    hl_ms(100u);
    go(SERVO_FN_LIMRUN);
    hl_ms(100u);
    CHECK_EQ(prog_ready_count(), 0);
    CHECK_EQ(prog_ready_at(PRR_PORT), -1);
}

int main(void)
{
    RUN(the_series_row_shows_and_picks_the_series);
    RUN(the_series_row_refuses_while_the_page_holds_the_port);
    RUN(four_rows_sit_above_the_rail_line);
    RUN(a_held_press_refuses_the_series_list_and_pick);
    RUN(a_held_port_refuses_a_series_pick_past_the_row);
    RUN(a_pick_outside_the_choices_changes_nothing);
    RUN(the_programmers_card_has_no_rows_elsewhere);
    return test_summary("hitec_series_row");
}
