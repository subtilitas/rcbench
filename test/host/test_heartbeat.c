/*
 * The safety line, both ends: the generator on the panel and the monitor on
 * the coprocessor.  The monitor does three things a monostable cannot: refuse
 * a line that edges too fast to be a panel, refuse one that has gone quiet,
 * and refuse to trust either again on the strength of a single edge.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "tick_wrap.h"

#include "heartbeat.h"

/* Drive the generator for @p ms of wall clock in 1 ms steps from @p t0,
 * counting the edges it asks for.  Returns the number of edges. */
static int run_gen(heartbeat_gen_t *g, uint32_t t0, uint32_t ms, bool alive)
{
    bool prev = heartbeat_gen_step(g, t0, alive);
    int edges = 0;
    for (uint32_t i = 1; i <= ms; ++i) {
        const bool now = heartbeat_gen_step(g, t0 + i, alive);
        if (now != prev) {
            ++edges;
        }
        prev = now;
    }
    return edges;
}

/* Feed the monitor @p n edges @p gap apart, starting at @p t0.  Returns the
 * time of the last edge. */
static uint32_t feed(heartbeat_mon_t *m, uint32_t t0, uint32_t gap, int n)
{
    uint32_t t = t0;
    for (int i = 0; i < n; ++i) {
        heartbeat_mon_edge(m, t);
        t += gap;
    }
    return t - gap;
}

TEST_CASE(the_generator_starts_low_and_edges_at_the_asked_rate)
{
    heartbeat_gen_t g;
    heartbeat_gen_init(&g);

    /* The first step must not itself be an edge: at t=0 there is no interval
     * behind it, and an immediate edge would hand the monitor a gap measured
     * from a timestamp that means nothing. */
    CHECK_EQ(heartbeat_gen_step(&g, 1000, true), false);

    /* One second at 20 ms a toggle is 50 edges, give or take the boundary. */
    heartbeat_gen_init(&g);
    const int edges = run_gen(&g, 1000, 1000, true);
    CHECK(edges >= 48 && edges <= 51);
}

/*
 * The property that makes it a heartbeat rather than an enable: not alive
 * means low in the same call, not low eventually.
 */
TEST_CASE(withdrawing_alive_drops_the_line_in_the_same_call)
{
    heartbeat_gen_t g;
    heartbeat_gen_init(&g);
    (void)run_gen(&g, 0, 200, true);

    /* Find a moment when the line is high, so that dropping it is visible. */
    uint32_t t = 200;
    while (!heartbeat_gen_step(&g, t, true) && t < 400) {
        ++t;
    }
    CHECK(heartbeat_gen_step(&g, t, true));
    CHECK_EQ(heartbeat_gen_step(&g, t, false), false);
}

/*
 * Coming back is a fresh start, not a resumption.  A generator that kept the
 * pre-stop timestamp would emit its first edge after whatever remained of
 * the period, possibly under the monitor's 4 ms floor, and the recovery
 * would be rejected as noise.
 */
TEST_CASE(recovery_waits_a_full_period_before_the_first_edge)
{
    heartbeat_gen_t g;
    heartbeat_gen_init(&g);
    (void)run_gen(&g, 0, 200, true);

    /* Stop at 219, one millisecond short of a period boundary at 220. */
    CHECK_EQ(heartbeat_gen_step(&g, 219, false), false);

    /* The next period's worth of steps must produce no edge. */
    for (uint32_t i = 0; i < HEARTBEAT_PERIOD_MS; ++i) {
        CHECK_EQ(heartbeat_gen_step(&g, 219 + i, true), false);
    }
    CHECK(heartbeat_gen_step(&g, 219 + HEARTBEAT_PERIOD_MS, true));
}

TEST_CASE(the_monitor_needs_a_run_of_good_intervals_before_it_believes)
{
    heartbeat_mon_t m;
    heartbeat_mon_init(&m);

    /* No edge at all is not alive, however long you ask. */
    CHECK_EQ(heartbeat_mon_alive(&m, 0), false);

    /* One edge is not an interval. */
    heartbeat_mon_edge(&m, 1000);
    CHECK_EQ(heartbeat_mon_alive(&m, 1000), false);

    /* Nor are HEARTBEAT_GOOD_RUN - 1 of them. */
    uint32_t t = 1000;
    for (uint32_t i = 1; i < HEARTBEAT_GOOD_RUN; ++i) {
        t += 20;
        heartbeat_mon_edge(&m, t);
        CHECK_EQ(heartbeat_mon_alive(&m, t), false);
    }

    t += 20;
    heartbeat_mon_edge(&m, t);
    CHECK(heartbeat_mon_alive(&m, t));
}

/*
 * The case the monostable is blind to.  A shorted or ringing line retriggers
 * a monostable perfectly well; only a firmware that knows what period to
 * expect can call it what it is.
 */
TEST_CASE(a_line_edging_too_fast_is_noise_and_not_a_heartbeat)
{
    heartbeat_mon_t m;
    heartbeat_mon_init(&m);

    (void)feed(&m, 0, 1, 400);          /* 1 ms apart: 500 Hz, not a panel */
    CHECK_EQ(heartbeat_mon_alive(&m, 400), false);
    CHECK(m.rejected_fast > 0);
    CHECK_EQ(m.rejected_slow, 0);       /* it was never quiet */
}

/* And noise arriving after the line was trusted takes the trust away in the
 * interval it arrives, rather than being averaged out by the good ones. */
TEST_CASE(noise_after_a_good_run_drops_the_line_at_once)
{
    heartbeat_mon_t m;
    heartbeat_mon_init(&m);
    uint32_t t = feed(&m, 0, 20, 10);
    CHECK(heartbeat_mon_alive(&m, t));

    heartbeat_mon_edge(&m, t + 1);      /* one interval under the floor */
    CHECK_EQ(heartbeat_mon_alive(&m, t + 1), false);
}

TEST_CASE(a_line_that_goes_quiet_dies_without_anything_arriving_to_say_so)
{
    heartbeat_mon_t m;
    heartbeat_mon_init(&m);
    uint32_t t = feed(&m, 0, 20, 10);
    CHECK(heartbeat_mon_alive(&m, t));

    /* Still alive 1 ms inside the window ... */
    CHECK(heartbeat_mon_alive(&m, t + HEARTBEAT_MAX_GAP_MS - 1));
    /* ... and dead at it, with no edge having been delivered either way. */
    CHECK_EQ(heartbeat_mon_alive(&m, t + HEARTBEAT_MAX_GAP_MS), false);
    CHECK(m.rejected_slow > 0);
}

/* Having gone quiet, it does not come back on the first edge that returns. */
TEST_CASE(recovering_from_silence_earns_trust_again_from_scratch)
{
    heartbeat_mon_t m;
    heartbeat_mon_init(&m);
    uint32_t t = feed(&m, 0, 20, 10);
    CHECK(heartbeat_mon_alive(&m, t));

    t += 1000;
    CHECK_EQ(heartbeat_mon_alive(&m, t), false);

    heartbeat_mon_edge(&m, t);
    heartbeat_mon_edge(&m, t + 20);
    CHECK_EQ(heartbeat_mon_alive(&m, t + 20), false);   /* one interval only */

    const uint32_t end = feed(&m, t + 40, 20, (int)HEARTBEAT_GOOD_RUN);
    CHECK(heartbeat_mon_alive(&m, end));
}

/*
 * The real rate, not the requested one.  The generator asks for 20 ms and the
 * render loop delivers 26 to 52 ms, so the monitor accepts what the panel
 * produces, including the slow frames of a bench under load.
 */
TEST_CASE(the_monitor_accepts_the_rate_the_render_loop_really_delivers)
{
    const uint32_t rates[] = { 26, 40, 52, HEARTBEAT_MAX_GAP_MS - 1 };
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        heartbeat_mon_t m;
        heartbeat_mon_init(&m);
        const uint32_t end = feed(&m, 5000, rates[i], 12);
        if (!heartbeat_mon_alive(&m, end)) {
            T_FAIL("%u ms between edges was rejected", (unsigned)rates[i]);
        }
    }
}

/*
 * Sampled before the turnover as well as after it.  A naive comparison and a
 * correct one agree on the far side of the wrap; they differ immediately
 * before the counter turns over, with the deadline on the other side of it.
 */
TEST_CASE(both_ends_survive_the_millisecond_counter_wrapping)
{
    const uint32_t near_top = UINT32_MAX - 30u;

    heartbeat_mon_t m;
    heartbeat_mon_init(&m);
    uint32_t t = feed(&m, near_top - 200u, 20u, 12);
    CHECK(heartbeat_mon_alive(&m, t));

    /* Before the wrap: inside the window, and a naive `now - last > max` with
     * signed arithmetic would already be reading a negative age here. */
    CHECK(heartbeat_mon_alive(&m, (uint32_t)(t + 10u)));
    /* Across it: inside the window, with the clock below the last edge. */
    CHECK(heartbeat_mon_alive(&m, (uint32_t)(t + 40u)));
    /* And past it, the timeout still fires. */
    CHECK_EQ(heartbeat_mon_alive(&m, (uint32_t)(t + HEARTBEAT_MAX_GAP_MS)),
             false);

    /* The generator, stepped across the same turnover, keeps edging. */
    heartbeat_gen_t g;
    heartbeat_gen_init(&g);
    const int edges = run_gen(&g, near_top - 100u, 200u, true);
    CHECK(edges >= 8 && edges <= 11);
}

/* --------------------------------------------------- the window's limits */

/* A monitor that trusts the line, its last edge at @p t0: the edges before
 * it are 20 ms apart and end there, wherever on the clock @p t0 is. */
static void trusted_at(heartbeat_mon_t *m, uint32_t t0)
{
    const uint32_t n = 2u * HEARTBEAT_GOOD_RUN;
    heartbeat_mon_init(m);
    (void)feed(m, (uint32_t)(t0 - n * 20u), 20u, (int)n + 1);
}

/* One edge @p gap after a trusted line's last, with no poll in between:
 * the edge handler alone judges the interval. */
static void one_edge_after(heartbeat_mon_t *m, uint32_t t0, uint32_t gap)
{
    trusted_at(m, t0);
    heartbeat_mon_edge(m, (uint32_t)(t0 + gap));
}

static void floor_case(uint32_t t0)
{
    heartbeat_mon_t m;

    /* The floor itself is a heartbeat ... */
    one_edge_after(&m, t0, HEARTBEAT_MIN_GAP_MS);
    CHECK(heartbeat_mon_alive(&m, (uint32_t)(t0 + HEARTBEAT_MIN_GAP_MS)));
    CHECK_EQ(m.rejected_fast, 0);
    CHECK_EQ(m.rejected_slow, 0);

    /* ... and so is 1 ms above it ... */
    one_edge_after(&m, t0, HEARTBEAT_MIN_GAP_MS + 1u);
    CHECK(heartbeat_mon_alive(&m,
                              (uint32_t)(t0 + HEARTBEAT_MIN_GAP_MS + 1u)));
    CHECK_EQ(m.rejected_fast, 0);

    /* ... and 1 ms under it is noise, counted once as fast. */
    one_edge_after(&m, t0, HEARTBEAT_MIN_GAP_MS - 1u);
    CHECK_EQ(heartbeat_mon_alive(&m,
                                 (uint32_t)(t0 + HEARTBEAT_MIN_GAP_MS - 1u)),
             false);
    CHECK_EQ(m.rejected_fast, 1);
    CHECK_EQ(m.rejected_slow, 0);
}

/* With the clock started 2 ms before the wrap, every gap here ends after
 * it. */
TEST_CASE(an_interval_at_the_floor_is_taken_and_1_ms_under_it_is_not)
{
    at_tick_0_and_before_the_wrap(floor_case, 2u);
}

static void ceiling_case(uint32_t t0)
{
    heartbeat_mon_t m;

    /* 1 ms under the ceiling and the ceiling itself are a heartbeat ... */
    one_edge_after(&m, t0, HEARTBEAT_MAX_GAP_MS - 1u);
    CHECK(heartbeat_mon_alive(&m,
                              (uint32_t)(t0 + HEARTBEAT_MAX_GAP_MS - 1u)));
    CHECK_EQ(m.rejected_slow, 0);

    one_edge_after(&m, t0, HEARTBEAT_MAX_GAP_MS);
    CHECK(heartbeat_mon_alive(&m, (uint32_t)(t0 + HEARTBEAT_MAX_GAP_MS)));
    CHECK_EQ(m.rejected_slow, 0);
    CHECK_EQ(m.rejected_fast, 0);

    /* ... and 1 ms over it is a stall, counted once as slow.  The edge
     * handler says so: no poll has seen the silence. */
    one_edge_after(&m, t0, HEARTBEAT_MAX_GAP_MS + 1u);
    CHECK_EQ(m.alive, false);
    CHECK_EQ(m.good_run, 0);
    CHECK_EQ(heartbeat_mon_alive(&m,
                                 (uint32_t)(t0 + HEARTBEAT_MAX_GAP_MS + 1u)),
             false);
    CHECK_EQ(m.rejected_slow, 1);
    CHECK_EQ(m.rejected_fast, 0);
}

/* The wrap 2 ms into the interval, and 1 ms before the ceiling. */
TEST_CASE(an_interval_at_the_ceiling_is_taken_and_1_ms_over_it_is_not)
{
    at_tick_0_and_before_the_wrap(ceiling_case, 2u);
    at_tick_0_and_before_the_wrap(ceiling_case, HEARTBEAT_MAX_GAP_MS - 1u);
}

/*
 * The edge and the poll disagree at the ceiling by design: an interval of
 * exactly HEARTBEAT_MAX_GAP_MS is a good one to the edge handler, and that
 * long without an edge is silence to the poll.  Which of the two runs first
 * in that millisecond decides, and the poll's verdict is not undone by the
 * edge: it closes one good interval of the HEARTBEAT_GOOD_RUN needed.
 */
static void poll_then_edge_case(uint32_t t0)
{
    heartbeat_mon_t m;
    const uint32_t at = (uint32_t)(t0 + HEARTBEAT_MAX_GAP_MS);

    trusted_at(&m, t0);
    CHECK(heartbeat_mon_alive(&m, (uint32_t)(at - 1u)));
    CHECK_EQ(heartbeat_mon_alive(&m, at), false);
    CHECK_EQ(m.rejected_slow, 1);

    heartbeat_mon_edge(&m, at);
    CHECK_EQ(heartbeat_mon_alive(&m, at), false);
    CHECK_EQ(m.good_run, 1);

    /* HEARTBEAT_GOOD_RUN - 1 more good intervals, and not one fewer. */
    uint32_t t = at;
    for (uint32_t i = 2; i < HEARTBEAT_GOOD_RUN; ++i) {
        t += 20u;
        heartbeat_mon_edge(&m, t);
        CHECK_EQ(heartbeat_mon_alive(&m, t), false);
    }
    t += 20u;
    heartbeat_mon_edge(&m, t);
    CHECK(heartbeat_mon_alive(&m, t));
    CHECK_EQ(m.rejected_slow, 1);
}

TEST_CASE(silence_seen_by_the_poll_is_not_undone_by_an_edge_at_the_ceiling)
{
    at_tick_0_and_before_the_wrap(poll_then_edge_case, 2u);
    at_tick_0_and_before_the_wrap(poll_then_edge_case,
                                  HEARTBEAT_MAX_GAP_MS - 1u);
}

/*
 * A rejected interval takes the whole run with it, fast or slow: the line
 * is trusted again after HEARTBEAT_GOOD_RUN good intervals, and one good
 * interval after the bad one is not that.
 */
static void run_reset_case(uint32_t t0)
{
    const uint32_t bad[] = { HEARTBEAT_MIN_GAP_MS - 1u,
                             HEARTBEAT_MAX_GAP_MS + 1u };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        heartbeat_mon_t m;
        one_edge_after(&m, t0, bad[i]);
        uint32_t t = (uint32_t)(t0 + bad[i]);
        for (uint32_t k = 1; k < HEARTBEAT_GOOD_RUN; ++k) {
            t += 20u;
            heartbeat_mon_edge(&m, t);
            if (heartbeat_mon_alive(&m, t)) {
                T_FAIL("trusted %u good interval(s) after one of %u ms",
                       (unsigned)k, (unsigned)bad[i]);
            }
        }
        t += 20u;
        heartbeat_mon_edge(&m, t);
        CHECK(heartbeat_mon_alive(&m, t));
    }
}

TEST_CASE(one_bad_interval_costs_the_whole_run_of_good_ones)
{
    at_tick_0_and_before_the_wrap(run_reset_case, 2u);
}

/* The run counts to HEARTBEAT_GOOD_RUN and stays there, however long the
 * line has been good: the count cannot wrap. */
TEST_CASE(the_run_of_good_intervals_stops_counting_at_what_trust_needs)
{
    heartbeat_mon_t m;
    heartbeat_mon_init(&m);
    const uint32_t t = feed(&m, 0, 20, 1000);
    CHECK(heartbeat_mon_alive(&m, t));
    CHECK_EQ(m.good_run, HEARTBEAT_GOOD_RUN);
}

/*
 * The slow count is the number of stalls, not the number of polls that
 * found the line dead: a line that never edged counts none, a silence counts
 * one however often it is asked about, and a line that was still earning
 * trust when it went quiet counts as one that had it.
 */
TEST_CASE(a_silence_is_counted_once_and_a_line_never_seen_not_at_all)
{
    heartbeat_mon_t m;
    heartbeat_mon_init(&m);
    for (uint32_t t = 0; t < 1000u; t += 100u) {
        CHECK_EQ(heartbeat_mon_alive(&m, t), false);
    }
    CHECK_EQ(m.rejected_slow, 0);

    trusted_at(&m, 5000u);
    for (uint32_t t = 5000u + HEARTBEAT_MAX_GAP_MS; t < 7000u; t += 50u) {
        CHECK_EQ(heartbeat_mon_alive(&m, t), false);
    }
    CHECK_EQ(m.rejected_slow, 1);

    /* One good interval behind it, and HEARTBEAT_GOOD_RUN - 1 of them. */
    for (uint32_t run = 1; run < HEARTBEAT_GOOD_RUN; ++run) {
        heartbeat_mon_init(&m);
        const uint32_t last = feed(&m, 100u, 20u, (int)run + 1);
        CHECK_EQ(m.good_run, run);
        CHECK_EQ(heartbeat_mon_alive(&m, last + HEARTBEAT_MAX_GAP_MS - 1u),
                 false);
        CHECK_EQ(m.rejected_slow, 0);
        CHECK_EQ(heartbeat_mon_alive(&m, last + HEARTBEAT_MAX_GAP_MS),
                 false);
        CHECK_EQ(m.rejected_slow, 1);
        CHECK_EQ(m.good_run, 0);
        CHECK_EQ(heartbeat_mon_alive(&m, last + 2u * HEARTBEAT_MAX_GAP_MS),
                 false);
        CHECK_EQ(m.rejected_slow, 1);
    }
}

/*
 * A stop taken while the line is high leaves it low for the recovery too:
 * the first step after it returns low, and the first edge, one period
 * later, is a rise.  A generator that kept the level would hand the monitor
 * an edge in the step that resumes.
 */
static void stop_while_high_case(uint32_t t0)
{
    heartbeat_gen_t g;
    heartbeat_gen_init(&g);
    CHECK_EQ(heartbeat_gen_step(&g, t0, true), false);
    CHECK(heartbeat_gen_step(&g, (uint32_t)(t0 + HEARTBEAT_PERIOD_MS), true));

    const uint32_t stop = (uint32_t)(t0 + HEARTBEAT_PERIOD_MS + 1u);
    CHECK_EQ(heartbeat_gen_step(&g, stop, false), false);
    for (uint32_t i = 1; i < 1u + HEARTBEAT_PERIOD_MS; ++i) {
        if (heartbeat_gen_step(&g, (uint32_t)(stop + i), true)) {
            T_FAIL("high %u ms after the stop", (unsigned)i);
        }
    }
    CHECK(heartbeat_gen_step(&g, (uint32_t)(stop + 1u + HEARTBEAT_PERIOD_MS),
                             true));
}

/* The wrap in the first period, and in the period after the stop. */
TEST_CASE(a_stop_while_the_line_is_high_resumes_low)
{
    at_tick_0_and_before_the_wrap(stop_while_high_case, 2u);
    at_tick_0_and_before_the_wrap(stop_while_high_case,
                                  HEARTBEAT_PERIOD_MS + 5u);
}

/* The generator edges at the period and not 1 ms before it, on either side
 * of the wrap. */
static void period_case(uint32_t t0)
{
    heartbeat_gen_t g;
    heartbeat_gen_init(&g);
    CHECK_EQ(heartbeat_gen_step(&g, t0, true), false);
    CHECK_EQ(heartbeat_gen_step(&g, (uint32_t)(t0 + 1u), true), false);
    CHECK_EQ(heartbeat_gen_step(&g, (uint32_t)(t0 + HEARTBEAT_PERIOD_MS - 1u),
                                true), false);
    CHECK(heartbeat_gen_step(&g, (uint32_t)(t0 + HEARTBEAT_PERIOD_MS), true));
    CHECK(heartbeat_gen_step(&g,
                             (uint32_t)(t0 + 2u * HEARTBEAT_PERIOD_MS - 1u),
                             true));
    CHECK_EQ(heartbeat_gen_step(&g, (uint32_t)(t0 + 2u * HEARTBEAT_PERIOD_MS),
                                true), false);
}

TEST_CASE(the_generator_edges_at_the_period_and_not_1_ms_before)
{
    at_tick_0_and_before_the_wrap(period_case, 2u);
    at_tick_0_and_before_the_wrap(period_case, HEARTBEAT_PERIOD_MS + 2u);
}

/* Null arguments are a programming error, but a safety module that faults on
 * one has turned a mistake into an outage. */
TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    heartbeat_gen_init(NULL);
    heartbeat_mon_init(NULL);
    heartbeat_mon_edge(NULL, 0);
    CHECK_EQ(heartbeat_gen_step(NULL, 0, true), false);
    CHECK_EQ(heartbeat_mon_alive(NULL, 0), false);
}

int main(void)
{
    RUN(the_generator_starts_low_and_edges_at_the_asked_rate);
    RUN(withdrawing_alive_drops_the_line_in_the_same_call);
    RUN(recovery_waits_a_full_period_before_the_first_edge);
    RUN(the_monitor_needs_a_run_of_good_intervals_before_it_believes);
    RUN(a_line_edging_too_fast_is_noise_and_not_a_heartbeat);
    RUN(noise_after_a_good_run_drops_the_line_at_once);
    RUN(a_line_that_goes_quiet_dies_without_anything_arriving_to_say_so);
    RUN(recovering_from_silence_earns_trust_again_from_scratch);
    RUN(the_monitor_accepts_the_rate_the_render_loop_really_delivers);
    RUN(both_ends_survive_the_millisecond_counter_wrapping);
    RUN(an_interval_at_the_floor_is_taken_and_1_ms_under_it_is_not);
    RUN(an_interval_at_the_ceiling_is_taken_and_1_ms_over_it_is_not);
    RUN(silence_seen_by_the_poll_is_not_undone_by_an_edge_at_the_ceiling);
    RUN(one_bad_interval_costs_the_whole_run_of_good_ones);
    RUN(the_run_of_good_intervals_stops_counting_at_what_trust_needs);
    RUN(a_silence_is_counted_once_and_a_line_never_seen_not_at_all);
    RUN(a_stop_while_the_line_is_high_resumes_low);
    RUN(the_generator_edges_at_the_period_and_not_1_ms_before);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("heartbeat");
}
