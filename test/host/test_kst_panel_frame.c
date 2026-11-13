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
 * - a_card_pulled_under_a_write_hold_writes_at_the_next_hold
 * - the_session_takes_no_press_under_the_footers_write_hold
 * - a_touch_loss_after_the_footers_hold_completes_sends_nothing
 * - a_second_finger_on_the_footers_write_keeps_the_first_hold
 * - a_press_held_on_the_session_starts_no_footer_hold
 * - the_footers_press_says_a_refusal_at_once
 * - a_finger_that_leaves_the_footers_write_ends_its_hold
 * - the_footers_hold_ends_when_the_write_goes_away
 * - the_sessions_write_go_refuses_what_the_footer_does_not_offer
 * - the_footer_asks_no_arm_and_the_card_drives_nothing
 * - a_finger_on_the_card_or_write_keeps_settings_and_port_shut
 * - a_press_on_the_supply_while_it_reads_on_switches_it_off
 * - a_third_finger_on_the_supply_switches_it_off
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
        CHECK_EQ(ready_count(), 3);            /* PORT, supply, SERVO */
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

/* The footer's WRITE n VALUES pressed for @p secs and let go. */
static void press_footer_write(float secs)
{
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ticks(secs);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    scr->tick(0.02f);
    scr->render(&cv, 0);
}

/* WRITE n VALUES in the footer: grey and naming why with no servo and with
 * no change; with changes, from the TUNE tab, a tap shows the hold's hint
 * and writes nothing, a release before 2s writes nothing, and 2s of
 * contact writes with no page between.  Over an open page it starts no
 * hold and says why. */
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
    ui_router_set_alert(NULL);
    tap_write();
    CHECK_STR_EQ(alert(), TR(HOLD_HINT_WRITE));
    CHECK_EQ(kst_panel_editor(), 0);
    ui_router_set_alert(NULL);
    press_footer_write(1.9f);
    CHECK_STR_EQ(alert(), TR(HOLD_HINT_WRITE));
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(kst_prog_changes(M), 3);
    CHECK(!kst_prog_write_enabled(M));
    press_footer_write(2.5f);
    CHECK_EQ(kst_panel_editor(), 0);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(kst_prog_changes(M), 0);
    CHECK(!kst_prog_write_enabled(M));
    CHECK_EQ(kst_panel_page(), PG_TUNE);
    /* Over an open page: no hold, and the band says why. */
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    kst_panel_back();
    CHECK_EQ(kst_panel_editor(), 3);    /* LEAVE's page */
    ui_router_set_alert(NULL);
    press_footer_write(2.5f);
    CHECK_STR_EQ(alert(), TR(PRR_W_PAGE));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(kst_prog_changes(M), 1);
}

/* WRITE's hold, on the footer and on the tab, with a card in the slot at
 * its press and none when it completes: nothing is written, the edits
 * stay and the band says so.  The next hold, its press without a card,
 * writes without the backup file. */
TEST_CASE(a_card_pulled_under_a_write_hold_writes_at_the_next_hold)
{
    for (int on_tab = 0; on_tab < 2; ++on_tab) {
        const gfx_rect_t t = val(PG_SERVO, 3, 1);

        kst_on();
        connected();
        card_in();
        edits();
        ui_router_set_alert(NULL);
        if (on_tab != 0) {
            tap(kst_panel_tab_rect(PG_SERVO));
            ev(cx(t), cy(t), TOUCH_EVENT_DOWN, 1);
        } else {
            ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
        }
        ticks(1.0f);
        card_out();
        ticks(1.5f);
        ev(on_tab != 0 ? cx(t) : ARM_X, on_tab != 0 ? cy(t) : ARM_Y,
           TOUCH_EVENT_UP, 1);
        settle();
        CHECK_STR_EQ(alert(), TR(KP_CARD_PULLED));
        CHECK_EQ(g_rig.sim.n_writes, 0);
        CHECK_EQ(kst_prog_changes(M), 3);
        CHECK(!kst_prog_write_enabled(M));
        if (on_tab != 0) {
            hold_write();
        } else {
            press_footer_write(2.5f);
        }
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
        CHECK_EQ(g_rig.fc.saves, 0);
        CHECK(kst_prog_no_file(M));
    }
}

/* A second finger on the session while the footer's WRITE is held: the
 * session takes no press, the band says why, and the completed hold
 * writes the draft its press began with. */
TEST_CASE(the_session_takes_no_press_under_the_footers_write_hold)
{
    kst_on();
    connected();
    edits();
    tap(kst_panel_tab_rect(PG_TUNE));
    CHECK_EQ(kst_panel_page(), PG_TUNE);
    const uint16_t was = draft(KST_F_DEAD_BAND);
    const gfx_rect_t plus = kst_panel_step_rect(PG_TUNE, 0, true);

    ui_router_set_alert(NULL);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    ev(cx(plus), cy(plus), TOUCH_EVENT_DOWN, 2);
    ticks(0.1f);
    ev(cx(plus), cy(plus), TOUCH_EVENT_UP, 2);
    CHECK_STR_EQ(alert(), TR(FOOT_PRESS_HELD));
    CHECK_EQ(draft(KST_F_DEAD_BAND), was);
    ticks(1.4f);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo(KST_F_DEAD_BAND), was);
    CHECK_EQ(kst_prog_changes(M), 0);
}

/**
 * @brief KST's screen with the servo read and three values edited, the
 *        footer's WRITE n VALUES live.
 */
static void footer_live(void)
{
    kst_on();
    connected();
    edits();
    ui_router_set_alert(NULL);
}

/** @brief The footer's WRITE: no write went and the edits stand. */
static void nothing_written(void)
{
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(kst_prog_changes(M), 3);
    CHECK(!kst_prog_write_enabled(M));
}

/* A second finger on the footer's WRITE under its hold neither starts the
 * hold again nor ends it: the first finger's 2s write. */
TEST_CASE(a_second_finger_on_the_footers_write_keeps_the_first_hold)
{
    footer_live();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ticks(1.5f);
    ev(ARM_X + 6, ARM_Y, TOUCH_EVENT_DOWN, 2);
    ticks(0.7f);
    ev(ARM_X + 6, ARM_Y, TOUCH_EVENT_UP, 2);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
}

/* A finger already on the session's rows: the footer's WRITE starts no
 * hold under it, and 2.5s on it write nothing. */
TEST_CASE(a_press_held_on_the_session_starts_no_footer_hold)
{
    footer_live();
    tap(kst_panel_tab_rect(PG_TUNE));
    const gfx_rect_t r = val(PG_TUNE, 1, 0);

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 2);
    CHECK(kst_panel_pressed());
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ticks(2.5f);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    ev(r.x - 40, cy(r), TOUCH_EVENT_MOVE, 2);
    ev(r.x - 40, cy(r), TOUCH_EVENT_UP, 2);
    nothing_written();
}

/* A write the session refuses whatever the hold (STOP latched): the
 * press on the footer says why at once and starts no hold. */
TEST_CASE(the_footers_press_says_a_refusal_at_once)
{
    footer_live();
    rig_stop(true);
    settle();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    CHECK_EQ(msg(), KST_PROG_MSG_STOPPED);
    ticks(2.5f);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    nothing_written();
    rig_stop(false);
    settle();
}

/* A finger that slides off the footer's WRITE ends its hold; one that
 * moves on it keeps it, and one that slides off after the hold fired
 * leaves the write going. */
TEST_CASE(a_finger_that_leaves_the_footers_write_ends_its_hold)
{
    footer_live();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ticks(0.5f);
    ev(ARM_X + 4, ARM_Y, TOUCH_EVENT_MOVE, 1);
    ticks(0.5f);
    ev(ARM_X, ARM_Y - 120, TOUCH_EVENT_MOVE, 1);
    ticks(2.0f);
    ev(ARM_X, ARM_Y - 120, TOUCH_EVENT_UP, 1);
    nothing_written();
    /* Off the button after the hold fired: the write goes. */
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ticks(2.2f);
    ev(ARM_X, ARM_Y - 120, TOUCH_EVENT_MOVE, 1);
    ev(ARM_X, ARM_Y - 120, TOUCH_EVENT_UP, 1);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
}

/* The session takes the write away under the footer's hold (DISCARD
 * EDITS by the model), or the screen shows another function: the hold
 * ends and nothing is written. */
TEST_CASE(the_footers_hold_ends_when_the_write_goes_away)
{
    footer_live();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    kst_prog_discard(M);
    ticks(0.1f);
    edits();
    ticks(1.5f);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    nothing_written();

    footer_live();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    servo_screen_show_fn(SERVO_FN_LIVE);
    ticks(0.1f);
    servo_screen_show_fn(SERVO_FN_KST);
    ticks(1.5f);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    nothing_written();
}

/* kst_panel_write_go() asks nothing with an editor open, with a press in
 * the session or with no edit; with none of them it sends on the next
 * tick. */
TEST_CASE(the_sessions_write_go_refuses_what_the_footer_does_not_offer)
{
    footer_live();
    kst_panel_back();
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(!kst_panel_write_go());
    kst_panel_back();
    {
        const gfx_rect_t r = val(PG_SERVO, 1, 0);

        ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 2);
        CHECK(!kst_panel_write_go());
        ev(r.x - 40, cy(r), TOUCH_EVENT_MOVE, 2);
        ev(r.x - 40, cy(r), TOUCH_EVENT_UP, 2);
    }
    nothing_written();
    kst_prog_discard(M);
    CHECK(!kst_panel_write_go());
    edits();
    CHECK(kst_panel_write_go());
    tick(0.02f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
}

/* The footer's hold that completes sends on the next tick, as the tab's
 * does: a touch loss in between sends nothing and keeps the edits; with
 * no loss the next tick sends. */
TEST_CASE(a_touch_loss_after_the_footers_hold_completes_sends_nothing)
{
    for (int lost = 1; lost >= 0; --lost) {
        kst_on();
        connected();
        edits();
        rig_frames(10);
        const unsigned writes = g_rig.sim.n_writes;

        ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
        for (int i = 0; i < 99; ++i) {
            scr->tick(0.02f);
        }
        CHECK(kst_prog_state(M) != KST_PROG_WORKING);
        scr->tick(0.05f);       /* the hold completes in this tick */
        CHECK(kst_prog_state(M) != KST_PROG_WORKING);
        if (lost != 0) {
            scr->cancel();
        }
        scr->tick(0.02f);
        ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
        if (lost != 0) {
            rig_frames(50);
            CHECK_EQ(g_rig.sim.n_writes, writes);
            CHECK_EQ(kst_prog_changes(M), 3);
            CHECK(!kst_prog_write_enabled(M));
        } else {
            settle();
            CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
            CHECK_EQ(kst_prog_changes(M), 0);
        }
    }
}

/* No ARM on KST: a finger on the footer's middle for 2.5s asks none.  The
 * output card's places -- CENTER, SWEEP, OUTPUT ON -- are the READY card
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
    screen_tap(SV_CENTER_X, SV_BTN_Y);
    screen_tap(SV_SWEEP_X, SV_BTN_Y);
    ev(SV_SUP_OUT_X, SV_SUP_Y, TOUCH_EVENT_DOWN, 2);
    ticks(2.5f);
    ev(SV_SUP_OUT_X, SV_SUP_Y, TOUCH_EVENT_UP, 2);
    while (servo_screen_take(&c)) {
        CHECK(c.kind != SERVO_CMD_ARM);
        CHECK(c.kind != SERVO_CMD_CENTER);
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

/* The PD mini's output on: the supply row's red dot reads ON, the router
 * hands a press there through the band, and the press asks OFF, as a stop
 * does, before the finger lifts or wherever it slides.  The OFF is taken
 * under a panel too: with the PORT list open over the session, a press on
 * the row still asks it. */
TEST_CASE(a_press_on_the_supply_while_it_reads_on_switches_it_off)
{
    supply_cmd_t sc;

    kst_on();
    g_rig.rail = true;
    kst_prog_feed_t f = M->f;
    f.rd.flags |= LINK_KS_F_RAIL;
    kst_panel_feed(&f);
    supply_screen_set_output(true);
    ticks(0.1f);
    rdy_row_t r = row(PRR_SUPPLY);
    CHECK_STR_EQ(r.label, "PD MINI");
    CHECK_EQ(r.st, (uint8_t)RDY_LIVE);
    const gfx_rect_t a = ready_row_rect(PRR_SUPPLY);
    CHECK(scr->safe_at(a.x + a.w / 2, a.y + a.h / 2));
    CHECK(!scr->safe_at(a.x + a.w / 2, ready_row_rect(PRR_SERVO).y + 4));
    while (supply_screen_poll_cmd(&sc)) { }
    ev(a.x + a.w / 2, a.y + a.h / 2, TOUCH_EVENT_DOWN, 1);
    CHECK(supply_screen_poll_cmd(&sc));
    CHECK(sc.off && !sc.on);
    ev(a.x + a.w / 2, 100, TOUCH_EVENT_MOVE, 1);
    ev(a.x + a.w / 2, 100, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&sc));
    /* Under the PORT list. */
    tap_row(PRR_PORT);
    CHECK(servo_screen_port_choices() > 0);
    while (supply_screen_poll_cmd(&sc)) { }
    ev(a.x + a.w / 2, a.y + a.h / 2, TOUCH_EVENT_DOWN, 1);
    CHECK(supply_screen_poll_cmd(&sc));
    CHECK(sc.off && !sc.on);
    ev(a.x + a.w / 2, a.y + a.h / 2, TOUCH_EVENT_UP, 1);
}

/* A stop counts whatever other fingers hold: with one finger on WRITE and
 * one on a READY row, a third on the supply row that reads ON asks OFF on
 * its press, and a fourth there asks it again. */
TEST_CASE(a_third_finger_on_the_supply_switches_it_off)
{
    supply_cmd_t sc;

    kst_on();
    g_rig.rail = true;
    kst_prog_feed_t f = M->f;
    f.rd.flags |= LINK_KS_F_RAIL;
    kst_panel_feed(&f);
    supply_screen_set_output(true);
    ticks(0.1f);
    const gfx_rect_t a = ready_row_rect(PRR_SUPPLY);
    const gfx_rect_t sv = ready_row_rect(PRR_SERVO);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ev(sv.x + sv.w / 2, sv.y + sv.h / 2, TOUCH_EVENT_DOWN, 2);
    while (supply_screen_poll_cmd(&sc)) { }
    ev(a.x + a.w / 2, a.y + a.h / 2, TOUCH_EVENT_DOWN, 3);
    CHECK(supply_screen_poll_cmd(&sc));
    CHECK(sc.off && !sc.on);
    ev(a.x + a.w / 3, a.y + a.h / 2, TOUCH_EVENT_DOWN, 4);
    CHECK(supply_screen_poll_cmd(&sc));
    CHECK(sc.off && !sc.on);
    for (uint8_t id = 4; id >= 1; --id) {
        ev(a.x + a.w / 2, 100, TOUCH_EVENT_UP, id);
    }
}

int main(void)
{
    RUN(the_session_has_no_close_and_the_home_tag_closes_it);
    RUN(the_ready_card_follows_the_session);
    RUN(a_tap_on_port_opens_the_list_until_the_session_holds_it);
    RUN(a_tap_on_a_row_not_met_names_it_in_the_band);
    RUN(the_footers_write_is_the_sessions_from_any_tab);
    RUN(a_card_pulled_under_a_write_hold_writes_at_the_next_hold);
    RUN(the_session_takes_no_press_under_the_footers_write_hold);
    RUN(a_touch_loss_after_the_footers_hold_completes_sends_nothing);
    RUN(a_second_finger_on_the_footers_write_keeps_the_first_hold);
    RUN(a_press_held_on_the_session_starts_no_footer_hold);
    RUN(the_footers_press_says_a_refusal_at_once);
    RUN(a_finger_that_leaves_the_footers_write_ends_its_hold);
    RUN(the_footers_hold_ends_when_the_write_goes_away);
    RUN(the_sessions_write_go_refuses_what_the_footer_does_not_offer);
    RUN(the_footer_asks_no_arm_and_the_card_drives_nothing);
    RUN(a_finger_on_the_card_or_write_keeps_settings_and_port_shut);
    RUN(a_press_on_the_supply_while_it_reads_on_switches_it_off);
    RUN(a_third_finger_on_the_supply_switches_it_off);
    return test_summary("kst_panel_frame");
}
