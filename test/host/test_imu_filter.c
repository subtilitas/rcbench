/*
 * The IMU's angle low-pass (imu.h): deg += (new - deg) / k per angle frame
 * and axis, IMU FILTER k 1 to 50; its seed, its wrap at 180°, the still
 * time that waits for it to settle, the settle allowance AUTO adds, and
 * the angle's text in hundredths.
 *
 * The failures this is about: a filter of 1 that still lags; a step that
 * moves by another share than 1/k; an angle that slides in from 0 after a
 * connect; a crossing of 180° that swings through 0; a stored 0 or 99
 * that divides by 0 or filters past 50; a still time that lets a run take
 * a lagging angle; an allowance shorter than the slowest settle; and a
 * text of "-0.00" or a .005 that rounds the wrong way.
 *
 * SPDX-License-Identifier: MIT
 */
#include <limits.h>
#include <math.h>
#include <string.h>

#include "greatest.h"
#include "imu.h"
#include "imu_sensor.h"
#include "wit_frame.h"

/* What 1 LSB of the angle word is, °. */
#define LSB (180.0f / 32768.0f)

/** Feed @p m one frame of the three angles at @p t. */
static void feed3(imu_t *m, float x, float y, float z, uint32_t t)
{
    uint8_t f[WIT_FRAME_LEN];
    wit_angle_frame(f, x, y, z);
    imu_feed(m, f, sizeof f, t);
}

/** Feed @p m one frame of @p x on every axis at @p t. */
static void feed(imu_t *m, float x, uint32_t t)
{
    feed3(m, x, x, x, t);
}

/** The angle word's own value of @p deg: what the sensor sends. */
static float word_of(float deg)
{
    return (float)lround((double)deg * 32768.0 / 180.0) * LSB;
}

/* IMU FILTER 1 is no filter: every frame's angle as it came, on every
 * axis, the step itself the new angle. */
TEST_CASE(a_filter_of_1_passes_every_frame_through)
{
    static const float k_v[] = { 0.0f, 37.5f, -120.25f, 179.9f, -179.9f,
                                 0.01f, 90.0f };
    imu_t m;
    imu_init(&m);
    CHECK_EQ(m.filter, 1u);
    uint32_t t = 1000u;
    for (size_t i = 0; i < sizeof k_v / sizeof k_v[0]; ++i) {
        feed3(&m, k_v[i], -k_v[i], k_v[i] / 2.0f, t);
        CHECK_EQ(m.r.angle[0], word_of(k_v[i]));
        CHECK_EQ(m.r.angle[1], word_of(-k_v[i]));
        CHECK_EQ(m.r.angle[2], word_of(k_v[i] / 2.0f));
        t += 100u;
    }
    CHECK_EQ(imu_filter_step(12.0f, -150.0f, 1), -150.0f);
    CHECK_EQ(imu_filter_step(12.0f, 33.3f, 0), 33.3f);
}

/* From 0 to a step of 10°: after n frames the angle is 10 (1 - (1-1/k)^n)
 * on every axis, for k 2, 5, 10 and 50; after k frames 63% to 75% of the
 * step, after 3k frames 95% or more. */
TEST_CASE(the_step_response_moves_one_kth_of_the_rest_each_frame)
{
    static const int32_t k_k[] = { 2, 5, 10, 50 };
    for (size_t j = 0; j < sizeof k_k / sizeof k_k[0]; ++j) {
        const int32_t k = k_k[j];
        imu_t m;
        imu_init(&m);
        imu_set_filter(&m, k);
        uint32_t t = 500u;
        feed(&m, 0.0f, t);
        CHECK_EQ(m.r.angle[0], 0.0f);
        const float step = word_of(10.0f);
        double want = 0.0;
        for (int32_t n = 1; n <= 3 * k; ++n) {
            t += 100u;
            feed(&m, 10.0f, t);
            want += ((double)step - want) / (double)k;
            for (int a = 0; a < 3; ++a) {
                CHECK_NEAR(m.r.angle[a], (float)want, 1e-4f);
            }
            if (n == k) {
                CHECK(m.r.angle[0] >= 0.63f * step);
                CHECK(m.r.angle[0] <= 0.76f * step);
            }
        }
        CHECK(m.r.angle[0] >= 0.95f * step);
        CHECK(m.r.angle[0] < step);
    }
}

/* The first frame after imu_init(), after imu_reseed() and after the
 * reading went stale is the filtered angle itself; the frame after it
 * moves a k-th of the way. */
TEST_CASE(the_first_frame_seeds_the_filter_after_init_reseed_and_stale)
{
    imu_t m;
    imu_init(&m);
    imu_set_filter(&m, 10);
    feed(&m, 40.0f, 1000u);
    CHECK_EQ(m.r.angle[0], word_of(40.0f));
    feed(&m, 50.0f, 1100u);
    CHECK_NEAR(m.r.angle[0], word_of(40.0f) + 1.0f, 1e-3f);
    /* A reseed while valid: the next frame is the angle. */
    imu_reseed(&m);
    feed(&m, 60.0f, 1200u);
    CHECK_EQ(m.r.angle[0], word_of(60.0f));
    feed(&m, 70.0f, 1300u);
    CHECK_NEAR(m.r.angle[0], word_of(60.0f) + 1.0f, 1e-3f);
    /* Stale at 300ms: the next frame seeds; at 299ms it does not. */
    feed(&m, 80.0f, 1599u);
    CHECK(fabsf(m.r.angle[0] - word_of(80.0f)) > 5.0f);
    imu_tick(&m, 1899u);
    CHECK(!m.r.valid);
    feed(&m, -20.0f, 1950u);
    CHECK_EQ(m.r.angle[0], word_of(-20.0f));
    /* NULL does nothing. */
    imu_reseed(NULL);
    imu_set_filter(NULL, 5);
}

/* The sensor side: the stream that flows again seeds (a connect and a
 * reconnect, also one quicker than the stale time), a bind seeds, and IMU
 * OFF keeps IMU FILTER for the next connect. */
TEST_CASE(a_connect_a_reconnect_and_a_bind_seed_the_filter)
{
    imu_sensor_t s;
    imu_sensor_init(&s);
    CHECK_EQ(s.filter, 1u);
    imu_sensor_filter(&s, 10);
    uint8_t f[WIT_FRAME_LEN];
    uint32_t t = 1000u;
    imu_sensor_tick(&s, t, false);
    imu_sensor_tick(&s, ++t, true);             /* connected */
    wit_angle_frame(f, 40.0f, 0.0f, 0.0f);
    imu_sensor_feed(&s, f, sizeof f, t += 20u);
    CHECK_EQ(s.m.r.angle[0], word_of(40.0f));
    wit_angle_frame(f, 50.0f, 0.0f, 0.0f);
    imu_sensor_feed(&s, f, sizeof f, t += 100u);
    imu_sensor_tick(&s, t, true);
    CHECK_NEAR(s.m.r.angle[0], word_of(40.0f) + 1.0f, 1e-3f);
    /* A drop and a reconnect within 100ms: the reading is still valid,
     * and the reconnect seeds. */
    imu_sensor_tick(&s, t += 50u, false);
    CHECK(s.m.r.valid);
    imu_sensor_tick(&s, t += 50u, true);
    wit_angle_frame(f, -30.0f, 0.0f, 0.0f);
    imu_sensor_feed(&s, f, sizeof f, t += 10u);
    CHECK_EQ(s.m.r.angle[0], word_of(-30.0f));
    /* A link that stays up does not seed again. */
    imu_sensor_tick(&s, t += 10u, true);
    wit_angle_frame(f, -20.0f, 0.0f, 0.0f);
    imu_sensor_feed(&s, f, sizeof f, t += 90u);
    CHECK_NEAR(s.m.r.angle[0], word_of(-30.0f) + 1.0f, 1e-3f);
    /* A bind seeds. */
    imu_sensor_rebind(&s);
    wit_angle_frame(f, 5.0f, 0.0f, 0.0f);
    imu_sensor_feed(&s, f, sizeof f, t += 100u);
    CHECK_EQ(s.m.r.angle[0], word_of(5.0f));
    /* IMU OFF keeps the filter: after it, the second frame moves 1/10. */
    imu_sensor_off(&s);
    CHECK_EQ(s.m.filter, 10u);
    wit_angle_frame(f, 0.0f, 0.0f, 0.0f);
    imu_sensor_feed(&s, f, sizeof f, t += 1000u);
    wit_angle_frame(f, 10.0f, 0.0f, 0.0f);
    imu_sensor_feed(&s, f, sizeof f, t += 100u);
    CHECK_NEAR(s.m.r.angle[0], word_of(10.0f) / 10.0f, 1e-3f);
}

/* Across ±180° both ways, on every axis: from 179.9° to -179.9° the
 * filtered angle moves 0.2° the short way and never comes near 0; the
 * same from -179.9° to 179.9°, and across with a step of 10°. */
TEST_CASE(a_crossing_of_180_moves_the_short_way_both_directions)
{
    static const float k_from[] = { 179.9f, -179.9f, 175.0f, -175.0f };
    static const float k_to[]   = { -179.9f, 179.9f, -175.0f, 175.0f };
    for (size_t j = 0; j < sizeof k_from / sizeof k_from[0]; ++j) {
        imu_t m;
        imu_init(&m);
        imu_set_filter(&m, 4);
        uint32_t t = 100u;
        feed(&m, k_from[j], t);
        const float from = word_of(k_from[j]), to = word_of(k_to[j]);
        /* The short way: 0.2° or 10° across the wrap. */
        const float span = 360.0f - fabsf(from - to);
        const float dir = (from > 0.0f) ? 1.0f : -1.0f;
        for (int n = 1; n <= 40; ++n) {
            t += 100u;
            feed(&m, k_to[j], t);
            for (int a = 0; a < 3; ++a) {
                CHECK(fabsf(m.r.angle[a]) >= 170.0f);
                CHECK(m.r.angle[a] >= -180.0f && m.r.angle[a] < 180.0f);
            }
            /* The distance covered the short way, toward the wrap. */
            float moved = (m.r.angle[0] - from) * dir;
            if (moved < 0.0f) {
                moved += 360.0f;
            }
            const float want = span * (1.0f - powf(0.75f, (float)n));
            CHECK_NEAR(moved, want, 2e-3f);
        }
        CHECK_NEAR(m.r.angle[0], to, 1e-3f);
    }
    /* imu_filter_step() itself, both ways. */
    CHECK_NEAR(imu_filter_step(179.9f, -179.9f, 2), -180.0f, 1e-3f);
    CHECK_NEAR(imu_filter_step(-179.9f, 179.9f, 2), -180.0f, 1e-3f);
    CHECK_NEAR(imu_filter_step(170.0f, -170.0f, 2), -180.0f, 1e-3f);
    CHECK_NEAR(imu_filter_step(-170.0f, 170.0f, 4), -175.0f, 1e-3f);
}

/* 1 and 50 are the range; 0, a negative, 51, 99 and INT32_MAX are held to
 * it, in the clamp, in the model and in the step. */
TEST_CASE(the_limits_1_and_50_hold_and_out_of_range_values_are_clamped)
{
    CHECK_EQ(imu_filter_clamp(1), 1u);
    CHECK_EQ(imu_filter_clamp(50), 50u);
    CHECK_EQ(imu_filter_clamp(2), 2u);
    CHECK_EQ(imu_filter_clamp(49), 49u);
    CHECK_EQ(imu_filter_clamp(0), 1u);
    CHECK_EQ(imu_filter_clamp(-1), 1u);
    CHECK_EQ(imu_filter_clamp(INT32_MIN), 1u);
    CHECK_EQ(imu_filter_clamp(51), 50u);
    CHECK_EQ(imu_filter_clamp(99), 50u);
    CHECK_EQ(imu_filter_clamp(INT32_MAX), 50u);
    imu_t m;
    imu_init(&m);
    imu_set_filter(&m, 99);
    CHECK_EQ(m.filter, 50u);
    imu_set_filter(&m, 0);
    CHECK_EQ(m.filter, 1u);
    imu_set_filter(&m, 50);
    CHECK_EQ(m.filter, 50u);
    CHECK_NEAR(imu_filter_step(0.0f, 50.0f, 50), 1.0f, 1e-6f);
    CHECK_NEAR(imu_filter_step(0.0f, 50.0f, 51), 1.0f, 1e-6f);
    CHECK_NEAR(imu_filter_step(0.0f, 50.0f, 1000), 1.0f, 1e-6f);
    CHECK_EQ(imu_filter_step(0.0f, 50.0f, -3), 50.0f);
    /* The sensor side holds a stored value the same way. */
    imu_sensor_t s;
    imu_sensor_init(&s);
    imu_sensor_filter(&s, 200);
    CHECK_EQ(s.filter, 50u);
    CHECK_EQ(s.m.filter, 50u);
    imu_sensor_filter(&s, -7);
    CHECK_EQ(s.filter, 1u);
}

/**
 * @brief A step of @p deg held after a seed at 0, a frame every
 *        @p period ms with IMU FILTER @p k: the time from the step's frame
 *        to the first frame with a still time.
 * @param k      IMU FILTER.
 * @param deg    The step, °.
 * @param period The frame period, ms.
 * @param m      Takes the model at that frame.
 * @return The time, ms.
 */
static uint32_t settle_after_step(int32_t k, float deg, uint32_t period,
                                  imu_t *m)
{
    imu_init(m);
    imu_set_filter(m, k);
    uint32_t t = 10000u;
    feed3(m, 0.0f, 0.0f, 0.0f, t);
    const uint32_t from = t + period;
    for (int n = 0; n < 4000; ++n) {
        t += period;
        feed3(m, deg, 0.0f, 0.0f, t);
        if (m->r.still_ms > 0u) {
            return t - from;
        }
    }
    return UINT32_MAX;
}

/* The still time reads 0 until the filtered angle lies within 0.05° of
 * the sensor's, then counts from the sensor's stop: a run that waits for
 * 500ms of still time takes an angle within 0.05° of the surface, for k
 * 1, 2, 10 and 50, never a lagging one.  With k 1 it counts at once. */
TEST_CASE(the_still_time_waits_until_the_filtered_angle_settled)
{
    static const int32_t k_k[] = { 1, 2, 10, 50 };
    for (size_t j = 0; j < sizeof k_k / sizeof k_k[0]; ++j) {
        imu_t m;
        const uint32_t ms = settle_after_step(k_k[j], 30.0f, 100u, &m);
        CHECK(ms != UINT32_MAX);
        CHECK(fabsf(m.r.angle[0] - word_of(30.0f)) <= IMU_SETTLE_DEG);
        /* From the sensor's stop: the step's frame. */
        CHECK_EQ(m.r.still_ms, ms);
        if (k_k[j] == 1) {
            CHECK_EQ(ms, 100u);
        } else {
            CHECK(ms > 500u);
        }
        /* Every frame from then on: still, and within 0.05°. */
        uint32_t t = m.r.taken_ms;
        for (int n = 0; n < 10; ++n) {
            t += 100u;
            feed3(&m, 30.0f, 0.0f, 0.0f, t);
            CHECK(m.r.still_ms > ms);
            CHECK(fabsf(m.r.angle[0] - word_of(30.0f)) <= IMU_SETTLE_DEG);
        }
    }
    /* k 10, 27° left after the step's frame: 0.9^n x 27 <= 0.05 at the
     * 60th frame after it, 6,000ms; the frame before it is no still. */
    imu_t m;
    CHECK_EQ(settle_after_step(10, 30.0f, 100u, &m), 6000u);
    /* A move again starts over: 0 until it settled once more. */
    feed3(&m, 10.0f, 0.0f, 0.0f, m.r.taken_ms + 100u);
    CHECK_EQ(m.r.still_ms, 0u);
    feed3(&m, 10.0f, 0.0f, 0.0f, m.r.taken_ms + 100u);
    CHECK_EQ(m.r.still_ms, 0u);
}

/* The allowance: 0 for k 1; for k 10 at 10Hz 78 frames at 150ms; with no
 * rate or one below 3.3Hz, 300ms a frame.  It covers the settle of the
 * largest step, 179°, for every k from 2 to 50 at 10Hz and 50Hz. */
TEST_CASE(the_settle_allowance_covers_the_slowest_settle)
{
    CHECK_EQ(imu_filter_settle_ms(1, 100u), 0u);
    CHECK_EQ(imu_filter_settle_ms(0, 100u), 0u);
    CHECK_EQ(imu_filter_settle_ms(2, 100u), 12u * 150u);
    CHECK_EQ(imu_filter_settle_ms(10, 100u), 78u * 150u);
    CHECK_EQ(imu_filter_settle_ms(50, 100u), 406u * 150u);
    CHECK_EQ(imu_filter_settle_ms(99, 100u), 406u * 150u);
    CHECK_EQ(imu_filter_settle_ms(10, 500u), 78u * 30u);
    CHECK_EQ(imu_filter_settle_ms(10, 0u), 78u * 300u);
    CHECK_EQ(imu_filter_settle_ms(10, 50u), 78u * 300u);
    CHECK_EQ(imu_filter_settle_ms(10, 51u), 78u * 295u);
    static const struct { uint32_t period; uint16_t dhz; } k_r[] = {
        { 100u, 100u }, { 20u, 500u },
    };
    for (size_t r = 0; r < sizeof k_r / sizeof k_r[0]; ++r) {
        for (int32_t k = 2; k <= 50; ++k) {
            imu_t m;
            const uint32_t ms = settle_after_step(k, 179.0f, k_r[r].period,
                                                  &m);
            CHECK(ms <= imu_filter_settle_ms(k, k_r[r].dhz));
        }
    }
}

/* Two decimals, half away from zero at the hundredth as the float holds
 * the angle, the sign only on a value that is not 0.00, "---" for a value
 * that is no angle; a short buffer cut, NULL and 0 left alone. */
TEST_CASE(the_angle_text_has_two_decimals_and_rounds_005_away_from_zero)
{
    static const struct { float v; const char *plain, *plus; } k[] = {
        { 0.0f, "0.00", "+0.00" },
        { 12.345f, "12.35", "+12.35" },
        { -12.345f, "-12.35", "-12.35" },
        { 0.005f, "0.01", "+0.01" },
        { -0.005f, "-0.01", "-0.01" },
        { 1.005f, "1.01", "+1.01" },
        { -1.005f, "-1.01", "-1.01" },
        { 0.0049f, "0.00", "+0.00" },
        { -0.0049f, "0.00", "+0.00" },
        { -0.004f, "0.00", "+0.00" },
        { -0.0f, "0.00", "+0.00" },
        { 0.015f, "0.02", "+0.02" },
        { 179.995f, "180.00", "+180.00" },
        { -180.0f, "-180.00", "-180.00" },
        { 89.99f, "89.99", "+89.99" },
        { -7.1f, "-7.10", "-7.10" },
        { 100000.0f, "100000.00", "+100000.00" },
        { 100001.0f, "---", "---" },
        { NAN, "---", "---" },
        { INFINITY, "---", "---" },
        { -INFINITY, "---", "---" },
    };
    char b[16];
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        CHECK_STR_EQ(imu_deg_text(b, sizeof b, k[i].v, false), k[i].plain);
        CHECK_STR_EQ(imu_deg_text(b, sizeof b, k[i].v, true), k[i].plus);
    }
    /* "-180.00" in 9 bytes, cut in 5. */
    char nine[9];
    CHECK_STR_EQ(imu_deg_text(nine, sizeof nine, -180.0f, false), "-180.00");
    char five[5];
    CHECK_STR_EQ(imu_deg_text(five, sizeof five, -180.0f, false), "-180");
    CHECK(imu_deg_text(NULL, 8u, 1.0f, false) == NULL);
    b[0] = 'x';
    CHECK(imu_deg_text(b, 0u, 1.0f, false) == b);
    CHECK_EQ(b[0], 'x');
}

int main(void)
{
    RUN(a_filter_of_1_passes_every_frame_through);
    RUN(the_step_response_moves_one_kth_of_the_rest_each_frame);
    RUN(the_first_frame_seeds_the_filter_after_init_reseed_and_stale);
    RUN(a_connect_a_reconnect_and_a_bind_seed_the_filter);
    RUN(a_crossing_of_180_moves_the_short_way_both_directions);
    RUN(the_limits_1_and_50_hold_and_out_of_range_values_are_clamped);
    RUN(the_still_time_waits_until_the_filtered_angle_settled);
    RUN(the_settle_allowance_covers_the_slowest_settle);
    RUN(the_angle_text_has_two_decimals_and_rounds_005_away_from_zero);
    return test_summary("imu_filter");
}
