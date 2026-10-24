/*
 * The render loop's watchdog (frame_watch.h): a counter the render loop
 * moves once a pass, read by the control task, which stops an ARMED bench
 * or a live supply output when the counter has stood still for
 * FRAME_WATCH_MS.
 *
 * The edges: 1ms short of the timeout and the timeout itself, the counter
 * and the millisecond clock wrapping, SAFE with the supply off, the supply
 * on alone, one stop per stall, and a loop that recovers after the stop,
 * driven through the real arming policy (arming.c) the way the control task
 * drives it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <string.h>

#include "greatest.h"

#include "arming.h"
#include "frame_watch.h"
#include "heartbeat.h"

/* What the panel passes: four good intervals, plus one. */
#define SETTLE_MS (HEARTBEAT_GOOD_RUN * HEARTBEAT_PERIOD_MS + HEARTBEAT_PERIOD_MS)
/* The control task's period, CONTROL_PERIOD_MS in
 * firmware/panel/main/panel.h. */
#define PASS_MS 5u
/* A frame at the P4 panel's 60.38Hz, rounded. */
#define FRAME_MS 17u

static frame_watch_t w;

/* ---------------------------------------------------------- the timeout */

TEST_CASE(a_still_counter_stops_an_armed_bench_at_the_timeout_and_not_before)
{
    frame_watch_init(&w, 7u, 1000u);
    CHECK(!frame_watch_step(&w, 7u, true, false, 1000u));
    CHECK(!frame_watch_step(&w, 7u, true, false,
                            1000u + FRAME_WATCH_MS - 1u));
    CHECK_EQ(frame_watch_still_ms(&w, 1000u + FRAME_WATCH_MS - 1u),
             FRAME_WATCH_MS - 1u);
    CHECK(frame_watch_step(&w, 7u, true, false, 1000u + FRAME_WATCH_MS));
}

/* The clock runs from the last move, so a move 1ms before the timeout puts
 * the stop a whole timeout later. */
TEST_CASE(a_move_restarts_the_clock)
{
    frame_watch_init(&w, 0u, 0u);
    CHECK(!frame_watch_step(&w, 1u, true, false, FRAME_WATCH_MS - 1u));
    CHECK(!frame_watch_step(&w, 1u, true, false, FRAME_WATCH_MS));
    CHECK(!frame_watch_step(&w, 1u, true, false,
                            2u * FRAME_WATCH_MS - 2u));
    CHECK(frame_watch_step(&w, 1u, true, false, 2u * FRAME_WATCH_MS - 1u));
}

/* A loop that keeps passing, at a frame or at the 200ms of a flip that
 * timed out, never stops the bench. */
TEST_CASE(a_loop_that_keeps_passing_never_stops_the_bench)
{
    const uint32_t frames[] = { FRAME_MS, 200u + 50u + FRAME_MS,
                                FRAME_WATCH_MS - 1u };
    for (size_t k = 0; k < sizeof(frames) / sizeof(frames[0]); ++k) {
        uint32_t count = 0u;
        frame_watch_init(&w, count, 0u);
        uint32_t next = frames[k];
        for (uint32_t t = 0u; t < 60000u; t += PASS_MS) {
            if (t >= next) {
                ++count;
                next += frames[k];
            }
            CHECK(!frame_watch_step(&w, count, true, true, t));
        }
    }
}

/* One stop for one stall: the alert is raised once, and the next stall
 * begins at the next move. */
TEST_CASE(a_stall_stops_the_bench_once)
{
    frame_watch_init(&w, 3u, 0u);
    CHECK(frame_watch_step(&w, 3u, true, false, FRAME_WATCH_MS));
    for (uint32_t t = FRAME_WATCH_MS; t < 30000u; t += PASS_MS) {
        CHECK(!frame_watch_step(&w, 3u, true, true, t));
    }
    CHECK(!frame_watch_step(&w, 4u, true, false, 30000u));
    CHECK(!frame_watch_step(&w, 4u, true, false,
                            30000u + FRAME_WATCH_MS - 1u));
    CHECK(frame_watch_step(&w, 4u, true, false, 30000u + FRAME_WATCH_MS));
}

/* ------------------------------------------------------------ the wraps */

TEST_CASE(a_counter_that_wraps_through_zero_has_moved)
{
    frame_watch_init(&w, UINT32_MAX - 1u, 0u);
    CHECK(!frame_watch_step(&w, UINT32_MAX, true, false, 500u));
    CHECK(!frame_watch_step(&w, 0u, true, false, 1000u));
    CHECK(!frame_watch_step(&w, 0u, true, false,
                            1000u + FRAME_WATCH_MS - 1u));
    CHECK(frame_watch_step(&w, 0u, true, false, 1000u + FRAME_WATCH_MS));
    /* Any difference is a move, a jump of half the range included. */
    CHECK(!frame_watch_step(&w, 0x80000000u, true, false, 5000u));
    CHECK(!frame_watch_step(&w, 0x80000000u, true, false,
                            5000u + FRAME_WATCH_MS - 1u));
}

TEST_CASE(the_timeout_holds_across_the_millisecond_wrap)
{
    const uint32_t t0 = UINT32_MAX - 300u;
    frame_watch_init(&w, 9u, t0);
    CHECK(!frame_watch_step(&w, 9u, true, false, t0 + 299u));
    CHECK(!frame_watch_step(&w, 9u, true, false, t0 + 301u));   /* wrapped */
    CHECK(!frame_watch_step(&w, 9u, true, false,
                            t0 + FRAME_WATCH_MS - 1u));
    CHECK_EQ(frame_watch_still_ms(&w, t0 + FRAME_WATCH_MS),
             FRAME_WATCH_MS);
    CHECK(frame_watch_step(&w, 9u, true, false, t0 + FRAME_WATCH_MS));
}

/* ------------------------------------------------------- what is live */

/* SAFE with the supply off: a frame may take seconds -- the panel's own
 * update, a log on LOGS -- and nothing is stopped. */
TEST_CASE(a_safe_bench_with_the_supply_off_is_never_stopped)
{
    frame_watch_init(&w, 1u, 0u);
    for (uint32_t t = 0u; t < 3600000u; t += 997u) {
        CHECK(!frame_watch_step(&w, 1u, false, false, t));
    }
}

TEST_CASE(the_supply_on_alone_stops_the_bench)
{
    frame_watch_init(&w, 2u, 100u);
    CHECK(!frame_watch_step(&w, 2u, false, true,
                            100u + FRAME_WATCH_MS - 1u));
    CHECK(frame_watch_step(&w, 2u, false, true, 100u + FRAME_WATCH_MS));
}

/* A loop that froze after it posted an arm or an ON is stopped at the
 * first look that finds the bench live, not a timeout after it. */
TEST_CASE(a_stall_that_began_on_a_safe_bench_stops_it_once_it_is_live)
{
    frame_watch_init(&w, 5u, 0u);
    CHECK(!frame_watch_step(&w, 5u, false, false, 5000u));
    CHECK(frame_watch_step(&w, 5u, true, false, 5000u + PASS_MS));

    frame_watch_init(&w, 5u, 0u);
    CHECK(!frame_watch_step(&w, 5u, false, false, FRAME_WATCH_MS - 1u));
    CHECK(frame_watch_step(&w, 5u, false, true, FRAME_WATCH_MS));
}

/* A stall that ends while the bench is SAFE leaves nothing standing: the
 * move resets the clock before the bench is live again. */
TEST_CASE(a_stall_that_ends_on_a_safe_bench_leaves_nothing)
{
    frame_watch_init(&w, 5u, 0u);
    CHECK(!frame_watch_step(&w, 5u, false, false, 10000u));
    CHECK(!frame_watch_step(&w, 6u, false, false, 10005u));
    CHECK(!frame_watch_step(&w, 6u, true, false, 10010u));
    CHECK(!frame_watch_step(&w, 6u, true, false,
                            10005u + FRAME_WATCH_MS - 1u));
}

TEST_CASE(a_missing_watch_stops_nothing)
{
    frame_watch_init(NULL, 0u, 0u);
    CHECK(!frame_watch_step(NULL, 0u, true, true, FRAME_WATCH_MS));
    CHECK_EQ(frame_watch_still_ms(NULL, FRAME_WATCH_MS), 0u);
}

/* ------------------------------------- the stop, through the arming policy */

/* One control pass as the panel makes it: the watch, the stop it asks for,
 * the supply switched off at a stop it has not served, then the policy's
 * step on the bank. */
typedef struct {
    arming_t      a;
    frame_watch_t w;
    bool          supply;
    uint32_t      served;    /* stops the supply has answered */
    unsigned      alerts;
    unsigned      arms;      /* ARMING_ACT_ARM handed out     */
} bench_t;

static bench_t b;

static void pass(uint32_t count, uint32_t t)
{
    arming_touch_seen(&b.a, t);
    if (frame_watch_step(&b.w, count, b.a.armed, b.supply, t)) {
        arming_stop(&b.a);
        ++b.alerts;
    }
    if (arming_stop_count(&b.a) != b.served) {
        b.served = arming_stop_count(&b.a);
        b.supply = false;
    }
    if (arming_step(&b.a, t) == ARMING_ACT_ARM) {
        ++b.arms;
    }
}

TEST_CASE(a_loop_that_recovers_after_the_stop_arms_nothing)
{
    memset(&b, 0, sizeof(b));
    arming_init(&b.a, 0u, SETTLE_MS);
    frame_watch_init(&b.w, 0u, 0u);
    uint32_t count = 0u;
    uint32_t t = 100u;

    /* ARMED with the supply on, the loop passing. */
    arming_request_arm(&b.a, t);
    b.supply = true;
    for (; t < 100u + SETTLE_MS + 500u; t += PASS_MS) {
        count += (t % FRAME_MS == 0u) ? 1u : 0u;
        pass(count, t);
    }
    CHECK(b.a.armed);
    CHECK(b.supply);
    CHECK_EQ(b.arms, 1u);

    /* The loop stands still: one stop, a timeout after its last move. */
    const uint32_t froze = b.w.moved_ms;
    for (; (uint32_t)(t - froze) < FRAME_WATCH_MS; t += PASS_MS) {
        pass(count, t);
        CHECK(b.a.armed);
        CHECK_EQ(b.alerts, 0u);
    }
    pass(count, t);
    CHECK_EQ(b.alerts, 1u);
    CHECK(!b.a.armed);
    CHECK(!b.supply);
    CHECK(arming_stopped(&b.a));
    CHECK(!arming_heartbeat(&b.a, t));
    CHECK_EQ(arming_pressed_count(&b.a), 0u);   /* the bench's own stop */

    /* Still for 10s more, then passing again for 10s: no second stop, no
     * arm, the latch as the stop left it. */
    for (uint32_t end = t + 10000u; t < end; t += PASS_MS) {
        pass(count, t);
    }
    for (uint32_t end = t + 10000u; t < end; t += PASS_MS) {
        count += (t % FRAME_MS == 0u) ? 1u : 0u;
        pass(count, t);
        CHECK(!b.a.armed);
        CHECK(!b.supply);
        CHECK(arming_stopped(&b.a));
    }
    CHECK_EQ(b.alerts, 1u);
    CHECK_EQ(b.arms, 1u);

    /* Only an explicit arm clears it, and the watch holds the new arm. */
    arming_request_arm(&b.a, t);
    for (uint32_t end = t + SETTLE_MS + PASS_MS; t < end; t += PASS_MS) {
        count += (t % FRAME_MS == 0u) ? 1u : 0u;
        pass(count, t);
    }
    CHECK(b.a.armed);
    CHECK_EQ(b.arms, 2u);
    const uint32_t still = t;
    for (; (uint32_t)(t - still) <= FRAME_WATCH_MS + FRAME_MS; t += PASS_MS) {
        pass(count, t);
    }
    CHECK_EQ(b.alerts, 2u);
    CHECK(!b.a.armed);
}

int main(void)
{
    RUN(a_still_counter_stops_an_armed_bench_at_the_timeout_and_not_before);
    RUN(a_move_restarts_the_clock);
    RUN(a_loop_that_keeps_passing_never_stops_the_bench);
    RUN(a_stall_stops_the_bench_once);
    RUN(a_counter_that_wraps_through_zero_has_moved);
    RUN(the_timeout_holds_across_the_millisecond_wrap);
    RUN(a_safe_bench_with_the_supply_off_is_never_stopped);
    RUN(the_supply_on_alone_stops_the_bench);
    RUN(a_stall_that_began_on_a_safe_bench_stops_it_once_it_is_live);
    RUN(a_stall_that_ends_on_a_safe_bench_leaves_nothing);
    RUN(a_missing_watch_stops_nothing);
    RUN(a_loop_that_recovers_after_the_stop_arms_nothing);
    return test_summary("frame_watch");
}
