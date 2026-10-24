/*
 * The two pico-sdk clocks firmware/iomcu/src/outputs_hw.c reads, for a host
 * suite: a microsecond count since boot that the suite sets.  Nothing else
 * of the SDK (software development kit) is stood in for here; a file that
 * needs more does not build against this header.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

/* Microseconds since boot, set by the suite (test_outputs_hw.c). */
extern uint64_t pico_stub_us;

typedef uint64_t absolute_time_t;

static inline absolute_time_t get_absolute_time(void)
{
    return pico_stub_us;
}

/* As the SDK's: the millisecond count truncated to 32 bits, so it wraps at
 * 2^32ms (49.7 days). */
static inline uint32_t to_ms_since_boot(absolute_time_t t)
{
    return (uint32_t)(t / 1000u);
}

static inline uint32_t time_us_32(void)
{
    return (uint32_t)pico_stub_us;
}
