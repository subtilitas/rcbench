/**
 * @file
 * @brief The PNG encoder: chunk order, sums, limits and the sink's refusal.
 *        It writes pictures for check_png_enc.py, which decodes them with
 *        Python's zlib and holds every pixel to the expected value.
 *
 * Usage: test_png_enc [folder]; with a folder the pictures go there.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#define REQUIRE(c)                                                            \
    do {                                                                      \
        if (!(c)) {                                                           \
            T_FAIL("required %s", #c);                                        \
            return;                                                           \
        }                                                                     \
    } while (0)

#include "fm_crc.h"
#include "png_enc.h"

/** @brief The file as the sink took it. */
static uint8_t *g_file;
static size_t   g_len;
static size_t   g_cap;
/** @brief The sink refuses after this many bytes; SIZE_MAX never. */
static size_t   g_refuse = SIZE_MAX;
static png_enc_t g_enc;

/**
 * @brief The sink: keeps the bytes.
 * @param data  the bytes
 * @param n     how many
 * @param ctx   unused
 * @return false once past g_refuse
 */
static bool sink(const uint8_t *data, size_t n, void *ctx)
{
    (void)ctx;
    if (g_len + n > g_refuse) {
        return false;
    }
    if (g_len + n > g_cap) {
        g_cap = (g_len + n) * 2u;
        g_file = realloc(g_file, g_cap);
        REQUIRE_ALLOC(g_file);
    }
    memcpy(g_file + g_len, data, n);
    g_len += n;
    return true;
}

/**
 * @brief Pixel (x, y) of pattern @p kind; check_png_enc.py computes the same.
 * @param kind  0 flat, 1 gradient, 2 noise, 3 bars
 * @param x     column
 * @param y     row
 * @return RGB565
 */
static uint16_t pattern(int kind, int x, int y)
{
    switch (kind) {
    case 0:  return 0x1234u;
    case 1:  return (uint16_t)((x * 7 + y * 11) & 0xFFFF);
    case 2:  return (uint16_t)(((uint32_t)(x * 2654435761u)
                                ^ (uint32_t)(y * 40503u)) >> 13);
    default: return (uint16_t)((((x / 37) ^ (y / 19)) & 1) ? 0xF800u : 0x07E0u);
    }
}

/**
 * @brief Encode one picture into g_file.
 * @param kind  the pattern
 * @param w     its width
 * @param h     its height
 * @return true when every call said so
 */
static bool encode(int kind, int w, int h)
{
    static uint16_t row[PNG_ENC_MAX_W];
    g_len = 0u;
    if (!png_enc_start(&g_enc, w, h, sink, NULL)) {
        return false;
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            row[x] = pattern(kind, x, y);
        }
        if (!png_enc_row(&g_enc, row)) {
            return false;
        }
    }
    return png_enc_end(&g_enc);
}

/**
 * @brief A 32-bit big-endian value.
 * @param p  the bytes
 * @return the value
 */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
           | ((uint32_t)p[2] << 8) | p[3];
}

TEST_CASE(chunks_come_in_order_with_right_sums)
{
    REQUIRE(encode(2, 64, 40));
    static const uint8_t k_sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A,
                                      0x0A };
    CHECK(memcmp(g_file, k_sig, 8) == 0);
    size_t at = 8u;
    int seen = 0;
    bool idat = false;
    bool iend = false;
    while (at + 12u <= g_len) {
        const uint32_t n = be32(g_file + at);
        const char *type = (const char *)g_file + at + 4;
        REQUIRE(at + 12u + n <= g_len);
        CHECK_EQ(be32(g_file + at + 8u + n),
                 fm_crc32(0u, g_file + at + 4u, 4u + n));
        if (seen == 0) {
            CHECK(memcmp(type, "IHDR", 4) == 0);
            CHECK_EQ(n, 13);
            CHECK_EQ(be32(g_file + at + 8), 64);
            CHECK_EQ(be32(g_file + at + 12), 40);
            CHECK_EQ(g_file[at + 16], 8);
            CHECK_EQ(g_file[at + 17], 2);
        } else if (memcmp(type, "IDAT", 4) == 0) {
            CHECK(!iend);
            idat = true;
        } else {
            CHECK(memcmp(type, "IEND", 4) == 0);
            CHECK(idat);
            CHECK_EQ(n, 0);
            iend = true;
        }
        ++seen;
        at += 12u + n;
    }
    CHECK(iend);
    CHECK_EQ(at, g_len);
}

TEST_CASE(the_state_and_a_flat_panel_stay_small)
{
    CHECK(sizeof(png_enc_t) <= 37000u);
    REQUIRE(encode(0, 1024, 600));
    /* 1,844,400 stream bytes in matches of 258 at most. */
    CHECK(g_len < 20000u);
}

TEST_CASE(limits_are_refused)
{
    static uint16_t row[4];
    CHECK(!png_enc_start(&g_enc, 0, 1, sink, NULL));
    CHECK(!png_enc_start(&g_enc, PNG_ENC_MAX_W + 1, 1, sink, NULL));
    CHECK(!png_enc_start(&g_enc, 1, 0, sink, NULL));
    CHECK(!png_enc_start(&g_enc, 1, 1, NULL, NULL));
    REQUIRE(png_enc_start(&g_enc, 4, 2, sink, NULL));
    CHECK(!png_enc_end(&g_enc));                /* rows missing */
    REQUIRE(png_enc_row(&g_enc, row));
    REQUIRE(png_enc_row(&g_enc, row));
    CHECK(!png_enc_row(&g_enc, row));           /* a row past the last */
    CHECK(png_enc_end(&g_enc));
}

TEST_CASE(a_refused_write_answers_false_from_then_on)
{
    g_refuse = 100u;
    CHECK(!encode(2, 512, 300));
    CHECK(!g_enc.ok);
    g_refuse = SIZE_MAX;
}

TEST_CASE(a_row_of_one_pixel_and_a_single_row_encode)
{
    REQUIRE(encode(1, 1, 3));
    REQUIRE(encode(1, 5, 1));
}

/**
 * @brief Write picture @p kind to <folder>/<name>.png.
 * @param dir   the folder
 * @param kind  the pattern
 * @param w     its width
 * @param h     its height
 * @return true when written
 */
static bool dump(const char *dir, int kind, int w, int h)
{
    char path[512];
    (void)snprintf(path, sizeof(path), "%s/p%d_%d_%d.png", dir, kind, w, h);
    if (!encode(kind, w, h)) {
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    const bool ok = fwrite(g_file, 1u, g_len, f) == g_len;
    return (fclose(f) == 0) && ok;
}

int main(int argc, char **argv)
{
    RUN(chunks_come_in_order_with_right_sums);
    RUN(the_state_and_a_flat_panel_stay_small);
    RUN(limits_are_refused);
    RUN(a_refused_write_answers_false_from_then_on);
    RUN(a_row_of_one_pixel_and_a_single_row_encode);
    if (argc > 1) {
        for (int kind = 0; kind < 4; ++kind) {
            if (!dump(argv[1], kind, 1024, 600) || !dump(argv[1], kind, 1, 1)
                || !dump(argv[1], kind, 3, 7) || !dump(argv[1], kind, 300, 2)) {
                fprintf(stderr, "cannot write into %s\n", argv[1]);
                return 2;
            }
        }
    }
    free(g_file);
    return test_summary("png_enc");
}
