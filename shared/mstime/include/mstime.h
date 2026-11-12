/**
 * @file
 * @brief Wrap-safe comparisons of a 32-bit millisecond counter: a deadline
 *        reached, a deadline passed, a span elapsed.
 *
 * The counters wrap after 49.7 days (2^32ms).  Each helper subtracts first and
 * reads the difference as a signed or an unsigned span, so it stays correct
 * across the wrap as long as the two times are less than 2^31ms apart (the
 * deadline helpers) or the span is under 2^32ms (the elapsed helper).
 *
 * A module under protocols/ does not include this header, because a protocol
 * module stands alone (CONTRIBUTING.md, "Where code goes"); it keeps its own
 * copy.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_MSTIME_H
#define RCBENCH_MSTIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Whether the clock is at or past a deadline: true from the
 *        millisecond @p deadline_ms on, for half the counter range.
 *
 * @param now_ms      Current time, in milliseconds.
 * @param deadline_ms The deadline, in milliseconds.
 * @return true at the deadline and after it.
 */
static inline bool mstime_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(deadline_ms - now_ms) <= 0;
}

/**
 * @brief Whether the clock is past a deadline: true from the millisecond
 *        after @p deadline_ms on, for half the counter range.
 *
 * @param now_ms      Current time, in milliseconds.
 * @param deadline_ms The deadline, in milliseconds.
 * @return true after the deadline.
 */
static inline bool mstime_past(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(deadline_ms - now_ms) < 0;
}

/**
 * @brief Whether at least @p ms milliseconds have passed since @p then.
 *
 * `now < then + ms` is wrong for the span around the wrap; the subtraction
 * wraps with the counter and stays correct.
 *
 * @param now_ms  Current time, in milliseconds.
 * @param then_ms The time the span started, in milliseconds.
 * @param ms      The span, in milliseconds.
 * @return true when @p now_ms is @p ms or more after @p then_ms.
 */
static inline bool mstime_elapsed(uint32_t now_ms, uint32_t then_ms, uint32_t ms)
{
    return (uint32_t)(now_ms - then_ms) >= ms;
}

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_MSTIME_H */
