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
 * is latched, the IOmcu refuses to arm while the heartbeat is not
 * trusted, and the latch was only cleared after a successful arm. A stop with
 * an IOmcu attached could not be cleared without a reboot.
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

    (void)arm_by(t + 10);
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
    uint32_t t = 10000;                  /* the panel has been up 10s */
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
    arming_t ar;
    arming_init(&ar, 0, SETTLE_MS);
    CHECK(!arming_stopped(&ar));
    arming_stop(&ar);
    CHECK(arming_stopped(&ar));
    arming_request_arm(&ar, 10u);
    CHECK(!arming_stopped(&ar));
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
    arming_t ar;
    arming_init(&ar, 0, SETTLE_MS);
    CHECK_EQ(arming_stop_count(&ar), 0);

    arming_stop(&ar);
    CHECK_EQ(arming_stop_count(&ar), 1);
    arming_stop(&ar);                        /* the latch is already set */
    CHECK(arming_stopped(&ar));
    CHECK_EQ(arming_stop_count(&ar), 2);

    /* The far end's stop counts too, and an arm does not undo the count. */
    arming_stop_from_far_end(&ar);
    CHECK_EQ(arming_stop_count(&ar), 3);
    arming_request_arm(&ar, 10u);
    CHECK_EQ(arming_stop_count(&ar), 3);
    CHECK_EQ(arming_stop_count(NULL), 0);
}

/* A pressed STOP is a stop and a press; the bench's own stops are stops
 * only, so a run can tell the two apart. */
TEST_CASE(a_pressed_stop_is_counted_apart_from_the_benchs_own)
{
    arming_t ar;
    arming_init(&ar, 0, SETTLE_MS);
    arming_stop_pressed(&ar);
    CHECK(arming_stopped(&ar));
    CHECK_EQ(arming_stop_count(&ar), 1);
    CHECK_EQ(arming_pressed_count(&ar), 1);
    arming_stop(&ar);                        /* touch lost under a press */
    arming_stop_from_far_end(&ar);           /* the IOmcu's */
    CHECK_EQ(arming_stop_count(&ar), 3);
    CHECK_EQ(arming_pressed_count(&ar), 1);
    /* Touch that stops answering for ARMING_TOUCH_DEAD_MS. */
    arming_touch_poll(&ar, ARMING_TOUCH_DEAD_MS);
    CHECK_EQ(arming_stop_count(&ar), 4);
    CHECK_EQ(arming_pressed_count(&ar), 1);
    arming_request_arm(&ar, ARMING_TOUCH_DEAD_MS);
    CHECK_EQ(arming_pressed_count(&ar), 1);
    arming_stop_pressed(NULL);
    CHECK_EQ(arming_pressed_count(NULL), 0);
}

TEST_CASE(a_settling_arm_is_abandoned_when_touch_dies)
{
    /*
     * The request is already the policy's by then, so no screen can retract
     * it: if the settle simply ran on, a controller that recovered inside
     * the two seconds would arm from a gesture nobody has re-made.
     */
    /*
     * A settle longer than the bench's, so a 500ms outage fits inside it.
     * The bench settles in about 100ms, which is shorter than the silence
     * that declares touch dead; the rule under test is the policy's, and it
     * has to hold for whatever settle it is given.
     */
    const uint32_t settle = 4u * ARMING_TOUCH_DEAD_MS;
    arming_t ar;
    arming_init(&ar, 0, settle);
    arming_touch_seen(&ar, 0);
    arming_request_arm(&ar, 0);

    /* Touch dies during the settle, then answers again before the deadline. */
    const uint32_t dead_at = ARMING_TOUCH_DEAD_MS + 1u;
    arming_touch_poll(&ar, dead_at);
    arming_touch_seen(&ar, dead_at + 10u);

    CHECK_EQ(arming_step(&ar, settle + 1u), ARMING_ACT_NONE);
    CHECK(!ar.armed);
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
    arming_t ar;
    arming_init(&ar, 0, SETTLE_MS);
    arming_touch_seen(&ar, 0);
    arming_request_arm(&ar, 0);
    CHECK_EQ(arming_step(&ar, SETTLE_MS + 1u), ARMING_ACT_ARM);
    CHECK(ar.armed);

    const uint32_t dead_at = SETTLE_MS + ARMING_TOUCH_DEAD_MS + 2u;
    arming_touch_poll(&ar, dead_at);        /* seen only by the pump */
    arming_touch_seen(&ar, dead_at + 5u);   /* and it answers again */

    CHECK_EQ(arming_step(&ar, dead_at + 10u), ARMING_ACT_DISARM);
    CHECK(!ar.armed);
    /* Once, not on every later step. */
    CHECK_EQ(arming_step(&ar, dead_at + 20u), ARMING_ACT_NONE);
}

TEST_CASE(touch_health_can_be_judged_apart_from_the_policy_step)
{
    /*
     * The caller that judges touch and the caller that steps the policy are
     * not the same and need not run at the same rate.  A death and a
     * recovery between two steps must still count.
     */
    arming_t ar;
    arming_init(&ar, 0, SETTLE_MS);
    arming_touch_seen(&ar, 0);
    (void)arming_step(&ar, 10u);
    CHECK_EQ(arming_stop_count(&ar), 0);

    const uint32_t dead_at = ARMING_TOUCH_DEAD_MS + 1u;
    arming_touch_poll(&ar, dead_at);          /* seen only by the pump */
    arming_touch_seen(&ar, dead_at + 5u);     /* and it answers again */
    (void)arming_step(&ar, dead_at + 10u);    /* the policy runs afterwards */
    CHECK_EQ(arming_stop_count(&ar), 1);

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
    arming_t ar;
    arming_init(&ar, 0, SETTLE_MS);
    arming_touch_seen(&ar, 0);
    CHECK_EQ(arming_stop_count(&ar), 0);

    (void)arming_step(&ar, 10u);
    CHECK_EQ(arming_stop_count(&ar), 0);       /* still answering */

    const uint32_t dead_at = ARMING_TOUCH_DEAD_MS + 1u;
    (void)arming_step(&ar, dead_at);
    CHECK_EQ(arming_stop_count(&ar), 1);
    (void)arming_step(&ar, dead_at + 100u);
    (void)arming_step(&ar, dead_at + 200u);
    CHECK_EQ(arming_stop_count(&ar), 1);       /* the edge, not the level */

    /* It answers again, and dying a second time counts again. */
    arming_touch_seen(&ar, dead_at + 300u);
    (void)arming_step(&ar, dead_at + 300u);
    CHECK_EQ(arming_stop_count(&ar), 1);
    (void)arming_step(&ar, dead_at + 300u + ARMING_TOUCH_DEAD_MS + 1u);
    CHECK_EQ(arming_stop_count(&ar), 2);
}

/* ---------------------------------------------- waiting for the far end */

#define WAIT_MS ARMING_LINE_WAIT_MS

/* A policy that asks the far end, with touch answering at @p t. */
static void init_asking(uint32_t t)
{
    arming_init(&a, t, SETTLE_MS);
    arming_set_line_wait(&a, WAIT_MS);
    arming_touch_seen(&a, t);
}

TEST_CASE(the_panel_passes_the_shared_settle_and_bound)
{
    CHECK_EQ(HEARTBEAT_SETTLE_MS, 100);
    CHECK_EQ(SETTLE_MS, HEARTBEAT_SETTLE_MS);
    CHECK_EQ(ARMING_LINE_WAIT_MS, 200);
    /* Inside the 500ms after which touch counts as dead, so an arm that is
     * given up was not abandoned for touch first. */
    CHECK(HEARTBEAT_SETTLE_MS + ARMING_LINE_WAIT_MS < ARMING_TOUCH_DEAD_MS);
}

TEST_CASE(an_arm_waits_for_the_far_end_to_trust_the_line)
{
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);

    /* Not asked before the settle is up, to the millisecond. */
    arming_touch_seen(&a, 1000 + SETTLE_MS - 1);
    CHECK(!arming_line_wanted(&a, 1000 + SETTLE_MS - 1));
    CHECK_EQ(arming_step(&a, 1000 + SETTLE_MS - 1), ARMING_ACT_NONE);

    /* At the settle: asked, and nothing written until the answer is yes. */
    uint32_t t = 1000 + SETTLE_MS;
    CHECK(arming_line_wanted(&a, t));
    arming_line_report(&a, false);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    CHECK(!a.armed);
    CHECK(a.arming);
    CHECK(arming_heartbeat(&a, t));       /* the line runs while it waits */

    t += 5;
    arming_touch_seen(&a, t);
    CHECK(arming_line_wanted(&a, t));
    arming_line_report(&a, true);
    CHECK(!arming_line_wanted(&a, t));    /* answered: not asked twice */
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_ARM);
    CHECK(a.armed);
    CHECK(!a.arming);
    CHECK_EQ(arming_step(&a, t + 5), ARMING_ACT_NONE);
}

TEST_CASE(a_line_already_trusted_arms_at_the_settle)
{
    /* The power-up arm: the line has run since the task started. */
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    const uint32_t t = 1000 + SETTLE_MS;
    arming_touch_seen(&a, t);
    CHECK(arming_line_wanted(&a, t));
    arming_line_report(&a, true);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_ARM);
}

TEST_CASE(an_arm_the_far_end_never_trusts_is_given_up_at_the_bound)
{
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    const uint32_t bound = 1000 + SETTLE_MS + WAIT_MS;
    for (uint32_t t = 1000 + SETTLE_MS; t < bound; t += 5) {
        arming_touch_seen(&a, t);
        CHECK(arming_line_wanted(&a, t));
        arming_line_report(&a, false);
        CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    }
    /* One millisecond short it still waits; at the bound it gives up. */
    arming_touch_seen(&a, bound - 1);
    arming_line_report(&a, false);
    CHECK_EQ(arming_step(&a, bound - 1), ARMING_ACT_NONE);
    CHECK(a.arming);
    arming_touch_seen(&a, bound);
    arming_line_report(&a, false);
    CHECK_EQ(arming_step(&a, bound), ARMING_ACT_GIVE_UP);
    CHECK(!a.arming);
    CHECK(!a.armed);
    CHECK(!a.stopped);                    /* the operator can ask again */
    CHECK(!arming_line_wanted(&a, bound + 1));
    CHECK_EQ(arming_step(&a, bound + 1), ARMING_ACT_NONE);

    /* A yes that arrives in the pass at the bound is still taken. */
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    arming_touch_seen(&a, bound);
    arming_line_report(&a, true);
    CHECK_EQ(arming_step(&a, bound), ARMING_ACT_ARM);

    /* And a pass that comes late, one past the bound, gives up as well. */
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    arming_touch_seen(&a, bound + 1);
    CHECK_EQ(arming_step(&a, bound + 1), ARMING_ACT_GIVE_UP);
}

TEST_CASE(the_wait_for_the_line_survives_a_millisecond_wrap)
{
    /* The request 50ms before the wrap: the settle ends 50ms after it and
     * the bound 250ms after it. */
    const uint32_t t0 = 0xFFFFFFFFu - 49u;
    init_asking(t0 - 10u);
    arming_request_arm(&a, t0);
    arming_touch_seen(&a, t0 + SETTLE_MS - 1u);
    CHECK(!arming_line_wanted(&a, t0 + SETTLE_MS - 1u));
    CHECK_EQ(arming_step(&a, t0 + SETTLE_MS - 1u), ARMING_ACT_NONE);
    arming_touch_seen(&a, t0 + SETTLE_MS);
    CHECK(arming_line_wanted(&a, t0 + SETTLE_MS));
    CHECK_EQ(arming_step(&a, t0 + SETTLE_MS), ARMING_ACT_NONE);
    arming_touch_seen(&a, t0 + SETTLE_MS + WAIT_MS - 1u);
    CHECK_EQ(arming_step(&a, t0 + SETTLE_MS + WAIT_MS - 1u), ARMING_ACT_NONE);
    arming_touch_seen(&a, t0 + SETTLE_MS + WAIT_MS);
    CHECK_EQ(arming_step(&a, t0 + SETTLE_MS + WAIT_MS), ARMING_ACT_GIVE_UP);

    /* And the bound itself straddling the wrap while the settle does not. */
    const uint32_t t1 = 0xFFFFFFFFu - 149u;
    init_asking(t1 - 10u);
    arming_request_arm(&a, t1);
    arming_touch_seen(&a, t1 + SETTLE_MS);
    CHECK(arming_line_wanted(&a, t1 + SETTLE_MS));
    arming_touch_seen(&a, t1 + SETTLE_MS + WAIT_MS - 1u);
    CHECK_EQ(arming_step(&a, t1 + SETTLE_MS + WAIT_MS - 1u), ARMING_ACT_NONE);
    arming_touch_seen(&a, t1 + SETTLE_MS + WAIT_MS - 1u);
    arming_line_report(&a, true);
    CHECK_EQ(arming_step(&a, t1 + SETTLE_MS + WAIT_MS - 1u), ARMING_ACT_ARM);
}

/* Every way the wait ends other than an arm or the bound. */
TEST_CASE(a_wait_for_the_line_ends_with_whatever_ends_an_arm)
{
    const uint32_t t = 1000 + SETTLE_MS + 20;

    /* STOP. */
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    arming_touch_seen(&a, t);
    arming_stop_pressed(&a);
    CHECK(!arming_line_wanted(&a, t));
    arming_line_report(&a, true);         /* an answer already on its way */
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    CHECK(!a.armed);
    CHECK(a.stopped);
    CHECK_EQ(arming_step(&a, t + WAIT_MS), ARMING_ACT_NONE);   /* no give-up */

    /* A disarm: leaving the screen, or DISARM. */
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    arming_touch_seen(&a, t);
    arming_request_disarm(&a);
    CHECK(!arming_line_wanted(&a, t));
    arming_line_report(&a, true);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    CHECK(!a.armed);
    CHECK_EQ(arming_step(&a, t + WAIT_MS), ARMING_ACT_NONE);

    /* Touch that stops answering during the wait. */
    init_asking(0);
    arming_touch_seen(&a, 750);           /* its last answer */
    arming_request_arm(&a, 1000);
    CHECK(arming_line_wanted(&a, 1000 + SETTLE_MS));
    arming_line_report(&a, false);
    CHECK_EQ(arming_step(&a, 750 + ARMING_TOUCH_DEAD_MS - 1), ARMING_ACT_NONE);
    CHECK(a.arming);
    /* Dead 50ms ahead of the bound: abandoned, not given up. */
    CHECK_EQ(arming_step(&a, 750 + ARMING_TOUCH_DEAD_MS), ARMING_ACT_NONE);
    CHECK(!a.arming);
    CHECK_EQ(arming_step(&a, 1000 + SETTLE_MS + WAIT_MS), ARMING_ACT_NONE);
    arming_touch_seen(&a, 2000);
    arming_line_report(&a, true);
    CHECK_EQ(arming_step(&a, 2000), ARMING_ACT_NONE);
    CHECK(!a.armed);

    /* The link going quiet. */
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    arming_touch_seen(&a, t);
    CHECK(arming_link_lost(&a, false));
    CHECK(a.stopped);
    CHECK(!arming_line_wanted(&a, t));
    arming_line_report(&a, true);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    CHECK(!a.armed);

    /* A refusal from the far end after the arm was written. */
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    arming_touch_seen(&a, t);
    arming_line_report(&a, true);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_ARM);
    arming_refused(&a);
    CHECK(!a.armed);
    /* The next arm asks afresh: the last answer is not carried over. */
    arming_touch_seen(&a, t + 1000);
    arming_request_arm(&a, t + 1000);
    arming_touch_seen(&a, t + 1000 + SETTLE_MS);
    CHECK(arming_line_wanted(&a, t + 1000 + SETTLE_MS));
    CHECK_EQ(arming_step(&a, t + 1000 + SETTLE_MS), ARMING_ACT_NONE);
}

TEST_CASE(without_a_far_end_to_ask_the_settle_alone_decides)
{
    arming_init(&a, 0, SETTLE_MS);        /* no arming_set_line_wait() */
    arming_touch_seen(&a, 0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    arming_touch_seen(&a, 1000 + SETTLE_MS);
    CHECK(!arming_line_wanted(&a, 1000 + SETTLE_MS));
    CHECK_EQ(arming_step(&a, 1000 + SETTLE_MS), ARMING_ACT_ARM);

    /* A report with no arm waiting is dropped, and nulls are harmless. */
    arming_line_report(&a, true);
    CHECK(!a.line_trusted);
    arming_set_line_wait(NULL, 5);
    arming_line_report(NULL, true);
    CHECK(!arming_line_wanted(NULL, 0));
}

/* ----------------------------------------------- the link going and coming */

TEST_CASE(a_link_lost_while_armed_is_a_stop)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_by(100);
    CHECK(a.armed);
    const uint32_t stops = arming_stop_count(&a);
    const uint32_t pressed = arming_pressed_count(&a);

    CHECK(arming_link_lost(&a, true));
    CHECK(a.stopped);
    CHECK(!a.armed);
    CHECK(!arming_heartbeat(&a, t));      /* and the line goes with it */
    CHECK_EQ(arming_stop_count(&a), stops + 1);
    CHECK_EQ(arming_pressed_count(&a), pressed);   /* the bench's own */
    /* The policy has no disarm left to hand back: the caller disarms its
     * bank on the return value. */
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);

    /* It stays stopped when the link returns, until an arm. */
    CHECK(!arming_link_found(&a, false));
    for (int i = 0; i < 20; ++i) {
        t += 100;
        arming_touch_seen(&a, t);
        CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
        CHECK(a.stopped);
    }
    arming_request_arm(&a, t);
    CHECK(!a.stopped);
}

TEST_CASE(a_link_lost_while_disarmed_changes_nothing)
{
    arming_init(&a, 0, SETTLE_MS);
    arming_touch_seen(&a, 0);
    /* The link is needed by an armed bank, an armed policy and a waiting
     * arm, and by nothing else. */
    CHECK(!arming_link_needed(&a, false));
    CHECK(arming_link_needed(&a, true));
    CHECK(!arming_link_needed(NULL, true));
    arming_request_arm(&a, 0);
    CHECK(arming_link_needed(&a, false));
    (void)arming_step(&a, SETTLE_MS);
    CHECK(a.armed);
    CHECK(arming_link_needed(&a, false));
    arming_request_disarm(&a);
    CHECK(!arming_link_needed(&a, false));
    CHECK(!arming_link_lost(&a, false));
    CHECK(!a.stopped);
    CHECK_EQ(arming_stop_count(&a), 0);
    CHECK(arming_heartbeat(&a, 0));
    CHECK(!arming_link_found(&a, false));
    CHECK(!a.stopped);
    CHECK_EQ(arming_stop_count(&a), 0);
    CHECK(!arming_link_lost(NULL, true));
    CHECK(!arming_link_found(NULL, true));
}

TEST_CASE(the_bank_alone_being_armed_is_enough_for_a_link_stop)
{
    /* The bank and the policy can disagree for a pass; the bank is what
     * writes ARM = 1 at the poll. */
    arming_init(&a, 0, SETTLE_MS);
    arming_touch_seen(&a, 0);
    CHECK(arming_link_lost(&a, true));
    CHECK(a.stopped);

    arming_init(&a, 0, SETTLE_MS);
    arming_touch_seen(&a, 0);
    CHECK(arming_link_found(&a, true));
    CHECK(a.stopped);
    CHECK_EQ(arming_stop_count(&a), 1);
}

TEST_CASE(a_far_end_that_appears_stops_a_bank_armed_without_one)
{
    arming_init(&a, 0, SETTLE_MS);
    const uint32_t t = arm_by(100);       /* the simulator: no link */
    CHECK(a.armed);
    CHECK(arming_link_found(&a, true));
    CHECK(a.stopped);
    CHECK(!a.armed);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);

    /* An arm still waiting when the far end appears is left to complete:
     * it writes CLEAR and the frame that arms. */
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    CHECK(!arming_link_found(&a, false));
    CHECK(a.arming);
    CHECK(!a.stopped);
    arming_touch_seen(&a, 1000 + SETTLE_MS);
    arming_line_report(&a, true);
    CHECK_EQ(arming_step(&a, 1000 + SETTLE_MS), ARMING_ACT_ARM);
}

TEST_CASE(a_write_nobody_answers_is_a_stop_and_a_refusal_is_not)
{
    /* The far end answered and refused: disarmed, the latch clear, the
     * line running, so the operator can ask again. */
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_by(100);
    CHECK(a.armed);
    uint32_t stops = arming_stop_count(&a);
    CHECK(!arming_write_failed(&a, true));
    CHECK(!a.armed);
    CHECK(!a.arming);
    CHECK(!a.stopped);
    CHECK(arming_heartbeat(&a, t));
    CHECK_EQ(arming_stop_count(&a), stops);

    /* Nobody answered: a stop of the bench's own, the line withheld, and
     * no disarm left for the policy to hand back. */
    arming_init(&a, 0, SETTLE_MS);
    t = arm_by(100);
    stops = arming_stop_count(&a);
    const uint32_t pressed = arming_pressed_count(&a);
    CHECK(arming_write_failed(&a, false));
    CHECK(!a.armed);
    CHECK(!a.arming);
    CHECK(a.stopped);
    CHECK(!arming_heartbeat(&a, t));
    CHECK_EQ(arming_stop_count(&a), stops + 1);
    CHECK_EQ(arming_pressed_count(&a), pressed);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    /* The link found down afterwards adds no second stop. */
    CHECK(!arming_link_lost(&a, false));
    CHECK_EQ(arming_stop_count(&a), stops + 1);

    CHECK(!arming_write_failed(NULL, false));
    CHECK(!arming_write_failed(NULL, true));
}

/*
 * The question about the line is an exchange, and an exchange can take up
 * to 1000ms to come back.  A yes that comes back after the bound arms
 * nothing: the bound is on when the bench may start driving, not on when
 * the question was asked.
 */
TEST_CASE(a_trusted_report_past_the_bound_arms_nothing)
{
    const uint32_t bases[2] = { 1000u, 0xFFFFFFFFu - 249u };  /* and the wrap */
    for (unsigned b = 0; b < 2u; ++b) {
        const uint32_t t0 = bases[b];
        const uint32_t bound = t0 + SETTLE_MS + WAIT_MS;

        /* One millisecond inside: taken. */
        init_asking(t0 - 10u);
        arming_touch_seen(&a, t0);
        arming_request_arm(&a, t0);
        arming_touch_seen(&a, bound - 1u);
        CHECK(arming_line_wanted(&a, bound - 1u));
        arming_line_report(&a, true);
        CHECK_EQ(arming_step(&a, bound - 1u), ARMING_ACT_ARM);

        /* At the bound: taken. */
        init_asking(t0 - 10u);
        arming_touch_seen(&a, t0);
        arming_request_arm(&a, t0);
        arming_touch_seen(&a, bound);
        CHECK(arming_line_wanted(&a, bound));
        arming_line_report(&a, true);
        CHECK_EQ(arming_step(&a, bound), ARMING_ACT_ARM);

        /* One millisecond past: asked at the settle, answered late. */
        init_asking(t0 - 10u);
        arming_touch_seen(&a, t0);
        arming_request_arm(&a, t0);
        CHECK(arming_line_wanted(&a, t0 + SETTLE_MS));
        arming_touch_seen(&a, bound + 1u);
        arming_line_report(&a, true);
        CHECK_EQ(arming_step(&a, bound + 1u), ARMING_ACT_GIVE_UP);
        CHECK(!a.armed);
        CHECK(!a.arming);
        CHECK(!a.stopped);

        /* And 700ms past, an exchange that all but timed out. */
        init_asking(t0 - 10u);
        arming_touch_seen(&a, t0);
        arming_request_arm(&a, t0);
        CHECK(arming_line_wanted(&a, t0 + SETTLE_MS));
        arming_touch_seen(&a, bound + 700u);
        arming_line_report(&a, true);
        CHECK_EQ(arming_step(&a, bound + 700u), ARMING_ACT_GIVE_UP);
        CHECK(!a.armed);

        /* Past the bound there is nothing left to ask. */
        init_asking(t0 - 10u);
        arming_touch_seen(&a, t0);
        arming_request_arm(&a, t0);
        CHECK(arming_line_wanted(&a, bound));
        CHECK(!arming_line_wanted(&a, bound + 1u));
    }
}

TEST_CASE(with_no_far_end_in_the_pass_the_settle_alone_decides)
{
    /* Not before the settle. */
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    arming_touch_seen(&a, 1000 + SETTLE_MS - 1);
    arming_line_nobody(&a);
    CHECK_EQ(arming_step(&a, 1000 + SETTLE_MS - 1), ARMING_ACT_NONE);
    arming_touch_seen(&a, 1000 + SETTLE_MS);
    arming_line_nobody(&a);
    CHECK_EQ(arming_step(&a, 1000 + SETTLE_MS), ARMING_ACT_ARM);

    /* A pass a second late behind a probe nobody answered: still armed,
     * one past the bound and 700ms past it, and across the wrap. */
    const uint32_t bases[2] = { 1000u, 0xFFFFFFFFu - 249u };
    for (unsigned b = 0; b < 2u; ++b) {
        const uint32_t bound = bases[b] + SETTLE_MS + WAIT_MS;
        for (uint32_t late = 1u; late <= 701u; late += 700u) {
            init_asking(bases[b] - 10u);
            arming_touch_seen(&a, bases[b]);
            arming_request_arm(&a, bases[b]);
            arming_touch_seen(&a, bound + late);
            arming_line_nobody(&a);
            CHECK_EQ(arming_step(&a, bound + late), ARMING_ACT_ARM);
        }
    }

    /* It holds for one step: a far end that answers by the next pass is
     * asked, and past the bound its arm is given up. */
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    arming_touch_seen(&a, 1000 + SETTLE_MS - 5);
    arming_line_nobody(&a);
    CHECK_EQ(arming_step(&a, 1000 + SETTLE_MS - 5), ARMING_ACT_NONE);
    arming_touch_seen(&a, 1000 + SETTLE_MS);
    CHECK(arming_line_wanted(&a, 1000 + SETTLE_MS));
    CHECK_EQ(arming_step(&a, 1000 + SETTLE_MS), ARMING_ACT_NONE);
    arming_touch_seen(&a, 1000 + SETTLE_MS + WAIT_MS + 1);
    CHECK_EQ(arming_step(&a, 1000 + SETTLE_MS + WAIT_MS + 1),
             ARMING_ACT_GIVE_UP);

    /* A stop still wins, and a null is harmless. */
    init_asking(0);
    arming_touch_seen(&a, 1000);
    arming_request_arm(&a, 1000);
    arming_stop(&a);
    arming_touch_seen(&a, 1000 + SETTLE_MS);
    arming_line_nobody(&a);
    CHECK_EQ(arming_step(&a, 1000 + SETTLE_MS), ARMING_ACT_NONE);
    CHECK(!a.armed);
    arming_line_nobody(NULL);
}

/* ------------------------------- an exchange that ends with no answer */

/*
 * Every kind of request the panel sends.  The decision does not look at
 * the kind, and this table is what holds it to that: a kind added to the
 * firmware is added here, and it is judged like the rest because there is
 * one judge.
 */
enum {
    Q_BENCH_READ, Q_CONTROL_WRITE, Q_STATUS_READ, Q_SENSE, Q_SERVO_SENSE,
    Q_SERVO_RELEASE, Q_SERVO_RATE, Q_CLEAR, Q_ARM_FRAME, Q_SUPPLY, Q_TONE,
    Q_KINDS
};
static const char *const q_name[Q_KINDS] = {
    "BENCH read", "control write", "STATUS read", "SENSE", "SERVO_SENSE",
    "servo release", "servo rate", "CLEAR", "arming frame", "supply", "tone",
};
enum { Q_ARMED, Q_ARMING, Q_DISARMED, Q_LINK_DOWN, Q_STATES };
static const char *const q_state_name[Q_STATES] = {
    "armed", "arming", "disarmed", "link down",
};

/* The panel's exchange() as a model: one way in for every request, one way
 * out for a missing answer. */
static bool     q_link_is_up, q_link_quiet, q_bank;
static uint16_t q_throttle;
static unsigned q_on_wire;       /* requests that reached the wire */
static unsigned q_stops_here;    /* stops made where an exchange ended */
static int      q_dead;          /* the kind nobody answers, or -1 */

static bool q_exchange(int kind)
{
    if (q_link_quiet) {
        return false;            /* not sent */
    }
    ++q_on_wire;
    if (kind != q_dead) {
        return true;
    }
    switch (arming_exchange_unanswered(&a, q_link_is_up, q_bank)) {
    case ARMING_QUIET_STOP:
        q_bank = false;
        q_throttle = 0;
        ++q_stops_here;
        q_link_quiet = true;
        break;
    case ARMING_QUIET_LINK_DOWN:
        q_link_quiet = true;
        break;
    case ARMING_QUIET_NOTHING:
    default:
        break;
    }
    return false;
}

/* The poll's decision: the latch, and nothing else. */
static void q_poll_decides(void)
{
    if (q_link_quiet) {
        q_link_quiet = false;
        q_link_is_up = false;
    }
}

TEST_CASE(an_unanswered_exchange_is_judged_alike_whatever_it_carried)
{
    for (int kind = 0; kind < Q_KINDS; ++kind) {
        for (int state = 0; state < Q_STATES; ++state) {
            for (int pos = 0; pos < 3; ++pos) {
                /* A pass of three requests with this one first, in the
                 * middle or last; the other two are the next two kinds. */
                int pass[3];
                for (int i = 0, other = kind; i < 3; ++i) {
                    if (i == pos) {
                        pass[i] = kind;
                    } else {
                        other = (other + 1) % Q_KINDS;
                        pass[i] = other;
                    }
                }

                uint32_t t = 1000;
                arming_init(&a, 0, SETTLE_MS);
                arming_touch_seen(&a, t);
                q_link_is_up = state != Q_LINK_DOWN;
                q_link_quiet = false;
                q_bank = false;
                q_throttle = 0;
                if (state == Q_ARMED || state == Q_LINK_DOWN) {
                    t = arm_by(t);              /* no far end was asked */
                    if (state == Q_ARMED) {
                        arming_touch_seen(&a, t);
                    }
                    q_bank = a.armed;
                    q_throttle = 7000;
                } else if (state == Q_ARMING) {
                    arming_set_line_wait(&a, WAIT_MS);
                    arming_request_arm(&a, t);
                }
                CHECK(state != Q_ARMED || (a.armed && q_bank));
                CHECK(state != Q_ARMING || a.arming);
                const uint32_t stops = arming_stop_count(&a);
                const uint32_t pressed = arming_pressed_count(&a);
                q_on_wire = 0;
                q_stops_here = 0;
                q_dead = kind;

                bool got[3];
                for (int i = 0; i < 3; ++i) {
                    got[i] = q_exchange(pass[i]);
                }
                const bool quiet_after_pass = q_link_quiet;
                q_poll_decides();

                bool ok = !got[pos];
                switch (state) {
                case Q_ARMED:
                case Q_ARMING:
                    /* Stopped where the exchange ended, once, as the
                     * bench's own stop; nothing sent after it; the link
                     * down at the poll; and no arm left to complete. */
                    ok = ok && q_stops_here == 1u && a.stopped && !a.armed
                         && !a.arming && !q_bank && q_throttle == 0u
                         && !arming_heartbeat(&a, t)
                         && arming_stop_count(&a) == stops + 1u
                         && arming_pressed_count(&a) == pressed
                         && q_on_wire == (unsigned)pos + 1u
                         && quiet_after_pass && !q_link_is_up;
                    arming_touch_seen(&a, t + SETTLE_MS + WAIT_MS);
                    arming_line_report(&a, true);
                    ok = ok && arming_step(&a, t + SETTLE_MS + WAIT_MS)
                                   == ARMING_ACT_NONE
                         && !a.armed;
                    break;
                case Q_DISARMED:
                    /* No stop to make: the link down, and nothing sent
                     * after it. */
                    ok = ok && q_stops_here == 0u && !a.stopped
                         && arming_heartbeat(&a, t)
                         && arming_stop_count(&a) == stops
                         && q_on_wire == (unsigned)pos + 1u
                         && quiet_after_pass && !q_link_is_up;
                    break;
                case Q_LINK_DOWN:
                default:
                    /* A probe: the bank armed with no far end runs on,
                     * and the pass sends the rest. */
                    ok = ok && q_stops_here == 0u && !a.stopped && a.armed
                         && q_bank && q_throttle == 7000u
                         && arming_heartbeat(&a, t)
                         && arming_stop_count(&a) == stops
                         && q_on_wire == 3u && !quiet_after_pass
                         && !q_link_is_up;
                    for (int i = 0; i < 3; ++i) {
                        ok = ok && (i == pos || got[i]);
                    }
                    break;
                }
                if (!ok) {
                    T_FAIL("%s unanswered, %s, place %d of the pass",
                           q_name[kind], q_state_name[state], pos + 1);
                }
            }
        }
    }

    /* A pass in which every request is answered touches nothing. */
    arming_init(&a, 0, SETTLE_MS);
    (void)arm_by(100);
    q_link_is_up = true;
    q_link_quiet = false;
    q_bank = true;
    q_dead = -1;
    q_on_wire = 0;
    for (int kind = 0; kind < Q_KINDS; ++kind) {
        CHECK(q_exchange(kind));
    }
    q_poll_decides();
    CHECK_EQ(q_on_wire, Q_KINDS);
    CHECK(q_link_is_up);
    CHECK(a.armed);
    CHECK(!a.stopped);

    /* And the three answers on their own. */
    arming_init(&a, 0, SETTLE_MS);
    arming_touch_seen(&a, 0);
    CHECK_EQ(arming_exchange_unanswered(&a, false, true),
             ARMING_QUIET_NOTHING);
    CHECK_EQ(arming_exchange_unanswered(&a, false, false),
             ARMING_QUIET_NOTHING);
    CHECK_EQ(arming_exchange_unanswered(&a, true, false),
             ARMING_QUIET_LINK_DOWN);
    CHECK_EQ(arming_stop_count(&a), 0);
    CHECK_EQ(arming_exchange_unanswered(&a, true, true), ARMING_QUIET_STOP);
    CHECK_EQ(arming_stop_count(&a), 1);
    /* A second one in the same outage stops nothing twice. */
    CHECK_EQ(arming_exchange_unanswered(&a, true, false),
             ARMING_QUIET_LINK_DOWN);
    CHECK_EQ(arming_stop_count(&a), 1);
    CHECK_EQ(arming_exchange_unanswered(NULL, true, true),
             ARMING_QUIET_LINK_DOWN);
    CHECK_EQ(arming_exchange_unanswered(NULL, false, true),
             ARMING_QUIET_NOTHING);
}

/* ------------------------------------------- the deadline in the handshake */

/* An arm handed out at @p at for a hold at @p t0, the far end asked. */
static void handed_out(uint32_t t0, uint32_t at)
{
    init_asking(t0 - 10u);
    arming_touch_seen(&a, t0);
    arming_request_arm(&a, t0);
    arming_touch_seen(&a, at);
    arming_line_report(&a, true);
    (void)arming_step(&a, at);
}

TEST_CASE(the_arms_deadline_holds_through_the_handshake)
{
    const uint32_t bases[2] = { 1000u, 0xFFFFFFFFu - 249u };  /* and the wrap */
    for (unsigned b = 0; b < 2u; ++b) {
        const uint32_t t0 = bases[b];
        const uint32_t dl = t0 + SETTLE_MS + WAIT_MS;

        /* The first point is arming_step() itself: handed out at the
         * deadline and one before it, given up one past it. */
        handed_out(t0, dl - 1u);
        CHECK(a.armed);
        handed_out(t0, dl);
        CHECK(a.armed);
        handed_out(t0, dl + 1u);
        CHECK(!a.armed);

        /* Before CLEAR. */
        for (int d = -1; d <= 1; ++d) {
            handed_out(t0, t0 + SETTLE_MS);
            const uint32_t stops = arming_stop_count(&a);
            const bool open = arming_handshake_open(&a, dl + (uint32_t)d,
                                                    false);
            if (d <= 0) {
                CHECK(open);
                CHECK(a.armed);
            } else {
                /* Given up as a refusal is: nothing written, no stop, the
                 * line running, the operator can ask again. */
                CHECK(!open);
                CHECK(!a.armed);
                CHECK(!a.arming);
                CHECK(!a.stopped);
                CHECK(arming_heartbeat(&a, dl + 1u));
                CHECK_EQ(arming_stop_count(&a), stops);
                CHECK_EQ(arming_step(&a, dl + 2u), ARMING_ACT_NONE);
            }
        }

        /* Before the frame that arms, the CLEAR acknowledged. */
        for (int d = -1; d <= 1; ++d) {
            handed_out(t0, t0 + SETTLE_MS);
            CHECK(arming_handshake_open(&a, t0 + SETTLE_MS + 1u, false));
            const uint32_t stops = arming_stop_count(&a);
            const uint32_t pressed = arming_pressed_count(&a);
            const bool open = arming_handshake_open(&a, dl + (uint32_t)d,
                                                    true);
            if (d <= 0) {
                CHECK(open);
                CHECK(a.armed);
                CHECK(!a.stopped);
            } else {
                /* Given up as a stop: the line is withheld so the far end,
                 * whose latch the CLEAR released, sets it again. */
                CHECK(!open);
                CHECK(!a.armed);
                CHECK(a.stopped);
                CHECK(!arming_heartbeat(&a, dl + 1u));
                CHECK_EQ(arming_stop_count(&a), stops + 1);
                CHECK_EQ(arming_pressed_count(&a), pressed);
                CHECK_EQ(arming_step(&a, dl + 2u), ARMING_ACT_NONE);
                /* And the next hold clears it. */
                arming_touch_seen(&a, dl + 2000u);
                arming_request_arm(&a, dl + 2000u);
                CHECK(!a.stopped);
                CHECK(a.arming);
            }
        }

        /* 700ms past, an exchange that all but timed out, at both. */
        handed_out(t0, t0 + SETTLE_MS);
        CHECK(!arming_handshake_open(&a, dl + 700u, false));
        CHECK(!a.stopped);
        handed_out(t0, t0 + SETTLE_MS);
        CHECK(!arming_handshake_open(&a, dl + 700u, true));
        CHECK(a.stopped);
    }
}

TEST_CASE(the_handshake_ends_with_whatever_ends_an_arm)
{
    const uint32_t t0 = 1000u;
    const uint32_t at = t0 + SETTLE_MS;
    const uint32_t dl = t0 + SETTLE_MS + WAIT_MS;

    /* STOP between two of its exchanges: the latch, as before; the caller
     * asks arming_stopped() and writes no frame. */
    handed_out(t0, at);
    CHECK(arming_handshake_open(&a, at + 1u, false));
    arming_stop_pressed(&a);
    CHECK(arming_stopped(&a));
    CHECK_EQ(arming_step(&a, at + 5u), ARMING_ACT_DISARM);
    CHECK(!arming_handshake_open(&a, at + 6u, true));      /* no arm in hand */
    CHECK_EQ(arming_stop_count(&a), 1);                    /* and no second */

    /* A disarm. */
    handed_out(t0, at);
    arming_request_disarm(&a);
    CHECK(!arming_handshake_open(&a, at + 1u, false));
    CHECK(!a.stopped);
    CHECK_EQ(arming_stop_count(&a), 0);

    /* The link going quiet under one of its exchanges. */
    handed_out(t0, at);
    CHECK_EQ(arming_exchange_unanswered(&a, true, false), ARMING_QUIET_STOP);
    CHECK(!arming_handshake_open(&a, at + 1u, true));
    CHECK_EQ(arming_stop_count(&a), 1);

    /* A refusal. */
    handed_out(t0, at);
    arming_refused(&a);
    CHECK(!arming_handshake_open(&a, at + 1u, true));
    CHECK(!a.stopped);

    /* An arm that asked no far end has no deadline: with no line wait, and
     * with no far end in the pass. */
    arming_init(&a, 0, SETTLE_MS);
    arming_touch_seen(&a, t0);
    (void)arm_by(t0);
    CHECK(a.armed);
    CHECK(arming_handshake_open(&a, dl + 5000u, false));
    CHECK(arming_handshake_open(&a, dl + 5000u, true));
    init_asking(t0 - 10u);
    arming_touch_seen(&a, t0);
    arming_request_arm(&a, t0);
    arming_touch_seen(&a, dl + 800u);
    arming_line_nobody(&a);
    CHECK_EQ(arming_step(&a, dl + 800u), ARMING_ACT_ARM);
    CHECK(arming_handshake_open(&a, dl + 5000u, true));
    CHECK(a.armed);

    /* A deadline is not carried from one arm to the next. */
    handed_out(t0, at);
    arming_request_disarm(&a);
    arming_set_line_wait(&a, 0u);
    arming_touch_seen(&a, dl + 3000u);
    arming_request_arm(&a, dl + 3000u);
    arming_touch_seen(&a, dl + 3000u + SETTLE_MS);
    CHECK_EQ(arming_step(&a, dl + 3000u + SETTLE_MS), ARMING_ACT_ARM);
    CHECK(arming_handshake_open(&a, dl + 9000u, true));

    CHECK(!arming_handshake_open(NULL, 0, false));
}

/* ------------------------------------------- writing the arm, point by point */

/* What the panel learned, with the link up and nothing in the way. */
static arming_hs_facts_t clear_way(void)
{
    const arming_hs_facts_t f = { .link_up = true, .rate_settled = true };
    return f;
}

/* A programmer that holds the port refuses the arm before anything is done
 * for it, link or no link, and the operator can ask again. */
TEST_CASE(a_programmer_holding_the_port_refuses_the_arm_unwritten)
{
    const uint32_t t0 = 1000u;
    const uint32_t at = t0 + SETTLE_MS;
    for (int up = 0; up <= 1; ++up) {
        arming_hs_facts_t f = clear_way();
        f.link_up    = up != 0;
        f.port_taken = true;
        handed_out(t0, at);
        CHECK_EQ(arming_handshake_step(&a, ARMING_HS_HANDED, &f, at),
                 ARMING_HS_PROG);
        CHECK(!a.armed);
        CHECK(!a.arming);
        CHECK(!a.stopped);
        CHECK_EQ(arming_stop_count(&a), 0);
        CHECK(arming_heartbeat(&a, at));
        CHECK_EQ(arming_step(&a, at + 1u), ARMING_ACT_NONE);
    }

    /* Without it the arm goes on, and nothing is changed. */
    arming_hs_facts_t f = clear_way();
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_HANDED, &f, at),
             ARMING_HS_GO);
    CHECK(a.armed);
    /* Whatever else is wrong is not for this point to say. */
    f.release_owed = true;
    f.rate_settled = false;
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_HANDED, &f, at + 5000u),
             ARMING_HS_GO);
    CHECK(a.armed);
}

/* Before CLEAR, in the order the panel asks: the link, the release, the
 * rate, the deadline. */
TEST_CASE(the_writes_before_clear_are_refused_in_order)
{
    const uint32_t t0 = 1000u;
    const uint32_t at = t0 + SETTLE_MS;
    const uint32_t dl = t0 + SETTLE_MS + WAIT_MS;

    /* No link: this end's bank alone, whatever else holds, the deadline
     * included; the policy is left armed for it. */
    arming_hs_facts_t f = clear_way();
    f.link_up      = false;
    f.release_owed = true;
    f.rate_settled = false;
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, dl + 700u),
             ARMING_HS_LOCAL);
    CHECK(a.armed);
    CHECK(!a.stopped);

    /* An unpaid release before an unknown rate, and both before the
     * deadline.  The policy is the caller's to end: its rule for a link
     * gone quiet decides between a refusal and a stop. */
    f.link_up = true;
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, dl + 700u),
             ARMING_HS_SERVO_HELD);
    CHECK(a.armed);
    CHECK(!a.stopped);
    f.release_owed = false;
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, dl + 700u),
             ARMING_HS_SERVO_RATE);
    CHECK(a.armed);
    CHECK_EQ(arming_stop_count(&a), 0);

    /* The way clear: CLEAR up to and at the deadline. */
    f = clear_way();
    for (int d = -1; d <= 0; ++d) {
        handed_out(t0, at);
        CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f,
                                       dl + (uint32_t)d),
                 ARMING_HS_GO);
        CHECK(a.armed);
    }
    /* One past it, given up as a refusal: nothing written, no stop. */
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, dl + 1u),
             ARMING_HS_CLOSED);
    CHECK(!a.armed);
    CHECK(!a.stopped);
    CHECK_EQ(arming_stop_count(&a), 0);
    CHECK(arming_heartbeat(&a, dl + 1u));

    /* A disarm posted is for the point after CLEAR, not this one. */
    f.disarm_asked = true;
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, at + 1u),
             ARMING_HS_GO);
}

/*
 * The throttle's pulse endpoints owed to the far end: the frame that arms
 * carries no CHAN_CFG, so before CLEAR the arm is refused like an unpaid
 * release, after the release and the rate and before the deadline.  With
 * no link the panel's own bank arms and nothing is written.
 */
TEST_CASE(owed_endpoints_refuse_the_arm_before_clear)
{
    const uint32_t t0 = 1000u;
    const uint32_t at = t0 + SETTLE_MS;
    const uint32_t dl = t0 + SETTLE_MS + WAIT_MS;

    arming_hs_facts_t f = clear_way();
    f.endpoints_blocked = true;
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, at + 1u),
             ARMING_HS_ENDPOINTS);
    /* The policy is the caller's to end, as for SERVO_HELD: answered, a
     * refusal that leaves the bench SAFE, no stop and the line up. */
    CHECK(a.armed);
    CHECK(!a.stopped);
    CHECK(!arming_write_failed(&a, true));
    CHECK(!a.armed);
    CHECK(!a.arming);
    CHECK(!a.stopped);
    CHECK_EQ(arming_stop_count(&a), 0);
    CHECK(arming_heartbeat(&a, at + 1u));
    /* Not tried again by itself: the hold is pressed again. */
    for (uint32_t t = at + 2u; t < at + 2000u; t += 50u) {
        arming_touch_seen(&a, t);
        CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
        CHECK(!a.armed);
    }

    /* Unanswered exchanges before the verdict: a stop, as for SERVO_HELD. */
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, at + 1u),
             ARMING_HS_ENDPOINTS);
    CHECK(arming_write_failed(&a, false));
    CHECK(a.stopped);
    CHECK_EQ(arming_stop_count(&a), 1);

    /* After the release and the rate, before the deadline. */
    f.release_owed = true;
    f.rate_settled = false;
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, dl + 700u),
             ARMING_HS_SERVO_HELD);
    f.release_owed = false;
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, dl + 700u),
             ARMING_HS_SERVO_RATE);
    f.rate_settled = true;
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, dl + 700u),
             ARMING_HS_ENDPOINTS);
    CHECK(a.armed);
    CHECK_EQ(arming_stop_count(&a), 0);

    /* No link: this end's bank, whatever is owed. */
    f.link_up = false;
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, at + 1u),
             ARMING_HS_LOCAL);
    CHECK(a.armed);

    /* Nothing owed: CLEAR. */
    f = clear_way();
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, at + 1u),
             ARMING_HS_GO);

    /* At HANDED the debt is not this point's to say. */
    f.endpoints_blocked = true;
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_HANDED, &f, at),
             ARMING_HS_GO);
}

/*
 * An edit published while CLEAR was on the wire: the frame that arms is not
 * written, and the arm is given up as a refusal.  A stop or a disarm in the
 * same exchange is still the operator's and is told as WITHDRAWN.
 */
TEST_CASE(an_edit_during_clear_refuses_the_frame_that_arms)
{
    const uint32_t t0 = 1000u;
    const uint32_t at = t0 + SETTLE_MS;
    const uint32_t dl = t0 + SETTLE_MS + WAIT_MS;
    arming_hs_facts_t f = clear_way();

    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, at + 1u),
             ARMING_HS_GO);
    f.endpoints_blocked = true;
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, at + 2u),
             ARMING_HS_ENDPOINTS);
    CHECK(!a.armed);
    CHECK(!a.arming);
    CHECK(!a.stopped);
    CHECK_EQ(arming_stop_count(&a), 0);
    CHECK(arming_heartbeat(&a, at + 2u));
    CHECK_EQ(arming_step(&a, at + 3u), ARMING_ACT_NONE);

    /* Before the deadline is asked: owed past it is still ENDPOINTS. */
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, dl + 700u),
             ARMING_HS_ENDPOINTS);
    CHECK(!a.stopped);

    /* A stop in the same exchange comes first. */
    handed_out(t0, at);
    arming_stop_pressed(&a);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, at + 2u),
             ARMING_HS_WITHDRAWN);
    CHECK(a.stopped);
    CHECK(!arming_heartbeat(&a, at + 2u));

    /* And a disarm posted. */
    f.disarm_asked = true;
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, at + 2u),
             ARMING_HS_WITHDRAWN);
}

/*
 * After the frame that arms came back acknowledged: endpoints that are no
 * longer in force -- an edit published while that frame was on the wire --
 * take the arm back with no stop and the line up; otherwise the arm
 * stands.  Nothing else is this point's to say.
 */
TEST_CASE(endpoints_blocked_after_the_arming_frame_take_the_arm_back)
{
    const uint32_t t0 = 1000u;
    const uint32_t at = t0 + SETTLE_MS;
    const uint32_t dl = t0 + SETTLE_MS + WAIT_MS;
    arming_hs_facts_t f = clear_way();

    handed_out(t0, at);
    f.endpoints_blocked = true;
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_ARMED, &f, at + 2u),
             ARMING_HS_ENDPOINTS);
    CHECK(!a.armed);
    CHECK(!a.arming);
    CHECK(!a.stopped);
    CHECK_EQ(arming_stop_count(&a), 0);
    CHECK(arming_heartbeat(&a, at + 2u));
    CHECK_EQ(arming_step(&a, at + 3u), ARMING_ACT_NONE);

    /* Past the deadline, a disarm posted, the release or the rate: the
     * frame has armed the far end, so the arm stands. */
    f = clear_way();
    f.disarm_asked = true;
    f.release_owed = true;
    f.rate_settled = false;
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_ARMED, &f, dl + 700u),
             ARMING_HS_GO);
    CHECK(a.armed);
    CHECK(!a.stopped);
}

/*
 * A STOP after ARMING_ACT_ARM, landing in the CLEAR's exchange: the pump
 * applies it there, and the latch alone does not take the arm back -- the
 * policy is still armed, and the deadline alone would let the frame that
 * arms be written.  The point after CLEAR withdraws it, once.
 */
TEST_CASE(a_stop_after_arm_withdraws_it_after_clear)
{
    const uint32_t t0 = 1000u;
    const uint32_t at = t0 + SETTLE_MS;
    const uint32_t dl = t0 + SETTLE_MS + WAIT_MS;
    const arming_hs_facts_t f = clear_way();

    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, at + 1u),
             ARMING_HS_GO);
    arming_stop_pressed(&a);
    CHECK(a.armed);
    CHECK(arming_handshake_open(&a, at + 2u, true));   /* the gap it closes */

    handed_out(t0, at);
    uint32_t served = arming_stop_count(&a);
    arming_stop_pressed(&a);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, at + 2u),
             ARMING_HS_WITHDRAWN);
    CHECK(!a.armed);
    CHECK(a.stopped);                    /* the latch stands: line withheld */
    CHECK(!arming_heartbeat(&a, at + 2u));
    CHECK_EQ(arming_stop_count(&a), 1);  /* the press, and no second stop */
    CHECK_EQ(arming_pressed_count(&a), 1);
    CHECK_EQ(arming_step(&a, at + 3u), ARMING_ACT_NONE);
    /* And the next pass lets go for it. */
    CHECK(arming_stop_unserved(&a, &served));
    CHECK(!arming_stop_unserved(&a, &served));

    /* A stop past the deadline is still the operator's: withdrawn, not
     * closed, and counted once. */
    handed_out(t0, at);
    arming_stop_pressed(&a);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, dl + 700u),
             ARMING_HS_WITHDRAWN);
    CHECK_EQ(arming_stop_count(&a), 1);

    /* A stop from the far end in the CLEAR's exchange took the arm back
     * itself; the latch withdraws whatever is left. */
    handed_out(t0, at);
    arming_stop_from_far_end(&a);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, at + 2u),
             ARMING_HS_WITHDRAWN);
    CHECK(!a.armed);
    CHECK_EQ(arming_stop_count(&a), 1);
}

/* After CLEAR: a disarm posted withdraws; otherwise the deadline decides,
 * and past it the arm is given up as a stop. */
TEST_CASE(after_clear_a_disarm_withdraws_and_the_deadline_stops)
{
    const uint32_t t0 = 1000u;
    const uint32_t at = t0 + SETTLE_MS;
    const uint32_t dl = t0 + SETTLE_MS + WAIT_MS;
    arming_hs_facts_t f = clear_way();

    f.disarm_asked = true;
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, at + 2u),
             ARMING_HS_WITHDRAWN);
    CHECK(!a.armed);
    CHECK(!a.stopped);
    CHECK_EQ(arming_stop_count(&a), 0);

    f.disarm_asked = false;
    for (int d = -1; d <= 0; ++d) {
        handed_out(t0, at);
        CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f,
                                       dl + (uint32_t)d),
                 ARMING_HS_GO);
        CHECK(a.armed);
        CHECK(!a.stopped);
    }
    handed_out(t0, at);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, dl + 1u),
             ARMING_HS_CLOSED);
    CHECK(!a.armed);
    CHECK(a.stopped);
    CHECK_EQ(arming_stop_count(&a), 1);
    CHECK_EQ(arming_pressed_count(&a), 0);

    /* An arm that asked no far end has no deadline at either point. */
    arming_init(&a, 0, SETTLE_MS);
    arming_touch_seen(&a, t0);
    (void)arm_by(t0);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_PREPARED, &f, dl + 5000u),
             ARMING_HS_GO);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f, dl + 5000u),
             ARMING_HS_GO);

    /* Nothing to judge: given up, and nothing changed. */
    CHECK_EQ(arming_handshake_step(NULL, ARMING_HS_HANDED, &f, at),
             ARMING_HS_CLOSED);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_HANDED, NULL, at),
             ARMING_HS_CLOSED);
    CHECK(a.armed);
}

/*
 * Touch that dies and answers again inside the CLEAR's exchange counts a
 * stop and latches nothing.  The point after CLEAR asks the latch, so the
 * frame that arms is written; the disarm the outage left is the next step's.
 */
TEST_CASE(a_stop_count_that_moves_without_the_latch_disarms_on_the_next_step)
{
    const arming_hs_facts_t f = clear_way();

    /* An arm that asked no far end, so no deadline runs out meanwhile. */
    arming_init(&a, 0, SETTLE_MS);
    const uint32_t t = arm_by(100);
    CHECK(a.armed);
    uint32_t served = arming_stop_count(&a);
    arming_touch_poll(&a, t + ARMING_TOUCH_DEAD_MS);
    arming_touch_seen(&a, t + ARMING_TOUCH_DEAD_MS + 1u);
    arming_touch_poll(&a, t + ARMING_TOUCH_DEAD_MS + 1u);
    CHECK_EQ(arming_stop_count(&a), 1);
    CHECK(!a.stopped);
    CHECK_EQ(arming_handshake_step(&a, ARMING_HS_CLEARED, &f,
                                   t + ARMING_TOUCH_DEAD_MS + 1u),
             ARMING_HS_GO);
    CHECK(a.armed);
    CHECK_EQ(arming_step(&a, t + ARMING_TOUCH_DEAD_MS + 2u),
             ARMING_ACT_DISARM);
    CHECK(arming_stop_unserved(&a, &served));
}

/* The count this task let go for: once per change, however many stops the
 * change holds, and across the counter's wrap. */
TEST_CASE(a_stale_stop_count_is_let_go_for_once)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t served = 0u;
    CHECK(!arming_stop_unserved(&a, &served));

    (void)arm_by(100);
    arming_stop(&a);
    CHECK(arming_stop_unserved(&a, &served));
    CHECK_EQ(served, 1);
    CHECK(!arming_stop_unserved(&a, &served));

    /* Two stops between looks: one let-go, and the count taken whole. */
    arming_stop_pressed(&a);
    arming_stop_from_far_end(&a);
    CHECK(arming_stop_unserved(&a, &served));
    CHECK_EQ(served, 3);
    CHECK(!arming_stop_unserved(&a, &served));

    /* A stale count from either side, the wrap included: != and not >. */
    served = 7u;
    CHECK(arming_stop_unserved(&a, &served));
    CHECK_EQ(served, 3);
    a.stops = 0u;
    served  = 0xFFFFFFFFu;
    CHECK(arming_stop_unserved(&a, &served));
    CHECK_EQ(served, 0);

    CHECK(!arming_stop_unserved(&a, NULL));
    served = 5u;
    CHECK(arming_stop_unserved(NULL, &served));   /* a missing policy is 0 */
    CHECK_EQ(served, 0);
}

/* ------------------------------------------------- every tick, the wrap too */

/*
 * The starts and holds the cases below run from: tick 0, the tick before
 * the wrap, and k ms before the wrap so that the settle (k 49), the bound
 * (k 149), the line wait's middle (k 299) or the touch timeout (k 499)
 * straddles it.  1000 is the reference far from both.
 */
static const uint32_t s_bases[] = {
    1000u, 0u, 1u, 0xFFFFFFFFu, 0xFFFFFFFFu - 49u, 0xFFFFFFFFu - 149u,
    0xFFFFFFFFu - 299u, 0xFFFFFFFFu - 499u, 0xFFFFFFFFu - 2499u,
};
#define N_BASES (sizeof(s_bases) / sizeof(s_bases[0]))

/* The base or case a failure belongs to, in the message. */
#define AT_BASE(cond, base)                                                   \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s (at 0x%08x)", #cond, (unsigned)(base));       \
        }                                                                     \
    } while (0)

/* Armed at exactly @p start, no far end asked: the hold SETTLE_MS before. */
static void run_from(uint32_t start)
{
    const uint32_t hold = start - SETTLE_MS;
    arming_init(&a, hold - 10u, SETTLE_MS);
    arming_touch_seen(&a, hold);
    arming_request_arm(&a, hold);
    arming_touch_seen(&a, start);
    AT_BASE(arming_step(&a, start) == ARMING_ACT_ARM, start);
    AT_BASE(a.armed, start);
    AT_BASE(arming_run_seconds(&a) == 0u, start);
}

/* A pass at @p at with touch answering; the run's seconds after it. */
static uint32_t run_at(uint32_t at)
{
    arming_touch_seen(&a, at);
    (void)arming_step(&a, at);
    return arming_run_seconds(&a);
}

/*
 * Any tick is a start the run clock counts from, 0 included.  A clock that
 * took a zero start for "not started" restarted itself at the next pass of a
 * run that began on tick 0, the tick every 2^32ms (49.7 days) wraps to.
 */
TEST_CASE(every_tick_is_a_start_the_run_clock_counts_from)
{
    const uint32_t starts[] = {
        0u, 1u, 0xFFFFFFFFu, 0xFFFFFFFFu - 1u, 0xFFFFFFFFu - 499u,
        0xFFFFFFFFu - 999u, 0xFFFFFFFFu - 2499u, 1000u,
    };
    for (unsigned i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        const uint32_t s0 = starts[i];
        run_from(s0);
        AT_BASE(run_at(s0 + 1u) == 0u, s0);
        AT_BASE(run_at(s0 + 500u) == 0u, s0);   /* tick 0 for one start */
        AT_BASE(run_at(s0 + 999u) == 0u, s0);
        AT_BASE(run_at(s0 + 1000u) == 1u, s0);  /* the second, to the ms */
        AT_BASE(run_at(s0 + 1999u) == 1u, s0);
        AT_BASE(run_at(s0 + 2000u) == 2u, s0);
        AT_BASE(run_at(s0 + 2500u) == 2u, s0);  /* tick 0 for another */
        AT_BASE(run_at(s0 + 5000u) == 5u, s0);
        AT_BASE(run_at(s0 + 3600000u) == 3600u, s0);
        AT_BASE(a.armed, s0);
    }
}

/*
 * Each timeout fires at its documented millisecond and not one before, on
 * both sides of the wrap and straddling it: the settle (SETTLE_MS, 100ms
 * from the hold), the bound (ARMING_LINE_WAIT_MS past the settle), the
 * handshake's deadline (the bound) and dead touch (ARMING_TOUCH_DEAD_MS,
 * 500ms from the last answer).
 */
TEST_CASE(every_timeout_fires_at_its_millisecond_across_the_wrap)
{
    for (unsigned i = 0; i < N_BASES; ++i) {
        const uint32_t b = s_bases[i];

        /* The settle, nobody asked. */
        arming_init(&a, b - 10u, SETTLE_MS);
        arming_touch_seen(&a, b);
        arming_request_arm(&a, b);
        arming_touch_seen(&a, b + SETTLE_MS - 1u);
        AT_BASE(arming_step(&a, b + SETTLE_MS - 1u) == ARMING_ACT_NONE, b);
        AT_BASE(a.arming && !a.armed, b);
        arming_touch_seen(&a, b + SETTLE_MS);
        AT_BASE(arming_step(&a, b + SETTLE_MS) == ARMING_ACT_ARM, b);

        /* The bound, the far end saying no. */
        const uint32_t bound = b + SETTLE_MS + WAIT_MS;
        init_asking(b - 10u);
        arming_touch_seen(&a, b);
        arming_request_arm(&a, b);
        arming_touch_seen(&a, bound - 1u);
        arming_line_report(&a, false);
        AT_BASE(arming_step(&a, bound - 1u) == ARMING_ACT_NONE, b);
        AT_BASE(a.arming, b);
        arming_touch_seen(&a, bound);
        arming_line_report(&a, false);
        AT_BASE(arming_step(&a, bound) == ARMING_ACT_GIVE_UP, b);
        AT_BASE(!a.arming && !a.armed && !a.stopped, b);

        /* The handshake's deadline, both before and after CLEAR. */
        for (int cleared = 0; cleared <= 1; ++cleared) {
            handed_out(b, b + SETTLE_MS);
            AT_BASE(arming_handshake_open(&a, bound, cleared != 0), b);
            AT_BASE(!arming_handshake_open(&a, bound + 1u, cleared != 0), b);
            AT_BASE(!a.armed, b);
            AT_BASE(a.stopped == (cleared != 0), b);
        }

        /* Dead touch, from the last answer, while armed. */
        run_from(b);
        AT_BASE(arming_step(&a, b + ARMING_TOUCH_DEAD_MS - 1u)
                    == ARMING_ACT_NONE, b);
        AT_BASE(a.armed, b);
        AT_BASE(!arming_touch_dead(&a, b + ARMING_TOUCH_DEAD_MS - 1u), b);
        AT_BASE(arming_step(&a, b + ARMING_TOUCH_DEAD_MS)
                    == ARMING_ACT_DISARM, b);
        AT_BASE(!a.armed, b);
    }
}

/* Every way a run ends, for a run that crossed the wrap: the clock holds
 * the run's length, and the next run counts from zero. */
TEST_CASE(a_run_across_the_wrap_ends_and_holds_whatever_ends_it)
{
    const uint32_t s0 = 0xFFFFFFFFu - 1499u;  /* the wrap 1.5s in */
    for (int how = 0; how < 5; ++how) {
        run_from(s0);
        uint32_t t = s0 + 3000u;              /* 1500 past the wrap */
        AT_BASE(run_at(t) == 3u, how);
        bool stops = true;
        switch (how) {
        case 0:   /* STOP pressed */
            arming_stop_pressed(&a);
            AT_BASE(arming_step(&a, t) == ARMING_ACT_DISARM, how);
            break;
        case 1:   /* the far end */
            arming_stop_from_far_end(&a);
            AT_BASE(arming_step(&a, t) == ARMING_ACT_NONE, how);
            break;
        case 2:   /* the link goes quiet */
            AT_BASE(arming_link_lost(&a, true), how);
            AT_BASE(arming_step(&a, t) == ARMING_ACT_NONE, how);
            break;
        case 3:   /* touch stops answering, 500ms after its last answer */
            AT_BASE(arming_step(&a, t + ARMING_TOUCH_DEAD_MS - 1u)
                        == ARMING_ACT_NONE, how);
            AT_BASE(a.armed, how);
            t += ARMING_TOUCH_DEAD_MS;
            AT_BASE(arming_step(&a, t) == ARMING_ACT_DISARM, how);
            AT_BASE(arming_run_seconds(&a) == 3u, how);
            stops = false;
            break;
        default:  /* the operator disarms */
            arming_request_disarm(&a);
            AT_BASE(arming_step(&a, t) == ARMING_ACT_NONE, how);
            stops = false;
            break;
        }
        AT_BASE(!a.armed && !a.arming, how);
        AT_BASE(a.stopped == stops, how);
        /* The line runs unless a stop is latched or touch is dead. */
        AT_BASE(arming_heartbeat(&a, t) == (!stops && how != 3), how);
        AT_BASE(arming_run_seconds(&a) == 3u, how);

        /* Held, not running on. */
        t += 4000u;
        AT_BASE(run_at(t) == 3u, how);

        /* The next run counts from zero, from its own arm. */
        t = arm_by(t);
        AT_BASE(a.armed, how);
        AT_BASE(arming_run_seconds(&a) == 0u, how);
        AT_BASE(run_at(t + 999u) == 0u, how);
        AT_BASE(run_at(t + 1000u) == 1u, how);
    }
}

/* ------------------------------------------------------------ the roles */

#define ROLE_A 0x01u
#define ROLE_B 0x02u

/* Armed for @p roles and running at t, with touch answering. */
static uint32_t arm_roles_by(uint32_t t, uint8_t roles)
{
    arming_request_arm_roles(&a, t, roles);
    t += SETTLE_MS;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_ARM);
    return t;
}

/* An arm names its roles; a plain arm is every role. */
TEST_CASE(an_arm_carries_the_roles_it_was_asked_for)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_roles_by(100, ROLE_A);
    CHECK(a.armed);
    CHECK_EQ(arming_roles(&a), ROLE_A);

    arming_request_disarm(&a);
    (void)arming_step(&a, t);
    t = arm_by(t + 100);
    CHECK(a.armed);
    CHECK_EQ(arming_roles(&a), ARMING_ROLES_ALL);

    /* No role is no request, and NULL answers none. */
    arming_request_disarm(&a);
    (void)arming_step(&a, t);
    arming_request_arm_roles(&a, t, 0u);
    CHECK(!a.arming);
    t += SETTLE_MS;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    CHECK(!a.armed);
    arming_request_arm_roles(NULL, t, ROLE_A);
    CHECK_EQ(arming_roles(NULL), 0u);
}

/*
 * Other roles while armed go through a disarm: the next step writes ARM = 0
 * and nothing else, and the arm for the new roles follows the settle as any
 * arm does, with a new run.
 */
TEST_CASE(a_role_switch_while_armed_disarms_first)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_roles_by(100, ROLE_A);
    t += 3000;
    arming_touch_seen(&a, t);
    (void)arming_step(&a, t);
    CHECK_EQ(arming_run_seconds(&a), 3u);

    arming_request_arm_roles(&a, t, ROLE_B);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_DISARM);
    CHECK(!a.armed);
    CHECK_EQ(arming_roles(&a), ROLE_B);
    /* Nothing until the settle, even with the time already there. */
    t += 1;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    t += SETTLE_MS;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_ARM);
    CHECK(a.armed);
    t += 1000;
    arming_touch_seen(&a, t);
    (void)arming_step(&a, t);
    CHECK_EQ(arming_run_seconds(&a), 1u);
}

/* The same roles again re-assert the arm: no disarm in between. */
TEST_CASE(the_same_roles_again_do_not_disarm)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_roles_by(100, ROLE_A);
    arming_request_arm_roles(&a, t, ROLE_A);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    CHECK(a.armed);
    t += SETTLE_MS;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_ARM);
    CHECK(a.armed);
}

/* A stop during a switch: the disarm, and no arm for the new roles. */
TEST_CASE(a_stop_during_a_role_switch_arms_nothing)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_roles_by(100, ROLE_A);
    arming_request_arm_roles(&a, t, ROLE_B);
    arming_stop(&a);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_DISARM);
    CHECK(!a.armed);
    for (int i = 0; i < 10; ++i) {
        t += SETTLE_MS;
        arming_touch_seen(&a, t);
        CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
        CHECK(!a.armed);
    }

    /* Touch that dies during a switch: the disarm all the same. */
    arming_init(&a, 0, SETTLE_MS);
    t = arm_roles_by(100, ROLE_A);
    arming_request_arm_roles(&a, t, ROLE_B);
    t += 10u * SETTLE_MS + 10000u;           /* no touch since */
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_DISARM);
    t += SETTLE_MS;
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    CHECK(!a.armed);
}

/* Other roles asked for while an arm settles replace it, with no disarm. */
TEST_CASE(other_roles_while_settling_replace_the_arm)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = 100;
    arming_request_arm_roles(&a, t, ROLE_A);
    t += SETTLE_MS / 2u;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    arming_request_arm_roles(&a, t, ROLE_B);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_NONE);
    t += SETTLE_MS;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_ARM);
    CHECK_EQ(arming_roles(&a), ROLE_B);
}

/*
 * The new arm follows the ARM = 0 write by the settle, whenever the DISARM
 * step comes after the request.
 */
TEST_CASE(a_late_role_switch_disarm_still_gets_the_whole_settle)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_roles_by(100, ROLE_A);
    arming_request_arm_roles(&a, t, ROLE_B);
    const uint32_t disarm_at = t + 60u;
    arming_touch_seen(&a, disarm_at);
    CHECK_EQ(arming_step(&a, disarm_at), ARMING_ACT_DISARM);
    CHECK(arming_link_needed(&a, false));       /* the re-arm is still owed */
    for (uint32_t k = 1; k < SETTLE_MS; k += 10u) {
        arming_touch_seen(&a, disarm_at + k);
        CHECK_EQ(arming_step(&a, disarm_at + k), ARMING_ACT_NONE);
        CHECK(arming_link_needed(&a, false));
    }
    arming_touch_seen(&a, disarm_at + SETTLE_MS);
    CHECK_EQ(arming_step(&a, disarm_at + SETTLE_MS), ARMING_ACT_ARM);
    CHECK(a.armed);
}

/* The same across the wrap of the millisecond clock. */
TEST_CASE(a_late_role_switch_disarm_settles_across_the_clock_wrap)
{
    arming_init(&a, 0xFFFFFF00u, SETTLE_MS);
    uint32_t t = arm_roles_by(0xFFFFFF10u, ROLE_A);
    arming_request_arm_roles(&a, t, ROLE_B);
    const uint32_t disarm_at = t + 60u;
    arming_touch_seen(&a, disarm_at);
    CHECK_EQ(arming_step(&a, disarm_at), ARMING_ACT_DISARM);
    arming_touch_seen(&a, disarm_at + SETTLE_MS - 1u);
    CHECK_EQ(arming_step(&a, disarm_at + SETTLE_MS - 1u), ARMING_ACT_NONE);
    arming_touch_seen(&a, disarm_at + SETTLE_MS);
    CHECK_EQ(arming_step(&a, disarm_at + SETTLE_MS), ARMING_ACT_ARM);
}

/* A role switch is a new run: the clock stops at the disarm and starts
 * again at the arm. */
TEST_CASE(a_role_switch_restarts_the_run_clock)
{
    arming_init(&a, 0, SETTLE_MS);
    uint32_t t = arm_roles_by(100, ROLE_A);
    t += 5000;
    arming_touch_seen(&a, t);
    (void)arming_step(&a, t);
    CHECK(a.in_run);
    CHECK_EQ(arming_run_seconds(&a), 5u);

    arming_request_arm_roles(&a, t, ROLE_B);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_DISARM);
    CHECK(!a.in_run);
    /* No pass between the disarm and the arm: the run still starts anew. */
    t += SETTLE_MS;
    arming_touch_seen(&a, t);
    CHECK_EQ(arming_step(&a, t), ARMING_ACT_ARM);
    t += 2000;
    arming_touch_seen(&a, t);
    (void)arming_step(&a, t);
    CHECK_EQ(arming_run_seconds(&a), 2u);
}

/* The settle the docs state is 100ms. */
_Static_assert(HEARTBEAT_SETTLE_MS == 100u, "the role switch settles 100 ms");

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
    RUN(a_pressed_stop_is_counted_apart_from_the_benchs_own);
    RUN(touch_that_stops_answering_counts_once);
    RUN(a_settling_arm_is_abandoned_when_touch_dies);
    RUN(touch_health_can_be_judged_apart_from_the_policy_step);
    RUN(an_outage_that_recovers_still_brings_the_bank_down);
    RUN(the_panel_passes_the_shared_settle_and_bound);
    RUN(an_arm_waits_for_the_far_end_to_trust_the_line);
    RUN(a_line_already_trusted_arms_at_the_settle);
    RUN(an_arm_the_far_end_never_trusts_is_given_up_at_the_bound);
    RUN(the_wait_for_the_line_survives_a_millisecond_wrap);
    RUN(a_wait_for_the_line_ends_with_whatever_ends_an_arm);
    RUN(without_a_far_end_to_ask_the_settle_alone_decides);
    RUN(a_link_lost_while_armed_is_a_stop);
    RUN(a_link_lost_while_disarmed_changes_nothing);
    RUN(the_bank_alone_being_armed_is_enough_for_a_link_stop);
    RUN(a_far_end_that_appears_stops_a_bank_armed_without_one);
    RUN(a_write_nobody_answers_is_a_stop_and_a_refusal_is_not);
    RUN(a_trusted_report_past_the_bound_arms_nothing);
    RUN(with_no_far_end_in_the_pass_the_settle_alone_decides);
    RUN(an_unanswered_exchange_is_judged_alike_whatever_it_carried);
    RUN(the_arms_deadline_holds_through_the_handshake);
    RUN(the_handshake_ends_with_whatever_ends_an_arm);
    RUN(a_programmer_holding_the_port_refuses_the_arm_unwritten);
    RUN(the_writes_before_clear_are_refused_in_order);
    RUN(owed_endpoints_refuse_the_arm_before_clear);
    RUN(an_edit_during_clear_refuses_the_frame_that_arms);
    RUN(endpoints_blocked_after_the_arming_frame_take_the_arm_back);
    RUN(a_stop_after_arm_withdraws_it_after_clear);
    RUN(after_clear_a_disarm_withdraws_and_the_deadline_stops);
    RUN(a_stop_count_that_moves_without_the_latch_disarms_on_the_next_step);
    RUN(a_stale_stop_count_is_let_go_for_once);
    RUN(every_tick_is_a_start_the_run_clock_counts_from);
    RUN(every_timeout_fires_at_its_millisecond_across_the_wrap);
    RUN(a_run_across_the_wrap_ends_and_holds_whatever_ends_it);
    RUN(an_arm_carries_the_roles_it_was_asked_for);
    RUN(a_role_switch_while_armed_disarms_first);
    RUN(a_late_role_switch_disarm_still_gets_the_whole_settle);
    RUN(a_late_role_switch_disarm_settles_across_the_clock_wrap);
    RUN(a_role_switch_restarts_the_run_clock);
    RUN(the_same_roles_again_do_not_disarm);
    RUN(a_stop_during_a_role_switch_arms_nothing);
    RUN(other_roles_while_settling_replace_the_arm);
    return test_summary("arming");
}
