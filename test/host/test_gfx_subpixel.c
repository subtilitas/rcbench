/*
 * Host unit tests for gfx_subpixel.h: the disc, the capsule and the ring
 * placed in fractions of a pixel.
 *
 * Coverage is read back from the green channel of white drawn on black:
 * 0 to 63 in RGB565.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <math.h>

#include "gfx.h"
#include "gfx_subpixel.h"

#define W 48
#define H 48

static gfx_color_t s_pixels[W * H];
static gfx_color_t s_other[W * H];
static gfx_canvas_t s_c;

/** @brief The canvas cleared to black, the clip the whole of it. */
static void fresh(void)
{
    memset(s_pixels, 0, sizeof(s_pixels));
    gfx_canvas_init(&s_c, s_pixels, W, H, 0);
}

/** @brief A canvas over @p px with a pattern under the shapes, so a blend
 *         that reads the pixel back differs from one that does not. */
static void patterned(gfx_color_t *px)
{
    for (int i = 0; i < W * H; ++i) {
        px[i] = (gfx_color_t)(i * 2654435761u >> 16);
    }
    gfx_canvas_init(&s_c, px, W, H, 0);
}

/** @brief The coverage of pixel @p i, 0 to 1. */
static double cov_at(int i)
{
    return (double)((s_pixels[i] >> 5) & 63u) / 63.0;
}

/** @brief The sum of the canvas's coverage and its centroid. */
static double coverage(double *cx, double *cy)
{
    double sum = 0.0, sx = 0.0, sy = 0.0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double w = cov_at(y * W + x);
            sum += w;
            sx += w * x;
            sy += w * y;
        }
    }
    if (cx != NULL) {
        *cx = sx / sum;
    }
    if (cy != NULL) {
        *cy = sy / sum;
    }
    return sum;
}

/* ----------------------------------------------- the whole-pixel shapes */

/* The whole-pixel disc and capsule are drawn through the float ones: at
 * whole numbers the two give the same pixels, over a background they blend
 * with, and every other screen's picture stays as it was. */
TEST_CASE(at_whole_pixels_the_float_shapes_are_the_int_ones)
{
    patterned(s_pixels);
    gfx_fill_circle_aa(&s_c, 20, 18, 7, GFX_YELLOW);
    gfx_capsule_aa(&s_c, 5, 6, 37, 30, 9, GFX_CYAN);
    gfx_fill_circle_aa(&s_c, -2, 44, 6, GFX_RED);       /* off two edges */
    gfx_capsule_aa(&s_c, 40, -3, 52, 20, 26, GFX_BLUE);
    patterned(s_other);
    gfx_fill_circle_aaf(&s_c, 20.0f, 18.0f, 7.0f, GFX_YELLOW);
    gfx_capsule_aaf(&s_c, 5.0f, 6.0f, 37.0f, 30.0f, 9.0f, GFX_CYAN);
    gfx_fill_circle_aaf(&s_c, -2.0f, 44.0f, 6.0f, GFX_RED);
    gfx_capsule_aaf(&s_c, 40.0f, -3.0f, 52.0f, 20.0f, 26.0f, GFX_BLUE);
    CHECK_EQ(memcmp(s_pixels, s_other, sizeof(s_pixels)), 0);
}

/* ------------------------------------------------------------ the disc */

/* A disc moved by tenths of a pixel moves its picture by tenths: the
 * centroid of its coverage is its centre to 0.02 px at every step, where
 * a disc on whole pixels stands still and then jumps a pixel. */
TEST_CASE(a_disc_moves_by_fractions_of_a_pixel)
{
    for (int k = 0; k <= 10; ++k) {
        fresh();
        const float x = 20.0f + 0.1f * (float)k;
        const float y = 23.5f - 0.07f * (float)k;
        gfx_fill_circle_aaf(&s_c, x, y, 5.0f, GFX_WHITE);
        double cx, cy;
        const double sum = coverage(&cx, &cy);
        CHECK(fabs(cx - x) < 0.02);
        CHECK(fabs(cy - y) < 0.02);
        /* Its area stays pi r^2 wherever it sits. */
        CHECK(fabs(sum - 3.14159265 * 25.0) < 0.8);
    }
}

/* ---------------------------------------------------------- the capsule */

/* A capsule's end moved along it by a quarter of a pixel adds about a
 * quarter of its width to its area, step by step, and never takes any
 * away. */
TEST_CASE(a_capsule_grows_by_fractions_of_a_pixel)
{
    fresh();
    gfx_capsule_aaf(&s_c, 8.0f, 24.0f, 28.0f, 24.0f, 6.0f, GFX_WHITE);
    const double base = coverage(NULL, NULL);
    double last = 0.0;
    for (int k = 1; k <= 8; ++k) {
        fresh();
        const float f = 0.25f * (float)k;
        gfx_capsule_aaf(&s_c, 8.0f, 24.0f, 28.0f + f, 24.0f, 6.0f, GFX_WHITE);
        const double grown = coverage(NULL, NULL) - base;
        CHECK(fabs(grown - 6.0 * f) < 0.35);
        CHECK(grown > last);
        last = grown;
    }
}

/* A capsule of no width and a canvas that is not there draw nothing. */
TEST_CASE(a_capsule_of_no_width_draws_nothing)
{
    fresh();
    gfx_capsule_aaf(&s_c, 8.0f, 8.0f, 30.0f, 30.0f, 0.0f, GFX_WHITE);
    gfx_capsule_aaf(&s_c, 8.0f, 8.0f, 30.0f, 30.0f, -3.0f, GFX_WHITE);
    gfx_capsule_aaf(&s_c, 8.0f, 8.0f, 30.0f, 30.0f, NAN, GFX_WHITE);
    gfx_fill_circle_aaf(&s_c, 20.0f, 20.0f, 0.0f, GFX_WHITE);
    gfx_fill_circle_aaf(&s_c, 20.0f, 20.0f, NAN, GFX_WHITE);
    gfx_capsule_aaf(NULL, 8.0f, 8.0f, 30.0f, 30.0f, 4.0f, GFX_WHITE);
    gfx_fill_circle_aaf(NULL, 20.0f, 20.0f, 4.0f, GFX_WHITE);
    CHECK(coverage(NULL, NULL) == 0.0);
}

/* ------------------------------------------------------------- the ring */

/* A whole ring has its band's area, 2 pi r t, and its centroid is its
 * centre to 0.05 px, wherever between pixels the centre sits. */
TEST_CASE(a_ring_has_its_bands_area_and_centre)
{
    for (int k = 0; k <= 4; ++k) {
        fresh();
        const float x = 23.0f + 0.23f * (float)k;
        const float y = 22.6f + 0.11f * (float)k;
        gfx_ring_aaf(&s_c, x, y, 12.0f, 3.0f, 0.0f, 360.0f, GFX_WHITE,
                     GFX_WHITE, 0.0f);
        double cx, cy;
        const double sum = coverage(&cx, &cy);
        CHECK(fabs(sum - 2.0 * 3.14159265 * 12.0 * 3.0) < 2.0);
        CHECK(fabs(cx - x) < 0.05);
        CHECK(fabs(cy - y) < 0.05);
    }
}

/* An arc keeps to its span: from 0 to 90 degrees, up being +90, it lights
 * the upper right quarter and nothing past half a pixel beyond its ends;
 * its middle is the full colour. */
TEST_CASE(an_arc_keeps_to_its_span)
{
    fresh();
    const float cx = 20.0f, cy = 26.0f;
    gfx_ring_aaf(&s_c, cx, cy, 14.0f, 3.0f, 0.0f, 90.0f, GFX_WHITE,
                 GFX_WHITE, 0.0f);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (s_pixels[y * W + x] == 0u) {
                continue;
            }
            CHECK((float)x >= cx - 1.0f);
            CHECK((float)y <= cy + 1.0f);
        }
    }
    /* 45 degrees on the band: x = cx + 9.9, y = cy - 9.9. */
    CHECK_EQ(s_pixels[16 * W + 30], GFX_WHITE);
    /* And the area is a quarter of the ring's, to the half pixel of each
     * blended end. */
    const double sum = coverage(NULL, NULL);
    CHECK(fabs(sum - 0.25 * 2.0 * 3.14159265 * 14.0 * 3.0) < 3.0);
}

/* The fade runs from @p into at each end to the colour in the middle. */
TEST_CASE(an_arc_fades_from_its_ends)
{
    fresh();
    gfx_ring_aaf(&s_c, 24.0f, 24.0f, 15.0f, 3.0f, 0.0f, 180.0f, GFX_WHITE,
                 GFX_RED, 60.0f);
    /* The top of the arc, 90 degrees, 30 past the fade: white. */
    CHECK_EQ(s_pixels[9 * W + 24], GFX_WHITE);
    /* Four degrees in from the end at 0 degrees: nearly the red. */
    const gfx_color_t near_end = s_pixels[(24 - 1) * W + 39];
    CHECK(((near_end >> 11) & 31u) == 31u);
    CHECK(((near_end >> 5) & 63u) < 8u);
}

/* A span given in either order, or a turn earlier, is the same arc. */
TEST_CASE(an_arc_is_the_same_either_way_round)
{
    fresh();
    gfx_ring_aaf(&s_c, 24.0f, 24.0f, 15.0f, 3.0f, 200.0f, 470.0f, GFX_WHITE,
                 GFX_WHITE, 0.0f);
    memcpy(s_other, s_pixels, sizeof(s_pixels));
    fresh();
    gfx_ring_aaf(&s_c, 24.0f, 24.0f, 15.0f, 3.0f, 470.0f, 200.0f, GFX_WHITE,
                 GFX_WHITE, 0.0f);
    CHECK_EQ(memcmp(s_pixels, s_other, sizeof(s_pixels)), 0);
    /* A turn earlier is the same arc, to the rounding of its angles. */
    const double want = coverage(NULL, NULL);
    fresh();
    gfx_ring_aaf(&s_c, 24.0f, 24.0f, 15.0f, 3.0f, -160.0f, 110.0f,
                 GFX_WHITE, GFX_WHITE, 0.0f);
    CHECK(fabs(coverage(NULL, NULL) - want) < 0.05);
}

/* A ring drawn under a clip touches nothing outside it, and inside it the
 * same pixels as unclipped. */
TEST_CASE(a_ring_keeps_to_the_clip)
{
    fresh();
    gfx_ring_aaf(&s_c, 24.3f, 23.8f, 15.0f, 3.0f, 0.0f, 360.0f, GFX_WHITE,
                 GFX_WHITE, 0.0f);
    memcpy(s_other, s_pixels, sizeof(s_pixels));
    fresh();
    const gfx_rect_t box = { 10, 4, 20, 17 };
    CHECK(gfx_clip_set(&s_c, box));
    gfx_ring_aaf(&s_c, 24.3f, 23.8f, 15.0f, 3.0f, 0.0f, 360.0f, GFX_WHITE,
                 GFX_WHITE, 0.0f);
    int inside = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int i = y * W + x;
            if (gfx_rect_contains(box, x, y)) {
                CHECK_EQ(s_pixels[i], s_other[i]);
                inside += (s_pixels[i] != 0u) ? 1 : 0;
            } else {
                CHECK_EQ(s_pixels[i], 0u);
            }
        }
    }
    CHECK(inside > 0);
}

/* No radius, no width, no canvas: nothing drawn. */
TEST_CASE(a_ring_of_nothing_draws_nothing)
{
    fresh();
    gfx_ring_aaf(&s_c, 24.0f, 24.0f, 0.0f, 3.0f, 0.0f, 360.0f, GFX_WHITE,
                 GFX_WHITE, 0.0f);
    gfx_ring_aaf(&s_c, 24.0f, 24.0f, 10.0f, 0.0f, 0.0f, 360.0f, GFX_WHITE,
                 GFX_WHITE, 0.0f);
    gfx_ring_aaf(&s_c, 24.0f, 24.0f, NAN, 2.0f, 0.0f, 360.0f, GFX_WHITE,
                 GFX_WHITE, 0.0f);
    gfx_ring_aaf(NULL, 24.0f, 24.0f, 10.0f, 2.0f, 0.0f, 360.0f, GFX_WHITE,
                 GFX_WHITE, 0.0f);
    CHECK(coverage(NULL, NULL) == 0.0);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    RUN(at_whole_pixels_the_float_shapes_are_the_int_ones);
    RUN(a_disc_moves_by_fractions_of_a_pixel);
    RUN(a_capsule_grows_by_fractions_of_a_pixel);
    RUN(a_capsule_of_no_width_draws_nothing);
    RUN(a_ring_has_its_bands_area_and_centre);
    RUN(an_arc_keeps_to_its_span);
    RUN(an_arc_fades_from_its_ends);
    RUN(an_arc_is_the_same_either_way_round);
    RUN(a_ring_keeps_to_the_clip);
    RUN(a_ring_of_nothing_draws_nothing);
    return test_summary("gfx_subpixel");
}
