/*
 * The IMU model fed three angles and a time (imu_feed_angles()), for a
 * sensor other than the WitMotion: the same low-pass, still time, rate
 * and age as a WitMotion angle frame through imu_feed().  Needs imu.h,
 * wit_frame.h and greatest.h only.
 *
 * The failures this is about: an angle that the path quantises or moves
 * by another share than 1/k; a still time that counts before the
 * filtered angle settled or from another frame than the sensor's stop; a
 * reading that differs from the WitMotion path's for the same angles at
 * the same times; an angle outside -180 to 180 that seeds the filter
 * there; a NaN that poisons the filter or keeps a reading valid; and a
 * fed angle that changes the WitMotion stream's counts.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "greatest.h"
#include "imu.h"
#include "wit_frame.h"

/* A frame every 128ms: four samples of a 31.25Hz stream averaged, about
 * 7.8Hz. */
#define PERIOD_MS 128u

/** Feed @p m roll @p x, pitch 0 and yaw 0 at @p t. */
static void feed_x(imu_t *m, float x, uint32_t t)
{
    imu_feed_angles(m, x, 0.0f, 0.0f, t);
}

/* IMU FILTER 1 passes each angle through as it came, on every axis, with
 * no step of the WitMotion word (0.0055°): 12.3456° stays 12.3456°. */
TEST_CASE(a_filter_of_1_takes_each_angle_as_it_came)
{
    static const float k_v[] = { 0.0f, 12.3456f, -0.001f, 179.999f,
                                 -180.0f, -45.678f };
    imu_t m;
    imu_init(&m);
    uint32_t t = 1000u;
    for (size_t i = 0; i < sizeof k_v / sizeof k_v[0]; ++i) {
        imu_feed_angles(&m, k_v[i], -k_v[i] / 2.0f, k_v[i] / 4.0f, t);
        CHECK(m.r.valid);
        CHECK_EQ(m.r.taken_ms, t);
        CHECK_FEQ(m.r.angle[0], k_v[i]);
        CHECK_FEQ(m.r.angle[1], -k_v[i] / 2.0f);
        CHECK_FEQ(m.r.angle[2], k_v[i] / 4.0f);
        CHECK_NEAR(imu_deg(&m.r, IMU_AXIS_X, false), k_v[i], 1e-4f);
        t += PERIOD_MS;
    }
}

/* From a seed of 0 to a step of 10° on every axis: after n frames the
 * angle is 10 (1 - (1-1/k)^n), for k 2, 3, 10 and 50.  k 3 covers 70% of
 * the step in 3 frames and 99% in 12. */
TEST_CASE(the_step_response_moves_one_kth_of_the_rest_each_frame)
{
    static const int32_t k_k[] = { 2, 3, 10, 50 };
    for (size_t j = 0; j < sizeof k_k / sizeof k_k[0]; ++j) {
        const int32_t k = k_k[j];
        imu_t m;
        imu_init(&m);
        imu_set_filter(&m, k);
        uint32_t t = 500u;
        imu_feed_angles(&m, 0.0f, 0.0f, 0.0f, t);
        CHECK_FEQ(m.r.angle[0], 0.0f);
        float want = 0.0f;
        for (int32_t n = 1; n <= 3 * k; ++n) {
            t += PERIOD_MS;
            imu_feed_angles(&m, 10.0f, 10.0f, 10.0f, t);
            want += (10.0f - want) / (float)k;
            for (int a = 0; a < 3; ++a) {
                CHECK_NEAR(m.r.angle[a], want, 1e-4f);
            }
            if (k == 3 && n == 3) {
                CHECK_NEAR(m.r.angle[0], 10.0f * (1.0f - 8.0f / 27.0f),
                           1e-4f);
            }
            if (k == 3 && n == 12) {
                CHECK(m.r.angle[0] >= 9.9f);
            }
        }
        CHECK(m.r.angle[0] < 10.0f);
    }
}

/* k 3 at 128ms, still at 0° and then a step of 10° on roll held: roll's
 * still time reads 0 until its filtered angle lies within 0.05° of 10°,
 * the 13th frame after the step's (6.67 x (2/3)^13 = 0.034°), then counts
 * from the step's frame, 1664ms.  Pitch's and yaw's run on throughout.  A
 * move of more than 0.5° starts roll's stretch again. */
TEST_CASE(the_still_time_waits_for_the_settle_and_counts_from_the_stop)
{
    imu_t m;
    imu_init(&m);
    imu_set_filter(&m, 3);
    const uint32_t t0 = 5000u;
    uint32_t t = t0;
    for (int n = 0; n <= 8; ++n, t += PERIOD_MS) {
        feed_x(&m, 0.0f, t);
    }
    CHECK_EQ(imu_still_ms(&m.r, IMU_AXIS_X), 8u * PERIOD_MS);
    const uint32_t step = t;
    for (int n = 0; n < 13; ++n, t += PERIOD_MS) {
        feed_x(&m, 10.0f, t);
        CHECK_EQ(imu_still_ms(&m.r, IMU_AXIS_X), 0u);
        CHECK(fabsf(m.r.angle[0] - 10.0f) > IMU_SETTLE_DEG);
        CHECK_EQ(imu_still_ms(&m.r, IMU_AXIS_Y), t - t0);
        CHECK_EQ(imu_still_ms(&m.r, IMU_AXIS_Z), t - t0);
    }
    feed_x(&m, 10.0f, t);
    CHECK_EQ(imu_still_ms(&m.r, IMU_AXIS_X), 13u * PERIOD_MS);
    CHECK_EQ(t - step, 1664u);
    CHECK(fabsf(m.r.angle[0] - 10.0f) <= IMU_SETTLE_DEG);
    /* Noise inside IMU_STILL_DEG keeps it counting once settled. */
    t += PERIOD_MS;
    feed_x(&m, 10.02f, t);
    CHECK_EQ(imu_still_ms(&m.r, IMU_AXIS_X), t - step);
    /* 0.6° away from the stretch's first frame: a new stretch. */
    t += PERIOD_MS;
    feed_x(&m, 10.6f, t);
    CHECK_EQ(imu_still_ms(&m.r, IMU_AXIS_X), 0u);
    t += PERIOD_MS;
    feed_x(&m, 10.6f, t);
    CHECK_EQ(imu_still_ms(&m.r, IMU_AXIS_X), 0u);
}

/* The reading goes stale IMU_STALE_MS after the last frame; a frame after
 * a gap of 300ms seeds the filter, one after 299ms steps it; a reseed
 * seeds; the rate counts the frames fed, 7.8Hz at 128ms. */
TEST_CASE(the_age_the_seed_and_the_rate_follow_the_frames_fed)
{
    imu_t m;
    imu_init(&m);
    imu_set_filter(&m, 4);
    uint32_t t = 0u;
    for (int n = 0; n < 9; ++n, t += PERIOD_MS) {
        feed_x(&m, 20.0f, t);
    }
    /* The window from 0 closes at the 9th frame, 1024ms: 9 frames, 8.7Hz. */
    CHECK_EQ(m.r.rate_dhz, 87u);
    const uint32_t last = m.r.taken_ms;
    imu_tick(&m, last + IMU_STALE_MS - 1u);
    CHECK(m.r.valid);
    feed_x(&m, 40.0f, last + IMU_STALE_MS - 1u);
    CHECK_NEAR(m.r.angle[0], 25.0f, 1e-4f);
    imu_tick(&m, m.r.taken_ms + IMU_STALE_MS);
    CHECK(!m.r.valid);
    CHECK_FEQ(imu_deg(&m.r, IMU_AXIS_X, false), 0.0f);
    feed_x(&m, -30.0f, m.r.taken_ms + 400u);
    CHECK(m.r.valid);
    CHECK_FEQ(m.r.angle[0], -30.0f);
    /* A gap of 300ms that no tick saw seeds too. */
    feed_x(&m, 50.0f, m.r.taken_ms + IMU_STALE_MS);
    CHECK_FEQ(m.r.angle[0], 50.0f);
    imu_reseed(&m);
    feed_x(&m, 70.0f, m.r.taken_ms + PERIOD_MS);
    CHECK_FEQ(m.r.angle[0], 70.0f);
    feed_x(&m, 74.0f, m.r.taken_ms + PERIOD_MS);
    CHECK_NEAR(m.r.angle[0], 71.0f, 1e-4f);
}

/**
 * @brief The WitMotion path's angles for a frame of @p x, @p y, @p z: the
 *        frame built and decoded as imu_feed() decodes it.
 * @param x   Roll, °.
 * @param y   Pitch, °.
 * @param z   Yaw, °.
 * @param f   Takes the frame.
 * @param out Takes the decoded angles.
 */
static void wit_angles(float x, float y, float z, uint8_t f[WIT_FRAME_LEN],
                       wit_angle_t *out)
{
    wit_angle_frame(f, x, y, z);
    wit_parser_t p;
    wit_parser_init(&p);
    wit_frame_t fr;
    bool got = false;
    for (size_t i = 0; i < WIT_FRAME_LEN; ++i) {
        got = wit_parser_feed(&p, f[i], &fr);
    }
    CHECK(got);
    CHECK(wit_decode_angle(&fr, out));
}

/* Every field of the angle path equal on the two models. */
#define CHECK_SAME(a, b)                                                     \
    do {                                                                     \
        CHECK_EQ((a).r.valid, (b).r.valid);                                  \
        CHECK_EQ((a).r.taken_ms, (b).r.taken_ms);                            \
        CHECK_EQ((a).r.rate_dhz, (b).r.rate_dhz);                            \
        CHECK_EQ((a).rate_count, (b).rate_count);                            \
        CHECK_EQ((a).rate_from_ms, (b).rate_from_ms);                        \
        CHECK_EQ((a).seed, (b).seed);                                        \
        CHECK_EQ((a).r.zeroed, (b).r.zeroed);                                \
        for (int ax_ = 0; ax_ < 3; ++ax_) {                                  \
            CHECK_FEQ((a).r.angle[ax_], (b).r.angle[ax_]);                    \
            CHECK_FEQ((a).r.zero[ax_], (b).r.zero[ax_]);                      \
            CHECK_EQ((a).r.still_ms[ax_], (b).r.still_ms[ax_]);              \
            CHECK_FEQ((a).still_ref[ax_], (b).still_ref[ax_]);                \
            CHECK_EQ((a).still_from_ms[ax_], (b).still_from_ms[ax_]);        \
            CHECK_EQ((a).settled[ax_], (b).settled[ax_]);                    \
            CHECK_FEQ((a).resid[ax_], (b).resid[ax_]);                        \
        }                                                                    \
    } while (0)

/* 3,000 frames of random angles, each as a WitMotion frame into one model
 * and as its decoded angles into the other, at the same times: periods of
 * 5 to 400ms (gaps past IMU_STALE_MS among them), ticks between frames,
 * IMU FILTER changed, reseeds, zeros, holds of the same angles long
 * enough to settle, and crossings of ±180°.  Every field of the angle
 * path is the same after every call; the WitMotion stream's counts move
 * on its model only. */
TEST_CASE(the_same_angles_give_the_same_reading_as_the_witmotion_path)
{
    imu_t w, a;
    imu_init(&w);
    imu_init(&a);
    uint32_t seed = 12345u;
    uint32_t t = 0xFFFFF000u;   /* across the clock's wrap */
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int holds = 0;
    for (int n = 0; n < 3000; ++n) {
        seed = seed * 1103515245u + 12345u;
        const uint32_t r = seed >> 8;
        if (holds > 0) {
            --holds;
        } else if (r % 7u == 0u) {
            holds = (int)(r % 40u);
        } else {
            x = (float)((int32_t)(r % 36000u) - 18000) / 100.0f;
            y = (r % 3u == 0u) ? y + 0.3f : y;
            z = (r % 5u == 0u) ? 179.9f : -179.9f;
        }
        if (r % 97u == 0u) {
            const int32_t k = (int32_t)(r % 52u);
            imu_set_filter(&w, k);
            imu_set_filter(&a, k);
        }
        if (r % 211u == 0u) {
            imu_reseed(&w);
            imu_reseed(&a);
        }
        if (r % 131u == 0u) {
            CHECK_EQ(imu_zero(&w), imu_zero(&a));
        }
        const uint32_t gap = (r % 50u == 0u) ? 300u + r % 200u
                                             : 5u + r % 150u;
        if (r % 3u == 0u) {
            imu_tick(&w, t + gap / 2u);
            imu_tick(&a, t + gap / 2u);
            CHECK_SAME(w, a);
        }
        t += gap;
        uint8_t f[WIT_FRAME_LEN];
        wit_angle_t ang;
        wit_angles(x, y, z, f, &ang);
        imu_feed(&w, f, sizeof f, t);
        imu_feed_angles(&a, ang.roll, ang.pitch, ang.yaw, t);
        CHECK_SAME(w, a);
        imu_reading_t rw, ra;
        CHECK_EQ(imu_reading(&w, &rw), imu_reading(&a, &ra));
        for (int ax = 0; ax < 3; ++ax) {
            CHECK_FEQ(imu_deg(&rw, (imu_axis_t)ax, true),
                     imu_deg(&ra, (imu_axis_t)ax, true));
            CHECK_EQ(imu_still_ms(&rw, (imu_axis_t)ax),
                     imu_still_ms(&ra, (imu_axis_t)ax));
        }
    }
    /* The stream's counts: 3,000 frames and their version on the
     * WitMotion model, none on the other. */
    CHECK_EQ(w.r.frames, 3000u);
    CHECK_EQ(w.r.version, 0x1234u);
    CHECK_EQ(a.r.frames, 0u);
    CHECK_EQ(a.r.version, 0u);
    CHECK_EQ(a.r.bad, 0u);
    CHECK_EQ(a.content, 0u);
    CHECK_EQ(a.content_n, 0u);
    CHECK(isnan(a.r.battery_v));
    CHECK(isnan(a.r.temp_c));
}

/* An angle below -180° or at 180° and above is wrapped first: 180° is
 * -180°, 190° is -170°, -190° is 170°, 539.5° is 179.5°, -540° is -180°.
 * It seeds and steps the filter as its wrapped value does. */
TEST_CASE(an_angle_outside_minus_180_to_180_is_wrapped_first)
{
    static const struct { float in, want; } k[] = {
        { 180.0f, -180.0f }, { 190.0f, -170.0f }, { -190.0f, 170.0f },
        { 539.5f, 179.5f }, { -540.0f, -180.0f }, { -180.0f, -180.0f },
        { 179.5f, 179.5f },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        imu_t m;
        imu_init(&m);
        imu_feed_angles(&m, k[i].in, k[i].in, k[i].in, 100u);
        for (int a = 0; a < 3; ++a) {
            CHECK_NEAR(m.r.angle[a], k[i].want, 1e-4f);
        }
        CHECK(m.r.angle[0] >= -180.0f && m.r.angle[0] < 180.0f);
    }
    /* With k 2, from 170° to 200° (-160°): 15° the short way, to -175°. */
    imu_t m;
    imu_init(&m);
    imu_set_filter(&m, 2);
    feed_x(&m, 170.0f, 100u);
    feed_x(&m, 200.0f, 200u);
    CHECK_NEAR(m.r.angle[0], -175.0f, 1e-4f);
}

/* A frame with an angle that is not finite is dropped whole: the other
 * two axes keep their angle, the still time, the rate and the time of
 * the last frame stay, and the reading ages to stale 300ms after the
 * last finite frame.  NULL does nothing. */
TEST_CASE(a_frame_with_a_nan_or_an_infinity_is_dropped_whole)
{
    imu_t m;
    imu_init(&m);
    imu_set_filter(&m, 2);
    imu_feed_angles(&m, 10.0f, 20.0f, 30.0f, 1000u);
    imu_feed_angles(&m, 10.0f, 20.0f, 30.0f, 1100u);
    const imu_t before = m;
    imu_feed_angles(&m, NAN, 50.0f, 50.0f, 1200u);
    imu_feed_angles(&m, 50.0f, INFINITY, 50.0f, 1250u);
    imu_feed_angles(&m, 50.0f, 50.0f, -INFINITY, 1299u);
    CHECK(m.r.valid);
    CHECK_EQ(m.r.taken_ms, 1100u);
    CHECK_EQ(m.rate_count, before.rate_count);
    for (int a = 0; a < 3; ++a) {
        CHECK_FEQ(m.r.angle[a], before.r.angle[a]);
        CHECK_EQ(m.r.still_ms[a], before.r.still_ms[a]);
    }
    CHECK(memcmp(m.still_ref, before.still_ref, sizeof m.still_ref) == 0);
    imu_feed_angles(&m, NAN, NAN, NAN, 1400u);
    CHECK(!m.r.valid);
    CHECK_EQ(imu_still_ms(&m.r, IMU_AXIS_Y), 0u);
    /* The next finite frame seeds. */
    imu_feed_angles(&m, 1.0f, 2.0f, 3.0f, 1500u);
    CHECK(m.r.valid);
    CHECK_FEQ(m.r.angle[2], 3.0f);
    imu_feed_angles(NULL, 1.0f, 2.0f, 3.0f, 1600u);
}

int main(void)
{
    RUN(a_filter_of_1_takes_each_angle_as_it_came);
    RUN(the_step_response_moves_one_kth_of_the_rest_each_frame);
    RUN(the_still_time_waits_for_the_settle_and_counts_from_the_stop);
    RUN(the_age_the_seed_and_the_rate_follow_the_frames_fed);
    RUN(the_same_angles_give_the_same_reading_as_the_witmotion_path);
    RUN(an_angle_outside_minus_180_to_180_is_wrapped_first);
    RUN(a_frame_with_a_nan_or_an_infinity_is_dropped_whole);
    return test_summary("imu_angles");
}
