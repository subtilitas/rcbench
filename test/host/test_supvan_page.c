/**
 * @file test_supvan_page.c
 * @brief protocols/supvan's pages: the 40 x 30mm label's 224 columns as 3
 *        print buffers of 84, 84 and 56 columns, byte for byte against
 *        test/host/fixtures/supvan/label-bufs.bin, with the page flags on
 *        the first and the last; 1, 84 and 85 columns; the checksum's
 *        256-byte block at its edge; the data frames against
 *        label-frames.bin, the frame count at 1, 500, 501 and 255 frames;
 *        the speed byte at each threshold.
 *
 * The fixtures are written by tools/gen_supvan_fixtures.py from the
 * report's rules (protocols/supvan/README.md, "Test fixtures"); station
 * step H0's captures replace them.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "supvan.h"

/** @brief The test label's columns: 30mm less 2 x 8 dots of margin. */
#define LABEL_COLS 224u

/**
 * @brief Read a fixture of test/host/fixtures/supvan.
 * @param name the file's name
 * @param n    set to its length
 * @return its bytes, malloc'd, or NULL
 */
static uint8_t *fixture(const char *name, size_t *n)
{
    char path[128];
    (void)snprintf(path, sizeof path, "fixtures/supvan/%s", name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    uint8_t *b = malloc(16384);
    *n = (b != NULL) ? fread(b, 1, 16384, f) : 0u;
    (void)fclose(f);
    return b;
}

/**
 * @brief The test label's bitmap, as gen_supvan_fixtures.py's
 *        label_bits() draws it.
 * @param bits LABEL_COLS * 48 bytes
 */
static void label_bits(uint8_t *bits)
{
    memset(bits, 0, LABEL_COLS * SV_LINE_BYTES);
    for (unsigned col = 0; col < LABEL_COLS; ++col) {
        for (unsigned row = 32; row < 352; ++row) {
            if ((col * 7u + row * 3u) % 23u < 8u) {
                bits[col * SV_LINE_BYTES + row / 8u] |=
                    (uint8_t)(1u << (row % 8u));
            }
        }
    }
}

/**
 * @brief The checksum the report gives for a buffer of @p cols columns.
 * @param b    the buffer
 * @param cols its columns
 * @return the 16-bit sum
 */
static uint16_t checksum(const uint8_t *b, unsigned cols)
{
    unsigned sum = 0u;
    for (unsigned i = 2u; i < 14u; ++i) {
        sum += b[i];
    }
    for (unsigned i = 255u; i < 14u + cols * 48u; i += 256u) {
        sum += b[i];
    }
    return (uint16_t)sum;
}

TEST_CASE(buffers_per_columns)
{
    CHECK_EQ(sv_buf_count(0u), 0);
    CHECK_EQ(sv_buf_count(1u), 1);
    CHECK_EQ(sv_buf_count(84u), 1);
    CHECK_EQ(sv_buf_count(85u), 2);
    CHECK_EQ(sv_buf_count(224u), 3);
    CHECK_EQ(sv_buf_count(65535u), 781);
}

TEST_CASE(the_label_is_three_buffers_equal_to_the_fixture)
{
    static uint8_t bits[LABEL_COLS * SV_LINE_BYTES];
    label_bits(bits);
    size_t n = 0u;
    uint8_t *want = fixture("label-bufs.bin", &n);
    REQUIRE_ALLOC(want);
    CHECK_EQ(n, 3u * SV_BUF_SIZE);
    static uint8_t buf[SV_BUF_SIZE];
    const unsigned cols[3] = { 84u, 84u, 56u };
    const uint8_t page[3] = { SV_PAGE_START, 0u,
                              SV_PAGE_END | SV_PRINT_END };
    for (uint16_t i = 0; i < 3u; ++i) {
        CHECK(sv_buf_fill(buf, bits, LABEL_COLS, i));
        CHECK(memcmp(buf, want + i * SV_BUF_SIZE, SV_BUF_SIZE) == 0);
        CHECK_EQ(buf[4] | (buf[5] << 8), cols[i]);
        CHECK_EQ(buf[2], page[i]);
        CHECK_EQ(buf[3], 0x50);
        CHECK_EQ(buf[6], 48);
        CHECK_EQ(buf[8], 8);
        CHECK_EQ(buf[10], 8);
        CHECK_EQ(buf[12], 4);
        CHECK_EQ(buf[0] | (buf[1] << 8), checksum(buf, cols[i]));
        CHECK(memcmp(buf + 14, bits + i * 84u * 48u, cols[i] * 48u) == 0);
    }
    CHECK(!sv_buf_fill(buf, bits, LABEL_COLS, 3u));
    free(want);
}

TEST_CASE(one_column_is_one_buffer_that_starts_and_ends_the_page)
{
    uint8_t bits[SV_LINE_BYTES];
    memset(bits, 0xA5, sizeof bits);
    static uint8_t buf[SV_BUF_SIZE];
    CHECK(sv_buf_fill(buf, bits, 1u, 0u));
    CHECK_EQ(buf[2], SV_PAGE_START | SV_PAGE_END | SV_PRINT_END);
    CHECK_EQ(buf[4], 1);
    CHECK_EQ(buf[5], 0);
    CHECK_EQ(buf[0] | (buf[1] << 8), 0x0E + 0x50 + 1 + 48 + 8 + 8 + 4);
    CHECK(memcmp(buf + 14, bits, sizeof bits) == 0);
    for (unsigned i = 14u + 48u; i < SV_BUF_SIZE; ++i) {
        if (buf[i] != 0u) {
            T_FAIL("byte %u is %u", i, buf[i]);
            break;
        }
    }
    CHECK(!sv_buf_fill(buf, bits, 1u, 1u));
}

TEST_CASE(eighty_four_columns_fill_one_buffer_and_85_spill_one)
{
    static uint8_t bits[85u * SV_LINE_BYTES];
    for (size_t i = 0; i < sizeof bits; ++i) {
        bits[i] = (uint8_t)(i * 13u + 1u);
    }
    static uint8_t buf[SV_BUF_SIZE];
    CHECK(sv_buf_fill(buf, bits, 84u, 0u));
    CHECK_EQ(buf[2], SV_PAGE_START | SV_PAGE_END | SV_PRINT_END);
    CHECK_EQ(buf[4], 84);
    CHECK_EQ(buf[0] | (buf[1] << 8), checksum(buf, 84u));
    CHECK_EQ(buf[14u + 84u * 48u - 1u], bits[84u * 48u - 1u]);
    CHECK_EQ(buf[14u + 84u * 48u], 0);
    CHECK(sv_buf_fill(buf, bits, 85u, 0u));
    CHECK_EQ(buf[2], SV_PAGE_START);
    CHECK_EQ(buf[4], 84);
    CHECK(sv_buf_fill(buf, bits, 85u, 1u));
    CHECK_EQ(buf[2], SV_PAGE_END | SV_PRINT_END);
    CHECK_EQ(buf[4], 1);
    CHECK(memcmp(buf + 14, bits + 84u * 48u, 48u) == 0);
    CHECK_EQ(buf[0] | (buf[1] << 8), checksum(buf, 1u));
}

TEST_CASE(the_checksum_reads_byte_255_once_the_data_reaches_it)
{
    /* 5 columns end at byte 254, 6 at byte 302: byte 255 is image byte
     * 241, column 5. */
    static uint8_t bits[6u * SV_LINE_BYTES];
    memset(bits, 0, sizeof bits);
    bits[241] = 0x77;
    static uint8_t buf[SV_BUF_SIZE];
    CHECK(sv_buf_fill(buf, bits, 5u, 0u));
    const unsigned five = (unsigned)(buf[0] | (buf[1] << 8));
    CHECK(sv_buf_fill(buf, bits, 6u, 0u));
    CHECK_EQ(buf[255], 0x77);
    CHECK_EQ((unsigned)(buf[0] | (buf[1] << 8)), five + 1u + 0x77u);
}

TEST_CASE(a_buffer_past_the_end_or_without_memory_is_refused)
{
    const uint8_t bits[SV_LINE_BYTES] = { 0 };
    static uint8_t buf[SV_BUF_SIZE];
    memset(buf, 0xEE, sizeof buf);
    CHECK(!sv_buf_fill(buf, bits, 0u, 0u));
    CHECK(!sv_buf_fill(buf, NULL, 1u, 0u));
    CHECK(!sv_buf_fill(NULL, bits, 1u, 0u));
    CHECK_EQ(buf[0], 0xEE);
    CHECK_EQ(buf[SV_BUF_SIZE - 1u], 0xEE);
}

TEST_CASE(frames_per_stream)
{
    CHECK_EQ(sv_chunk_count(0u), 0);
    CHECK_EQ(sv_chunk_count(1u), 1);
    CHECK_EQ(sv_chunk_count(500u), 1);
    CHECK_EQ(sv_chunk_count(501u), 2);
    CHECK_EQ(sv_chunk_count(255u * 500u), 255);
    CHECK_EQ(sv_chunk_count(255u * 500u + 1u), 0);
    CHECK_EQ(sv_chunk_count(0xFFFFFFFFu), 0);
}

TEST_CASE(the_label_stream_frames_equal_the_fixture)
{
    size_t zn = 0u;
    size_t fn = 0u;
    uint8_t *z = fixture("label.lz", &zn);
    uint8_t *want = fixture("label-frames.bin", &fn);
    REQUIRE_ALLOC(z);
    REQUIRE_ALLOC(want);
    const uint16_t count = sv_chunk_count((uint32_t)zn);
    CHECK_EQ(count * SV_FRAME_LEN, fn);
    uint8_t frame[SV_FRAME_LEN];
    for (uint16_t i = 0; i < count; ++i) {
        CHECK(sv_chunk_build(frame, z, (uint32_t)zn, (uint8_t)i));
        CHECK(memcmp(frame, want + i * SV_FRAME_LEN, SV_FRAME_LEN) == 0);
        CHECK_EQ(frame[10], i);
        CHECK_EQ(frame[11], count);
    }
    CHECK(!sv_chunk_build(frame, z, (uint32_t)zn, (uint8_t)count));
    free(z);
    free(want);
}

TEST_CASE(the_last_frame_is_padded_with_zeros)
{
    uint8_t z[501];
    memset(z, 0xFF, sizeof z);
    uint8_t frame[SV_FRAME_LEN];
    CHECK(sv_chunk_build(frame, z, sizeof z, 1u));
    static const uint8_t head[12] = { 0x7E, 0x5A, 0xFC, 0x01, 0x10, 0x02,
                                      0xAA, 0xBB, 0x02, 0x01, 0x01, 0x02 };
    /* checksum: index 1 + count 2 + one 0xFF = 0x0102 */
    CHECK(memcmp(frame, head, sizeof head) == 0);
    CHECK_EQ(frame[12], 0xFF);
    CHECK_EQ(frame[13], 0x00);
    CHECK_EQ(frame[SV_FRAME_LEN - 1u], 0x00);
    CHECK(sv_chunk_build(frame, z, sizeof z, 0u));
    CHECK_EQ(frame[SV_FRAME_LEN - 1u], 0xFF);
    /* 500 x 0xFF + 0 + 2 wraps the 16-bit sum: 127,502 & 0xFFFF */
    CHECK_EQ(frame[8] | (frame[9] << 8), (500 * 255 + 2) & 0xFFFF);
    CHECK(!sv_chunk_build(frame, NULL, sizeof z, 0u));
    CHECK(!sv_chunk_build(NULL, z, sizeof z, 0u));
    CHECK(!sv_chunk_build(frame, z, 0u, 0u));
}

TEST_CASE(the_speed_byte_at_each_threshold)
{
    const struct {
        uint32_t mean;
        uint8_t speed;
    } k[] = {
        { 100000u, 10u }, { 3001u, 10u }, { 3000u, 15u }, { 2801u, 15u },
        { 2800u, 20u },   { 2501u, 20u }, { 2500u, 25u }, { 2001u, 25u },
        { 2000u, 40u },   { 1501u, 40u }, { 1500u, 45u }, { 1001u, 45u },
        { 1000u, 55u },   { 501u, 55u },  { 500u, 60u },  { 0u, 60u },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        CHECK_EQ(sv_speed(k[i].mean), k[i].speed);
    }
}

int main(void)
{
    RUN(buffers_per_columns);
    RUN(the_label_is_three_buffers_equal_to_the_fixture);
    RUN(one_column_is_one_buffer_that_starts_and_ends_the_page);
    RUN(eighty_four_columns_fill_one_buffer_and_85_spill_one);
    RUN(the_checksum_reads_byte_255_once_the_data_reaches_it);
    RUN(a_buffer_past_the_end_or_without_memory_is_refused);
    RUN(frames_per_stream);
    RUN(the_label_stream_frames_equal_the_fixture);
    RUN(the_last_frame_is_padded_with_zeros);
    RUN(the_speed_byte_at_each_threshold);
    return test_summary("supvan_page");
}
