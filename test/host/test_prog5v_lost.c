/*
 * The PROG 5V watch (prog5v_lost.h): two new INA3221 windows in a row under
 * 3000mV, both read 500ms or more after the panel's ON, trip once an ON.
 *
 * The edges: 2999mV and 3000mV, one low window and a good one, a window read
 * twice, windows before the settle time and at exactly 500ms, an ON the
 * IOmcu has not confirmed, an invalid reading between two low windows, one
 * trip an ON and none until the ON goes and comes back, an OFF asked under
 * an ON the page still reads on (the cuts), the window number's and the
 * clock's wrap, and the console line.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <string.h>

#include "greatest.h"

#include "prog5v_lost.h"

/* The ON's time and the time a window first counts. */
#define T_ON 1000u
#define T_OK (T_ON + PROG5V_LOST_SETTLE_MS)
/* A latched M7 reads near 0V; a loaded one a few hundred mV under 5V. */
#define MV_DEAD 120u
#define MV_GOOD 4800u

static prog5v_lost_t d;

/**
 * @brief One pass with window @p n reading @p mv.
 * @param on   the panel's ON stands
 * @param cuts the PROG_SUPPLY cuts
 * @param t    the time, ms
 * @param n    the window's number
 * @param mv   its mean bus voltage, mV
 * @return the step's trip
 */
static bool win(bool on, uint32_t cuts, uint32_t t, uint16_t n, uint16_t mv)
{
    const prog5v_window_t w = { true, mv, n };
    return prog5v_lost_step(&d, on, cuts, t, &w);
}

/** @brief The watch at the start, the ON confirmed at T_ON with cuts 3. */
static void on_at_t_on(void)
{
    prog5v_lost_init(&d);
    CHECK(!prog5v_lost_step(&d, false, 3u, T_ON - 100u, NULL));
    CHECK(!prog5v_lost_step(&d, true, 3u, T_ON, NULL));
}

/* ------------------------------------------------------------- the trip */

TEST_CASE(two_new_windows_under_3000mv_trip)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, 2999u));
    CHECK(win(true, 3u, T_OK + 40u, 11u, 2999u));
    CHECK_EQ(prog5v_lost_mv(&d), 2999u);
    CHECK_EQ(d.trip_ms, PROG5V_LOST_SETTLE_MS + 40u);
}

TEST_CASE(windows_at_3000mv_do_not_trip)
{
    on_at_t_on();
    for (uint16_t n = 10u; n < 40u; ++n) {
        CHECK(!win(true, 3u, T_OK + 40u * n, n, 3000u));
    }
    CHECK_EQ(prog5v_lost_mv(&d), 0u);
}

TEST_CASE(one_low_window_then_a_good_one_does_not_trip)
{
    on_at_t_on();
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
    on_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 40u, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 80u, 10u, MV_DEAD));
    CHECK(win(true, 3u, T_OK + 120u, 11u, MV_DEAD));
}

/* A window that is read again after a good one does not count again. */
TEST_CASE(a_window_read_again_does_not_reset_the_count)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK + 20u, 10u, MV_GOOD));   /* same number */
    CHECK(win(true, 3u, T_OK + 40u, 11u, MV_DEAD));
}

/* ------------------------------------------------------ the settle time */

TEST_CASE(windows_before_the_settle_time_do_not_count)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_ON + 1u, 1u, MV_DEAD));
    CHECK(!win(true, 3u, T_ON + 200u, 2u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK - 1u, 3u, MV_DEAD));
    /* The first one at the settle time counts alone. */
    CHECK(!win(true, 3u, T_OK, 4u, MV_DEAD));
    CHECK(win(true, 3u, T_OK + 40u, 5u, MV_DEAD));
}

TEST_CASE(the_first_window_counts_at_exactly_500ms)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_OK, 4u, MV_DEAD));
    CHECK(win(true, 3u, T_OK, 5u, MV_DEAD));
}

TEST_CASE(an_on_not_confirmed_does_not_trip)
{
    prog5v_lost_init(&d);
    for (uint16_t n = 1u; n < 40u; ++n) {
        CHECK(!win(false, 3u, T_ON + 40u * n, n, 0u));
    }
    /* Confirmed now: the settle time starts here, not at the first pass. */
    const uint32_t t = T_ON + 40u * 40u;
    CHECK(!win(true, 3u, t, 40u, 0u));
    CHECK(!win(true, 3u, t + PROG5V_LOST_SETTLE_MS - 1u, 41u, 0u));
    CHECK(!win(true, 3u, t + PROG5V_LOST_SETTLE_MS, 42u, 0u));
    CHECK(win(true, 3u, t + PROG5V_LOST_SETTLE_MS + 40u, 43u, 0u));
}

/* An OFF asked under the ON -- an error's, the operator's -- moves the cuts;
 * the page reads the pin on until its next read, and the output falls. */
TEST_CASE(an_off_asked_under_the_on_restarts_the_settle_time)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_GOOD));
    const uint32_t t = T_OK + 1000u;
    CHECK(!win(true, 4u, t, 11u, MV_DEAD));
    CHECK(!win(true, 4u, t + 40u, 12u, MV_DEAD));
    CHECK(!win(true, 4u, t + PROG5V_LOST_SETTLE_MS - 1u, 13u, MV_DEAD));
    /* The page still reads on after the settle time: counted again. */
    CHECK(!win(true, 4u, t + PROG5V_LOST_SETTLE_MS, 14u, MV_DEAD));
    CHECK(win(true, 4u, t + PROG5V_LOST_SETTLE_MS + 40u, 15u, MV_DEAD));
}

/* A low window counted before the OFF does not count after it, even with
 * the page holding that window past the new settle time. */
TEST_CASE(a_window_counted_before_an_off_asked_is_dropped)
{
    on_at_t_on();
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
    on_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_DEAD));
    const prog5v_window_t bad = { false, MV_DEAD, 11u };
    CHECK(!prog5v_lost_step(&d, true, 3u, T_OK + 40u, &bad));
    CHECK(!win(true, 3u, T_OK + 80u, 12u, MV_DEAD));
    CHECK(!prog5v_lost_step(&d, true, 3u, T_OK + 120u, NULL));
    CHECK(!win(true, 3u, T_OK + 160u, 13u, MV_DEAD));
    CHECK(win(true, 3u, T_OK + 200u, 14u, MV_DEAD));
}

/* The window number wraps at 16 bits; a wrapped number is a new window. */
TEST_CASE(the_window_number_wraps)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_OK, 0xFFFFu, MV_DEAD));
    CHECK(win(true, 3u, T_OK + 40u, 0u, MV_DEAD));
}

/* A window read while off and read again after the ON is not new. */
TEST_CASE(a_window_from_before_the_on_is_not_new)
{
    prog5v_lost_init(&d);
    CHECK(!win(false, 3u, T_ON - 40u, 9u, MV_DEAD));
    CHECK(!win(true, 3u, T_ON, 9u, MV_DEAD));
    CHECK(!win(true, 3u, T_OK, 9u, MV_DEAD));       /* stale page */
    CHECK(!win(true, 3u, T_OK + 40u, 10u, MV_DEAD));
    CHECK(win(true, 3u, T_OK + 80u, 11u, MV_DEAD));
}

/* ------------------------------------------------- once an ON, and again */

TEST_CASE(one_trip_an_on_until_the_on_goes_and_comes_back)
{
    on_at_t_on();
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
    /* Off, and on again: a new ON, its own settle time and trip. */
    CHECK(!win(false, 4u, t, 60u, 0u));
    CHECK_EQ(prog5v_lost_mv(&d), 0u);
    t += 1000u;
    CHECK(!win(true, 4u, t, 61u, MV_GOOD));
    CHECK(!win(true, 4u, t + PROG5V_LOST_SETTLE_MS - 1u, 62u, MV_DEAD));
    CHECK(!win(true, 4u, t + PROG5V_LOST_SETTLE_MS, 63u, MV_DEAD));
    CHECK(win(true, 4u, t + PROG5V_LOST_SETTLE_MS + 40u, 64u, MV_DEAD));
}

/* ------------------------------------------------------------ the clock */

TEST_CASE(the_settle_time_holds_across_the_clocks_wrap)
{
    const uint32_t on = 0xFFFFFF00u;
    prog5v_lost_init(&d);
    CHECK(!win(true, 1u, on, 1u, MV_GOOD));
    CHECK(!win(true, 1u, on + PROG5V_LOST_SETTLE_MS - 1u, 2u, MV_DEAD));
    CHECK(!win(true, 1u, on + PROG5V_LOST_SETTLE_MS, 3u, MV_DEAD));
    CHECK(win(true, 1u, on + PROG5V_LOST_SETTLE_MS + 40u, 4u, MV_DEAD));
}

/* An ON held for 2^32ms: the time since it reads short again, and the
 * watch stays settled. */
TEST_CASE(an_on_held_past_the_clocks_period_stays_settled)
{
    on_at_t_on();
    CHECK(!win(true, 3u, T_OK, 10u, MV_GOOD));
    CHECK(!win(true, 3u, T_ON + 10u, 11u, MV_DEAD));   /* 2^32ms later */
    CHECK(win(true, 3u, T_ON + 50u, 12u, MV_DEAD));
}

/* ------------------------------------------------------- the console line */

TEST_CASE(the_console_line_names_the_part_the_channel_and_the_readings)
{
    char buf[112];
    on_at_t_on();
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

TEST_CASE(nothing_is_done_without_a_state)
{
    const prog5v_window_t w = { true, 0u, 1u };
    prog5v_lost_init(NULL);
    CHECK(!prog5v_lost_step(NULL, true, 0u, T_OK, &w));
    CHECK_EQ(prog5v_lost_mv(NULL), 0u);
}

int main(void)
{
    RUN(two_new_windows_under_3000mv_trip);
    RUN(windows_at_3000mv_do_not_trip);
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
    RUN(the_settle_time_holds_across_the_clocks_wrap);
    RUN(an_on_held_past_the_clocks_period_stays_settled);
    RUN(the_console_line_names_the_part_the_channel_and_the_readings);
    RUN(nothing_is_done_without_a_state);
    return test_summary("prog5v_lost");
}
