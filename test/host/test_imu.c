/*
 * The IMU's reading (imu.h).
 *
 * The failures this is about: an angle that stays valid after the sensor
 * went quiet and feeds a run a stale angle; a zero that wraps the wrong way
 * at 180°; a still time that runs on while the surface moves; a rate that
 * spreads its count over a silence; a battery that reads 0V before its
 * frame came.  The frames are built byte by byte (imu_rig.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "imu_rig.h"

#include "servo_gap.h"

/* The non-safety allowance of servo_gap.h, the IMU's stale time. */
_Static_assert(IMU_STALE_MS == SERVO_GAP_MAX_MS,
               "the IMU goes stale at the dropout allowance of a run");

/* What 1 LSB of the angle word is, °. */
#define LSB (180.0f / 32768.0f)

TEST_CASE(imu_stale_equals_the_gap_allowance)
{
    CHECK_EQ(IMU_STALE_MS, 300u);
    CHECK_EQ(IMU_STALE_MS, SERVO_GAP_MAX_MS);
}

/* Valid from the first angle frame; at 299ms still, at 300ms and 301ms
 * not.  The same across the clock's wrap. */
TEST_CASE(angle_frames_make_the_reading_valid_and_stale_after_300ms)
{
    static const uint32_t k_t0[] = { 1000u, 0xFFFFFF00u };
    for (size_t k = 0; k < sizeof k_t0 / sizeof k_t0[0]; ++k) {
        const uint32_t t0 = k_t0[k];
        imu_t m;
        imu_init(&m);
        imu_reading_t r;
        CHECK(!imu_reading(&m, &r));
        rig_feed_angle(&m, 10.0f, -20.0f, 30.0f, t0);
        CHECK(imu_reading(&m, &r));
        CHECK_EQ(r.taken_ms, t0);
        CHECK(fabsf(r.angle[0] - 10.0f) < LSB);
        CHECK(fabsf(r.angle[1] + 20.0f) < LSB);
        CHECK(fabsf(r.angle[2] - 30.0f) < LSB);
        CHECK_EQ(r.version, 0x1234u);
        imu_tick(&m, t0 + 299u);
        CHECK(imu_reading(&m, &r));
        imu_tick(&m, t0 + 300u);
        CHECK(!imu_reading(&m, &r));
        imu_tick(&m, t0 + 301u);
        CHECK(!imu_reading(&m, &r));
        CHECK_EQ(imu_deg(&r, IMU_AXIS_X, false), 0.0f);
        /* A frame again makes it valid again. */
        rig_feed_angle(&m, 11.0f, 0.0f, 0.0f, t0 + 400u);
        CHECK(imu_reading(&m, &r));
    }
    /* Only an angle frame does: an acceleration frame does not. */
    imu_t m;
    imu_init(&m);
    uint8_t f[WIT_FRAME_LEN];
    rig_frame(f, WIT_T_ACC, 0, 0, 2048, 2500);
    imu_feed(&m, f, sizeof f, 5u);
    imu_reading_t r;
    CHECK(!imu_reading(&m, &r));
    CHECK_EQ(r.frames, 1u);
}

/* From the zero, wrapped to -180 up to 180, negated with the sign. */
TEST_CASE(the_zero_is_subtracted_and_wrapped_at_180)
{
    imu_t m;
    imu_init(&m);
    CHECK(!imu_zero(&m));               /* nothing to take yet */
    rig_feed_angle(&m, 170.0f, -170.0f, 0.0f, 10u);
    CHECK(imu_zero(&m));
    imu_reading_t r;
    (void)imu_reading(&m, &r);
    CHECK(r.zeroed);
    CHECK(fabsf(imu_deg(&r, IMU_AXIS_X, false)) < LSB);
    /* X from 170 to -170 is +20, not -340; Y from -170 to 170 is -20. */
    rig_feed_angle(&m, -170.0f, 170.0f, 90.0f, 20u);
    (void)imu_reading(&m, &r);
    CHECK(fabsf(imu_deg(&r, IMU_AXIS_X, false) - 20.0f) < 2.0f * LSB);
    CHECK(fabsf(imu_deg(&r, IMU_AXIS_Y, false) + 20.0f) < 2.0f * LSB);
    CHECK(fabsf(imu_deg(&r, IMU_AXIS_Z, false) - 90.0f) < 2.0f * LSB);
    CHECK(fabsf(imu_deg(&r, IMU_AXIS_X, true) + 20.0f) < 2.0f * LSB);
    /* Exactly half a turn from the zero reads -180, with either sign. */
    imu_unzero(&m);
    (void)imu_reading(&m, &r);
    CHECK(!r.zeroed);
    r.angle[0] = 180.0f;
    CHECK_EQ(imu_deg(&r, IMU_AXIS_X, false), -180.0f);
    CHECK_EQ(imu_deg(&r, IMU_AXIS_X, true), -180.0f);
    r.angle[0] = 179.0f;
    CHECK(fabsf(imu_deg(&r, IMU_AXIS_X, true) + 179.0f) < 1e-4f);
    /* An axis out of range reads 0, as no reading does. */
    CHECK_EQ(imu_deg(&r, (imu_axis_t)3, false), 0.0f);
    CHECK_EQ(imu_deg(&r, (imu_axis_t)-1, false), 0.0f);
    CHECK_EQ(imu_deg(NULL, IMU_AXIS_X, false), 0.0f);
}

/* Within 0.5° of where it settled on every axis the still time runs; a
 * move past it on one axis starts it again; a stale reading ends it. */
TEST_CASE(still_ms_counts_while_the_angle_stays_within_half_a_degree)
{
    imu_t m;
    imu_init(&m);
    imu_reading_t r;
    rig_feed_angle(&m, 0.0f, 0.0f, 0.0f, 1000u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.still_ms, 0u);
    rig_feed_angle(&m, 0.45f, -0.45f, 0.45f, 1100u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.still_ms, 100u);
    rig_feed_angle(&m, -0.45f, 0.0f, 0.0f, 1200u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.still_ms, 200u);
    /* 0.6° on Z alone: moved. */
    rig_feed_angle(&m, 0.0f, 0.0f, 0.6f, 1300u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.still_ms, 0u);
    rig_feed_angle(&m, 0.0f, 0.0f, 0.6f, 1350u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.still_ms, 50u);
    /* Across ±180 the difference is the short way round. */
    rig_feed_angle(&m, 179.9f, 0.0f, 0.6f, 1400u);
    rig_feed_angle(&m, -179.9f, 0.0f, 0.6f, 1450u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.still_ms, 50u);
    /* Stale: the still time ends, and the next frame starts it again. */
    imu_tick(&m, 1750u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.still_ms, 0u);
    rig_feed_angle(&m, -179.9f, 0.0f, 0.6f, 1800u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.still_ms, 0u);
    /* It holds at 65535ms. */
    for (uint32_t t = 1900u; t < 1900u + 70000u; t += 200u) {
        rig_feed_angle(&m, -179.9f, 0.0f, 0.6f, t);
    }
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.still_ms, 65535u);
}

/* The angle frames of each window of 1s, in 0.1Hz; a window that ran two
 * lengths for want of frames reads 0. */
TEST_CASE(the_rate_counts_angle_frames_over_the_last_second)
{
    imu_t m;
    imu_init(&m);
    imu_tick(&m, 0u);
    /* 50 frames in the first second, 20ms apart. */
    for (uint32_t t = 0u; t < 1000u; t += 20u) {
        rig_feed_angle(&m, 0.0f, 0.0f, 0.0f, t);
    }
    imu_reading_t r;
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.rate_dhz, 0u);           /* the window is still open */
    imu_tick(&m, 999u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.rate_dhz, 0u);
    imu_tick(&m, 1000u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.rate_dhz, 500u);
    /* Other frames do not count: 10 acceleration frames in the next. */
    uint8_t f[WIT_FRAME_LEN];
    rig_frame(f, WIT_T_ACC, 0, 0, 0, 0);
    for (uint32_t t = 1000u; t < 2000u; t += 100u) {
        imu_feed(&m, f, sizeof f, t);
    }
    for (uint32_t t = 1000u; t < 2000u; t += 100u) {
        rig_feed_angle(&m, 0.0f, 0.0f, 0.0f, t);
    }
    imu_tick(&m, 2000u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.rate_dhz, 100u);
    /* A window closed late by 250ms: its count over its 1250ms. */
    for (uint32_t t = 2000u; t < 3000u; t += 40u) {
        rig_feed_angle(&m, 0.0f, 0.0f, 0.0f, t);
    }
    imu_tick(&m, 3250u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.rate_dhz, 200u);
    /* Silence of 2s: 0, not a count spread over it. */
    rig_feed_angle(&m, 0.0f, 0.0f, 0.0f, 3300u);
    imu_tick(&m, 5250u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.rate_dhz, 0u);
    /* Across the clock's wrap. */
    imu_init(&m);
    imu_tick(&m, 0xFFFFFE00u);
    for (uint32_t k = 0u; k < 20u; ++k) {
        rig_feed_angle(&m, 0.0f, 0.0f, 0.0f, 0xFFFFFE00u + 50u * k);
    }
    imu_tick(&m, 0xFFFFFE00u + 1000u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.rate_dhz, 200u);
}

/* NAN until their frames came; 0x52's fourth word unsigned /100. */
TEST_CASE(battery_and_temperature_come_from_their_frames)
{
    imu_t m;
    imu_init(&m);
    imu_reading_t r;
    (void)imu_reading(&m, &r);
    CHECK(isnan(r.battery_v));
    CHECK(isnan(r.temp_c));
    uint8_t f[WIT_FRAME_LEN];
    rig_frame(f, WIT_T_GYRO, 0, 0, 0, 395);
    imu_feed(&m, f, sizeof f, 1u);
    rig_frame(f, WIT_T_ACC, 0, 0, 2048, 2450);
    imu_feed(&m, f, sizeof f, 2u);
    (void)imu_reading(&m, &r);
    CHECK(fabsf(r.battery_v - 3.95f) < 1e-4f);
    CHECK(fabsf(r.temp_c - 24.5f) < 1e-4f);
    CHECK_EQ(r.frames, 2u);
    /* A magnetic frame counts as a frame and changes nothing else. */
    rig_frame(f, WIT_T_MAG, 1, 2, 3, 4);
    imu_feed(&m, f, sizeof f, 3u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.frames, 3u);
    CHECK(!r.valid);
    /* A bad sum and a stray byte: counted in bad, no frame. */
    rig_frame(f, WIT_T_GYRO, 0, 0, 0, 100);
    f[10] = (uint8_t)(f[10] + 1u);
    imu_feed(&m, f, sizeof f, 4u);
    const uint8_t stray = 0x00u;
    imu_feed(&m, &stray, 1u, 5u);
    (void)imu_reading(&m, &r);
    CHECK_EQ(r.frames, 3u);
    CHECK(r.bad >= 2u);
    CHECK(fabsf(r.battery_v - 3.95f) < 1e-4f);
}

TEST_CASE(a_null_argument_does_nothing)
{
    imu_init(NULL);
    imu_feed(NULL, (const uint8_t *)"x", 1u, 0u);
    imu_t m;
    imu_init(&m);
    imu_feed(&m, NULL, 4u, 0u);
    imu_tick(NULL, 0u);
    CHECK(!imu_zero(NULL));
    imu_unzero(NULL);
    imu_reading_t r;
    CHECK(!imu_reading(NULL, &r));
    CHECK(!imu_reading(&m, NULL));
    CHECK_STR_EQ(imu_axis_name(IMU_AXIS_X), "X");
    CHECK_STR_EQ(imu_axis_name(IMU_AXIS_Y), "Y");
    CHECK_STR_EQ(imu_axis_name(IMU_AXIS_Z), "Z");
    CHECK_STR_EQ(imu_axis_name((imu_axis_t)3), "");
}

/* The frame types since the restart as RSW bits, and the good frames,
 * held at 255; a bad sum counts in neither; a restart clears both. */
TEST_CASE(the_content_counts_frame_types_since_the_restart)
{
    imu_t m;
    imu_init(&m);
    CHECK_EQ(m.content, 0u);
    CHECK_EQ(m.content_n, 0u);
    uint8_t f[WIT_FRAME_LEN];
    rig_frame(f, WIT_T_ACC, 0, 0, 2048, 2450);
    imu_feed(&m, f, sizeof f, 1u);
    rig_frame(f, WIT_T_MAG, 1, 2, 3, 4);
    imu_feed(&m, f, sizeof f, 2u);
    CHECK_EQ(m.content, WIT_RSW_ACC | WIT_RSW_MAG);
    CHECK_EQ(m.content_n, 2u);
    imu_content_restart(&m);
    CHECK_EQ(m.content, 0u);
    CHECK_EQ(m.content_n, 0u);
    rig_frame(f, WIT_T_GYRO, 0, 0, 0, 395);
    f[10] = (uint8_t)(f[10] + 1u);                  /* a bad sum */
    imu_feed(&m, f, sizeof f, 3u);
    CHECK_EQ(m.content, 0u);
    CHECK_EQ(m.content_n, 0u);
    for (int i = 0; i < 300; ++i) {
        rig_frame(f, (i % 2 == 0) ? WIT_T_GYRO : WIT_T_ANGLE, 0, 0, 0, 395);
        imu_feed(&m, f, sizeof f, 4u + (uint32_t)i);
    }
    CHECK_EQ(m.content, WIT_RSW_GYRO | WIT_RSW_ANGLE);
    CHECK_EQ(m.content_n, 255u);
    imu_content_restart(NULL);
}

int main(void)
{
    RUN(imu_stale_equals_the_gap_allowance);
    RUN(angle_frames_make_the_reading_valid_and_stale_after_300ms);
    RUN(the_zero_is_subtracted_and_wrapped_at_180);
    RUN(still_ms_counts_while_the_angle_stays_within_half_a_degree);
    RUN(the_rate_counts_angle_frames_over_the_last_second);
    RUN(battery_and_temperature_come_from_their_frames);
    RUN(the_content_counts_frame_types_since_the_restart);
    RUN(a_null_argument_does_nothing);
    return test_summary("imu");
}
