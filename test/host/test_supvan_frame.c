/**
 * @file test_supvan_frame.c
 * @brief protocols/supvan's frames: the 16-byte command against the
 *        station report's layout; the answer joiner with a 47-byte answer
 *        split 20 + 20 + 7 and fed one byte at a time, leading noise, a
 *        length out of range that resyncs, two answers in one piece and
 *        the report's 26-byte 0xF0 answer; the status bits one by one and
 *        the order of their errors; the material record's offsets, its
 *        shortest length and the roll limits at both ends.
 *
 * Bytes marked "report" are copied from the station's report of
 * 2026-11-25 (protocols/supvan/README.md, "Test fixtures").
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <string.h>

#include "greatest.h"
#include "supvan.h"

TEST_CASE(check_device_is_the_reports_command)
{
    /* report: 7E 5A 0C 00 10 01 AA CMD CHK_L CHK_H 00 01 P0 P1,
     * CHK = sum of bytes 10 to 15 */
    static const uint8_t want[SV_CMD_LEN] = { 0x7E, 0x5A, 0x0C, 0x00, 0x10,
                                              0x01, 0xAA, 0x12, 0x01, 0x00,
                                              0x00, 0x01, 0x00, 0x00, 0x00,
                                              0x00 };
    uint8_t c[SV_CMD_LEN];
    sv_cmd_build(c, SV_C_CHECK, 0u, 0u);
    CHECK(memcmp(c, want, sizeof want) == 0);
}

TEST_CASE(bulk_and_buf_full_carry_their_parameters_little_endian)
{
    uint8_t c[SV_CMD_LEN];
    sv_cmd_build(c, SV_C_BULK, 512u, 7u);
    static const uint8_t bulk[SV_CMD_LEN] = { 0x7E, 0x5A, 0x0C, 0x00, 0x10,
                                              0x01, 0xAA, 0x5C, 0x0A, 0x00,
                                              0x00, 0x01, 0x00, 0x02, 0x07,
                                              0x00 };
    CHECK(memcmp(c, bulk, sizeof bulk) == 0);
    sv_cmd_build(c, SV_C_BUF_FULL, 3285u, 45u);
    CHECK_EQ(c[12], 0xD5);
    CHECK_EQ(c[13], 0x0C);
    CHECK_EQ(c[14], 45);
    CHECK_EQ(c[8], 0x0F);
    CHECK_EQ(c[9], 0x01);
    sv_cmd_build(c, SV_C_STOP, 0xFFFFu, 0xFFFFu);
    CHECK_EQ(c[8] | (c[9] << 8), 1 + 4 * 255);
}

/**
 * @brief A material answer of 47 bytes: LEN 43, byte 4 0x55, the record
 *        from byte 22.
 * @param f the 47 bytes
 */
static void material(uint8_t f[47])
{
    memset(f, 0, 47);
    f[0] = 0x7E;
    f[1] = 0x5A;
    f[2] = 43;
    f[4] = 0x55;
    f[7] = SV_C_MATERIAL;
    for (unsigned i = 0; i < SV_UID_LEN; ++i) {
        f[22 + i] = (uint8_t)(0xE0u + i);
    }
    f[39] = 1;
    f[40] = 40;
    f[41] = 30;
    f[42] = 3;
    f[43] = 0x27; /* 295 = 0x127 */
    f[44] = 0x01;
}

/**
 * @brief Feed @p n bytes in pieces of @p piece and count whole frames.
 * @param rx    the joiner
 * @param in    the bytes
 * @param n     their count
 * @param piece bytes a call
 * @param lens  set to each frame's length, up to 4
 * @param cmds  set to each frame's command byte, up to 4
 * @return frames joined
 */
static unsigned feed(sv_rx_t *rx, const uint8_t *in, size_t n, size_t piece,
                     uint16_t lens[4], uint8_t cmds[4])
{
    unsigned frames = 0u;
    for (size_t o = 0u; o < n; o += piece) {
        const size_t k = (n - o < piece) ? n - o : piece;
        size_t used = 0u;
        do {
            const uint8_t *f = NULL;
            uint16_t len = 0u;
            used += sv_rx_feed(rx, in + o + used, k - used, &f, &len);
            if (f != NULL) {
                if (frames < 4u) {
                    lens[frames] = len;
                    cmds[frames] = f[SV_CMD_AT];
                }
                frames++;
            }
        } while (used < k);
    }
    /* a frame whose last byte came with the last piece */
    const uint8_t *f = NULL;
    uint16_t len = 0u;
    (void)sv_rx_feed(rx, NULL, 0u, &f, &len);
    if (f != NULL) {
        if (frames < 4u) {
            lens[frames] = len;
            cmds[frames] = f[SV_CMD_AT];
        }
        frames++;
    }
    return frames;
}

TEST_CASE(an_answer_split_20_20_7_and_byte_by_byte_joins_once)
{
    uint8_t f[47];
    material(f);
    sv_rx_t rx;
    uint16_t lens[4] = { 0 };
    uint8_t cmds[4] = { 0 };
    sv_rx_reset(&rx);
    CHECK_EQ(feed(&rx, f, 20u, 20u, lens, cmds), 0u);
    CHECK_EQ(feed(&rx, f + 20, 20u, 20u, lens, cmds), 0u);
    CHECK_EQ(feed(&rx, f + 40, 7u, 7u, lens, cmds), 1u);
    CHECK_EQ(lens[0], 47);
    CHECK_EQ(cmds[0], SV_C_MATERIAL);
    sv_rx_reset(&rx);
    CHECK_EQ(feed(&rx, f, sizeof f, 1u, lens, cmds), 1u);
    CHECK_EQ(lens[0], 47);
}

TEST_CASE(the_whole_frame_comes_out_as_it_went_in)
{
    uint8_t f[47];
    material(f);
    sv_rx_t rx;
    sv_rx_reset(&rx);
    const uint8_t *out = NULL;
    uint16_t len = 0u;
    CHECK_EQ(sv_rx_feed(&rx, f, sizeof f, &out, &len), sizeof f);
    REQUIRE_ALLOC(out);
    CHECK_EQ(len, 47);
    CHECK(memcmp(out, f, sizeof f) == 0);
    /* the next call drops it */
    CHECK_EQ(sv_rx_feed(&rx, NULL, 0u, &out, &len), 0u);
    CHECK(out == NULL);
    CHECK_EQ(len, 0);
    CHECK_EQ(rx.n, 0);
}

TEST_CASE(noise_before_a_frame_is_dropped)
{
    uint8_t in[60];
    static const uint8_t noise[] = { 0x00, 0x7E, 0x7E, 0x00, 0x5A, 0xFF,
                                     0x7E, 0x11, 0x42 };
    uint8_t f[47];
    material(f);
    memcpy(in, noise, sizeof noise);
    memcpy(in + sizeof noise, f, sizeof f);
    sv_rx_t rx;
    uint16_t lens[4] = { 0 };
    uint8_t cmds[4] = { 0 };
    for (size_t piece = 1u; piece <= 20u; piece += 19u) {
        sv_rx_reset(&rx);
        CHECK_EQ(feed(&rx, in, sizeof noise + sizeof f, piece, lens, cmds),
                 1u);
        CHECK_EQ(lens[0], 47);
    }
}

TEST_CASE(a_length_out_of_range_resyncs_on_the_next_frame)
{
    uint8_t f[47];
    material(f);
    /* 7E 5A with LEN 125 (129 bytes, one past SV_RX_MAX), then 7E 5A with
     * LEN 3 (7 bytes, one under SV_RX_MIN), then the frame. */
    uint8_t in[8 + 47];
    static const uint8_t bad[8] = { 0x7E, 0x5A, 125, 0x00,
                                    0x7E, 0x5A, 3, 0x00 };
    memcpy(in, bad, sizeof bad);
    memcpy(in + sizeof bad, f, sizeof f);
    sv_rx_t rx;
    uint16_t lens[4] = { 0 };
    uint8_t cmds[4] = { 0 };
    sv_rx_reset(&rx);
    CHECK_EQ(feed(&rx, in, sizeof in, 20u, lens, cmds), 1u);
    CHECK_EQ(lens[0], 47);
    CHECK_EQ(cmds[0], SV_C_MATERIAL);
}

TEST_CASE(the_limits_of_the_length_are_taken)
{
    uint8_t in[SV_RX_MAX];
    memset(in, 0, sizeof in);
    in[0] = 0x7E;
    in[1] = 0x5A;
    in[2] = (uint8_t)(SV_RX_MAX - SV_HEAD);
    sv_rx_t rx;
    uint16_t lens[4] = { 0 };
    uint8_t cmds[4] = { 0 };
    sv_rx_reset(&rx);
    CHECK_EQ(feed(&rx, in, SV_RX_MAX, 20u, lens, cmds), 1u);
    CHECK_EQ(lens[0], SV_RX_MAX);
    in[2] = (uint8_t)(SV_RX_MIN - SV_HEAD);
    sv_rx_reset(&rx);
    CHECK_EQ(feed(&rx, in, SV_RX_MIN, 3u, lens, cmds), 1u);
    CHECK_EQ(lens[0], SV_RX_MIN);
}

TEST_CASE(two_answers_in_one_piece_come_out_one_by_one)
{
    uint8_t in[16 + 47];
    sv_cmd_build(in, SV_C_START, 0u, 0u);
    material(in + 16);
    sv_rx_t rx;
    uint16_t lens[4] = { 0 };
    uint8_t cmds[4] = { 0 };
    sv_rx_reset(&rx);
    CHECK_EQ(feed(&rx, in, sizeof in, sizeof in, lens, cmds), 2u);
    CHECK_EQ(lens[0], 16);
    CHECK_EQ(cmds[0], SV_C_START);
    CHECK_EQ(lens[1], 47);
    CHECK_EQ(cmds[1], SV_C_MATERIAL);
}

TEST_CASE(the_reports_f0_answer_is_one_longer_frame)
{
    /* report, addendum 1: the answer to the reserved 0xF0 */
    static const uint8_t f0[26] = { 0x7e, 0x5a, 0x16, 0x00, 0x55, 0x03, 0x55,
                                    0xf0, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x04, 0x50, 0x80, 0x00, 0x00,
                                    0x00, 0x00, 0x14, 0x01, 0x0a };
    uint8_t in[27];
    memcpy(in, f0, sizeof f0);
    in[26] = 0x10;
    sv_rx_t rx;
    uint16_t lens[4] = { 0 };
    uint8_t cmds[4] = { 0 };
    sv_rx_reset(&rx);
    CHECK_EQ(feed(&rx, in, sizeof in, 20u, lens, cmds), 1u);
    CHECK_EQ(lens[0], 26);
    CHECK_EQ(cmds[0], 0xF0);
    sv_status_t st;
    CHECK(!sv_status_parse(in, 26u, &st));
}

/**
 * @brief An INQUIRY answer of 20 bytes with the 4 status bytes.
 * @param f  the 20 bytes
 * @param r  bytes 14 to 17
 */
static void status(uint8_t f[20], const uint8_t r[4])
{
    memset(f, 0, 20);
    f[0] = 0x7E;
    f[1] = 0x5A;
    f[2] = 16;
    f[7] = SV_C_INQUIRY;
    memcpy(f + 14, r, 4);
    f[18] = 0x26;
    f[19] = 0x01;
}

TEST_CASE(each_status_bit_lands_in_its_field)
{
    struct {
        uint8_t at, bit;
        size_t off;
    } const k[] = {
        { 0, 0x01, offsetof(sv_status_t, buf_full) },
        { 0, 0x02, offsetof(sv_status_t, label_rw) },
        { 0, 0x04, offsetof(sv_status_t, label_end) },
        { 0, 0x08, offsetof(sv_status_t, label_mode) },
        { 0, 0x40, offsetof(sv_status_t, low_battery) },
        { 1, 0x04, offsetof(sv_status_t, busy) },
        { 1, 0x08, offsetof(sv_status_t, head_hot) },
        { 2, 0x08, offsetof(sv_status_t, cover_open) },
        { 2, 0x10, offsetof(sv_status_t, usb_power) },
        { 2, 0x40, offsetof(sv_status_t, printing) },
        { 3, 0x01, offsetof(sv_status_t, no_label) },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        uint8_t r[4] = { 0, 0, 0, 0 };
        r[k[i].at] = k[i].bit;
        uint8_t f[20];
        status(f, r);
        sv_status_t st;
        memset(&st, 0, sizeof st);
        CHECK(sv_status_parse(f, sizeof f, &st));
        CHECK_EQ(st.count, 294);
        const bool *flags = (const bool *)(const void *)&st;
        size_t set = 0u;
        for (size_t b = 0; b < offsetof(sv_status_t, count); ++b) {
            set += flags[b] ? 1u : 0u;
        }
        CHECK_EQ(set, 1u);
        CHECK(*(const bool *)(const void *)((const uint8_t *)&st + k[i].off));
    }
    /* the bits no field names are not read */
    const uint8_t other[4] = { 0xB0, 0xF3, 0xA7, 0xFE };
    uint8_t f[20];
    status(f, other);
    sv_status_t st;
    CHECK(sv_status_parse(f, sizeof f, &st));
    CHECK_EQ(sv_status_error(&st), SV_OK);
    CHECK(!st.busy && !st.printing && !st.buf_full && !st.usb_power);
}

TEST_CASE(a_short_or_foreign_status_is_refused)
{
    const uint8_t r[4] = { 0, 0, 0, 0 };
    uint8_t f[20];
    status(f, r);
    sv_status_t st;
    CHECK(!sv_status_parse(f, 19u, &st));
    CHECK(!sv_status_parse(NULL, 20u, &st));
    f[7] = SV_C_MATERIAL;
    CHECK(!sv_status_parse(f, 20u, &st));
    f[7] = SV_C_INQUIRY;
    f[1] = 0x5B;
    CHECK(!sv_status_parse(f, 20u, &st));
    f[1] = 0x5A;
    f[0] = 0x7F;
    CHECK(!sv_status_parse(f, 20u, &st));
}

TEST_CASE(the_seven_error_flags_map_in_order)
{
    struct {
        uint8_t at, bit;
        sv_err_t err;
    } const k[] = {
        { 0, 0x02, SV_ERR_LABEL },    { 0, 0x04, SV_ERR_NO_LABEL },
        { 0, 0x08, SV_ERR_LABEL },    { 0, 0x40, SV_ERR_BATTERY },
        { 1, 0x08, SV_ERR_HOT },      { 2, 0x08, SV_ERR_COVER },
        { 3, 0x01, SV_ERR_NO_LABEL },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        uint8_t r[4] = { 0, 0, 0, 0 };
        r[k[i].at] = k[i].bit;
        uint8_t f[20];
        status(f, r);
        sv_status_t st;
        CHECK(sv_status_parse(f, sizeof f, &st));
        CHECK_EQ(sv_status_error(&st), k[i].err);
    }
    /* all at once: cover first, then no label, label, battery, head */
    uint8_t r[4] = { 0x4E, 0x08, 0x08, 0x01 };
    uint8_t f[20];
    sv_status_t st;
    const sv_err_t order[] = { SV_ERR_COVER, SV_ERR_NO_LABEL, SV_ERR_LABEL,
                               SV_ERR_BATTERY, SV_ERR_HOT };
    for (size_t i = 0; i < 5u; ++i) {
        status(f, r);
        CHECK(sv_status_parse(f, sizeof f, &st));
        CHECK_EQ(sv_status_error(&st), order[i]);
        if (i == 0u) {
            r[2] = 0u;
        } else if (i == 1u) {
            r[3] = 0u;
            r[0] = 0x4A;
        } else if (i == 2u) {
            r[0] = 0x40;
        } else {
            r[0] = 0u;
        }
    }
}

TEST_CASE(the_material_record_is_read_at_its_offsets)
{
    uint8_t f[47];
    material(f);
    sv_material_t m;
    CHECK(sv_material_parse(f, sizeof f, &m));
    CHECK_EQ(m.uid[0], 0xE0);
    CHECK_EQ(m.uid[6], 0xE6);
    CHECK_EQ(m.type, 1);
    CHECK_EQ(m.w_mm, 40);
    CHECK_EQ(m.h_mm, 30);
    CHECK_EQ(m.gap_mm, 3);
    CHECK_EQ(m.left, 295u);
    f[46] = 0x80;
    CHECK(sv_material_parse(f, sizeof f, &m));
    CHECK_EQ(m.left, 0x80000127u);
    CHECK(!sv_material_parse(f, 46u, &m));
    CHECK(!sv_material_parse(NULL, 47u, &m));
    f[7] = SV_C_INQUIRY;
    CHECK(!sv_material_parse(f, 47u, &m));
    f[7] = SV_C_MATERIAL;
    f[0] = 0;
    CHECK(!sv_material_parse(f, 47u, &m));
    f[0] = 0x7E;
    f[1] = 0;
    CHECK(!sv_material_parse(f, 47u, &m));
}

TEST_CASE(the_roll_limits_hold_at_both_ends)
{
    sv_material_t m;
    memset(&m, 0, sizeof m);
    CHECK_EQ(sv_roll_check(&m), SV_ERR_NO_LABEL);
    m.w_mm = 40;
    CHECK_EQ(sv_roll_check(&m), SV_ERR_NO_LABEL);
    m.w_mm = 0;
    m.h_mm = 30;
    CHECK_EQ(sv_roll_check(&m), SV_ERR_NO_LABEL);
    const struct {
        uint8_t w, h;
        sv_err_t err;
    } k[] = {
        { 15, 20, SV_OK },         { 48, 100, SV_OK },
        { 14, 30, SV_ERR_SMALL },  { 40, 19, SV_ERR_SMALL },
        { 49, 30, SV_ERR_LARGE },  { 40, 101, SV_ERR_LARGE },
        { 40, 30, SV_OK },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        m.w_mm = k[i].w;
        m.h_mm = k[i].h;
        CHECK_EQ(sv_roll_check(&m), k[i].err);
    }
}

int main(void)
{
    RUN(check_device_is_the_reports_command);
    RUN(bulk_and_buf_full_carry_their_parameters_little_endian);
    RUN(an_answer_split_20_20_7_and_byte_by_byte_joins_once);
    RUN(the_whole_frame_comes_out_as_it_went_in);
    RUN(noise_before_a_frame_is_dropped);
    RUN(a_length_out_of_range_resyncs_on_the_next_frame);
    RUN(the_limits_of_the_length_are_taken);
    RUN(two_answers_in_one_piece_come_out_one_by_one);
    RUN(the_reports_f0_answer_is_one_longer_frame);
    RUN(each_status_bit_lands_in_its_field);
    RUN(a_short_or_foreign_status_is_refused);
    RUN(the_seven_error_flags_map_in_order);
    RUN(the_material_record_is_read_at_its_offsets);
    RUN(the_roll_limits_hold_at_both_ends);
    return test_summary("supvan_frame");
}
