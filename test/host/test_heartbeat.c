/*
 * The safety line, both ends.
 *
 * What is worth testing here is not that a toggle toggles.  It is the three
 * things the monostable downstream cannot do: refuse a line that edges too
 * fast to be a panel, refuse one that has gone quiet, and refuse to believe
 * either of them again on the strength of a single edge.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

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
 * means low now, not low eventually.
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
 * And coming back is a fresh start, not a resumption.  If the generator kept
 * the pre-stop timestamp, the first edge after recovery would be however much
 * of a period was left -- possibly under the monitor's floor, which would get
 * the recovery rejected as noise.
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

    /* Still alive just inside the window ... */
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
    CHECK_EQ(heartbeat_mon_alive(&m, t + 20), false);   /* not yet */

    const uint32_t end = feed(&m, t + 40, 20, (int)HEARTBEAT_GOOD_RUN);
    CHECK(heartbeat_mon_alive(&m, end));
}

/*
 * The real rate, not the requested one.  The generator asks for 20 ms and the
 * render loop delivers 26 to 52, so the monitor has to accept what the panel
 * actually produces -- including the slow frames, which are the ones a bench
 * under load spends most of its time in.
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
 * Sampled *before* the turnover as well as after it.  Two earlier wrap tests
 * in this project passed while testing nothing: one started far enough from
 * the top that it never wrapped, and the other crossed the wrap but only
 * looked at the far side, where a naive comparison and a correct one agree.
 * The moment that separates them is the one just before the counter turns
 * over, with the deadline on the other side of it.
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
    /* Across it: still inside the window, now with now < last. */
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
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("heartbeat");
}
