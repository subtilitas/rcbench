/*
 * The sensor side of the Bluetooth IMU (imu_sensor.h) without a radio: the
 * test stands in for the link, says whether the stream flows and takes the
 * commands when it asks for them.
 *
 * The failures this is about: two commands closer than 50ms; a calibration
 * that ends a ms early or late, or not at all across the 32-bit wrap; a
 * RATE written while the sensor is not connected, or twice; a write group
 * cut by a full outbox; a stale stretch of exactly 300ms not counted; an
 * IMU OFF that leaves the sensor in CALSW 1 or keeps an old zero; a
 * failed write that loses the calibration's exit or a RATE for good; an
 * RSW written to a sensor that holds 0x0C, or twice on one connection.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "greatest.h"
#include "imu_sensor.h"

/** An angle frame of roll @p deg, pitch and yaw 0, version 0x1234. */
static void angle_frame(uint8_t f[WIT_FRAME_LEN], float deg)
{
    const long w = lround((double)deg * 32768.0 / 180.0);
    const uint16_t word[4] = { (uint16_t)(int16_t)w, 0u, 0u, 0x1234u };
    uint8_t sum = (uint8_t)(0x55u + WIT_T_ANGLE);
    f[0] = 0x55u;
    f[1] = WIT_T_ANGLE;
    for (unsigned i = 0; i < 4u; ++i) {
        f[2u + 2u * i] = (uint8_t)(word[i] & 0xFFu);
        f[3u + 2u * i] = (uint8_t)(word[i] >> 8);
        sum = (uint8_t)(sum + f[2u + 2u * i] + f[3u + 2u * i]);
    }
    f[10] = sum;
}

/** A 0x52 frame of rates 0 and battery @p centivolt / 100V. */
static void gyro_frame(uint8_t f[WIT_FRAME_LEN], uint16_t centivolt)
{
    uint8_t sum = (uint8_t)(0x55u + WIT_T_GYRO);
    memset(f, 0, WIT_FRAME_LEN);
    f[0] = 0x55u;
    f[1] = WIT_T_GYRO;
    f[8] = (uint8_t)(centivolt & 0xFFu);
    f[9] = (uint8_t)(centivolt >> 8);
    f[10] = (uint8_t)(sum + f[8] + f[9]);
}

/** One sample of a sensor that holds RSW 0x0C into @p s at @p t: a 0x52
 *  frame, then an angle frame; then a tick with @p up. */
static void frame_at(imu_sensor_t *s, uint32_t t, bool up)
{
    uint8_t f[WIT_FRAME_LEN];
    gyro_frame(f, 400u);
    imu_sensor_feed(s, f, sizeof f, t);
    angle_frame(f, 1.0f);
    imu_sensor_feed(s, f, sizeof f, t);
    imu_sensor_tick(s, t, up);
}

/** Whether @p c is FF AA @p reg @p lo 00. */
static bool is_cmd(const uint8_t c[WIT_CMD_LEN], uint8_t reg, uint8_t lo)
{
    return c[0] == 0xFFu && c[1] == 0xAAu && c[2] == reg && c[3] == lo
           && c[4] == 0u;
}

/** Whether @p c is the unlock FF AA 69 88 B5. */
static bool is_unlock(const uint8_t c[WIT_CMD_LEN])
{
    return c[0] == 0xFFu && c[1] == 0xAAu && c[2] == 0x69u && c[3] == 0x88u
           && c[4] == 0xB5u;
}

/* The first command goes at once; the next at 50ms after it, not at 49ms;
 * also with the clock wrapping between them. */
TEST_CASE(a_command_goes_50ms_after_the_one_before_and_not_at_49ms)
{
    static const uint32_t starts[] = { 1000u, 0xFFFFFFFFu - 20u };
    for (size_t k = 0; k < sizeof starts / sizeof starts[0]; ++k) {
        const uint32_t t = starts[k];
        imu_sensor_t s;
        imu_sensor_init(&s);
        frame_at(&s, t, true);
        CHECK(imu_sensor_calibrate(&s, true, t));     /* unlock, CALSW 1 */
        CHECK_EQ(s.out_n, 2u);
        uint8_t c[WIT_CMD_LEN];
        CHECK(imu_sensor_next(&s, t, c));
        CHECK(is_unlock(c));
        CHECK(!imu_sensor_next(&s, t + IMU_CMD_GAP_MS - 1u, c));
        CHECK_EQ(s.out_n, 1u);
        CHECK(imu_sensor_next(&s, t + IMU_CMD_GAP_MS, c));
        CHECK(is_cmd(c, WIT_REG_CALSW, WIT_CAL_ACC));
        CHECK(!imu_sensor_next(&s, t + 10u * IMU_CMD_GAP_MS, c)); /* empty */
    }
}

/* CALIBRATE runs 3000ms: at 2999ms 1ms is left, at 3000ms it ends DONE
 * with unlock, CALSW 0 and SAVE queued; started 100ms before the wrap. */
TEST_CASE(the_calibration_ends_at_3000ms_and_not_at_2999ms)
{
    static const uint32_t starts[] = { 5000u, 0xFFFFFFFFu - 99u };
    for (size_t k = 0; k < sizeof starts / sizeof starts[0]; ++k) {
        const uint32_t t0 = starts[k];
        imu_sensor_t s;
        imu_sensor_init(&s);
        frame_at(&s, t0, true);
        CHECK(imu_sensor_calibrate(&s, true, t0));
        CHECK(!imu_sensor_calibrate(&s, true, t0));   /* one at a time */
        CHECK(!imu_sensor_quiet(&s));
        s.out_n = 0u;                                 /* taken by the link */
        imu_view_t v;
        memset(&v, 0, sizeof v);
        imu_sensor_view(&s, &v);
        CHECK_EQ(v.cal, IMU_CAL_RUNNING);
        CHECK_EQ(v.cal_left_ms, IMU_CAL_MS);
        for (uint32_t ms = 20u; ms < IMU_CAL_MS; ms += 20u) {
            frame_at(&s, t0 + ms, true);
        }
        imu_sensor_tick(&s, t0 + IMU_CAL_MS - 1u, true);
        imu_sensor_view(&s, &v);
        CHECK_EQ(v.cal, IMU_CAL_RUNNING);
        CHECK_EQ(v.cal_left_ms, 1u);
        CHECK(!imu_sensor_quiet(&s));
        CHECK_EQ(s.out_n, 0u);
        imu_sensor_tick(&s, t0 + IMU_CAL_MS, true);
        imu_sensor_view(&s, &v);
        CHECK_EQ(v.cal, IMU_CAL_DONE);
        CHECK_EQ(v.cal_left_ms, 0u);
        CHECK_EQ(s.out_n, 3u);
        CHECK(is_unlock(s.out[(s.out_head + 0u) % IMU_OUTBOX]));
        CHECK(is_cmd(s.out[(s.out_head + 1u) % IMU_OUTBOX], WIT_REG_CALSW,
                     WIT_CAL_EXIT));
        CHECK(is_cmd(s.out[(s.out_head + 2u) % IMU_OUTBOX], WIT_REG_SAVE, 0u));
    }
}

/* CALIBRATE is refused without the stream or without a valid reading. A
 * reading that goes stale in it, or a frame that ends a stale stretch the
 * ticks missed, ends it FAILED. */
TEST_CASE(a_calibration_needs_the_stream_and_fails_on_a_stale_reading)
{
    imu_sensor_t s;
    imu_sensor_init(&s);
    CHECK(!imu_sensor_calibrate(&s, true, 100u));      /* no reading */
    frame_at(&s, 100u, true);
    CHECK(!imu_sensor_calibrate(&s, false, 100u));     /* no stream */
    CHECK(imu_sensor_calibrate(&s, true, 100u));
    imu_sensor_tick(&s, 100u + IMU_STALE_MS, true);    /* stale */
    frame_at(&s, 500u, true);
    imu_sensor_tick(&s, 100u + IMU_CAL_MS, true);
    CHECK_EQ(s.cal, IMU_CAL_FAILED);

    imu_sensor_init(&s);
    frame_at(&s, 100u, true);
    CHECK(imu_sensor_calibrate(&s, true, 100u));
    /* Frames 300ms apart, no tick between them: each ends a gap. */
    uint8_t f[WIT_FRAME_LEN];
    angle_frame(f, 0.0f);
    for (uint32_t t = 100u + IMU_STALE_MS; t < 100u + IMU_CAL_MS;
         t += IMU_STALE_MS) {
        imu_sensor_feed(&s, f, sizeof f, t);
    }
    CHECK_EQ(s.gaps, 9u);
    /* A frame 290ms on is no gap; the reading is valid at the end. */
    imu_sensor_feed(&s, f, sizeof f, 100u + IMU_CAL_MS - 10u);
    CHECK_EQ(s.gaps, 9u);
    imu_sensor_tick(&s, 100u + IMU_CAL_MS, true);
    CHECK(s.m.r.valid);
    CHECK_EQ(s.cal, IMU_CAL_FAILED);
}

/* A RATE and an ALGORITHM change go as unlock, RATE, AXIS6, SAVE once the
 * sensor is connected, and only once; the first values a controller takes
 * and the same values again send nothing. */
TEST_CASE(a_rate_change_goes_once_and_only_while_connected)
{
    imu_sensor_t s;
    imu_sensor_init(&s);
    imu_sensor_values(&s, WIT_RATE_10HZ, false, false);
    CHECK(!s.rate_due);
    CHECK(!s.algo_due);
    imu_sensor_values(&s, WIT_RATE_10HZ, false, true);
    CHECK(!s.rate_due);
    CHECK(!s.algo_due);
    imu_sensor_values(&s, WIT_RATE_200HZ, true, true);
    CHECK(s.rate_due);
    CHECK(s.algo_due);
    imu_sensor_tick(&s, 100u, true);                   /* no reading */
    CHECK_EQ(s.out_n, 0u);
    frame_at(&s, 120u, false);                         /* no stream */
    CHECK_EQ(s.out_n, 0u);
    frame_at(&s, 140u, true);
    CHECK_EQ(s.out_n, 4u);
    CHECK(!s.rate_due);
    CHECK(!s.algo_due);
    CHECK(is_unlock(s.out[0]));
    CHECK(is_cmd(s.out[1], WIT_REG_RATE, WIT_RATE_200HZ));
    CHECK(is_cmd(s.out[2], WIT_REG_AXIS6, 1u));
    CHECK(is_cmd(s.out[3], WIT_REG_SAVE, 0u));
    frame_at(&s, 160u, true);
    CHECK_EQ(s.out_n, 4u);
    /* ALGORITHM alone: unlock, AXIS6, SAVE. */
    s.out_n = 0u;
    s.out_head = 0u;
    imu_sensor_values(&s, WIT_RATE_200HZ, false, true);
    CHECK(!s.rate_due);
    frame_at(&s, 180u, true);
    CHECK_EQ(s.out_n, 3u);
    CHECK(is_cmd(s.out[1], WIT_REG_AXIS6, 0u));
}

/* The group goes whole: with 4 slots free for unlock, RATE, AXIS6 and SAVE
 * it is queued, with 3 it waits; a calibration holds it back. */
TEST_CASE(the_write_group_waits_for_room_and_for_the_calibration)
{
    imu_sensor_t s;
    imu_sensor_init(&s);
    imu_sensor_values(&s, WIT_RATE_10HZ, false, false);
    imu_sensor_values(&s, WIT_RATE_20HZ, true, true);
    s.out_n = IMU_OUTBOX - 3u;
    frame_at(&s, 100u, true);
    CHECK_EQ(s.out_n, IMU_OUTBOX - 3u);
    CHECK(s.rate_due);
    s.out_n = IMU_OUTBOX - 4u;
    frame_at(&s, 120u, true);
    CHECK_EQ(s.out_n, IMU_OUTBOX);
    CHECK(!s.rate_due);
    /* One more command past the room is dropped. */
    CHECK(imu_sensor_calibrate(&s, true, 120u));
    CHECK_EQ(s.out_n, IMU_OUTBOX);

    imu_sensor_init(&s);
    imu_sensor_values(&s, WIT_RATE_10HZ, false, false);
    frame_at(&s, 100u, true);
    CHECK(imu_sensor_calibrate(&s, true, 100u));
    s.out_n = 0u;
    imu_sensor_values(&s, WIT_RATE_50HZ, false, true);
    frame_at(&s, 120u, true);
    CHECK_EQ(s.out_n, 0u);
    CHECK(s.rate_due);
}

/* A frame 300ms after the one before counts a gap at the stale point; at
 * 299ms it does not; the first frame after IMU ON is none; across the
 * 32-bit wrap the same. */
TEST_CASE(a_frame_300ms_after_the_last_counts_a_gap_and_299ms_does_not)
{
    static const uint32_t starts[] = { 1000u, 0xFFFFFFFFu - 150u };
    for (size_t k = 0; k < sizeof starts / sizeof starts[0]; ++k) {
        const uint32_t t = starts[k];
        imu_sensor_t s;
        imu_sensor_init(&s);
        uint8_t f[WIT_FRAME_LEN];
        angle_frame(f, 2.0f);
        imu_sensor_feed(&s, f, sizeof f, t);
        CHECK_EQ(s.gaps, 0u);
        imu_sensor_feed(&s, f, sizeof f, t + IMU_STALE_MS - 1u);
        CHECK_EQ(s.gaps, 0u);
        const uint32_t t2 = t + IMU_STALE_MS - 1u + IMU_STALE_MS;
        imu_sensor_feed(&s, f, sizeof f, t2);
        CHECK_EQ(s.gaps, 1u);
        CHECK_EQ(s.gap_at_ms, t + IMU_STALE_MS - 1u + IMU_STALE_MS);
        /* Bytes in chunks of one: the frame counts once, at its end. */
        for (size_t i = 0; i < sizeof f; ++i) {
            imu_sensor_feed(&s, &f[i], 1u, t2 + 2u * IMU_STALE_MS);
        }
        CHECK_EQ(s.gaps, 2u);
        CHECK_EQ(s.m.r.taken_ms, t2 + 2u * IMU_STALE_MS);
        /* IMU OFF: the first frame after it is no gap. */
        imu_sensor_off(&s);
        imu_sensor_feed(&s, f, sizeof f, t2 + 9u * IMU_STALE_MS);
        CHECK_EQ(s.gaps, 2u);
    }
}

/* IMU OFF: a running calibration ends FAILED with its exit queued, a zero
 * kept is a new zero generation, the reading is gone; without a zero the
 * generation stays. */
TEST_CASE(imu_off_fails_the_calibration_and_counts_a_kept_zero)
{
    imu_sensor_t s;
    imu_sensor_init(&s);
    CHECK(!imu_sensor_zero(&s));                       /* no reading */
    CHECK_EQ(s.zero_gen, 0u);
    frame_at(&s, 100u, true);
    CHECK(imu_sensor_zero(&s));
    CHECK_EQ(s.zero_gen, 1u);
    CHECK(s.m.r.zeroed);
    CHECK(imu_sensor_calibrate(&s, true, 100u));
    s.out_n = 0u;
    imu_sensor_off(&s);
    CHECK_EQ(s.cal, IMU_CAL_FAILED);
    CHECK_EQ(s.out_n, 3u);
    CHECK(is_cmd(s.out[(s.out_head + 1u) % IMU_OUTBOX], WIT_REG_CALSW,
                 WIT_CAL_EXIT));
    CHECK_EQ(s.zero_gen, 2u);
    CHECK(!s.m.r.valid);
    CHECK(!s.m.r.zeroed);
    CHECK_EQ(imu_sensor_link(&s), IMU_LINK_BOUND);
    imu_sensor_off(&s);
    CHECK_EQ(s.zero_gen, 2u);
    imu_sensor_unzero(&s);
    CHECK_EQ(s.zero_gen, 3u);
}

/* Take every command the outbox lets go at @p t, 50ms apart from @p t on;
 * @return how many went, the last into @p last. */
static unsigned drain(imu_sensor_t *s, uint32_t t, uint8_t last[WIT_CMD_LEN])
{
    unsigned n = 0u;
    while (imu_sensor_next(s, t + n * IMU_CMD_GAP_MS, last)) {
        ++n;
    }
    return n;
}

/* The sensor refuses the calibration's unlock while the stream flows on:
 * CALSW 1 never goes, the calibration fails at once and does not end DONE
 * 3000ms later, and its exit goes. */
TEST_CASE(a_refused_calibration_start_fails_the_calibration)
{
    imu_sensor_t s;
    imu_sensor_init(&s);
    imu_sensor_values(&s, WIT_RATE_10HZ, true, false);
    frame_at(&s, 100u, true);
    CHECK(imu_sensor_calibrate(&s, true, 100u));
    uint8_t c[WIT_CMD_LEN];
    CHECK(imu_sensor_next(&s, 100u, c));
    CHECK(is_unlock(c));
    imu_sensor_flush(&s);                             /* the unlock refused */
    CHECK_EQ(s.cal, IMU_CAL_FAILED);
    for (uint32_t t = 200u; t <= 3400u; t += 100u) {
        frame_at(&s, t, true);
    }
    CHECK_EQ(s.cal, IMU_CAL_FAILED);
    CHECK(imu_sensor_next(&s, 3500u, c));
    CHECK(is_unlock(c));
    CHECK(imu_sensor_next(&s, 3550u, c));
    CHECK(is_cmd(c, WIT_REG_CALSW, 0u));
    CHECK(imu_sensor_next(&s, 3600u, c));
    CHECK(is_cmd(c, WIT_REG_SAVE, 0u));
    CHECK(!imu_sensor_next(&s, 3650u, c));
}

/* A write that fails drops its group, and what the group was for is due
 * again: the calibration's exit and a RATE, held while the stream flows on
 * (no retry on the same connection) and queued when it flows again.  The
 * exit waits for 3 free slots, not 2.  A dropped CALSW 1 fails the
 * calibration and queues its exit at once; it is not sent again. */
TEST_CASE(a_failed_write_brings_back_its_group_at_the_next_connection)
{
    imu_sensor_t s;
    imu_sensor_init(&s);
    imu_sensor_values(&s, WIT_RATE_10HZ, true, false);
    frame_at(&s, 100u, true);
    CHECK(imu_sensor_calibrate(&s, true, 100u));
    uint8_t c[WIT_CMD_LEN];
    CHECK_EQ(drain(&s, 100u, c), 2u);                 /* unlock, CALSW 1 */
    imu_sensor_flush(&s);                             /* CALSW 1 failed */
    CHECK_EQ(s.cal, IMU_CAL_FAILED);
    CHECK(!s.exit_due);
    CHECK(!s.held);
    CHECK_EQ(s.out_n, 3u);                            /* the exit queued */
    imu_sensor_off(&s);                               /* nothing more */
    CHECK_EQ(s.out_n, 3u);
    CHECK(imu_sensor_next(&s, 1000u, c));             /* its unlock goes */
    imu_sensor_values(&s, WIT_RATE_20HZ, true, true); /* RATE due */
    imu_sensor_flush(&s);                             /* the unlock failed */
    CHECK_EQ(s.out_n, 0u);
    CHECK(s.exit_due);
    CHECK(s.rate_due);
    CHECK(s.held);
    /* The stream flows on: nothing comes back on this connection. */
    frame_at(&s, 2000u, true);
    frame_at(&s, 2020u, true);
    CHECK_EQ(s.out_n, 0u);
    /* It stops, and flows again with 2 slots free: RATE waits for the
     * exit, which waits for room. */
    imu_sensor_tick(&s, 2040u, false);
    s.out_n = IMU_OUTBOX - 2u;
    frame_at(&s, 2060u, true);
    CHECK(!s.held);
    CHECK(s.exit_due);
    CHECK_EQ(s.out_n, IMU_OUTBOX - 2u);
    s.out_n = 0u;
    s.out_head = 0u;
    frame_at(&s, 2080u, true);
    CHECK(!s.exit_due);
    CHECK(!s.rate_due);
    CHECK_EQ(s.out_n, 6u);              /* exit, then unlock RATE SAVE */
    CHECK(imu_sensor_next(&s, 3000u, c));
    CHECK(is_unlock(c));
    CHECK(imu_sensor_next(&s, 3050u, c));
    CHECK(is_cmd(c, WIT_REG_CALSW, 0u));
    CHECK(imu_sensor_next(&s, 3100u, c));
    CHECK(is_cmd(c, WIT_REG_SAVE, 0u));
    CHECK(imu_sensor_next(&s, 3150u, c));
    CHECK(is_unlock(c));
    CHECK(imu_sensor_next(&s, 3200u, c));
    CHECK(is_cmd(c, WIT_REG_RATE, WIT_RATE_20HZ));
    CHECK(imu_sensor_next(&s, 3250u, c));
    CHECK(is_cmd(c, WIT_REG_SAVE, 0u));
    /* All of it taken: a failure now brings back the RATE group only. */
    imu_sensor_flush(&s);
    CHECK(!s.exit_due);
    CHECK(s.rate_due);
    CHECK(!s.algo_due);
}

/* BOUND before the first frame, CONNECTED with a valid reading, NO FRAMES
 * once it is stale, BOUND again after another sensor is bound; quiet only
 * with no calibration and nothing held. */
TEST_CASE(the_link_state_runs_bound_connected_no_frames)
{
    imu_sensor_t s;
    imu_sensor_init(&s);
    CHECK_EQ(imu_sensor_link(&s), IMU_LINK_BOUND);
    CHECK(imu_sensor_quiet(&s));
    frame_at(&s, 100u, true);
    CHECK_EQ(imu_sensor_link(&s), IMU_LINK_CONNECTED);
    imu_sensor_tick(&s, 100u + IMU_STALE_MS - 1u, true);
    CHECK_EQ(imu_sensor_link(&s), IMU_LINK_CONNECTED);
    imu_sensor_tick(&s, 100u + IMU_STALE_MS, true);
    CHECK_EQ(imu_sensor_link(&s), IMU_LINK_NO_FRAMES);
    imu_sensor_rebind(&s);
    CHECK_EQ(imu_sensor_link(&s), IMU_LINK_BOUND);
    s.out_n = 1u;
    CHECK(!imu_sensor_quiet(&s));
    imu_view_t v;
    memset(&v, 0, sizeof v);
    imu_sensor_view(&s, &v);
    CHECK_EQ(v.cal, IMU_CAL_NONE);
    CHECK_EQ(v.r.taken_ms, 100u);
    CHECK(fabsf(v.r.angle[0] - 1.0f) < 0.01f);
}

/** One frame of @p type with words 0 and a fourth word @p w3. */
static void typed_frame(uint8_t f[WIT_FRAME_LEN], uint8_t type, uint16_t w3)
{
    memset(f, 0, WIT_FRAME_LEN);
    f[0] = 0x55u;
    f[1] = type;
    f[8] = (uint8_t)(w3 & 0xFFu);
    f[9] = (uint8_t)(w3 >> 8);
    f[10] = (uint8_t)(0x55u + type + f[8] + f[9]);
}

/** One sample of a sensor at RSW @p rsw into @p s at @p t, in the
 *  sensor's order 0x51, 0x52, 0x53, 0x54, a tick with @p up after each
 *  frame.  @return the frames it fed. */
static unsigned sample_at(imu_sensor_t *s, uint32_t t, uint16_t rsw, bool up)
{
    static const uint8_t k_types[] = { WIT_T_ACC, WIT_T_GYRO, WIT_T_ANGLE,
                                       WIT_T_MAG };
    unsigned n = 0u;
    for (size_t i = 0; i < sizeof k_types; ++i) {
        if ((rsw & wit_rsw_bit(k_types[i])) == 0u) {
            continue;
        }
        uint8_t f[WIT_FRAME_LEN];
        if (k_types[i] == WIT_T_ANGLE) {
            angle_frame(f, 1.0f);
        } else {
            typed_frame(f, k_types[i], 400u);
        }
        imu_sensor_feed(s, f, sizeof f, t);
        imu_sensor_tick(s, t, up);
        ++n;
    }
    return n;
}

/* A connection whose first 8 frames show other types than 0x52 and 0x53
 * queues unlock, RSW 0x0C, SAVE: at the 8th frame and not at the 7th, once
 * on the connection, and again on the next one while the sensor sends
 * them.  A stream of 0x53 alone gets it too.  A sensor at 0x0C gets
 * nothing; a RATE due goes in the same group, after RSW. */
TEST_CASE(a_connection_at_another_rsw_queues_rsw_0x0c_and_save_once)
{
    imu_sensor_t s;
    imu_sensor_init(&s);
    imu_sensor_tick(&s, 0u, true);
    unsigned fed = 0u;
    uint32_t t = 100u;
    while (fed + 4u < IMU_RSW_FRAMES) {             /* 4 frames a sample */
        fed += sample_at(&s, t, WIT_RSW_DEFAULT, true);
        t += 20u;
    }
    CHECK_EQ(fed, 4u);
    uint8_t f[WIT_FRAME_LEN];
    typed_frame(f, WIT_T_ACC, 400u);
    imu_sensor_feed(&s, f, sizeof f, t);
    typed_frame(f, WIT_T_GYRO, 400u);
    imu_sensor_feed(&s, f, sizeof f, t);
    angle_frame(f, 1.0f);
    imu_sensor_feed(&s, f, sizeof f, t);
    imu_sensor_tick(&s, t, true);                   /* the 7th frame */
    CHECK_EQ(s.out_n, 0u);
    CHECK(!s.rsw_due);
    typed_frame(f, WIT_T_MAG, 0u);
    imu_sensor_feed(&s, f, sizeof f, t);
    imu_sensor_tick(&s, t, true);                   /* the 8th */
    CHECK_EQ(s.out_n, 3u);
    CHECK(!s.rsw_due);
    CHECK(is_unlock(s.out[0]));
    CHECK(is_cmd(s.out[1], WIT_REG_RSW, IMU_RSW));
    CHECK(is_cmd(s.out[2], WIT_REG_SAVE, 0u));
    CHECK_EQ(IMU_RSW, 0x0Cu);
    for (int i = 0; i < 20; ++i) {                  /* once a connection */
        t += 20u;
        (void)sample_at(&s, t, WIT_RSW_DEFAULT, true);
    }
    CHECK_EQ(s.out_n, 3u);
    /* The next connection: the sensor still sends 0x51 to 0x54. */
    s.out_n = 0u;
    s.out_head = 0u;
    imu_sensor_tick(&s, t + 1u, false);
    imu_sensor_tick(&s, t + 2u, true);
    for (int i = 0; i < 2; ++i) {
        t += 20u;
        (void)sample_at(&s, t, WIT_RSW_DEFAULT, true);
    }
    CHECK_EQ(s.out_n, 3u);
    CHECK(is_cmd(s.out[1], WIT_REG_RSW, IMU_RSW));

    /* 0x53 alone: no battery frame, RSW 0x0C goes. */
    imu_sensor_init(&s);
    imu_sensor_tick(&s, 0u, true);
    for (unsigned i = 0u; i < IMU_RSW_FRAMES; ++i) {
        (void)sample_at(&s, 100u + 20u * i, WIT_RSW_ANGLE, true);
    }
    CHECK_EQ(s.out_n, 3u);
    CHECK(is_cmd(s.out[1], WIT_REG_RSW, IMU_RSW));

    /* A sensor that holds 0x0C: no write, also over 40 frames. */
    imu_sensor_init(&s);
    imu_sensor_tick(&s, 0u, true);
    for (unsigned i = 0u; i < 20u; ++i) {
        (void)sample_at(&s, 100u + 20u * i, IMU_RSW, true);
    }
    CHECK(s.rsw_checked);
    CHECK(!s.rsw_due);
    CHECK_EQ(s.out_n, 0u);

    /* RSW and a RATE due: unlock, RSW, RATE, SAVE. */
    imu_sensor_init(&s);
    imu_sensor_values(&s, WIT_RATE_10HZ, false, false);
    imu_sensor_values(&s, WIT_RATE_200HZ, false, true);
    imu_sensor_tick(&s, 0u, true);
    s.out_n = IMU_OUTBOX - 1u;                      /* no room: it waits */
    for (unsigned i = 0u; i < 2u; ++i) {
        (void)sample_at(&s, 100u + 20u * i, WIT_RSW_DEFAULT, true);
    }
    CHECK(s.rsw_due);
    CHECK(s.rate_due);
    s.out_n = 0u;
    s.out_head = 0u;
    (void)sample_at(&s, 200u, WIT_RSW_DEFAULT, true);
    CHECK_EQ(s.out_n, 4u);
    CHECK(is_unlock(s.out[0]));
    CHECK(is_cmd(s.out[1], WIT_REG_RSW, IMU_RSW));
    CHECK(is_cmd(s.out[2], WIT_REG_RATE, WIT_RATE_200HZ));
    CHECK(is_cmd(s.out[3], WIT_REG_SAVE, 0u));
}

/* RSW's unlock refused: the group drops, RSW is due again and held while
 * the stream flows on; at the next connection unlock, RSW 0x0C, SAVE go
 * with the first valid reading.  A failure after all of it went brings
 * RSW back the same way. */
TEST_CASE(a_refused_rsw_write_is_due_again_at_the_next_connection)
{
    imu_sensor_t s;
    imu_sensor_init(&s);
    imu_sensor_tick(&s, 0u, true);
    (void)sample_at(&s, 100u, WIT_RSW_DEFAULT, true);
    (void)sample_at(&s, 120u, WIT_RSW_DEFAULT, true);
    CHECK_EQ(s.out_n, 3u);
    uint8_t c[WIT_CMD_LEN];
    CHECK(imu_sensor_next(&s, 200u, c));
    CHECK(is_unlock(c));
    imu_sensor_flush(&s);                           /* the unlock refused */
    CHECK_EQ(s.out_n, 0u);
    CHECK(s.rsw_due);
    CHECK(!s.rate_due);
    CHECK(s.held);
    for (uint32_t t = 300u; t < 1300u; t += 20u) {
        (void)sample_at(&s, t, WIT_RSW_DEFAULT, true);
    }
    CHECK_EQ(s.out_n, 0u);
    imu_sensor_tick(&s, 1300u, false);              /* the link drops */
    frame_at(&s, 1400u, true);                      /* back: one sample */
    CHECK(!s.held);
    CHECK(!s.rsw_due);
    CHECK_EQ(s.out_n, 3u);
    CHECK(imu_sensor_next(&s, 1500u, c));
    CHECK(is_unlock(c));
    CHECK(imu_sensor_next(&s, 1550u, c));
    CHECK(is_cmd(c, WIT_REG_RSW, IMU_RSW));
    CHECK(imu_sensor_next(&s, 1600u, c));
    CHECK(is_cmd(c, WIT_REG_SAVE, 0u));
    for (uint32_t t = 1620u; t < 2600u; t += 20u) {  /* not twice */
        (void)sample_at(&s, t, WIT_RSW_DEFAULT, true);
    }
    CHECK_EQ(s.out_n, 0u);
    imu_sensor_flush(&s);                           /* SAVE not answered */
    CHECK(s.rsw_due);
    CHECK(s.held);
    CHECK(!s.rate_due);
    CHECK(!s.algo_due);
}

int main(void)
{
    RUN(a_command_goes_50ms_after_the_one_before_and_not_at_49ms);
    RUN(the_calibration_ends_at_3000ms_and_not_at_2999ms);
    RUN(a_calibration_needs_the_stream_and_fails_on_a_stale_reading);
    RUN(a_rate_change_goes_once_and_only_while_connected);
    RUN(the_write_group_waits_for_room_and_for_the_calibration);
    RUN(a_frame_300ms_after_the_last_counts_a_gap_and_299ms_does_not);
    RUN(imu_off_fails_the_calibration_and_counts_a_kept_zero);
    RUN(a_refused_calibration_start_fails_the_calibration);
    RUN(a_failed_write_brings_back_its_group_at_the_next_connection);
    RUN(the_link_state_runs_bound_connected_no_frames);
    RUN(a_connection_at_another_rsw_queues_rsw_0x0c_and_save_once);
    RUN(a_refused_rsw_write_is_due_again_at_the_next_connection);
    return test_summary("imu_sensor");
}
