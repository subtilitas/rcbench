/**
 * @file test_stop_latch.c
 * @brief The panel's STOP as an emergency stop: one touch latches it, a hold
 * of 2s asks for the reset, and the reset gives the heartbeat back only
 * once the IOmcu has confirmed everything off and 500ms have passed since
 * the last STOP.  The policy (arming.h) and the reset (stop_latch.h)
 * together, as the panel's control task runs them.
 *
 * Cases:
 * - a_touch_latches_and_withholds_the_heartbeat
 * - an_arm_is_refused_while_the_stop_stands
 * - a_stop_from_the_far_end_still_clears_with_an_arm
 * - a_reset_asked_before_the_confirmation_waits
 * - a_reset_is_asked_only_while_a_stop_stands
 * - a_hold_older_than_the_last_stop_asks_nothing
 * - a_confirmation_at_the_bound_resets
 * - a_confirmation_past_the_bound_resets_nothing
 * - no_confirmation_keeps_the_latch_and_the_line_withheld
 * - a_hold_younger_than_1900ms_after_the_last_stop_asks_nothing
 * - a_stop_during_the_wait_withdraws_the_reset
 * - an_error_during_the_wait_withdraws_the_reset
 * - an_error_while_latched_changes_nothing_else
 * - clear_is_owed_once_per_reset
 * - the_heartbeat_resumes_only_after_the_confirmation
 * - nothing_switches_on_after_the_reset
 * - the_heartbeat_fault_is_hidden_while_latched_and_after_the_reset
 * - the_hold_and_the_bound_hold_across_the_millisecond_wrap
 * - a_restart_comes_up_unlatched
 * - a_held_port_is_no_output_and_every_page_must_read_off
 * - null_is_harmless
 * - the_confirmation_is_read_every_50ms_while_a_reset_waits
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include "arming.h"
#include "heartbeat.h"
#include "link_pages.h"
#include "stop_latch.h"

/** @brief The settle the panel passes. */
#define SETTLE_MS HEARTBEAT_SETTLE_MS

static arming_t     a;
static stop_latch_t l;

/** @brief A fresh panel at @p t: policy and reset, touch answering. */
static void start(uint32_t t)
{
    arming_init(&a, t, SETTLE_MS);
    arming_touch_seen(&a, t);
    stop_latch_init(&l, arming_pressed_count(&a));
}

/** @brief One pass at @p t as panel_stop.c runs it, the reading given. */
static stop_latch_act_t pass(uint32_t t, bool confirmed)
{
    arming_touch_seen(&a, t);
    (void)stop_latch_follow(&l, arming_pressed_count(&a), t);
    const stop_latch_act_t act =
        stop_latch_step(&l, t, arming_held(&a), confirmed);
    if (act == STOP_LATCH_RESUME) {
        arming_stop_release(&a);
    }
    (void)arming_step(&a, t);
    return act;
}

/** @brief A STOP touched at @p t and seen by the pass at @p t. */
static void touch_stop(uint32_t t)
{
    arming_stop_pressed(&a);
    (void)pass(t, false);
}

TEST_CASE(a_touch_latches_and_withholds_the_heartbeat)
{
    start(1000u);
    CHECK(arming_heartbeat(&a, 1000u));
    touch_stop(1000u);
    CHECK(arming_held(&a));
    CHECK(arming_stopped(&a));
    CHECK(!arming_heartbeat(&a, 1000u));
    /* Time alone releases nothing. */
    for (uint32_t t = 1000u; t <= 61000u; t += 500u) {
        CHECK_EQ(pass(t, true), STOP_LATCH_NONE);
        CHECK(arming_held(&a));
        CHECK(!arming_heartbeat(&a, t));
    }
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
    CHECK(!stop_latch_ask(&l, arming_held(&a), 5000u, 5000u));
    arming_request_arm(&a, 100u);
    CHECK(!arming_stopped(&a));
    CHECK(arming_heartbeat(&a, 100u));
}

TEST_CASE(a_reset_asked_before_the_confirmation_waits)
{
    start(0u);
    touch_stop(0u);
    CHECK(stop_latch_ask(&l, arming_held(&a), 2000u, 2000u));
    CHECK(stop_latch_waiting(&l));
    for (uint32_t t = 2000u; t <= 3999u; t += 50u) {
        CHECK_EQ(pass(t, false), STOP_LATCH_NONE);
        CHECK(arming_held(&a));
        CHECK(!arming_heartbeat(&a, t));
    }
    CHECK(stop_latch_waiting(&l));
    CHECK_EQ(pass(4000u, true), STOP_LATCH_RESUME);
    CHECK(!arming_held(&a));
    CHECK(!arming_stopped(&a));
    CHECK(arming_heartbeat(&a, 4000u));
}

TEST_CASE(a_reset_is_asked_only_while_a_stop_stands)
{
    start(0u);
    CHECK(!stop_latch_ask(&l, arming_held(&a), 100u, 100u));
    CHECK(!stop_latch_waiting(&l));
    touch_stop(200u);
    CHECK(stop_latch_ask(&l, arming_held(&a), 2200u, 2200u));
    /* A second hold while the first waits restarts nothing. */
    CHECK(!stop_latch_ask(&l, arming_held(&a), 2300u, 2300u));
    CHECK_EQ(l.ask_ms, 2200u);
}

TEST_CASE(a_hold_older_than_the_last_stop_asks_nothing)
{
    start(0u);
    touch_stop(0u);
    /* The hold completed at 2000ms, and a STOP was applied at 2010ms,
     * before the control task took the request. */
    arming_stop_pressed(&a);
    (void)stop_latch_follow(&l, arming_pressed_count(&a), 2010u);
    CHECK(!stop_latch_ask(&l, true, 2000u, 2020u));
    CHECK(!stop_latch_waiting(&l));
    for (uint32_t t = 2020u; t <= 5000u; t += 50u) {
        CHECK_EQ(pass(t, true), STOP_LATCH_NONE);
        CHECK(!arming_heartbeat(&a, t));
    }
    CHECK(arming_held(&a));
}

TEST_CASE(a_confirmation_at_the_bound_resets)
{
    start(0u);
    touch_stop(0u);
    const uint32_t ask = 2000u;
    CHECK(stop_latch_ask(&l, true, ask, ask));
    for (uint32_t t = ask; t < ask + STOP_LATCH_CONFIRM_MS; ++t) {
        CHECK_EQ(pass(t, false), STOP_LATCH_NONE);
    }
    CHECK_EQ(pass(ask + STOP_LATCH_CONFIRM_MS, true), STOP_LATCH_RESUME);
    CHECK(arming_heartbeat(&a, ask + STOP_LATCH_CONFIRM_MS));
}

TEST_CASE(a_confirmation_past_the_bound_resets_nothing)
{
    start(0u);
    touch_stop(0u);
    const uint32_t ask = 2000u;
    CHECK(stop_latch_ask(&l, true, ask, ask));
    CHECK_EQ(pass(ask + STOP_LATCH_CONFIRM_MS + 1u, true),
             STOP_LATCH_GIVE_UP);
    CHECK(arming_held(&a));
    CHECK(!arming_heartbeat(&a, ask + STOP_LATCH_CONFIRM_MS + 1u));
    CHECK_EQ(stop_latch_resets(&l), 0u);
}

TEST_CASE(no_confirmation_keeps_the_latch_and_the_line_withheld)
{
    start(0u);
    touch_stop(0u);
    CHECK(stop_latch_ask(&l, true, 2000u, 2000u));
    int give_ups = 0;
    for (uint32_t t = 2000u; t <= 10000u; t += 5u) {
        if (pass(t, false) == STOP_LATCH_GIVE_UP) {
            ++give_ups;
            CHECK_EQ(t, 2000u + STOP_LATCH_CONFIRM_MS + 5u);
        }
        CHECK(arming_held(&a));
        CHECK(!arming_heartbeat(&a, t));
    }
    /* Told once; the next hold asks again. */
    CHECK_EQ(give_ups, 1);
    CHECK(!stop_latch_waiting(&l));
    CHECK(stop_latch_ask(&l, arming_held(&a), 10000u, 10000u));
    CHECK_EQ(pass(10000u, true), STOP_LATCH_RESUME);
}

TEST_CASE(a_hold_younger_than_1900ms_after_the_last_stop_asks_nothing)
{
    start(0u);
    touch_stop(1000u);
    CHECK(!stop_latch_ask(&l, true, 1000u + STOP_LATCH_HOLD_SEEN_MS - 1u,
                          3000u));
    CHECK(!stop_latch_waiting(&l));
    CHECK_EQ(pass(3000u, true), STOP_LATCH_NONE);
    CHECK(!arming_heartbeat(&a, 3000u));
    /* At 1900ms the heartbeat has been withheld well past the 500ms the
     * IOmcu needs to doubt the line. */
    CHECK(stop_latch_ask(&l, true, 1000u + STOP_LATCH_HOLD_SEEN_MS, 3000u));
    CHECK_EQ(pass(3000u, true), STOP_LATCH_RESUME);
    CHECK(arming_heartbeat(&a, 3000u));
}

TEST_CASE(a_stop_during_the_wait_withdraws_the_reset)
{
    start(0u);
    touch_stop(0u);
    CHECK(stop_latch_ask(&l, true, 2000u, 2000u));
    arming_stop_pressed(&a);   /* a second finger, or the same one again */
    CHECK(stop_latch_follow(&l, arming_pressed_count(&a), 2100u));
    CHECK(!stop_latch_waiting(&l));
    CHECK_EQ(pass(2200u, true), STOP_LATCH_NONE);
    CHECK(arming_held(&a));
    /* Asked again, the hold runs from that stop. */
    CHECK(!stop_latch_ask(&l, true, 3999u, 3999u));
    CHECK(stop_latch_ask(&l, true, 4000u, 4000u));
    CHECK_EQ(pass(4000u, true), STOP_LATCH_RESUME);
}

TEST_CASE(an_error_during_the_wait_withdraws_the_reset)
{
    start(0u);
    touch_stop(0u);
    CHECK(stop_latch_ask(&l, true, 2000u, 2000u));
    CHECK(stop_latch_error(&l));
    CHECK(!stop_latch_waiting(&l));
    for (uint32_t t = 2000u; t <= 6000u; t += 50u) {
        CHECK_EQ(pass(t, true), STOP_LATCH_NONE);
        CHECK(!arming_heartbeat(&a, t));
    }
    CHECK(arming_held(&a));
}

TEST_CASE(an_error_while_latched_changes_nothing_else)
{
    start(0u);
    touch_stop(0u);
    CHECK(!stop_latch_error(&l));
    /* The error's disarm, as error_act() asks it. */
    arming_request_disarm(&a);
    CHECK(arming_held(&a));
    CHECK(arming_stopped(&a));
    CHECK(!arming_heartbeat(&a, 100u));
    arming_request_arm(&a, 100u);
    CHECK(arming_held(&a));
}

TEST_CASE(clear_is_owed_once_per_reset)
{
    start(0u);
    touch_stop(0u);
    CHECK(stop_latch_ask(&l, true, 2000u, 2000u));
    CHECK_EQ(pass(2000u, true), STOP_LATCH_RESUME);
    for (uint32_t t = 2005u; t <= 5000u; t += 5u) {
        CHECK_EQ(pass(t, true), STOP_LATCH_NONE);
    }
    CHECK_EQ(stop_latch_resets(&l), 1u);
    /* A second hold on an unlatched bench asks for nothing. */
    CHECK(!stop_latch_ask(&l, arming_held(&a), 5000u, 5000u));
    CHECK_EQ(stop_latch_resets(&l), 1u);
    /* A second stop and its reset: one more. */
    touch_stop(6000u);
    CHECK(stop_latch_ask(&l, true, 8000u, 8000u));
    CHECK_EQ(pass(8000u, true), STOP_LATCH_RESUME);
    CHECK_EQ(stop_latch_resets(&l), 2u);
}

TEST_CASE(the_heartbeat_resumes_only_after_the_confirmation)
{
    start(0u);
    touch_stop(0u);
    CHECK(stop_latch_ask(&l, true, 2000u, 2000u));
    /* Only reads with something still on, the floor long past. */
    for (uint32_t t = 2000u; t < 3000u; t += 5u) {
        CHECK_EQ(pass(t, false), STOP_LATCH_NONE);
        CHECK(!arming_heartbeat(&a, t));
    }
    CHECK_EQ(pass(3000u, true), STOP_LATCH_RESUME);
    CHECK(arming_heartbeat(&a, 3000u));
}

TEST_CASE(nothing_switches_on_after_the_reset)
{
    start(0u);
    touch_stop(0u);
    CHECK(stop_latch_ask(&l, true, 2000u, 2000u));
    CHECK_EQ(pass(2000u, true), STOP_LATCH_RESUME);
    for (uint32_t t = 2000u; t <= 4000u; t += 5u) {
        arming_touch_seen(&a, t);
        arming_line_nobody(&a);
        CHECK(arming_step(&a, t) != ARMING_ACT_ARM);
    }
    CHECK(!a.armed);
    /* An arm asked now is a fresh arm, with its own settle. */
    arming_request_arm(&a, 4000u);
    arming_line_nobody(&a);
    CHECK_EQ(arming_step(&a, 4000u), ARMING_ACT_NONE);
    arming_touch_seen(&a, 4000u + SETTLE_MS);
    arming_line_nobody(&a);
    CHECK_EQ(arming_step(&a, 4000u + SETTLE_MS), ARMING_ACT_ARM);
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
    CHECK(stop_latch_ask(&l, true, 2000u, 2000u));
    CHECK_EQ(pass(2000u, true), STOP_LATCH_RESUME);
    CHECK_EQ(stop_latch_faults(&l, 2000u + STOP_LATCH_GRACE_MS, false, beat),
             0u);
    CHECK_EQ(stop_latch_faults(&l, 2001u + STOP_LATCH_GRACE_MS, false, beat),
             beat);
    /* A STOP ends the grace; while it stands the bit is hidden anyway. */
    touch_stop(2500u);
    CHECK_EQ(stop_latch_faults(&l, 2600u, false, beat), beat);
    CHECK_EQ(stop_latch_faults(NULL, 2600u, true, beat), 0u);
}

TEST_CASE(the_hold_and_the_bound_hold_across_the_millisecond_wrap)
{
    const uint32_t t0 = 0xFFFFFF00u;
    start(t0);
    touch_stop(t0);
    CHECK(!stop_latch_ask(&l, true, t0 + STOP_LATCH_HOLD_SEEN_MS - 1u,
                          t0 + STOP_LATCH_HOLD_SEEN_MS));
    CHECK(stop_latch_ask(&l, true, t0 + STOP_LATCH_HOLD_SEEN_MS,
                         t0 + STOP_LATCH_HOLD_SEEN_MS));
    CHECK_EQ(pass(t0 + STOP_LATCH_HOLD_SEEN_MS, true), STOP_LATCH_RESUME);

    touch_stop(t0 + 3000u);
    CHECK(stop_latch_ask(&l, true, t0 + 5000u, t0 + 5000u));
    CHECK_EQ(pass(t0 + 5000u + STOP_LATCH_CONFIRM_MS, false),
             STOP_LATCH_NONE);
    CHECK_EQ(pass(t0 + 5001u + STOP_LATCH_CONFIRM_MS, false),
             STOP_LATCH_GIVE_UP);
}

TEST_CASE(a_restart_comes_up_unlatched)
{
    start(0u);
    touch_stop(0u);
    CHECK(stop_latch_ask(&l, true, 2000u, 2000u));
    /* The panel restarts: nothing of the latch is kept. */
    start(50u);
    CHECK(!arming_held(&a));
    CHECK(!arming_stopped(&a));
    CHECK(!stop_latch_waiting(&l));
    CHECK(arming_heartbeat(&a, 50u));
    CHECK(!a.armed);
    CHECK_EQ(pass(60u, true), STOP_LATCH_NONE);
    arming_request_arm(&a, 100u);
    CHECK(a.arming);
}

TEST_CASE(a_held_port_is_no_output_and_every_page_must_read_off)
{
    stop_latch_reading_t r = { .link_up = true, .far_read = true };
    CHECK(stop_latch_all_off(&r));
    /* A programming session stopped under STOP keeps its port. */
    r.port_held = true;
    CHECK(stop_latch_all_off(&r));
    const stop_latch_reading_t base = r;
    r.bank_armed = true;  CHECK(!stop_latch_all_off(&r)); r = base;
    r.supply_on = true;   CHECK(!stop_latch_all_off(&r)); r = base;
    r.prog_on = true;     CHECK(!stop_latch_all_off(&r)); r = base;
    r.far_read = false;   CHECK(!stop_latch_all_off(&r)); r = base;
    r.far_armed = true;   CHECK(!stop_latch_all_off(&r)); r = base;
    r.far_supply = true;  CHECK(!stop_latch_all_off(&r)); r = base;
    r.far_prog = true;    CHECK(!stop_latch_all_off(&r)); r = base;
    /* With no link the far pages are not asked; this end still is. */
    r.link_up = false;
    r.far_read = false;
    CHECK(stop_latch_all_off(&r));
    r.supply_on = true;
    CHECK(!stop_latch_all_off(&r));
    CHECK(!stop_latch_all_off(NULL));
}

TEST_CASE(null_is_harmless)
{
    stop_latch_init(NULL, 0u);
    CHECK(!stop_latch_follow(NULL, 1u, 0u));
    CHECK(!stop_latch_ask(NULL, true, 0u, 0u));
    CHECK(!stop_latch_error(NULL));
    CHECK(!stop_latch_waiting(NULL));
    CHECK(!stop_latch_read_due(NULL, 0u));
    CHECK_EQ(stop_latch_step(NULL, 0u, true, true), STOP_LATCH_NONE);
    CHECK_EQ(stop_latch_resets(NULL), 0u);
    CHECK(!arming_held(NULL));
    arming_stop_release(NULL);
}

TEST_CASE(the_confirmation_is_read_every_50ms_while_a_reset_waits)
{
    start(0u);
    CHECK(!stop_latch_read_due(&l, 0u));
    touch_stop(0u);
    CHECK(stop_latch_ask(&l, true, 2000u, 2000u));
    CHECK(stop_latch_read_due(&l, 2000u));
    CHECK(!stop_latch_read_due(&l, 2000u + STOP_LATCH_ASK_MS - 1u));
    CHECK(stop_latch_read_due(&l, 2000u + STOP_LATCH_ASK_MS));
    CHECK(!stop_latch_read_due(&l, 2001u + STOP_LATCH_ASK_MS));
}

int main(void)
{
    RUN(a_touch_latches_and_withholds_the_heartbeat);
    RUN(an_arm_is_refused_while_the_stop_stands);
    RUN(a_stop_from_the_far_end_still_clears_with_an_arm);
    RUN(a_reset_asked_before_the_confirmation_waits);
    RUN(a_reset_is_asked_only_while_a_stop_stands);
    RUN(a_hold_older_than_the_last_stop_asks_nothing);
    RUN(a_confirmation_at_the_bound_resets);
    RUN(a_confirmation_past_the_bound_resets_nothing);
    RUN(no_confirmation_keeps_the_latch_and_the_line_withheld);
    RUN(a_hold_younger_than_1900ms_after_the_last_stop_asks_nothing);
    RUN(a_stop_during_the_wait_withdraws_the_reset);
    RUN(an_error_during_the_wait_withdraws_the_reset);
    RUN(an_error_while_latched_changes_nothing_else);
    RUN(clear_is_owed_once_per_reset);
    RUN(the_heartbeat_resumes_only_after_the_confirmation);
    RUN(nothing_switches_on_after_the_reset);
    RUN(the_heartbeat_fault_is_hidden_while_latched_and_after_the_reset);
    RUN(the_hold_and_the_bound_hold_across_the_millisecond_wrap);
    RUN(a_restart_comes_up_unlatched);
    RUN(a_held_port_is_no_output_and_every_page_must_read_off);
    RUN(null_is_harmless);
    RUN(the_confirmation_is_read_every_50ms_while_a_reset_waits);
    return test_summary("stop_latch");
}
