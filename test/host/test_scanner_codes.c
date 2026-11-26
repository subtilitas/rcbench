/**
 * @file test_scanner_codes.c
 * @brief The stored setup code (scanner_codes.h) read as a QR code of
 *        version 1: the 3 finder patterns, the timing patterns, the dark
 *        module, and both copies of the format bits equal and a valid
 *        BCH code word.  tools/gen_scanner_codes.py checks the same and
 *        the Reed-Solomon code words; this holds the C table the panel
 *        draws.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"
#include "scanner_codes.h"

/** @brief Module (x, y) of the Serial Output code. */
static bool dark(int x, int y)
{
    return scanner_code_dark(scanner_code_serial, x, y);
}

/** @brief Whether a finder pattern sits with its top left at (x0, y0). */
static bool finder(int x0, int y0)
{
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            const int dx = x > 3 ? x - 3 : 3 - x;
            const int dy = y > 3 ? y - 3 : 3 - y;
            const int ring = dx > dy ? dx : dy;
            if (dark(x0 + x, y0 + y) != (ring != 2)) {
                return false;
            }
        }
    }
    return true;
}

/** @brief Whether @p w is one of the 32 format words, masked. */
static bool bch_valid(unsigned w)
{
    for (unsigned d = 0u; d < 32u; ++d) {
        unsigned v = d << 10;
        for (int i = 14; i >= 10; --i) {
            if ((v >> i) & 1u) {
                v ^= 0x537u << (i - 10);
            }
        }
        if ((((d << 10) | v) ^ 0x5412u) == w) {
            return true;
        }
    }
    return false;
}

TEST_CASE(the_three_finder_patterns_sit_in_their_corners)
{
    CHECK(finder(0, 0));
    CHECK(finder(14, 0));
    CHECK(finder(0, 14));
    CHECK(!finder(14, 14));
    CHECK(!finder(1, 0));
}

TEST_CASE(the_timing_patterns_and_the_dark_module_are_in_place)
{
    for (int i = 8; i <= 12; ++i) {
        CHECK_EQ(dark(i, 6), i % 2 == 0);
        CHECK_EQ(dark(6, i), i % 2 == 0);
    }
    CHECK(dark(8, 13));
}

TEST_CASE(both_copies_of_the_format_bits_are_one_valid_word)
{
    static const int a[15][2] = {
        { 0, 8 }, { 1, 8 }, { 2, 8 }, { 3, 8 }, { 4, 8 }, { 5, 8 },
        { 7, 8 }, { 8, 8 }, { 8, 7 }, { 8, 5 }, { 8, 4 }, { 8, 3 },
        { 8, 2 }, { 8, 1 }, { 8, 0 },
    };
    static const int b[15][2] = {
        { 8, 20 }, { 8, 19 }, { 8, 18 }, { 8, 17 }, { 8, 16 }, { 8, 15 },
        { 8, 14 }, { 13, 8 }, { 14, 8 }, { 15, 8 }, { 16, 8 }, { 17, 8 },
        { 18, 8 }, { 19, 8 }, { 20, 8 },
    };
    unsigned wa = 0u, wb = 0u;
    for (int i = 0; i < 15; ++i) {
        wa = (wa << 1) | (dark(a[i][0], a[i][1]) ? 1u : 0u);
        wb = (wb << 1) | (dark(b[i][0], b[i][1]) ? 1u : 0u);
    }
    CHECK_EQ(wa, wb);
    CHECK(bch_valid(wa));
    CHECK(!bch_valid(wa ^ 1u));
    /* Level M (00), mask 3. */
    CHECK_EQ(((wa ^ 0x5412u) >> 10) & 0x1Fu, 3u);
}

TEST_CASE(outside_the_matrix_and_null_read_light)
{
    CHECK(!scanner_code_dark(scanner_code_serial, -1, 0));
    CHECK(!scanner_code_dark(scanner_code_serial, 0, 21));
    CHECK(!scanner_code_dark(NULL, 0, 0));
}

int main(void)
{
    RUN(the_three_finder_patterns_sit_in_their_corners);
    RUN(the_timing_patterns_and_the_dark_module_are_in_place);
    RUN(both_copies_of_the_format_bits_are_one_valid_word);
    RUN(outside_the_matrix_and_null_read_light);
    return test_summary("scanner_codes");
}
