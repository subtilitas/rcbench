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
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_limit_panel_rig.h"

#include "servo_ready_int.h"
#include "ui_text.h"

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

int main(void)
{
    RUN(the_series_row_shows_and_picks_the_series);
    RUN(the_series_row_refuses_while_the_page_holds_the_port);
    RUN(four_rows_sit_above_the_rail_line);
    return test_summary("hitec_series_row");
}
