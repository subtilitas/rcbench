/**
 * @file test_supply_screen.c
 * @brief What the supply screen decides: the switch's hold and tap, the set points
 * and their snapping, and what a stop, a touch loss and a leave leave
 * behind.  The task strip, the settings, the home tag and the redraws are in
 * test_supply_screen_*.c.
 *
 * Not what it looks like; that is the golden image's job.
 *
 * Cases:
 * - the_output_comes_on_with_a_hold_and_only_once
 * - a_short_press_on_output_on_does_nothing
 * - a_finger_that_leaves_the_switch_switches_nothing
 * - the_output_goes_off_with_a_tap
 * - the_release_that_ends_the_hold_does_not_switch_off
 * - an_off_outranks_an_on_in_the_same_poll
 * - a_second_contact_cannot_steal_the_release
 * - a_second_contact_on_another_button_cannot_steal_the_off
 * - a_stop_abandons_the_hold_and_drops_a_posted_on
 * - a_stop_while_the_output_reports_off_strands_no_press
 * - a_touch_loss_drops_an_on_and_keeps_an_off
 * - leaving_under_a_press_on_output_off_sends_the_off
 * - leaving_keeps_an_output_it_did_not_switch_on_and_drops_the_hold
 * - leaving_switches_off_the_output_this_screen_switched_on
 * - the_overlays_of_supply_are_no_leave
 * - a_leave_switches_off_only_the_screens_own_output
 * - an_output_nobody_asked_for_is_claimed_once
 * - an_off_owed_under_a_held_port_goes_when_the_port_lets_go
 * - a_stale_off_report_does_not_drop_the_owner_of_an_on_on_its_way
 * - a_dropped_hold_releases_the_owner_so_a_later_claim_works
 * - a_dropped_hold_keeps_the_owner_of_an_on_already_taken
 * - an_on_taken_before_the_port_held_is_owed_its_off
 * - an_output_being_switched_off_is_not_claimed
 * - reset_peaks_posts_its_own_command
 * - the_fine_steps_move_a_set_point_by_a_tenth
 * - a_tap_on_a_track_sets_a_value_the_supply_takes
 * - new_caps_pull_the_set_points_into_range
 *
 * SPDX-License-Identifier: MIT
 */

#include "supply_screen_rig.h"

/* ---------------------------------------------------------------- switch */

TEST_CASE(the_output_comes_on_with_a_hold_and_only_once)
{
    fresh();
    supply_cmd_t c;
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    CHECK(!supply_screen_poll_cmd(&c));       /* half a hold asks nothing */
    tick_for(HOLD_TICKS);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.on);
    CHECK(!c.off);
    tick_for(HOLD_TICKS);                     /* still down: no second ON */
    CHECK(!supply_screen_poll_cmd(&c));
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));
}

TEST_CASE(a_short_press_on_output_on_does_nothing)
{
    fresh();
    tap(OUT_X, OUT_Y);
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_finger_that_leaves_the_switch_switches_nothing)
{
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_MOVE, 1);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_MOVE, 1);    /* coming back is not resuming */
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(the_output_goes_off_with_a_tap)
{
    fresh();
    supply_screen_set_output(true);
    supply_cmd_t c;
    tap(OUT_X, OUT_Y);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!c.on);

    /* A press that slides off before lifting is not a tap. */
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));
}

TEST_CASE(the_release_that_ends_the_hold_does_not_switch_off)
{
    /* The supply reports the output on while the finger is still down. */
    fresh();
    supply_cmd_t c;
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    CHECK(supply_screen_poll_cmd(&c) && c.on);
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));

    /* And after the finger has lifted. */
    fresh();
    hold_on();
    CHECK(supply_screen_poll_cmd(&c) && c.on);
    supply_screen_set_output(true);
    CHECK(!supply_screen_poll_cmd(&c));
}

TEST_CASE(an_off_outranks_an_on_in_the_same_poll)
{
    fresh();
    supply_cmd_t c;
    hold_on();                         /* ON posted, not yet collected */
    supply_screen_set_output(true);    /* the supply reports it on */
    tap(OUT_X, OUT_Y);                 /* and the operator turns it off */
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!c.on);

    /* And an ON completed after an OFF that is still pending: the output
     * went off under it (a stop) before the OFF was collected. */
    fresh();
    supply_screen_set_output(true);
    tap(OUT_X, OUT_Y);
    supply_screen_set_output(false);
    hold_on();
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!c.on);
}

TEST_CASE(a_second_contact_cannot_steal_the_release)
{
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 2);
    CHECK(!supply_screen_poll_cmd(NULL));
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c) && c.off);
}

TEST_CASE(a_second_contact_on_another_button_cannot_steal_the_off)
{
    fresh();
    supply_cmd_t c;
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(RESET_X, RESET_Y, TOUCH_EVENT_DOWN, 2);
    ev(RESET_X, RESET_Y, TOUCH_EVENT_UP, 2);
    ev(V_UP_X, V_ROW_Y, TOUCH_EVENT_DOWN, 3);
    ev(V_UP_X, V_ROW_Y, TOUCH_EVENT_UP, 3);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!c.reset);

    /* And the buttons answer again once the first contact has lifted. */
    tap(RESET_X, RESET_Y);
    CHECK(supply_screen_poll_cmd(&c) && c.reset);
}

TEST_CASE(a_stop_abandons_the_hold_and_drops_a_posted_on)
{
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    supply_screen_cancel_on();
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(NULL));

    /* An ON already posted and not collected goes too. */
    hold_on();
    supply_screen_cancel_on();
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_stop_while_the_output_reports_off_strands_no_press)
{
    /* The output goes off under a finger on OUTPUT OFF: the press neither
     * switches it back on nor waits to. */
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    supply_screen_set_output(false);
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_touch_loss_drops_an_on_and_keeps_an_off)
{
    supply_cmd_t c;
    fresh();
    hold_on();
    scr->cancel();
    CHECK(!supply_screen_poll_cmd(&c));

    /* A hold under way when the loss lands asks for nothing. */
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    scr->cancel();
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(&c));

    /* A press on OUTPUT OFF whose release went missing is an OFF made. */
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
}

TEST_CASE(leaving_under_a_press_on_output_off_sends_the_off)
{
    /* A second finger on HOME leaves the screen while the first is still
     * on OUTPUT OFF: the OFF it was making is sent, not lost. */
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    scr->leave();
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    /* And a hold toward ON left the same way asks for nothing. */
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    scr->leave();
    CHECK(!supply_screen_poll_cmd(NULL));
}

/* An output this screen did not switch on -- on at the start, or another
 * screen's -- stays on when it is left; a hold toward ON is dropped. */
TEST_CASE(leaving_keeps_an_output_it_did_not_switch_on_and_drops_the_hold)
{
    fresh();
    supply_screen_set_output(true);
    scr->leave();
    CHECK(!supply_screen_poll_cmd(NULL));

    fresh();
    supply_screen_ask_on(SUPPLY_BY_SERVO);
    drain();
    supply_screen_set_output(true);
    scr->leave();
    CHECK(!supply_screen_poll_cmd(NULL));
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SERVO);

    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    scr->leave();
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(NULL));

    /* And the switch comes back unfaded: the screen on return draws as one
     * that was never pressed. */
    fresh();
    scr->leave();
    scr->render(&cv, 0);
    gfx_color_t *was = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(was);
    memcpy(was, fb, (size_t)W * H * sizeof(gfx_color_t));
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    scr->leave();
    scr->render(&cv, 0);
    const int same = memcmp(fb, was, (size_t)W * H * sizeof(gfx_color_t));
    free(was);
    CHECK_EQ(same, 0);
}

/*
 * Leaving switches off an output this screen switched on: reported on, an
 * ON taken and not yet reported, and an ON not yet taken.  An output it
 * switched on and that has since gone off is nobody's: the leave asks
 * nothing.
 */
TEST_CASE(leaving_switches_off_the_output_this_screen_switched_on)
{
    supply_cmd_t c;

    for (int k = 0; k < 3; ++k) {
        fresh();
        hold_on();
        CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SUPPLY);
        if (k >= 1) {
            CHECK(supply_screen_poll_cmd(&c));      /* the ON taken */
            CHECK(c.on);
        }
        if (k == 2) {
            supply_screen_set_output(true);         /* and reported */
        }
        scr->leave();
        CHECK(supply_screen_poll_cmd(&c));
        CHECK(c.off);
        CHECK(!c.on);
        /* Live only while the supply still reports it on. */
        CHECK_EQ(supply_screen_output_live(), k == 2);
        CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
        /* Back on the screen and left again: nothing more is asked. */
        scr->enter();
        scr->leave();
        CHECK(!supply_screen_poll_cmd(NULL));
    }

    /* On and off again before the leave: nothing to switch off. */
    fresh();
    hold_on();
    drain();
    supply_screen_set_output(true);
    tick_for(1);
    supply_screen_set_output(false);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
    scr->leave();
    CHECK(!supply_screen_poll_cmd(NULL));

    /* An ON a STOP dropped before it went out: nobody's, nothing asked. */
    fresh();
    hold_on();
    supply_screen_cancel_on();
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
    scr->leave();
    CHECK(!supply_screen_poll_cmd(NULL));

    /* An ON taken that the application then dropped: nobody's. */
    fresh();
    hold_on();
    drain();
    supply_screen_set_on_coming(false);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
    scr->leave();
    CHECK(!supply_screen_poll_cmd(NULL));
}

/* The settings, the keypads and the question are this screen's own: an
 * output switched on here stays on under each of them. */
TEST_CASE(the_overlays_of_supply_are_no_leave)
{
    fresh();
    hold_on();
    drain();
    supply_screen_set_output(true);
    tap(SET_TAB_X, STRIP_Y);             /* SETTINGS */
    tick_for(4);
    tap(LIVE_TAB_X, STRIP_Y);            /* and LIVE again */
    tap(V_UP_X, V_ROW_Y);                /* a question on a live output */
    tick_for(4);
    tap(DISCARD_X, ASK_Y);               /* answered CANCEL */
    tap(CARD_X, CARD_V_Y);               /* a set point's keypad */
    tick_for(4);
    CHECK(!supply_screen_poll_cmd(NULL));
    CHECK(supply_screen_output_on());
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SUPPLY);
}

/*
 * Each screen's leave switches off only its own output: SERVO's ON is not
 * SUPPLY's to switch off, and the reverse.  supply_screen_left() is what
 * each screen's leave() calls.
 */
TEST_CASE(a_leave_switches_off_only_the_screens_own_output)
{
    static const supply_by_t by[] = { SUPPLY_BY_SUPPLY, SUPPLY_BY_SERVO,
                                      SUPPLY_BY_STICK };
    supply_cmd_t c;

    for (size_t a = 0; a < sizeof(by) / sizeof(by[0]); ++a) {
        for (size_t b = 0; b < sizeof(by) / sizeof(by[0]); ++b) {
            fresh();
            supply_screen_ask_on(by[a]);
            drain();
            supply_screen_set_output(true);
            supply_screen_left(by[b]);
            if (a == b) {
                CHECK(supply_screen_poll_cmd(&c));
                CHECK(c.off);
            } else {
                CHECK(!supply_screen_poll_cmd(NULL));
                CHECK_EQ(supply_screen_owner(), by[a]);
            }
        }
    }
    /* Nobody's output is left to nobody. */
    fresh();
    supply_screen_set_output(true);
    supply_screen_left(SUPPLY_BY_NONE);
    CHECK(!supply_screen_poll_cmd(NULL));
}

/*
 * An output on that nobody here asked for -- the servo programmer's
 * CONNECT switches a PD mini on through the IOmcu -- is claimed by
 * the screen that shows it; an owned one keeps its owner, and an output
 * that is off is nobody's to claim.
 */
TEST_CASE(an_output_nobody_asked_for_is_claimed_once)
{
    fresh();
    supply_screen_claim(SUPPLY_BY_SERVO);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);    /* off */
    supply_screen_set_output(true);
    supply_screen_claim(SUPPLY_BY_SERVO);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SERVO);
    supply_screen_claim(SUPPLY_BY_SUPPLY);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SERVO);
    supply_screen_set_output(false);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
}

/*
 * Left while a programming port holds the supply, the OFF would end the
 * port's session and cut a write: it is owed, and sent as the port lets
 * go.  The screen shown again before that drops it; an output that went
 * off meanwhile owes nothing.
 */
TEST_CASE(an_off_owed_under_a_held_port_goes_when_the_port_lets_go)
{
    supply_cmd_t c;

    for (int k = 0; k < 3; ++k) {
        fresh();
        supply_screen_ask_on(SUPPLY_BY_SERVO);
        drain();
        supply_screen_set_output(true);
        supply_screen_set_port_held(true);
        supply_screen_left(SUPPLY_BY_SERVO);
        CHECK(!supply_screen_poll_cmd(NULL));
        CHECK(supply_screen_off_owed());
        CHECK(supply_screen_output_on());
        if (k == 1) {
            supply_screen_entered(SUPPLY_BY_SERVO);  /* shown again */
            CHECK(!supply_screen_off_owed());
        } else if (k == 2) {
            supply_screen_set_output(false);         /* the port's STOP */
            CHECK(!supply_screen_off_owed());
        }
        supply_screen_set_port_held(false);
        if (k == 0) {
            CHECK(supply_screen_poll_cmd(&c));
            CHECK(c.off);
            CHECK(!c.on);
        } else {
            CHECK(!supply_screen_poll_cmd(NULL));
        }
        CHECK(!supply_screen_off_owed());
    }
    /* Another screen entered owes nothing back. */
    fresh();
    supply_screen_ask_on(SUPPLY_BY_SERVO);
    drain();
    supply_screen_set_output(true);
    supply_screen_set_port_held(true);
    supply_screen_left(SUPPLY_BY_SERVO);
    supply_screen_entered(SUPPLY_BY_SUPPLY);
    CHECK(supply_screen_off_owed());
    supply_screen_set_port_held(false);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
}

/*
 * An ON on its way is an output to switch off: the supply may still report
 * the old off state after the ON was taken, and that stale report neither
 * drops the owner nor excuses the leave.
 */
TEST_CASE(a_stale_off_report_does_not_drop_the_owner_of_an_on_on_its_way)
{
    supply_cmd_t c;

    static const supply_by_t by[] = { SUPPLY_BY_SERVO, SUPPLY_BY_SUPPLY };
    for (size_t k = 0; k < 2; ++k) {
        fresh();
        supply_screen_ask_on(by[k]);
        drain();                                /* taken, not yet reported */
        supply_screen_set_output(false);        /* the old state, still off */
        CHECK_EQ(supply_screen_owner(), by[k]);
        supply_screen_left(by[k]);
        CHECK(supply_screen_poll_cmd(&c));
        CHECK(c.off);
        CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
    }
}

/*
 * A hold on OUTPUT ON that loses its touch before the ON was taken leaves
 * the output off and nobody's: the servo programmer's CONNECT that switches
 * the PD mini on afterwards is claimed, and left, as SERVO's.
 */
TEST_CASE(a_dropped_hold_releases_the_owner_so_a_later_claim_works)
{
    supply_cmd_t c;

    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);                       /* the ON is asked */
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SUPPLY);
    scr->cancel();                              /* the touch is lost */
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
    CHECK(!supply_screen_output_live());
    CHECK(!supply_screen_poll_cmd(&c));         /* the ON is dropped */
    supply_screen_set_output(true);             /* CONNECT's PD mini */
    supply_screen_claim(SUPPLY_BY_SERVO);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SERVO);
    supply_screen_left(SUPPLY_BY_SERVO);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
}

/*
 * A hold on OUTPUT ON that loses its touch after the ON was taken keeps
 * that ON's owner until the application reports what became of it.  The
 * panel's control task drops such an ON, or switches the output off with
 * the touch-loss alert until a whole frame has seen it on.  A loss after
 * that frame but before the screen's first sample leaves it on, and the
 * screen's part is that the leave switches it off; an ON the application
 * drops lets the owner go.
 */
TEST_CASE(a_dropped_hold_keeps_the_owner_of_an_on_already_taken)
{
    supply_cmd_t c;

    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    drain();                                    /* the ON is taken */
    scr->cancel();                              /* the touch is lost */
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SUPPLY);
    CHECK(supply_screen_output_live());
    CHECK(!supply_screen_poll_cmd(&c));         /* nothing new is asked */
    supply_screen_set_output(true);             /* the ON lands */
    supply_screen_claim(SUPPLY_BY_SERVO);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SUPPLY);
    scr->leave();
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off && !c.on);

    /* Taken, then dropped by the application: nobody's, nothing owed. */
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    drain();
    scr->cancel();
    supply_screen_set_on_coming(false);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
    CHECK(!supply_screen_output_live());
    scr->leave();
    CHECK(!supply_screen_poll_cmd(&c));
}

/*
 * An ON SERVO asked, taken before a programming port took the supply,
 * stays SERVO's: left while the port holds, the output that comes on
 * afterwards is switched off when the port lets go.  Not taken, the ON is
 * dropped with the hold and nothing is owed.
 */
TEST_CASE(an_on_taken_before_the_port_held_is_owed_its_off)
{
    supply_cmd_t c;

    /* Taken, the port holds, SERVO is left, the ON lands. */
    fresh();
    supply_screen_ask_on(SUPPLY_BY_SERVO);
    drain();
    supply_screen_set_port_held(true);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_SERVO);
    supply_screen_left(SUPPLY_BY_SERVO);
    CHECK(supply_screen_off_owed());
    CHECK(!supply_screen_poll_cmd(&c));         /* nothing under the port */
    supply_screen_set_output(true);
    CHECK(supply_screen_off_owed());
    supply_screen_set_port_held(false);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off && !c.on);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);

    /* Taken, SERVO left, the application drops the ON: nothing owed. */
    fresh();
    supply_screen_ask_on(SUPPLY_BY_SERVO);
    drain();
    supply_screen_set_port_held(true);
    supply_screen_left(SUPPLY_BY_SERVO);
    supply_screen_set_on_coming(false);
    CHECK(!supply_screen_off_owed());
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
    supply_screen_set_port_held(false);
    CHECK(!supply_screen_poll_cmd(&c));

    /* Not taken: the hold drops it, SERVO's leave owes nothing. */
    fresh();
    supply_screen_ask_on(SUPPLY_BY_SERVO);
    supply_screen_set_port_held(true);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
    supply_screen_left(SUPPLY_BY_SERVO);
    CHECK(!supply_screen_off_owed());
    supply_screen_set_port_held(false);
    CHECK(!supply_screen_poll_cmd(&c));
}

/* An output being switched off is nobody's to claim. */
TEST_CASE(an_output_being_switched_off_is_not_claimed)
{
    fresh();
    supply_screen_set_output(true);
    tap(OUT_X, OUT_Y);                          /* OUTPUT OFF, not yet sent */
    CHECK(supply_screen_off_pending());
    supply_screen_claim(SUPPLY_BY_SERVO);
    CHECK_EQ(supply_screen_owner(), SUPPLY_BY_NONE);
}

TEST_CASE(reset_peaks_posts_its_own_command)
{
    fresh();
    supply_cmd_t c;
    tap(RESET_X, RESET_Y);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.reset);
    CHECK(!c.on && !c.off);

    /* Slid off before the release: nothing. */
    ev(RESET_X, RESET_Y, TOUCH_EVENT_DOWN, 1);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));
}

/* ------------------------------------------------------------ set points */

TEST_CASE(the_fine_steps_move_a_set_point_by_a_tenth)
{
    fresh();
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);

    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);

    tap(V_DOWN_X, I_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 1.9f, 1e-4f);

    /* Down past the bottom of the range stops at it. */
    for (int k = 0; k < 40; ++k) {
        tap(V_DOWN_X, I_ROW_Y);
    }
    CHECK_NEAR(supply_screen_set_i(), 0.5f, 1e-4f);

    /* A set point is a level, not a command: nothing is posted. */
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_tap_on_a_track_sets_a_value_the_supply_takes)
{
    fresh();
    tap(TRACK_X + TRACK_W / 2, V_ROW_Y);
    /* About the middle of 3.3 to 21V, on a 20mV step above 3.3V. */
    const float v = supply_screen_set_v();
    CHECK(v > 11.5f && v < 12.7f);
    const float steps = (v - 3.3f) / 0.02f;
    CHECK_NEAR(steps, roundf(steps), 1e-2f);

    /* The current track the same, on its 50mA step. */
    tap(TRACK_X + TRACK_W - 1, I_ROW_Y);
    CHECK_NEAR(supply_screen_set_i(), 5.0f, 1e-4f);
}

TEST_CASE(new_caps_pull_the_set_points_into_range)
{
    fresh();
    const supply_caps_t narrow = { 5.0f, 5.0f, 0.0f, 0.5f, 3.0f, 0.05f };
    supply_screen_set_caps(&narrow);
    CHECK_NEAR(supply_screen_set_v(), 5.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);
    const supply_caps_t low = { 3.3f, 21.0f, 0.02f, 0.5f, 1.0f, 0.05f };
    supply_screen_set_caps(&low);
    CHECK_NEAR(supply_screen_set_i(), 1.0f, 1e-4f);
    supply_screen_set_caps(NULL);
    CHECK(!supply_screen_poll_cmd(NULL));     /* caps are not a command */
}

/*
 * The KST programmer's hold and the Hitec programmer's lock the supply
 * together: it stays held while either holds a port.  A press the hold
 * refuses, OUTPUT ON or a set point, says why in the alert band; OFF is
 * taken.
 */
TEST_CASE(either_programmers_hold_locks_the_supply_and_a_refused_press_says_why)
{
    fresh();
    CHECK(!supply_screen_port_held());
    supply_screen_set_port_held(true);
    supply_screen_set_hitec_held(true);
    CHECK(supply_screen_port_held());
    supply_screen_set_port_held(false);
    CHECK(supply_screen_port_held());
    supply_screen_set_hitec_held(false);
    CHECK(!supply_screen_port_held());
    supply_screen_set_hitec_held(true);
    CHECK(supply_screen_port_held());

    ui_router_set_alert(NULL);
    tap(OUT_X, OUT_Y);
    CHECK(!supply_screen_output_on());
    CHECK(!supply_screen_poll_cmd(NULL));
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), TR(SUP_HELD_REFUSED)) == 0);
    ui_router_set_alert(NULL);
    tap(V_UP_X, V_ROW_Y);
    CHECK(!supply_screen_poll_cmd(NULL));
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), TR(SUP_HELD_REFUSED)) == 0);
    ui_router_set_alert(NULL);
    tap(CARD_X, CARD_V_Y);
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), TR(SUP_HELD_REFUSED)) == 0);
    CHECK(!supply_screen_keypad()->open);
    /* A press on the plot is no refused control: no alert. */
    ui_router_set_alert(NULL);
    tap(OFF_X, OFF_Y);
    CHECK(ui_router_alert() == NULL || ui_router_alert()[0] == '\0');
    supply_screen_set_hitec_held(false);
    ui_router_set_alert(NULL);
}

int main(void)
{
    RUN(the_output_comes_on_with_a_hold_and_only_once);
    RUN(a_short_press_on_output_on_does_nothing);
    RUN(a_finger_that_leaves_the_switch_switches_nothing);
    RUN(the_output_goes_off_with_a_tap);
    RUN(the_release_that_ends_the_hold_does_not_switch_off);
    RUN(an_off_outranks_an_on_in_the_same_poll);
    RUN(a_second_contact_cannot_steal_the_release);
    RUN(a_second_contact_on_another_button_cannot_steal_the_off);
    RUN(a_stop_abandons_the_hold_and_drops_a_posted_on);
    RUN(a_stop_while_the_output_reports_off_strands_no_press);
    RUN(a_touch_loss_drops_an_on_and_keeps_an_off);
    RUN(leaving_under_a_press_on_output_off_sends_the_off);
    RUN(leaving_keeps_an_output_it_did_not_switch_on_and_drops_the_hold);
    RUN(leaving_switches_off_the_output_this_screen_switched_on);
    RUN(the_overlays_of_supply_are_no_leave);
    RUN(a_leave_switches_off_only_the_screens_own_output);
    RUN(an_output_nobody_asked_for_is_claimed_once);
    RUN(an_off_owed_under_a_held_port_goes_when_the_port_lets_go);
    RUN(a_stale_off_report_does_not_drop_the_owner_of_an_on_on_its_way);
    RUN(a_dropped_hold_releases_the_owner_so_a_later_claim_works);
    RUN(a_dropped_hold_keeps_the_owner_of_an_on_already_taken);
    RUN(an_on_taken_before_the_port_held_is_owed_its_off);
    RUN(an_output_being_switched_off_is_not_claimed);
    RUN(reset_peaks_posts_its_own_command);
    RUN(the_fine_steps_move_a_set_point_by_a_tenth);
    RUN(a_tap_on_a_track_sets_a_value_the_supply_takes);
    RUN(new_caps_pull_the_set_points_into_range);
    RUN(either_programmers_hold_locks_the_supply_and_a_refused_press_says_why);
    free(fb);
    free(fb2);
    return test_summary("supply_screen");
}
