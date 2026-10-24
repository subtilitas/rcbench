/*
 * When a new build's first start is kept (shared/fwupdate/fw_trial.h).
 *
 * The failures under test: a build kept before the start has run as a
 * whole, so that a fault after the keep restarts for ever under a kept
 * build; a tick from before the first frame taken as a display that
 * works; a trial held open past its bound on a screen loop that runs; a
 * keep on a bench that cannot take the flash write; a keep returned
 * twice; and a bound that a clock wrapping past 2^32ms misses or reopens.
 *
 * The panel's end -- the marks from app_main, the control and logger
 * tasks and the screen loop, and the otadata write -- is firmware
 * (firmware/panel/main/panel_trial.c) and not in this suite.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"
#include "tick_wrap.h"

#include "fw_trial.h"

/* Every step but the ones in @p skip, the tick after the frame. */
static void mark_all_but(fw_trial_t *t, uint32_t skip)
{
    fw_trial_mark(t, (FW_TRIAL_CARD | FW_TRIAL_SETTINGS | FW_TRIAL_CONTROL
                      | FW_TRIAL_LOGGER | FW_TRIAL_FRAME) & ~skip);
    fw_trial_mark(t, FW_TRIAL_TICK & ~skip);
}

TEST_CASE(every_step_keeps_the_build_once)
{
    fw_trial_t t;
    fw_trial_init(&t, 0u);
    mark_all_but(&t, 0u);
    CHECK_EQ(fw_trial_missing(&t), 0u);
    CHECK_EQ(fw_trial_poll(&t, 4000u, true), FW_TRIAL_KEEP);
    /* Settled: the keep is not returned a second time. */
    CHECK_EQ(fw_trial_poll(&t, 4017u, true), FW_TRIAL_SETTLED);
    CHECK_EQ(fw_trial_poll(&t, FW_TRIAL_BOUND_MS, true), FW_TRIAL_SETTLED);
}

TEST_CASE(any_step_missing_holds_the_trial_inside_the_bound)
{
    const uint32_t steps[] = {
        FW_TRIAL_CARD, FW_TRIAL_SETTINGS, FW_TRIAL_CONTROL,
        FW_TRIAL_LOGGER, FW_TRIAL_FRAME, FW_TRIAL_TICK,
    };
    for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
        fw_trial_t t;
        fw_trial_init(&t, 0u);
        mark_all_but(&t, steps[i]);
        /* Without the frame its tick does not count either. */
        const uint32_t want = (steps[i] == FW_TRIAL_FRAME)
                                  ? (FW_TRIAL_FRAME | FW_TRIAL_TICK)
                                  : steps[i];
        CHECK_EQ(fw_trial_missing(&t), want);
        CHECK_EQ(fw_trial_poll(&t, 0u, true), FW_TRIAL_OPEN);
        CHECK_EQ(fw_trial_poll(&t, FW_TRIAL_BOUND_MS - 1u, true),
                 FW_TRIAL_OPEN);
    }
}

TEST_CASE(a_tick_before_the_first_frame_counts_for_nothing)
{
    fw_trial_t t;
    fw_trial_init(&t, 0u);
    fw_trial_mark(&t, FW_TRIAL_CARD | FW_TRIAL_SETTINGS | FW_TRIAL_CONTROL
                      | FW_TRIAL_LOGGER);
    /* The screen loop's order: a tick, then the flip that first works. */
    fw_trial_mark(&t, FW_TRIAL_TICK);
    fw_trial_mark(&t, FW_TRIAL_FRAME);
    CHECK_EQ(fw_trial_missing(&t), FW_TRIAL_TICK);
    CHECK_EQ(fw_trial_poll(&t, 3000u, true), FW_TRIAL_OPEN);
    /* The next pass's tick follows that frame. */
    fw_trial_mark(&t, FW_TRIAL_TICK);
    CHECK_EQ(fw_trial_poll(&t, 3017u, true), FW_TRIAL_KEEP);

    /* A tick and a frame in one mark: the frame counts first. */
    fw_trial_t u;
    fw_trial_init(&u, 0u);
    fw_trial_mark(&u, FW_TRIAL_FRAME | FW_TRIAL_TICK);
    CHECK_EQ(fw_trial_missing(&u) & (FW_TRIAL_FRAME | FW_TRIAL_TICK), 0u);
}

TEST_CASE(bits_outside_the_steps_are_ignored)
{
    fw_trial_t t;
    fw_trial_init(&t, 0u);
    fw_trial_mark(&t, ~FW_TRIAL_ALL);
    CHECK_EQ(fw_trial_missing(&t), FW_TRIAL_ALL);
    mark_all_but(&t, 0u);
    fw_trial_mark(&t, 0x40u);
    CHECK_EQ(fw_trial_poll(&t, 1000u, true), FW_TRIAL_KEEP);
}

TEST_CASE(a_bench_that_is_not_quiet_holds_the_keep)
{
    fw_trial_t t;
    fw_trial_init(&t, 0u);
    mark_all_but(&t, 0u);
    CHECK_EQ(fw_trial_poll(&t, 2000u, false), FW_TRIAL_OPEN);
    /* Past the bound too: the flash write waits for a quiet bench. */
    CHECK_EQ(fw_trial_poll(&t, FW_TRIAL_BOUND_MS + 5000u, false),
             FW_TRIAL_OPEN);
    CHECK_EQ(fw_trial_poll(&t, FW_TRIAL_BOUND_MS + 5017u, true),
             FW_TRIAL_KEEP);
}

TEST_CASE(a_settled_trial_is_never_kept)
{
    fw_trial_t t;
    fw_trial_init(&t, 0u);
    fw_trial_settle(&t);
    mark_all_but(&t, 0u);
    CHECK_EQ(fw_trial_poll(&t, 1000u, true), FW_TRIAL_SETTLED);
    CHECK_EQ(fw_trial_poll(&t, FW_TRIAL_BOUND_MS, true), FW_TRIAL_SETTLED);
}

/* The bound, counted from @p t0: a ticking screen loop with the logger's
 * pass never seen. */
static void bound_case(uint32_t t0)
{
    fw_trial_t t;
    fw_trial_init(&t, t0);
    mark_all_but(&t, FW_TRIAL_LOGGER);
    CHECK_EQ(fw_trial_poll(&t, t0 + 1u, true), FW_TRIAL_OPEN);
    CHECK_EQ(fw_trial_poll(&t, t0 + FW_TRIAL_BOUND_MS - 1u, true),
             FW_TRIAL_OPEN);
    CHECK_EQ(fw_trial_poll(&t, t0 + FW_TRIAL_BOUND_MS, true),
             FW_TRIAL_KEEP_BOUND);
    CHECK_EQ(fw_trial_poll(&t, t0 + FW_TRIAL_BOUND_MS + 17u, true),
             FW_TRIAL_SETTLED);
}

TEST_CASE(the_bound_keeps_a_ticking_screen_loop_at_30s)
{
    at_tick_0_and_before_the_wrap(bound_case, 1000u);
    at_tick_0_and_before_the_wrap(bound_case, FW_TRIAL_BOUND_MS);
}

TEST_CASE(the_bound_does_not_keep_a_screen_loop_that_never_ticked)
{
    /* Display up, no frame ever flushed: a build that never draws is
     * not kept, at the bound or after it. */
    fw_trial_t t;
    fw_trial_init(&t, 0u);
    fw_trial_mark(&t, FW_TRIAL_CARD | FW_TRIAL_SETTINGS | FW_TRIAL_CONTROL
                      | FW_TRIAL_LOGGER | FW_TRIAL_TICK);
    CHECK_EQ(fw_trial_poll(&t, FW_TRIAL_BOUND_MS, true), FW_TRIAL_OPEN);
    CHECK_EQ(fw_trial_poll(&t, 600000u, true), FW_TRIAL_OPEN);
    /* A frame and a tick after it, late: kept on the bound. */
    fw_trial_mark(&t, FW_TRIAL_FRAME);
    fw_trial_mark(&t, FW_TRIAL_TICK);
    CHECK_EQ(fw_trial_poll(&t, 600017u, true), FW_TRIAL_KEEP);
}

TEST_CASE(the_bound_stays_passed_when_the_count_wraps)
{
    /* Seen once, the bound holds: a count that wraps back to a small
     * difference does not reopen the wait for every step. */
    fw_trial_t t;
    fw_trial_init(&t, 0u);
    fw_trial_mark(&t, FW_TRIAL_FRAME);
    CHECK_EQ(fw_trial_poll(&t, FW_TRIAL_BOUND_MS, true), FW_TRIAL_OPEN);
    fw_trial_mark(&t, FW_TRIAL_TICK);
    CHECK_EQ(fw_trial_poll(&t, 5u, true), FW_TRIAL_KEEP_BOUND);
}

int main(void)
{
    RUN(every_step_keeps_the_build_once);
    RUN(any_step_missing_holds_the_trial_inside_the_bound);
    RUN(a_tick_before_the_first_frame_counts_for_nothing);
    RUN(bits_outside_the_steps_are_ignored);
    RUN(a_bench_that_is_not_quiet_holds_the_keep);
    RUN(a_settled_trial_is_never_kept);
    RUN(the_bound_keeps_a_ticking_screen_loop_at_30s);
    RUN(the_bound_does_not_keep_a_screen_loop_that_never_ticked);
    RUN(the_bound_stays_passed_when_the_count_wraps);
    return test_summary("fw_trial");
}
