/**
 * @file test_supply_screen_draw.c
 * @brief The supply screen's trace and redraws, a press that lost its release and
 * the header's pins: where a run's trace starts and stops, and that no
 * stale pixel stays in either buffer.
 *
 * Cases:
 * - reset_pd_mini_is_offered_only_for_the_module
 * - the_header_names_the_pd_minis_uart_pins
 * - the_pins_in_the_header_are_drawn_and_a_change_redraws_it
 * - the_heading_says_what_the_output_does_on_either_pane
 * - a_reading_that_did_not_arrive_is_not_plotted_as_the_last_one
 * - a_run_is_traced_from_the_switch_on_and_held_after_it
 * - a_redraw_leaves_no_stale_pixels
 * - each_framebuffer_is_updated_independently
 * - a_set_point_moved_offline_repaints_the_cards_in_both_buffers
 * - a_screen_without_a_sample_draws
 * - an_on_reported_during_a_second_hold_is_not_an_off
 * - a_question_for_an_on_that_was_dropped_goes
 * - an_apply_held_as_its_on_is_dropped_applies_nothing
 * - a_dropped_question_lets_go_of_its_apply
 * - stop_lets_go_of_supplys_apply
 * - stop_and_an_apply_tap_in_one_frame_apply_nothing_on_supply
 * - a_question_asked_after_a_stop_stands_on_supply
 * - a_press_that_lost_its_release_gives_way_to_the_next_press
 * - a_lost_release_on_output_off_is_sent_at_the_next_press
 * - an_on_posted_by_a_hold_that_lost_its_release_is_dropped
 * - a_second_contact_does_not_end_a_press_that_is_held
 * - a_release_the_tracker_makes_for_a_jump_presses_nothing_but_off
 *
 * SPDX-License-Identifier: MIT
 */

#include "supply_screen_rig.h"

#define TABLE_X   SU_TABLE_TAB_X /* the TABLE tab */

#define PLOT_TAB_X SU_PLOT_TAB_X /* the PLOT tab */

#define TABLE_Y   SU_PANE_Y

#define MODRESET_Y SU_MODRESET_Y /* RESET PD MINI, left column, SETTINGS */

/* A whole frame drawn from nothing, so two screens can be compared by their
 * pixels: the trace, the readouts and the panel's title tag. */
static void render_both(void)
{
    scr->render(&cv, 0);
    supply_invalidate();
    scr->render(&cv, 0);
}

TEST_CASE(the_heading_says_what_the_output_does_on_either_pane)
{
    /* TABLE with the output off reads as PLOT with nothing recorded does,
     * not as a live output. */
    fresh();
    tap(TABLE_X, TABLE_Y);
    supply_invalidate();
    scr->render(&cv, 0);
    supply_screen_set_output(true);
    supply_state_t st = reading(6.0f, 1.0f);
    st.output = true;
    supply_screen_push(&st);
    supply_invalidate();
    scr->render(&cv2, 0);
    /* The upper panel's title tag: x 8..130, y 56..76. */
    int differ = 0;
    for (int y = SU_TAG_Y; y < SU_TAG_Y + 20; ++y) {
        for (int x = 8; x < 130; ++x) {
            differ += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    CHECK(differ > 0);

    /* After a run: PLOT holds it, TABLE has nothing to hold and is idle --
     * so TABLE now reads as it did before any run. */
    supply_screen_set_output(false);
    supply_invalidate();
    scr->render(&cv2, 0);                /* TABLE, after the run */
    int same = 0, total = 0;
    for (int y = SU_TAG_Y; y < SU_TAG_Y + 20; ++y) {
        for (int x = 8; x < 130; ++x) {
            same += (fb[y * W + x] == fb2[y * W + x]) ? 1 : 0;
            ++total;
        }
    }
    CHECK_EQ(same, total);
    tap(PLOT_TAB_X, TABLE_Y);
    supply_invalidate();
    scr->render(&cv2, 0);                /* PLOT, holding the run */
    differ = 0;
    for (int y = SU_TAG_Y; y < SU_TAG_Y + 20; ++y) {
        for (int x = 8; x < 130; ++x) {
            differ += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    CHECK(differ > 0);
}

TEST_CASE(a_reading_that_did_not_arrive_is_not_plotted_as_the_last_one)
{
    /* Two runs alike but for the value left in a reading whose flag says it
     * did not arrive: the plot draws them the same. */
    gfx_color_t *want = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(want);
    for (int pass = 0; pass < 2; ++pass) {
        fresh();
        supply_screen_set_output(true);
        supply_state_t st = reading(6.0f, 1.5f);
        st.output = true;
        for (int k = 0; k < 20; ++k) {
            supply_screen_push(&st);
        }
        /* The readings stop arriving; one run keeps the stale numbers. */
        st.ok = 0u;
        if (pass == 1) {
            st.v = 0.0f;
            st.i = 0.0f;
            st.p = 0.0f;
        }
        for (int k = 0; k < 20; ++k) {
            supply_screen_push(&st);
        }
        supply_invalidate();
        scr->render(&cv, 0);
        if (pass == 0) {
            memcpy(want, fb, (size_t)W * H * sizeof(gfx_color_t));
        }
    }
    const int same = memcmp(fb, want, (size_t)W * H * sizeof(gfx_color_t));
    free(want);
    CHECK_EQ(same, 0);
}

TEST_CASE(a_run_is_traced_from_the_switch_on_and_held_after_it)
{
    fresh();
    supply_state_t st = reading(6.0f, 1.0f);
    supply_screen_push(&st);               /* off: no trace */
    render_both();

    /* The reference: a screen that only ever saw the run. */
    supply_screen_set_output(true);
    for (int k = 0; k < 30; ++k) {
        st = reading(6.0f, 1.0f + 0.02f * (float)k);
        st.output = true;
        supply_screen_push(&st);
    }
    supply_screen_set_output(false);
    st = reading(0.0f, 0.0f);
    supply_screen_push(&st);              /* after the run: readouts only */
    render_both();
    gfx_color_t *want = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(want);
    memcpy(want, fb, (size_t)W * H * sizeof(gfx_color_t));

    /* The same run with off-time samples before it on a used screen. */
    fresh();
    for (int k = 0; k < 50; ++k) {
        st = reading(9.0f, 3.0f);
        supply_screen_push(&st);
    }
    supply_screen_set_output(true);        /* a run before it, */
    for (int k = 0; k < 40; ++k) {
        st = reading(9.0f, 3.0f);
        st.output = true;
        supply_screen_push(&st);
    }
    supply_screen_set_output(false);       /* held, */
    supply_screen_set_output(true);        /* and cleared by the next */
    for (int k = 0; k < 30; ++k) {
        st = reading(6.0f, 1.0f + 0.02f * (float)k);
        st.output = true;
        supply_screen_push(&st);
    }
    supply_screen_set_output(false);
    st = reading(0.0f, 0.0f);
    supply_screen_push(&st);
    render_both();
    const int same = memcmp(fb, want, (size_t)W * H * sizeof(gfx_color_t));
    free(want);
    CHECK_EQ(same, 0);
}

/* ---------------------------------------------------------------- redraw */

/*
 * Drawing state B over state A must give the pixels B gives on a buffer that
 * never saw A; with two framebuffers, anything left over shows as flicker.
 */
static void check_redraw(void (*to_b)(void))
{
    fresh();
    supply_state_t st = reading(6.0f, 1.2f);
    supply_screen_push(&st);
    scr->render(&cv, 0);
    to_b();
    for (int k = 0; k < 8; ++k) {          /* past any flash */
        scr->render(&cv, 0);
    }
    supply_invalidate();
    scr->render(&cv2, 0);
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);
}

static void b_output_on(void)
{
    supply_screen_set_output(true);
    supply_state_t st = reading(5.95f, 1.8f);
    st.output = true;
    st.mode = SUPPLY_MODE_CC;
    supply_screen_push(&st);
}

static void b_set_point(void) { tap(V_UP_X, V_ROW_Y); drain(); }

static void b_table(void)     { tap(TABLE_X, TABLE_Y); }

static void b_offline(void)
{
    supply_state_t st = reading(0.0f, 0.0f);
    st.online = false;
    st.ok = 0u;
    supply_screen_set_model(true);
    supply_screen_push(&st);
}

static void b_table_then_set(void)
{
    tap(TABLE_X, TABLE_Y);
    scr->render(&cv, 0);
    tap(V_DOWN_X, I_ROW_Y);
    drain();
}

static void b_settings(void) { tap(SET_TAB_X, STRIP_Y); }

static void b_keypad(void)   { tap(CARD_X, CARD_V_Y); key(UI_KEY_7); }

static void b_question(void)
{
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);
}

static void b_question_new_caps(void)
{
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);
    scr->render(&cv, 0);
    const supply_caps_t caps = { 9.0f, 21.0f, 0.02f, 0.5f, 5.0f, 0.05f };
    supply_screen_set_caps(&caps);
}

static void b_closed(void)
{
    tap(SET_TAB_X, STRIP_Y);
    scr->render(&cv, 0);
    tap(LIVE_TAB_X, STRIP_Y);
}

static void b_settings_keypad(void)
{
    tap(SET_TAB_X, STRIP_Y);
    scr->render(&cv, 0);
    tap(ROW_L_X, VMAX_Y);
    key(UI_KEY_7);
}

static void b_keypad_to_settings(void)
{
    tap(CARD_X, CARD_V_Y);
    key(UI_KEY_7);
    scr->render(&cv, 0);
    tap(SET_TAB_X, STRIP_Y);
}

static void b_settings_question(void)
{
    supply_screen_set_output(true);
    tap(SET_TAB_X, STRIP_Y);
    scr->render(&cv, 0);
    tap(CARD_X, CARD_V_Y);
    keys("7");
}

static void b_hold_half(void)
{
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
}

TEST_CASE(a_redraw_leaves_no_stale_pixels)
{
    check_redraw(b_output_on);
    check_redraw(b_set_point);
    check_redraw(b_table);
    check_redraw(b_offline);
    check_redraw(b_table_then_set);
    check_redraw(b_hold_half);
    check_redraw(b_settings);
    check_redraw(b_keypad);
    check_redraw(b_question);
    check_redraw(b_closed);
    check_redraw(b_settings_keypad);
    check_redraw(b_keypad_to_settings);
    check_redraw(b_settings_question);
    check_redraw(b_question_new_caps);
}

TEST_CASE(a_set_point_moved_offline_repaints_the_cards_in_both_buffers)
{
    /* Offline, the cards' brackets show the screen's own set point; a drag
     * that moves it repaints them in each buffer, not only in the one that
     * also took a sample. */
    fresh();
    supply_state_t st = reading(0.0f, 0.0f);
    st.online = false;
    st.ok = 0u;
    ev(TRACK_X + 100, V_ROW_Y, TOUCH_EVENT_DOWN, 1);
    scr->render(&cv, 0);
    scr->render(&cv2, 1);                /* both buffers hold the press */
    supply_screen_push(&st);
    scr->render(&cv, 0);                 /* A takes the sample */
    ev(TRACK_X + 160, V_ROW_Y, TOUCH_EVENT_MOVE, 1);
    scr->render(&cv2, 1);                /* B: the sample and the move */
    scr->render(&cv, 0);                 /* A: the move alone */
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);
    ev(TRACK_X + 160, V_ROW_Y, TOUCH_EVENT_UP, 1);
}

TEST_CASE(each_framebuffer_is_updated_independently)
{
    fresh();
    supply_state_t st = reading(6.0f, 1.0f);
    supply_screen_push(&st);
    scr->render(&cv, 0);
    scr->render(&cv2, 1);
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* A run, a set point and the switch, painted into buffer 1 first. */
    b_output_on();
    tap(V_UP_X, V_ROW_Y);
    drain();
    for (int k = 0; k < 8; ++k) {
        scr->render(&cv2, 1);
        scr->render(&cv, 0);
    }
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* A frame with nothing new leaves a correct buffer correct. */
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);
}

TEST_CASE(a_screen_without_a_sample_draws)
{
    /* No sample yet, no caps from a driver: the screen draws the model's
     * defaults and nothing reads uninitialised. */
    fresh();
    scr->render(&cv, 0);
    supply_screen_push(NULL);
    scr->event(NULL);
    CHECK(!supply_screen_poll_cmd(NULL));
}

/* SETTINGS offers RESET PD MINI while the PD mini is the supply, and not
 * while the panel runs its model; it asks for the restart with the output
 * off. */
TEST_CASE(reset_pd_mini_is_offered_only_for_the_module)
{
    fresh();
    supply_screen_set_model(true);
    tap(SET_TAB_X, STRIP_Y);
    tap(ROW_L_X, MODRESET_Y);
    supply_cmd_t c;
    CHECK(!supply_screen_poll_cmd(&c));
    tap(LIVE_TAB_X, STRIP_Y);                /* LIVE again */

    supply_screen_set_model(false);
    tap(SET_TAB_X, STRIP_Y);
    tap(ROW_L_X, MODRESET_Y);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.module_reset);
    CHECK(c.off);
    CHECK(!c.on);
}

/* A second hold on OUTPUT ON while the first one's ON is on its way: the
 * output reporting on during it does not make its release an OFF. */
TEST_CASE(an_on_reported_during_a_second_hold_is_not_an_off)
{
    fresh();
    hold_on();
    drain();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(4);
    supply_screen_set_output(true);
    tick_for(1);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    supply_cmd_t c;
    CHECK(!supply_screen_poll_cmd(&c) || !c.off);
}

/* The question asked while an ON was on its way goes when that ON is
 * dropped -- a STOP -- though the output never reported on. */
TEST_CASE(a_question_for_an_on_that_was_dropped_goes)
{
    fresh();
    hold_on();
    const float was = supply_screen_set_v();
    tap(CARD_X, CARD_V_Y);
    keys("7");
    CHECK_EQ(supply_screen_set_v(), was);       /* waiting for APPLY */
    supply_screen_cancel_on();
    tick_for(1);
    tap(APPLY_X, ASK_Y);                        /* nothing there now */
    CHECK_EQ(supply_screen_set_v(), was);
}

/* APPLY pressed on a question about a coming ON, then STOP drops the ON
 * before the finger lifts: the release applies nothing. */
TEST_CASE(an_apply_held_as_its_on_is_dropped_applies_nothing)
{
    fresh();
    hold_on();
    const float was = supply_screen_set_v();
    tap(CARD_X, CARD_V_Y);
    keys("7");
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_DOWN, 1);
    supply_screen_cancel_on();
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_set_v(), was);
}

/* The same, with the frame's tick between: the question goes, and takes
 * the press on its APPLY with it. */
TEST_CASE(a_dropped_question_lets_go_of_its_apply)
{
    fresh();
    hold_on();
    const float was = supply_screen_set_v();
    tap(CARD_X, CARD_V_Y);
    keys("7");
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_DOWN, 1);
    supply_screen_cancel_on();
    tick_for(1);
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_set_v(), was);
}

/* The same on SUPPLY with the output already on: STOP lets go of APPLY. */
TEST_CASE(stop_lets_go_of_supplys_apply)
{
    fresh();
    supply_screen_set_output(true);
    const float was = supply_screen_set_v();
    tap(CARD_X, CARD_V_Y);
    keys("7");
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_DOWN, 1);
    supply_screen_cancel_on();
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_set_v(), was);
}

/* STOP and a whole APPLY tap in one frame on SUPPLY: the stop is seen
 * first, while the output still reports on.  The tap applies nothing, and
 * the question has gone by the frame's tick. */
TEST_CASE(stop_and_an_apply_tap_in_one_frame_apply_nothing_on_supply)
{
    fresh();
    supply_screen_set_output(true);
    const float was = supply_screen_set_v();
    tap(CARD_X, CARD_V_Y);
    keys("7");
    supply_screen_cancel_on();
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_DOWN, 1);
    ev(APPLY_X, ASK_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_set_v(), was);
    tick_for(1);
    tap(APPLY_X, ASK_Y);                        /* nothing there now */
    CHECK_EQ(supply_screen_set_v(), was);
}

/* A question asked after a stop, the output still on, stands. */
TEST_CASE(a_question_asked_after_a_stop_stands_on_supply)
{
    fresh();
    supply_screen_set_output(true);
    supply_screen_cancel_on();
    tick_for(1);
    tap(CARD_X, CARD_V_Y);
    keys("7");
    tap(APPLY_X, ASK_Y);
    CHECK_EQ(supply_screen_set_v(), 7.0f);
}

/* ------------------------------------------- a press that lost its release */

/*
 * The cases below feed the screen frames of contacts through the tracker
 * (touch_feed.h), with no router in front of it.  The feed works in panel
 * coordinates and removes the band's height, as the router does.
 */
#define P(y) ((y) + UI_BAND_H)

static void fed(void)
{
    fresh();
    feed_reset();
    feed_to_screen(scr);
}

/*
 * One contact holds the screen, and a contact that has gone cannot.  A drag
 * on the voltage track loses its release, with no loss told.  The next
 * press carries the same track id: it says the first contact is over, so
 * the screen takes it.  A hold on OUTPUT ON switches on at the set point the
 * supply has, and the lift moves no set point.
 */
TEST_CASE(a_press_that_lost_its_release_gives_way_to_the_next_press)
{
    fed();
    finger(FEED_LONE, TRACK_X + 60, P(V_ROW_Y));
    glide(FEED_LONE, TRACK_X + 80, P(V_ROW_Y), 8);
    const float set = supply_screen_set_v();
    feed_lose_next(1);
    lift(FEED_LONE);
    drain();

    finger(FEED_LONE, OUT_X, P(OUT_Y));
    tick_for(HOLD_TICKS);
    supply_cmd_t c = { 0 };
    const bool posted = supply_screen_poll_cmd(&c);
    lift(FEED_LONE);
    CHECK(posted && c.on);
    CHECK_NEAR(supply_screen_set_v(), set, 0.001f);
    CHECK(!supply_screen_poll_cmd(NULL));
}

/* On a live output the press that went missing was a tap on OUTPUT OFF: an
 * OFF the operator made, sent when the next press shows it is over. */
TEST_CASE(a_lost_release_on_output_off_is_sent_at_the_next_press)
{
    fed();
    supply_screen_set_output(true);
    drain();
    finger(FEED_LONE, OUT_X, P(OUT_Y));
    feed_lose_next(1);
    lift(FEED_LONE);
    CHECK(!supply_screen_poll_cmd(NULL));

    finger(FEED_LONE, OFF_X, P(OFF_Y));
    supply_cmd_t c = { 0 };
    const bool posted = supply_screen_poll_cmd(&c);
    lift(FEED_LONE);
    CHECK(posted && c.off);
}

/*
 * A hold on OUTPUT ON loses its release and completes on its timer with
 * nothing on the glass.  The ON it posted is still waiting when the next
 * press shows the contact had gone, and it is dropped with the press.
 */
TEST_CASE(an_on_posted_by_a_hold_that_lost_its_release_is_dropped)
{
    fed();
    finger(FEED_LONE, OUT_X, P(OUT_Y));
    tick_for(HOLD_TICKS / 2);
    feed_lose_next(1);
    lift(FEED_LONE);
    tick_for(HOLD_TICKS);

    finger(FEED_LONE, OFF_X, P(OFF_Y));
    CHECK(!supply_screen_poll_cmd(NULL));
    lift(FEED_LONE);
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(NULL));

    /* A hold that is held switches on. */
    finger(FEED_LONE, OUT_X, P(OUT_Y));
    tick_for(HOLD_TICKS);
    supply_cmd_t c = { 0 };
    CHECK(supply_screen_poll_cmd(&c) && c.on);
    lift(FEED_LONE);
}

/* A second finger, with a track id of its own, still cannot take the screen
 * from a press that is being held. */
TEST_CASE(a_second_contact_does_not_end_a_press_that_is_held)
{
    fed();
    finger(0, TRACK_X + 60, P(V_ROW_Y));
    glide(0, TRACK_X + 80, P(V_ROW_Y), 8);
    finger(1, OUT_X, P(OUT_Y));
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(NULL));
    lift(1);
    const float before = supply_screen_set_v();
    glide(0, TRACK_X + 120, P(V_ROW_Y), 8);
    lift(0);
    CHECK(supply_screen_set_v() > before);
}

/*
 * A contact the tracker ends for a jump: its release is on the control it
 * pressed and the finger did not make it.  RESET PEAKS, the SETTINGS button
 * and a settings row are not pressed.  OUTPUT OFF is: switching off takes
 * a release however it came about.
 */
TEST_CASE(a_release_the_tracker_makes_for_a_jump_presses_nothing_but_off)
{
    fed();
    supply_cmd_t c;
    finger(FEED_LONE, RESET_X, P(RESET_Y));
    finger(FEED_LONE, OFF_X, P(OFF_Y));
    CHECK_EQ(feed_ups, 1);                    /* the tracker's release */
    lift(FEED_LONE);
    CHECK(!supply_screen_poll_cmd(NULL));
    feed_tap(FEED_LONE, RESET_X, P(RESET_Y));
    CHECK(supply_screen_poll_cmd(&c) && c.reset);

    /* SETTINGS: not switched to, so the plot's controls still answer. */
    finger(FEED_LONE, SET_TAB_X, P(STRIP_Y));
    finger(FEED_LONE, OFF_X, P(OFF_Y));
    lift(FEED_LONE);
    feed_tap(FEED_LONE, RESET_X, P(RESET_Y));
    CHECK(supply_screen_poll_cmd(&c) && c.reset);

    /* A switch among the settings: not flipped by the tracker's release.
     * The contact is not lifted where it is next reported. */
    feed_tap(FEED_LONE, SET_TAB_X, P(STRIP_Y));
    const bool was = settings_get_bool(SET_SUPPLY_CONFIRM_SLIDE);
    finger(FEED_LONE, ROW_R_X, P(CONF_SL_Y));
    finger(FEED_LONE, ROW_R_X, P(CONF_SL_Y) - 200);
    CHECK_EQ(feed_ups, 8);                    /* the last, the tracker's */
    CHECK_EQ(settings_get_bool(SET_SUPPLY_CONFIRM_SLIDE), was);
    scr->cancel();
    feed_reset();
    feed_to_screen(scr);
    feed_tap(FEED_LONE, ROW_R_X, P(CONF_SL_Y));
    CHECK_EQ(settings_get_bool(SET_SUPPLY_CONFIRM_SLIDE), !was);
    settings_cancel_save();

    /* OUTPUT OFF. */
    fed();
    supply_screen_set_output(true);
    finger(FEED_LONE, OUT_X, P(OUT_Y));
    finger(FEED_LONE, OFF_X, P(OFF_Y));
    CHECK(supply_screen_poll_cmd(&c) && c.off);
    lift(FEED_LONE);
}

/*
 * The header names the PD mini's UART pins, so a second supply on other pins
 * would read differently.  No pin set, or only one, says so; the model has
 * none.  A change redraws the header and nothing else.  The row holds
 * 680px: the range takes 23 cells of the 8px face at its right end
 * (184px), so the text before it keeps to 47 cells of the 10px face.
 */
TEST_CASE(the_header_names_the_pd_minis_uart_pins)
{
    char line[80];
    fresh();
    supply_screen_set_model(false);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    supply_screen_push(&st);
    supply_screen_set_baud(460800u);

    ui_text_set_language(UI_LANG_EN);
    supply_screen_set_pins(12, 13);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "PD MINI TX GPIO 12 RX GPIO 13   ONLINE 460800");
    CHECK(strlen(line) <= 47u);
    supply_screen_set_pins(0, 1);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "PD MINI TX GPIO 0 RX GPIO 1   ONLINE 460800");
    supply_screen_set_pins(47, 46);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "PD MINI TX GPIO 47 RX GPIO 46   ONLINE 460800");

    /* Not set, or one of the two not set. */
    supply_screen_set_pins(-1, -1);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "PD MINI NO PINS   ONLINE 460800");
    supply_screen_set_pins(12, -1);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "PD MINI NO PINS   ONLINE 460800");
    /* One GPIO for both, a stored set-up the page is written disabled
     * with: no pins either, at both ends of the range. */
    supply_screen_set_pins(18, 18);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "PD MINI NO PINS   ONLINE 460800");
    supply_screen_set_pins(0, 0);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "PD MINI NO PINS   ONLINE 460800");
    supply_screen_set_pins(47, 47);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "PD MINI NO PINS   ONLINE 460800");

    /* Not answering, and the German text, the longest header there is. */
    supply_screen_set_pins(12, 13);
    st.online = false;
    supply_screen_push(&st);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "PD MINI TX GPIO 12 RX GPIO 13   NOT ANSWERING");
    ui_text_set_language(UI_LANG_DE);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "PD MINI TX GPIO 12 RX GPIO 13   ANTWORTET NICHT");
    CHECK(strlen(line) <= 47u);
    supply_screen_set_pins(-1, -1);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "PD MINI OHNE PINS   ANTWORTET NICHT");

    /* The model names no pin, whatever the settings hold. */
    ui_text_set_language(UI_LANG_EN);
    supply_screen_set_pins(12, 13);
    supply_screen_set_model(true);
    st.online = true;
    supply_screen_push(&st);
    supply_screen_header_text(line, sizeof(line));
    CHECK_STR_EQ(line, "SIMULATED: SWITCH ON IN PD MINI   ONLINE");
}

TEST_CASE(the_pins_in_the_header_are_drawn_and_a_change_redraws_it)
{
    fresh();
    supply_screen_set_model(false);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    supply_screen_push(&st);
    supply_screen_set_pins(12, 13);
    scr->render(&cv, 0);
    memcpy(fb2, fb, (size_t)W * H * sizeof(gfx_color_t));

    /* The same pins again change nothing. */
    supply_screen_set_pins(12, 13);
    scr->render(&cv, 0);
    CHECK_EQ(header_differs(), 0);

    /* Other pins redraw the header. */
    supply_screen_set_pins(4, 5);
    scr->render(&cv, 0);
    CHECK(header_differs() > 0);

    /* So does taking them away. */
    memcpy(fb2, fb, (size_t)W * H * sizeof(gfx_color_t));
    supply_screen_set_pins(-1, -1);
    scr->render(&cv, 0);
    CHECK(header_differs() > 0);
}

int main(void)
{
    RUN(reset_pd_mini_is_offered_only_for_the_module);
    RUN(the_header_names_the_pd_minis_uart_pins);
    RUN(the_pins_in_the_header_are_drawn_and_a_change_redraws_it);
    RUN(the_heading_says_what_the_output_does_on_either_pane);
    RUN(a_reading_that_did_not_arrive_is_not_plotted_as_the_last_one);
    RUN(a_run_is_traced_from_the_switch_on_and_held_after_it);
    RUN(a_redraw_leaves_no_stale_pixels);
    RUN(each_framebuffer_is_updated_independently);
    RUN(a_set_point_moved_offline_repaints_the_cards_in_both_buffers);
    RUN(a_screen_without_a_sample_draws);
    RUN(an_on_reported_during_a_second_hold_is_not_an_off);
    RUN(a_question_for_an_on_that_was_dropped_goes);
    RUN(an_apply_held_as_its_on_is_dropped_applies_nothing);
    RUN(a_dropped_question_lets_go_of_its_apply);
    RUN(stop_lets_go_of_supplys_apply);
    RUN(stop_and_an_apply_tap_in_one_frame_apply_nothing_on_supply);
    RUN(a_question_asked_after_a_stop_stands_on_supply);
    RUN(a_press_that_lost_its_release_gives_way_to_the_next_press);
    RUN(a_lost_release_on_output_off_is_sent_at_the_next_press);
    RUN(an_on_posted_by_a_hold_that_lost_its_release_is_dropped);
    RUN(a_second_contact_does_not_end_a_press_that_is_held);
    RUN(a_release_the_tracker_makes_for_a_jump_presses_nothing_but_off);
    free(fb);
    free(fb2);
    return test_summary("supply_screen_draw");
}
