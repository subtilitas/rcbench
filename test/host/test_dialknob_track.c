/*
 * The panel's side of the Dial's link (protocols/dialknob): the turn
 * tracker and the link's state.
 *
 * The failures this is about: a turn applied after a reconnect or a stall,
 * as a jump of what was turned in the gap; a counter wrap or a bit error
 * taken as a turn of thousands of detents; a packet taken twice; a lost
 * Dial that leaves an ARMED bench running; and a loss while SAFE that ends
 * a later run.  Each rule of PLAN-DIAL section 2.4 and 2.7 is a case, at its
 * limit and one past it.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "dialknob.h"

/** An event with a seq and a counter, nothing pressed. */
static dk_evt_t ev(uint8_t seq, int counter)
{
    dk_evt_t e;
    memset(&e, 0, sizeof e);
    e.seq = seq;
    e.counter = (int16_t)counter;
    return e;
}

/** Feed one event; return the detents and the take in @p why. */
static int32_t feed(dk_track_t *t, uint8_t seq, int counter, uint32_t ms,
                    dk_take_t *why)
{
    dk_evt_t e = ev(seq, counter);
    return dk_track_feed(t, &e, ms, why);
}

TEST_CASE(the_first_packet_after_a_connect_sets_the_reference_and_moves_nothing)
{
    dk_track_t t;
    dk_take_t why;

    dk_track_init(&t);
    CHECK_EQ(feed(&t, 10, 500, 1000, &why), 0);
    CHECK_EQ(why, DK_TAKE_REF);
    CHECK_EQ(t.refs, 1u);
    CHECK_EQ(feed(&t, 11, 503, 1020, &why), 3);
    CHECK_EQ(why, DK_TAKE_MOVE);
    CHECK_EQ(feed(&t, 12, 501, 1040, &why), -2);
    CHECK_EQ(t.taken, 2u);

    /* A reconnect: whatever the counter did in between moves nothing. */
    dk_track_reset(&t);
    CHECK_EQ(feed(&t, 200, -9000, 5000, &why), 0);
    CHECK_EQ(why, DK_TAKE_REF);
    CHECK_EQ(t.refs, 2u);
    CHECK_EQ(t.taken, 2u);                  /* counts survive the reset */
    CHECK_EQ(feed(&t, 201, -8999, 5010, &why), 1);
}

TEST_CASE(a_packet_more_than_300ms_after_the_last_sets_the_reference)
{
    dk_track_t t;
    dk_take_t why;

    dk_track_init(&t);
    feed(&t, 1, 0, 1000, &why);
    CHECK_EQ(feed(&t, 2, 5, 1000 + DK_STALE_MS, &why), 5);   /* at 300 */
    CHECK_EQ(why, DK_TAKE_MOVE);
    CHECK_EQ(feed(&t, 3, 9, 1000 + 2 * DK_STALE_MS + 1, &why), 0);
    CHECK_EQ(why, DK_TAKE_STALE);           /* 301 after the one before */
    CHECK_EQ(t.stale, 1u);
    /* The stale one is the reference: the next moves from its counter. */
    CHECK_EQ(feed(&t, 4, 10, 1000 + 2 * DK_STALE_MS + 21, &why), 1);
    CHECK_EQ(why, DK_TAKE_MOVE);
}

TEST_CASE(the_stale_rule_holds_across_the_clock_wrap)
{
    dk_track_t t;
    dk_take_t why;

    dk_track_init(&t);
    feed(&t, 1, 0, 0xFFFFFF00u, &why);
    CHECK_EQ(feed(&t, 2, 2, 0xFFFFFF00u + DK_STALE_MS, &why), 2);
    CHECK_EQ(why, DK_TAKE_MOVE);            /* 300ms, across 2^32 */
    CHECK_EQ(feed(&t, 3, 4, 0xFFFFFF00u + 2u * DK_STALE_MS + 1u, &why), 0);
    CHECK_EQ(why, DK_TAKE_STALE);
}

TEST_CASE(a_difference_over_16_detents_is_a_glitch_and_a_new_reference)
{
    dk_track_t t;
    dk_take_t why;

    dk_track_init(&t);
    feed(&t, 1, 100, 0, &why);
    CHECK_EQ(feed(&t, 2, 100 + DK_MAX_STEP, 10, &why), DK_MAX_STEP);
    CHECK_EQ(why, DK_TAKE_MOVE);
    CHECK_EQ(feed(&t, 3, 116 - DK_MAX_STEP, 20, &why), -DK_MAX_STEP);
    CHECK_EQ(feed(&t, 4, 100 + DK_MAX_STEP + 1, 30, &why), 0);
    CHECK_EQ(why, DK_TAKE_GLITCH);
    CHECK_EQ(feed(&t, 5, 117 - DK_MAX_STEP - 1, 40, &why), 0);
    CHECK_EQ(why, DK_TAKE_GLITCH);
    CHECK_EQ(t.glitches, 2u);
    CHECK_EQ(feed(&t, 6, 101, 50, &why), 1);    /* from the glitch's 100 */
    /* A bit error in the counter's top bit is a glitch, not 32,768. */
    CHECK_EQ(feed(&t, 7, 101 - 32768, 60, &why), 0);
    CHECK_EQ(why, DK_TAKE_GLITCH);
}

TEST_CASE(the_counter_wraps_from_32767_to_minus_32768_as_one_detent)
{
    dk_track_t t;
    dk_take_t why;

    dk_track_init(&t);
    feed(&t, 1, 32766, 0, &why);
    CHECK_EQ(feed(&t, 2, -32768, 10, &why), 2);
    CHECK_EQ(why, DK_TAKE_MOVE);
    CHECK_EQ(feed(&t, 3, 32767, 20, &why), -1);
    CHECK_EQ(feed(&t, 4, -32768 + 15, 30, &why), DK_MAX_STEP);
}

TEST_CASE(a_repeated_seq_is_dropped_and_its_gesture_not_taken_twice)
{
    dk_track_t t;
    dk_take_t why;
    dk_evt_t e;

    dk_track_init(&t);
    feed(&t, 50, 0, 0, &why);
    e = ev(51, 3);
    e.gesture = DK_G_HOLD;
    CHECK_EQ(dk_track_feed(&t, &e, 10, &why), 3);
    CHECK_EQ(why, DK_TAKE_MOVE);
    CHECK_EQ(dk_track_feed(&t, &e, 20, &why), 0);   /* the same packet */
    CHECK_EQ(why, DK_TAKE_REPEAT);
    CHECK_EQ(t.repeats, 1u);
    /* A repeat moves no reference: the next packet moves from 3. */
    CHECK_EQ(feed(&t, 52, 4, 30, &why), 1);
    /* Nor a repeat's clock: 52's 30ms counts for the stale rule. */
    CHECK_EQ(feed(&t, 52, 4, 300, &why), 0);
    CHECK_EQ(why, DK_TAKE_REPEAT);
    CHECK_EQ(feed(&t, 53, 5, 30 + DK_STALE_MS + 1, &why), 0);
    CHECK_EQ(why, DK_TAKE_STALE);
}

TEST_CASE(a_skipped_seq_is_taken_and_counted_with_no_motion_lost)
{
    dk_track_t t;
    dk_take_t why;

    dk_track_init(&t);
    feed(&t, 250, 0, 0, &why);
    /* 251 to 254 lost: the counter is absolute, so 255 carries them. */
    CHECK_EQ(feed(&t, 255, 7, 10, &why), 7);
    CHECK_EQ(why, DK_TAKE_MOVE);
    CHECK_EQ(t.skipped, 4u);
    CHECK_EQ(feed(&t, 0, 8, 20, &why), 1);      /* the seq wraps: no skip */
    CHECK_EQ(t.skipped, 4u);
    CHECK_EQ(feed(&t, 2, 9, 30, &why), 1);      /* 1 lost */
    CHECK_EQ(t.skipped, 5u);
    CHECK_EQ(feed(&t, 1, 10, 40, &why), 1);     /* behind: 254 skipped */
    CHECK_EQ(t.skipped, 259u);
}

TEST_CASE(the_tracker_survives_null_arguments)
{
    dk_track_t t;
    dk_take_t why = DK_TAKE_MOVE;
    dk_evt_t e = ev(1, 1);

    dk_track_init(&t);
    CHECK_EQ(dk_track_feed(NULL, &e, 0, &why), 0);
    CHECK_EQ(why, DK_TAKE_REPEAT);
    CHECK_EQ(dk_track_feed(&t, NULL, 0, NULL), 0);
    CHECK_EQ(dk_track_feed(&t, &e, 0, NULL), 0);    /* a reference */
    dk_track_init(NULL);
    dk_track_reset(NULL);
}

TEST_CASE(the_link_is_degraded_at_500ms_and_lost_at_1000ms_without_a_packet)
{
    dk_link_t l;

    dk_link_init(&l);
    CHECK_EQ(dk_link_step(&l, 0), DK_LINK_DOWN);
    dk_link_connect(&l, 1000);
    CHECK_EQ(dk_link_step(&l, 1000), DK_LINK_UP);
    CHECK_EQ(dk_link_step(&l, 1000 + DK_DEGRADED_MS - 1), DK_LINK_UP);
    CHECK_EQ(dk_link_step(&l, 1000 + DK_DEGRADED_MS), DK_LINK_DEGRADED);
    dk_link_rx(&l, 1600);
    CHECK_EQ(dk_link_step(&l, 1600), DK_LINK_UP);
    CHECK_EQ(dk_link_step(&l, 1600 + DK_LOST_MS - 1), DK_LINK_DEGRADED);
    CHECK_EQ(l.losses, 0u);
    CHECK_EQ(dk_link_step(&l, 1600 + DK_LOST_MS), DK_LINK_LOST);
    CHECK_EQ(l.losses, 1u);
    CHECK_EQ(dk_link_step(&l, 9000), DK_LINK_LOST);
    CHECK_EQ(l.losses, 1u);                 /* one loss, not one a step */
    /* Packets again on the same connection: UP. */
    dk_link_rx(&l, 9100);
    CHECK_EQ(dk_link_step(&l, 9100), DK_LINK_UP);
}

TEST_CASE(a_step_that_skips_degraded_is_still_lost_at_1000ms)
{
    dk_link_t l;

    dk_link_init(&l);
    dk_link_connect(&l, 0);
    CHECK_EQ(dk_link_step(&l, DK_LOST_MS), DK_LINK_LOST);
    CHECK_EQ(l.losses, 1u);
    dk_link_init(&l);
    dk_link_connect(&l, 0xFFFFFE00u);       /* across the clock's wrap */
    CHECK_EQ(dk_link_step(&l, 0xFFFFFE00u + DK_DEGRADED_MS), DK_LINK_DEGRADED);
    CHECK_EQ(dk_link_step(&l, 0xFFFFFE00u + DK_LOST_MS - 1u), DK_LINK_DEGRADED);
    CHECK_EQ(dk_link_step(&l, 0xFFFFFE00u + DK_LOST_MS), DK_LINK_LOST);
}

TEST_CASE(a_disconnect_is_lost_at_once)
{
    dk_link_t l;

    dk_link_init(&l);
    dk_link_connect(&l, 0);
    dk_link_rx(&l, 10);
    dk_link_disconnect(&l);
    CHECK_EQ(dk_link_step(&l, 11), DK_LINK_LOST);
    CHECK_EQ(l.losses, 1u);
    dk_link_rx(&l, 20);                     /* nothing while disconnected */
    CHECK_EQ(dk_link_step(&l, 20), DK_LINK_LOST);
    dk_link_disconnect(&l);                 /* a second report: no loss */
    CHECK_EQ(l.losses, 1u);
    dk_link_connect(&l, 3000);
    CHECK_EQ(dk_link_step(&l, 3000), DK_LINK_UP);
}

TEST_CASE(a_loss_while_armed_is_an_error_once)
{
    dk_link_t l;

    dk_link_init(&l);
    dk_link_connect(&l, 0);
    CHECK(!dk_link_loss_error(&l, true));   /* no loss yet */
    dk_link_step(&l, DK_LOST_MS - 1u);
    CHECK(!dk_link_loss_error(&l, true));   /* DEGRADED is a notice */
    dk_link_step(&l, DK_LOST_MS);
    CHECK(dk_link_loss_error(&l, true));
    CHECK(!dk_link_loss_error(&l, true));   /* taken once */
    dk_link_step(&l, 50000);
    CHECK(!dk_link_loss_error(&l, true));   /* still LOST: no new loss */

    dk_link_connect(&l, 60000);
    dk_link_disconnect(&l);
    CHECK(dk_link_loss_error(&l, true));    /* the disconnect, at once */
}

TEST_CASE(a_loss_while_safe_is_no_error_and_is_not_kept_for_a_later_arm)
{
    dk_link_t l;

    dk_link_init(&l);
    dk_link_connect(&l, 0);
    dk_link_step(&l, DK_LOST_MS);
    CHECK(!dk_link_loss_error(&l, false));  /* SAFE: taken, no error */
    CHECK(!dk_link_loss_error(&l, true));   /* the ARM afterwards */

    /* The Dial switched off on the panel drops a pending loss. */
    dk_link_connect(&l, 2000);
    dk_link_disconnect(&l);
    dk_link_off(&l);
    CHECK_EQ(dk_link_step(&l, 2001), DK_LINK_DOWN);
    CHECK(!dk_link_loss_error(&l, true));
    /* And a Dial never connected is no loss at all. */
    dk_link_init(&l);
    CHECK_EQ(dk_link_step(&l, 100000), DK_LINK_DOWN);
    CHECK(!dk_link_loss_error(&l, true));
    CHECK(!dk_link_loss_error(NULL, true));
    CHECK_EQ(dk_link_step(NULL, 0), DK_LINK_DOWN);
}

int main(void)
{
    RUN(the_first_packet_after_a_connect_sets_the_reference_and_moves_nothing);
    RUN(a_packet_more_than_300ms_after_the_last_sets_the_reference);
    RUN(the_stale_rule_holds_across_the_clock_wrap);
    RUN(a_difference_over_16_detents_is_a_glitch_and_a_new_reference);
    RUN(the_counter_wraps_from_32767_to_minus_32768_as_one_detent);
    RUN(a_repeated_seq_is_dropped_and_its_gesture_not_taken_twice);
    RUN(a_skipped_seq_is_taken_and_counted_with_no_motion_lost);
    RUN(the_tracker_survives_null_arguments);
    RUN(the_link_is_degraded_at_500ms_and_lost_at_1000ms_without_a_packet);
    RUN(a_step_that_skips_degraded_is_still_lost_at_1000ms);
    RUN(a_disconnect_is_lost_at_once);
    RUN(a_loss_while_armed_is_an_error_once);
    RUN(a_loss_while_safe_is_no_error_and_is_not_kept_for_a_later_arm);
    return test_summary("dialknob_track");
}
