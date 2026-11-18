/*
 * The settle rule's window (servo_settle.h).
 *
 * Under test: the span needs readings that reach back the whole window;
 * a creep that leaves the band keeps it open and one inside closes it;
 * noise inside the band holds; a reading outside the band falls out once
 * the window has passed it; buckets with no reading are skipped; angles
 * across ±180° wrap; the longest window; NULL and a value not finite.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "greatest.h"

#include "servo_settle.h"

static servo_settle_t s;

TEST_CASE(the_span_needs_the_whole_window_of_readings)
{
    servo_settle_reset(&s);
    CHECK(servo_settle_span(&s, 2000u) < 0.0f);
    for (uint32_t t = 0; t < 2000u; t += 10u) {
        servo_settle_add(&s, 10.0f, 1000u + t);
        CHECK(!servo_settle_held(&s, 2000u, 0.2f));
    }
    servo_settle_add(&s, 10.0f, 3000u);
    CHECK(servo_settle_held(&s, 2000u, 0.2f));
    CHECK_NEAR(servo_settle_span(&s, 2000u), 0.0f, 1e-6f);
    /* A window of 0 holds at the first reading. */
    servo_settle_reset(&s);
    servo_settle_add(&s, 3.0f, 5u);
    CHECK(servo_settle_held(&s, 0u, 0.2f));
}

TEST_CASE(a_creep_inside_the_band_holds_and_one_past_it_does_not)
{
    /* 0.15°/s: 0.30° over 2s, past 0.20°; 0.09°/s: 0.18° within. */
    servo_settle_reset(&s);
    for (uint32_t t = 0; t <= 4000u; t += 10u) {
        servo_settle_add(&s, 40.0f - 0.15f * (float)t / 1000.0f, t);
    }
    CHECK(!servo_settle_held(&s, 2000u, 0.2f));
    CHECK_NEAR(servo_settle_span(&s, 2000u), 0.315f, 0.016f);
    servo_settle_reset(&s);
    for (uint32_t t = 0; t <= 4000u; t += 10u) {
        servo_settle_add(&s, 40.0f - 0.09f * (float)t / 1000.0f, t);
    }
    CHECK(servo_settle_held(&s, 2000u, 0.2f));
}

TEST_CASE(noise_inside_the_band_holds_and_a_spike_falls_out_after_the_window)
{
    servo_settle_reset(&s);
    uint32_t t = 0u;
    for (; t <= 3000u; t += 10u) {
        const float n = ((t / 10u) % 3u == 0u) ? 0.08f : -0.08f;
        servo_settle_add(&s, -20.0f + n, t);
    }
    CHECK(servo_settle_held(&s, 2000u, 0.2f));
    servo_settle_add(&s, -19.5f, t);          /* a 0.5° spike */
    CHECK(!servo_settle_held(&s, 2000u, 0.2f));
    const uint32_t spike = t;
    for (t += 10u; t - spike <= 2000u; t += 10u) {
        servo_settle_add(&s, -20.0f, t);
        CHECK(!servo_settle_held(&s, 2000u, 0.2f));
    }
    /* One bucket past the window. */
    for (; t - spike <= 2000u + SERVO_SETTLE_BUCKET_MS; t += 10u) {
        servo_settle_add(&s, -20.0f, t);
    }
    CHECK(servo_settle_held(&s, 2000u, 0.2f));
}

TEST_CASE(buckets_without_a_reading_are_skipped_and_angles_wrap)
{
    servo_settle_reset(&s);
    servo_settle_add(&s, 179.95f, 0u);
    servo_settle_add(&s, -179.95f, 900u);     /* 0.10° on, across 180 */
    servo_settle_add(&s, 179.98f, 2500u);
    CHECK(servo_settle_held(&s, 2000u, 0.2f));
    CHECK_NEAR(servo_settle_span(&s, 2000u), 0.07f, 0.01f);
    /* A gap longer than every bucket empties them all. */
    servo_settle_add(&s, 170.0f, 2600u);
    servo_settle_add(&s, 170.0f, 2600u + 20000u);
    CHECK(servo_settle_held(&s, 2000u, 0.2f));
    /* A reading older than the newest goes into the newest's bucket. */
    servo_settle_add(&s, 170.1f, 2600u + 19000u);
    CHECK_NEAR(servo_settle_span(&s, 2000u), 0.1f, 0.01f);
}

TEST_CASE(the_longest_window_and_the_edges)
{
    servo_settle_reset(&s);
    for (uint32_t t = 0; t <= 12000u; t += 10u) {
        servo_settle_add(&s, (t < 1500u) ? 5.0f : 5.1f, t);
    }
    /* 10s back from 12s reaches past 1.5s: 0.1°; a window above the
     * longest is taken as it. */
    CHECK_NEAR(servo_settle_span(&s, SERVO_SETTLE_WIN_MAX_MS), 0.0f, 1e-5f);
    CHECK_NEAR(servo_settle_span(&s, 60000u), 0.0f, 1e-5f);
    servo_settle_reset(&s);
    for (uint32_t t = 0; t <= 10000u; t += 10u) {
        servo_settle_add(&s, (t < 50u) ? 5.0f : 5.1f, t);
    }
    CHECK_NEAR(servo_settle_span(&s, SERVO_SETTLE_WIN_MAX_MS), 0.1f, 1e-5f);
    /* The clock's wrap. */
    servo_settle_reset(&s);
    for (uint32_t k = 0; k <= 300u; ++k) {
        servo_settle_add(&s, 1.0f, 0xFFFFFF00u + 10u * k);
    }
    CHECK(servo_settle_held(&s, 2000u, 0.0f));
    /* NULL, and a value not finite. */
    servo_settle_reset(NULL);
    servo_settle_add(NULL, 1.0f, 0u);
    CHECK(servo_settle_span(NULL, 0u) < 0.0f);
    CHECK(!servo_settle_held(NULL, 0u, 1.0f));
    servo_settle_reset(&s);
    servo_settle_add(&s, NAN, 0u);
    servo_settle_add(&s, INFINITY, 1u);
    CHECK(!s.have);
}

int main(void)
{
    RUN(the_span_needs_the_whole_window_of_readings);
    RUN(a_creep_inside_the_band_holds_and_one_past_it_does_not);
    RUN(noise_inside_the_band_holds_and_a_spike_falls_out_after_the_window);
    RUN(buckets_without_a_reading_are_skipped_and_angles_wrap);
    RUN(the_longest_window_and_the_edges);
    return test_summary("servo_settle");
}
