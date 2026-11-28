/*
 * The Guition JC3636K518C's boot face (shared/dial/dk518_boot.c), drawn at
 * 360 x 360.
 *
 * The failures this is about: a face drawn into a canvas of the M5Stack
 * Dial's 240 x 240, or past a canvas's edge; a line off the middle; a ring
 * that misses the rim; the name's line empty before the BLE host knows its
 * address; and a long name or version written past its buffer.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "dk518_face.h"
#include "ui_theme.h"

/** @brief The frame, one pixel of guard on each side. */
static gfx_color_t s_fb[DK518_SIZE * DK518_SIZE + 2];
/** @brief The canvas over s_fb + 1. */
static gfx_canvas_t s_c;

/** @brief The background of the Dial's dark theme. */
static gfx_color_t bg(void)
{
    const uint32_t c = ui_theme_rgb24(UI_THEME_DARK, UI_C_BG);
    return GFX_RGB((c >> 16) & 0xFFu, (c >> 8) & 0xFFu, c & 0xFFu);
}

/** @brief Pixels other than the background inside a box.
 *  @param x left @param y top @param w width @param h height
 *  @return the count */
static int ink(int x, int y, int w, int h)
{
    int n = 0;
    const gfx_color_t b = bg();
    for (int r = y; r < y + h; ++r) {
        for (int k = x; k < x + w; ++k) {
            n += s_fb[1 + r * DK518_SIZE + k] != b;
        }
    }
    return n;
}

/** @brief The leftmost and rightmost inked columns of a band of rows,
 *         60px in from either edge, clear of the rim ring.
 *  @param y top @param h height @param lo takes the left @param hi the
 *  right */
static void span(int y, int h, int *lo, int *hi)
{
    const gfx_color_t b = bg();
    *lo = DK518_SIZE;
    *hi = -1;
    for (int r = y; r < y + h; ++r) {
        for (int k = 60; k < DK518_SIZE - 60; ++k) {
            if (s_fb[1 + r * DK518_SIZE + k] != b) {
                *lo = k < *lo ? k : *lo;
                *hi = k > *hi ? k : *hi;
            }
        }
    }
}

/** @brief Draw the face with @p name and @p version.
 *  @return dk518_boot_draw()'s result */
static bool draw(const char *name, const char *version)
{
    const dk518_boot_t b = { name, version };
    s_fb[0] = 0x1234u;
    s_fb[DK518_SIZE * DK518_SIZE + 1] = 0x1234u;
    gfx_canvas_init(&s_c, s_fb + 1, DK518_SIZE, DK518_SIZE, 0);
    return dk518_boot_draw(&s_c, &b);
}

TEST_CASE(the_face_fills_its_canvas_and_writes_nothing_past_it)
{
    CHECK(draw("DIAL-3A10", "0.23.18"));
    CHECK_EQ(s_fb[0], 0x1234u);
    CHECK_EQ(s_fb[DK518_SIZE * DK518_SIZE + 1], 0x1234u);
    /* The corners, outside the rim, are background. */
    CHECK_EQ(ink(0, 0, 4, 4), 0);
    CHECK_EQ(ink(DK518_SIZE - 4, DK518_SIZE - 4, 4, 4), 0);
}

TEST_CASE(the_rim_ring_closes_the_face_at_radius_172)
{
    CHECK(draw("DIAL-3A10", "0.23.18"));
    const int mid = DK518_SIZE / 2;
    /* Top, bottom, left and right of the ring are lit; 10px inside and
     * outside it are not. */
    CHECK(ink(mid, mid - DK518_RIM_R, 1, 1) == 1);
    CHECK(ink(mid, mid + DK518_RIM_R, 1, 1) == 1);
    CHECK(ink(mid - DK518_RIM_R, mid, 1, 1) == 1);
    CHECK(ink(mid + DK518_RIM_R, mid, 1, 1) == 1);
    CHECK_EQ(ink(mid - DK518_RIM_R - 6, mid, 1, 1), 0);
    CHECK_EQ(ink(mid - DK518_RIM_R + 10, mid, 1, 1), 0);
    CHECK_EQ(ink(mid, mid - DK518_RIM_R + 10, 1, 1), 0);
}

TEST_CASE(every_line_is_centered_on_the_middle_column)
{
    static const int rows[][2] = {
        { 76, 20 }, { 112, 28 }, { 146, 72 }, { 234, 28 }, { 270, 24 },
    };
    CHECK(draw("DIAL-3A10", "0.23.18"));
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
        int lo = 0;
        int hi = 0;
        span(rows[i][0], rows[i][1], &lo, &hi);
        CHECK(hi > lo);
        /* Glyph margins differ by a few pixels left and right. */
        CHECK(lo + hi - (DK518_SIZE - 1) <= 8);
        CHECK(lo + hi - (DK518_SIZE - 1) >= -8);
    }
}

TEST_CASE(a_dial_without_its_name_yet_shows_DIAL_and_no_version_no_line)
{
    CHECK(draw(NULL, NULL));
    const int named = ink(60, 234, DK518_SIZE - 120, 28);
    CHECK(named > 0);
    CHECK_EQ(ink(60, 270, DK518_SIZE - 120, 24), 0);
    CHECK(draw("", ""));
    CHECK_EQ(ink(60, 234, DK518_SIZE - 120, 28), named);
    CHECK(draw("DIAL-3A10", ""));
    CHECK(ink(60, 234, DK518_SIZE - 120, 28) > named);
}

TEST_CASE(a_long_name_or_version_is_cut_to_16_characters)
{
    int lo = 0;
    int hi = 0;
    int lo16 = 0;
    int hi16 = 0;
    /* 16 characters drawn, and the same 16 of a longer text. */
    CHECK(draw("DIAL-0123456789A", "65535.65535.655"));
    const int n16 = ink(0, 234, DK518_SIZE, 60);
    span(234, 28, &lo16, &hi16);
    CHECK(draw("DIAL-0123456789ABCDEFGHIJ", "65535.65535.65535-extra"));
    CHECK_EQ(ink(0, 234, DK518_SIZE, 60), n16);
    span(234, 28, &lo, &hi);
    CHECK_EQ(lo, lo16);
    CHECK_EQ(hi, hi16);
    /* Multi-byte characters are cut between two characters, inside the
     * line's buffer. */
    CHECK(draw("\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4"
               "\xC3\xA4\xC3\xA4\xC3\xA4", "\xC3\xB6\xC3\xB6\xC3\xB6\xC3\xB6"
               "\xC3\xB6\xC3\xB6\xC3\xB6\xC3\xB6\xC3\xB6"));
    CHECK_EQ(s_fb[DK518_SIZE * DK518_SIZE + 1], 0x1234u);
}

TEST_CASE(a_canvas_of_another_size_or_no_argument_draws_nothing)
{
    static gfx_color_t small[240 * 240];
    gfx_canvas_t c;
    const dk518_boot_t b = { "DIAL-3A10", "0.23.18" };
    gfx_canvas_init(&c, small, 240, 240, 0);
    CHECK(!dk518_boot_draw(&c, &b));
    CHECK_EQ(small[120 * 240 + 120], 0u);
    gfx_canvas_init(&c, s_fb + 1, DK518_SIZE, DK518_SIZE - 1, 0);
    CHECK(!dk518_boot_draw(&c, &b));
    CHECK(!dk518_boot_draw(NULL, &b));
    CHECK(!dk518_boot_draw(&s_c, NULL));
}

int main(void)
{
    RUN(the_face_fills_its_canvas_and_writes_nothing_past_it);
    RUN(the_rim_ring_closes_the_face_at_radius_172);
    RUN(every_line_is_centered_on_the_middle_column);
    RUN(a_dial_without_its_name_yet_shows_DIAL_and_no_version_no_line);
    RUN(a_long_name_or_version_is_cut_to_16_characters);
    RUN(a_canvas_of_another_size_or_no_argument_draws_nothing);
    return test_summary("dk518_boot");
}
