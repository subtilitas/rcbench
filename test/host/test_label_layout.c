/**
 * @file
 * @brief The servo label's layout: the QR code's matrix, version and module
 *        size per roll, side by side against stacked, the faces of the
 *        lines, DUT NAME's breaks, the refusals and the test label.  Every
 *        drawn label is checked dot for dot inside the code's block (so no
 *        text reaches the quiet zone) and for dots off the label's width.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "label_layout.h"

static uint8_t s_bits[LABEL_BMP_MAX];
static label_bmp_t s_bmp;
static label_info_t s_info;

/**
 * @brief Lay out a servo label on a roll of @p w x @p h mm.
 * @param w    width, mm
 * @param h    height, mm
 * @param id   the id
 * @param dut  DUT NAME
 * @return label_layout()'s answer
 */
static label_err_t lay(uint8_t w, uint8_t h, const char *id, const char *dut)
{
    const label_roll_t roll = { w, h };
    const label_text_t t = { id, dut };
    s_bmp = (label_bmp_t){ .bits = s_bits, .cap = sizeof s_bits };
    return label_layout(&roll, &t, &s_bmp, &s_info);
}

/**
 * @brief Every dot of the code's block is its module: dark modules black,
 *        the quiet zone and light modules white.
 * @return the count of dots that differ
 */
static long block_errors(void)
{
    const int m = s_info.module;
    const int q = (int)LABEL_QUIET * m;
    long bad = 0;
    for (int y = 0; y < s_info.qr_side; y++) {
        for (int x = 0; x < s_info.qr_side; x++) {
            const bool want = x >= q && y >= q
                && label_qr_module(&s_info, (x - q) / m, (y - q) / m);
            bad += label_px_at(&s_bmp, s_info.qr_x + x, s_info.qr_y + y)
                   != want;
        }
    }
    return bad;
}

/**
 * @brief Dots on head rows outside the label's width.
 * @return the count
 */
static long dots_off_label(void)
{
    long n = 0;
    for (int col = 0; col < (int)s_bmp.cols; col++) {
        for (int row = 0; row < 384; row++) {
            const bool on = (s_bits[(size_t)col * 48u + (size_t)row / 8u]
                             >> (row % 8)) & 1u;
            n += on && (row < s_bmp.off || row >= s_bmp.off + s_bmp.w);
        }
    }
    return n;
}

/**
 * @brief Each drawn line inside the label, 8 dots from its right edge, and
 *        clear of the code's block.
 * @return the count of lines that are not
 */
static int lines_astray(void)
{
    int bad = 0;
    for (unsigned i = 0u; i < LABEL_LINES; i++) {
        const label_line_t *l = &s_info.line[i];
        if (l->face == NULL) {
            continue;
        }
        const int right = l->x + l->w;
        const int bottom = l->y + l->h;
        const bool beside = l->x >= s_info.qr_x + s_info.qr_side;
        const bool under = l->y >= s_info.qr_y + s_info.qr_side;
        if (l->x < 0 || l->y < 0 || right > s_bmp.w - 8
            || bottom > s_bmp.cols || !(beside || under)) {
            printf("      line %u astray: %d,%d %ux%u\n", i, l->x, l->y,
                   (unsigned)l->w, (unsigned)l->h);
            bad++;
        }
    }
    return bad;
}

/** @brief The checks every drawn label passes. */
#define CHECK_DRAWN()                                                         \
    do {                                                                      \
        CHECK_EQ(block_errors(), 0);                                          \
        CHECK_EQ(dots_off_label(), 0);                                        \
        CHECK_EQ(lines_astray(), 0);                                          \
    } while (0)

/* The vendored qrcodegen 1.8.0's matrix of KST_X10-0000A at level M: byte
 * mode (the '_'), version 1.  An independent decoder reads it back in
 * test/tools/test_label_qr.py. */
static const char *const k_kst[21] = {
    "#######..##...#######",
    "#.....#..##...#.....#",
    "#.###.#.##.#..#.###.#",
    "#.###.#.#.#...#.###.#",
    "#.###.#.###.#.#.###.#",
    "#.....#.#..#..#.....#",
    "#######.#.#.#.#######",
    "........#.#..........",
    "#.#####....#..#####..",
    "..#.#...##..#####.##.",
    ".##.#.##.##.##.#.#.#.",
    "...###.#.####..#.####",
    "#.#####.#...####.##.#",
    "........#...#...###..",
    "#######..#.##..#..##.",
    "#.....#.#.#.#....###.",
    "#.###.#.#...####.####",
    "#.###.#.##.#....##...",
    "#.###.#.##..#..#.#...",
    "#.....#...#.#..#.##..",
    "#######.##..########.",
};

TEST_CASE(the_code_of_kst_x10_0000a_is_the_vector_dot_for_dot)
{
    CHECK_EQ(lay(40u, 30u, "KST_X10-0000A", "WING LEFT"), LABEL_OK);
    CHECK_EQ(s_info.version, 1);
    CHECK_EQ(s_info.modules, 21);
    const int m = s_info.module;
    const int x0 = s_info.qr_x + 2 * m;
    const int y0 = s_info.qr_y + 2 * m;
    for (int my = 0; my < 21; my++) {
        for (int mx = 0; mx < 21; mx++) {
            const bool want = k_kst[my][mx] == '#';
            CHECK_EQ(label_qr_module(&s_info, mx, my), want);
            /* Every dot of the module, read back through the printer's
             * layout. */
            for (int d = 0; d < m * m; d++) {
                if (label_px_at(&s_bmp, x0 + mx * m + d % m,
                                y0 + my * m + d / m) != want) {
                    T_FAIL("module %d,%d", mx, my);
                    break;
                }
            }
        }
    }
    CHECK(!label_qr_module(&s_info, -1, 0));
    CHECK(!label_qr_module(&s_info, 21, 0));
    CHECK(!label_qr_module(&s_info, 0, 21));
    CHECK_DRAWN();
}

TEST_CASE(version_1_holds_20_characters_or_14_with_an_underscore)
{
    CHECK_EQ(lay(40u, 30u, "A-00000", ""), LABEL_OK);
    CHECK_EQ(s_info.version, 1);
    CHECK_EQ(lay(40u, 30u, "ABCDEFGHIJKLMN-0000A", ""), LABEL_OK);
    CHECK_EQ(s_info.version, 1);
    CHECK_EQ(lay(40u, 30u, "ABCDEFGHIJKLMNO-0000A", ""), LABEL_OK);
    CHECK_EQ(s_info.version, 2);
    CHECK_EQ(s_info.modules, 25);
    CHECK_EQ(lay(40u, 30u, "KST_X10_-0000A", ""), LABEL_OK);
    CHECK_EQ(s_info.version, 1);
    CHECK_EQ(lay(40u, 30u, "KST_X10_A-0000A", ""), LABEL_OK);
    CHECK_EQ(s_info.version, 2);
    CHECK_EQ(lay(40u, 30u, "ABCDEFGHIJKLMNOPQ-ZZZZZ", ""), LABEL_OK);
    CHECK_EQ(s_info.version, 2);
    CHECK_DRAWN();
    CHECK_EQ(lay(40u, 30u, "KST_X10_HV WING L-ZZZZZ", ""), LABEL_OK);
    CHECK_EQ(s_info.version, 2);
    CHECK_DRAWN();
}

TEST_CASE(a_40x30_label_is_side_by_side_with_7_or_6_dot_modules)
{
    CHECK_EQ(lay(40u, 30u, "KST_X10-0000A", "WING LEFT"), LABEL_OK);
    CHECK(!s_info.stacked);
    CHECK_EQ(s_info.module, 7);
    CHECK_EQ(s_info.qr_side, 175);
    CHECK_EQ(s_info.qr_x, 0);
    CHECK_EQ(s_info.qr_y, 24);
    CHECK_EQ(s_info.line[0].x, 183);
    CHECK_DRAWN();
    CHECK_EQ(lay(40u, 30u, "ABCDEFGHIJKLMNOPQ-ZZZZZ", "WING LEFT"), LABEL_OK);
    CHECK_EQ(s_info.module, 6);
    CHECK_EQ(s_info.qr_side, 174);
    CHECK_EQ(s_info.line[0].x, 182);
    CHECK_DRAWN();
}

TEST_CASE(the_module_follows_the_roll)
{
    CHECK_EQ(lay(48u, 30u, "KST_X10-0000A", "WING LEFT"), LABEL_OK);
    CHECK(!s_info.stacked);
    CHECK_EQ(s_info.module, 8);
    CHECK_DRAWN();
    /* 30 x 20mm: a 99-dot column, the serial in 16x28. */
    CHECK_EQ(lay(30u, 20u, "KST_X10-0000A", "WING LEFT"), LABEL_OK);
    CHECK(!s_info.stacked);
    CHECK_EQ(s_info.module, 5);
    CHECK(s_info.line[1].face == &gfx_font_16x28);
    CHECK_DRAWN();
    CHECK_EQ(lay(15u, 30u, "KST_X10-0000A", "WING LEFT"), LABEL_OK);
    CHECK(s_info.stacked);
    CHECK_EQ(s_info.module, 4);
    CHECK_EQ(s_info.qr_x, 10);
    CHECK_EQ(s_info.qr_y, 4);
    CHECK_EQ(s_info.line[0].y, 4 + 100 + 4);
    CHECK_DRAWN();
    CHECK_EQ(lay(25u, 100u, "KST_X10-0000A", "WING LEFT"), LABEL_OK);
    CHECK(s_info.stacked);
    CHECK_EQ(s_info.module, 8);
    CHECK(s_info.line[1].face == &gfx_font_20x36);
    CHECK_DRAWN();
}

TEST_CASE(a_module_under_4_dots_is_too_small)
{
    /* 15mm wide: 120 dots hold 25 or 29 modules at 4 dots, 33 at 3. */
    CHECK_EQ(lay(15u, 20u, "KST_X10-0000A", "WING LEFT"), LABEL_OK);
    CHECK_EQ(s_info.module, 4);
    CHECK_DRAWN();
    CHECK_EQ(lay(15u, 20u, "ABCDEFGHIJKLMNOPQ-ZZZZZ", ""), LABEL_OK);
    CHECK_EQ(s_info.version, 2);
    CHECK_EQ(s_info.module, 4);
    CHECK_DRAWN();
    CHECK_EQ(lay(15u, 20u, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCD-ZZZZZ", ""),
             LABEL_TOO_SMALL);
    CHECK_EQ(lay(48u, 20u, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCD-ZZZZZ", ""),
             LABEL_OK);
    CHECK_EQ(s_info.version, 3);
    CHECK_DRAWN();
}

TEST_CASE(lines_that_do_not_fit_go_from_the_bottom_then_join)
{
    /* 15 x 20mm: 32 dots under the code; without DUT NAME prefix and
     * serial are one line. */
    CHECK_EQ(lay(15u, 20u, "KST_X10-0000A", ""), LABEL_OK);
    CHECK(s_info.line[2].face == NULL);
    CHECK(s_info.line[3].face == NULL);
    CHECK(s_info.line[1].face == NULL);
    CHECK_STR_EQ(s_info.line[0].text, "KST_X10-0000A");
    CHECK(s_info.line[0].face == &gfx_font_8x16);
    CHECK_DRAWN();
    /* With DUT NAME it is DUT NAME alone, in 10x20. */
    CHECK_EQ(lay(15u, 20u, "KST_X10-0000A", "WING LEFT"), LABEL_OK);
    CHECK(s_info.line[0].face == NULL);
    CHECK(s_info.line[1].face == NULL);
    CHECK_STR_EQ(s_info.line[2].text, "WING LEFT");
    CHECK(s_info.line[2].face == &gfx_font_10x20);
    CHECK(s_info.line[3].face == NULL);
    CHECK_DRAWN();
}

/**
 * @brief Whether a drawn line reads @p s.
 * @param s  the text
 * @return true when one of the four lines is @p s
 */
static bool drawn(const char *s)
{
    for (unsigned i = 0u; i < LABEL_LINES; i++) {
        if (s_info.line[i].face != NULL
            && strcmp(s_info.line[i].text, s) == 0) {
            return true;
        }
    }
    return false;
}

TEST_CASE(a_25x25_label_shows_dut_name_under_the_code_not_the_id)
{
    /* 200 x 184 dots, version 1 at 6 dots: 22 dots under the code, one
     * line of 10x20.  The id stays in the code. */
    CHECK_EQ(lay(25u, 25u, "KST_X10-0000A", "WING LEFT"), LABEL_OK);
    CHECK(s_info.stacked);
    CHECK_EQ(s_info.version, 1);
    CHECK_EQ(s_info.module, 6);
    CHECK(s_info.line[0].face == NULL);
    CHECK(s_info.line[1].face == NULL);
    CHECK_STR_EQ(s_info.line[2].text, "WING LEFT");
    CHECK(s_info.line[2].face == &gfx_font_10x20);
    CHECK(s_info.line[3].face == NULL);
    CHECK_EQ(s_info.line[2].x, 8);
    CHECK_EQ(s_info.line[2].y, 4 + 150 + 4);
    CHECK(!drawn("KST_X10-0000A"));
    CHECK_DRAWN();
}

TEST_CASE(a_25x25_label_without_dut_name_shows_the_id)
{
    CHECK_EQ(lay(25u, 25u, "KST_X10-0000A", ""), LABEL_OK);
    CHECK_STR_EQ(s_info.line[0].text, "KST_X10-0000A");
    CHECK(s_info.line[0].face == &gfx_font_10x20);
    CHECK(s_info.line[1].face == NULL);
    CHECK(s_info.line[2].face == NULL);
    CHECK_DRAWN();
    /* Spaces alone are no DUT NAME. */
    CHECK_EQ(lay(25u, 25u, "KST_X10-0000A", "   "), LABEL_OK);
    CHECK_STR_EQ(s_info.line[0].text, "KST_X10-0000A");
    CHECK(s_info.line[2].face == NULL);
    CHECK_DRAWN();
}

TEST_CASE(a_25x25_label_holds_23_characters_of_dut_name_in_8x16)
{
    /* Version 2 at 5 dots: 26 dots under the code; 23 cells of 8x16 are
     * the 184-dot column. */
    CHECK_EQ(lay(25u, 25u, "KST_X10_HV_WING_L-ZZZZZ",
                 "AILERON LEFT OUTBOARD 2"), LABEL_OK);
    CHECK_EQ(s_info.version, 2);
    CHECK_EQ(s_info.module, 5);
    CHECK(s_info.line[0].face == NULL);
    CHECK(s_info.line[1].face == NULL);
    CHECK_STR_EQ(s_info.line[2].text, "AILERON LEFT OUTBOARD 2");
    CHECK(s_info.line[2].face == &gfx_font_8x16);
    CHECK_EQ(s_info.line[2].w, 184);
    CHECK(s_info.line[3].face == NULL);
    CHECK_DRAWN();
    /* 24 characters: one line of 6x12, as two lines do not fit. */
    CHECK_EQ(lay(25u, 25u, "KST_X10_HV_WING_L-ZZZZZ",
                 "AILERON LEFT OUTBOARD 2B"), LABEL_OK);
    CHECK_STR_EQ(s_info.line[2].text, "AILERON LEFT OUTBOARD 2B");
    CHECK(s_info.line[2].face == &gfx_font_6x12);
    CHECK(s_info.line[3].face == NULL);
    CHECK_DRAWN();
    /* Past 30 cells of 6x12: the first line, broken at a space. */
    CHECK_EQ(lay(25u, 25u, "KST_X10_HV_WING_L-ZZZZZ",
                 "LEFT WING AILERON OUTBOARD SERVO"), LABEL_OK);
    CHECK_STR_EQ(s_info.line[2].text, "LEFT WING AILERON OUTBOARD");
    CHECK(s_info.line[2].face == &gfx_font_6x12);
    CHECK(s_info.line[3].face == NULL);
    CHECK_DRAWN();
}

TEST_CASE(labels_that_hold_everything_keep_the_id_and_dut_name)
{
    const uint8_t rolls[][2] = { { 40u, 30u }, { 48u, 30u }, { 25u, 100u },
                                 { 30u, 20u }, { 15u, 30u } };
    for (size_t r = 0u; r < sizeof rolls / sizeof rolls[0]; r++) {
        CHECK_EQ(lay(rolls[r][0], rolls[r][1], "KST_X10-0000A", "WING LEFT"),
                 LABEL_OK);
        CHECK_STR_EQ(s_info.line[0].text, "KST_X10-");
        CHECK_STR_EQ(s_info.line[1].text, "0000A");
        CHECK_STR_EQ(s_info.line[2].text, "WING LEFT");
        CHECK(s_info.line[2].face == &gfx_font_8x16);
        CHECK_DRAWN();
    }
}

TEST_CASE(the_test_label_keeps_its_version_where_the_roll_is_small)
{
    const label_roll_t roll = { 25u, 25u };
    s_bmp = (label_bmp_t){ .bits = s_bits, .cap = sizeof s_bits };
    CHECK_EQ(label_layout_test(&roll, "0.23.17", &s_bmp, &s_info), LABEL_OK);
    CHECK(s_info.line[0].face != NULL);
    CHECK(s_info.line[2].face == NULL);
    CHECK_DRAWN();
}

TEST_CASE(the_prefix_takes_the_largest_face_that_holds_it)
{
    CHECK_EQ(lay(40u, 30u, "KST-0000A", ""), LABEL_OK);
    CHECK_STR_EQ(s_info.line[0].text, "KST-");
    CHECK(s_info.line[0].face == &gfx_font_16x28);
    /* The 16x28 face draws '_' on its last rows: it takes KST_X10- too. */
    CHECK_EQ(lay(40u, 30u, "KST_X10-0000A", ""), LABEL_OK);
    CHECK_STR_EQ(s_info.line[0].text, "KST_X10-");
    CHECK(s_info.line[0].face == &gfx_font_16x28);
    {
        const label_line_t *l = &s_info.line[0];
        const int cell = l->face->width;
        int ink = 0;
        for (int y = l->y; y < l->y + (int)l->h; y++) {
            for (int x = l->x + 3 * cell; x < l->x + 4 * cell; x++) {
                ink += label_px_at(&s_bmp, x, y);
            }
        }
        CHECK(ink > 0);
    }
    CHECK_STR_EQ(s_info.line[1].text, "0000A");
    CHECK(s_info.line[1].face == &gfx_font_20x36);
    CHECK(s_info.line[2].face == NULL);
    CHECK(s_info.line[3].face == NULL);
    /* 17 characters and the '-': 108 dots in 6x12. */
    CHECK_EQ(lay(40u, 30u, "ABCDEFGHIJKLMNOPQ-ZZZZZ", ""), LABEL_OK);
    CHECK_STR_EQ(s_info.line[0].text, "ABCDEFGHIJKLMNOPQ-");
    CHECK(s_info.line[0].face == &gfx_font_6x12);
    CHECK_EQ(s_info.line[0].w, 108);
    /* A '-' inside the prefix: the id splits at the last one. */
    CHECK_EQ(lay(40u, 30u, "A-B-00001", ""), LABEL_OK);
    CHECK_STR_EQ(s_info.line[0].text, "A-B-");
    CHECK_STR_EQ(s_info.line[1].text, "00001");
    /* No '-': the id is line 2 alone. */
    CHECK_EQ(lay(40u, 30u, "NODASH", ""), LABEL_OK);
    CHECK(s_info.line[0].face == NULL);
    CHECK_STR_EQ(s_info.line[1].text, "NODASH");
    CHECK_DRAWN();
}

TEST_CASE(dut_name_takes_two_lines_of_8x16_broken_at_a_space)
{
    /* 40 x 30mm, version 1: a 129-dot column, 16 cells of 8x16. */
    CHECK_EQ(lay(40u, 30u, "KST_X10-0000A", "ABCDEFGHIJKLMNOP"), LABEL_OK);
    CHECK_STR_EQ(s_info.line[2].text, "ABCDEFGHIJKLMNOP");
    CHECK(s_info.line[2].face == &gfx_font_8x16);
    CHECK(s_info.line[3].face == NULL);
    CHECK_DRAWN();
    CHECK_EQ(lay(40u, 30u, "KST_X10-0000A", "ABCDEFGHIJKLMNOPQ"), LABEL_OK);
    CHECK_STR_EQ(s_info.line[2].text, "ABCDEFGHIJKLMNOP");
    CHECK_STR_EQ(s_info.line[3].text, "Q");
    CHECK(s_info.line[3].face == &gfx_font_8x16);
    CHECK_EQ(lay(40u, 30u, "KST_X10-0000A", "ABCDEFGHIJKLMNOPQRSTUVW"),
             LABEL_OK);
    CHECK_STR_EQ(s_info.line[2].text, "ABCDEFGHIJKLMNOP");
    CHECK_STR_EQ(s_info.line[3].text, "QRSTUVW");
    CHECK_EQ(lay(40u, 30u, "KST_X10-0000A", "WING LEFT OUTBOARD"), LABEL_OK);
    CHECK_STR_EQ(s_info.line[2].text, "WING LEFT");
    CHECK_STR_EQ(s_info.line[3].text, "OUTBOARD");
    /* A space right after 16 cells breaks there. */
    CHECK_EQ(lay(40u, 30u, "KST_X10-0000A", "ABCDEFGHIJKLMNOP QRSTUV"),
             LABEL_OK);
    CHECK_STR_EQ(s_info.line[2].text, "ABCDEFGHIJKLMNOP");
    CHECK_STR_EQ(s_info.line[3].text, "QRSTUV");
    /* Spaces around it are dropped. */
    CHECK_EQ(lay(40u, 30u, "KST_X10-0000A", "  WING  "), LABEL_OK);
    CHECK_STR_EQ(s_info.line[2].text, "WING");
    CHECK(s_info.line[3].face == NULL);
    CHECK_DRAWN();
}

TEST_CASE(dut_name_that_needs_a_third_line_is_6x12_and_cut)
{
    /* 21 cells of 6x12 a line on 129 dots. */
    CHECK_EQ(lay(40u, 30u, "KST_X10-0000A",
                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJ"), LABEL_OK);
    CHECK(s_info.line[2].face == &gfx_font_6x12);
    CHECK_STR_EQ(s_info.line[2].text, "ABCDEFGHIJKLMNOPQRSTU");
    CHECK_STR_EQ(s_info.line[3].text, "VWXYZ0123456789ABCDEF");
    CHECK_EQ(lay(40u, 30u, "KST_X10-0000A",
                 "LEFT WING AILERON OUTBOARD SERVO"), LABEL_OK);
    CHECK(s_info.line[2].face == &gfx_font_6x12);
    CHECK_STR_EQ(s_info.line[2].text, "LEFT WING AILERON");
    CHECK_STR_EQ(s_info.line[3].text, "OUTBOARD SERVO");
    CHECK_DRAWN();
}

TEST_CASE(utf8_in_dut_name_counts_cells_not_bytes)
{
    CHECK_EQ(lay(40u, 30u, "KST_X10-0000A",
                 "M\xC3\x9CLLER \xC3\x84\xC3\x96\xC3\x9C\xC3\x84\xC3\x96"
                 "\xC3\x9C\xC3\x84\xC3\x96\xC3\x9C\xC3\x84"), LABEL_OK);
    CHECK(s_info.line[2].face == &gfx_font_8x16);
    CHECK_STR_EQ(s_info.line[2].text, "M\xC3\x9CLLER");
    CHECK_EQ(s_info.line[3].w, 10 * 8);
    CHECK_DRAWN();
}

TEST_CASE(ids_with_spaces_and_of_7_and_23_characters_draw)
{
    const char *const ids[] = { "A-00000", "KST X10-00001",
                                "KST X10 HV WING L-ZZZZZ",
                                "KST_X10_HV_WING_L-ZZZZZ" };
    const uint8_t rolls[][2] = { { 40u, 30u }, { 48u, 30u }, { 30u, 20u },
                                 { 15u, 30u }, { 25u, 100u },
                                 { 25u, 25u } };
    for (size_t i = 0u; i < sizeof ids / sizeof ids[0]; i++) {
        for (size_t r = 0u; r < sizeof rolls / sizeof rolls[0]; r++) {
            CHECK_EQ(lay(rolls[r][0], rolls[r][1], ids[i],
                         "AILERON RIGHT OUTBOARD"), LABEL_OK);
            CHECK_DRAWN();
            CHECK(s_info.line[1].face != NULL || s_info.line[0].face != NULL
                  || s_info.line[2].face != NULL);
        }
    }
}

TEST_CASE(a_roll_out_of_range_or_a_bad_id_draws_nothing)
{
    CHECK_EQ(lay(14u, 30u, "KST_X10-0000A", ""), LABEL_TOO_SMALL);
    CHECK_EQ(lay(40u, 101u, "KST_X10-0000A", ""), LABEL_TOO_LARGE);
    CHECK_EQ(lay(49u, 30u, "KST_X10-0000A", ""), LABEL_TOO_LARGE);
    CHECK_EQ(lay(0u, 0u, "KST_X10-0000A", ""), LABEL_NO_LABEL);
    CHECK_EQ(lay(40u, 30u, "", ""), LABEL_QR);
    CHECK_EQ(lay(40u, 30u, NULL, NULL), LABEL_QR);
    /* Past version 3 at level M: 62 bytes. */
    char big[80];
    memset(big, 'a', sizeof big - 1u);
    big[sizeof big - 1u] = '\0';
    CHECK_EQ(lay(40u, 30u, big, ""), LABEL_QR);
    const label_roll_t roll = { 40u, 30u };
    s_bmp = (label_bmp_t){ .bits = s_bits, .cap = 100u };
    CHECK_EQ(label_layout(&roll, NULL, &s_bmp, &s_info), LABEL_MEMORY);
}

TEST_CASE(the_test_label_names_the_bench_the_version_and_the_roll)
{
    const label_roll_t roll = { 40u, 30u };
    s_bmp = (label_bmp_t){ .bits = s_bits, .cap = sizeof s_bits };
    CHECK_EQ(label_layout_test(&roll, "0.23.17", &s_bmp, &s_info), LABEL_OK);
    CHECK_EQ(s_info.version, 1);
    CHECK_STR_EQ(s_info.line[0].text, "RCBENCH");
    CHECK_STR_EQ(s_info.line[1].text, "0.23.17");
    CHECK(s_info.line[1].face == &gfx_font_16x28);
    CHECK_STR_EQ(s_info.line[2].text, "40x30mm");
    CHECK_DRAWN();
    const label_roll_t r2 = { 48u, 100u };
    CHECK_EQ(label_layout_test(&r2, NULL, &s_bmp, &s_info), LABEL_OK);
    CHECK(s_info.line[1].face == NULL);
    CHECK_STR_EQ(s_info.line[2].text, "48x100mm");
    CHECK_DRAWN();
    CHECK_EQ(label_layout_test(NULL, "1", &s_bmp, &s_info), LABEL_NO_LABEL);
}

int main(void)
{
    RUN(the_code_of_kst_x10_0000a_is_the_vector_dot_for_dot);
    RUN(version_1_holds_20_characters_or_14_with_an_underscore);
    RUN(a_40x30_label_is_side_by_side_with_7_or_6_dot_modules);
    RUN(the_module_follows_the_roll);
    RUN(a_module_under_4_dots_is_too_small);
    RUN(lines_that_do_not_fit_go_from_the_bottom_then_join);
    RUN(a_25x25_label_shows_dut_name_under_the_code_not_the_id);
    RUN(a_25x25_label_without_dut_name_shows_the_id);
    RUN(a_25x25_label_holds_23_characters_of_dut_name_in_8x16);
    RUN(labels_that_hold_everything_keep_the_id_and_dut_name);
    RUN(the_test_label_keeps_its_version_where_the_roll_is_small);
    RUN(the_prefix_takes_the_largest_face_that_holds_it);
    RUN(dut_name_takes_two_lines_of_8x16_broken_at_a_space);
    RUN(dut_name_that_needs_a_third_line_is_6x12_and_cut);
    RUN(utf8_in_dut_name_counts_cells_not_bytes);
    RUN(ids_with_spaces_and_of_7_and_23_characters_draw);
    RUN(a_roll_out_of_range_or_a_bad_id_draws_nothing);
    RUN(the_test_label_names_the_bench_the_version_and_the_roll);
    return test_summary("label_layout");
}
