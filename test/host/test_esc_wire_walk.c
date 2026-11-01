/*
 * The ESC signal-wire session on what the operator meets at the bench: what a stop
 * leaves on offer, the keypads against SUPPLY's limits, a link that falls with no
 * change of stage, how long a word stands, each hold's own hint, a page at rest that
 * moves no revision.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_wire_int.h"

#include "esc_wire_rig.h"

/** @brief A press only, at the middle of @p r. */
static void press(gfx_rect_t r)
{
    const touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                              .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                         .y = (int16_t)(r.y + r.h / 2), .strength = 40 } };

    esc_wire_pages.event(&e);
}

/** @brief The band's STOP, as the panel's latch and the page's stop. */
static void panel_stop(void)
{
    g_stopped = true;
    esc_link_stop(&g_l);
    bench_run(200u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_STOPPED);
}

/** @brief A session cut by a stop, the stop cleared: PIN HELD. */
static void held_after_page_stop(void)
{
    session_up(up_am_rig);
    connected();
    esc_link_stop(&g_l);
    bench_run(200u);
    ew_tap(esc_wire_panel_action_rect(0));
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
}

/** @brief An entry aborted: PIN HELD, nothing latched. */
static void held_after_abort(void)
{
    session_up(up_am_rig);
    ew_tap(esc_wire_panel_value_rect(4));
    kp_type("5");
    ew_hold(2.2f);
    bench_run(10u);
    press(esc_wire_panel_action_rect(0));
    bench_run(3000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
}

/** @brief The link cut, with the feed by hand from here on. */
static void link_cut(void)
{
    esc_link_lost(&g_l);
    esc_link_readout(&g_l, &g_f.rd);
    g_f.now_ms += 5u;
    g_hand = true;
    bench_run(100u);
}

TEST_CASE(the_panels_stop_offers_pin_back_and_no_clear_stop)
{
    session_up(up_am_rig);
    connected();
    panel_stop();
    ew_tap(esc_wire_panel_action_rect(0));
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_STOPPED);
    ew_tap(esc_wire_panel_action_rect(1));
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    CHECK(!esc_wire_panel_holds_port());
}

TEST_CASE(a_page_stop_with_no_panel_latch_keeps_clear_stop)
{
    held_after_page_stop();
    CHECK(esc_wire_panel_holds_port());
}

TEST_CASE(after_a_stop_pin_held_offers_only_pin_back)
{
    held_after_page_stop();
    CHECK(ewp_s.no_again);
    CHECK_EQ(esc_wire_panel_hold_rect().w, 0);
    /* The press goes nowhere, and a hold through the seam sends nothing either. */
    ewp_hold_apply();
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    ew_tap(esc_wire_panel_action_rect(2));
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    /* With the pin free the way is open again. */
    CHECK(!ewp_s.no_again);
    CHECK(esc_wire_panel_hold_rect().w > 0);
}

TEST_CASE(an_abort_keeps_enter_again)
{
    held_after_abort();
    CHECK(!ewp_s.no_again);
    CHECK(esc_wire_panel_hold_rect().w > 0);
    connected();
}

TEST_CASE(a_refused_enter_keeps_enter_again)
{
    session_up(up_am_rig);
    g.v.online = false;
    ew_hold(2.2f);
    bench_run(2000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    CHECK(!ewp_s.no_again);
    CHECK(esc_wire_panel_hold_rect().w > 0);
}

TEST_CASE(a_latched_supply_refuses_the_enter_as_what_it_is)
{
    session_up(up_am_rig);
    g.v.latched = true;
    ew_hold(2.2f);
    bench_run(2000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    CHECK(strstr(esc_wire_panel_message(), "PIN BACK") != NULL);
    CHECK(strstr(esc_wire_panel_message(), "failed") == NULL);
    CHECK(ewp_s.no_again);
    CHECK_EQ(esc_wire_panel_hold_rect().w, 0);
}

TEST_CASE(the_keypads_top_out_at_supplys_limits)
{
    session_up(up_am_rig);
    const supply_caps_t lim = supply_screen_caps();

    ew_tap(esc_wire_panel_value_rect(2));
    CHECK(esc_wire_panel_keypad()->open);
    CHECK_NEAR(esc_wire_panel_keypad()->max, lim.v_max, 0.001f);
    CHECK_NEAR(esc_wire_panel_keypad()->min, lim.v_min > 3.0f ? lim.v_min : 3.0f, 0.001f);
    ew_tap(ui_keypad_key_rect(esc_wire_panel_keypad(), UI_KEY_CANCEL));
    ew_tap(esc_wire_panel_value_rect(3));
    CHECK(esc_wire_panel_keypad()->open);
    CHECK_NEAR(esc_wire_panel_keypad()->max, lim.i_max, 0.001f);
    ew_tap(ui_keypad_key_rect(esc_wire_panel_keypad(), UI_KEY_CANCEL));
    /* Another supply's limits are read at the next open. */
    supply_caps_t low = lim;

    low.v_max = 14.23f;
    low.i_max = 2.0f;
    supply_screen_set_caps(&low);
    supply_screen_settings_loaded();
    ew_tap(esc_wire_panel_value_rect(2));
    CHECK_NEAR(esc_wire_panel_keypad()->max, supply_screen_caps().v_max, 0.001f);
    CHECK(esc_wire_panel_keypad()->max <= 14.24f);
}

TEST_CASE(a_connect_above_supplys_limit_goes_nowhere)
{
    session_up(up_am_rig);
    const supply_caps_t lim = supply_screen_caps();
    const uint16_t top_mv = (uint16_t)(lim.v_max * 1000.0f + 0.5f);
    const uint16_t top_ma = (uint16_t)(lim.i_max * 1000.0f + 0.5f);
    esc_prog_connect_t c;

    memset(&c, 0, sizeof(c));
    c.target = 2u;
    c.power.power = LINK_ESC_SWITCHED;
    c.power.mv = top_mv;
    c.power.ma = top_ma;
    c.power.off_ms = ESC_WIRE_OFF_DEFAULT_MS;
    c.power.hold_ms = 1000u;
    c.power.mv = (uint16_t)(top_mv + 1u);
    CHECK_EQ(esc_prog_connect(esc_wire_panel_model(), &c), ESC_PROG_MSG_RANGE);
    c.power.mv = top_mv;
    c.power.ma = (uint16_t)(top_ma + 1u);
    CHECK_EQ(esc_prog_connect(esc_wire_panel_model(), &c), ESC_PROG_MSG_RANGE);
}

TEST_CASE(a_falling_link_closes_connects_keypad)
{
    session_up(up_am_rig);
    ew_tap(esc_wire_panel_value_rect(2));
    CHECK(esc_wire_panel_keypad()->open);
    link_cut();
    CHECK(!esc_wire_panel_keypad()->open);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_NO_LINK));
}

TEST_CASE(aborted_gives_way_to_the_link_state)
{
    held_after_abort();
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_ABORTED));
    link_cut();
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_NO_LINK));
}

TEST_CASE(a_return_with_the_pin_free_leaves_no_abort_word)
{
    held_after_abort();
    link_cut();
    g_f.rd.phase = ESC_LINK_READY;
    g_f.rd.minor = LINK_MINOR_ESC_WIRE;
    g_f.rd.known = true;
    g_f.rd.held = false;
    g_f.rd.ours = false;
    g_f.rd.state = LINK_ESC_FREE;
    g_f.rd.flags = 0u;
    g_f.rd.gpio = 0xFFu;
    bench_run(50u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    CHECK_STR_EQ(esc_wire_panel_message(), "");
}

TEST_CASE(a_written_word_survives_a_link_gap_and_not_the_next_action)
{
    session_up(up_am_rig);
    connected();
    esc_wire_panel_model()->msg = ESC_PROG_MSG_WRITE_DONE;
    bench_run(20u);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_WRITE_DONE));
    esc_link_lost(&g_l);
    bench_run(50u);
    esc_link_came_up(&g_l, ESC_LINK_MINOR);
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_model()->msg, ESC_PROG_MSG_WRITE_DONE);
    /* A panel that came up again reads the pin as another writer's. */
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_OTHER);
    ew_tap(esc_wire_panel_action_rect(0));
    CHECK(esc_wire_panel_model()->msg != ESC_PROG_MSG_WRITE_DONE);
}

TEST_CASE(the_next_action_clears_a_word)
{
    held_after_abort();
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_ABORTED));
    /* PIN BACK is the next action. */
    ew_tap(esc_wire_panel_action_rect(2));
    bench_run(500u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_MSG_LEFT));
    /* The next one takes that too. */
    ew_tap(esc_wire_panel_value_rect(2));
    CHECK_STR_EQ(esc_wire_panel_message(), "");
}

TEST_CASE(an_error_that_ended_a_flow_stands_on_connect_until_the_next_action)
{
    session_up(up_am_rig);
    esc_prog_word_clear(esc_wire_panel_model());
    esc_wire_panel_model()->msg = ESC_PROG_MSG_NO_ANSWER;
    esc_prog_word_on_free(esc_wire_panel_model());
    CHECK_EQ(esc_wire_panel_model()->msg, ESC_PROG_MSG_NO_ANSWER);
    bench_run(50u);
    CHECK(esc_wire_panel_message()[0] != '\0');
    ew_tap(esc_wire_panel_value_rect(2));
    CHECK_STR_EQ(esc_wire_panel_message(), "");
}

TEST_CASE(a_word_of_no_error_goes_on_arrival)
{
    static const esc_prog_msg_t k_go[] = {
        ESC_PROG_MSG_ABORTED, ESC_PROG_MSG_STOPPED, ESC_PROG_MSG_CLEARED,
        ESC_PROG_MSG_READ_DONE, ESC_PROG_MSG_LOADED, ESC_PROG_MSG_HAND_WAIT,
        ESC_PROG_MSG_WRITE_DONE, ESC_PROG_MSG_RESTORE_DONE,
    };

    session_up(up_am_rig);
    for (size_t i = 0u; i < sizeof(k_go) / sizeof(k_go[0]); ++i) {
        esc_wire_panel_model()->msg = k_go[i];
        esc_prog_word_on_free(esc_wire_panel_model());
        CHECK_EQ(esc_wire_panel_model()->msg, ESC_PROG_MSG_NONE);
    }
    esc_wire_panel_model()->msg = ESC_PROG_MSG_LEFT;
    esc_prog_word_on_free(esc_wire_panel_model());
    CHECK_EQ(esc_wire_panel_model()->msg, ESC_PROG_MSG_LEFT);
}

TEST_CASE(each_hold_page_has_its_own_hint)
{
    session_up(up_am_rig);
    press(esc_wire_panel_hold_rect());
    CHECK_EQ(ewp_s.hold.hint, (int)TX_EW_HOLD_CONNECT);
    ewp_cancel();
    connected();
    static const struct { int page; ui_text_id_t hint; } k_page[] = {
        { EWP_PAGE_RESTORE, TX_EW_HINT_RESTORE },
        { EWP_PAGE_NO_BACKUP, TX_EW_HINT_NO_BACKUP },
        { EWP_PAGE_LEAVE, TX_HOLD_HINT_LEAVE },
        { EWP_PAGE_WRITE, TX_HOLD_HINT_WRITE },
    };

    for (size_t i = 0u; i < sizeof(k_page) / sizeof(k_page[0]); ++i) {
        ewp_s.page = k_page[i].page;
        ewp_s.w_seen_last = true;
        press(esc_wire_panel_hold_rect());
        CHECK_EQ(ewp_s.hold.hint, (int)k_page[i].hint);
        ewp_cancel();
    }
    ewp_s.page = EWP_PAGE_NONE;
    /* ENTER AGAIN. */
    held_after_abort();
    press(esc_wire_panel_hold_rect());
    CHECK_EQ(ewp_s.hold.hint, (int)TX_EW_HOLD_ENTER);
}

TEST_CASE(the_release_page_names_the_pin)
{
    session_up(up_am_rig);
    connected();
    g_f.rd.ours = false;
    g_f.rd.state = LINK_ESC_OPEN;
    g_hand = true;
    bench_run(20u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_OTHER);
    char pin[EWP_LINE];

    (void)esc_wire_panel_target_text(pin, sizeof(pin));
    CHECK(strstr(pin, "GPIO") != NULL);
    ew_tap(esc_wire_panel_action_rect(0));
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_RELEASE);
    bool named = false;

    for (int i = 0; i < ewp_s.page_lines; ++i) {
        named = named || strstr(esc_wire_panel_page_line(i), pin) != NULL;
    }
    CHECK(named);
    press(esc_wire_panel_hold_rect());
    CHECK_EQ(ewp_s.hold.hint, (int)TX_EW_HINT_RELEASE);
}

TEST_CASE(stopped_has_a_german_title)
{
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(TR(EW_STOPPED_TITLE), "GESTOPPT");
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(TR(EW_STOPPED_TITLE), "STOPPED");
}

TEST_CASE(the_targets_padding_moves_no_revision)
{
    session_up(up_am_rig);
    esc_wire_target_t t[2];

    memset(t, 0x55, sizeof(t));
    t[0].target = 2u;
    t[0].gpio = 5u;
    t[1].target = LINK_EW_TARGET_PORT;
    t[1].gpio = 13u;
    esc_wire_panel_set_targets(t, 2u, true);
    const uint32_t rev = esc_wire_pages.revision();

    memset(t, 0xAA, sizeof(t));
    t[0].target = 2u;
    t[0].gpio = 5u;
    t[1].target = LINK_EW_TARGET_PORT;
    t[1].gpio = 13u;
    esc_wire_panel_set_targets(t, 2u, true);
    CHECK_EQ(esc_wire_pages.revision(), rev);
    /* A pin that does move does. */
    t[1].gpio = 14u;
    esc_wire_panel_set_targets(t, 2u, true);
    CHECK(esc_wire_pages.revision() != rev);
}

int main(void)
{
    RUN(the_panels_stop_offers_pin_back_and_no_clear_stop);
    RUN(a_page_stop_with_no_panel_latch_keeps_clear_stop);
    RUN(after_a_stop_pin_held_offers_only_pin_back);
    RUN(an_abort_keeps_enter_again);
    RUN(a_refused_enter_keeps_enter_again);
    RUN(a_latched_supply_refuses_the_enter_as_what_it_is);
    RUN(the_keypads_top_out_at_supplys_limits);
    RUN(a_connect_above_supplys_limit_goes_nowhere);
    RUN(a_falling_link_closes_connects_keypad);
    RUN(aborted_gives_way_to_the_link_state);
    RUN(a_return_with_the_pin_free_leaves_no_abort_word);
    RUN(a_written_word_survives_a_link_gap_and_not_the_next_action);
    RUN(the_next_action_clears_a_word);
    RUN(an_error_that_ended_a_flow_stands_on_connect_until_the_next_action);
    RUN(a_word_of_no_error_goes_on_arrival);
    RUN(each_hold_page_has_its_own_hint);
    RUN(the_release_page_names_the_pin);
    RUN(stopped_has_a_german_title);
    RUN(the_targets_padding_moves_no_revision);
    return test_summary("esc_wire_walk");
}
