/*
 * The rules that decide whether something spins.
 *
 * Every case here is a defect this project has actually had. The policy was
 * inside the panel's control loop, which no test can reach; these are the
 * checks that would have caught them.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include "arming.h"
#include "heartbeat.h"

/* What the panel passes: four good intervals, plus one. */
#define SETTLE_MS (HEARTBEAT_GOOD_RUN * HEARTBEAT_PERIOD_MS + HEARTBEAT_PERIOD_MS)

static arming_t a;

/* Armed and running at t, with touch answering. */
static uint32_t arm_by(uint32_t t)
{
    arming_request_arm(&a, t);
    t += SETTLE_MS;
    arming_touch_seen(&a, t);
    (void)arming_step(&a, t);
    return t;
}

TEST_CASE(a_stop_latches_until_an_explicit_arm)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_by(100);
    CHECK(a.armed);

    arming_stop(&a);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_DISARM);
    CHECK(!a.armed);

    /* Time passing does not clear it, and neither does anything but an arm. */
    for (int i = 0; i < 20; ++i) {
        t += 100;
        arming_touch_seen(&a, t);
        CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
        CHECK(!a.armed);
        CHECK(a.stopped);
    }
}

/*
 * The deadlock this project shipped: the heartbeat is suppressed while a stop
 * is latched, the coprocessor refuses to arm while the heartbeat is not
 * trusted, and the latch was only cleared after a successful arm. A stop with
 * a coprocessor attached could not be cleared without a reboot.
 */
TEST_CASE(an_arm_clears_the_latch_before_it_needs_the_line)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_by(100);
    arming_stop(&a);
    (void)arming_step(&a, t);
    CHECK(!arming_heartbeat(&a, t));   /* the line is down, as designed */

    /* Asking to arm clears the latch immediately, so the line can edge... */
    arming_request_arm(&a, t);
    CHECK(!a.stopped);
    CHECK(arming_heartbeat(&a, t));
    /* ...and the arm is not written until it has had time to be believed. */
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    CHECK(!a.armed);

    t += SETTLE_MS;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_ARM);
    CHECK(a.armed);
}

/* A stop while the line is settling wins; the arm is abandoned, not queued. */
TEST_CASE(a_stop_during_the_settle_abandons_the_arm)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = 100;
    arming_request_arm(&a, t);
    t += SETTLE_MS / 2;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);

    arming_stop(&a);
    t += SETTLE_MS;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    CHECK(!a.armed);
    CHECK(a.stopped);

    /* And it does not fire later either. */
    t += 10000;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    CHECK(!a.armed);
}

/* A refused arm leaves the latch clear: the loop is running and the line
 * should say so, and the operator can ask again. */
TEST_CASE(a_refused_arm_can_be_asked_again)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = 100;
    arming_request_arm(&a, t);
    t += SETTLE_MS;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_ARM);

    arming_refused(&a);
    CHECK(!a.armed);
    CHECK(!a.stopped);
    CHECK(arming_heartbeat(&a, t));

    t = arm_by(t + 10);
    CHECK(a.armed);
}

TEST_CASE(touch_that_stops_answering_disarms_and_refuses_to_arm)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_by(100);
    CHECK(a.armed);

    t += ARMING_TOUCH_DEAD_MS;
    CHECK(arming_touch_dead(&a, t));
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_DISARM);
    CHECK(!a.armed);
    CHECK(!arming_heartbeat(&a, t));

    /* And it will not arm again while touch is silent. */
    arming_request_arm(&a, t);
    t += SETTLE_MS;
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    CHECK(!a.armed);

    /* Touch coming back is not itself an arm. */
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    CHECK(!a.armed);
}

/*
 * The heartbeat asserts that the loop owning STOP is running. It is
 * suppressed by a latched stop and by dead touch, and by nothing else --
 * notably not by the link, whose failure has its own watchdog at each end.
 */
TEST_CASE(the_heartbeat_follows_the_stop_and_the_touch_only)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = 100;
    arming_touch_seen(&a, t);
    CHECK(arming_heartbeat(&a, t));      /* disarmed, but running */

    t = arm_by(t);
    CHECK(arming_heartbeat(&a, t));

    arming_request_disarm(&a);
    (void)arming_step(&a, t);
    CHECK(arming_heartbeat(&a, t));      /* disarmed is not stopped */

    arming_stop(&a);
    CHECK(!arming_heartbeat(&a, t));
}

/* The clock times the run, not the panel: it starts at the arm and holds the
 * last run's length once the bench disarms. */
TEST_CASE(the_run_clock_times_the_run)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = 10000;                  /* the panel has been up 10 s */
    arming_touch_seen(&a, t);
    (void)arming_step(&a, t);
    CHECK_EQ(arming_run_seconds(&a), 0u);

    t = arm_by(t);
    for (int i = 0; i < 5; ++i) {
        t += 1000;
        arming_touch_seen(&a, t);
        (void)arming_step(&a, t);
    }
    CHECK_EQ(arming_run_seconds(&a), 5u);

    arming_request_disarm(&a);
    (void)arming_step(&a, t);
    const uint32_t held = arming_run_seconds(&a);
    CHECK_EQ(held, 5u);

    /* It holds rather than running on. */
    t += 4000;
    arming_touch_seen(&a, t);
    (void)arming_step(&a, t);
    CHECK_EQ(arming_run_seconds(&a), held);

    /* And the next run starts from zero. */
    t = arm_by(t);
    t += 2000;
    arming_touch_seen(&a, t);
    (void)arming_step(&a, t);
    CHECK_EQ(arming_run_seconds(&a), 2u);
}

/* A far-end NACK or failsafe latches at this end too. */
TEST_CASE(the_far_end_can_stop_the_bench)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_by(100);
    CHECK(a.armed);

    arming_stop_from_far_end(&a);
    CHECK(!a.armed);
    CHECK(a.stopped);
    CHECK(!arming_heartbeat(&a, t));
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);   /* already disarmed */
}

/* Timestamps wrap; the rules must not. */
TEST_CASE(the_rules_survive_a_millisecond_wrap)
{
    const uint32_t near_wrap = 0xFFFFFF00u;
    arming_init(&a, near_wrap, SETTLE_MS);
    uint32_t t = near_wrap;
    arming_touch_seen(&a, t);
    CHECK(!arming_touch_dead(&a, t));

    arming_request_arm(&a, t);
    t += SETTLE_MS;                       /* wraps past zero */
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_ARM);
    CHECK(a.armed);

    t += 3000;
    arming_touch_seen(&a, t);
    (void)arming_step(&a, t);
    CHECK_EQ(arming_run_seconds(&a), 3u);

    /* And touch going silent across the wrap still reads as dead. */
    t += ARMING_TOUCH_DEAD_MS;
    CHECK(arming_touch_dead(&a, t));
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_DISARM);
}

int main(void)
{
    RUN(a_stop_latches_until_an_explicit_arm);
    RUN(an_arm_clears_the_latch_before_it_needs_the_line);
    RUN(a_stop_during_the_settle_abandons_the_arm);
    RUN(a_refused_arm_can_be_asked_again);
    RUN(touch_that_stops_answering_disarms_and_refuses_to_arm);
    RUN(the_heartbeat_follows_the_stop_and_the_touch_only);
    RUN(the_run_clock_times_the_run);
    RUN(the_far_end_can_stop_the_bench);
    RUN(the_rules_survive_a_millisecond_wrap);
    return test_summary("arming");
}
