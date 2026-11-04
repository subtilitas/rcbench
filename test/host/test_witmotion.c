/*
 * WitMotion serial protocol.
 *
 * The failures this is about: a frame with a wrong sum taken for a reading,
 * a byte lost on the wire that costs the next good frame as well, and a
 * command whose bytes differ from the sensor's by one.  The tests build
 * frames and commands byte by byte from the layout in witmotion.h, not with
 * the module's own code.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "witmotion.h"

/** Build one frame: sync, type, four little-endian words, sum. */
static void make_frame(uint8_t f[WIT_FRAME_LEN], uint8_t type,
                       int16_t a, int16_t b, int16_t c, int16_t d)
{
    const int16_t w[4] = { a, b, c, d };
    uint8_t sum = 0x55u;
    f[0] = 0x55u;
    f[1] = type;
    sum = (uint8_t)(sum + type);
    for (unsigned i = 0; i < 4u; ++i) {
        f[2u + 2u * i] = (uint8_t)((uint16_t)w[i] & 0xFFu);
        f[3u + 2u * i] = (uint8_t)((uint16_t)w[i] >> 8);
        sum = (uint8_t)(sum + f[2u + 2u * i] + f[3u + 2u * i]);
    }
    f[10] = sum;
}

/** Feed @p n bytes; return the number of frames handed out, the last in @p out. */
static unsigned feed(wit_parser_t *p, const uint8_t *b, size_t n,
                     wit_frame_t *out)
{
    unsigned got = 0u;
    wit_frame_t f;
    for (size_t i = 0; i < n; ++i) {
        if (wit_parser_feed(p, b[i], &f)) {
            *out = f;
            ++got;
        }
    }
    return got;
}

TEST_CASE(a_frame_with_a_good_sum_decodes)
{
    uint8_t f[WIT_FRAME_LEN];
    wit_parser_t p;
    wit_frame_t out;
    wit_angle_t a;

    make_frame(f, WIT_T_ANGLE, 0x1234, -2, 0x4000, 0x0102);
    wit_parser_init(&p);
    CHECK_EQ(feed(&p, f, WIT_FRAME_LEN, &out), 1u);
    CHECK_EQ(out.type, WIT_T_ANGLE);
    CHECK_EQ(out.v[0], 0x1234);
    CHECK_EQ(out.v[1], -2);
    CHECK_EQ(out.v[2], 0x4000);
    CHECK_EQ(out.v[3], 0x0102);
    CHECK_EQ(p.frames, 1u);
    CHECK_EQ(p.bad_sum, 0u);
    CHECK_EQ(p.resyncs, 0u);
    CHECK_EQ(p.n, 0u);

    CHECK(wit_decode_angle(&out, &a));
    CHECK_NEAR(a.pitch, -2.0 / 32768.0 * 180.0, 1e-6);
    CHECK_NEAR(a.yaw, 90.0, 1e-4);
    CHECK_EQ(a.version, 0x0102u);
}

TEST_CASE(a_frame_with_a_bad_sum_is_dropped_and_counted)
{
    uint8_t f[WIT_FRAME_LEN];
    wit_parser_t p;
    wit_frame_t out;

    make_frame(f, WIT_T_ACC, 1, 2, 3, 4);
    f[10] = (uint8_t)(f[10] + 1u);
    wit_parser_init(&p);
    CHECK_EQ(feed(&p, f, WIT_FRAME_LEN, &out), 0u);
    CHECK_EQ(p.frames, 0u);
    CHECK_EQ(p.bad_sum, 1u);
    CHECK_EQ(p.resyncs, 10u);       /* the 10 bytes after the sync, no 0x55 in them */
    CHECK_EQ(p.n, 0u);

    /* The next frame is read at once. */
    make_frame(f, WIT_T_ACC, 5, 6, 7, 8);
    CHECK_EQ(feed(&p, f, WIT_FRAME_LEN, &out), 1u);
    CHECK_EQ(out.v[0], 5);
}

TEST_CASE(a_byte_before_the_sync_is_dropped_and_counted)
{
    uint8_t f[WIT_FRAME_LEN + 3];
    wit_parser_t p;
    wit_frame_t out;

    f[0] = 0x00u;
    f[1] = 0xFFu;
    f[2] = 0x12u;
    make_frame(&f[3], WIT_T_MAG, 10, 20, 30, 0);
    wit_parser_init(&p);
    CHECK_EQ(feed(&p, f, sizeof f, &out), 1u);
    CHECK_EQ(p.resyncs, 3u);
    CHECK_EQ(p.bad_sum, 0u);
    CHECK_EQ(out.type, WIT_T_MAG);
}

TEST_CASE(a_sync_byte_inside_a_frame_resyncs_without_losing_the_next_frame)
{
    uint8_t a[WIT_FRAME_LEN], b[WIT_FRAME_LEN];
    uint8_t s[2 * WIT_FRAME_LEN];
    wit_parser_t p;
    wit_frame_t out;

    make_frame(a, WIT_T_ACC, 100, 200, 300, 400);
    make_frame(b, WIT_T_GYRO, 11, 22, 33, 44);

    /* The first frame loses its last 4 bytes: the second one starts where
     * the parser still waits for the first. */
    for (unsigned i = 0; i < 7u; ++i) {
        s[i] = a[i];
    }
    for (unsigned i = 0; i < WIT_FRAME_LEN; ++i) {
        s[7u + i] = b[i];
    }
    wit_parser_init(&p);
    CHECK_EQ(feed(&p, s, 7u + WIT_FRAME_LEN, &out), 1u);
    CHECK_EQ(out.type, WIT_T_GYRO);
    CHECK_EQ(out.v[0], 11);
    CHECK_EQ(out.v[3], 44);
    CHECK_EQ(p.bad_sum, 1u);
    CHECK_EQ(p.resyncs, 6u);        /* a[1..6], before b's sync */
    CHECK_EQ(p.frames, 1u);
    CHECK_EQ(p.n, 0u);

    /* A sync byte as data of a good frame is no resync. */
    make_frame(a, WIT_T_ACC, 0x5555, 0x5555, 0x5555, 0x5555);
    CHECK_EQ(feed(&p, a, WIT_FRAME_LEN, &out), 1u);
    CHECK_EQ(out.v[2], 0x5555);
}

TEST_CASE(every_type_decodes_to_its_unit_at_the_ends_of_the_int16_range)
{
    uint8_t f[WIT_FRAME_LEN];
    wit_parser_t p;
    wit_frame_t out;
    wit_acc_t acc;
    wit_gyro_t gyro;
    wit_angle_t ang;
    wit_mag_t mag;

    wit_parser_init(&p);

    make_frame(f, WIT_T_ACC, 32767, -32768, 0, -2500);
    CHECK_EQ(feed(&p, f, WIT_FRAME_LEN, &out), 1u);
    CHECK(wit_decode_acc(&out, &acc));
    CHECK_NEAR(acc.ax, 15.9995 * 9.80665, 1e-3);
    CHECK_NEAR(acc.ay, -16.0 * 9.80665, 1e-4);
    CHECK_NEAR(acc.az, 0.0, 1e-9);
    CHECK_NEAR(acc.temp_c, -25.0, 1e-4);

    make_frame(f, WIT_T_GYRO, 32767, -32768, 16384, 420);
    CHECK_EQ(feed(&p, f, WIT_FRAME_LEN, &out), 1u);
    CHECK(wit_decode_gyro(&out, &gyro));
    CHECK_NEAR(gyro.wx, 1999.939, 1e-2);
    CHECK_NEAR(gyro.wy, -2000.0, 1e-4);
    CHECK_NEAR(gyro.wz, 1000.0, 1e-4);
    CHECK_NEAR(gyro.battery_v, 4.20, 1e-5);

    make_frame(f, WIT_T_ANGLE, 32767, -32768, 0, 0x0123);
    CHECK_EQ(feed(&p, f, WIT_FRAME_LEN, &out), 1u);
    CHECK(wit_decode_angle(&out, &ang));
    CHECK_NEAR(ang.roll, 179.995, 1e-3);
    CHECK_NEAR(ang.pitch, -180.0, 1e-6);
    CHECK_NEAR(ang.yaw, 0.0, 1e-9);
    CHECK_EQ(ang.version, 0x0123u);

    make_frame(f, WIT_T_MAG, 32767, -32768, 100, 0);
    CHECK_EQ(feed(&p, f, WIT_FRAME_LEN, &out), 1u);
    CHECK(wit_decode_mag(&out, &mag));
    CHECK_NEAR(mag.mx, 4915.05, 1e-2);
    CHECK_NEAR(mag.my, -4915.2, 1e-2);
    CHECK_NEAR(mag.mz, 15.0, 1e-4);
}

TEST_CASE(the_fourth_word_of_the_rate_frame_is_unsigned)
{
    wit_frame_t f = { .type = WIT_T_GYRO, .v = { 0, 0, 0, (int16_t)0xFFFF } };
    wit_gyro_t g;

    CHECK(wit_decode_gyro(&f, &g));
    CHECK_NEAR(g.battery_v, 655.35, 1e-2);
}

TEST_CASE(a_frame_of_another_type_is_refused_by_each_decoder)
{
    static const uint8_t types[] = { WIT_T_TIME, WIT_T_ACC, WIT_T_GYRO,
                                     WIT_T_ANGLE, WIT_T_MAG, WIT_T_QUAT,
                                     0x00, 0x5A, 0xFF };
    wit_acc_t acc;
    wit_gyro_t gyro;
    wit_angle_t ang;
    wit_mag_t mag;

    for (unsigned i = 0; i < sizeof types; ++i) {
        const wit_frame_t f = { .type = types[i], .v = { 1, 2, 3, 4 } };
        CHECK_EQ(wit_decode_acc(&f, &acc), types[i] == WIT_T_ACC);
        CHECK_EQ(wit_decode_gyro(&f, &gyro), types[i] == WIT_T_GYRO);
        CHECK_EQ(wit_decode_angle(&f, &ang), types[i] == WIT_T_ANGLE);
        CHECK_EQ(wit_decode_mag(&f, &mag), types[i] == WIT_T_MAG);
    }
}

/** Compare a command with its bytes. */
static void expect(size_t n, const uint8_t *got, uint8_t r, uint8_t lo,
                   uint8_t hi)
{
    CHECK_EQ(n, WIT_CMD_LEN);
    CHECK_EQ(got[0], 0xFF);
    CHECK_EQ(got[1], 0xAA);
    CHECK_EQ(got[2], r);
    CHECK_EQ(got[3], lo);
    CHECK_EQ(got[4], hi);
}

TEST_CASE(each_command_encodes_its_bytes)
{
    uint8_t c[WIT_CMD_LEN];

    expect(wit_cmd_unlock(c), c, 0x69, 0x88, 0xB5);
    expect(wit_cmd_save(c), c, 0x00, 0x00, 0x00);
    expect(wit_cmd_rate(c, WIT_RATE_200HZ), c, 0x03, 0x0B, 0x00);
    expect(wit_cmd_rate(c, WIT_RATE_0_2HZ), c, 0x03, 0x01, 0x00);
    expect(wit_cmd_rsw(c, WIT_RSW_ANGLE), c, 0x02, 0x08, 0x00);
    expect(wit_cmd_rsw(c, WIT_RSW_DEFAULT), c, 0x02, 0x1E, 0x00);
    expect(wit_cmd_rsw(c, WIT_RSW_QUAT | WIT_RSW_ANGLE), c, 0x02, 0x08, 0x02);
    expect(wit_cmd_calsw(c, 1), c, 0x01, 0x01, 0x00);
    expect(wit_cmd_calsw(c, WIT_CAL_EXIT), c, 0x01, 0x00, 0x00);
    expect(wit_cmd_calsw(c, WIT_CAL_MAG), c, 0x01, 0x02, 0x00);
    expect(wit_cmd_axis6(c, true), c, 0x24, 0x01, 0x00);
    expect(wit_cmd_axis6(c, false), c, 0x24, 0x00, 0x00);
    expect(wit_cmd(c, 0x7F, 0xABCD), c, 0x7F, 0xCD, 0xAB);
}

TEST_CASE(a_rate_code_outside_the_table_is_refused)
{
    uint8_t c[WIT_CMD_LEN] = { 1, 2, 3, 4, 5 };

    CHECK_EQ(wit_cmd_rate(c, 0x00), 0u);
    CHECK_EQ(wit_cmd_rate(c, 0x0C), 0u);
    CHECK_EQ(wit_cmd_rate(c, 0xFF), 0u);
    CHECK_EQ(c[0], 1);              /* a refusal writes nothing */
    CHECK_EQ(c[4], 5);
    CHECK_EQ(wit_cmd_calsw(c, 3), 0u);
    CHECK_EQ(wit_cmd_calsw(c, 0xFF), 0u);
    CHECK_EQ(c[0], 1);
    CHECK_EQ(wit_cmd_rate(c, 0x0B), WIT_CMD_LEN);
}

TEST_CASE(rate_dhz_names_every_code)
{
    static const uint16_t want[] = { 2, 5, 10, 20, 50, 100, 200, 500, 1000,
                                     1250, 2000 };

    CHECK_EQ(wit_rate_dhz(0x00), 0u);
    for (unsigned c = 1; c <= 0x0Bu; ++c) {
        CHECK_EQ(wit_rate_dhz((uint8_t)c), want[c - 1u]);
    }
    CHECK_EQ(wit_rate_dhz(0x0C), 0u);
    CHECK_EQ(wit_rate_dhz(0xFF), 0u);
    CHECK_EQ(wit_rate_dhz(WIT_RATE_50HZ), 500u);
}

/** One xorshift32 step: a fixed, portable noise source. */
static uint32_t next(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

TEST_CASE(two_thousand_frames_of_noise_never_hand_out_a_frame_with_a_bad_sum)
{
    enum { N = 2000 * WIT_FRAME_LEN };
    static uint8_t s[N];
    uint32_t seed = 0x1234ABCDu;
    wit_parser_t p;
    wit_frame_t out;
    static int16_t ref[N / WIT_FRAME_LEN][4];
    static uint8_t ref_type[N / WIT_FRAME_LEN];
    unsigned want = 0u, got = 0u;

    /* Noise with the sync byte over-represented, a good frame every 50th
     * 11 bytes, and a good frame with one byte flipped every 50th. */
    for (unsigned i = 0; i < N; ++i) {
        const uint32_t r = next(&seed);
        s[i] = (r & 7u) == 0u ? WIT_SYNC : (uint8_t)(r >> 8);
    }
    for (unsigned i = 0; i + WIT_FRAME_LEN <= N; i += 50u * WIT_FRAME_LEN) {
        make_frame(&s[i], WIT_T_ANGLE, (int16_t)i, 1, 2, 3);
        make_frame(&s[i + 25u * WIT_FRAME_LEN], WIT_T_ACC, 4, 5, 6, 7);
        s[i + 25u * WIT_FRAME_LEN + 4u] ^= 0x10u;
    }

    /* The reference: every 0x55 is tried as a start in order, a good sum
     * takes 11 bytes, anything else moves on one byte. */
    for (unsigned i = 0; i + WIT_FRAME_LEN <= N;) {
        uint8_t sum = 0u;
        for (unsigned k = 0; k < WIT_FRAME_LEN - 1u; ++k) {
            sum = (uint8_t)(sum + s[i + k]);
        }
        if (s[i] == WIT_SYNC && sum == s[i + 10u]) {
            ref_type[want] = s[i + 1u];
            for (unsigned k = 0; k < 4u; ++k) {
                ref[want][k] = (int16_t)(uint16_t)(s[i + 2u + 2u * k]
                                | (s[i + 3u + 2u * k] << 8));
            }
            ++want;
            i += WIT_FRAME_LEN;
        } else {
            ++i;
        }
    }

    wit_parser_init(&p);
    for (unsigned i = 0; i < N; ++i) {
        if (wit_parser_feed(&p, s[i], &out)) {
            CHECK(got < want);
            CHECK_EQ(out.type, ref_type[got]);
            CHECK_EQ(out.v[0], ref[got][0]);
            CHECK_EQ(out.v[1], ref[got][1]);
            CHECK_EQ(out.v[2], ref[got][2]);
            CHECK_EQ(out.v[3], ref[got][3]);
            ++got;
        }
        CHECK(p.n < WIT_FRAME_LEN);
    }
    CHECK_EQ(got, want);
    CHECK_EQ(p.frames, got);
    CHECK(got >= 40u);              /* the planted good frames at least */
    CHECK(p.bad_sum > 0u);
}

TEST_CASE(a_null_argument_does_nothing)
{
    uint8_t c[WIT_CMD_LEN] = { 9, 9, 9, 9, 9 };
    wit_parser_t p;
    wit_frame_t out = { .type = 7, .v = { 1, 2, 3, 4 } };
    wit_acc_t acc;
    wit_gyro_t gyro;
    wit_angle_t ang;
    wit_mag_t mag;

    wit_parser_init(NULL);
    wit_parser_init(&p);
    CHECK(!wit_parser_feed(NULL, 0x55, &out));
    CHECK(!wit_parser_feed(&p, 0x55, NULL));
    CHECK_EQ(p.n, 0u);              /* the refused byte was not taken */
    CHECK_EQ(p.resyncs, 0u);

    CHECK(!wit_decode_acc(NULL, &acc));
    CHECK(!wit_decode_acc(&out, NULL));
    CHECK(!wit_decode_gyro(NULL, &gyro));
    CHECK(!wit_decode_gyro(&out, NULL));
    CHECK(!wit_decode_angle(NULL, &ang));
    CHECK(!wit_decode_angle(&out, NULL));
    CHECK(!wit_decode_mag(NULL, &mag));
    CHECK(!wit_decode_mag(&out, NULL));

    CHECK_EQ(wit_cmd(NULL, 1, 2), 0u);
    CHECK_EQ(wit_cmd_unlock(NULL), 0u);
    CHECK_EQ(wit_cmd_save(NULL), 0u);
    CHECK_EQ(wit_cmd_rate(NULL, 3), 0u);
    CHECK_EQ(wit_cmd_rsw(NULL, 3), 0u);
    CHECK_EQ(wit_cmd_calsw(NULL, 1), 0u);
    CHECK_EQ(wit_cmd_axis6(NULL, true), 0u);
    CHECK_EQ(c[0], 9);
}

TEST_CASE(a_parser_state_past_its_buffer_is_reset_not_overrun)
{
    wit_parser_t p;
    wit_frame_t out;
    uint8_t f[WIT_FRAME_LEN];

    wit_parser_init(&p);
    p.n = 200u;
    make_frame(f, WIT_T_ACC, 1, 2, 3, 4);
    CHECK_EQ(feed(&p, f, WIT_FRAME_LEN, &out), 1u);
    CHECK_EQ(out.v[3], 4);
}

int main(void)
{
    RUN(a_frame_with_a_good_sum_decodes);
    RUN(a_frame_with_a_bad_sum_is_dropped_and_counted);
    RUN(a_byte_before_the_sync_is_dropped_and_counted);
    RUN(a_sync_byte_inside_a_frame_resyncs_without_losing_the_next_frame);
    RUN(every_type_decodes_to_its_unit_at_the_ends_of_the_int16_range);
    RUN(the_fourth_word_of_the_rate_frame_is_unsigned);
    RUN(a_frame_of_another_type_is_refused_by_each_decoder);
    RUN(each_command_encodes_its_bytes);
    RUN(a_rate_code_outside_the_table_is_refused);
    RUN(rate_dhz_names_every_code);
    RUN(two_thousand_frames_of_noise_never_hand_out_a_frame_with_a_bad_sum);
    RUN(a_null_argument_does_nothing);
    RUN(a_parser_state_past_its_buffer_is_reset_not_overrun);
    return test_summary("witmotion");
}
