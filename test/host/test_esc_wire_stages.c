/*
 * The ESC signal-wire session's stages on the bench of esc_wire_rig.h: OPEN with
 * nothing going on is a page of its own with a way out, BACK names a button that
 * acts, a change of stage closes what was open and the screen says the link's state,
 * a leave on an erased AM32, what a press cut by the band does, and the identity tab.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_wire_int.h"

#include "esc_wire_rig.h"

/** @brief A press only, at the middle of @p r, with finger @p id. */
static void press(gfx_rect_t r, uint8_t id)
{
    const touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                              .point = { .id = id, .x = (int16_t)(r.x + r.w / 2),
                                         .y = (int16_t)(r.y + r.h / 2), .strength = 40 } };

    esc_wire_pages.event(&e);
}

/** @brief A release at the middle of @p r, cut when @p cut says so. */
static void lift(gfx_rect_t r, uint8_t id, bool cut)
{
    const touch_event_t e = { .type = TOUCH_EVENT_UP, .flags = cut ? TOUCH_FLAG_NO_TAP : 0u,
                              .point = { .id = id, .x = (int16_t)(r.x + r.w / 2),
                                         .y = (int16_t)(r.y + r.h / 2) } };

    esc_wire_pages.event(&e);
}

/** @brief Whether a stage shows ABORT, as the band's check sees it. */
static bool abort_is_on_the_screen(void)
{
    const gfx_rect_t a = esc_wire_panel_action_rect(0);

    return ewp_safe_at(a.x + 5, a.y + 5);
}

TEST_CASE(abort_in_an_entry_ends_on_the_pin_held_page_with_its_ways_out)
{
    session_up(up_am_rig);
    ew_tap(esc_wire_panel_value_rect(4));
    kp_type("5");
    ew_hold(2.2f);
    bench_run(10u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_ENTERING);
    CHECK(abort_is_on_the_screen());
    press(esc_wire_panel_action_rect(0), 1u);
    bench_run(3000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    CHECK(esc_wire_panel_holds_port());
    /* No ABORT stands there to do nothing; the pin's own ways out do. */
    CHECK(!abort_is_on_the_screen());
    CHECK(esc_wire_panel_hold_rect().w > 0);
    /* BACK names what ends it, and that ends it. */
    esc_wire_pages.back();
    CHECK(strstr(esc_wire_panel_message(), TR(EW_CLOSE_PIN)) != NULL);
    ew_tap(esc_wire_panel_action_rect(2));
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    CHECK(!esc_wire_panel_holds_port());
    CHECK(!g.v.asked_on);
}

TEST_CASE(enter_again_is_a_hold_and_goes_on_to_a_session)
{
    session_up(up_am_rig);
    ew_tap(esc_wire_panel_value_rect(4));
    kp_type("5");
    ew_hold(2.2f);
    bench_run(10u);
    press(esc_wire_panel_action_rect(0), 1u);
    bench_run(3000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    /* A tap is not enough. */
    ew_tap(esc_wire_panel_hold_rect());
    bench_run(100u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    connected();
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_SESSION);
}

TEST_CASE(clear_stop_leaves_the_pin_held_page)
{
    session_up(up_am_rig);
    connected();
    esc_link_stop(&g_l);
    bench_run(200u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_STOPPED);
    esc_wire_pages.back();
    CHECK(strstr(esc_wire_panel_message(), TR(EW_CLOSE_PIN)) != NULL);
    ew_tap(esc_wire_panel_action_rect(0));
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_CLEARED));
    CHECK(!abort_is_on_the_screen());
}

TEST_CASE(a_refused_enter_shows_its_reason_on_the_pin_held_page)
{
    session_up(up_am_rig);
    g.v.online = false;
    ew_hold(2.2f);
    bench_run(2000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    CHECK(strstr(esc_wire_panel_message(), "PD mini") != NULL);
    CHECK(esc_wire_panel_holds_port());
    /* The PD mini answers now: ENTER again goes through. */
    g.v.online = true;
    connected();
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_SESSION);
}

TEST_CASE(a_voltage_too_close_to_the_source_says_so)
{
    session_up(up_am_rig);
    /* SUPPLY's 7.40V is over what the source gives. */
    g.v.vin_mv = 7000u;
    ew_hold(2.2f);
    bench_run(2000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_REF_RANGE));
    CHECK(strstr(esc_wire_panel_message(), "source") != NULL);
    CHECK(!g.v.asked_on);
}

TEST_CASE(back_names_an_action_that_acts_in_every_stage)
{
    /* Another writer. */
    session_up(up_am_rig);
    g_hand = true;
    memset(&g_f, 0, sizeof(g_f));
    g_f.rd.phase = ESC_LINK_READY;
    g_f.rd.known = true;
    g_f.rd.held = true;
    g_f.rd.state = LINK_ESC_OPEN;
    g_f.rd.gpio = 5u;
    esc_wire_panel_feed(&g_f);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_OTHER);
    esc_wire_pages.back();
    CHECK(strstr(esc_wire_panel_message(), TR(EW_RELEASE_BTN)) != NULL);
    /* The pin did not come back. */
    g_f.rd.state = LINK_ESC_NOT_BACK;
    esc_wire_panel_feed(&g_f);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_NOT_BACK);
    esc_wire_pages.back();
    CHECK(strstr(esc_wire_panel_message(), TR(EW_CLOSE_AGAIN)) != NULL);
}

TEST_CASE(a_stage_change_closes_an_editor_and_a_hold_page)
{
    session_up(up_am_rig);
    connected();
    ew_tap(esc_wire_panel_tab_rect(1));
    ew_tap(esc_wire_panel_value_rect(0));
    CHECK(esc_wire_panel_keypad()->open);
    g.v.asked_on = false;
    bench_run(300u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_STOPPED);
    CHECK(!esc_wire_panel_keypad()->open);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_SUPPLY));
}

TEST_CASE(a_stop_under_a_write_page_closes_it_and_its_hold_writes_nothing)
{
    session_up(up_am_rig);
    connected();
    ew_tap(esc_wire_panel_tab_rect(1));
    ew_tap(esc_wire_panel_value_rect(2));
    ew_tap(esc_wire_panel_choice_rect(0));
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_WRITE);
    /* A finger on the hold when the stop comes. */
    press(esc_wire_panel_hold_rect(), 1u);
    bench_run(500u);
    esc_link_stop(&g_l);
    bench_run(2500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_STOPPED);
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NONE);
    CHECK(!ewp_s.hold_down);
    CHECK_EQ(esc.am[0x11], 1u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_STOPPED);
}

TEST_CASE(a_lost_link_closes_the_page_and_says_the_link_is_gone)
{
    session_up(up_am_rig);
    connected();
    ew_tap(esc_wire_panel_tab_rect(1));
    ew_tap(esc_wire_panel_value_rect(2));
    ew_tap(esc_wire_panel_choice_rect(0));
    ew_tap(esc_wire_panel_action_rect(2));
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_WRITE);
    esc_link_lost(&g_l);
    esc_link_readout(&g_l, &g_f.rd);
    g_f.image = *esc_link_image(&g_l);
    g_f.now_ms += 5u;
    g_hand = true;
    esc_wire_panel_feed(&g_f);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NONE);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_NO_LINK));
}

TEST_CASE(leave_on_an_erased_am32_says_it_stays_erased_and_closes)
{
    session_up(up_am_rig);
    memset(esc.am, 0xFF, sizeof(esc.am));
    connected();
    CHECK((esc_wire_panel_model()->f.rd.flags & LINK_EW_F_ERASED) != 0u);
    CHECK(g.v.asked_on);
    ew_tap(esc_wire_panel_back_rect());
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_LEAVE);
    CHECK_STR_EQ(esc_wire_panel_page_line(0), TR(EW_LEAVE_ERASED_1));
    CHECK_STR_EQ(esc_wire_panel_page_line(1), TR(EW_LEAVE_ERASED_2));
    ew_hold(2.2f);
    bench_run(2000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    CHECK(!esc_wire_panel_holds_port());
    CHECK(!g.v.asked_on);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_LEFT));
}

TEST_CASE(dimmed_rows_under_by_hand_open_nothing)
{
    session_up(up_am_rig);
    ewp_s.power = LINK_ESC_BY_HAND;
    ew_tap(esc_wire_panel_value_rect(2));
    CHECK(!esc_wire_panel_keypad()->open);
    ew_tap(esc_wire_panel_value_rect(3));
    CHECK(!esc_wire_panel_keypad()->open);
    ew_tap(esc_wire_panel_value_rect(5));
    CHECK(!esc_wire_panel_keypad()->open);
    /* ENTRY HOLD counts under both. */
    ew_tap(esc_wire_panel_value_rect(4));
    CHECK(esc_wire_panel_keypad()->open);
}

TEST_CASE(a_press_cut_by_the_band_ends_and_does_nothing)
{
    session_up(up_am_rig);
    ewp_s.power = LINK_ESC_BY_HAND;
    ew_hold(2.2f);
    bench_run(100u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_HAND);
    /* POWER IS ON, cut. */
    press(esc_wire_panel_action_rect(1), 1u);
    lift(esc_wire_panel_action_rect(1), 1u, true);
    bench_run(100u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_HAND);
    CHECK(!ewp_s.have);
}

TEST_CASE(a_cut_press_on_a_choice_selects_nothing)
{
    session_up(up_am_rig);
    ew_tap(esc_wire_panel_value_rect(1));
    CHECK_EQ(ewp_s.ch_what, EWP_CH_POWER);
    press(esc_wire_panel_choice_rect(1), 1u);
    lift(esc_wire_panel_choice_rect(1), 1u, true);
    CHECK_EQ(ewp_s.ch_what, EWP_CH_POWER);
    CHECK_EQ(ewp_s.power, (uint8_t)LINK_ESC_SWITCHED);
}

TEST_CASE(abort_goes_with_a_second_finger_while_the_first_holds)
{
    session_up(up_am_rig);
    ew_tap(esc_wire_panel_value_rect(4));
    kp_type("5");
    /* The hold's finger stays down, on through the entry. */
    press(esc_wire_panel_hold_rect(), 1u);
    bench_run(2200u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_ENTERING);
    /* A press taken by another control does not stand in ABORT's way. */
    ewp_s.have = true;
    ewp_s.id = 1u;
    press(esc_wire_panel_action_rect(0), 2u);
    bench_run(3000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
}

TEST_CASE(the_identity_tab_shows_all_of_the_name)
{
    session_up(up_am_rig);
    g_hand = true;
    memset(&g_f, 0, sizeof(g_f));
    g_f.rd.phase = ESC_LINK_READY;
    g_f.image.gen = 1u;
    snprintf(g_f.image.name, sizeof(g_f.image.name), "ABCDEFGHIJKLMNOPQRSTUVWXYZ012");
    esc_wire_panel_feed(&g_f);
    CHECK_STR_EQ(esc_wire_panel_name_line(), "BOARD ABCDEFGHIJKLMNOPQRSTUVWXYZ012");
}

/** @brief ENTRY HOLD 5s, CONNECT, then ABORT 10ms in: the pin held, nothing on. */
static void up_to_pin_held(void)
{
    ew_tap(esc_wire_panel_value_rect(4));
    kp_type("5");
    ew_hold(2.2f);
    bench_run(10u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_ENTERING);
    press(esc_wire_panel_action_rect(0), 1u);
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
}

TEST_CASE(enter_again_sends_what_the_page_shows_not_the_last_connect)
{
    session_up(up_am_rig);
    supply_screen_put(12.0f, 1.0f);
    bench_run(20u);
    up_to_pin_held();
    CHECK_STR_EQ(esc_wire_panel_value_text(2), "12.00V");
    CHECK(!g.v.asked_on);
    /* SUPPLY moves: the page follows it, and the hold sends the page. */
    supply_screen_put(5.0f, 0.5f);
    bench_run(20u);
    CHECK_STR_EQ(esc_wire_panel_value_text(1), TR(EW_POWER_SWITCHED));
    CHECK_STR_EQ(esc_wire_panel_value_text(2), "5.00V");
    CHECK_STR_EQ(esc_wire_panel_value_text(3), "0.50A");
    CHECK_STR_EQ(esc_wire_panel_value_text(4), "5.0s");
    CHECK_STR_EQ(esc_wire_panel_value_text(5), "1.0s");
    g.on_mv = 0u;
    g.on_ma = 0u;
    ew_hold(2.2f);
    bench_run(3000u);
    CHECK(g.v.asked_on);
    CHECK_EQ(g.on_mv, 5000u);
    CHECK_EQ(g.on_ma, 500u);
}

TEST_CASE(the_model_sends_only_the_power_it_is_handed)
{
    session_up(up_am_rig);
    up_to_pin_held();
    esc_prog_connect_t c;

    /* A voltage out of range goes nowhere, whatever the last connect held. */
    ewp_connect_values(&c);
    c.power.mv = 0u;
    CHECK_EQ(esc_prog_enter_again(esc_wire_panel_model(), &c), ESC_PROG_MSG_RANGE);
    CHECK_EQ(esc_prog_enter_again(esc_wire_panel_model(), NULL), ESC_PROG_MSG_BUSY);
    bench_run(200u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    CHECK(!g.v.asked_on);
    /* In another state it does not go. */
    ewp_connect_values(&c);
    g_hand = true;
    g_f.rd.state = LINK_ESC_READY;
    esc_wire_panel_feed(&g_f);
    CHECK_EQ(esc_prog_enter_again(esc_wire_panel_model(), &c), ESC_PROG_MSG_NOT_IN_MODE);
    g_hand = false;
}

TEST_CASE(a_value_that_moves_under_the_hold_ends_the_hold)
{
    session_up(up_am_rig);
    up_to_pin_held();
    press(esc_wire_panel_hold_rect(), 1u);
    bench_run(1000u);
    CHECK(ewp_s.hold_down);
    supply_screen_put(5.0f, 0.5f);
    bench_run(2000u);
    CHECK(!ewp_s.hold_down);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    CHECK(!g.v.asked_on);
}

TEST_CASE(pin_held_shows_by_hand_and_no_voltage_or_limit)
{
    session_up(up_am_rig);
    ewp_s.power = LINK_ESC_BY_HAND;
    CHECK_STR_EQ(esc_wire_panel_value_text(1), TR(EW_POWER_HAND));
    CHECK_STR_EQ(esc_wire_panel_value_text(2), TR(EW_POWER_NONE));
    CHECK_STR_EQ(esc_wire_panel_value_text(3), TR(EW_POWER_NONE));
    esc_prog_connect_t c;

    ewp_connect_values(&c);
    CHECK_EQ(c.power.mv, 0u);
    CHECK_EQ(c.power.ma, 0u);
}

TEST_CASE(enter_again_by_hand_returns_to_hand_with_the_supply_off)
{
    session_up(up_am_rig);
    ewp_s.power = LINK_ESC_BY_HAND;
    ew_hold(2.2f);
    bench_run(100u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_HAND);
    press(esc_wire_panel_action_rect(0), 1u);
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    /* The abort's word, not the wait's. */
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_ABORTED));
    /* No outcome came for that ABORT: the next command ends its claim on one. */
    CHECK_EQ(esc_wire_panel_model()->aborted, 1u);
    ew_hold(2.2f);
    CHECK_EQ(esc_wire_panel_model()->aborted, 0u);
    bench_run(100u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_HAND);
    CHECK(!g.v.asked_on);
    /* A stop later is a stop: the old ABORT does not name it. */
    esc_link_stop(&g_l);
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_STOPPED);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_CUT));
}

TEST_CASE(an_abort_is_named_as_an_abort_and_a_stop_as_a_stop)
{
    session_up(up_am_rig);
    up_to_pin_held();
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_ABORTED));
    /* ENTER AGAIN takes the word away. */
    ew_hold(2.2f);
    CHECK(strcmp(esc_wire_panel_message(), TR(EW_MSG_ABORTED)) != 0);
    /* The band's STOP. */
    bench_run(50u);
    esc_link_stop(&g_l);
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_STOPPED);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_CUT));
}

TEST_CASE(the_link_state_replaces_a_good_word_and_a_written_word_stays)
{
    session_up(up_am_rig);
    esc_prog_t *m = esc_wire_panel_model();

    g_hand = true;
    memset(&g_f, 0, sizeof(g_f));
    g_f.rd.phase = ESC_LINK_NO_PAGE;
    static const esc_prog_msg_t k_gives[] = { ESC_PROG_MSG_NONE, ESC_PROG_MSG_READ_DONE,
                                              ESC_PROG_MSG_CLEARED, ESC_PROG_MSG_LOADED,
                                              ESC_PROG_MSG_LEFT, ESC_PROG_MSG_HAND_WAIT };

    for (size_t i = 0u; i < sizeof(k_gives) / sizeof(k_gives[0]); ++i) {
        m->msg = k_gives[i];
        esc_wire_panel_feed(&g_f);
        CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_NO_PAGE));
    }
    /* Back: the link's word goes. */
    g_f.rd.phase = ESC_LINK_READY;
    esc_wire_panel_feed(&g_f);
    CHECK_STR_EQ(esc_wire_panel_message(), "");
    static const esc_prog_msg_t k_stays[] = { ESC_PROG_MSG_WRITE_DONE,
                                              ESC_PROG_MSG_RESTORE_DONE,
                                              ESC_PROG_MSG_NO_ANSWER };

    g_f.rd.phase = ESC_LINK_DOWN;
    for (size_t i = 0u; i < sizeof(k_stays) / sizeof(k_stays[0]); ++i) {
        m->msg = k_stays[i];
        esc_wire_panel_feed(&g_f);
        CHECK_EQ(esc_prog_message(m, NULL), k_stays[i]);
    }
    m->msg = ESC_PROG_MSG_NONE;
    esc_wire_panel_feed(&g_f);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_NO_LINK));
}

TEST_CASE(a_failing_save_after_a_give_up_keeps_the_note_and_the_esc)
{
    static esc_card_req_t r;

    session_up(up_am_rig);
    connected();
    ew_tap(esc_wire_panel_tab_rect(1));
    ew_tap(esc_wire_panel_value_rect(2));
    ew_tap(esc_wire_panel_choice_rect(0));
    ew_tap(esc_wire_panel_action_rect(2));
    g_card_wait = true;
    ew_hold(2.2f);
    bench_run(10u);
    CHECK(esc_wire_panel_card_take(&r));
    press(esc_wire_panel_action_rect(0), 1u);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_WRITE_GIVEN_UP));
    g_card_wait = false;
    memset(&g_done, 0, sizeof(g_done));
    g_done.op = r.op;
    g_done.id = r.id;
    g_done.result = ESC_CARD_DONE_FULL;
    esc_wire_panel_card_done(&g_done);
    bench_run(20u);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_WRITE_GIVEN_UP));
    CHECK_EQ(esc.am[0x11], 1u);
    CHECK(strstr(TR(EW_WRITE_GIVEN_UP), "unchanged") != NULL);
}

TEST_CASE(an_esc_session_holds_supply_on_a_carrier_and_a_servo_port_does_not)
{
    session_up(up_am_rig);
    supply_screen_set_ports_own(true);
    supply_screen_set_page_held(false);
    CHECK(!supply_screen_port_held());
    /* The page's word on a carrier is an ESC session's. */
    supply_screen_set_page_held(true);
    CHECK(supply_screen_port_held());
    supply_screen_set_page_held(false);
    /* KST and Hitec there stay off it. */
    supply_screen_set_port_held(true);
    CHECK(!supply_screen_port_held());
    supply_screen_set_port_held(false);
    supply_screen_set_hitec_held(true);
    CHECK(!supply_screen_port_held());
    supply_screen_set_hitec_held(false);
    supply_screen_set_ports_own(false);
    /* On any other board the three hold it. */
    supply_screen_set_page_held(true);
    CHECK(supply_screen_port_held());
    supply_screen_set_page_held(false);
}

int main(void)
{
    RUN(abort_in_an_entry_ends_on_the_pin_held_page_with_its_ways_out);
    RUN(enter_again_is_a_hold_and_goes_on_to_a_session);
    RUN(clear_stop_leaves_the_pin_held_page);
    RUN(a_refused_enter_shows_its_reason_on_the_pin_held_page);
    RUN(a_voltage_too_close_to_the_source_says_so);
    RUN(back_names_an_action_that_acts_in_every_stage);
    RUN(a_stage_change_closes_an_editor_and_a_hold_page);
    RUN(a_stop_under_a_write_page_closes_it_and_its_hold_writes_nothing);
    RUN(a_lost_link_closes_the_page_and_says_the_link_is_gone);
    RUN(leave_on_an_erased_am32_says_it_stays_erased_and_closes);
    RUN(dimmed_rows_under_by_hand_open_nothing);
    RUN(a_press_cut_by_the_band_ends_and_does_nothing);
    RUN(a_cut_press_on_a_choice_selects_nothing);
    RUN(abort_goes_with_a_second_finger_while_the_first_holds);
    RUN(the_identity_tab_shows_all_of_the_name);
    RUN(enter_again_sends_what_the_page_shows_not_the_last_connect);
    RUN(the_model_sends_only_the_power_it_is_handed);
    RUN(a_value_that_moves_under_the_hold_ends_the_hold);
    RUN(pin_held_shows_by_hand_and_no_voltage_or_limit);
    RUN(enter_again_by_hand_returns_to_hand_with_the_supply_off);
    RUN(an_abort_is_named_as_an_abort_and_a_stop_as_a_stop);
    RUN(the_link_state_replaces_a_good_word_and_a_written_word_stays);
    RUN(a_failing_save_after_a_give_up_keeps_the_note_and_the_esc);
    RUN(an_esc_session_holds_supply_on_a_carrier_and_a_servo_port_does_not);
    return test_summary("esc_wire_stages");
}
