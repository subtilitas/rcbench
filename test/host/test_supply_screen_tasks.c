/**
 * @file test_supply_screen_tasks.c
 * @brief The supply screen's footer SETTINGS and the list it opens, and its
 * question: the caps and the start values, a change to a live output, and
 * the keypads.
 *
 * Cases:
 * - settings_opens_the_list_and_a_tile_opens_live
 * - a_release_the_finger_did_not_make_opens_no_list
 * - settings_takes_the_keypad_with_it
 * - settings_opens_with_the_output_off_only
 * - a_second_finger_on_settings_opens_nothing_while_off_is_held
 * - the_source_line_stays_in_sight_under_the_list
 * - the_list_sets_a_cap_the_set_points_obey
 * - a_lowered_cap_brings_the_set_point_and_the_start_down
 * - a_switch_in_the_list_flips_on_its_key
 * - a_start_value_read_over_its_cap_is_corrected_and_kept
 * - the_set_points_start_at_the_start_values
 * - a_card_or_a_value_opens_the_keypad_for_its_set_point
 * - a_change_to_a_live_output_waits_for_the_question
 * - an_on_asked_for_counts_as_live_before_it_is_seen
 * - an_on_dropped_on_its_way_stops_counting_as_live
 * - the_question_closes_when_the_output_goes_off
 * - each_question_is_switched_on_its_own
 *
 * SPDX-License-Identifier: MIT
 */

#include "supply_screen_rig.h"

#include "ui_footer.h"
#include "ui_screen.h"

/* Pixels that differ between the two buffers inside a rectangle. */
static int differ_box(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            n += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    return n;
}

/* The list's SAVE, then the home tag's way back: LIVE again. */
static void leave_settings(void)
{
    list_save();
    CHECK(scr->back());
}

/*
 * The footer's SETTINGS opens the list; the back way closes it.  A press
 * that slides off the button, one dropped by a touch loss and one under
 * which the screen is left open nothing, and a tile's entry opens LIVE
 * whatever was shown when the screen was left.
 */
TEST_CASE(settings_opens_the_list_and_a_tile_opens_live)
{
    fresh();
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    open_settings();
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    CHECK(supply_screen_settings_shown());
    CHECK(scr->back());                       /* nothing changed: closed */
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);

    /* Off the button before the release. */
    ev(SETTINGS_X, SETTINGS_Y, TOUCH_EVENT_DOWN, 1);
    ev(SETTINGS_X, SETTINGS_Y - 120, TOUCH_EVENT_MOVE, 1);
    ev(SETTINGS_X, SETTINGS_Y - 120, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    /* Touch events lost under the press. */
    ev(SETTINGS_X, SETTINGS_Y, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    ev(SETTINGS_X, SETTINGS_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    /* Left under the press, and entered again. */
    ev(SETTINGS_X, SETTINGS_Y, TOUCH_EVENT_DOWN, 1);
    scr->leave();
    scr->enter();
    ev(SETTINGS_X, SETTINGS_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);

    /* The list open when left: the next entry is LIVE. */
    open_settings();
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    scr->leave();
    scr->enter();
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    CHECK(!supply_screen_poll_cmd(NULL));
}

/* The release the tracker makes for a contact that jumps opens no list:
 * it is not a tap. */
TEST_CASE(a_release_the_finger_did_not_make_opens_no_list)
{
    fresh();
    feed_reset();
    feed_to_screen(scr);
    finger(FEED_LONE, SETTINGS_X, SETTINGS_Y + UI_BAND_H);
    finger(FEED_LONE, OFF_X, OFF_Y + UI_BAND_H);
    CHECK_EQ(feed_ups, 1);
    lift(FEED_LONE);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    feed_tap(FEED_LONE, SETTINGS_X, SETTINGS_Y + UI_BAND_H);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
}

/*
 * SETTINGS takes an open set point's keypad with it, typed or not, as its
 * CANCEL does: the set point stays as it was.  The list's own keypad goes
 * with the back way, and the setting stays as it was.
 */
TEST_CASE(settings_takes_the_keypad_with_it)
{
    fresh();
    tap(CARD_X, CARD_V_Y);                    /* VOLTAGE's keypad, on LIVE */
    key(UI_KEY_9);
    open_settings();
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    CHECK(scr->back());
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(V_UP_X, V_ROW_Y);                     /* no keypad over the step */
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);

    open_settings();
    tap_value(SET_SUPPLY_V_MAX);              /* VOLTAGE MAX's keypad */
    CHECK_EQ(supply_screen_keypad_setting(), (int)SET_SUPPLY_V_MAX);
    list_key(UI_KEY_8);
    CHECK(scr->back());                       /* the keypad closes */
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 21.0f, 1e-4f);
    CHECK(!settings_save_asked());
    tap_value(SET_SUPPLY_V_MAX);
    list_keys("9");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 9.0f, 1e-4f);
}

/*
 * The list covers OUTPUT OFF, so SETTINGS opens it with the output off
 * only: on a live output, or with an ON on its way, the band says why it
 * does not.  An output going on under the list closes it.
 */
TEST_CASE(settings_opens_with_the_output_off_only)
{
    fresh();
    supply_screen_set_output(true);
    ui_router_set_alert(NULL);
    open_settings();
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    CHECK(ui_router_alert() != NULL);
    CHECK_STR_EQ(ui_router_alert(), TR(FOOT_SETTINGS_ON));
    supply_screen_open_limits();              /* nor from another screen */
    CHECK(!supply_screen_settings_shown());

    supply_screen_set_output(false);
    hold_on();                                /* the ON on its way */
    open_settings();
    CHECK(!supply_screen_settings_shown());
    supply_screen_cancel_on();
    drain();

    open_settings();
    CHECK(supply_screen_settings_shown());
    supply_screen_set_output(true);           /* on under the list */
    scr->tick(TICK_S);
    CHECK(!supply_screen_settings_shown());
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
}

/* One contact holds the screen: a second finger on SETTINGS while OUTPUT
 * OFF is held opens nothing, and the OFF is the first finger's. */
TEST_CASE(a_second_finger_on_settings_opens_nothing_while_off_is_held)
{
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(SETTINGS_X, SETTINGS_Y, TOUCH_EVENT_DOWN, 2);
    ev(SETTINGS_X, SETTINGS_Y, TOUCH_EVENT_UP, 2);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c) && c.off);
}

/*
 * The footer's left slot -- SUPPLY and the source under it -- is drawn the
 * same on LIVE, under the keypad and in the list's strip: where the output
 * comes from stays in sight wherever the body is.
 */
TEST_CASE(the_source_line_stays_in_sight_under_the_list)
{
    fresh();
    supply_screen_set_model(false);
    supply_screen_set_pins(12, 13);
    supply_state_t st = reading(6.0f, 1.0f);
    supply_screen_push(&st);
    const gfx_rect_t name = ui_footer_rect(UI_FOOTER_NAME);
    supply_invalidate();
    scr->render(&cv, 0);
    tap(CARD_X, CARD_V_Y);                    /* the keypad over the column */
    supply_invalidate();
    scr->render(&cv2, 0);
    CHECK_EQ(differ_box(name.x, name.y, name.x + name.w, name.y + name.h), 0);
    key(UI_KEY_CANCEL);
    open_settings();
    supply_invalidate();
    scr->render(&cv2, 0);
    CHECK(differ_box(0, 0, W, UI_FOOTER_Y) > 0);     /* the list is drawn */
    CHECK_EQ(differ_box(name.x, name.y, name.x + name.w, name.y + name.h), 0);
    /* And the line itself is there: not the background. */
    int ink = 0;
    const gfx_color_t bg = ui_theme_color(UI_C_BG);
    for (int y = name.y + 27; y < name.y + name.h; ++y) {
        for (int x = name.x; x < name.x + name.w; ++x) {
            ink += (fb2[y * W + x] != bg) ? 1 : 0;
        }
    }
    CHECK(ink > 0);
}

/* ------------------------------------------------- settings and keypad */

TEST_CASE(the_list_sets_a_cap_the_set_points_obey)
{
    fresh();
    open_settings();
    tap_value(SET_SUPPLY_V_MAX);
    list_keys("8.4");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 8.4f, 1e-4f);
    CHECK_NEAR(supply_screen_limits().v_max, 8.4f, 1e-4f);
    CHECK(!settings_save_asked());            /* SAVE writes, as on SETUP */
    list_save();
    CHECK(settings_save_asked());
    /* The set point's controls are LIVE's, under the list. */
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    CHECK(scr->back());
    for (int k = 0; k < 40; ++k) {
        tap(V_UP_X, V_ROW_Y);
    }
    CHECK_NEAR(supply_screen_set_v(), 8.4f, 1e-4f);
    tap(V_DOWN_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 8.3f, 1e-4f);
    CHECK(!supply_screen_poll_cmd(NULL));
}

/*
 * A start value takes effect at a restart; a cap lowered under the set
 * point and the start brings both down, and the keypad refuses a start
 * over the cap.
 */
TEST_CASE(a_lowered_cap_brings_the_set_point_and_the_start_down)
{
    fresh();
    open_settings();
    tap_value(SET_SUPPLY_V_START);
    list_keys("12");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 12.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);   /* at a restart only */
    leave_settings();
    tap(CARD_X, CARD_V_Y);
    keys("12");
    CHECK_NEAR(supply_screen_set_v(), 12.0f, 1e-4f);
    open_settings();
    tap_value(SET_SUPPLY_V_MAX);
    list_keys("8");
    CHECK_NEAR(supply_screen_set_v(), 8.0f, 1e-4f);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 8.0f, 1e-4f);
    tap_value(SET_SUPPLY_V_START);
    list_keys("7");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 7.0f, 1e-4f);
    /* And the keypad for a start value offers only what the cap allows:
     * 12V is refused, and the start stays where it was. */
    tap_value(SET_SUPPLY_V_START);
    list_keys("12");
    CHECK_EQ(supply_screen_keypad_setting(), (int)SET_SUPPLY_V_START);
    list_key(UI_KEY_CANCEL);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 7.0f, 1e-4f);
}

TEST_CASE(a_switch_in_the_list_flips_on_its_key)
{
    fresh();
    open_settings();
    tap_plus(SET_SUPPLY_CONFIRM_SLIDE);
    CHECK(!settings_get_bool(SET_SUPPLY_CONFIRM_SLIDE));
    CHECK(settings_get_bool(SET_SUPPLY_CONFIRM_KEYS));
    tap_plus(SET_SUPPLY_CONFIRM_KEYS);
    CHECK(!settings_get_bool(SET_SUPPLY_CONFIRM_KEYS));
    tap_plus(SET_SUPPLY_CONFIRM_SLIDE);
    CHECK(settings_get_bool(SET_SUPPLY_CONFIRM_SLIDE));
    /* A trip is typed, in amps, and 0 turns it off. */
    tap_value(SET_SUPPLY_TRIP_I);
    list_keys("2.5");
    CHECK_NEAR(supply_screen_limits().trip_i, 2.5f, 1e-4f);
    tap_value(SET_SUPPLY_TRIP_I);
    list_keys("0");
    CHECK_EQ(supply_screen_limits().trip_i, 0.0f);
}

TEST_CASE(a_start_value_read_over_its_cap_is_corrected_and_kept)
{
    /* Read back over the cap, as a save that reached one key and not the
     * other leaves them: brought down, and a save asked for. */
    fresh();
    settings_set(SET_SUPPLY_V_MAX, 8.0f);
    settings_set(SET_SUPPLY_V_START, 12.0f);
    CHECK(!settings_save_asked());
    supply_screen_settings_loaded();
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 8.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_v(), 8.0f, 1e-4f);
    CHECK(settings_save_asked());
}

TEST_CASE(the_set_points_start_at_the_start_values)
{
    fresh();
    settings_set(SET_SUPPLY_V_START, 7.5f);
    settings_set(SET_SUPPLY_I_START, 1.0f);
    settings_set(SET_SUPPLY_TRIP_MS, 250.0f);
    supply_screen_settings_loaded();
    CHECK_NEAR(supply_screen_set_v(), 7.5f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 1.0f, 1e-4f);
    CHECK_NEAR(supply_screen_limits().trip_s, 0.25f, 1e-4f);
}

TEST_CASE(a_card_or_a_value_opens_the_keypad_for_its_set_point)
{
    fresh();
    tap(CARD_X, CARD_V_Y);
    keys("7.4");
    CHECK_NEAR(supply_screen_set_v(), 7.4f, 1e-4f);
    tap(CARD_X, CARD_I_Y);
    keys("1.5");
    CHECK_NEAR(supply_screen_set_i(), 1.5f, 1e-4f);
    tap(TEXT_X, TEXT_V_Y);
    keys("9");
    CHECK_NEAR(supply_screen_set_v(), 9.0f, 1e-4f);
    tap(TEXT_X, TEXT_I_Y);
    keys("30");                         /* refused: the keypad stays */
    key(UI_KEY_CANCEL);
    CHECK_NEAR(supply_screen_set_i(), 1.5f, 1e-4f);
    /* A typed value is rounded to the supply's step. */
    tap(CARD_X, CARD_V_Y);
    keys("5.01");
    CHECK_NEAR(supply_screen_set_v(), 5.02f, 1e-4f);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_change_to_a_live_output_waits_for_the_question)
{
    fresh();
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    tap(V_UP_X, V_ROW_Y);
    tap(DISCARD_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    /* From the keypad, the same question. */
    tap(CARD_X, CARD_I_Y);
    keys("1.2");
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_i(), 1.2f, 1e-4f);
    /* A tap on the track asks on the release. */
    tap(TRACK_X + TRACK_W / 2, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    tap(APPLY_X, ASK_Y);
    CHECK(supply_screen_set_v() > 11.0f);
    /* With the output off, nothing is asked. */
    supply_screen_set_output(false);
    tap(V_DOWN_X, V_ROW_Y);
    CHECK(supply_screen_set_v() < 12.7f);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(an_on_asked_for_counts_as_live_before_it_is_seen)
{
    /* The ON has gone; the first sample to show it has not arrived.  A step
     * in between still asks. */
    fresh();
    hold_on();
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c) && c.on);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    /* Seen applied, then off: no longer live, nothing asked. */
    supply_screen_set_output(true);
    supply_screen_set_output(false);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.2f, 1e-4f);
    /* An ON a stop dropped is not waited for. */
    hold_on();
    supply_screen_cancel_on();
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.3f, 1e-4f);
    /* One collected before a touch loss is on the wire and still counts,
     * until the application drops it. */
    hold_on();
    (void)supply_screen_poll_cmd(&c);
    scr->cancel();
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.3f, 1e-4f);
    tap(DISCARD_X, ASK_Y);
    supply_screen_set_on_coming(false);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.4f, 1e-4f);
    /* Nor one a touch loss dropped before it was collected. */
    hold_on();
    scr->cancel();
    CHECK(!supply_screen_poll_cmd(&c));
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.5f, 1e-4f);
    /* Nor one an OFF overtook. */
    hold_on();
    supply_screen_set_output(true);
    supply_screen_set_output(false);
    tap(OUT_X, OUT_Y);                   /* off: nothing to switch off */
    hold_on();
    tap(OUT_X, OUT_Y);
    (void)supply_screen_poll_cmd(&c);
}

TEST_CASE(an_on_dropped_on_its_way_stops_counting_as_live)
{
    /* Handed over and then dropped by the application: told nothing is
     * coming, the screen stops asking.  One still held is not dropped. */
    fresh();
    hold_on();
    supply_screen_set_on_coming(false);     /* still held: kept */
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(DISCARD_X, ASK_Y);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c) && c.on);
    supply_screen_set_on_coming(true);      /* on its way */
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(DISCARD_X, ASK_Y);
    supply_screen_set_on_coming(false);     /* dropped */
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
}

TEST_CASE(the_question_closes_when_the_output_goes_off)
{
    fresh();
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);                    /* asked */
    supply_screen_set_output(false);         /* STOP, a trip, OFF */
    tap(APPLY_X, ASK_Y);                     /* no question to answer */
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(V_UP_X, V_ROW_Y);                    /* off: no question */
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
}

TEST_CASE(each_question_is_switched_on_its_own)
{
    fresh();
    supply_screen_set_output(true);
    settings_set(SET_SUPPLY_CONFIRM_SLIDE, 0.0f);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    tap(CARD_X, CARD_V_Y);
    keys("7");
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    tap(DISCARD_X, ASK_Y);
    settings_set(SET_SUPPLY_CONFIRM_SLIDE, 1.0f);
    settings_set(SET_SUPPLY_CONFIRM_KEYS, 0.0f);
    tap(CARD_X, CARD_V_Y);
    keys("7");
    CHECK_NEAR(supply_screen_set_v(), 7.0f, 1e-4f);
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 7.0f, 1e-4f);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 7.1f, 1e-4f);
}

int main(void)
{
    RUN(settings_opens_the_list_and_a_tile_opens_live);
    RUN(a_release_the_finger_did_not_make_opens_no_list);
    RUN(settings_takes_the_keypad_with_it);
    RUN(settings_opens_with_the_output_off_only);
    RUN(a_second_finger_on_settings_opens_nothing_while_off_is_held);
    RUN(the_source_line_stays_in_sight_under_the_list);
    RUN(the_list_sets_a_cap_the_set_points_obey);
    RUN(a_lowered_cap_brings_the_set_point_and_the_start_down);
    RUN(a_switch_in_the_list_flips_on_its_key);
    RUN(a_start_value_read_over_its_cap_is_corrected_and_kept);
    RUN(the_set_points_start_at_the_start_values);
    RUN(a_card_or_a_value_opens_the_keypad_for_its_set_point);
    RUN(a_change_to_a_live_output_waits_for_the_question);
    RUN(an_on_asked_for_counts_as_live_before_it_is_seen);
    RUN(an_on_dropped_on_its_way_stops_counting_as_live);
    RUN(the_question_closes_when_the_output_goes_off);
    RUN(each_question_is_switched_on_its_own);
    free(fb);
    free(fb2);
    return test_summary("supply_screen_tasks");
}
