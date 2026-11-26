/**
 * @file
 * @brief The label's 1-bit canvas: roll limits, the mapping of reading
 *        coordinates to the printer's column-major LSB-first bytes, the
 *        clip to the label and text in 1-bit and 8-bit faces.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "label_bitmap.h"

/** @brief A bitmap of the longest label. */
static uint8_t s_bits[LABEL_BMP_MAX];

/**
 * @brief Set up @p b for a roll of @p w_mm x @p h_mm over s_bits.
 * @param b     the bitmap
 * @param w_mm  width
 * @param h_mm  height
 * @return label_bmp_init()'s answer
 */
static label_err_t bmp_for(label_bmp_t *b, uint8_t w_mm, uint8_t h_mm)
{
    const label_roll_t roll = { w_mm, h_mm };
    memset(b, 0, sizeof *b);
    b->bits = s_bits;
    b->cap  = sizeof s_bits;
    return label_bmp_init(b, &roll);
}

/**
 * @brief Set bits in the first @p n bytes of s_bits.
 * @param n  bytes
 * @return the count of set bits
 */
static long bits_set(size_t n)
{
    long c = 0;
    for (size_t i = 0u; i < n; i++) {
        for (unsigned v = s_bits[i]; v != 0u; v &= v - 1u) {
            c++;
        }
    }
    return c;
}

/**
 * @brief Whether head row @p row of column @p col is set, as the printer
 *        reads it.
 * @param col  column
 * @param row  head row, 0..383
 * @return the bit
 */
static bool head_dot(int col, int row)
{
    return (s_bits[(size_t)col * 48u + (size_t)row / 8u] >> (row % 8)) & 1u;
}

TEST_CASE(the_corners_of_a_40x30_label_land_on_the_station_rows)
{
    label_bmp_t b;
    CHECK_EQ(bmp_for(&b, 40u, 30u), LABEL_OK);
    CHECK_EQ(b.w, 320);
    CHECK_EQ(b.cols, 224);
    CHECK_EQ(b.off, 32);
    label_px(&b, 0, 0);
    /* (0, 0): column 0, row 351: byte 43, bit 7. */
    CHECK_EQ(s_bits[43], 0x80);
    CHECK(head_dot(0, 351));
    label_px(&b, 319, 223);
    /* (W - 1, H - 1): column 223, row 32: byte 223 * 48 + 4, bit 0. */
    CHECK_EQ(s_bits[223u * 48u + 4u], 0x01);
    CHECK(head_dot(223, 32));
    CHECK_EQ(bits_set(LABEL_BMP_MAX), 2);
    /* x walks down the head rows, y along the columns. */
    label_px(&b, 1, 0);
    CHECK(head_dot(0, 350));
    label_px(&b, 0, 1);
    CHECK(head_dot(1, 351));
    CHECK(label_px_at(&b, 0, 0));
    CHECK(label_px_at(&b, 319, 223));
    CHECK(!label_px_at(&b, 2, 0));
}

TEST_CASE(a_roll_is_held_to_15_to_48mm_by_20_to_100mm)
{
    uint16_t w = 0u;
    uint16_t cols = 0u;
    label_roll_t r = { 14u, 30u };
    CHECK_EQ(label_roll_check(&r, &w, &cols), LABEL_TOO_SMALL);
    r = (label_roll_t){ 15u, 30u };
    CHECK_EQ(label_roll_check(&r, &w, &cols), LABEL_OK);
    CHECK_EQ(w, 120);
    CHECK_EQ(cols, 224);
    r = (label_roll_t){ 48u, 30u };
    CHECK_EQ(label_roll_check(&r, &w, &cols), LABEL_OK);
    CHECK_EQ(w, 384);
    r = (label_roll_t){ 49u, 30u };
    CHECK_EQ(label_roll_check(&r, NULL, NULL), LABEL_TOO_LARGE);
    r = (label_roll_t){ 40u, 19u };
    CHECK_EQ(label_roll_check(&r, NULL, NULL), LABEL_TOO_SMALL);
    r = (label_roll_t){ 40u, 20u };
    CHECK_EQ(label_roll_check(&r, &w, &cols), LABEL_OK);
    CHECK_EQ(cols, 144);
    r = (label_roll_t){ 40u, 100u };
    CHECK_EQ(label_roll_check(&r, &w, &cols), LABEL_OK);
    CHECK_EQ(cols, 784);
    r = (label_roll_t){ 40u, 101u };
    CHECK_EQ(label_roll_check(&r, NULL, NULL), LABEL_TOO_LARGE);
    r = (label_roll_t){ 0u, 30u };
    CHECK_EQ(label_roll_check(&r, NULL, NULL), LABEL_NO_LABEL);
    r = (label_roll_t){ 40u, 0u };
    CHECK_EQ(label_roll_check(&r, NULL, NULL), LABEL_NO_LABEL);
    CHECK_EQ(label_roll_check(NULL, NULL, NULL), LABEL_NO_LABEL);
}

TEST_CASE(the_bitmap_takes_48_bytes_a_column_and_refuses_a_short_buffer)
{
    const label_roll_t r4030 = { 40u, 30u };
    const label_roll_t r48100 = { 48u, 100u };
    const label_roll_t r14 = { 14u, 30u };
    CHECK_EQ(label_bmp_bytes(&r4030), 10752);
    CHECK_EQ(label_bmp_bytes(&r48100), 37632);
    CHECK_EQ(LABEL_BMP_MAX, 37632);
    CHECK_EQ(label_bmp_bytes(&r14), 0);

    memset(s_bits, 0xFF, sizeof s_bits);
    label_bmp_t b = { .bits = s_bits, .cap = 10751u };
    CHECK_EQ(label_bmp_init(&b, &r4030), LABEL_MEMORY);
    b.bits = NULL;
    b.cap = sizeof s_bits;
    CHECK_EQ(label_bmp_init(&b, &r4030), LABEL_MEMORY);
    b.bits = s_bits;
    b.cap = 10752u;
    CHECK_EQ(label_bmp_init(&b, &r4030), LABEL_OK);
    CHECK_EQ(bits_set(10752u), 0);
    /* Past the label the buffer is the caller's and stays as it was. */
    CHECK_EQ(s_bits[10752], 0xFF);
    CHECK_EQ(label_bmp_init(&b, &r14), LABEL_TOO_SMALL);
}

TEST_CASE(a_label_is_centered_on_the_head)
{
    label_bmp_t b;
    CHECK_EQ(bmp_for(&b, 48u, 30u), LABEL_OK);
    CHECK_EQ(b.off, 0);
    CHECK_EQ(bmp_for(&b, 15u, 30u), LABEL_OK);
    CHECK_EQ(b.off, 132);
    CHECK_EQ(bmp_for(&b, 25u, 100u), LABEL_OK);
    CHECK_EQ(b.off, 92);
    CHECK_EQ(b.cols, 784);
}

TEST_CASE(a_fill_of_the_whole_label_sets_its_rows_and_nothing_else)
{
    const uint8_t widths[] = { 15u, 25u, 40u, 48u };
    for (size_t k = 0u; k < sizeof widths; k++) {
        label_bmp_t b;
        CHECK_EQ(bmp_for(&b, widths[k], 20u), LABEL_OK);
        label_fill(&b, -50, -50, 1000, 2000);
        for (int col = 0; col < (int)b.cols; col++) {
            for (int row = 0; row < 384; row++) {
                const bool in = row >= b.off && row < b.off + b.w;
                if (head_dot(col, row) != in) {
                    T_FAIL("%u mm: column %d row %d", widths[k], col, row);
                    return;
                }
            }
        }
        CHECK_EQ(bits_set((size_t)b.cols * 48u), (long)b.w * b.cols);
    }
}

TEST_CASE(a_dot_off_the_label_is_dropped)
{
    label_bmp_t b;
    CHECK_EQ(bmp_for(&b, 40u, 30u), LABEL_OK);
    label_px(&b, -1, 0);
    label_px(&b, 0, -1);
    label_px(&b, 320, 0);
    label_px(&b, 0, 224);
    label_fill(&b, 320, 0, 10, 10);
    label_fill(&b, 0, 224, 10, 10);
    label_fill(&b, 5, 5, 0, 10);
    label_fill(&b, 5, 5, 10, -3);
    CHECK_EQ(bits_set(LABEL_BMP_MAX), 0);
    CHECK(!label_px_at(&b, -1, 0));
    CHECK(!label_px_at(&b, 320, 0));
    CHECK(!label_px_at(&b, 0, 224));
    label_fill(&b, 318, 222, 10, 10);
    CHECK_EQ(bits_set(LABEL_BMP_MAX), 4);
}

/* A 1-bit face of 10 x 2 dots: '?', '@', 'A' and the extra 'Ä' (U+00C4). */
static const uint8_t k_bits1[] = {
    0xC0, 0x40, 0x00, 0x00,   /* '?': dots 0, 1, 9 in row 0  */
    0x00, 0x00, 0x00, 0x00,   /* '@': empty                  */
    0x80, 0x00, 0x40, 0x00,   /* 'A': dot 0 row 0, dot 1 row 1 */
    0x00, 0x00, 0xFF, 0xC0,   /* 'Ä': row 1 full             */
};
static const uint16_t k_extra1[] = { 0x00C4u };
static const gfx_font_t k_face1 = {
    .glyphs = k_bits1, .width = 10, .height = 2, .bytes_per_row = 2,
    .bpp = 1, .first = '?', .last = 'A', .extra_count = 1,
    .extra = k_extra1,
};

TEST_CASE(a_1bit_face_draws_its_bits_and_a_missing_glyph_as_the_question_mark)
{
    label_bmp_t b;
    CHECK_EQ(bmp_for(&b, 40u, 30u), LABEL_OK);
    CHECK_EQ(label_text(&b, 10, 20, &k_face1, "A", 320), 1);
    CHECK(label_px_at(&b, 10, 20));
    CHECK(label_px_at(&b, 11, 21));
    CHECK_EQ(bits_set(LABEL_BMP_MAX), 2);

    CHECK_EQ(bmp_for(&b, 40u, 30u), LABEL_OK);
    CHECK_EQ(label_text(&b, 0, 0, &k_face1, "Z", 320), 1);
    CHECK(label_px_at(&b, 0, 0));
    CHECK(label_px_at(&b, 1, 0));
    CHECK(label_px_at(&b, 9, 0));
    CHECK_EQ(bits_set(LABEL_BMP_MAX), 3);

    CHECK_EQ(bmp_for(&b, 40u, 30u), LABEL_OK);
    CHECK_EQ(label_text(&b, 0, 0, &k_face1, "A\xC3\x84", 320), 2);
    for (int x = 10; x < 20; x++) {
        CHECK(label_px_at(&b, x, 1));
    }
    CHECK_EQ(bits_set(LABEL_BMP_MAX), 12);
}

TEST_CASE(only_whole_cells_left_of_the_end_are_drawn)
{
    label_bmp_t b;
    CHECK_EQ(bmp_for(&b, 40u, 30u), LABEL_OK);
    CHECK_EQ(label_text(&b, 0, 0, &k_face1, "AAAA", 30), 3);
    CHECK(label_px_at(&b, 20, 0));
    CHECK(!label_px_at(&b, 30, 0));
    CHECK_EQ(label_text(&b, 0, 5, &k_face1, "AAAA", 29), 2);
    CHECK_EQ(label_text(&b, 0, 9, &k_face1, "", 320), 0);
}

/* A 1-bit face with no '?': a missing glyph is an empty cell. */
static const gfx_font_t k_face_noq = {
    .glyphs = k_bits1 + 8, .width = 10, .height = 2, .bytes_per_row = 2,
    .bpp = 1, .first = 'A', .last = 'A', .extra_count = 0, .extra = NULL,
};

TEST_CASE(a_face_without_the_question_mark_leaves_a_missing_glyph_empty)
{
    label_bmp_t b;
    CHECK_EQ(bmp_for(&b, 40u, 30u), LABEL_OK);
    CHECK_EQ(label_text(&b, 0, 0, &k_face_noq, "ZA", 320), 2);
    CHECK(label_px_at(&b, 10, 0));
    CHECK_EQ(bits_set(LABEL_BMP_MAX), 2);
}

/* An 8-bit face of 4 x 1: coverage 127, 128, 255, 0. */
static const uint8_t k_bits8[] = { 127u, 128u, 255u, 0u };
static const gfx_font_t k_face8 = {
    .glyphs = k_bits8, .width = 4, .height = 1, .bytes_per_row = 4,
    .bpp = 8, .first = '?', .last = '?', .extra_count = 0, .extra = NULL,
};

TEST_CASE(an_8bit_face_is_black_from_coverage_128)
{
    label_bmp_t b;
    CHECK_EQ(bmp_for(&b, 40u, 30u), LABEL_OK);
    CHECK_EQ(label_text(&b, 0, 0, &k_face8, "?", 320), 1);
    CHECK(!label_px_at(&b, 0, 0));
    CHECK(label_px_at(&b, 1, 0));
    CHECK(label_px_at(&b, 2, 0));
    CHECK(!label_px_at(&b, 3, 0));
}

TEST_CASE(a_panel_face_draws_as_its_glyph_table_says)
{
    label_bmp_t b;
    CHECK_EQ(bmp_for(&b, 40u, 30u), LABEL_OK);
    const gfx_font_t *f = &gfx_font_8x16;
    CHECK_EQ(label_text(&b, 30, 40, f, "K", 320), 1);
    const uint8_t *g = f->glyphs
        + (size_t)('K' - f->first) * f->height * f->bytes_per_row;
    long ink = 0;
    for (int r = 0; r < (int)f->height; r++) {
        for (int c = 0; c < (int)f->width; c++) {
            const bool want = g[r * f->bytes_per_row + c] >= 128u;
            ink += want;
            if (label_px_at(&b, 30 + c, 40 + r) != want) {
                T_FAIL("K at %d,%d", c, r);
            }
        }
    }
    CHECK(ink > 10);
    CHECK_EQ(bits_set(LABEL_BMP_MAX), ink);
}

TEST_CASE(a_face_that_draws_no_dot_for_a_character_is_marked)
{
    CHECK(label_face_draws(&gfx_font_16x28, "KST X10-0000A"));
    CHECK(label_face_draws(&gfx_font_16x28, "KST_X10-"));
    CHECK(label_face_draws(&gfx_font_12x24, "KST_X10-"));
    CHECK(label_face_draws(&gfx_font_8x16, "M\xC3\x9CLLER"));
    CHECK(label_face_draws(&gfx_font_8x16, ""));
    /* A glyph the face lacks, with no '?' to stand in, draws nothing. */
    CHECK(label_face_draws(&k_face_noq, "A A"));
    CHECK(!label_face_draws(&k_face_noq, "AZ"));
    /* '@' is in the face's range but empty. */
    CHECK(!label_face_draws(&k_face1, "A@"));
    CHECK(label_face_draws(&k_face1, "AZ\xC3\x84"));
    /* A face without a glyph table draws nothing. */
    const gfx_font_t none = { .glyphs = NULL, .width = 10, .height = 2,
                              .bytes_per_row = 2, .bpp = 1, .first = 'A',
                              .last = 'A' };
    CHECK(!label_face_draws(&none, "A"));
    label_bmp_t b;
    CHECK_EQ(bmp_for(&b, 40u, 30u), LABEL_OK);
    CHECK_EQ(label_text(&b, 0, 0, &none, "AA", 320), 2);
    CHECK_EQ(bits_set(LABEL_BMP_MAX), 0);
}

int main(void)
{
    RUN(the_corners_of_a_40x30_label_land_on_the_station_rows);
    RUN(a_roll_is_held_to_15_to_48mm_by_20_to_100mm);
    RUN(the_bitmap_takes_48_bytes_a_column_and_refuses_a_short_buffer);
    RUN(a_label_is_centered_on_the_head);
    RUN(a_fill_of_the_whole_label_sets_its_rows_and_nothing_else);
    RUN(a_dot_off_the_label_is_dropped);
    RUN(a_1bit_face_draws_its_bits_and_a_missing_glyph_as_the_question_mark);
    RUN(only_whole_cells_left_of_the_end_are_drawn);
    RUN(a_face_without_the_question_mark_leaves_a_missing_glyph_empty);
    RUN(an_8bit_face_is_black_from_coverage_128);
    RUN(a_panel_face_draws_as_its_glyph_table_says);
    RUN(a_face_that_draws_no_dot_for_a_character_is_marked);
    return test_summary("label_bitmap");
}
