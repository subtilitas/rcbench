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

/*
 * And no disarm ever follows it, which is a trap for the caller.
 *
 * arming_stop_from_far_end() clears a->armed itself, and arming_step()'s
 * disarm branch is gated on a->armed, so ARMING_ACT_DISARM cannot be
 * returned afterwards -- not on this pass and not on any later one. Whatever
 * a caller does when it sees that action, it has to do here too.
 *
 * The panel learned this the expensive way: everything it does on a disarm
 * returns the throttle command to zero, and the far-end stop did not, so the
 * next arm carried the throttle from before the stop into the same
 * transaction that armed.
 */
TEST_CASE(a_far_end_stop_leaves_no_disarm_for_the_caller_to_act_on)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_by(100);
    CHECK(a.armed);

    arming_stop_from_far_end(&a);
    for (uint32_t i = 0; i < 40; ++i) {
        t += 50;
        if (arming_step(&a, t) == ARMING_ACT_DISARM) {
            T_FAIL("a disarm followed a far-end stop at %u ms", t);
            return;
        }
    }
    CHECK(!a.armed);
    CHECK(a.stopped);
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

TEST_CASE(the_latch_can_be_asked_about)
{
    /* A screen asks so a gesture already under way can be abandoned: a stop
     * on a bench that was not armed changes nothing else it could look at. */
    arming_t a;
    arming_init(&a, 0, SETTLE_MS);
    CHECK(!arming_stopped(&a));
    arming_stop(&a);
    CHECK(arming_stopped(&a));
    arming_request_arm(&a, 10u);
    CHECK(!arming_stopped(&a));
    CHECK(!arming_stopped(NULL));
}

TEST_CASE(every_stop_counts_even_when_the_latch_does_not_move)
{
    /*
     * The latch is a level: a second stop while one is already in force
     * changes nothing about it.  A screen watching the level would not
     * cancel a hold begun after the first stop, and that hold would complete
     * and clear the latch.  The count is the event.
     */
    arming_t a;
    arming_init(&a, 0, SETTLE_MS);
    CHECK_EQ(arming_stop_count(&a), 0);

    arming_stop(&a);
    CHECK_EQ(arming_stop_count(&a), 1);
    arming_stop(&a);                        /* the latch is already set */
    CHECK(arming_stopped(&a));
    CHECK_EQ(arming_stop_count(&a), 2);

    /* The far end's stop counts too, and an arm does not undo the count. */
    arming_stop_from_far_end(&a);
    CHECK_EQ(arming_stop_count(&a), 3);
    arming_request_arm(&a, 10u);
    CHECK_EQ(arming_stop_count(&a), 3);
    CHECK_EQ(arming_stop_count(NULL), 0);
}

TEST_CASE(a_settling_arm_is_abandoned_when_touch_dies)
{
    /*
     * The request is already the policy's by then, so no screen can retract
     * it: if the settle simply ran on, a controller that recovered inside
     * the two seconds would arm from a gesture nobody has re-made.
     */
    /*
     * A settle longer than the bench's, so a 500 ms outage fits inside it.
     * The bench settles in about 100 ms, which is shorter than the silence
     * that declares touch dead; the rule under test is the policy's, and it
     * has to hold for whatever settle it is given.
     */
    const uint32_t settle = 4u * ARMING_TOUCH_DEAD_MS;
    arming_t a;
    arming_init(&a, 0, settle);
    arming_touch_seen(&a, 0);
    arming_request_arm(&a, 0);

    /* Touch dies during the settle, then answers again before the deadline. */
    const uint32_t dead_at = ARMING_TOUCH_DEAD_MS + 1u;
    arming_touch_poll(&a, dead_at);
    arming_touch_seen(&a, dead_at + 10u);

    CHECK_EQ(arming_step(&a, settle + 1u), ARMING_ACT_NONE);
    CHECK(!a.armed);
}

TEST_CASE(an_outage_that_recovers_still_brings_the_bank_down)
{
    /*
     * Touch can die and answer again inside one blocking link exchange.  By
     * the time the policy runs, the controller is healthy and nothing in the
     * state would say the outage happened -- and the heartbeat need not have
     * been withheld long enough for the far end to fail safe either.  What
     * was armed comes down all the same.
     */
    arming_t a;
    arming_init(&a, 0, SETTLE_MS);
    arming_touch_seen(&a, 0);
    arming_request_arm(&a, 0);
    CHECK_EQ(arming_step(&a, SETTLE_MS + 1u), ARMING_ACT_ARM);
    CHECK(a.armed);

    const uint32_t dead_at = SETTLE_MS + ARMING_TOUCH_DEAD_MS + 2u;
    arming_touch_poll(&a, dead_at);        /* seen only by the pump */
    arming_touch_seen(&a, dead_at + 5u);   /* and it answers again */

    CHECK_EQ(arming_step(&a, dead_at + 10u), ARMING_ACT_DISARM);
    CHECK(!a.armed);
    /* Once, not on every later step. */
    CHECK_EQ(arming_step(&a, dead_at + 20u), ARMING_ACT_NONE);
}

TEST_CASE(touch_health_can_be_judged_apart_from_the_policy_step)
{
    /*
     * The caller that judges touch and the caller that steps the policy are
     * not the same and need not run at the same rate.  A death and a
     * recovery between two steps must still count.
     */
    arming_t a;
    arming_init(&a, 0, SETTLE_MS);
    arming_touch_seen(&a, 0);
    (void)arming_step(&a, 10u);
    CHECK_EQ(arming_stop_count(&a), 0);

    const uint32_t dead_at = ARMING_TOUCH_DEAD_MS + 1u;
    arming_touch_poll(&a, dead_at);          /* seen only by the pump */
    arming_touch_seen(&a, dead_at + 5u);     /* and it answers again */
    (void)arming_step(&a, dead_at + 10u);    /* the policy runs afterwards */
    CHECK_EQ(arming_stop_count(&a), 1);

    arming_touch_poll(NULL, 0);
}

TEST_CASE(touch_that_stops_answering_counts_once)
{
    /*
     * A hold advances on frames, not on touch events, so one left standing
     * when the controller died goes on counting and would arm on recovery
     * with nobody having touched anything since.  It counts as a stop, and
     * once: a screen that abandoned its gesture must not be told again every
     * pass while the controller stays quiet.
     */
    arming_t a;
    arming_init(&a, 0, SETTLE_MS);
    arming_touch_seen(&a, 0);
    CHECK_EQ(arming_stop_count(&a), 0);

    (void)arming_step(&a, 10u);
    CHECK_EQ(arming_stop_count(&a), 0);       /* still answering */

    const uint32_t dead_at = ARMING_TOUCH_DEAD_MS + 1u;
    (void)arming_step(&a, dead_at);
    CHECK_EQ(arming_stop_count(&a), 1);
    (void)arming_step(&a, dead_at + 100u);
    (void)arming_step(&a, dead_at + 200u);
    CHECK_EQ(arming_stop_count(&a), 1);       /* the edge, not the level */

    /* It answers again, and dying a second time counts again. */
    arming_touch_seen(&a, dead_at + 300u);
    (void)arming_step(&a, dead_at + 300u);
    CHECK_EQ(arming_stop_count(&a), 1);
    (void)arming_step(&a, dead_at + 300u + ARMING_TOUCH_DEAD_MS + 1u);
    CHECK_EQ(arming_stop_count(&a), 2);
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
    RUN(a_far_end_stop_leaves_no_disarm_for_the_caller_to_act_on);
    RUN(the_rules_survive_a_millisecond_wrap);
    RUN(the_latch_can_be_asked_about);
    RUN(every_stop_counts_even_when_the_latch_does_not_move);
    RUN(touch_that_stops_answering_counts_once);
    RUN(a_settling_arm_is_abandoned_when_touch_dies);
    RUN(touch_health_can_be_judged_apart_from_the_policy_step);
    RUN(an_outage_that_recovers_still_brings_the_bank_down);
    return test_summary("arming");
}
