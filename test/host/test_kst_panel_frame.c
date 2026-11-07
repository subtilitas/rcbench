/**
 * @file test_kst_panel_frame.c
 * @brief KST's screen around its session (servo_prog_ready.c): the
 *        session has no CLOSE and the home tag is its way back; the READY
 *        card in the output card's place names PORT, the supply CONNECT
 *        switches and the servo read; a tap on PORT opens the PORT list
 *        until the session holds the port, a tap on another row names it;
 *        the footer's middle is the session's WRITE n VALUES, not ARM, and
 *        writes from any tab; a finger on the card or on WRITE keeps
 *        SETTINGS and the PORT list shut.
 *
 * Cases:
 * - the_session_has_no_close_and_the_home_tag_closes_it
 * - the_ready_card_follows_the_session
 * - a_tap_on_port_opens_the_list_until_the_session_holds_it
 * - a_tap_on_a_row_not_met_names_it_in_the_band
 * - the_footers_write_is_the_sessions_from_any_tab
 * - the_footer_asks_no_arm_and_the_card_drives_nothing
 * - a_finger_on_the_card_or_write_keeps_settings_and_port_shut
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"
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

/** @brief A tap on READY row @p i, the screen ticked after it. */
static void tap_row(int i)
{
    const gfx_rect_t r = ready_row_rect(i);
    screen_tap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief A tap on the footer's middle slot. */
static void tap_write(void) { screen_tap(ARM_X, ARM_Y); }

/** @brief The router on a screen with the band, where a refused press
 *         puts its reason (ui_action_refused()); the SERVO screen is
 *         driven by the rig. */
static void band_on(void)
{
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_MOTOR);
    ui_router_set_alert(NULL);
}

/** @brief KST's screen, the link up, the tester on PROG PORT 1 as the
 *         IOmcu reports it, the band on. */
static void kst_on(void)
{
    band_on();
    fresh_screen();
    link_up();
    servo_screen_set_drive((uint8_t)(RIG_PORT + 1u), 13u, false);
    program_open();
}

/* The tab row runs to the session's right edge: where CLOSE sat a tap
 * selects LOCKED.  The home tag closes a session that holds no port and
 * the screen goes back to the hub. */
TEST_CASE(the_session_has_no_close_and_the_home_tag_closes_it)
{
    kst_on();
    CHECK(kst_panel_is_open());
    /* CLOSE's old place: x 500 to 620 of the session's 620px. */
    screen_tap(AREA.x + AREA.w - 8 - 60, AREA.y + 8 + 24);
    CHECK(kst_panel_is_open());
    CHECK_EQ(kst_panel_page(), PG_LOCKED);
    CHECK(!scr->back());
    CHECK(!kst_panel_is_open());
}

/* Before CONNECT: the port, the supply as CONNECT will take it, the servo
 * not connected.  A PD mini that answers: PD MINI, ON AT CONNECT.  After
 * CONNECT and the read: the port held (a red dot), the servo's type. */
TEST_CASE(the_ready_card_follows_the_session)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        kst_on();
        ui_text_set_language((ui_lang_t)lang);
        CHECK_EQ(ready_count(), (int)PRR_COUNT);
        rdy_row_t r = row(PRR_PORT);
        CHECK_STR_EQ(r.label, TR(SVH_PORT));
        CHECK_EQ(r.st, (uint8_t)RDY_OK);
        r = row(PRR_SUPPLY);
        CHECK_STR_EQ(r.label, TR(RDY_SUPPLY));
        CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
        CHECK_STR_EQ(r.value, TR(PRR_BY_HAND));
        r = row(PRR_SERVO);
        CHECK_EQ(r.st, (uint8_t)RDY_IDLE);
        CHECK_STR_EQ(r.value, TR(PRR_NOT_CONN));

        g_rig.rail = true;
        kst_prog_feed_t f = M->f;
        f.rd.flags |= LINK_KS_F_RAIL;
        kst_panel_feed(&f);
        r = row(PRR_SUPPLY);
        CHECK_STR_EQ(r.label, "PD MINI");
        CHECK_STR_EQ(r.value, TR(PRR_AT_CONNECT));

        connected();
        r = row(PRR_PORT);
        CHECK_EQ(r.st, (uint8_t)RDY_LIVE);
        r = row(PRR_SERVO);
        CHECK_EQ(r.st, (uint8_t)RDY_OK);
        CHECK_STR_EQ(r.value, kst_panel_button(PG_SERVO, 1, 1));
    }
    ui_text_set_language(UI_LANG_EN);
}

/* PORT's fix is the PORT list while the session holds nothing; held, the
 * list stays shut and the band names the lock. */
TEST_CASE(a_tap_on_port_opens_the_list_until_the_session_holds_it)
{
    kst_on();
    tap_row(PRR_PORT);
    CHECK(servo_screen_port_choices() > 0);
    scr->cancel();
    (void)scr->back();
    program_open();
    connected();
    ui_router_set_alert(NULL);
    tap_row(PRR_PORT);
    CHECK_EQ(servo_screen_port_choices(), -1);
    CHECK(strlen(alert()) > 0u);
}

/* A row not met says why: SUPPLY with no PD mini the switching by hand,
 * SERVO before CONNECT the session's CONNECT. */
TEST_CASE(a_tap_on_a_row_not_met_names_it_in_the_band)
{
    char want[128];

    kst_on();
    ui_router_set_alert(NULL);
    tap_row(PRR_SUPPLY);
    snprintf(want, sizeof(want), TR(RDY_BAND), TR(RDY_SUPPLY),
             TR(PRR_HAND_WHY));
    CHECK_STR_EQ(alert(), want);
    tap_row(PRR_SERVO);
    snprintf(want, sizeof(want), TR(RDY_BAND), TR(SVH_SERVO),
             TR(PRR_CONN_WHY));
    CHECK_STR_EQ(alert(), want);
}

/* WRITE n VALUES in the footer: grey and naming why with no servo and with
 * no change; with changes, from the TUNE tab, ENABLE WRITE's page first,
 * then the write page.  Over an open page it opens nothing. */
TEST_CASE(the_footers_write_is_the_sessions_from_any_tab)
{
    kst_on();
    ui_router_set_alert(NULL);
    tap_write();
    CHECK_STR_EQ(alert(), TR(PRR_W_CONNECT));
    CHECK_EQ(kst_panel_editor(), 0);
    connected();
    tap_write();
    CHECK_STR_EQ(alert(), TR(PRR_W_NONE));
    CHECK_EQ(kst_panel_editor(), 0);
    edits();
    tap(kst_panel_tab_rect(PG_TUNE));
    CHECK_EQ(kst_panel_page(), PG_TUNE);
    tap_write();
    CHECK_EQ(kst_panel_editor(), 3);    /* ENABLE WRITE's page */
    CHECK(!kst_prog_write_enabled(M));
    /* A second tap over the page opens nothing and says why. */
    ui_router_set_alert(NULL);
    tap_write();
    CHECK_STR_EQ(alert(), TR(PRR_W_PAGE));
    hold(2.1f);
    CHECK(kst_prog_write_enabled(M));
    CHECK_EQ(kst_panel_editor(), 0);
    tap_write();
    CHECK_EQ(kst_panel_editor(), 3);    /* the write page */
    CHECK(kst_panel_warning_line(0) != NULL);
}

/* No ARM on KST: a finger on the footer's middle for 2.5s asks none.  The
 * output card's places -- CENTRE, SWEEP, OUTPUT ON -- are the READY card
 * and command nothing. */
TEST_CASE(the_footer_asks_no_arm_and_the_card_drives_nothing)
{
    servo_cmd_t c;
    supply_cmd_t sc;

    kst_on();
    while (servo_screen_take(&c)) { }
    while (supply_screen_poll_cmd(&sc)) { }
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ticks(2.5f);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    screen_tap(SV_CENTRE_X, SV_BTN_Y);
    screen_tap(SV_SWEEP_X, SV_BTN_Y);
    ev(SV_SUP_OUT_X, SV_SUP_Y, TOUCH_EVENT_DOWN, 2);
    ticks(2.5f);
    ev(SV_SUP_OUT_X, SV_SUP_Y, TOUCH_EVENT_UP, 2);
    while (servo_screen_take(&c)) {
        CHECK(c.kind != SERVO_CMD_ARM);
        CHECK(c.kind != SERVO_CMD_CENTRE);
    }
    CHECK(!supply_screen_poll_cmd(&sc));
    CHECK(!servo_screen_sweeping());
}

/* A finger on a READY row, or on WRITE: SETTINGS opens nothing and the
 * PORT button opens no list, each naming the finger; lifted, both open. */
TEST_CASE(a_finger_on_the_card_or_write_keeps_settings_and_port_shut)
{
    for (int k = 0; k < 2; ++k) {
        kst_on();
        const gfx_rect_t r = ready_row_rect(PRR_SERVO);
        const int x = (k == 0) ? r.x + r.w / 2 : ARM_X;
        const int y = (k == 0) ? r.y + r.h / 2 : ARM_Y;
        ev(x, y, TOUCH_EVENT_DOWN, 3);
        ui_router_set_alert(NULL);
        ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_DOWN, 2);
        ev(SV_PORT_X, SV_PORT_Y, TOUCH_EVENT_UP, 2);
        CHECK_EQ(servo_screen_port_choices(), -1);
        CHECK_STR_EQ(alert(), TR(FOOT_PRESS_HELD));
        ui_router_set_alert(NULL);
        ev(SETB_X, BTN_Y, TOUCH_EVENT_DOWN, 2);
        ev(SETB_X, BTN_Y, TOUCH_EVENT_UP, 2);
        CHECK_STR_EQ(alert(), TR(FOOT_PRESS_HELD));
        CHECK_EQ(kst_panel_editor(), 0);
        /* The first finger moved off, then lifted: nothing of its own. */
        ev(x, y - 200, TOUCH_EVENT_MOVE, 3);
        ev(x, y - 200, TOUCH_EVENT_UP, 3);
        screen_tap(SV_PORT_X, SV_PORT_Y);
        CHECK(servo_screen_port_choices() > 0);
    }
}

int main(void)
{
    RUN(the_session_has_no_close_and_the_home_tag_closes_it);
    RUN(the_ready_card_follows_the_session);
    RUN(a_tap_on_port_opens_the_list_until_the_session_holds_it);
    RUN(a_tap_on_a_row_not_met_names_it_in_the_band);
    RUN(the_footers_write_is_the_sessions_from_any_tab);
    RUN(the_footer_asks_no_arm_and_the_card_drives_nothing);
    RUN(a_finger_on_the_card_or_write_keeps_settings_and_port_shut);
    return test_summary("kst_panel_frame");
}
