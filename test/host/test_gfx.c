/*
 * Host unit tests for the gfx rasteriser.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <limits.h>

#include "gfx.h"

/* Tall enough that a 2x-scaled 8x16 glyph fits without clipping. */
#define W 32
#define H 40

static gfx_color_t s_pixels[W * H];
static gfx_canvas_t s_c;

static void fresh(void)
{
    memset(s_pixels, 0, sizeof(s_pixels));
    gfx_canvas_init(&s_c, s_pixels, W, H, 0);
}

static int count_of(gfx_color_t color)
{
    int n = 0;
    for (int i = 0; i < W * H; ++i) {
        if (s_pixels[i] == color) {
            ++n;
        }
    }
    return n;
}

/* ------------------------------------------------------- rotated text */

/*
 * The watermark is laid over screens that repaint only what changed, so it
 * meets pixels carrying its own output from previous frames.  A blend would
 * compound there: 15% over 15% is 28%, then 39%, until the warning is solid
 * everywhere except the plot area, which repaints its background every
 * frame.  Pins: drawing the watermark again changes nothing.
 */
TEST_CASE(rotated_text_is_idempotent)
{
    fresh();
    gfx_text_rotated(&s_c, W / 2, H / 2, "SIM", &gfx_font_8x16,
                     GFX_WHITE, 1, 38, -30.0f);
    gfx_color_t once[W * H];
    memcpy(once, s_pixels, sizeof(once));

    for (int again = 0; again < 20; ++again) {
        gfx_text_rotated(&s_c, W / 2, H / 2, "SIM", &gfx_font_8x16,
                         GFX_WHITE, 1, 38, -30.0f);
    }
    CHECK_EQ(memcmp(once, s_pixels, sizeof(once)), 0);
    /* And it actually drew something, so the check above is not vacuous. */
    CHECK(count_of(GFX_WHITE) > 0);
}

/*
 * The point list and the drawn pixels are one scan, so they have to agree:
 * every recorded point carries the colour, and there are exactly as many
 * points as pixels drawn.  The watermark replays the list instead of
 * rotating and dividing per pixel over the whole canvas each frame.
 */
TEST_CASE(rotated_text_points_are_the_pixels_it_draws)
{
    fresh();
    gfx_text_rotated(&s_c, W / 2, H / 2, "SIM", &gfx_font_8x16,
                     GFX_WHITE, 1, 38, -30.0f);
    const int drawn = count_of(GFX_WHITE);
    CHECK(drawn > 0);

    uint32_t pts[W * H];
    const int n = gfx_text_rotated_points(&s_c, W / 2, H / 2, "SIM",
                                          &gfx_font_8x16, 1, 38, -30.0f,
                                          pts, (int)(sizeof(pts) / sizeof(pts[0])));
    CHECK_EQ(n, drawn);
    for (int i = 0; i < n; ++i) {
        const int x = (int)(pts[i] & 0xffffu);
        const int y = (int)(pts[i] >> 16);
        CHECK(x >= 0 && x < W && y >= 0 && y < H);
        CHECK_EQ(s_pixels[(size_t)y * W + x], GFX_WHITE);
    }
}

/* A table too small to hold the mark is reported, not overrun. */
TEST_CASE(rotated_text_points_reports_a_table_that_is_too_small)
{
    fresh();
    uint32_t pts[4];
    CHECK_EQ(gfx_text_rotated_points(&s_c, W / 2, H / 2, "SIM",
                                     &gfx_font_8x16, 1, 38, -30.0f, pts, 2),
             -1);
    CHECK_EQ(gfx_text_rotated_points(&s_c, W / 2, H / 2, "SIM",
                                     &gfx_font_8x16, 1, 38, -30.0f, NULL,
                                     (int)(sizeof(pts) / sizeof(pts[0]))),
             -1);
    /* Recording draws nothing. */
    CHECK_EQ(count_of(GFX_WHITE), 0);
}

/* Coverage scales with alpha. */
TEST_CASE(rotated_text_alpha_sets_coverage)
{
    fresh();
    gfx_text_rotated(&s_c, W / 2, H / 2, "SIM", &gfx_font_8x16,
                     GFX_WHITE, 1, 38, -30.0f);
    const int light = count_of(GFX_WHITE);

    fresh();
    gfx_text_rotated(&s_c, W / 2, H / 2, "SIM", &gfx_font_8x16,
                     GFX_WHITE, 1, 200, -30.0f);
    const int heavy = count_of(GFX_WHITE);

    CHECK(heavy > light);
    CHECK(light > 0);
}

/* -------------------------------------------------- seven-segment numerals */

static gfx_seg_style_t seg_style(bool ghost)
{
    gfx_seg_style_t st = { .digit_w = 14, .digit_h = 22, .thickness = 3,
                           .gap = 3, .slant = 0, .ghost = ghost };
    return st;
}

TEST_CASE(seg_digits_light_the_right_segments)
{
    const gfx_seg_style_t st = seg_style(false);

    /* 8 lights all seven, so nothing can cover more; 1 lights two, so
     * nothing can cover less.  Every other digit falls between. */
    fresh();
    gfx_seg_text(&s_c, 1, 1, "8", &st, GFX_RED, GFX_BLUE);
    const int eight = count_of(GFX_RED);
    fresh();
    gfx_seg_text(&s_c, 1, 1, "1", &st, GFX_RED, GFX_BLUE);
    const int one = count_of(GFX_RED);
    CHECK(one > 0);
    CHECK(eight > one);

    for (const char *p = "023456789"; *p; ++p) {
        const char d[2] = { *p, 0 };
        fresh();
        gfx_seg_text(&s_c, 1, 1, d, &st, GFX_RED, GFX_BLUE);
        const int n = count_of(GFX_RED);
        if (n < one || n > eight) {
            T_FAIL("'%c' lit %d pixels, outside 1's %d and 8's %d",
                   *p, n, one, eight);
        }
    }

    /* A space lights nothing but still advances, and an unsupported
     * character draws nothing rather than drawing something wrong. */
    fresh();
    gfx_seg_text(&s_c, 1, 1, " ", &st, GFX_RED, GFX_BLUE);
    CHECK_EQ(count_of(GFX_RED), 0);
    fresh();
    gfx_seg_text(&s_c, 1, 1, "Z", &st, GFX_RED, GFX_BLUE);
    CHECK_EQ(count_of(GFX_RED), 0);
}

TEST_CASE(seg_width_matches_what_it_draws)
{
    const gfx_seg_style_t st = seg_style(false);
    /* A dot takes a narrower cell than a digit, so the two must not be
     * measured the same -- a full cell of whitespace around a decimal point
     * is what makes a rendered number look accidentally spaced. */
    CHECK(gfx_seg_width(".", &st) < gfx_seg_width("8", &st));
    CHECK_EQ(gfx_seg_width("", &st), 0);
    CHECK_EQ(gfx_seg_text(&s_c, 1, 1, "88", &st, GFX_RED, GFX_BLUE),
             gfx_seg_width("88", &st));

    fresh();
    const int drawn = gfx_seg_text(&s_c, 1, 1, "8", &st, GFX_RED, GFX_BLUE);
    CHECK_EQ(drawn, gfx_seg_width("8", &st));
    CHECK_EQ(drawn, st.digit_w);
}

TEST_CASE(the_ghost_shows_the_unlit_segments)
{
    fresh();
    const gfx_seg_style_t plain = seg_style(false);
    gfx_seg_text(&s_c, 1, 1, "1", &plain, GFX_RED, GFX_BLUE);
    CHECK_EQ(count_of(GFX_BLUE), 0);

    fresh();
    const gfx_seg_style_t ghost = seg_style(true);
    gfx_seg_text(&s_c, 1, 1, "1", &ghost, GFX_RED, GFX_BLUE);
    /* The five segments a 1 does not use are visible, and the two it does
     * are drawn in the lit colour over them. */
    CHECK(count_of(GFX_BLUE) > 0);
    CHECK(count_of(GFX_RED) > 0);
}

/* --------------------------------------------------------------- colour */

TEST_CASE(rgb565_packing)
{
    CHECK_EQ(GFX_BLACK, 0x0000);
    CHECK_EQ(GFX_WHITE, 0xFFFF);
    CHECK_EQ(GFX_RED, 0xF800);
    CHECK_EQ(GFX_GREEN, 0x07E0);
    CHECK_EQ(GFX_BLUE, 0x001F);

    uint8_t r, g, b;
    gfx_unpack(GFX_RGB(255, 128, 0), &r, &g, &b);
    CHECK_EQ(r, 255);
    CHECK(g >= 124 && g <= 132);
    CHECK_EQ(b, 0);

    /* NULL outputs must be tolerated. */
    gfx_unpack(GFX_WHITE, NULL, NULL, NULL);
}

TEST_CASE(lerp_endpoints_and_midpoint)
{
    CHECK_EQ(gfx_lerp(GFX_BLACK, GFX_WHITE, 0), GFX_BLACK);
    CHECK_EQ(gfx_lerp(GFX_BLACK, GFX_WHITE, 255), GFX_WHITE);

    gfx_color_t mid = gfx_lerp(GFX_BLACK, GFX_WHITE, 128);
    uint8_t r, g, b;
    gfx_unpack(mid, &r, &g, &b);
    CHECK(r > 110 && r < 145);
    CHECK(g > 110 && g < 145);
    CHECK(b > 110 && b < 145);
}

/* ------------------------------------------------------------- geometry */

TEST_CASE(rect_intersect_cases)
{
    gfx_rect_t out;
    CHECK(gfx_rect_intersect(gfx_rect_make(0, 0, 10, 10),
                             gfx_rect_make(5, 5, 10, 10), &out));
    CHECK_EQ(out.x, 5);
    CHECK_EQ(out.y, 5);
    CHECK_EQ(out.w, 5);
    CHECK_EQ(out.h, 5);

    CHECK(!gfx_rect_intersect(gfx_rect_make(0, 0, 4, 4),
                              gfx_rect_make(10, 10, 4, 4), &out));
    CHECK(gfx_rect_empty(out));

    /* Touching edges do not overlap. */
    CHECK(!gfx_rect_intersect(gfx_rect_make(0, 0, 5, 5),
                              gfx_rect_make(5, 0, 5, 5), NULL));

    CHECK(gfx_rect_contains(gfx_rect_make(2, 3, 4, 5), 2, 3));
    CHECK(gfx_rect_contains(gfx_rect_make(2, 3, 4, 5), 5, 7));
    CHECK(!gfx_rect_contains(gfx_rect_make(2, 3, 4, 5), 6, 7));
    CHECK(!gfx_rect_contains(gfx_rect_make(2, 3, 4, 5), 1, 3));
}

TEST_CASE(canvas_init_and_sub)
{
    fresh();
    CHECK_EQ(s_c.width, W);
    CHECK_EQ(s_c.height, H);
    CHECK_EQ(s_c.stride, W);
    CHECK_EQ(s_c.clip.w, W);

    gfx_canvas_t sub;
    CHECK(gfx_canvas_sub(&s_c, gfx_rect_make(4, 4, 8, 8), &sub));
    CHECK_EQ(sub.width, 8);
    CHECK_EQ(sub.stride, W);
    CHECK(sub.pixels == s_pixels + 4 * W + 4);

    /* Writing into the sub-canvas lands in the parent at the right offset. */
    gfx_fill_rect(&sub, 0, 0, 2, 2, GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 4, 4), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 6, 6), GFX_BLACK);

    /* Fully clipped window. */
    gfx_canvas_t none;
    CHECK(!gfx_canvas_sub(&s_c, gfx_rect_make(100, 100, 4, 4), &none));
    CHECK_EQ(none.width, 0);

    /* Negative sizes are normalised, not trusted. */
    gfx_canvas_t neg;
    gfx_canvas_init(&neg, s_pixels, -5, -5, 0);
    CHECK_EQ(neg.width, 0);
    CHECK_EQ(neg.height, 0);
}

TEST_CASE(clip_set_and_intersect)
{
    fresh();
    CHECK(gfx_clip_set(&s_c, gfx_rect_make(4, 4, 8, 8)));
    CHECK_EQ(gfx_clip_get(&s_c).x, 4);

    CHECK(gfx_clip_intersect(&s_c, gfx_rect_make(8, 8, 8, 8)));
    CHECK_EQ(gfx_clip_get(&s_c).x, 8);
    CHECK_EQ(gfx_clip_get(&s_c).w, 4);

    CHECK(!gfx_clip_intersect(&s_c, gfx_rect_make(0, 0, 2, 2)));

    gfx_clip_reset(&s_c);
    CHECK_EQ(gfx_clip_get(&s_c).w, W);

    /* Clip is intersected with the canvas, never larger than it. */
    CHECK(gfx_clip_set(&s_c, gfx_rect_make(-10, -10, 1000, 1000)));
    CHECK_EQ(gfx_clip_get(&s_c).x, 0);
    CHECK_EQ(gfx_clip_get(&s_c).w, W);
}

/* ----------------------------------------------------------- primitives */

TEST_CASE(pixel_respects_bounds_and_clip)
{
    fresh();
    gfx_pixel(&s_c, 3, 4, GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 3, 4), GFX_RED);

    gfx_pixel(&s_c, -1, 0, GFX_GREEN);
    gfx_pixel(&s_c, W, 0, GFX_GREEN);
    gfx_pixel(&s_c, 0, H, GFX_GREEN);
    CHECK_EQ(count_of(GFX_GREEN), 0);

    gfx_clip_set(&s_c, gfx_rect_make(10, 10, 4, 4));
    gfx_pixel(&s_c, 0, 0, GFX_BLUE);
    gfx_pixel(&s_c, 10, 10, GFX_BLUE);
    CHECK_EQ(count_of(GFX_BLUE), 1);
    gfx_clip_reset(&s_c);

    /* Out-of-range reads are 0, not a crash. */
    CHECK_EQ(gfx_pixel_get(&s_c, -1, -1), 0);
    CHECK_EQ(gfx_pixel_get(&s_c, W, H), 0);
}

TEST_CASE(fill_rect_clips_to_canvas)
{
    fresh();
    gfx_fill_rect(&s_c, -4, -4, 8, 8, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 16);
    CHECK_EQ(gfx_pixel_get(&s_c, 0, 0), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 4, 0), GFX_BLACK);

    fresh();
    gfx_fill_rect(&s_c, W - 2, H - 2, 10, 10, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 4);

    fresh();
    gfx_fill_rect(&s_c, 0, 0, 0, 5, GFX_RED);
    gfx_fill_rect(&s_c, 0, 0, 5, -5, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 0);
}

/* fill_span() has three paths -- memset for colours whose bytes are equal, a
 * 32-bit word loop, and the odd-pixel fixups at either end.  Exercise all of
 * them, at odd offsets and odd widths, and check nothing spills sideways. */
TEST_CASE(span_fill_paths)
{
    const gfx_color_t asym = GFX_RGB(255, 153, 0);   /* bytes differ */
    CHECK((asym & 0xFF) != (asym >> 8));

    for (int x = 0; x < 4; ++x) {
        for (int w = 1; w < 9; ++w) {
            fresh();
            gfx_fill_rect(&s_c, x + 4, 3, w, 2, asym);
            CHECK_EQ(count_of(asym), w * 2);
            CHECK_EQ(gfx_pixel_get(&s_c, x + 4, 3), asym);
            CHECK_EQ(gfx_pixel_get(&s_c, x + 4 + w - 1, 4), asym);
            CHECK_EQ(gfx_pixel_get(&s_c, x + 3, 3), GFX_BLACK);
            CHECK_EQ(gfx_pixel_get(&s_c, x + 4 + w, 3), GFX_BLACK);
        }
    }

    /* The memset path, same geometry. */
    fresh();
    gfx_fill_rect(&s_c, 5, 3, 7, 2, GFX_WHITE);
    CHECK_EQ(count_of(GFX_WHITE), 14);
    CHECK_EQ(gfx_pixel_get(&s_c, 4, 3), GFX_BLACK);
    CHECK_EQ(gfx_pixel_get(&s_c, 12, 3), GFX_BLACK);

    /* Whole-canvas fill takes the contiguous fast path; every pixel and only
     * those pixels. */
    fresh();
    gfx_fill_rect(&s_c, 0, 0, W, H, asym);
    CHECK_EQ(count_of(asym), W * H);
}

TEST_CASE(clear_honours_clip)
{
    fresh();
    gfx_clip_set(&s_c, gfx_rect_make(2, 2, 3, 4));
    gfx_clear(&s_c, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 12);
    gfx_clip_reset(&s_c);
    gfx_clear(&s_c, GFX_WHITE);
    CHECK_EQ(count_of(GFX_WHITE), W * H);
}

TEST_CASE(lines_cover_expected_pixels)
{
    fresh();
    gfx_hline(&s_c, 2, 3, 5, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 5);
    CHECK_EQ(gfx_pixel_get(&s_c, 2, 3), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 6, 3), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 7, 3), GFX_BLACK);

    /* Negative extents run backwards from the anchor. */
    fresh();
    gfx_hline(&s_c, 10, 0, -3, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 3);
    CHECK_EQ(gfx_pixel_get(&s_c, 8, 0), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 10, 0), GFX_RED);

    fresh();
    gfx_vline(&s_c, 4, 10, -3, GFX_GREEN);
    CHECK_EQ(count_of(GFX_GREEN), 3);
    CHECK_EQ(gfx_pixel_get(&s_c, 4, 8), GFX_GREEN);

    /* A diagonal hits both endpoints and exactly one pixel per column. */
    fresh();
    gfx_line(&s_c, 0, 0, 9, 9, GFX_BLUE);
    CHECK_EQ(count_of(GFX_BLUE), 10);
    CHECK_EQ(gfx_pixel_get(&s_c, 0, 0), GFX_BLUE);
    CHECK_EQ(gfx_pixel_get(&s_c, 9, 9), GFX_BLUE);

    /* Axis-aligned lines take the fast path but must still hit both ends. */
    fresh();
    gfx_line(&s_c, 9, 2, 2, 2, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 8);
    fresh();
    gfx_line(&s_c, 3, 9, 3, 2, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 8);

    /* Shallow slope: one pixel per column, both endpoints present. */
    fresh();
    gfx_line(&s_c, 0, 0, 10, 3, GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 0, 0), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 10, 3), GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 11);
}

TEST_CASE(thick_line_is_wider_than_thin)
{
    fresh();
    gfx_line(&s_c, 2, 2, 20, 12, GFX_RED);
    int thin = count_of(GFX_RED);

    fresh();
    gfx_thick_line(&s_c, 2, 2, 20, 12, 5, GFX_RED);
    int thick = count_of(GFX_RED);
    CHECK(thick > thin * 3);

    /* thickness <= 1 degrades to the plain line. */
    fresh();
    gfx_thick_line(&s_c, 2, 2, 20, 12, 1, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), thin);
}

TEST_CASE(draw_rect_outline_only)
{
    fresh();
    gfx_draw_rect(&s_c, 2, 2, 6, 5, GFX_RED);
    /* perimeter of a 6x5 box = 2*6 + 2*5 - 4 */
    CHECK_EQ(count_of(GFX_RED), 18);
    CHECK_EQ(gfx_pixel_get(&s_c, 2, 2), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 7, 6), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 4, 4), GFX_BLACK);

    gfx_draw_rect(&s_c, 0, 0, 0, 0, GFX_GREEN);
    CHECK_EQ(count_of(GFX_GREEN), 0);
}

TEST_CASE(circles_are_symmetric)
{
    fresh();
    gfx_fill_circle(&s_c, 12, 12, 6, GFX_RED);
    for (int dy = -6; dy <= 6; ++dy) {
        for (int dx = -6; dx <= 6; ++dx) {
            gfx_color_t a = gfx_pixel_get(&s_c, 12 + dx, 12 + dy);
            CHECK_EQ(a, gfx_pixel_get(&s_c, 12 - dx, 12 + dy));
            CHECK_EQ(a, gfx_pixel_get(&s_c, 12 + dx, 12 - dy));
        }
    }
    /* Centre filled, well outside the radius not. */
    CHECK_EQ(gfx_pixel_get(&s_c, 12, 12), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 12 + 7, 12 + 7), GFX_BLACK);

    fresh();
    gfx_draw_circle(&s_c, 12, 12, 6, GFX_GREEN);
    CHECK_EQ(gfx_pixel_get(&s_c, 12, 12), GFX_BLACK);
    CHECK_EQ(gfx_pixel_get(&s_c, 12, 6), GFX_GREEN);
    CHECK_EQ(gfx_pixel_get(&s_c, 18, 12), GFX_GREEN);

    /* Degenerate radii. */
    fresh();
    gfx_fill_circle(&s_c, 5, 5, 0, GFX_RED);
    gfx_draw_circle(&s_c, 9, 9, 0, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 2);
    gfx_fill_circle(&s_c, 5, 5, -3, GFX_GREEN);
    gfx_draw_circle(&s_c, 5, 5, -3, GFX_GREEN);
    CHECK_EQ(count_of(GFX_GREEN), 0);
}

TEST_CASE(round_rect_rounds_the_corners)
{
    fresh();
    gfx_fill_round_rect(&s_c, 2, 2, 16, 12, 5, GFX_RED);
    /* Corner pixel is cut away, edge midpoints are not. */
    CHECK_EQ(gfx_pixel_get(&s_c, 2, 2), GFX_BLACK);
    CHECK_EQ(gfx_pixel_get(&s_c, 17, 13), GFX_BLACK);
    CHECK_EQ(gfx_pixel_get(&s_c, 10, 2), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 2, 8), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 10, 8), GFX_RED);
    /* Nothing escaped the requested box. */
    CHECK_EQ(gfx_pixel_get(&s_c, 1, 8), GFX_BLACK);
    CHECK_EQ(gfx_pixel_get(&s_c, 18, 8), GFX_BLACK);

    /* radius 0 is a plain rectangle; oversized radius clamps to a stadium. */
    fresh();
    gfx_fill_round_rect(&s_c, 0, 0, 6, 4, 0, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 24);

    fresh();
    gfx_fill_round_rect(&s_c, 0, 0, 10, 10, 99, GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 0, 0), GFX_BLACK);
    CHECK_EQ(gfx_pixel_get(&s_c, 5, 5), GFX_RED);

    fresh();
    gfx_draw_round_rect(&s_c, 2, 2, 16, 12, 5, GFX_GREEN);
    CHECK_EQ(gfx_pixel_get(&s_c, 2, 2), GFX_BLACK);
    CHECK_EQ(gfx_pixel_get(&s_c, 10, 2), GFX_GREEN);
    CHECK_EQ(gfx_pixel_get(&s_c, 10, 8), GFX_BLACK);

    fresh();
    gfx_draw_round_rect(&s_c, 0, 0, 6, 4, 0, GFX_GREEN);
    CHECK_EQ(count_of(GFX_GREEN), 16);

    /* Degenerate sizes are no-ops. */
    fresh();
    gfx_fill_round_rect(&s_c, 0, 0, 0, 5, 2, GFX_RED);
    gfx_draw_round_rect(&s_c, 0, 0, 5, 0, 2, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 0);
}

TEST_CASE(gradient_runs_from_a_to_b)
{
    fresh();
    gfx_fill_rect_gradient(&s_c, 0, 0, 4, 8, GFX_BLACK, GFX_WHITE);
    CHECK_EQ(gfx_pixel_get(&s_c, 0, 0), GFX_BLACK);
    CHECK_EQ(gfx_pixel_get(&s_c, 3, 7), GFX_WHITE);
    CHECK(gfx_pixel_get(&s_c, 0, 4) != GFX_BLACK);

    /* A single-row gradient is colour a only. */
    fresh();
    gfx_fill_rect_gradient(&s_c, 0, 0, 4, 1, GFX_RED, GFX_WHITE);
    CHECK_EQ(gfx_pixel_get(&s_c, 0, 0), GFX_RED);

    gfx_fill_rect_gradient(&s_c, 0, 0, 0, 0, GFX_RED, GFX_WHITE);
}

/* ----------------------------------------------------------------- blits */

TEST_CASE(blit_copies_and_clips)
{
    static gfx_color_t src[4 * 4];
    for (int i = 0; i < 16; ++i) {
        src[i] = (gfx_color_t)(0x1000 + i);
    }

    fresh();
    gfx_blit(&s_c, 5, 5, src, 4, 4, 0);
    CHECK_EQ(gfx_pixel_get(&s_c, 5, 5), 0x1000);
    CHECK_EQ(gfx_pixel_get(&s_c, 8, 8), 0x100F);

    /* Partly off the left/top edge: only the visible part is copied, and the
     * source is offset to match. */
    fresh();
    gfx_blit(&s_c, -2, -2, src, 4, 4, 0);
    CHECK_EQ(gfx_pixel_get(&s_c, 0, 0), 0x100A);
    CHECK_EQ(gfx_pixel_get(&s_c, 1, 1), 0x100F);

    /* Entirely outside: nothing happens. */
    fresh();
    gfx_blit(&s_c, 100, 100, src, 4, 4, 0);
    gfx_blit(&s_c, 0, 0, NULL, 4, 4, 0);
    gfx_blit(&s_c, 0, 0, src, 0, 0, 0);
    for (int i = 0; i < W * H; ++i) {
        CHECK_EQ(s_pixels[i], 0);
    }
}

TEST_CASE(blit_key_skips_transparent_pixels)
{
    static gfx_color_t src[2 * 2] = { GFX_RED, GFX_MAGENTA, GFX_MAGENTA, GFX_BLUE };

    fresh();
    gfx_clear(&s_c, GFX_WHITE);
    gfx_blit_key(&s_c, 0, 0, src, 2, 2, 0, GFX_MAGENTA);
    CHECK_EQ(gfx_pixel_get(&s_c, 0, 0), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 1, 0), GFX_WHITE);
    CHECK_EQ(gfx_pixel_get(&s_c, 0, 1), GFX_WHITE);
    CHECK_EQ(gfx_pixel_get(&s_c, 1, 1), GFX_BLUE);

    gfx_blit_key(&s_c, 100, 100, src, 2, 2, 0, GFX_MAGENTA);
    gfx_blit_key(&s_c, 0, 0, NULL, 2, 2, 0, GFX_MAGENTA);
}

TEST_CASE(blit_1bpp_expands_a_mask)
{
    /* 9 pixels wide -> two bytes per row, second byte mostly padding. */
    static const uint8_t bits[] = {
        0xFF, 0x80,
        0x00, 0x80,
    };
    fresh();
    gfx_blit_1bpp(&s_c, 1, 1, bits, 9, 2, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 10);
    CHECK_EQ(gfx_pixel_get(&s_c, 1, 1), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 9, 1), GFX_RED);
    CHECK_EQ(gfx_pixel_get(&s_c, 1, 2), GFX_BLACK);
    CHECK_EQ(gfx_pixel_get(&s_c, 9, 2), GFX_RED);

    gfx_blit_1bpp(&s_c, 0, 0, NULL, 4, 4, GFX_RED);
    gfx_blit_1bpp(&s_c, 0, 0, bits, 0, 0, GFX_RED);
}

/* ------------------------------------------------------------------ text */

TEST_CASE(font_metrics)
{
    CHECK_EQ(gfx_font_8x16.width, 8);
    CHECK_EQ(gfx_font_8x16.height, 16);
    CHECK_EQ(gfx_font_8x16.first, 0x20);
    CHECK_EQ(gfx_font_8x16.last, 0x7E);

    CHECK_EQ(gfx_text_width(&gfx_font_8x16, "ABC", 1), 24);
    CHECK_EQ(gfx_text_width(&gfx_font_8x16, "ABC", 2), 48);
    CHECK_EQ(gfx_text_width(&gfx_font_8x16, "", 1), 0);
    CHECK_EQ(gfx_text_height(&gfx_font_8x16, 3), 48);

    /* scale < 1 is treated as 1; NULL arguments return 0. */
    CHECK_EQ(gfx_text_width(&gfx_font_8x16, "AB", 0), 16);
    CHECK_EQ(gfx_text_width(NULL, "AB", 1), 0);
    CHECK_EQ(gfx_text_width(&gfx_font_8x16, NULL, 1), 0);
    CHECK_EQ(gfx_text_height(NULL, 1), 0);
}

TEST_CASE(text_draws_and_scales)
{
    fresh();
    int adv = gfx_text(&s_c, 0, 0, "A", &gfx_font_8x16, GFX_RED, 1);
    CHECK_EQ(adv, 8);
    int at_1x = count_of(GFX_RED);
    CHECK(at_1x > 8);

    fresh();
    gfx_text(&s_c, 0, 0, "A", &gfx_font_8x16, GFX_RED, 2);
    CHECK_EQ(count_of(GFX_RED), at_1x * 4);

    /* A space marks no pixels but still advances. */
    fresh();
    CHECK_EQ(gfx_text(&s_c, 0, 0, " ", &gfx_font_8x16, GFX_RED, 1), 8);
    CHECK_EQ(count_of(GFX_RED), 0);

    /* Code points outside the font fall back to '?'. */
    fresh();
    gfx_char(&s_c, 0, 0, (char)0x01, &gfx_font_8x16, GFX_RED, 1);
    int fallback = count_of(GFX_RED);
    fresh();
    gfx_char(&s_c, 0, 0, '?', &gfx_font_8x16, GFX_RED, 1);
    CHECK_EQ(fallback, count_of(GFX_RED));

    CHECK_EQ(gfx_text(&s_c, 0, 0, NULL, &gfx_font_8x16, GFX_RED, 1), 0);
    CHECK_EQ(gfx_char(&s_c, 0, 0, 'A', NULL, GFX_RED, 1), 0);
}

TEST_CASE(text_bg_paints_the_cell)
{
    fresh();
    gfx_text_bg(&s_c, 0, 0, "AB", &gfx_font_8x16, GFX_RED, GFX_BLUE, 1);
    /*
     * Every pixel of both cells is painted; none is left as the canvas was.
     * With an antialiased face the glyph edges land between ink and
     * background, so the check is "painted", not "ink or background".
     */
    int painted = 0;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (s_pixels[y * W + x] != 0) {
                ++painted;
            }
        }
    }
    CHECK_EQ(painted, 16 * 16);
    CHECK(count_of(GFX_RED) > 0);
    CHECK(count_of(GFX_BLUE) > 0);
    /* And the edges really are intermediate, not one or the other. */
    CHECK(count_of(GFX_RED) + count_of(GFX_BLUE) < 16 * 16);
    CHECK_EQ(gfx_text_bg(&s_c, 0, 0, NULL, &gfx_font_8x16, GFX_RED, GFX_BLUE, 1), 0);
}

TEST_CASE(text_in_aligns_and_clips)
{
    fresh();
    gfx_rect_t box = gfx_rect_make(0, 0, 32, 16);

    gfx_text_in(&s_c, box, "A", &gfx_font_8x16, GFX_RED, 1, GFX_ALIGN_LEFT);
    int left = 0, right = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (s_pixels[y * W + x] == GFX_RED) {
                (x < 16) ? ++left : ++right;
            }
        }
    }
    CHECK(left > 0);
    CHECK_EQ(right, 0);

    fresh();
    gfx_text_in(&s_c, box, "A", &gfx_font_8x16, GFX_RED, 1, GFX_ALIGN_RIGHT);
    CHECK(gfx_pixel_get(&s_c, 4, 8) == GFX_BLACK);
    left = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 24; x < W; ++x) {
            if (s_pixels[y * W + x] == GFX_RED) {
                ++left;
            }
        }
    }
    CHECK(left > 0);

    fresh();
    gfx_text_in(&s_c, box, "A", &gfx_font_8x16, GFX_RED, 1, GFX_ALIGN_CENTER);
    CHECK(count_of(GFX_RED) > 0);

    /* The clip is restored afterwards. */
    CHECK_EQ(gfx_clip_get(&s_c).w, W);

    /* Text longer than the box is cut off, not wrapped or overflowed. */
    fresh();
    gfx_text_in(&s_c, gfx_rect_make(0, 0, 8, 16), "AAAA", &gfx_font_8x16,
                GFX_RED, 1, GFX_ALIGN_LEFT);
    for (int y = 0; y < H; ++y) {
        for (int x = 8; x < W; ++x) {
            CHECK_EQ(s_pixels[y * W + x], GFX_BLACK);
        }
    }

    /* An empty box draws nothing and still restores the clip. */
    gfx_text_in(&s_c, gfx_rect_make(100, 100, 4, 4), "A", &gfx_font_8x16,
                GFX_RED, 1, GFX_ALIGN_LEFT);
    CHECK_EQ(gfx_clip_get(&s_c).w, W);
    gfx_text_in(&s_c, box, NULL, &gfx_font_8x16, GFX_RED, 1, GFX_ALIGN_LEFT);
}

TEST_CASE(null_canvas_is_survivable)
{
    gfx_canvas_init(NULL, NULL, 4, 4, 0);
    gfx_clip_reset(NULL);
    CHECK(!gfx_clip_set(NULL, gfx_rect_make(0, 0, 1, 1)));
    CHECK(!gfx_clip_intersect(NULL, gfx_rect_make(0, 0, 1, 1)));
    CHECK(!gfx_canvas_sub(NULL, gfx_rect_make(0, 0, 1, 1), NULL));
    CHECK_EQ(gfx_pixel_get(NULL, 0, 0), 0);

    gfx_canvas_t empty;
    gfx_canvas_init(&empty, NULL, 4, 4, 0);
    gfx_pixel(&empty, 0, 0, GFX_RED);
    gfx_fill_rect(&empty, 0, 0, 4, 4, GFX_RED);
    gfx_clear(&empty, GFX_RED);
    gfx_fill_rect_gradient(&empty, 0, 0, 4, 4, GFX_RED, GFX_BLUE);
    gfx_blit(&empty, 0, 0, s_pixels, 2, 2, 0);
    gfx_blit_key(&empty, 0, 0, s_pixels, 2, 2, 0, GFX_RED);
    gfx_blit_1bpp(&empty, 0, 0, (const uint8_t *)"\x80", 1, 1, GFX_RED);
}

/*
 * The far edges of the coordinate system.  Every primitive takes int and every
 * rect field is int16_t; a plain cast would draw an off-screen shape at a
 * wrapped position and give gfx_blit a source offset outside the caller's
 * buffer, so coordinates saturate instead.
 */
TEST_CASE(out_of_range_geometry_saturates_instead_of_wrapping)
{
    fresh();
    gfx_fill_rect(&s_c, 65536, 0, 4, 4, GFX_GREEN);
    CHECK_EQ(count_of(GFX_GREEN), 0);

    fresh();
    gfx_fill_rect(&s_c, -65536, 0, 4, 4, GFX_GREEN);
    CHECK_EQ(count_of(GFX_GREEN), 0);

    /* An oversized width still covers the canvas rather than wrapping to a
     * few columns. */
    fresh();
    gfx_fill_rect(&s_c, 0, 0, 65540, 2, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), W * 2);

    /* A blit whose origin is out of range must draw nothing, not read 128 KiB
     * before its source. */
    static gfx_color_t src[4] = { GFX_BLUE, GFX_BLUE, GFX_BLUE, GFX_BLUE };
    fresh();
    gfx_blit(&s_c, 65536, 0, src, 2, 2, 2);
    gfx_blit_key(&s_c, 65536, 0, src, 2, 2, 2, GFX_BLACK);
    CHECK_EQ(count_of(GFX_BLUE), 0);

    /* -INT_MIN is undefined; the backwards-run rule stops short of it. */
    fresh();
    gfx_hline(&s_c, 0, 0, INT_MIN, GFX_RED);
    gfx_vline(&s_c, 0, 0, INT_MIN, GFX_RED);
    CHECK_EQ(count_of(GFX_RED), 0);
}

/* A stride narrower than the canvas would put the tail of every row in the
 * next row's memory; a sub-canvas must not regain what its parent clipped. */
TEST_CASE(degenerate_canvas_geometry_is_clamped)
{
    gfx_canvas_t narrow;
    gfx_canvas_init(&narrow, s_pixels, W, H, W / 2);
    CHECK_EQ(narrow.width, W / 2);
    CHECK_EQ(narrow.stride, W / 2);

    fresh();
    gfx_clip_set(&s_c, gfx_rect_make(0, 0, 8, 8));
    gfx_canvas_t sub;
    CHECK(gfx_canvas_sub(&s_c, gfx_rect_make(0, 0, W, H), &sub));
    gfx_clear(&sub, GFX_RED);
    /* The parent allowed 8x8 and no more. */
    CHECK_EQ(count_of(GFX_RED), 64);
}

/* The numeric face has no '?'.  An out-of-range code point draws something
 * visible rather than consuming its advance and leaving a gap. */
TEST_CASE(a_missing_glyph_is_visible)
{
    fresh();
    int adv = gfx_char(&s_c, 0, 0, 'Z', &gfx_font_num_24x30, GFX_RED, 1);
    CHECK_EQ(adv, gfx_font_num_24x30.width);
    CHECK(count_of(GFX_RED) > 0);
}

/*
 * Every pixel is clipped on the way out, so an absurd endpoint is safe for
 * memory, but Bresenham walks one step per pixel and a coordinate near
 * INT_MIN is billions of iterations.  The line is bounded to the clip before
 * it is walked.
 */
TEST_CASE(a_line_with_absurd_endpoints_is_bounded_and_stays_in_the_clip)
{
    fresh();
    gfx_line(&s_c, INT_MIN, INT_MIN, INT_MAX, INT_MAX, GFX_RED);
    gfx_line(&s_c, INT_MAX, 0, INT_MIN, H - 1, GFX_GREEN);
    /* It crosses the canvas, so it must actually have drawn something. */
    CHECK(count_of(GFX_RED) + count_of(GFX_GREEN) > 0);

    /* A line wholly off one side draws nothing at all. */
    fresh();
    gfx_line(&s_c, -500, -400, -10, -300, GFX_BLUE);
    gfx_line(&s_c, W + 10, 0, W + 900, H, GFX_BLUE);
    gfx_line(&s_c, 0, H + 5, W, H + 900, GFX_BLUE);
    CHECK_EQ(count_of(GFX_BLUE), 0);

    /* And nothing escapes a narrowed clip. */
    fresh();
    gfx_clip_set(&s_c, gfx_rect_make(4, 4, 8, 8));
    gfx_line(&s_c, INT_MIN, INT_MIN, INT_MAX, INT_MAX, GFX_RED);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (x >= 4 && x < 12 && y >= 4 && y < 12) {
                continue;
            }
            if (gfx_pixel_get(&s_c, x, y) != 0) {
                T_FAIL("wrote outside the clip at %d,%d", x, y);
                return;
            }
        }
    }
}

int main(void)
{
    RUN(rgb565_packing);
    RUN(lerp_endpoints_and_midpoint);
    RUN(rect_intersect_cases);
    RUN(canvas_init_and_sub);
    RUN(clip_set_and_intersect);
    RUN(pixel_respects_bounds_and_clip);
    RUN(fill_rect_clips_to_canvas);
    RUN(span_fill_paths);
    RUN(clear_honours_clip);
    RUN(lines_cover_expected_pixels);
    RUN(thick_line_is_wider_than_thin);
    RUN(draw_rect_outline_only);
    RUN(circles_are_symmetric);
    RUN(round_rect_rounds_the_corners);
    RUN(gradient_runs_from_a_to_b);
    RUN(blit_copies_and_clips);
    RUN(blit_key_skips_transparent_pixels);
    RUN(blit_1bpp_expands_a_mask);
    RUN(font_metrics);
    RUN(text_draws_and_scales);
    RUN(text_bg_paints_the_cell);
    RUN(text_in_aligns_and_clips);
    RUN(null_canvas_is_survivable);
    RUN(out_of_range_geometry_saturates_instead_of_wrapping);
    RUN(degenerate_canvas_geometry_is_clamped);
    RUN(a_missing_glyph_is_visible);
    RUN(a_line_with_absurd_endpoints_is_bounded_and_stays_in_the_clip);
    RUN(seg_digits_light_the_right_segments);
    RUN(seg_width_matches_what_it_draws);
    RUN(the_ghost_shows_the_unlit_segments);
    RUN(rotated_text_is_idempotent);
    RUN(rotated_text_points_are_the_pixels_it_draws);
    RUN(rotated_text_points_reports_a_table_that_is_too_small);
    RUN(rotated_text_alpha_sets_coverage);
    return test_summary("gfx");
}
