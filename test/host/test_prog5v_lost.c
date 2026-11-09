/*
 * The PROG 5V watch (prog5v_lost.h).  MISSING: no window of 4750mV or more
 * by 200ms after the ON, decided by the first new window read at or after
 * 200ms.  LOST: once the 5V came, two new windows in a row under 3000mV,
 * both read 500ms or more after the ON.  One trip an ON; while a
 * programming port holds the 5V a finding waits for a page read that still
 * reads the pin on.
 *
 * The edges: 4749mV and 4750mV for MISSING, 2999mV and 3000mV for LOST,
 * 4.5V under load after the 5V came, a window at 199ms and at 200ms,
 * 5.0V reaching and 3.1V staying, one low window and a good one, a window
 * read twice, windows before the settle time and at exactly 500ms, an ON
 * the IOmcu has not confirmed, an invalid reading between two low windows,
 * one trip an ON and none until the ON goes and comes back, an OFF asked
 * under an ON the page still reads on (the cuts), a port's finding
 * confirmed, ended by a read of the pin off and dropped by a good window,
 * also one read in the same step as the confirming page read,
 * the window number's and the clock's wrap, and the console lines.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <string.h>

#include "greatest.h"

#include "prog5v_lost.h"

/* The ON's time, the time MISSING decides and the time a window first
 * counts for LOST. */
#define T_ON    1000u
#define T_REACH (T_ON + PROG5V_REACH_MS)
#define T_OK    (T_ON + PROG5V_LOST_SETTLE_MS)
/* A latched M7 reads near 0V; M7 on a 5V input about 3.1V; a loaded one a
 * few tens of mV under 5V. */
#define MV_DEAD  120u
#define MV_SHORT 3100u
#define MV_GOOD  4980u

static prog5v_lost_t d;
/* The page as the step reads it. */
static prog5v_on_t g_on;

/**
 * @brief One pass with window @p n reading @p mv.
 * @param on   the ON stands
 * @param cuts the PROG_SUPPLY cuts
 * @param t    the time, ms
 * @param n    the window's number
 * @param mv   its mean bus voltage, mV
 * @return the step's trip
 */
static bool win(bool on, uint32_t cuts, uint32_t t, uint16_t n, uint16_t mv)
{
    const prog5v_window_t w = { true, mv, n };
    g_on.on   = on;
    g_on.cuts = cuts;
    return prog5v_lost_step(&d, &g_on, t, &w);
}

/**
 * @brief One pass with no window.
 * @param on   the ON stands
 * @param cuts the PROG_SUPPLY cuts
 * @param t    the time, ms
 * @return the step's trip
 */
static bool bare(bool on, uint32_t cuts, uint32_t t)
{
    g_on.on   = on;
    g_on.cuts = cuts;
    return prog5v_lost_step(&d, &g_on, t, NULL);
}

/** @brief The watch at the start, the panel's ON confirmed at T_ON with
 *         cuts 3. */
static void on_at_t_on(void)
{
    memset(&g_on, 0, sizeof(g_on));
    prog5v_lost_init(&d);
    CHECK(!bare(false, 3u, T_ON - 100u));
    CHECK(!bare(true, 3u, T_ON));
}

/** @brief on_at_t_on(), and window 5 at 5V read 100ms after the ON. */
static void reached_at_t_on(void)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_ON + 100u, 5u, MV_GOOD));
    CHECK(d.reached);
}

/* ------------------------------------------------------------- MISSING */

TEST_CASE(the_5v_reaching_5v_passes)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_ON + 40u, 1u, 0u));       /* before the ON */
    CHECK(!win(true, 3u, T_ON + 80u, 2u, 2500u));    /* the ramp */
    CHECK(!win(true, 3u, T_ON + 120u, 3u, 5000u));
    for (uint16_t n = 4u; n < 40u; ++n) {
        CHECK(!win(true, 3u, T_ON + 40u * n, n, 5000u));
    }
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_NONE);
}

TEST_CASE(the_5v_staying_at_3v1_is_missing)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_ON + 40u, 1u, MV_SHORT));
    CHECK(!win(true, 3u, T_ON + 80u, 2u, MV_SHORT));
    CHECK(!win(true, 3u, T_ON + 120u, 3u, MV_SHORT));
    CHECK(!win(true, 3u, T_ON + 160u, 4u, MV_SHORT));
    CHECK(win(true, 3u, T_REACH, 5u, MV_SHORT));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_MISSING);
    CHECK_EQ(prog5v_lost_mv(&d), MV_SHORT);
    CHECK_EQ(d.trip_ms, PROG5V_REACH_MS);
}

TEST_CASE(a_window_at_199ms_does_not_decide)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_REACH - 1u, 4u, MV_SHORT));
    /* The same window read again at 200ms is not new. */
    CHECK(!win(true, 3u, T_REACH, 4u, MV_SHORT));
    CHECK(win(true, 3u, T_REACH + 40u, 5u, MV_SHORT));
}

TEST_CASE(missing_edges_4749mv_and_4750mv)
{
    on_at_t_on();
    CHECK(win(true, 3u, T_REACH, 5u, PROG5V_MIN_MV - 1u));
    on_at_t_on();
    CHECK(!win(true, 3u, T_REACH, 5u, PROG5V_MIN_MV));
    CHECK(d.reached);
}

/* A good window late, at 200ms, still counts: the first new window at or
 * after 200ms decides either way. */
TEST_CASE(a_good_window_at_200ms_passes)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_ON + 160u, 4u, MV_DEAD));
    CHECK(!win(true, 3u, T_REACH, 5u, MV_GOOD));
    CHECK(!win(true, 3u, T_REACH + 40u, 6u, MV_GOOD));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_NONE);
}

TEST_CASE(no_window_read_is_no_finding)
{
    on_at_t_on();
    for (uint32_t t = T_ON; t < T_ON + 2000u; t += 40u) {
        CHECK(!bare(true, 3u, t));
    }
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_NONE);
}

/* The panel's OFF asked before the 5V read good: the page reads the pin on
 * until its next read, so MISSING waits for the settle time as well. */
TEST_CASE(an_off_asked_before_the_5v_came_holds_missing_back)
{
    on_at_t_on();
    CHECK(!win(true, 4u, T_ON + 100u, 3u, MV_DEAD));
    CHECK(!win(true, 4u, T_REACH, 4u, MV_DEAD));
    CHECK(!win(true, 4u, T_ON + 100u + PROG5V_LOST_SETTLE_MS - 1u, 5u,
               MV_DEAD));
    /* Off as the page reads it now: nothing. */
    CHECK(!win(false, 4u, T_ON + 100u + PROG5V_LOST_SETTLE_MS, 6u, MV_DEAD));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_NONE);
    /* A page that still reads on after the settle time: missing. */
    on_at_t_on();
    CHECK(!win(true, 4u, T_ON + 100u, 3u, MV_DEAD));
    CHECK(win(true, 4u, T_ON + 100u + PROG5V_LOST_SETTLE_MS, 7u, MV_DEAD));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_MISSING);
}

/* ---------------------------------------------------------------- LOST */

TEST_CASE(two_new_windows_under_3000mv_trip)
{
    reached_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, PROG5V_LOST_MV - 1u));
    CHECK(win(true, 3u, T_OK + 40u, 11u, PROG5V_LOST_MV - 1u));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_LOST);
    CHECK_EQ(prog5v_lost_mv(&d), PROG5V_LOST_MV - 1u);
    CHECK_EQ(d.trip_ms, PROG5V_LOST_SETTLE_MS + 40u);
}

TEST_CASE(windows_at_3000mv_do_not_trip)
{
    reached_at_t_on();
    for (uint16_t n = 10u; n < 40u; ++n) {
        CHECK(!win(true, 3u, T_OK + 40u * n, n, PROG5V_LOST_MV));
    }
    CHECK_EQ(prog5v_lost_mv(&d), 0u);
}

/* The 5V came at 5.0V; a servo's load then pulls the reading to 4.5V for
 * many windows: no trip, the run goes on. */
TEST_CASE(a_5v_at_4v5_under_load_after_it_came_does_not_trip)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_ON + 100u, 5u, 5000u));
    for (uint16_t n = 6u; n < 80u; ++n) {
        CHECK(!win(true, 3u, T_ON + 40u * n, n, 4500u));
    }
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_NONE);
    /* Under a session too: nothing waits. */
    on_at_t_on();
    g_on.port  = true;
    g_on.reads = 7u;
    CHECK(!win(true, 3u, T_ON + 100u, 5u, 5000u));
    for (uint16_t n = 6u; n < 80u; ++n) {
        g_on.reads += (n % 5u == 0u) ? 1u : 0u;
        CHECK(!win(true, 3u, T_ON + 40u * n, n, 4500u));
    }
    CHECK(!d.waits);
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_NONE);
}

TEST_CASE(one_low_window_then_a_good_one_does_not_trip)
{
    reached_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 40u, 11u, MV_GOOD));
    CHECK(!win(true, 3u, T_OK + 80u, 12u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 120u, 13u, MV_GOOD));
    /* The count starts again: two in a row trip. */
    CHECK(!win(true, 3u, T_OK + 160u, 14u, MV_DEAD));
    CHECK(win(true, 3u, T_OK + 200u, 15u, MV_DEAD));
}

TEST_CASE(the_same_window_read_twice_counts_once)
{
    reached_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 40u, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 80u, 10u, MV_DEAD));
    CHECK(win(true, 3u, T_OK + 120u, 11u, MV_DEAD));
}

/* A window that is read again after a good one does not count again. */
TEST_CASE(a_window_read_again_does_not_reset_the_count)
{
    reached_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 20u, 10u, MV_GOOD));   /* same number */
    CHECK(win(true, 3u, T_OK + 40u, 11u, MV_DEAD));
}

TEST_CASE(windows_before_the_settle_time_do_not_count)
{
    reached_at_t_on();
    CHECK(!win(true, 3u, T_ON + 200u, 6u, MV_DEAD));
    CHECK(!win(true, 3u, T_ON + 240u, 7u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK - 1u, 8u, MV_DEAD));
    /* The first one at the settle time counts alone. */
    CHECK(!win(true, 3u, T_OK, 9u, MV_DEAD));
    CHECK(win(true, 3u, T_OK + 40u, 10u, MV_DEAD));
}

TEST_CASE(the_first_window_counts_at_exactly_500ms)
{
    reached_at_t_on();
    CHECK(!win(true, 3u, T_OK, 9u, MV_DEAD));
    CHECK(win(true, 3u, T_OK, 10u, MV_DEAD));
}

TEST_CASE(an_on_not_confirmed_does_not_trip)
{
    memset(&g_on, 0, sizeof(g_on));
    prog5v_lost_init(&d);
    for (uint16_t n = 1u; n < 40u; ++n) {
        CHECK(!win(false, 3u, T_ON + 40u * n, n, 0u));
    }
    /* Confirmed now: the 200ms start here, not at the first pass. */
    const uint32_t t = T_ON + 40u * 40u;
    CHECK(!win(true, 3u, t, 40u, 0u));
    CHECK(!win(true, 3u, t + PROG5V_REACH_MS - 1u, 41u, 0u));
    CHECK(win(true, 3u, t + PROG5V_REACH_MS, 42u, 0u));
}

/* An OFF asked under the ON -- an error's, the operator's -- moves the cuts;
 * the page reads the pin on until its next read, and the output falls. */
TEST_CASE(an_off_asked_under_the_on_restarts_the_settle_time)
{
    reached_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_GOOD));
    const uint32_t t = T_OK + 1000u;
    CHECK(!win(true, 4u, t, 11u, MV_DEAD));
    CHECK(!win(true, 4u, t + 40u, 12u, MV_DEAD));
    CHECK(!win(true, 4u, t + PROG5V_LOST_SETTLE_MS - 1u, 13u, MV_DEAD));
    /* The page still reads on after the settle time: counted again. */
    CHECK(!win(true, 4u, t + PROG5V_LOST_SETTLE_MS, 14u, MV_DEAD));
    CHECK(win(true, 4u, t + PROG5V_LOST_SETTLE_MS + 40u, 15u, MV_DEAD));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_LOST);
}

/* A low window counted before the OFF does not count after it, even with
 * the page holding that window past the new settle time. */
TEST_CASE(a_window_counted_before_an_off_asked_is_dropped)
{
    reached_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));      /* one counted */
    const uint32_t t = T_OK + 40u;
    CHECK(!win(true, 4u, t, 10u, MV_DEAD));
    CHECK(!win(true, 4u, t + PROG5V_LOST_SETTLE_MS, 10u, MV_DEAD));
    CHECK(!win(true, 4u, t + PROG5V_LOST_SETTLE_MS + 40u, 11u, MV_DEAD));
    CHECK(win(true, 4u, t + PROG5V_LOST_SETTLE_MS + 80u, 12u, MV_DEAD));
}

/* ------------------------------------------------------- the reading */

TEST_CASE(an_invalid_reading_drops_the_count)
{
    reached_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    const prog5v_window_t bad = { false, MV_DEAD, 11u };
    g_on.on = true;
    CHECK(!prog5v_lost_step(&d, &g_on, T_OK + 40u, &bad));
    CHECK(!win(true, 3u, T_OK + 80u, 12u, MV_DEAD));
    CHECK(!bare(true, 3u, T_OK + 120u));
    CHECK(!win(true, 3u, T_OK + 160u, 13u, MV_DEAD));
    CHECK(win(true, 3u, T_OK + 200u, 14u, MV_DEAD));
}

/* The window number wraps at 16 bits; a wrapped number is a new window. */
TEST_CASE(the_window_number_wraps)
{
    reached_at_t_on();
    CHECK(!win(true, 3u, T_OK, 0xFFFFu, MV_DEAD));
    CHECK(win(true, 3u, T_OK + 40u, 0u, MV_DEAD));
}

/* A window read while off and read again after the ON is not new. */
TEST_CASE(a_window_from_before_the_on_is_not_new)
{
    memset(&g_on, 0, sizeof(g_on));
    prog5v_lost_init(&d);
    CHECK(!win(false, 3u, T_ON - 40u, 9u, MV_DEAD));
    CHECK(!win(true, 3u, T_ON, 9u, MV_DEAD));
    CHECK(!win(true, 3u, T_REACH, 9u, MV_DEAD));     /* stale page */
    CHECK(win(true, 3u, T_REACH + 40u, 10u, MV_DEAD));
}

/* ------------------------------------------------- once an ON, and again */

TEST_CASE(one_trip_an_on_until_the_on_goes_and_comes_back)
{
    reached_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    CHECK(win(true, 3u, T_OK + 40u, 11u, MV_DEAD));
    /* Still on, the windows still low: nothing more. */
    for (uint16_t n = 12u; n < 30u; ++n) {
        CHECK(!win(true, 3u, T_OK + 40u * n, n, MV_DEAD));
    }
    CHECK_EQ(prog5v_lost_mv(&d), MV_DEAD);
    /* The trip's own OFF moves the cuts; still nothing on the same ON. */
    uint32_t t = T_OK + 40u * 30u;
    for (uint16_t n = 30u; n < 60u; ++n) {
        CHECK(!win(true, 4u, t, n, MV_DEAD));
        t += 40u;
    }
    /* Off, and on again: a new ON, missing on its own. */
    CHECK(!win(false, 4u, t, 60u, 0u));
    CHECK_EQ(prog5v_lost_mv(&d), 0u);
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_NONE);
    t += 1000u;
    CHECK(!win(true, 4u, t, 61u, MV_DEAD));
    CHECK(!win(true, 4u, t + PROG5V_REACH_MS - 1u, 62u, MV_DEAD));
    CHECK(win(true, 4u, t + PROG5V_REACH_MS, 63u, MV_DEAD));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_MISSING);
}

/* ------------------------------------------------ a port holding the 5V */

/** @brief A port's session holds the 5V and the page read it on: reads 7. */
static void port_on(void)
{
    on_at_t_on();
    g_on.port  = true;
    g_on.reads = 7u;
}

TEST_CASE(a_ports_missing_5v_waits_for_a_page_read_that_reads_on)
{
    port_on();
    CHECK(!win(true, 3u, T_REACH, 5u, MV_SHORT));
    CHECK(d.waits);
    CHECK(!win(true, 3u, T_REACH + 40u, 6u, MV_SHORT));
    CHECK(!win(true, 3u, T_REACH + 80u, 7u, MV_SHORT));
    g_on.reads = 8u;                          /* the page read: still on */
    CHECK(win(true, 3u, T_REACH + 120u, 8u, MV_SHORT));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_MISSING);
    CHECK_EQ(prog5v_lost_mv(&d), MV_SHORT);
}

/* The session switched the 5V off for its cycle: the next read shows it. */
TEST_CASE(a_ports_finding_ends_with_a_read_of_the_pin_off)
{
    port_on();
    CHECK(!win(true, 3u, T_ON + 100u, 3u, MV_GOOD));
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 40u, 11u, MV_DEAD));
    CHECK(d.waits);
    CHECK(!win(false, 3u, T_OK + 80u, 12u, MV_DEAD));
    CHECK(!d.waits);
    /* On again, and it comes: no trip. */
    g_on.reads = 9u;
    CHECK(!win(true, 3u, T_OK + 1500u, 40u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 1540u, 41u, MV_GOOD));
    g_on.reads = 10u;
    CHECK(!win(true, 3u, T_OK + 1700u, 42u, MV_GOOD));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_NONE);
}

/* A cycle shorter than a page read: the 5V back before the read. */
TEST_CASE(a_ports_finding_drops_on_a_good_window)
{
    port_on();
    CHECK(!win(true, 3u, T_ON + 100u, 3u, MV_GOOD));
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 40u, 11u, MV_DEAD));
    CHECK(d.waits);
    CHECK(!win(true, 3u, T_OK + 80u, 12u, MV_GOOD));
    CHECK(!d.waits);
    g_on.reads = 8u;
    CHECK(!win(true, 3u, T_OK + 120u, 13u, MV_GOOD));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_NONE);
}

TEST_CASE(a_ports_lost_5v_trips_at_the_next_read)
{
    port_on();
    CHECK(!win(true, 3u, T_ON + 100u, 3u, MV_GOOD));
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 40u, 11u, MV_DEAD));
    /* The page read, and the same window read again: confirmed. */
    g_on.reads = 8u;
    CHECK(win(true, 3u, T_OK + 60u, 11u, MV_DEAD));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_LOST);
}

/* The panel reads PROG_SUPPLY before SENSE_WIN in a pass: the page read
 * that would confirm a waiting finding and a window that has recovered
 * come in the same step.  The window goes first: no trip.  With a low
 * window in that step instead, the read confirms it. */
TEST_CASE(a_ports_finding_and_a_recovered_window_in_one_step_do_not_trip)
{
    port_on();
    CHECK(!win(true, 3u, T_ON + 100u, 3u, MV_GOOD));
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 40u, 11u, MV_DEAD));
    CHECK(d.waits);
    g_on.reads = 8u;
    CHECK(!win(true, 3u, T_OK + 80u, 12u, MV_GOOD));
    CHECK(!d.waits);
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_NONE);
    /* MISSING the same way: a 5V that comes in the confirming step. */
    port_on();
    CHECK(!win(true, 3u, T_REACH, 5u, MV_SHORT));
    CHECK(d.waits);
    g_on.reads = 8u;
    CHECK(!win(true, 3u, T_REACH + 40u, 6u, MV_GOOD));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_NONE);
}

TEST_CASE(a_ports_finding_and_a_low_window_in_one_step_trip)
{
    port_on();
    CHECK(!win(true, 3u, T_ON + 100u, 3u, MV_GOOD));
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 40u, 11u, MV_DEAD));
    g_on.reads = 8u;
    CHECK(win(true, 3u, T_OK + 80u, 12u, MV_DEAD));
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_LOST);
    CHECK_EQ(prog5v_lost_mv(&d), MV_DEAD);
}

/* No reading in the confirming step: the finding goes, as for an invalid
 * reading; nothing is confirmed blind. */
TEST_CASE(a_ports_finding_without_a_reading_is_dropped)
{
    port_on();
    CHECK(!win(true, 3u, T_ON + 100u, 3u, MV_GOOD));
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 40u, 11u, MV_DEAD));
    g_on.reads = 8u;
    CHECK(!bare(true, 3u, T_OK + 60u));
    CHECK(!d.waits);
    CHECK_EQ(prog5v_lost_kind(&d), PROG5V_TRIP_NONE);
}

/* ------------------------------------------------------------ the clock */

TEST_CASE(the_settle_time_holds_across_the_clocks_wrap)
{
    const uint32_t on = 0xFFFFFF00u;
    memset(&g_on, 0, sizeof(g_on));
    prog5v_lost_init(&d);
    CHECK(!win(true, 1u, on, 1u, MV_GOOD));
    CHECK(!win(true, 1u, on + PROG5V_LOST_SETTLE_MS - 1u, 2u, MV_DEAD));
    CHECK(!win(true, 1u, on + PROG5V_LOST_SETTLE_MS, 3u, MV_DEAD));
    CHECK(win(true, 1u, on + PROG5V_LOST_SETTLE_MS + 40u, 4u, MV_DEAD));
}

TEST_CASE(the_reach_time_holds_across_the_clocks_wrap)
{
    const uint32_t on = 0xFFFFFFA0u;
    memset(&g_on, 0, sizeof(g_on));
    prog5v_lost_init(&d);
    CHECK(!bare(true, 1u, on));
    CHECK(!win(true, 1u, on + PROG5V_REACH_MS - 1u, 2u, MV_SHORT));
    CHECK(win(true, 1u, on + PROG5V_REACH_MS, 3u, MV_SHORT));
}

/* An ON held for 2^32ms: the time since it reads short again, and the
 * watch stays settled. */
TEST_CASE(an_on_held_past_the_clocks_period_stays_settled)
{
    reached_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_GOOD));
    CHECK(!win(true, 3u, T_ON + 10u, 11u, MV_DEAD));   /* 2^32ms later */
    CHECK(win(true, 3u, T_ON + 50u, 12u, MV_DEAD));
}

/* ------------------------------------------------------- the console line */

TEST_CASE(the_console_line_names_the_part_the_channel_and_the_readings)
{
    char buf[112];
    reached_at_t_on();
    CHECK(!win(true, 3u, T_OK + 100u, 10u, 120u));
    CHECK(win(true, 3u, T_OK + 140u, 11u, 85u));
    CHECK_STR_EQ(prog5v_lost_line(&d, 2u, buf, sizeof(buf)),
                 "PROG 5V lost: INA3221 0x40 CH3 read 0.12V, 0.08V in 2 "
                 "windows, 640ms after ON. All outputs off.");
    CHECK_STR_EQ(prog5v_lost_line(&d, 11u, buf, sizeof(buf)),
                 "PROG 5V lost: INA3221 0x43 CH3 read 0.12V, 0.08V in 2 "
                 "windows, 640ms after ON. All outputs off.");
    /* The longest line fits: 2999mV twice, the most milliseconds. */
    d.mv[0] = 2999u;
    d.mv[1] = 2999u;
    d.trip_ms = 0xFFFFFFFFu;
    const char *s = prog5v_lost_line(&d, 3u, buf, sizeof(buf));
    CHECK(strlen(s) < sizeof(buf) - 1u);
    CHECK(strstr(s, "0x41 CH1 read 2.99V, 2.99V") != NULL);
    CHECK_STR_EQ(prog5v_lost_line(&d, 12u, buf, sizeof(buf)), "");
    CHECK_STR_EQ(prog5v_lost_line(NULL, 2u, buf, sizeof(buf)), "");
    CHECK_STR_EQ(prog5v_lost_line(&d, 2u, NULL, 8u), "");
    CHECK_STR_EQ(prog5v_lost_line(&d, 2u, buf, 0u), "");
    /* A buffer of one byte holds the empty line, cut. */
    buf[0] = 'x';
    CHECK(prog5v_lost_line(&d, 2u, buf, 1u) == buf);
    CHECK_EQ(buf[0], '\0');
}

TEST_CASE(the_console_line_of_missing)
{
    char buf[112];
    on_at_t_on();
    CHECK(win(true, 3u, T_REACH, 5u, MV_SHORT));
    CHECK_STR_EQ(prog5v_lost_line(&d, 2u, buf, sizeof(buf)),
                 "PROG 5V missing: INA3221 0x40 CH3 read 3.10V 200ms after "
                 "ON. All outputs off.");
    d.trip_ms = 0xFFFFFFFFu;
    CHECK(strlen(prog5v_lost_line(&d, 11u, buf, sizeof(buf)))
          < sizeof(buf) - 1u);
}

TEST_CASE(nothing_is_done_without_a_state)
{
    const prog5v_window_t w = { true, 0u, 1u };
    const prog5v_on_t on = { true, false, 0u, 0u };
    prog5v_lost_init(NULL);
    CHECK(!prog5v_lost_step(NULL, &on, T_OK, &w));
    CHECK_EQ(prog5v_lost_mv(NULL), 0u);
    CHECK_EQ(prog5v_lost_kind(NULL), PROG5V_TRIP_NONE);
    /* No ON given: off. */
    prog5v_lost_init(&d);
    CHECK(!prog5v_lost_step(&d, NULL, T_OK, &w));
    CHECK(!d.on);
}

int main(void)
{
    RUN(the_5v_reaching_5v_passes);
    RUN(the_5v_staying_at_3v1_is_missing);
    RUN(a_window_at_199ms_does_not_decide);
    RUN(missing_edges_4749mv_and_4750mv);
    RUN(a_good_window_at_200ms_passes);
    RUN(no_window_read_is_no_finding);
    RUN(an_off_asked_before_the_5v_came_holds_missing_back);
    RUN(two_new_windows_under_3000mv_trip);
    RUN(windows_at_3000mv_do_not_trip);
    RUN(a_5v_at_4v5_under_load_after_it_came_does_not_trip);
    RUN(one_low_window_then_a_good_one_does_not_trip);
    RUN(the_same_window_read_twice_counts_once);
    RUN(a_window_read_again_does_not_reset_the_count);
    RUN(windows_before_the_settle_time_do_not_count);
    RUN(the_first_window_counts_at_exactly_500ms);
    RUN(an_on_not_confirmed_does_not_trip);
    RUN(an_off_asked_under_the_on_restarts_the_settle_time);
    RUN(a_window_counted_before_an_off_asked_is_dropped);
    RUN(an_invalid_reading_drops_the_count);
    RUN(the_window_number_wraps);
    RUN(a_window_from_before_the_on_is_not_new);
    RUN(one_trip_an_on_until_the_on_goes_and_comes_back);
    RUN(a_ports_missing_5v_waits_for_a_page_read_that_reads_on);
    RUN(a_ports_finding_ends_with_a_read_of_the_pin_off);
    RUN(a_ports_finding_drops_on_a_good_window);
    RUN(a_ports_lost_5v_trips_at_the_next_read);
    RUN(a_ports_finding_and_a_recovered_window_in_one_step_do_not_trip);
    RUN(a_ports_finding_and_a_low_window_in_one_step_trip);
    RUN(a_ports_finding_without_a_reading_is_dropped);
    RUN(the_settle_time_holds_across_the_clocks_wrap);
    RUN(the_reach_time_holds_across_the_clocks_wrap);
    RUN(an_on_held_past_the_clocks_period_stays_settled);
    RUN(the_console_line_names_the_part_the_channel_and_the_readings);
    RUN(the_console_line_of_missing);
    RUN(nothing_is_done_without_a_state);
    return test_summary("prog5v_lost");
}
