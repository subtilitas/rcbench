/*
 * The antialiased 1px ring from a coverage table (shared/gfx, gfx_ring.h).
 *
 * The failures this is about: a ring that leaves out or doubles a pixel of
 * its annulus, a pixel coloured by the wrong side of the arc's end, an arc
 * that ends at the wrong angle, a move that repaints more or other than
 * the pixels between the two ends, and a table that overruns its storage.
 *
 * The reference is built here from the same formula, pixel by pixel over
 * the whole canvas and without the table: coverage clamp(1 - |d - r|, 0, 1)
 * at the pixel's center, the arc's colour where the pixel's angle from 12
 * o'clock clockwise lies before the end.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>

#include "greatest.h"

#include "gfx.h"
#include "gfx_damage.h"
#include "gfx_ring.h"

#define W 240
#define C2 239
#define PI 3.14159265358979323846

static gfx_color_t s_px[W * W];
static gfx_color_t s_ref[W * W];
static gfx_ring_px_t s_tab[GFX_RING_CAP(GFX_RING_R_MAX)];
static uint32_t s_keys[GFX_RING_CAP(GFX_RING_R_MAX)];

static const gfx_color_t BG = GFX_RGB(2, 5, 9);
static const gfx_color_t ON = GFX_RGB(0, 240, 255);
static const gfx_color_t TRACK = GFX_RGB(2, 57, 63);

/** The angle of a pixel's center from (119.5, 119.5), degrees from 12
 *  o'clock clockwise, as the reference computes it. */
static double angle_deg(int x, int y)
{
    double a = atan2((double)x - 119.5, -((double)y - 119.5)) * 180.0 / PI;
    return a < 0.0 ? a + 360.0 : a;
}

/** The reference ring over the whole canvas. */
static int reference(int r, uint32_t end)
{
    int n = 0;
    for (int y = 0; y < W; ++y) {
        for (int x = 0; x < W; ++x) {
            const double d = hypot((double)x - 119.5, (double)y - 119.5);
            double cov = 1.0 - fabs(d - (double)r);
            cov = cov < 0.0 ? 0.0 : cov > 1.0 ? 1.0 : cov;
            const long k = lround(cov * 255.0);
            s_ref[y * W + x] = BG;
            if (k <= 0) {
                continue;
            }
            const double q = angle_deg(x, y) * 65536.0 / 360.0;
            const bool on = floor(q) < (double)end;
            const gfx_color_t col = on ? ON : TRACK;
            s_ref[y * W + x] = k == 255 ? col : gfx_lerp(BG, col, (uint8_t)k);
            ++n;
        }
    }
    return n;
}

static void canvas(gfx_canvas_t *c)
{
    gfx_canvas_init(c, s_px, W, W, 0);
    gfx_clear(c, BG);
}

static int differ(void)
{
    int n = 0;
    for (int i = 0; i < W * W; ++i) {
        n += s_px[i] != s_ref[i];
    }
    return n;
}

TEST_CASE(a_table_holds_every_pixel_of_its_annulus_once_under_its_cap)
{
    gfx_ring_t ring;
    for (int r = 1; r <= GFX_RING_R_MAX; ++r) {
        const int n = gfx_ring_build(&ring, 2 * 127, 2 * 127, r, s_tab,
                                     s_keys, GFX_RING_CAP(r));
        /* About 4 x pi x r: the annulus 2px wide. */
        if (n <= 0 || n > GFX_RING_CAP(r) || n < 8 * r) {
            T_FAIL("radius %d: %d pixels, cap %d", r, n, GFX_RING_CAP(r));
        }
        /* In angle order, each pixel once. */
        for (int i = 1; i < n; ++i) {
            const uint32_t a = gfx_ring_angle(&ring, s_tab[i - 1].x,
                                              s_tab[i - 1].y);
            const uint32_t b = gfx_ring_angle(&ring, s_tab[i].x, s_tab[i].y);
            if (a > b || (s_tab[i - 1].x == s_tab[i].x
                          && s_tab[i - 1].y == s_tab[i].y)) {
                T_FAIL("radius %d: entry %d out of order", r, i);
                break;
            }
        }
    }
}

TEST_CASE(the_ring_matches_the_reference_built_from_the_formula)
{
    static const uint32_t ends[] = { 0u, 1u, 16384u, 24249u, 32768u, 65535u,
                                     GFX_RING_TURN };
    gfx_ring_t ring;
    gfx_canvas_t c;
    for (int r = 103; r <= 118; r += 3) {
        const int n = gfx_ring_build(&ring, C2, C2, r, s_tab, s_keys,
                                     GFX_RING_CAP(r));
        for (size_t k = 0; k < sizeof ends / sizeof ends[0]; ++k) {
            canvas(&c);
            const int painted = gfx_ring_aa(&c, &ring, 0, GFX_RING_TURN,
                                            ends[k], ON, TRACK, BG);
            const int want = reference(r, ends[k]);
            CHECK_EQ(painted, n);
            CHECK_EQ(n, want);
            if (differ() != 0) {
                T_FAIL("radius %d, end %u: %d pixels differ", r,
                       (unsigned)ends[k], differ());
            }
        }
    }
}

TEST_CASE(a_ring_at_50_percent_ends_at_180_degrees)
{
    gfx_ring_t ring;
    gfx_canvas_t c;
    double last_on = 0.0;
    double first_track = 360.0;
    const int n = gfx_ring_build(&ring, C2, C2, 118, s_tab, s_keys,
                                 GFX_RING_CAP(118));
    const uint32_t end = gfx_ring_end(50, 100);
    CHECK_EQ(end, 32768);
    canvas(&c);
    gfx_ring_aa(&c, &ring, 0, GFX_RING_TURN, end, ON, TRACK, BG);
    for (int i = 0; i < n; ++i) {
        const int x = s_tab[i].x;
        const int y = s_tab[i].y;
        const gfx_color_t p = s_px[y * W + x];
        const double a = angle_deg(x, y);
        if (p == gfx_lerp(BG, ON, s_tab[i].cov) && a > last_on) {
            last_on = a;
        }
        if (p == gfx_lerp(BG, TRACK, s_tab[i].cov) && a < first_track) {
            first_track = a;
        }
    }
    CHECK_NEAR(last_on, 180.0, 1.0);
    CHECK_NEAR(first_track, 180.0, 1.0);
    CHECK(last_on < first_track);
}

TEST_CASE(a_move_repaints_only_the_pixels_between_the_two_ends)
{
    gfx_ring_t ring;
    gfx_canvas_t c;
    gfx_damage_t dmg;
    gfx_rect_t box[GFX_DAMAGE_MAX_RECTS];
    const int r = 115;
    gfx_ring_build(&ring, C2, C2, r, s_tab, s_keys, GFX_RING_CAP(r));
    canvas(&c);
    gfx_ring_aa(&c, &ring, 0, GFX_RING_TURN, 10000u, ON, TRACK, BG);
    /* A marker on a pixel of the ring outside the move: a move that paints
     * outside its span overwrites it. */
    const uint32_t far = gfx_ring_find(&ring, 50000u);
    const int mx = s_tab[far].x;
    const int my = s_tab[far].y;
    s_px[my * W + mx] = GFX_RGB(255, 0, 255);

    gfx_damage_init(&dmg, &c);
    gfx_damage_sink = &dmg;
    const int painted = gfx_ring_move(&c, &ring, 10000u, 12000u, ON, TRACK,
                                      BG);
    gfx_damage_sink = NULL;
    CHECK_EQ(painted, (int)(gfx_ring_find(&ring, 12000u)
                            - gfx_ring_find(&ring, 10000u)));
    CHECK(painted > 0);
    CHECK_EQ(s_px[my * W + mx], GFX_RGB(255, 0, 255));
    /* The damage is the box of the painted pixels: inside the quadrant
     * from 12 to 3 o'clock, which holds the move's span. */
    const int n = gfx_damage_rects(&dmg, box, GFX_DAMAGE_MAX_RECTS);
    CHECK(n >= 1);
    for (int i = 0; i < n; ++i) {
        CHECK(box[i].x >= 120 && box[i].y + box[i].h <= 120);
    }

    /* Back and forth ends where a full paint ends. */
    s_px[my * W + mx] = gfx_lerp(BG, TRACK, s_tab[far].cov);
    gfx_ring_move(&c, &ring, 12000u, 60000u, ON, TRACK, BG);
    gfx_ring_move(&c, &ring, 60000u, 32768u, ON, TRACK, BG);
    reference(r, 32768u);
    CHECK_EQ(differ(), 0);
    /* No move, no paint. */
    CHECK_EQ(gfx_ring_move(&c, &ring, 32768u, 32768u, ON, TRACK, BG), 0);
}

TEST_CASE(a_repaint_equals_the_first_paint_and_the_clip_holds)
{
    gfx_ring_t ring;
    gfx_canvas_t c;
    gfx_ring_build(&ring, C2, C2, 109, s_tab, s_keys, GFX_RING_CAP(109));
    canvas(&c);
    gfx_ring_aa(&c, &ring, 0, GFX_RING_TURN, 40000u, ON, TRACK, BG);
    gfx_ring_aa(&c, &ring, 0, GFX_RING_TURN, 40000u, ON, TRACK, BG);
    reference(109, 40000u);
    CHECK_EQ(differ(), 0);

    canvas(&c);
    gfx_clip_set(&c, gfx_rect_make(0, 0, 120, 240));
    const int painted = gfx_ring_aa(&c, &ring, 0, GFX_RING_TURN, 40000u, ON,
                                    TRACK, BG);
    CHECK(painted > 0 && painted < ring.n);
    for (int y = 0; y < W; ++y) {
        for (int x = 120; x < W; ++x) {
            if (s_px[y * W + x] != BG) {
                T_FAIL("pixel %d,%d painted outside the clip", x, y);
                return;
            }
        }
    }
}

TEST_CASE(a_build_refuses_what_does_not_fit)
{
    gfx_ring_t ring;
    CHECK_EQ(gfx_ring_build(&ring, C2, C2, 0, s_tab, s_keys, 100), 0);
    CHECK_EQ(gfx_ring_build(&ring, C2, C2, GFX_RING_R_MAX + 1, s_tab, s_keys,
                            GFX_RING_CAP(GFX_RING_R_MAX)), 0);
    CHECK_EQ(gfx_ring_build(&ring, C2, C2, 118, s_tab, s_keys, 1000), 0);
    /* A ring whose pixels leave 0 to 255. */
    CHECK_EQ(gfx_ring_build(&ring, 20, 20, 50, s_tab, s_keys,
                            GFX_RING_CAP(50)), 0);
    CHECK_EQ(gfx_ring_build(NULL, C2, C2, 10, s_tab, s_keys, 200), 0);
    CHECK_EQ(gfx_ring_build(&ring, C2, C2, 10, NULL, s_keys, 200), 0);
    CHECK_EQ(gfx_ring_build(&ring, C2, C2, 10, s_tab, NULL, 200), 0);
    CHECK(gfx_ring_build(&ring, C2, C2, 10, s_tab, s_keys, 200) > 0);
    CHECK_EQ(gfx_ring_find(NULL, 5u), 0u);
    CHECK_EQ(gfx_ring_find(&ring, GFX_RING_TURN), ring.n);
    CHECK_EQ(gfx_ring_find(&ring, 0u), 0u);
}

TEST_CASE(an_end_is_the_share_of_the_turn_rounded_and_held)
{
    CHECK_EQ(gfx_ring_end(0, 100), 0u);
    CHECK_EQ(gfx_ring_end(1, 4), 16384u);
    CHECK_EQ(gfx_ring_end(1, 3), 21845u);
    CHECK_EQ(gfx_ring_end(2, 3), 43691u);
    CHECK_EQ(gfx_ring_end(100, 100), GFX_RING_TURN);
    CHECK_EQ(gfx_ring_end(500, 100), GFX_RING_TURN);
    CHECK_EQ(gfx_ring_end(5, 0), 0u);
}

int main(void)
{
    RUN(a_table_holds_every_pixel_of_its_annulus_once_under_its_cap);
    RUN(the_ring_matches_the_reference_built_from_the_formula);
    RUN(a_ring_at_50_percent_ends_at_180_degrees);
    RUN(a_move_repaints_only_the_pixels_between_the_two_ends);
    RUN(a_repaint_equals_the_first_paint_and_the_clip_holds);
    RUN(a_build_refuses_what_does_not_fit);
    RUN(an_end_is_the_share_of_the_turn_rounded_and_held);
    return test_summary("gfx_ring");
}
