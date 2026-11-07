/**
 * @file test_hitec_panel_frame.c
 * @brief HITEC's screen around its session (servo_prog_ready.c): no CLOSE,
 *        the home tag closes the session; the READY card names PORT, the
 *        supply CONNECT switches and the servo read, and follows SERIES
 *        as HITEC's SETTINGS list changes it; with OUTPUTS as the target
 *        PORT is red and a tap opens the PORT list; the footer's middle is
 *        WRITE n VALUES, grey and naming why before CONNECT, and asks no
 *        ARM.
 *
 * Cases:
 * - the_session_has_no_close_and_the_home_tag_closes_it
 * - the_card_names_the_port_and_outputs_is_red
 * - series_from_the_list_reaches_the_supply_row
 * - the_footers_write_is_grey_before_connect_and_asks_no_arm
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

#include "hitec_panel.h"
#include "hitec_prog.h"
#include "servo_ready_int.h"
#include "splash_screen.h"

/** @brief The alert band's text, "" for none. */
static const char *alert(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/** @brief READY row @p i as the card draws it now. */
static rdy_row_t row(int i)
{
    rdy_row_t r;
    memset(&r, 0, sizeof(r));
    CHECK(ready_row(i, &r));
    return r;
}

/** @brief HITEC's screen, the tester on PROG PORT 1 at GPIO 13, the
 *         router on a screen with the band, where a refused press puts its
 *         reason (ui_action_refused()). */
static void on_hitec(void)
{
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_MOTOR);
    ui_router_set_alert(NULL);
    fresh();
    servo_screen_set_drive(1u, 13u, false);
    go(SERVO_FN_HITEC);
    scr->tick(0.02f);
    scr->render(&cv, 0);
    CHECK(hitec_panel_is_open());
}

/* The tab row runs to the session's right edge: CLOSE's old place is the
 * INFO tab.  The home tag closes a session that holds no port. */
TEST_CASE(the_session_has_no_close_and_the_home_tag_closes_it)
{
    on_hitec();
    const gfx_rect_t info = hitec_panel_tab_rect(HITEC_PANEL_PAGES - 1);
    CHECK(info.x + info.w > 560);
    tap(560, info.y + info.h / 2);
    CHECK(hitec_panel_is_open());
    CHECK_EQ(hitec_panel_page(), HITEC_PANEL_PAGES - 1);
    CHECK(!scr->back());
    CHECK(!hitec_panel_is_open());
}

/* A PROG PORT: PORT met, SERVO not connected.  OUTPUTS: PORT red with no
 * PROG PORT, the supply waiting on it, and a tap opens the PORT list. */
TEST_CASE(the_card_names_the_port_and_outputs_is_red)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        on_hitec();
        ui_text_set_language((ui_lang_t)lang);
        CHECK_EQ(ready_count(), (int)PRR_COUNT);
        rdy_row_t r = row(PRR_PORT);
        CHECK_EQ(r.st, (uint8_t)RDY_OK);
        CHECK(strstr(r.value, "13") != NULL);
        r = row(PRR_SERVO);
        CHECK_EQ(r.st, (uint8_t)RDY_IDLE);
        CHECK_STR_EQ(r.value, TR(PRR_NOT_CONN));

        servo_screen_set_drive(0u, 0u, false);
        r = row(PRR_PORT);
        CHECK_EQ(r.st, (uint8_t)RDY_BAD);
        CHECK_STR_EQ(r.value, TR(PRR_NO_PORT));
        CHECK_STR_EQ(r.why, TR(KP_NEED_TARGET_2));
        CHECK_EQ(row(PRR_SUPPLY).st, (uint8_t)RDY_IDLE);
        const gfx_rect_t p = ready_row_rect(PRR_PORT);
        tap(p.x + p.w / 2, p.y + p.h / 2);
        CHECK(servo_screen_port_choices() > 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* SERIES stepped in HITEC's list: CLASSIC needs a supply the bench sets,
 * and the supply row says CONNECT sets it on the PD mini; D series takes
 * the supply as it is. */
TEST_CASE(series_from_the_list_reaches_the_supply_row)
{
    on_hitec();
    rdy_row_t r = row(PRR_SUPPLY);
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    CHECK_STR_EQ(r.value, TR(PRR_AS_IS));
    tap(SV_FOOT_SET_X, SV_FOOT_Y);
    const ui_setlist_t *sl = servo_screen_list();
    CHECK(sl->open);
    const gfx_rect_t minus = ui_setlist_row_part(sl, 0, UI_SL_PART_MINUS);
    tap(minus.x + minus.w / 2, minus.y + minus.h / 2);
    CHECK_EQ(hitec_prog_series(hitec_panel_model()), (uint8_t)HITEC_CLASSIC);
    r = row(PRR_SUPPLY);
    CHECK_STR_EQ(r.label, "PD MINI");
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(PRR_AT_CONNECT));
    settings_cancel_save();
}

/* WRITE n VALUES before CONNECT: grey, a tap names why and opens nothing;
 * a finger on it for 2.5s asks no ARM. */
TEST_CASE(the_footers_write_is_grey_before_connect_and_asks_no_arm)
{
    servo_cmd_t c;

    on_hitec();
    while (servo_screen_take(&c)) { }
    ui_router_set_alert(NULL);
    tap(SV_ARM_X, SV_FOOT_Y);
    CHECK_STR_EQ(alert(), TR(PRR_W_CONNECT));
    CHECK_EQ(hitec_panel_editor(), 0);
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(0.026f);
    }
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_UP, 1);
    while (servo_screen_take(&c)) {
        CHECK(c.kind != SERVO_CMD_ARM);
    }
}

int main(void)
{
    RUN(the_session_has_no_close_and_the_home_tag_closes_it);
    RUN(the_card_names_the_port_and_outputs_is_red);
    RUN(series_from_the_list_reaches_the_supply_row);
    RUN(the_footers_write_is_grey_before_connect_and_asks_no_arm);
    return test_summary("hitec_panel_frame");
}
