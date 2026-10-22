/**
 * @file test_supply_screen_home.c
 * @brief The supply screen's drags, the home tag and the supply held by a
 * programming port: what each ends, answers or draws dimmed.
 *
 * Cases:
 * - a_drag_moves_the_set_point_by_the_distance_dragged
 * - a_typed_cap_rounds_down_and_a_typed_trip_stays_a_trip
 * - a_drag_on_a_live_output_changes_nothing_until_it_is_answered
 * - the_output_switch_works_under_every_overlay
 * - a_second_finger_on_a_track_changes_nothing_while_off_is_held
 * - leaving_drops_the_question_and_the_overlays
 * - the_home_tag_answers_supplys_question_cancel_first
 * - the_home_tag_closes_a_set_point_keypad_first
 * - the_home_tag_closes_a_settings_keypad_then_leaves_settings
 * - the_home_tag_closes_a_question_over_the_settings_first
 * - a_drag_the_home_tag_releases_opens_no_question
 * - a_trip_shows_on_the_mode_card
 * - a_supply_held_by_a_port_takes_off_and_no_other_press
 * - a_port_that_takes_the_supply_ends_what_was_under_way
 * - caps_reach_the_set_points_when_the_port_lets_go
 * - a_held_supply_is_drawn_dimmed_and_says_who_holds_it
 * - the_pd_minis_rate_is_shown_after_online
 *
 * SPDX-License-Identifier: MIT
 */

#include "supply_screen_rig.h"

TEST_CASE(a_drag_moves_the_set_point_by_the_distance_dragged)
{
    /* Many small moves, as a finger makes them: the set point follows the
     * finger and does not run ahead of it.  17.7 V over 413 px is about
     * 43 mV a pixel. */
    fresh();
    const float per_px = (21.0f - 3.3f) / (float)(TRACK_W - 1);
    ev(TRACK_X + 100, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    const float start = supply_screen_set_v();
    for (int k = 1; k <= 20; ++k) {
        ev(TRACK_X + 100 + k, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    }
    CHECK_NEAR(supply_screen_set_v(), start + 20.0f * per_px, 0.03f);
    for (int k = 0; k < 5; ++k) {          /* a still finger, jittering in y */
        ev(TRACK_X + 120, V_ROW_Y + (k & 1), TOUCH_EVENT_MOVE, 1);
    }
    CHECK_NEAR(supply_screen_set_v(), start + 20.0f * per_px, 0.03f);
    ev(TRACK_X + 120, V_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK_NEAR(supply_screen_set_v(), start + 20.0f * per_px, 0.03f);

    /* On a live output with the question on, the question offers the
     * distance dragged too. */
    fresh();
    supply_screen_set_output(true);
    ev(TRACK_X + 100, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int k = 1; k <= 10; ++k) {
        ev(TRACK_X + 100 + k, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    }
    ev(TRACK_X + 110, V_ROW_Y, TOUCH_EVENT_UP, 1);
    tap(APPLY_X, ASK_Y);
    const float at_100 = 3.3f + 100.0f * per_px;
    CHECK_NEAR(supply_screen_set_v(), at_100 + 10.0f * per_px, 0.03f);
}

TEST_CASE(a_typed_cap_rounds_down_and_a_typed_trip_stays_a_trip)
{
    fresh();
    tap(SET_TAB_X, STRIP_Y);
    tap(ROW_L_X, VMAX_Y);
    keys("12.01");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 12.0f, 1e-4f);
    tap(ROW_L_X, VMAX_Y + 42);           /* CURRENT MAX */
    keys("1.03");
    CHECK_NEAR(settings_get(SET_SUPPLY_I_MAX), 1.0f, 1e-4f);
    tap(ROW_R_X, TRIPI_Y);
    keys("0.02");
    CHECK_NEAR(settings_get(SET_SUPPLY_TRIP_I), 0.05f, 1e-4f);
    CHECK(supply_screen_limits().trip_i > 0.0f);
}

TEST_CASE(a_drag_on_a_live_output_changes_nothing_until_it_is_answered)
{
    fresh();
    supply_screen_set_output(true);
    ev(TRACK_X + 20, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 200, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    ev(TRACK_X + 200, V_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(DISCARD_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);

    /* Without the question it is live as it moves. */
    settings_set(SET_SUPPLY_CONFIRM_SLIDE, 0.0f);
    ev(TRACK_X + 20, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    const float first = supply_screen_set_v();
    ev(TRACK_X + 200, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    CHECK(supply_screen_set_v() > first);
    ev(TRACK_X + 200, V_ROW_Y, TOUCH_EVENT_UP, 1);

    /* And a drag whose release went missing goes back to the set point. */
    settings_set(SET_SUPPLY_CONFIRM_SLIDE, 1.0f);
    const float was = supply_screen_set_v();
    ev(TRACK_X + 20, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 300, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    scr->cancel();
    ev(TRACK_X + 300, V_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK_NEAR(supply_screen_set_v(), was, 1e-4f);
    tap(APPLY_X, ASK_Y);                 /* no question was asked */
    CHECK_NEAR(supply_screen_set_v(), was, 1e-4f);
    /* The next step starts from the set point, not from where the lost
     * drag had left the slider. */
    tap(V_UP_X, V_ROW_Y);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), was + 0.1f, 1e-3f);
}

TEST_CASE(the_output_switch_works_under_every_overlay)
{
    supply_cmd_t c;
    fresh();
    supply_screen_set_output(true);
    tap(SET_TAB_X, STRIP_Y);             /* SETTINGS */
    tap(OUT_X, OUT_Y);
    CHECK(supply_screen_poll_cmd(&c) && c.off);
    tap(CARD_X, CARD_V_Y);               /* the keypad */
    tap(OUT_X, OUT_Y);
    CHECK(supply_screen_poll_cmd(&c) && c.off);
    key(UI_KEY_CANCEL);
    tap(LIVE_TAB_X, STRIP_Y);
    tap(V_UP_X, V_ROW_Y);                /* the question */
    tap(OUT_X, OUT_Y);
    CHECK(supply_screen_poll_cmd(&c) && c.off);
    tap(RESET_X, RESET_Y);
    CHECK(supply_screen_poll_cmd(&c) && c.reset);
    /* While a question is asked, the strip and the cards wait for it. */
    tap(SET_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    tap(CARD_X, CARD_V_Y);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
}

TEST_CASE(a_second_finger_on_a_track_changes_nothing_while_off_is_held)
{
    fresh();
    settings_set(SET_SUPPLY_CONFIRM_SLIDE, 0.0f);
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + TRACK_W - 2, V_ROW_Y, TOUCH_EVENT_DOWN, 2);
    ev(TRACK_X + TRACK_W - 2, V_ROW_Y, TOUCH_EVENT_UP, 2);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c) && c.off);
}

TEST_CASE(leaving_drops_the_question_and_the_overlays)
{
    fresh();
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);
    scr->leave();
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(SET_TAB_X, STRIP_Y);
    scr->leave();
    scr->enter();                        /* a tile's entry: LIVE */
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    settings_set(SET_SUPPLY_CONFIRM_SLIDE, 0.0f);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    tap(CARD_X, CARD_V_Y);
    scr->leave();
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.2f, 1e-4f);
}

/* ------------------------------------------------------ the home tag */

/*
 * The home tag climbs one level (ui_screen_t.back): a question about a live
 * output is answered CANCEL and the screen stays.  The output this screen
 * switched on stays on and owned; with nothing open the tag leaves.
 */
TEST_CASE(the_home_tag_answers_supplys_question_cancel_first)
{
    fresh();
    hold_on();
    drain();
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);                    /* the question */
    CHECK(scr->back());
    tap(APPLY_X, ASK_Y);                     /* no question to answer */
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    CHECK(!supply_screen_poll_cmd(NULL));
    CHECK(supply_screen_output_on());
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SUPPLY);
    CHECK(!scr->back());
    /* And the set points are the screen's again: the next step asks. */
    tap(V_UP_X, V_ROW_Y);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
}

/* A set point's keypad closes with nothing typed kept. */
TEST_CASE(the_home_tag_closes_a_set_point_keypad_first)
{
    fresh();
    tap(CARD_X, CARD_V_Y);
    key(UI_KEY_7);
    CHECK(scr->back());
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(V_UP_X, V_ROW_Y);                    /* the keypad is gone */
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    CHECK(!scr->back());
    CHECK(!supply_screen_poll_cmd(NULL));
}

/*
 * SETTINGS, and a keypad over its page: the keypad closes first, with the
 * setting typed into it unchanged, and SETTINGS stays.  The tasks are
 * siblings, not levels, as on MOTOR & ESC and SERVO: on SETTINGS with
 * nothing open the tag leaves the screen.
 */
TEST_CASE(the_home_tag_closes_a_settings_keypad_then_leaves_settings)
{
    fresh();
    const float vmax = settings_get(SET_SUPPLY_V_MAX);
    tap(SET_TAB_X, STRIP_Y);                 /* SETTINGS */
    tap(ROW_L_X, VMAX_Y);                    /* VOLTAGE MAX's keypad */
    key(UI_KEY_8);
    CHECK(scr->back());
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), vmax, 1e-4f);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    tap(ROW_L_X, VMAX_Y);                    /* the page again, no keypad */
    keys("9");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 9.0f, 1e-4f);
    CHECK(!scr->back());                     /* SETTINGS is no level */
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    CHECK(!supply_screen_poll_cmd(NULL));
    settings_cancel_save();
}

/*
 * The router releases a drag before it asks back() (ui_router.c,
 * router_back()), with a release the finger did not make.  That release
 * opens no question, so the home tag finds nothing open and leaves.  With
 * the question on and the output live the drag changed nothing; otherwise
 * it moved the set point live and keeps that.
 */
TEST_CASE(a_drag_the_home_tag_releases_opens_no_question)
{
    for (int k = 0; k < 4; ++k) {
        const bool ask  = (k & 1) != 0;
        const bool live = (k & 2) != 0;
        fresh();
        settings_set(SET_SUPPLY_CONFIRM_SLIDE, ask ? 1.0f : 0.0f);
        supply_screen_set_output(live);
        const float was = supply_screen_set_v();
        ev(TRACK_X + 10, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
        ev(TRACK_X + 200, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
        const touch_event_t up = { .type = TOUCH_EVENT_UP,
                                   .point = { .id = 1, .x = TRACK_X + 200,
                                              .y = V_ROW_Y, .strength = 40 },
                                   .flags = TOUCH_FLAG_NO_TAP };
        scr->event(&up);
        if (scr->back()) {
            T_FAIL("ask %d live %d: a question was open", ask, live);
        }
        if (ask && live) {
            CHECK_NEAR(supply_screen_set_v(), was, 1e-4f);
        } else {
            CHECK(fabsf(supply_screen_set_v() - was) > 0.05f);
        }
        const float now = supply_screen_set_v();
        tap(APPLY_X, ASK_Y);                 /* no question to answer */
        CHECK_NEAR(supply_screen_set_v(), now, 1e-4f);
        settings_cancel_save();
    }
}

/* A question asked from a set point's keypad on SETTINGS: the question
 * goes first, and SETTINGS stays. */
TEST_CASE(the_home_tag_closes_a_question_over_the_settings_first)
{
    fresh();
    supply_screen_set_output(true);
    tap(SET_TAB_X, STRIP_Y);                 /* SETTINGS */
    tap(CARD_X, CARD_V_Y);                   /* a set point's keypad */
    keys("7");                               /* the question */
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    CHECK(scr->back());
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    tap(APPLY_X, ASK_Y);                     /* the change was dropped */
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    /* The strip answers again, and SETTINGS is no level of its own. */
    CHECK(!scr->back());
    tap(LIVE_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    CHECK(supply_screen_output_on());
    CHECK(!supply_screen_poll_cmd(NULL));
}

/*
 * The rate AUTO found is in the header, after ONLINE, and not in the alert
 * band, which stays until another alert replaces it.  A change redraws the
 * header without anything else moving.
 */
TEST_CASE(the_pd_minis_rate_is_shown_after_online)
{
    fresh();
    supply_screen_set_model(false);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    supply_screen_push(&st);
    supply_screen_set_baud(0u);
    scr->render(&cv, 0);
    memcpy(fb2, fb, (size_t)W * H * sizeof(gfx_color_t));

    supply_screen_set_baud(38400u);
    scr->render(&cv, 0);
    CHECK(header_differs() > 0);

    /* The same rate again changes nothing. */
    memcpy(fb2, fb, (size_t)W * H * sizeof(gfx_color_t));
    supply_screen_set_baud(38400u);
    scr->render(&cv, 0);
    CHECK_EQ(header_differs(), 0);

    /* Not while the module does not answer: NOT ANSWERING alone. */
    st.online = false;
    supply_screen_push(&st);
    supply_screen_set_baud(0u);
    scr->render(&cv, 0);
    memcpy(fb2, fb, (size_t)W * H * sizeof(gfx_color_t));
    supply_screen_set_baud(38400u);
    scr->render(&cv, 0);
    CHECK_EQ(header_differs(), 0);

    /* Nor for the model. */
    supply_screen_set_model(true);
    st.online = true;
    supply_screen_push(&st);
    supply_screen_set_baud(0u);
    scr->render(&cv, 0);
    memcpy(fb2, fb, (size_t)W * H * sizeof(gfx_color_t));
    supply_screen_set_baud(38400u);
    scr->render(&cv, 0);
    CHECK_EQ(header_differs(), 0);
}

TEST_CASE(a_trip_shows_on_the_mode_card)
{
    fresh();
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.mode = SUPPLY_MODE_OFF;
    supply_screen_push(&st);
    supply_invalidate();
    scr->render(&cv, 0);
    st.trip = SUPPLY_TRIP_CURRENT;
    supply_screen_push(&st);
    supply_invalidate();
    scr->render(&cv2, 0);
    /* The MODE card, the rail's fourth: x 558..794, y 212..266. */
    int differ = 0;
    for (int y = 212; y < 266; ++y) {
        for (int x = 558; x < 794; ++x) {
            differ += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    CHECK(differ > 0);
    /* And the voltage trip reads differently from the current trip. */
    memcpy(fb, fb2, (size_t)W * H * sizeof(gfx_color_t));
    st.trip = SUPPLY_TRIP_VOLTAGE;
    supply_screen_push(&st);
    supply_invalidate();
    scr->render(&cv2, 0);
    differ = 0;
    for (int y = 212; y < 266; ++y) {
        for (int x = 558; x < 794; ++x) {
            differ += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    CHECK(differ > 0);
}

/* -------------------------------------------- held by a programming port */

/* Pixels that differ between the two frames inside a rectangle. */
static int differ_in(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            n += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    return n;
}

/* While a servo programming port is out of PWM the screen offers what the
 * IOmcu takes: off, and nothing else. */
TEST_CASE(a_supply_held_by_a_port_takes_off_and_no_other_press)
{
    supply_cmd_t c;

    fresh();
    CHECK(!supply_screen_port_held());
    supply_screen_set_port_held(true);
    CHECK(supply_screen_port_held());
    /* No ON: not by the hold, not asked by another screen. */
    hold_on();
    CHECK(!supply_screen_poll_cmd(&c));
    supply_screen_ask_on(SUPPLY_BY_SERVO);
    CHECK(!supply_screen_poll_cmd(&c));
    CHECK(!supply_screen_output_live());
    CHECK(!supply_screen_output_held());
    /* No set point: steps, tracks, cards, values, another screen's put. */
    tap(V_UP_X, V_ROW_Y);
    tap(V_DOWN_X, I_ROW_Y);
    tap(TRACK_X + TRACK_W / 2, V_ROW_Y);
    tap(TRACK_X + TRACK_W - 1, I_ROW_Y);
    ev(TRACK_X + 10, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 200, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    ev(TRACK_X + 200, V_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);
    tap(CARD_X, CARD_V_Y);
    keys("7.4");
    tap(CARD_X, CARD_I_Y);
    keys("1.5");
    tap(TEXT_X, TEXT_V_Y);
    keys("9");
    tap(TEXT_X, TEXT_I_Y);
    keys("1");
    supply_screen_put(8.0f, 3.0f);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);
    /* What the port set is shown. */
    supply_screen_adopt(5.0f, 1.0f);
    CHECK_NEAR(supply_screen_set_v(), 5.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 1.0f, 1e-4f);
    /* RESET PEAKS works. */
    tap(RESET_X, RESET_Y);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.reset && !c.on && !c.off);
    /* OUTPUT OFF works, and no question is asked on the way. */
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 5.0f, 1e-4f);
    tap(OUT_X, OUT_Y);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off && !c.on);
    supply_screen_set_output(false);
    /* Let go, everything takes a press again. */
    supply_screen_set_port_held(false);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 5.1f, 1e-4f);
    supply_screen_put(8.0f, 3.0f);
    CHECK_NEAR(supply_screen_set_v(), 8.0f, 1e-4f);
    hold_on();
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.on);
}

/* What was under way when the port left PWM ends with nothing asked. */
TEST_CASE(a_port_that_takes_the_supply_ends_what_was_under_way)
{
    supply_cmd_t c;

    /* A hold on OUTPUT ON. */
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    CHECK(supply_screen_output_held());
    supply_screen_set_port_held(true);
    CHECK(!supply_screen_output_held());
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));

    /* An ON posted and not collected. */
    fresh();
    hold_on();
    CHECK(supply_screen_output_live());
    supply_screen_set_port_held(true);
    CHECK(!supply_screen_output_live());
    CHECK(!supply_screen_poll_cmd(&c));

    /* A set point being typed: the keypad goes, and its keys with it. */
    fresh();
    tap(CARD_X, CARD_V_Y);
    key(UI_KEY_7);
    supply_screen_set_port_held(true);
    key(UI_KEY_OK);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    supply_screen_set_port_held(false);
    key(UI_KEY_OK);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);

    /* A drag. */
    fresh();
    ev(TRACK_X + 10, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 100, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    const float before = supply_screen_set_v();
    supply_screen_set_port_held(true);
    ev(TRACK_X + 300, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    ev(TRACK_X + 300, V_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK_NEAR(supply_screen_set_v(), before, 1e-4f);

    /* A question about a live output: APPLY applies nothing after it. */
    fresh();
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);
    supply_screen_set_port_held(true);
    tap(APPLY_X, ASK_Y);
    supply_screen_set_port_held(false);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);

    /* A settings keypad is not the supply's command and stays. */
    fresh();
    tap(SET_TAB_X, STRIP_Y);
    tap(ROW_L_X, VMAX_Y);
    supply_screen_set_port_held(true);
    keys("12");
    CHECK_NEAR(supply_screen_caps().v_max, 12.0f, 1e-4f);
}

/* Caps that come down while a port holds the supply leave the set points
 * the port's; they come under the caps when it lets go. */
TEST_CASE(caps_reach_the_set_points_when_the_port_lets_go)
{
    const supply_caps_t narrow = { 3.3f, 5.0f, 0.02f, 0.5f, 1.5f, 0.05f };

    fresh();
    supply_screen_set_port_held(true);
    supply_screen_set_caps(&narrow);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);
    supply_screen_set_port_held(false);
    CHECK_NEAR(supply_screen_set_v(), 5.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 1.5f, 1e-4f);
    /* The same flag twice changes nothing. */
    supply_screen_set_port_held(false);
    CHECK_NEAR(supply_screen_set_v(), 5.0f, 1e-4f);
}

/* The held controls are drawn as taking no press, OUTPUT OFF is not, and
 * the MODE card names the holder. */
TEST_CASE(a_held_supply_is_drawn_dimmed_and_says_who_holds_it)
{
    supply_state_t st;

    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.mode = SUPPLY_MODE_OFF;
    for (int on = 0; on < 2; ++on) {
        fresh();
        st.output = on != 0;
        st.mode = on != 0 ? SUPPLY_MODE_CV : SUPPLY_MODE_OFF;
        supply_screen_set_output(on != 0);
        supply_screen_push(&st);
        /* Past the flash of an output that came on. */
        for (int f = 0; f < 40; ++f) {
            scr->render(&cv, 0);
            scr->tick(0.02f);
        }
        supply_invalidate();
        scr->render(&cv, 0);
        supply_screen_set_port_held(true);
        scr->render(&cv2, 0);
        /* The MODE card, the steps and the tracks. */
        CHECK(differ_in(558, 207, 794, 264) > 0);
        CHECK(differ_in(V_DOWN_X - 10, V_ROW_Y - 10, V_DOWN_X + 10,
                        V_ROW_Y + 10) > 0);
        CHECK(differ_in(V_UP_X - 10, I_ROW_Y - 10, V_UP_X + 10,
                        I_ROW_Y + 10) > 0);
        CHECK(differ_in(TRACK_X, V_ROW_Y - 12, TRACK_X + TRACK_W,
                        V_ROW_Y + 12) > 0);
        /* OUTPUT: dimmed where it would switch on, as it was where it
         * switches off. */
        if (on != 0) {
            CHECK_EQ(differ_in(OUT_X - 40, OUT_Y - 10, OUT_X + 40,
                               OUT_Y + 10), 0);
        } else {
            CHECK(differ_in(OUT_X - 40, OUT_Y - 10, OUT_X + 40,
                            OUT_Y + 10) > 0);
        }
        /* RESET PEAKS is as it was. */
        CHECK_EQ(differ_in(RESET_X - 40, RESET_Y - 10, RESET_X + 40,
                           RESET_Y + 10), 0);
        /* And back. */
        supply_screen_set_port_held(false);
        scr->render(&cv2, 0);
        CHECK_EQ(differ_in(0, 0, W, H), 0);
    }
}

int main(void)
{
    RUN(a_drag_moves_the_set_point_by_the_distance_dragged);
    RUN(a_typed_cap_rounds_down_and_a_typed_trip_stays_a_trip);
    RUN(a_drag_on_a_live_output_changes_nothing_until_it_is_answered);
    RUN(the_output_switch_works_under_every_overlay);
    RUN(a_second_finger_on_a_track_changes_nothing_while_off_is_held);
    RUN(leaving_drops_the_question_and_the_overlays);
    RUN(the_home_tag_answers_supplys_question_cancel_first);
    RUN(the_home_tag_closes_a_set_point_keypad_first);
    RUN(the_home_tag_closes_a_settings_keypad_then_leaves_settings);
    RUN(the_home_tag_closes_a_question_over_the_settings_first);
    RUN(a_drag_the_home_tag_releases_opens_no_question);
    RUN(a_trip_shows_on_the_mode_card);
    RUN(a_supply_held_by_a_port_takes_off_and_no_other_press);
    RUN(a_port_that_takes_the_supply_ends_what_was_under_way);
    RUN(caps_reach_the_set_points_when_the_port_lets_go);
    RUN(a_held_supply_is_drawn_dimmed_and_says_who_holds_it);
    RUN(the_pd_minis_rate_is_shown_after_online);
    free(fb);
    free(fb2);
    return test_summary("supply_screen_home");
}
