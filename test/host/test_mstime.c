/**
 * @file
 * @brief The wrap-safe millisecond helpers: the deadline helpers at the
 *        deadline, one millisecond either side and across the 2^32 wrap; the
 *        span helper at its edge and across the wrap.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"
#include "mstime.h"

TEST_CASE(a_deadline_is_reached_at_and_after_it_and_past_only_after)
{
    CHECK(!mstime_reached(99u, 100u));
    CHECK(mstime_reached(100u, 100u));
    CHECK(mstime_reached(101u, 100u));
    CHECK(!mstime_past(99u, 100u));
    CHECK(!mstime_past(100u, 100u));
    CHECK(mstime_past(101u, 100u));
}

TEST_CASE(the_deadline_helpers_hold_across_the_counter_wrap)
{
    /* The deadline lies 10ms after the wrap; the clock is 5ms before it. */
    CHECK(!mstime_reached(0xFFFFFFFBu, 5u));
    CHECK(!mstime_past(0xFFFFFFFBu, 5u));
    CHECK(mstime_reached(5u, 5u));
    CHECK(!mstime_past(5u, 5u));
    CHECK(mstime_past(6u, 5u));
    /* The deadline lies before the wrap; the clock has passed it. */
    CHECK(mstime_reached(3u, 0xFFFFFFFEu));
    CHECK(mstime_past(3u, 0xFFFFFFFEu));
}

TEST_CASE(a_span_is_elapsed_from_its_length_on_also_across_the_wrap)
{
    CHECK(!mstime_elapsed(1099u, 1000u, 100u));
    CHECK(mstime_elapsed(1100u, 1000u, 100u));
    CHECK(mstime_elapsed(1101u, 1000u, 100u));
    /* Started 20ms before the wrap. */
    CHECK(!mstime_elapsed(79u, 0xFFFFFFECu, 100u));
    CHECK(mstime_elapsed(80u, 0xFFFFFFECu, 100u));
    CHECK(mstime_elapsed(1000u, 1000u, 0u));
}

int main(void)
{
    RUN(a_deadline_is_reached_at_and_after_it_and_past_only_after);
    RUN(the_deadline_helpers_hold_across_the_counter_wrap);
    RUN(a_span_is_elapsed_from_its_length_on_also_across_the_wrap);
    return test_summary("mstime");
}
