/*
 * Stick programming: the beep detector on its own (esc_det.c), fed supply
 * readings at a chosen rate.
 *
 * The cases that matter most are the ones where a count could go wrong: a
 * beep that never shows in the current must not become a selection of the
 * number below it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "esc_profile.h"
#include "esc_sim.h"
#include "esc_stick.h"

/* ------------------------------------------------------- the detector */

static esc_det_t det;

/* The time the last det_fresh() left the detector at. */
static uint32_t g_at;

static void det_fresh(uint8_t les)
{
    esc_stick_timing_t t;
    esc_stick_timing_defaults(&t);
    esc_det_init(&det, &t, les);
    /* The floor at 150mA, then counting, after the quiet that has to come
     * first. */
    (void)esc_det_reading(&det, 0u, 150, false);
    esc_det_count(&det);
    g_at = 0u;
    for (int i = 0; i < 15; ++i) {
        g_at += 50u;
        (void)esc_det_reading(&det, g_at, 150, false);
    }
}

/* Readings every @p iv ms from @p *at: @p n of them at @p ma. */
static esc_det_event_t feed(uint32_t *at, uint32_t iv, int n, int32_t ma)
{
    esc_det_event_t last = ESC_DET_NONE;
    for (int i = 0; i < n; ++i) {
        *at += iv;
        const esc_det_event_t ev = esc_det_reading(&det, *at, ma, false);
        if (ev != ESC_DET_NONE) {
            last = ev;
        }
    }
    return last;
}

TEST_CASE(beeps_make_a_group_that_silence_ends)
{
    det_fresh(0u);
    uint32_t at = g_at;
    for (int b = 0; b < 3; ++b) {
        (void)feed(&at, 50u, 5, 750);      /* 250ms beep */
        CHECK_EQ(feed(&at, 50u, 1, 150), ESC_DET_PULSE);
        (void)feed(&at, 50u, 4, 150);      /* the rest of a 250ms gap */
    }
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK_EQ(det.count, 3);
    CHECK(det.valid);
}

TEST_CASE(long_beeps_count_by_the_profiles_measure)
{
    det_fresh(5u);
    uint32_t at = g_at;
    (void)feed(&at, 50u, 16, 750);         /* 800ms: long */
    (void)feed(&at, 50u, 5, 150);
    (void)feed(&at, 50u, 5, 750);          /* short */
    (void)feed(&at, 50u, 5, 150);
    (void)feed(&at, 50u, 5, 750);          /* short */
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK_EQ(det.count, 7);
    CHECK(det.valid);
}

/* A long beep in a menu that sounds none, or after a short one, is not the
 * menu speaking. */
TEST_CASE(a_long_beep_where_none_belongs_spoils_the_group)
{
    det_fresh(0u);
    uint32_t at = g_at;
    (void)feed(&at, 50u, 16, 750);
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK(!det.valid);

    det_fresh(5u);
    at = g_at;
    (void)feed(&at, 50u, 5, 750);
    (void)feed(&at, 50u, 5, 150);
    (void)feed(&at, 50u, 16, 750);
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK(!det.valid);
}

TEST_CASE(pulses_and_gaps_outside_their_lengths_spoil_the_group)
{
    /* One reading of a pulse where a beep must hold four. */
    det_fresh(0u);
    uint32_t at = g_at;
    (void)feed(&at, 50u, 2, 150);
    (void)feed(&at, 50u, 1, 750);
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK(!det.valid);

    /* Longer than any beep. */
    det_fresh(5u);
    at = g_at;
    (void)feed(&at, 50u, 40, 750);
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK(!det.valid);

    /* A gap of one reading between two beeps. */
    det_fresh(0u);
    at = g_at;
    (void)feed(&at, 50u, 5, 750);
    (void)feed(&at, 50u, 1, 150);
    (void)feed(&at, 50u, 5, 750);
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK(!det.valid);
}

/* Between the two thresholds the state holds: no chatter on a reading that
 * sits near the edge. */
TEST_CASE(hysteresis_holds_a_beep_through_a_dip)
{
    det_fresh(0u);
    uint32_t at = g_at;
    (void)feed(&at, 50u, 2, 750);
    /* 150 + 100 - 40 = 210 is the release: 230 is still a beep. */
    CHECK_EQ(feed(&at, 50u, 1, 230), ESC_DET_NONE);
    (void)feed(&at, 50u, 2, 750);
    CHECK_EQ(feed(&at, 50u, 1, 150), ESC_DET_PULSE);
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK_EQ(det.count, 1);
    CHECK(det.valid);
}

TEST_CASE(a_current_that_stays_high_is_reported_once)
{
    det_fresh(0u);
    uint32_t at = g_at;
    int stuck = 0;
    for (int i = 0; i < 200; ++i) {
        at += 50u;
        if (esc_det_reading(&det, at, 900, false) == ESC_DET_STUCK) {
            ++stuck;
        }
    }
    CHECK_EQ(stuck, 1);
}

/* A pulse that ends on tick 0 has ended.  Counting restarted while the line
 * has been quiet since that fall takes the gap already heard as quiet: the
 * wait for a quiet gap (700ms) ends 700ms after the fall, not after the
 * reading that follows it.  The readings before the fall are before the
 * 32-bit wrap. */
TEST_CASE(a_pulse_ending_on_tick_zero_starts_the_quiet_at_the_fall)
{
    esc_stick_timing_t t;
    esc_stick_timing_defaults(&t);
    CHECK_EQ(t.group_gap_ms, 700u);
    esc_det_init(&det, &t, 0u);
    uint32_t at = 0u - 21u * 50u;
    (void)esc_det_reading(&det, at, 150, false);
    esc_det_count(&det);
    for (int i = 0; i < 15; ++i) {
        at += 50u;
        (void)esc_det_reading(&det, at, 150, false);
    }
    for (int i = 0; i < 5; ++i) {
        at += 50u;
        (void)esc_det_reading(&det, at, 750, false);
    }
    at += 50u;
    CHECK_EQ(at, 0u);                       /* the first low reading */
    CHECK_EQ(esc_det_reading(&det, at, 150, false), ESC_DET_PULSE);
    CHECK_EQ(det.fall_ms, 0u);
    esc_det_count(&det);
    CHECK(det.quiet_known);
    CHECK_EQ(det.quiet_from, 0u);
    for (int i = 0; i < 13; ++i) {
        at += 50u;
        (void)esc_det_reading(&det, at, 150, false);
    }
    CHECK(det.wait_quiet);                  /* 650ms after the fall */
    at += 50u;
    (void)esc_det_reading(&det, at, 150, false);
    CHECK(!det.wait_quiet);                 /* 700ms after it */
}

/* The floor while the ESC starts is the lowest current seen, so a tone in
 * the entry does not lift it. */
TEST_CASE(the_floor_is_the_lowest_current_before_counting)
{
    esc_stick_timing_t t;
    esc_stick_timing_defaults(&t);
    esc_det_init(&det, &t, 0u);
    (void)esc_det_reading(&det, 0u, 700, false);
    (void)esc_det_reading(&det, 50u, 160, false);
    (void)esc_det_reading(&det, 100u, 900, false);
    CHECK_EQ(esc_det_floor_ma(&det), 160);
    CHECK(esc_det_reading(&det, 150u, 900, false) == ESC_DET_NONE);
    CHECK(!det.group_open);
}

int main(void)
{
    RUN(beeps_make_a_group_that_silence_ends);
    RUN(long_beeps_count_by_the_profiles_measure);
    RUN(a_long_beep_where_none_belongs_spoils_the_group);
    RUN(pulses_and_gaps_outside_their_lengths_spoil_the_group);
    RUN(hysteresis_holds_a_beep_through_a_dip);
    RUN(a_current_that_stays_high_is_reported_once);
    RUN(a_pulse_ending_on_tick_zero_starts_the_quiet_at_the_fall);
    RUN(the_floor_is_the_lowest_current_before_counting);
    return test_summary("esc_det");
}
