/*
 * One case at two places on the millisecond clock, for a host suite: tick 0,
 * and a start a chosen number of ms before the 32-bit count wraps at
 * 2^32 ms (49.7 days).
 *
 * A time comparison that is right only while the clock has not wrapped
 * comes in two forms, and a case has to see both fail:
 *
 *   now >= then + ms            fires early: `then + ms` wraps before `now`
 *   now >= then && now - then   never fires once `now` has wrapped
 *
 * The cases run from here take one of two shapes.  A timeout is asserted at
 * three times, counted from the start the case is given: not expired at a
 * time before the wrap (t0 + 1 serves), not expired one step before the
 * limit, expired at the limit; with @p k less than the limit the last two
 * fall after the wrap.  A sequence of waits is run whole and held to the
 * result and the duration it has at tick 0, with @p k placing the wrap
 * inside one of its waits.
 *
 * A failure is followed by the start tick it happened at.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "greatest.h"

typedef void (*tick_case_fn)(uint32_t t0);

/* The start @p k ms before the wrap: t0 + k is tick 0. */
static inline uint32_t tick_before_wrap(uint32_t k)
{
    return (uint32_t)(0u - k);
}

/* Runs @p fn with the clock started at tick 0 and at 2^32 - @p k. */
static inline void at_tick_0_and_before_the_wrap(tick_case_fn fn, uint32_t k)
{
    const uint32_t start[2] = { 0u, tick_before_wrap(k) };
    for (unsigned i = 0; i < 2u; ++i) {
        const int failed = t_case_failed;
        t_case_failed = 0;
        fn(start[i]);
        if (t_case_failed && i == 0u) {
            printf("      with the clock started at tick 0\n");
        } else if (t_case_failed) {
            printf("      with the clock started at 0x%08lX (2^32 - %lu)\n",
                   (unsigned long)start[i], (unsigned long)k);
        }
        t_case_failed |= failed;
    }
}
