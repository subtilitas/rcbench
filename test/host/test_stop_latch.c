/**
 * @file test_stop_latch.c
 * @brief The panel's STOP as an emergency stop: one touch latches it, a hold
 * of 2s on one unbroken contact asks for the reset, and the reset machine
 * walks LATCHED, HOLD_DONE, ASKING, CONFIRMED, RESUMED, CLEAR_WRITTEN and
 * UNLATCHED, the heartbeat given back only once the IOmcu has confirmed
 * everything off and 500ms have passed since the STOP.  The policy
 * (arming.h) and the machine (stop_latch.h) together, with an IOmcu of
 * registers behind the machine's reads.
 *
 * Cases:
 * - a_touch_latches_and_withholds_the_heartbeat
 * - an_arm_is_refused_while_the_stop_stands
 * - a_stop_from_the_far_end_still_clears_with_an_arm
 * - the_reset_walks_every_state_in_order
 * - a_hold_ending_1899ms_after_the_stop_is_refused_and_1900ms_runs
 * - a_stop_after_the_hold_began_refuses_it
 * - a_contact_break_during_the_hold_refuses_it
 * - a_break_in_the_hold_counted_after_it_sends_the_reset_back
 * - a_finger_leaving_after_the_hold_lets_the_reset_finish
 * - a_stop_from_any_state_goes_back_to_latched
 * - a_stop_inside_a_read_counts_before_the_reading
 * - a_hold_before_the_confirmation_restarts_nothing
 * - a_confirmation_at_the_bound_resets_and_a_late_one_gives_up
 * - no_confirmation_keeps_the_latch_and_the_line_withheld
 * - the_floor_counts_from_the_pass_that_saw_the_stop
 * - an_error_sends_a_reset_back_and_changes_nothing_latched
 * - clear_is_written_once_per_reset_after_the_heartbeat
 * - the_heartbeat_resumes_only_after_the_confirmation
 * - every_served_page_and_this_end_must_read_off
 * - with_no_link_this_end_confirms_and_no_clear_is_written
 * - nothing_switches_on_after_the_reset
 * - the_heartbeat_fault_is_hidden_while_latched_and_after_the_reset
 * - the_hold_and_the_bound_hold_across_the_millisecond_wrap
 * - a_restart_comes_up_unlatched
 * - null_is_harmless
 * - the_confirmation_is_read_every_50ms_while_asking
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "greatest.h"

#include "arming.h"
#include "heartbeat.h"
#include "link_pages.h"
#include "stop_latch.h"

/** @brief The settle the panel passes. */
#define SETTLE_MS HEARTBEAT_SETTLE_MS

static arming_t     a;
static stop_latch_t l;

/** @brief The world around the machine: the IOmcu's registers and what the
 *         machine did through its io. */
static struct {
    uint32_t t;             /**< the clock, ms                            */
    uint32_t press_ms;      /**< when the latest STOP was applied         */
    uint32_t breaks;        /**< contact breaks counted                   */
    uint32_t break_ms;      /**< when the latest was counted              */
    bool     link_up;       /**< the link answers                         */
    bool     local_on;      /**< something on at this end                 */
    bool     supply_page;   /**< the IOmcu serves SUPPLY                  */
    bool     prog_page;     /**< the IOmcu serves PROG_SUPPLY             */
    bool     answers;       /**< the IOmcu answers reads                  */
    uint16_t state;         /**< STATUS LINK_ST_STATE                     */
    uint16_t sp_flags;      /**< SUPPLY LINK_SP_FLAGS                     */
    uint16_t ps_flags;      /**< PROG_SUPPLY LINK_PS_FLAGS                */
    bool     stop_in_read;  /**< the next read lands a STOP               */
    int      reads;         /**< reads made                               */
    int      clears;        /**< CLEARs written                           */
    int      resumes;       /**< heartbeats given back                    */
    bool     beat_at_clear; /**< the heartbeat went at the last CLEAR     */
} w;

/** @brief The inputs as the control task reads them now. */
static void read_in(stop_latch_in_t *in)
{
    in->now_ms      = w.t;
    in->pressed     = arming_pressed_count(&a);
    in->press_ms    = w.press_ms;
    in->breaks      = w.breaks;
    in->break_ms    = w.break_ms;
    in->held        = arming_held(&a);
    in->link_up     = w.link_up;
    in->local_on    = w.local_on;
    in->supply_page = w.supply_page;
    in->prog_page   = w.prog_page;
}

/** @brief An operator's STOP, applied at the clock. */
static void stop_now(void)
{
    arming_stop_pressed(&a);
    w.press_ms = w.t;
}

static bool io_read(void *ctx, uint8_t page, uint8_t reg, uint16_t *out)
{
    (void)ctx;
    ++w.reads;
    if (w.stop_in_read) {
        w.stop_in_read = false;
        stop_now();
    }
    if (!w.answers) {
        return false;
    }
    if (page == LINK_PAGE_STATUS && reg == LINK_ST_STATE) {
        *out = w.state;
    } else if (page == LINK_PAGE_SUPPLY && reg == LINK_SP_FLAGS) {
        *out = w.sp_flags;
    } else if (page == LINK_PAGE_PROG_SUPPLY && reg == LINK_PS_FLAGS) {
        *out = w.ps_flags;
    } else {
        return false;
    }
    return true;
}

static void io_resume(void *ctx)
{
    (void)ctx;
    ++w.resumes;
    arming_stop_release(&a);
}

static void io_clear(void *ctx)
{
    (void)ctx;
    ++w.clears;
    w.beat_at_clear = arming_heartbeat(&a, w.t);
}

static void io_refresh(void *ctx, stop_latch_in_t *in)
{
    (void)ctx;
    read_in(in);
}

static const stop_latch_io_t io = {
    .ctx = NULL, .read = io_read, .resume = io_resume, .clear = io_clear,
    .refresh = io_refresh,
};

/** @brief A fresh panel at @p t, the link up, the IOmcu answering SAFE with
 *         everything off. */
static void start(uint32_t t)
{
    memset(&w, 0, sizeof(w));
    w.t       = t;
    w.link_up = true;
    w.answers = true;
    arming_init(&a, t, SETTLE_MS);
    arming_touch_seen(&a, t);
    stop_latch_in_t in = { 0 };
    read_in(&in);
    stop_latch_init(&l, &in);
}

/** @brief One pass at @p t; @p hold_ms is a completed hold's stamp, or
 *         UINT32_MAX for none. */
static stop_latch_act_t pass_hold(uint32_t t, uint32_t hold_ms)
{
    w.t = t;
    arming_touch_seen(&a, t);
    stop_latch_in_t in = { 0 };
    read_in(&in);
    in.hold_done = hold_ms != UINT32_MAX;
    in.hold_ms   = in.hold_done ? hold_ms : 0u;
    const stop_latch_act_t act = stop_latch_run(&l, &in, &io);
    (void)arming_step(&a, w.t);
    return act;
}

/** @brief One pass at @p t with no hold. */
static stop_latch_act_t pass(uint32_t t)
{
    return pass_hold(t, UINT32_MAX);
}

/** @brief A STOP at @p t, seen by the pass at @p t. */
static void touch_stop(uint32_t t)
{
    w.t = t;
    stop_now();
    (void)pass(t);
}

/** @brief Passes every 5ms from @p t until the machine is in @p s, for at
 *         most 3000ms.  Returns the time of the pass that reached it, or
 *         @p t + 3000 when none did. */
static uint32_t run_to(uint32_t t, stop_latch_state_t s)
{
    const uint32_t end = t + 3000u;
    for (; t != end && stop_latch_state(&l) != s; t += 5u) {
        (void)pass(t);
        if (stop_latch_state(&l) == s) {
            return t;
        }
    }
    return t;
}

/** @brief A STOP at @p t and a hold that completed 2000ms later, taken.
 *         Returns the time of that pass. */
static uint32_t stop_and_hold(uint32_t t)
{
    touch_stop(t);
    (void)pass_hold(t + 2000u, t + 2000u);
    return t + 2000u;
}

TEST_CASE(a_touch_latches_and_withholds_the_heartbeat)
{
    start(1000u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_UNLATCHED);
    CHECK(arming_heartbeat(&a, 1000u));
    touch_stop(1000u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
    CHECK(arming_held(&a));
    CHECK(arming_stopped(&a));
    CHECK(!arming_heartbeat(&a, 1000u));
    /* Time alone releases nothing, and nothing is read. */
    for (uint32_t t = 1000u; t <= 61000u; t += 500u) {
        CHECK_EQ(pass(t), STOP_LATCH_NONE);
        CHECK(arming_held(&a));
        CHECK(!arming_heartbeat(&a, t));
    }
    CHECK_EQ(w.reads, 0);
}

TEST_CASE(an_arm_is_refused_while_the_stop_stands)
{
    start(0u);
    touch_stop(100u);
    arming_request_arm(&a, 200u);
    CHECK(arming_held(&a));
    CHECK(arming_stopped(&a));
    CHECK(!arming_heartbeat(&a, 200u));
    /* Past the settle and any wait: nothing arms. */
    for (uint32_t t = 200u; t <= 2000u; t += 5u) {
        arming_touch_seen(&a, t);
        arming_line_nobody(&a);
        CHECK(arming_step(&a, t) != ARMING_ACT_ARM);
    }
    CHECK(!a.armed);
    arming_request_arm_roles(&a, 2000u, 0x01u);
    CHECK_EQ(arming_roles(&a), 0u);
}

TEST_CASE(a_stop_from_the_far_end_still_clears_with_an_arm)
{
    start(0u);
    arming_stop_from_far_end(&a);
    CHECK(arming_stopped(&a));
    CHECK(!arming_held(&a));
    /* The far end's own stop may be the emergency stop or an open wire:
     * its FAULT 10 stays on the band, and no hold resets it. */
    CHECK_EQ(stop_latch_faults(&l, 50u, arming_held(&a),
                               (uint16_t)LINK_FAULT_HEARTBEAT),
             (uint16_t)LINK_FAULT_HEARTBEAT);
    (void)pass_hold(5000u, 5000u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_UNLATCHED);
    arming_request_arm(&a, 5000u);
    CHECK(!arming_stopped(&a));
    CHECK(arming_heartbeat(&a, 5000u));
}

TEST_CASE(the_reset_walks_every_state_in_order)
{
    start(0u);
    w.supply_page = true;
    w.prog_page   = true;
    touch_stop(0u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
    (void)pass_hold(2000u, 2000u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_HOLD_DONE);
    CHECK_EQ(w.reads, 0);
    /* The read waits for a controller that went silent at the hold's end
     * to count as a break. */
    (void)pass(2000u + STOP_LATCH_FRESH_MS - 1u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_HOLD_DONE);
    (void)pass(2000u + STOP_LATCH_FRESH_MS);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_ASKING);
    CHECK_EQ(w.reads, 3);   /* STATUS, SUPPLY, PROG_SUPPLY */
    CHECK(!arming_heartbeat(&a, w.t));
    (void)pass(2105u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_CONFIRMED);
    CHECK_EQ(w.resumes, 0);
    CHECK(!arming_heartbeat(&a, w.t));
    (void)pass(2110u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_RESUMED);
    CHECK_EQ(w.resumes, 1);
    CHECK(arming_heartbeat(&a, w.t));
    CHECK_EQ(w.clears, 0);
    (void)pass(2115u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_CLEAR_WRITTEN);
    CHECK_EQ(w.clears, 1);
    CHECK_EQ(pass(2120u), STOP_LATCH_DONE);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_UNLATCHED);
    CHECK(!arming_held(&a));
    CHECK(!arming_stopped(&a));
    CHECK(!a.armed);
    CHECK_EQ(stop_latch_resets(&l), 1u);
    CHECK_EQ(w.reads, 3);
}

TEST_CASE(a_hold_ending_1899ms_after_the_stop_is_refused_and_1900ms_runs)
{
    start(0u);
    touch_stop(1000u);
    (void)pass_hold(3000u, 1000u + STOP_LATCH_HOLD_SEEN_MS - 1u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
    CHECK_EQ(stop_latch_refused(&l), 1u);
    CHECK_EQ(run_to(3005u, STOP_LATCH_UNLATCHED), 3005u + 3000u);
    CHECK(!arming_heartbeat(&a, w.t));
    (void)pass_hold(6100u, 1000u + STOP_LATCH_HOLD_SEEN_MS);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_HOLD_DONE);
    (void)run_to(6105u, STOP_LATCH_UNLATCHED);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_UNLATCHED);
    CHECK(arming_heartbeat(&a, w.t));
}

TEST_CASE(a_stop_after_the_hold_began_refuses_it)
{
    start(0u);
    touch_stop(0u);
    /* The hold completed at 2000ms, and a STOP was applied at 2010ms,
     * before the control task took the request. */
    touch_stop(2010u);
    (void)pass_hold(2020u, 2000u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
    for (uint32_t t = 2020u; t <= 5000u; t += 50u) {
        CHECK_EQ(pass(t), STOP_LATCH_NONE);
        CHECK(!arming_heartbeat(&a, t));
    }
    CHECK(arming_held(&a));
    CHECK_EQ(w.reads, 0);
}

TEST_CASE(a_contact_break_during_the_hold_refuses_it)
{
    start(0u);
    touch_stop(0u);
    /* The controller stopped answering 1500ms into the hold; the render
     * side's hold, fed no events, would run on. */
    w.breaks   = 1u;
    w.break_ms = 1600u;
    (void)pass(1600u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
    (void)pass_hold(2000u, 2000u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
    CHECK_EQ(stop_latch_refused(&l), 1u);
    CHECK_EQ(w.reads, 0);
    CHECK(arming_held(&a));
    /* A new hold on a contact that came back runs: its press is a STOP
     * after the break. */
    (void)run_to(stop_and_hold(3000u), STOP_LATCH_UNLATCHED);
    CHECK(!arming_held(&a));
}

TEST_CASE(a_break_in_the_hold_counted_after_it_sends_the_reset_back)
{
    start(0u);
    (void)stop_and_hold(0u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_HOLD_DONE);
    /* The controller went silent at 1950ms; 100ms on, at 2050ms, it counts
     * as a break: after the hold was taken, before the first read. */
    w.breaks   = 1u;
    w.break_ms = 2050u;
    (void)pass(2050u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
    CHECK_EQ(run_to(2055u, STOP_LATCH_ASKING), 2055u + 3000u);
    CHECK_EQ(w.reads, 0);
    CHECK(!arming_heartbeat(&a, w.t));
}

TEST_CASE(a_finger_leaving_after_the_hold_lets_the_reset_finish)
{
    start(0u);
    /* The IOmcu reads ARMED for a while: the reset stays in ASKING. */
    w.state = (uint16_t)LINK_STATE_ARMED;
    (void)stop_and_hold(0u);
    CHECK_EQ(run_to(2005u, STOP_LATCH_ASKING), 2100u);
    /* The finger left at 2001ms, after the hold completed; it counts as
     * a break 100ms on. */
    w.breaks   = 1u;
    w.break_ms = 2001u + STOP_LATCH_FRESH_MS;
    (void)pass(w.break_ms);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_ASKING);
    w.state = 0u;
    (void)run_to(w.t + 5u, STOP_LATCH_UNLATCHED);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_UNLATCHED);
    CHECK_EQ(w.resumes, 1);
    CHECK_EQ(w.clears, 1);
    CHECK(!arming_held(&a));
}

TEST_CASE(a_stop_from_any_state_goes_back_to_latched)
{
    const stop_latch_state_t states[] = {
        STOP_LATCH_UNLATCHED, STOP_LATCH_LATCHED, STOP_LATCH_HOLD_DONE,
        STOP_LATCH_ASKING, STOP_LATCH_CONFIRMED, STOP_LATCH_RESUMED,
        STOP_LATCH_CLEAR_WRITTEN,
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        start(0u);
        w.state = (uint16_t)LINK_STATE_ARMED;   /* keeps ASKING asking */
        uint32_t t = 0u;
        if (states[i] != STOP_LATCH_UNLATCHED) {
            t = stop_and_hold(0u);
        }
        if (states[i] == STOP_LATCH_LATCHED) {
            touch_stop(t);   /* a STOP after the hold: back to LATCHED */
        }
        if (states[i] >= STOP_LATCH_ASKING) {
            t = run_to(t, STOP_LATCH_ASKING);
        }
        if (states[i] >= STOP_LATCH_CONFIRMED) {
            w.state = 0u;
            t = run_to(t, states[i]);
        }
        CHECK_EQ(stop_latch_state(&l), states[i]);
        const int clears  = w.clears;
        const int resumes = w.resumes;
        touch_stop(t + 1u);
        CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
        CHECK(arming_held(&a));
        CHECK(!arming_heartbeat(&a, w.t));
        /* Nothing more is done for the reset that was under way. */
        (void)run_to(w.t + 5u, STOP_LATCH_UNLATCHED);
        CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
        CHECK_EQ(w.clears, clears);
        CHECK_EQ(w.resumes, resumes);
    }
}

TEST_CASE(a_stop_inside_a_read_counts_before_the_reading)
{
    start(0u);
    (void)stop_and_hold(0u);
    /* The exchange pumps the touch; a STOP lands in it. */
    w.stop_in_read = true;
    (void)pass(2100u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
    CHECK_EQ(w.reads, 1);
    (void)run_to(2105u, STOP_LATCH_UNLATCHED);
    CHECK_EQ(w.resumes, 0);
    CHECK(arming_held(&a));
}

TEST_CASE(a_hold_before_the_confirmation_restarts_nothing)
{
    start(0u);
    w.state = (uint16_t)LINK_STATE_ARMED;
    (void)stop_and_hold(0u);
    (void)pass(2100u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_ASKING);
    const uint32_t ask = l.ask_ms;
    /* A second request while asking, with no STOP between: nobody takes
     * it. */
    (void)pass_hold(2200u, 2200u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_ASKING);
    CHECK_EQ(l.ask_ms, ask);
    /* And one on an unlatched bench does nothing. */
    start(0u);
    (void)pass_hold(2000u, 2000u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_UNLATCHED);
    CHECK_EQ(w.reads, 0);
}

TEST_CASE(a_confirmation_at_the_bound_resets_and_a_late_one_gives_up)
{
    /* The bound is 2000ms after the first read. */
    for (uint32_t late = 0u; late <= 1u; ++late) {
        start(0u);
        w.state = (uint16_t)LINK_STATE_ARMED;
        (void)stop_and_hold(0u);
        (void)pass(2100u);
        const uint32_t ask = l.ask_ms;
        CHECK_EQ(ask, 2100u);
        for (uint32_t t = ask + 50u; t < ask + STOP_LATCH_CONFIRM_MS;
             t += 50u) {
            CHECK_EQ(pass(t), STOP_LATCH_NONE);
        }
        /* The reading that confirms is made at the bound; the step that
         * acts on it comes @p late ms past it. */
        w.state = 0u;
        CHECK_EQ(pass(ask + STOP_LATCH_CONFIRM_MS), STOP_LATCH_NONE);
        CHECK_EQ(pass(ask + STOP_LATCH_CONFIRM_MS + late),
                 late != 0u ? STOP_LATCH_GIVE_UP : STOP_LATCH_NONE);
        CHECK_EQ(stop_latch_state(&l),
                 late != 0u ? STOP_LATCH_LATCHED : STOP_LATCH_CONFIRMED);
    }
}

TEST_CASE(no_confirmation_keeps_the_latch_and_the_line_withheld)
{
    start(0u);
    w.answers = false;   /* the IOmcu never answers the read */
    (void)stop_and_hold(0u);
    int give_ups = 0;
    for (uint32_t t = 2005u; t <= 10000u; t += 5u) {
        if (pass(t) == STOP_LATCH_GIVE_UP) {
            ++give_ups;
            CHECK_EQ(t, 2100u + STOP_LATCH_CONFIRM_MS + 5u);
        }
        CHECK(arming_held(&a));
        CHECK(!arming_heartbeat(&a, t));
    }
    /* Told once; the latch stands and the next hold asks again. */
    CHECK_EQ(give_ups, 1);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
    CHECK_EQ(w.resumes, 0);
    CHECK_EQ(w.clears, 0);
    w.answers = true;
    (void)run_to(stop_and_hold(10000u), STOP_LATCH_UNLATCHED);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_UNLATCHED);
    CHECK(!arming_held(&a));
}

TEST_CASE(the_floor_counts_from_the_pass_that_saw_the_stop)
{
    start(0u);
    /* A STOP stamped at 0ms that the machine first saw at 1800ms: the hold
     * passes the 1900ms rule by its stamps, and the floor still holds the
     * heartbeat until 2300ms. */
    stop_now();
    (void)pass(1800u);
    (void)pass_hold(1900u, 1900u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_HOLD_DONE);
    for (uint32_t t = 2000u; t < 1800u + STOP_LATCH_FLOOR_MS; ++t) {
        (void)pass(t);
        CHECK(stop_latch_state(&l) <= STOP_LATCH_ASKING);
        CHECK(!arming_heartbeat(&a, t));
    }
    (void)pass(1800u + STOP_LATCH_FLOOR_MS);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_CONFIRMED);
}

TEST_CASE(an_error_sends_a_reset_back_and_changes_nothing_latched)
{
    const stop_latch_state_t states[] = {
        STOP_LATCH_HOLD_DONE, STOP_LATCH_ASKING, STOP_LATCH_CONFIRMED,
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        start(0u);
        w.state = (uint16_t)LINK_STATE_ARMED;
        uint32_t t = stop_and_hold(0u);
        if (states[i] >= STOP_LATCH_ASKING) {
            t = run_to(t, STOP_LATCH_ASKING);
        }
        if (states[i] == STOP_LATCH_CONFIRMED) {
            w.state = 0u;
            t = run_to(t, STOP_LATCH_CONFIRMED);
        }
        CHECK_EQ(stop_latch_state(&l), states[i]);
        CHECK(stop_latch_error(&l));
        CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
        (void)run_to(t + 5u, STOP_LATCH_UNLATCHED);
        CHECK_EQ(w.resumes, 0);
        CHECK(arming_held(&a));
    }
    /* Latched, an error changes nothing: its disarm leaves the stop. */
    start(0u);
    touch_stop(0u);
    CHECK(!stop_latch_error(&l));
    arming_request_disarm(&a);
    CHECK(arming_held(&a));
    CHECK(!arming_heartbeat(&a, 100u));
    arming_request_arm(&a, 100u);
    CHECK(arming_held(&a));
    /* Once the heartbeat goes again, the reset finishes. */
    start(0u);
    (void)run_to(stop_and_hold(0u), STOP_LATCH_RESUMED);
    CHECK(!stop_latch_error(&l));
    CHECK_EQ(pass(w.t + 5u), STOP_LATCH_NONE);
    CHECK_EQ(pass(w.t + 5u), STOP_LATCH_DONE);
    CHECK_EQ(w.clears, 1);
}

TEST_CASE(clear_is_written_once_per_reset_after_the_heartbeat)
{
    start(0u);
    (void)run_to(stop_and_hold(0u), STOP_LATCH_UNLATCHED);
    for (uint32_t t = w.t; t <= 8000u; t += 5u) {
        (void)pass(t);
    }
    CHECK_EQ(w.clears, 1);
    CHECK(w.beat_at_clear);
    CHECK_EQ(stop_latch_resets(&l), 1u);
    /* A second stop and its reset: one more. */
    (void)run_to(stop_and_hold(9000u), STOP_LATCH_UNLATCHED);
    CHECK_EQ(w.clears, 2);
    CHECK_EQ(stop_latch_resets(&l), 2u);
}

TEST_CASE(the_heartbeat_resumes_only_after_the_confirmation)
{
    start(0u);
    w.sp_flags    = LINK_SP_ON;
    w.supply_page = true;
    (void)stop_and_hold(0u);
    for (uint32_t t = 2005u; t < 3000u; t += 5u) {
        CHECK_EQ(pass(t), STOP_LATCH_NONE);
        CHECK(!arming_heartbeat(&a, t));
        CHECK(stop_latch_state(&l) <= STOP_LATCH_ASKING);
    }
    w.sp_flags = 0u;
    (void)run_to(3000u, STOP_LATCH_RESUMED);
    CHECK(arming_heartbeat(&a, w.t));
    CHECK_EQ(w.resumes, 1);
}

TEST_CASE(every_served_page_and_this_end_must_read_off)
{
    for (int c = 0; c < 6; ++c) {
        start(0u);
        w.supply_page = true;
        w.prog_page   = true;
        switch (c) {
        case 0: w.state = (uint16_t)LINK_STATE_ARMED; break;
        case 1: w.sp_flags = LINK_SP_ON; break;
        case 2: w.ps_flags = LINK_PS_ON; break;
        case 3: w.local_on = true; break;
        case 4: w.answers = false; break;
        default: w.prog_page = false; w.ps_flags = LINK_PS_ON; break;
        }
        (void)run_to(stop_and_hold(0u), STOP_LATCH_RESUMED);
        /* Only a page the IOmcu does not serve is not asked. */
        CHECK_EQ(stop_latch_state(&l) == STOP_LATCH_RESUMED, c == 5);
    }
}

TEST_CASE(with_no_link_this_end_confirms_and_no_clear_is_written)
{
    start(0u);
    w.link_up = false;
    (void)run_to(stop_and_hold(0u), STOP_LATCH_RESUMED);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_RESUMED);
    CHECK_EQ(w.reads, 0);
    CHECK_EQ(pass(w.t + 5u), STOP_LATCH_DONE);
    CHECK_EQ(w.clears, 0);
    CHECK(!arming_held(&a));
    /* This end's own outputs still have to read off. */
    start(0u);
    w.link_up  = false;
    w.local_on = true;
    (void)run_to(stop_and_hold(0u), STOP_LATCH_RESUMED);
    CHECK(stop_latch_state(&l) != STOP_LATCH_RESUMED);
    CHECK(arming_held(&a));
}

TEST_CASE(nothing_switches_on_after_the_reset)
{
    start(0u);
    const uint32_t t0 = run_to(stop_and_hold(0u), STOP_LATCH_UNLATCHED);
    for (uint32_t t = t0; t <= t0 + 2000u; t += 5u) {
        arming_touch_seen(&a, t);
        arming_line_nobody(&a);
        CHECK(arming_step(&a, t) != ARMING_ACT_ARM);
    }
    CHECK(!a.armed);
    /* An arm asked now is a fresh arm, with its own settle. */
    const uint32_t t1 = t0 + 2000u;
    arming_request_arm(&a, t1);
    arming_line_nobody(&a);
    CHECK_EQ(arming_step(&a, t1), ARMING_ACT_NONE);
    arming_touch_seen(&a, t1 + SETTLE_MS);
    arming_line_nobody(&a);
    CHECK_EQ(arming_step(&a, t1 + SETTLE_MS), ARMING_ACT_ARM);
}

TEST_CASE(the_heartbeat_fault_is_hidden_while_latched_and_after_the_reset)
{
    const uint16_t beat  = (uint16_t)LINK_FAULT_HEARTBEAT;
    const uint16_t other = (uint16_t)LINK_FAULT_OVERCURRENT;
    start(0u);
    CHECK_EQ(stop_latch_faults(&l, 0u, false, beat), beat);
    touch_stop(0u);
    CHECK_EQ(stop_latch_faults(&l, 100u, true, (uint16_t)(beat | other)),
             other);
    (void)run_to(stop_and_hold(0u), STOP_LATCH_RESUMED);
    /* Released and not yet done: still hidden. */
    CHECK_EQ(stop_latch_faults(&l, w.t, false, beat), 0u);
    const uint32_t done = run_to(w.t + 5u, STOP_LATCH_UNLATCHED);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_UNLATCHED);
    CHECK_EQ(stop_latch_faults(&l, done + STOP_LATCH_GRACE_MS, false, beat),
             0u);
    CHECK_EQ(stop_latch_faults(&l, done + STOP_LATCH_GRACE_MS + 1u, false,
                               beat),
             beat);
    /* A STOP ends the grace; while it stands the bit is hidden anyway. */
    touch_stop(done + 100u);
    CHECK_EQ(stop_latch_faults(&l, done + 200u, false, beat), beat);
    CHECK_EQ(stop_latch_faults(NULL, done + 200u, true, beat), 0u);
}

TEST_CASE(the_hold_and_the_bound_hold_across_the_millisecond_wrap)
{
    const uint32_t t0 = 0xFFFFFF00u;
    start(t0);
    touch_stop(t0);
    (void)pass_hold(t0 + 2000u, t0 + STOP_LATCH_HOLD_SEEN_MS - 1u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_LATCHED);
    (void)pass_hold(t0 + 2000u, t0 + STOP_LATCH_HOLD_SEEN_MS);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_HOLD_DONE);
    (void)run_to(t0 + 2005u, STOP_LATCH_UNLATCHED);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_UNLATCHED);
    CHECK(!arming_held(&a));

    w.state = (uint16_t)LINK_STATE_ARMED;
    (void)stop_and_hold(t0 + 3000u);
    (void)pass(t0 + 5100u);
    CHECK_EQ(pass(t0 + 5100u + STOP_LATCH_CONFIRM_MS), STOP_LATCH_NONE);
    CHECK_EQ(pass(t0 + 5101u + STOP_LATCH_CONFIRM_MS), STOP_LATCH_GIVE_UP);
}

TEST_CASE(a_restart_comes_up_unlatched)
{
    start(0u);
    (void)stop_and_hold(0u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_HOLD_DONE);
    /* The panel restarts: nothing of the latch is kept. */
    start(50u);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_UNLATCHED);
    CHECK(!arming_held(&a));
    CHECK(!arming_stopped(&a));
    CHECK(arming_heartbeat(&a, 50u));
    CHECK(!a.armed);
    CHECK_EQ(pass(60u), STOP_LATCH_NONE);
    CHECK_EQ(w.reads + w.clears + w.resumes, 0);
    arming_request_arm(&a, 100u);
    CHECK(a.arming);
}

TEST_CASE(null_is_harmless)
{
    stop_latch_in_t in = { 0 };
    stop_latch_init(NULL, &in);
    stop_latch_init(&l, NULL);
    CHECK_EQ(stop_latch_state(&l), STOP_LATCH_UNLATCHED);
    CHECK_EQ(stop_latch_run(NULL, &in, &io), STOP_LATCH_NONE);
    CHECK_EQ(stop_latch_run(&l, NULL, &io), STOP_LATCH_NONE);
    CHECK_EQ(stop_latch_run(&l, &in, NULL), STOP_LATCH_NONE);
    CHECK(!stop_latch_error(NULL));
    CHECK_EQ(stop_latch_state(NULL), STOP_LATCH_UNLATCHED);
    CHECK_EQ(stop_latch_resets(NULL), 0u);
    CHECK_EQ(stop_latch_refused(NULL), 0u);
    CHECK(!arming_held(NULL));
    arming_stop_release(NULL);
    /* An io with nothing in it answers nothing and confirms nothing. */
    const stop_latch_io_t none = { 0 };
    in.held    = true;
    in.link_up = true;
    stop_latch_init(&l, &in);
    in.hold_done = true;
    in.hold_ms   = 5000u;
    in.now_ms    = 5000u;
    (void)stop_latch_run(&l, &in, &none);
    in.hold_done = false;
    for (in.now_ms = 5100u; in.now_ms < 8000u; in.now_ms += 5u) {
        (void)stop_latch_run(&l, &in, &none);
        CHECK(stop_latch_state(&l) <= STOP_LATCH_ASKING);
    }
}

TEST_CASE(the_confirmation_is_read_every_50ms_while_asking)
{
    start(0u);
    w.state = (uint16_t)LINK_STATE_ARMED;
    (void)stop_and_hold(0u);
    (void)pass(2100u);
    CHECK_EQ(w.reads, 1);
    for (uint32_t t = 2101u; t < 2100u + STOP_LATCH_ASK_MS; ++t) {
        (void)pass(t);
    }
    CHECK_EQ(w.reads, 1);
    (void)pass(2100u + STOP_LATCH_ASK_MS);
    CHECK_EQ(w.reads, 2);
}

int main(void)
{
    RUN(a_touch_latches_and_withholds_the_heartbeat);
    RUN(an_arm_is_refused_while_the_stop_stands);
    RUN(a_stop_from_the_far_end_still_clears_with_an_arm);
    RUN(the_reset_walks_every_state_in_order);
    RUN(a_hold_ending_1899ms_after_the_stop_is_refused_and_1900ms_runs);
    RUN(a_stop_after_the_hold_began_refuses_it);
    RUN(a_contact_break_during_the_hold_refuses_it);
    RUN(a_break_in_the_hold_counted_after_it_sends_the_reset_back);
    RUN(a_finger_leaving_after_the_hold_lets_the_reset_finish);
    RUN(a_stop_from_any_state_goes_back_to_latched);
    RUN(a_stop_inside_a_read_counts_before_the_reading);
    RUN(a_hold_before_the_confirmation_restarts_nothing);
    RUN(a_confirmation_at_the_bound_resets_and_a_late_one_gives_up);
    RUN(no_confirmation_keeps_the_latch_and_the_line_withheld);
    RUN(the_floor_counts_from_the_pass_that_saw_the_stop);
    RUN(an_error_sends_a_reset_back_and_changes_nothing_latched);
    RUN(clear_is_written_once_per_reset_after_the_heartbeat);
    RUN(the_heartbeat_resumes_only_after_the_confirmation);
    RUN(every_served_page_and_this_end_must_read_off);
    RUN(with_no_link_this_end_confirms_and_no_clear_is_written);
    RUN(nothing_switches_on_after_the_reset);
    RUN(the_heartbeat_fault_is_hidden_while_latched_and_after_the_reset);
    RUN(the_hold_and_the_bound_hold_across_the_millisecond_wrap);
    RUN(a_restart_comes_up_unlatched);
    RUN(null_is_harmless);
    RUN(the_confirmation_is_read_every_50ms_while_asking);
    return test_summary("stop_latch");
}
