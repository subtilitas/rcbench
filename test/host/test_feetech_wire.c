/*
 * Feetech frames and the status decoder (protocols/feetech/feetech_wire.c),
 * held to the published examples.
 *
 * The vectors are the frames of Feetech's protocol manual, its instruction
 * generator workbook and a third-party table, with their checksums
 * recomputed (research notes, feetech.md section 9), and one SCS frame
 * whose checksum was computed here.  Each frame is built by the module and
 * compared byte for byte; each status frame is decoded.
 *
 * Under test besides: the decoder's search for a frame in noise, a header
 * inside data, a checksum that does not add up, a reply cut short, a reply
 * from another ID, a parameter count other than asked, a stop bit low, and
 * the echo of the request a driver that hears itself hands back; the limits
 * of the builders: ID 254 and 255, 0 and 17 data bytes, a SYNC WRITE that
 * does not fit.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "feetech_wire.h"

#define N(a) (unsigned)(sizeof(a) / sizeof((a)[0]))

static bool same(const feetech_frame_t *f, const uint8_t *want, unsigned n)
{
    return f->n == n && memcmp(f->bytes, want, n) == 0;
}

/* ---------------------------------------------------------- the vectors */

TEST_CASE(ping_read_and_write_frames_match_the_manual)
{
    feetech_frame_t f;
    static const uint8_t ping[] = { 0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB };
    static const uint8_t read[] = { 0xFF, 0xFF, 0x01, 0x04, 0x02, 0x38, 0x02,
                                    0xBE };
    static const uint8_t bc_id[] = { 0xFF, 0xFF, 0xFE, 0x04, 0x03, 0x05, 0x01,
                                     0xF4 };
    static const uint8_t write6[] = { 0xFF, 0xFF, 0x01, 0x09, 0x03, 0x2A, 0x00,
                                      0x08, 0x00, 0x00, 0xE8, 0x03, 0xD5 };
    static const uint8_t data6[] = { 0x00, 0x08, 0x00, 0x00, 0xE8, 0x03 };
    const uint8_t one = 1;

    CHECK(feetech_ping(&f, 1));
    CHECK(same(&f, ping, N(ping)));
    CHECK(feetech_read(&f, 1, 0x38, 2));
    CHECK(same(&f, read, N(read)));
    CHECK(feetech_write(&f, FEETECH_ID_BROADCAST, 0x05, &one, 1));
    CHECK(same(&f, bc_id, N(bc_id)));
    /* The manual prints this frame without its third 00; the checksum D5
     * is the full frame's. */
    CHECK(feetech_write(&f, 1, 0x2A, data6, N(data6)));
    CHECK(same(&f, write6, N(write6)));
}

TEST_CASE(reg_write_action_sync_write_and_reset_match_the_manual)
{
    feetech_frame_t f;
    static const uint8_t data6[] = { 0x00, 0x08, 0x00, 0x00, 0xE8, 0x03 };
    static const uint8_t reg1[] = { 0xFF, 0xFF, 0x01, 0x09, 0x04, 0x2A, 0x00,
                                    0x08, 0x00, 0x00, 0xE8, 0x03, 0xD4 };
    static const uint8_t reg10[] = { 0xFF, 0xFF, 0x0A, 0x09, 0x04, 0x2A, 0x00,
                                     0x08, 0x00, 0x00, 0xE8, 0x03, 0xCB };
    static const uint8_t action[] = { 0xFF, 0xFF, 0xFE, 0x02, 0x05, 0xFA };
    static const uint8_t sync[] = {
        0xFF, 0xFF, 0xFE, 0x20, 0x83, 0x2A, 0x06,
        0x01, 0x00, 0x08, 0x00, 0x00, 0xE8, 0x03,
        0x02, 0x00, 0x08, 0x00, 0x00, 0xE8, 0x03,
        0x03, 0x00, 0x08, 0x00, 0x00, 0xE8, 0x03,
        0x04, 0x00, 0x08, 0x00, 0x00, 0xE8, 0x03,
        0x58,
    };
    static const uint8_t reset1[] = { 0xFF, 0xFF, 0x01, 0x02, 0x06, 0xF6 };
    static const uint8_t reset0[] = { 0xFF, 0xFF, 0x00, 0x02, 0x06, 0xF7 };
    uint8_t blocks[4 * 7];

    CHECK(feetech_reg_write(&f, 1, 0x2A, data6, N(data6)));
    CHECK(same(&f, reg1, N(reg1)));
    CHECK(feetech_reg_write(&f, 10, 0x2A, data6, N(data6)));
    CHECK(same(&f, reg10, N(reg10)));
    CHECK(feetech_action(&f, FEETECH_ID_BROADCAST));
    CHECK(same(&f, action, N(action)));
    for (unsigned i = 0; i < 4u; ++i) {
        blocks[i * 7u] = (uint8_t)(i + 1u);
        memcpy(&blocks[i * 7u + 1u], data6, 6);
    }
    CHECK(feetech_sync_write(&f, 0x2A, 6, blocks, 4));
    CHECK(same(&f, sync, N(sync)));
    /* LEN = (L + 1) N + 4. */
    CHECK_EQ(f.bytes[3], (6 + 1) * 4 + 4);
    /* The manual's table shows ID 0 with F7 and its frame ID 1 with F6:
     * both are right for their ID. */
    CHECK(feetech_reset(&f, 1));
    CHECK(same(&f, reset1, N(reset1)));
    CHECK(feetech_reset(&f, 0));
    CHECK(same(&f, reset0, N(reset0)));
}

TEST_CASE(the_sts_generator_frames_match_the_workbook)
{
    feetech_frame_t f;
    const uint8_t zero = 0;
    const uint8_t two = 2;
    const uint8_t cal = 0x80;
    const uint8_t mode1 = 1;
    static const uint8_t unlock[] = { 0xFF, 0xFF, 0x01, 0x04, 0x03, 0x37, 0x00,
                                      0xC0 };
    static const uint8_t set_id[] = { 0xFF, 0xFF, 0x01, 0x04, 0x03, 0x05, 0x02,
                                      0xF0 };
    static const uint8_t centre[] = { 0xFF, 0xFF, 0x02, 0x04, 0x03, 0x28, 0x80,
                                      0x4E };
    static const uint8_t goal[] = { 0xFF, 0xFF, 0x02, 0x09, 0x03, 0x2A, 0x00,
                                    0x08, 0x00, 0x00, 0xE8, 0x03, 0xD4 };
    static const uint8_t goal_d[] = { 0x00, 0x08, 0x00, 0x00, 0xE8, 0x03 };
    static const uint8_t acc[] = { 0xFF, 0xFF, 0x02, 0x0A, 0x03, 0x29, 0x32,
                                   0x00, 0x04, 0x00, 0x00, 0xD0, 0x07, 0xBA };
    static const uint8_t acc_d[] = { 0x32, 0x00, 0x04, 0x00, 0x00, 0xD0,
                                     0x07 };
    static const uint8_t mode[] = { 0xFF, 0xFF, 0x02, 0x04, 0x03, 0x21, 0x01,
                                    0xD4 };
    static const uint8_t speed[] = { 0xFF, 0xFF, 0x02, 0x05, 0x03, 0x2E, 0xE8,
                                     0x83, 0x5C };
    static const uint8_t speed_d[] = { 0xE8, 0x83 };
    static const uint8_t read2[] = { 0xFF, 0xFF, 0x02, 0x04, 0x02, 0x38, 0x02,
                                     0xBD };

    CHECK(feetech_write(&f, 1, 0x37, &zero, 1));
    CHECK(same(&f, unlock, N(unlock)));
    CHECK(feetech_write(&f, 1, 0x05, &two, 1));
    CHECK(same(&f, set_id, N(set_id)));
    CHECK(feetech_write(&f, 2, 0x28, &cal, 1));
    CHECK(same(&f, centre, N(centre)));
    CHECK(feetech_write(&f, 2, 0x2A, goal_d, N(goal_d)));
    CHECK(same(&f, goal, N(goal)));
    CHECK(feetech_write(&f, 2, 0x29, acc_d, N(acc_d)));
    CHECK(same(&f, acc, N(acc)));
    CHECK(feetech_write(&f, 2, 0x21, &mode1, 1));
    CHECK(same(&f, mode, N(mode)));
    CHECK(feetech_write(&f, 2, 0x2E, speed_d, N(speed_d)));
    CHECK(same(&f, speed, N(speed)));
    CHECK(feetech_read(&f, 2, 0x38, 2));
    CHECK(same(&f, read2, N(read2)));
}

TEST_CASE(instructions_outside_the_manual_build_with_the_generic_builder)
{
    feetech_frame_t f;
    static const uint8_t clear[] = { 0xFF, 0xFF, 0x01, 0x02, 0x0A, 0xF2 };
    static const uint8_t calib[] = { 0xFF, 0xFF, 0x01, 0x04, 0x0B, 0x00, 0x04,
                                     0xEB };
    static const uint8_t calib_p[] = { 0x00, 0x04 };
    static const uint8_t reboot[] = { 0xFF, 0xFF, 0x01, 0x02, 0x08, 0xF4 };
    static const uint8_t sync_read[] = { 0xFF, 0xFF, 0xFE, 0x06, 0x82, 0x38,
                                         0x08, 0x01, 0x02, 0x36 };
    static const uint8_t sync_read_p[] = { 0x38, 0x08, 0x01, 0x02 };

    CHECK(feetech_build(&f, 1, 0x0A, NULL, 0));
    CHECK(same(&f, clear, N(clear)));
    CHECK(feetech_build(&f, 1, 0x0B, calib_p, N(calib_p)));
    CHECK(same(&f, calib, N(calib)));
    CHECK(feetech_build(&f, 1, 0x08, NULL, 0));
    CHECK(same(&f, reboot, N(reboot)));
    CHECK(feetech_build(&f, FEETECH_ID_BROADCAST, 0x82, sync_read_p,
                        N(sync_read_p)));
    CHECK(same(&f, sync_read, N(sync_read)));
}

TEST_CASE(an_scs_goal_frame_puts_the_high_byte_first)
{
    feetech_frame_t f;
    /* Position 512, time 0, speed 1000, high byte first. */
    static const uint8_t d[] = { 0x02, 0x00, 0x00, 0x00, 0x03, 0xE8 };
    static const uint8_t want[] = { 0xFF, 0xFF, 0x01, 0x09, 0x03, 0x2A, 0x02,
                                    0x00, 0x00, 0x00, 0x03, 0xE8, 0xDB };

    CHECK(feetech_write(&f, 1, 0x2A, d, N(d)));
    CHECK(same(&f, want, N(want)));
}

TEST_CASE(the_two_status_frames_of_the_manual_decode)
{
    static const uint8_t ok[] = { 0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFC };
    static const uint8_t pos[] = { 0xFF, 0xFF, 0x01, 0x04, 0x00, 0x18, 0x05,
                                   0xDD };
    feetech_status_t st;

    CHECK_EQ(feetech_parse(ok, N(ok), 0, 1, 0, &st), FEETECH_RX_OK);
    CHECK_EQ(st.id, 1);
    CHECK_EQ(st.error, 0);
    CHECK_EQ(st.n_params, 0);
    CHECK_EQ(feetech_parse(pos, N(pos), 0, 1, 2, &st), FEETECH_RX_OK);
    CHECK_EQ(st.n_params, 2);
    CHECK_EQ(st.params[0], 0x18);
    CHECK_EQ(st.params[1], 0x05);
    CHECK_EQ(st.skipped, 0);
}

/* -------------------------------------------------------- the decoder */

TEST_CASE(a_reply_after_noise_and_a_false_header_is_found)
{
    /* Noise, a lone FF, then FF FF FF before the real header: the first FF
     * is no header and the search moves on one byte. */
    static const uint8_t in[] = { 0x13, 0x00, 0xFF, 0x22, 0xFF, 0xFF, 0xFF,
                                  0x01, 0x02, 0x00, 0xFC };
    feetech_status_t st;

    CHECK_EQ(feetech_parse(in, N(in), 0, 1, 0, &st), FEETECH_RX_OK);
    CHECK_EQ(st.skipped, 5);
}

TEST_CASE(a_header_inside_data_is_skipped_by_its_checksum)
{
    /* A reply of 2 data bytes FF FF: the bytes inside are no frame, the
     * frame around them is. */
    uint8_t in[8] = { 0xFF, 0xFF, 0x01, 0x04, 0x00, 0xFF, 0xFF, 0 };
    feetech_status_t st;

    in[7] = feetech_checksum(&in[2], 5);
    CHECK_EQ(feetech_parse(in, N(in), 0, 1, 2, &st), FEETECH_RX_OK);
    CHECK_EQ(st.params[0], 0xFF);
    CHECK_EQ(st.params[1], 0xFF);
}

TEST_CASE(a_wrong_checksum_is_refused_and_a_good_frame_after_it_found)
{
    static const uint8_t bad[] = { 0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFD };
    static const uint8_t bad_then_good[] = { 0xFF, 0xFF, 0x01, 0x02, 0x00,
                                             0xFD, 0xFF, 0xFF, 0x01, 0x02,
                                             0x00, 0xFC };
    feetech_status_t st;

    CHECK_EQ(feetech_parse(bad, N(bad), 0, 1, 0, &st), FEETECH_RX_CHECKSUM);
    CHECK_EQ(feetech_parse(bad_then_good, N(bad_then_good), 0, 1, 0, &st),
             FEETECH_RX_OK);
    CHECK_EQ(st.skipped, 6);
}

TEST_CASE(a_reply_cut_short_is_truncated_at_every_length)
{
    static const uint8_t pos[] = { 0xFF, 0xFF, 0x01, 0x04, 0x00, 0x18, 0x05,
                                   0xDD };
    feetech_status_t st;

    CHECK_EQ(feetech_parse(pos, 0, 0, 1, 2, &st), FEETECH_RX_NONE);
    CHECK_EQ(feetech_parse(pos, 1, 0, 1, 2, &st), FEETECH_RX_NOISE);
    for (unsigned n = 2; n < N(pos); ++n) {
        CHECK_EQ(feetech_parse(pos, n, 0, 1, 2, &st), FEETECH_RX_TRUNCATED);
    }
    CHECK_EQ(feetech_parse(pos, N(pos), 0, 1, 2, &st), FEETECH_RX_OK);
}

TEST_CASE(a_good_reply_from_another_id_is_wrong_id_and_kept)
{
    static const uint8_t from2[] = { 0xFF, 0xFF, 0x02, 0x02, 0x04, 0xF7 };
    feetech_status_t st;

    CHECK_EQ(feetech_parse(from2, N(from2), 0, 1, 0, &st),
             FEETECH_RX_WRONG_ID);
    CHECK_EQ(st.id, 2);
    CHECK_EQ(st.error, 4);
}

TEST_CASE(another_parameter_count_than_asked_is_length)
{
    static const uint8_t pos[] = { 0xFF, 0xFF, 0x01, 0x04, 0x00, 0x18, 0x05,
                                   0xDD };
    feetech_status_t st;

    CHECK_EQ(feetech_parse(pos, N(pos), 0, 1, 1, &st), FEETECH_RX_LENGTH);
    CHECK_EQ(feetech_parse(pos, N(pos), 0, 1, 3, &st), FEETECH_RX_LENGTH);
    /* Any count above FEETECH_DATA_MAX takes any. */
    CHECK_EQ(feetech_parse(pos, N(pos), 0, 1, 0xFF, &st), FEETECH_RX_OK);
}

TEST_CASE(a_length_field_out_of_range_is_no_frame)
{
    /* LEN 1 is shorter than an ERR and a checksum; LEN 19 is more than 16
     * data bytes. */
    uint8_t short_len[5] = { 0xFF, 0xFF, 0x01, 0x01, 0 };
    uint8_t long_len[24] = { 0xFF, 0xFF, 0x01, 19 };
    feetech_status_t st;

    short_len[4] = feetech_checksum(&short_len[2], 2);
    long_len[23] = feetech_checksum(&long_len[2], 20);
    CHECK_EQ(feetech_parse(short_len, N(short_len), 0, 1, 0, &st),
             FEETECH_RX_NOISE);
    CHECK_EQ(feetech_parse(long_len, N(long_len), 0, 1, 0xFF, &st),
             FEETECH_RX_NOISE);
}

TEST_CASE(a_stop_bit_low_without_a_good_frame_is_framing)
{
    static const uint8_t junk[] = { 0x00, 0x80, 0x00 };
    static const uint8_t ok[] = { 0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFC };
    feetech_status_t st;

    CHECK_EQ(feetech_parse(junk, N(junk), 1, 1, 0, &st), FEETECH_RX_FRAMING);
    /* A good frame stands whatever else came. */
    CHECK_EQ(feetech_parse(ok, N(ok), 1, 1, 0, &st), FEETECH_RX_OK);
}

TEST_CASE(the_echo_of_the_request_is_counted_off)
{
    feetech_frame_t f;
    uint8_t rx[16];
    static const uint8_t ok[] = { 0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFC };

    CHECK(feetech_ping(&f, 1));
    memcpy(rx, f.bytes, f.n);
    memcpy(&rx[f.n], ok, N(ok));
    CHECK_EQ(feetech_echo(rx, f.n + N(ok), f.bytes, f.n), f.n);
    /* No echo: nothing to count off. */
    CHECK_EQ(feetech_echo(ok, N(ok), f.bytes, f.n), 0u);
    CHECK_EQ(feetech_echo(rx, f.n - 1u, f.bytes, f.n), 0u);
    CHECK_EQ(feetech_echo(NULL, 4, f.bytes, f.n), 0u);
}

/* ---------------------------------------------------------- the limits */

TEST_CASE(the_builders_refuse_what_the_wire_cannot_carry)
{
    feetech_frame_t f;
    const uint8_t d[FEETECH_DATA_MAX + 1u] = { 0 };
    const uint8_t blocks[FEETECH_FRAME_MAX] = { 0 };

    CHECK(feetech_ping(&f, FEETECH_ID_MAX));
    CHECK(feetech_ping(&f, FEETECH_ID_BROADCAST));
    CHECK(!feetech_ping(&f, 0xFF));
    CHECK_EQ(f.n, 0);
    CHECK(!feetech_read(&f, 1, 0, 0));
    CHECK(feetech_read(&f, 1, 0, FEETECH_DATA_MAX));
    CHECK(!feetech_read(&f, 1, 0, FEETECH_DATA_MAX + 1u));
    CHECK(!feetech_write(&f, 1, 0, d, 0));
    CHECK(feetech_write(&f, 1, 0, d, FEETECH_DATA_MAX));
    CHECK_EQ(f.n, FEETECH_DATA_MAX + 1u + FEETECH_FRAME_OVERHEAD);
    CHECK(!feetech_write(&f, 1, 0, d, FEETECH_DATA_MAX + 1u));
    CHECK(!feetech_write(&f, 1, 0, NULL, 1));
    CHECK(!feetech_build(&f, 1, FEETECH_WRITE, NULL, 1));
    CHECK(!feetech_build(NULL, 1, FEETECH_PING, NULL, 0));
    CHECK(!feetech_build(&f, 1, FEETECH_WRITE, blocks,
                         FEETECH_PARAMS_MAX + 1u));
    CHECK(feetech_build(&f, 1, FEETECH_WRITE, blocks, FEETECH_PARAMS_MAX));
    CHECK_EQ(f.n, FEETECH_FRAME_MAX);
    /* 8 servos of 6 bytes: 58 parameters, 64 bytes; a ninth does not
     * fit. */
    CHECK(feetech_sync_write(&f, 0x2A, 6, blocks, 8));
    CHECK_EQ(f.n, FEETECH_FRAME_MAX);
    CHECK(!feetech_sync_write(&f, 0x2A, 6, blocks, 9));
    CHECK(!feetech_sync_write(&f, 0x2A, 0, blocks, 1));
    CHECK(!feetech_sync_write(&f, 0x2A, 6, blocks, 0));
    CHECK(!feetech_sync_write(&f, 0x2A, 6, NULL, 1));
    /* A count whose product wraps an unsigned: refused before it. */
    CHECK(!feetech_sync_write(&f, 0x2A, 2, blocks, 0x55555555u));
    CHECK(!feetech_sync_write(&f, 0x2A, 255, blocks, 0x01000000u));
    CHECK(!feetech_sync_write(&f, 0x2A, 1, blocks, 0x80000000u));
    CHECK(!feetech_sync_write(&f, 0x2A, 1, blocks, 0xFFFFFFFFu));
    CHECK_EQ(f.n, 0u);
}

TEST_CASE(wire_time_is_ten_bits_a_byte_rounded_up)
{
    CHECK_EQ(feetech_bytes_us(1000000u, 6), 60u);
    CHECK_EQ(feetech_bytes_us(38400u, 1), 261u);   /* 260.4 us */
    CHECK_EQ(feetech_bytes_us(115200u, 8), 695u);  /* 694.4 us */
    CHECK_EQ(feetech_bytes_us(0, 8), 0u);
}

int main(void)
{
    RUN(ping_read_and_write_frames_match_the_manual);
    RUN(reg_write_action_sync_write_and_reset_match_the_manual);
    RUN(the_sts_generator_frames_match_the_workbook);
    RUN(instructions_outside_the_manual_build_with_the_generic_builder);
    RUN(an_scs_goal_frame_puts_the_high_byte_first);
    RUN(the_two_status_frames_of_the_manual_decode);
    RUN(a_reply_after_noise_and_a_false_header_is_found);
    RUN(a_header_inside_data_is_skipped_by_its_checksum);
    RUN(a_wrong_checksum_is_refused_and_a_good_frame_after_it_found);
    RUN(a_reply_cut_short_is_truncated_at_every_length);
    RUN(a_good_reply_from_another_id_is_wrong_id_and_kept);
    RUN(another_parameter_count_than_asked_is_length);
    RUN(a_length_field_out_of_range_is_no_frame);
    RUN(a_stop_bit_low_without_a_good_frame_is_framing);
    RUN(the_echo_of_the_request_is_counted_off);
    RUN(the_builders_refuse_what_the_wire_cannot_carry);
    RUN(wire_time_is_ten_bits_a_byte_rounded_up);
    return test_summary("feetech_wire");
}
