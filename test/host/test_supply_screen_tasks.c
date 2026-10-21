/**
 * @file test_supply_screen_tasks.c
 * @brief The supply screen's task strip, its settings page and its question: the
 * caps and the start values, a change to a live output, and the keypads.
 *
 * Cases:
 * - the_strip_switches_the_task_and_a_tile_opens_live
 * - a_release_the_finger_did_not_make_switches_no_task
 * - a_tab_switch_takes_the_keypad_with_it
 * - a_question_on_settings_holds_the_strip_until_answered
 * - a_second_finger_on_the_strip_switches_nothing_while_off_is_held
 * - the_source_line_stays_in_sight_on_every_task
 * - the_settings_page_sets_a_cap_the_set_points_obey
 * - a_lowered_cap_brings_the_set_point_and_the_start_down
 * - a_switch_in_the_settings_flips_on_a_tap
 * - a_start_value_read_over_its_cap_is_corrected_and_kept
 * - the_page_tells_an_unsaved_setup_change_from_a_waiting_save
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

#define VSTART_Y  212

#define CONF_KP_Y 360

/* ------------------------------------------------------- the task strip */

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

/*
 * LIVE | SETTINGS: a tap on the other tab switches, a tap on the tab shown
 * changes nothing, and a tile's entry opens LIVE whatever was shown when
 * the screen was left.  A press that slides off its tab, one dropped by a
 * touch loss and one whose release the finger did not make switch nothing.
 */
TEST_CASE(the_strip_switches_the_task_and_a_tile_opens_live)
{
    fresh();
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    tap(LIVE_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    tap(SET_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    tap(SET_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    tap(LIVE_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);

    /* Off the tab before the release. */
    ev(SET_TAB_X, STRIP_Y, TOUCH_EVENT_DOWN, 1);
    ev(SET_TAB_X, STRIP_Y + 120, TOUCH_EVENT_MOVE, 1);
    ev(SET_TAB_X, STRIP_Y + 120, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    /* Touch events lost under the press. */
    ev(SET_TAB_X, STRIP_Y, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    ev(SET_TAB_X, STRIP_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    /* Left under the press, and entered again. */
    ev(SET_TAB_X, STRIP_Y, TOUCH_EVENT_DOWN, 1);
    scr->leave();
    scr->enter();
    ev(SET_TAB_X, STRIP_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);

    /* SETTINGS when left: the next entry is LIVE. */
    tap(SET_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    scr->leave();
    scr->enter();
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    CHECK(!supply_screen_poll_cmd(NULL));
}

/* The release the tracker makes for a contact that jumps switches no task:
 * it is not a tap. */
TEST_CASE(a_release_the_finger_did_not_make_switches_no_task)
{
    fresh();
    feed_reset();
    feed_to_screen(scr);
    finger(FEED_LONE, SET_TAB_X, STRIP_Y + UI_BAND_H);
    finger(FEED_LONE, OFF_X, OFF_Y + UI_BAND_H);
    CHECK_EQ(feed_ups, 1);
    lift(FEED_LONE);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    feed_tap(FEED_LONE, SET_TAB_X, STRIP_Y + UI_BAND_H);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
}

/*
 * A tap on the other tab takes an open keypad with it, typed or not, as its
 * CANCEL does: the set point or the setting stays as it was.  The keypad is
 * the task's it was opened on; the task shown is the tab's.
 */
TEST_CASE(a_tab_switch_takes_the_keypad_with_it)
{
    fresh();
    tap(CARD_X, CARD_V_Y);                    /* VOLTAGE's keypad, on LIVE */
    key(UI_KEY_9);
    tap(SET_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    tap(LIVE_TAB_X, STRIP_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(V_UP_X, V_ROW_Y);                     /* no keypad over the step */
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);

    tap(SET_TAB_X, STRIP_Y);
    tap(ROW_L_X, VMAX_Y);                     /* VOLTAGE MAX's, on SETTINGS */
    key(UI_KEY_8);
    tap(LIVE_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 21.0f, 1e-4f);
    CHECK(!settings_save_asked());
    tap(SET_TAB_X, STRIP_Y);                  /* the page, no keypad on it */
    tap(ROW_L_X, VMAX_Y);
    keys("9");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 9.0f, 1e-4f);

    /* A tap on the tab shown keeps the keypad and what it holds. */
    tap(LIVE_TAB_X, STRIP_Y);
    tap(CARD_X, CARD_V_Y);
    key(UI_KEY_7);
    tap(LIVE_TAB_X, STRIP_Y);
    key(UI_KEY_OK);
    CHECK_NEAR(supply_screen_set_v(), 7.0f, 1e-4f);
}

/*
 * A card's keypad works on SETTINGS too, and a change to a live output asks
 * there as on LIVE.  The strip waits for the answer; once answered the page
 * is back.
 */
TEST_CASE(a_question_on_settings_holds_the_strip_until_answered)
{
    fresh();
    supply_screen_set_output(true);
    tap(SET_TAB_X, STRIP_Y);
    tap(CARD_X, CARD_V_Y);
    keys("7");                                /* asked: the output is on */
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(LIVE_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    tap(APPLY_X, ASK_Y);
    CHECK_NEAR(supply_screen_set_v(), 7.0f, 1e-4f);
    /* The page answers again. */
    const bool was = settings_get_bool(SET_SUPPLY_CONFIRM_SLIDE);
    tap(ROW_R_X, CONF_SL_Y);
    CHECK_EQ(settings_get_bool(SET_SUPPLY_CONFIRM_SLIDE), !was);
    tap(LIVE_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    CHECK(supply_screen_output_on());
    CHECK(!supply_screen_poll_cmd(NULL));
}

/* One contact holds the screen: a second finger on the strip while OUTPUT
 * OFF is held switches nothing, and the OFF is the first finger's. */
TEST_CASE(a_second_finger_on_the_strip_switches_nothing_while_off_is_held)
{
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(SET_TAB_X, STRIP_Y, TOUCH_EVENT_DOWN, 2);
    ev(SET_TAB_X, STRIP_Y, TOUCH_EVENT_UP, 2);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c) && c.off);
}

/*
 * The line right of the strip -- the source, ONLINE or NOT ANSWERING, the
 * range -- is drawn the same on LIVE, on SETTINGS and under the keypad: it
 * says where the numbers come from wherever the left column is.  The strip
 * shows the task.
 */
TEST_CASE(the_source_line_stays_in_sight_on_every_task)
{
    fresh();
    supply_screen_set_model(false);
    supply_screen_set_pins(12, 13);
    supply_state_t st = reading(6.0f, 1.0f);
    supply_screen_push(&st);
    supply_invalidate();
    scr->render(&cv, 0);
    tap(SET_TAB_X, STRIP_Y);
    supply_invalidate();
    scr->render(&cv2, 0);
    CHECK_EQ(header_differs(), 0);
    CHECK(differ_box(6, 3, 240, 34) > 0);     /* the strip says SETTINGS */
    tap(ROW_L_X, VMAX_Y);                     /* the keypad over the page */
    supply_invalidate();
    scr->render(&cv2, 0);
    CHECK_EQ(header_differs(), 0);
    CHECK(differ_box(6, 3, 240, 34) > 0);
    /* And the line itself is there: not the background. */
    int ink = 0;
    const gfx_color_t bg = ui_theme_color(UI_C_BG);
    for (int y = 0; y < 34; ++y) {
        for (int x = 252; x < 600; ++x) {
            ink += (fb2[y * W + x] != bg) ? 1 : 0;
        }
    }
    CHECK(ink > 0);
}

/* ------------------------------------------------- settings and keypad */

TEST_CASE(the_settings_page_sets_a_cap_the_set_points_obey)
{
    fresh();
    tap(SET_TAB_X, STRIP_Y);             /* SETTINGS */
    tap(ROW_L_X, VMAX_Y);                /* VOLTAGE MAX */
    keys("8.4");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 8.4f, 1e-4f);
    CHECK_NEAR(supply_screen_limits().v_max, 8.4f, 1e-4f);
    CHECK(settings_save_asked());
    /* The set point's controls are LIVE's, out of sight on SETTINGS. */
    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    tap(LIVE_TAB_X, STRIP_Y);
    for (int k = 0; k < 40; ++k) {
        tap(V_UP_X, V_ROW_Y);
    }
    CHECK_NEAR(supply_screen_set_v(), 8.4f, 1e-4f);
    /* A tap on the tab shown changes nothing; the other tab switches. */
    tap(LIVE_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    tap(SET_TAB_X, STRIP_Y);
    tap(SET_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    tap(LIVE_TAB_X, STRIP_Y);
    tap(V_DOWN_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 8.3f, 1e-4f);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_lowered_cap_brings_the_set_point_and_the_start_down)
{
    fresh();
    tap(SET_TAB_X, STRIP_Y);
    tap(ROW_L_X, VSTART_Y);
    keys("12");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 12.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);   /* at a restart only */
    tap(LIVE_TAB_X, STRIP_Y);
    tap(CARD_X, CARD_V_Y);
    keys("12");
    CHECK_NEAR(supply_screen_set_v(), 12.0f, 1e-4f);
    tap(SET_TAB_X, STRIP_Y);
    tap(ROW_L_X, VMAX_Y);
    keys("8");
    CHECK_NEAR(supply_screen_set_v(), 8.0f, 1e-4f);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 8.0f, 1e-4f);
    /* And the keypad for a start value offers only what the cap allows:
     * 12 V is refused, and the start stays where it was. */
    tap(ROW_L_X, VSTART_Y);
    keys("7");
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 7.0f, 1e-4f);
    tap(ROW_L_X, VSTART_Y);
    keys("12");
    key(UI_KEY_CANCEL);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 7.0f, 1e-4f);
}

TEST_CASE(a_switch_in_the_settings_flips_on_a_tap)
{
    fresh();
    tap(SET_TAB_X, STRIP_Y);
    tap(ROW_R_X, CONF_SL_Y);
    CHECK(!settings_get_bool(SET_SUPPLY_CONFIRM_SLIDE));
    CHECK(settings_get_bool(SET_SUPPLY_CONFIRM_KEYS));
    tap(ROW_R_X, CONF_KP_Y);
    CHECK(!settings_get_bool(SET_SUPPLY_CONFIRM_KEYS));
    tap(ROW_R_X, CONF_SL_Y);
    CHECK(settings_get_bool(SET_SUPPLY_CONFIRM_SLIDE));
    /* A trip is typed, in amps, and 0 turns it off. */
    tap(ROW_R_X, TRIPI_Y);
    keys("2.5");
    CHECK_NEAR(supply_screen_limits().trip_i, 2.5f, 1e-4f);
    tap(ROW_R_X, TRIPI_Y);
    keys("0");
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

TEST_CASE(the_page_tells_an_unsaved_setup_change_from_a_waiting_save)
{
    /* A change left on SETUP without SAVE is not a save waiting: nothing
     * will write it.  The overlay's bottom line says which. */
    fresh();
    tap(SET_TAB_X, STRIP_Y);
    settings_set(SET_PACK_CELLS, 4.0f);          /* SETUP's, no save asked */
    supply_invalidate();
    scr->render(&cv, 0);
    settings_request_save();
    supply_invalidate();
    scr->render(&cv2, 0);
    /* The bottom line: x 16..300, y 400..420. */
    int differ = 0;
    for (int y = 400; y < 420; ++y) {
        for (int x = 16; x < 300; ++x) {
            differ += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    CHECK(differ > 0);
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
    RUN(the_strip_switches_the_task_and_a_tile_opens_live);
    RUN(a_release_the_finger_did_not_make_switches_no_task);
    RUN(a_tab_switch_takes_the_keypad_with_it);
    RUN(a_question_on_settings_holds_the_strip_until_answered);
    RUN(a_second_finger_on_the_strip_switches_nothing_while_off_is_held);
    RUN(the_source_line_stays_in_sight_on_every_task);
    RUN(the_settings_page_sets_a_cap_the_set_points_obey);
    RUN(a_lowered_cap_brings_the_set_point_and_the_start_down);
    RUN(a_switch_in_the_settings_flips_on_a_tap);
    RUN(a_start_value_read_over_its_cap_is_corrected_and_kept);
    RUN(the_page_tells_an_unsaved_setup_change_from_a_waiting_save);
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
