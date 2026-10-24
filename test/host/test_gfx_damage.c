/*
 * The damage record: what the rasteriser wrote, for Screen rotation 180°
 * to copy and nothing else (gfx_damage.h).
 *
 * Held here:
 * - a record bound to a buffer starts empty, takes rectangles band by band
 *   and gives them back as at most GFX_DAMAGE_MAX_RECTS rectangles that
 *   hold every written pixel;
 * - a write through a sub-canvas counts where it lies in the buffer, a
 *   write into another buffer counts nothing, and a canvas inside the
 *   buffer with another stride counts the whole buffer;
 * - every primitive, over random shapes, clips and sub-canvases, changes
 *   no pixel outside the rectangles it reported (the oracle: the buffer
 *   before and after);
 * - with no record attached nothing is counted;
 * - a rectangle turned by 180° lands where the turned buffer has it.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include <stdint.h>
#include <string.h>

#include "gfx.h"
#include "gfx_damage.h"
#include "gfx_subpixel.h"

#define W 320
#define H 200

static gfx_color_t s_buf[W * H];
static gfx_color_t s_before[W * H];
static gfx_color_t s_other[64 * 64];
static gfx_color_t s_src[48 * 40];
static uint32_t s_seed = 4242u;

/** @brief The next pseudo-random number, 0 to 2^31 - 1. */
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return (s_seed >> 1) & 0x7FFFFFFFu;
}

/** @brief A pseudo-random int from @p lo to @p hi, both included. */
static int irnd(int lo, int hi)
{
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

/** @brief Whether (@p x, @p y) lies in one of @p n rectangles. */
static bool covered(const gfx_rect_t *r, int n, int x, int y)
{
    for (int i = 0; i < n; ++i) {
        if (gfx_rect_contains(r[i], x, y)) {
            return true;
        }
    }
    return false;
}

/** @brief The pixels of @p s_buf that differ from @p s_before and lie in
 *         none of @p d's rectangles, given at most @p max. */
static long uncovered(const gfx_damage_t *d, int max)
{
    gfx_rect_t r[GFX_DAMAGE_MAX_BANDS];
    const int n = gfx_damage_rects(d, r, max);
    long bad = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t)y * W + (size_t)x;
            if (s_buf[i] != s_before[i] && !covered(r, n, x, y)) {
                ++bad;
            }
        }
    }
    return bad;
}

/** @brief A root canvas over s_buf and a record bound to it. */
static void fresh(gfx_canvas_t *root, gfx_damage_t *d)
{
    gfx_canvas_init(root, s_buf, W, H, W);
    gfx_damage_init(d, root);
}

/* --------------------------------------------------------------- the record */

TEST_CASE(a_fresh_record_is_empty)
{
    gfx_canvas_t root;
    gfx_damage_t d;
    fresh(&root, &d);
    gfx_rect_t r[GFX_DAMAGE_MAX_RECTS];
    CHECK(gfx_damage_empty(&d));
    CHECK_EQ(gfx_damage_rects(&d, r, GFX_DAMAGE_MAX_RECTS), 0);
    CHECK_EQ(d.bands, (H + 15) / 16);
}

TEST_CASE(a_rectangle_comes_back_widened_to_its_bands)
{
    gfx_canvas_t root;
    gfx_damage_t d;
    fresh(&root, &d);
    gfx_damage_mark(&d, &root, 10, 20, 30, 5);
    gfx_rect_t r[GFX_DAMAGE_MAX_RECTS];
    CHECK_EQ(gfx_damage_rects(&d, r, GFX_DAMAGE_MAX_RECTS), 1);
    /* Rows 20 to 24 lie in band 1, rows 16 to 31. */
    CHECK_EQ(r[0].x, 10);
    CHECK_EQ(r[0].w, 30);
    CHECK_EQ(r[0].y, 16);
    CHECK_EQ(r[0].h, 16);
    CHECK(!gfx_damage_empty(&d));
    gfx_damage_reset(&d);
    CHECK(gfx_damage_empty(&d));
}

TEST_CASE(the_last_band_stops_at_the_buffer)
{
    gfx_canvas_t root;
    gfx_damage_t d;
    fresh(&root, &d);
    gfx_damage_mark(&d, &root, 0, H - 1, W, 1);
    gfx_rect_t r[GFX_DAMAGE_MAX_RECTS];
    CHECK_EQ(gfx_damage_rects(&d, r, GFX_DAMAGE_MAX_RECTS), 1);
    CHECK_EQ(r[0].y + r[0].h, H);
    CHECK_EQ(r[0].h, H % 16 == 0 ? 16 : H % 16);
}

TEST_CASE(far_apart_writes_stay_apart_and_neighbours_merge)
{
    gfx_canvas_t root;
    gfx_damage_t d;
    fresh(&root, &d);
    gfx_damage_mark(&d, &root, 0, 0, 20, 40);      /* bands 0 to 2  */
    gfx_damage_mark(&d, &root, 250, 150, 60, 10);  /* band 9        */
    gfx_rect_t r[GFX_DAMAGE_MAX_RECTS];
    const int n = gfx_damage_rects(&d, r, GFX_DAMAGE_MAX_RECTS);
    CHECK_EQ(n, 2);
    CHECK_EQ(r[0].x, 0);
    CHECK_EQ(r[0].y, 0);
    CHECK_EQ(r[0].w, 20);
    CHECK_EQ(r[0].h, 48);
    CHECK_EQ(r[1].x, 250);
    CHECK_EQ(r[1].y, 144);
}

TEST_CASE(too_many_rectangles_merge_down_to_the_most)
{
    gfx_canvas_t root;
    gfx_damage_t d;
    fresh(&root, &d);
    /* One write in every other band, each at another x: 7 bands. */
    for (int b = 0; b < 13; b += 2) {
        gfx_damage_mark(&d, &root, b * 20, b * 16 + 3, 7, 2);
    }
    gfx_rect_t r[GFX_DAMAGE_MAX_BANDS];
    CHECK_EQ(gfx_damage_rects(&d, r, GFX_DAMAGE_MAX_BANDS), 7);
    for (int max = 1; max <= 7; ++max) {
        const int n = gfx_damage_rects(&d, r, max);
        CHECK(n >= 1 && n <= max);
        for (int b = 0; b < 13; b += 2) {
            CHECK(covered(r, n, b * 20, b * 16 + 3));
            CHECK(covered(r, n, b * 20 + 6, b * 16 + 4));
        }
    }
}

TEST_CASE(a_record_marked_all_is_the_buffer)
{
    gfx_canvas_t root;
    gfx_damage_t d;
    fresh(&root, &d);
    gfx_damage_all(&d);
    gfx_rect_t r[GFX_DAMAGE_MAX_RECTS];
    CHECK_EQ(gfx_damage_rects(&d, r, GFX_DAMAGE_MAX_RECTS), 1);
    CHECK_EQ(r[0].x, 0);
    CHECK_EQ(r[0].y, 0);
    CHECK_EQ(r[0].w, W);
    CHECK_EQ(r[0].h, H);
    CHECK(!gfx_damage_empty(&d));
}

TEST_CASE(a_sub_canvas_counts_where_it_lies)
{
    gfx_canvas_t root, sub;
    gfx_damage_t d;
    fresh(&root, &d);
    CHECK(gfx_canvas_sub(&root, gfx_rect_make(100, 60, 80, 80), &sub));
    gfx_damage_attach(&d);
    gfx_fill_rect(&sub, 5, 7, 3, 2, 0xFFFFu);
    gfx_damage_attach(NULL);
    gfx_rect_t r[GFX_DAMAGE_MAX_RECTS];
    CHECK_EQ(gfx_damage_rects(&d, r, GFX_DAMAGE_MAX_RECTS), 1);
    CHECK_EQ(r[0].x, 105);
    CHECK_EQ(r[0].w, 3);
    CHECK_EQ(r[0].y, 64);      /* row 67 lies in band 4, rows 64 to 79 */
    /* And a second sub-canvas after it, past the translation it keeps. */
    gfx_canvas_t sub2;
    CHECK(gfx_canvas_sub(&root, gfx_rect_make(10, 170, 30, 30), &sub2));
    gfx_damage_mark(&d, &sub2, 0, 0, 4, 4);
    CHECK_EQ(gfx_damage_rects(&d, r, GFX_DAMAGE_MAX_RECTS), 2);
    CHECK_EQ(r[1].x, 10);
    CHECK_EQ(r[1].y, 160);
}

TEST_CASE(another_buffer_counts_nothing)
{
    gfx_canvas_t root, other;
    gfx_damage_t d;
    fresh(&root, &d);
    gfx_canvas_init(&other, s_other, 64, 64, 64);
    gfx_damage_attach(&d);
    gfx_fill_rect(&other, 0, 0, 64, 64, 0x1234u);
    gfx_pixel(&other, 3, 3, 0x4321u);
    gfx_damage_attach(NULL);
    CHECK(gfx_damage_empty(&d));
}

TEST_CASE(another_stride_inside_the_buffer_counts_all)
{
    gfx_canvas_t root, odd;
    gfx_damage_t d;
    fresh(&root, &d);
    gfx_canvas_init(&odd, s_buf + 5 * W, 40, 10, 40);
    gfx_damage_mark(&d, &odd, 0, 0, 1, 1);
    gfx_rect_t r[GFX_DAMAGE_MAX_RECTS];
    CHECK_EQ(gfx_damage_rects(&d, r, GFX_DAMAGE_MAX_RECTS), 1);
    CHECK_EQ(r[0].w, W);
    CHECK_EQ(r[0].h, H);
}

TEST_CASE(a_buffer_taller_than_the_bands_counts_all)
{
    static gfx_color_t tall[8 * 1100];
    gfx_canvas_t c;
    gfx_damage_t d;
    gfx_canvas_init(&c, tall, 8, 1100, 8);
    gfx_damage_init(&d, &c);
    CHECK(!gfx_damage_empty(&d));
    gfx_rect_t r[GFX_DAMAGE_MAX_RECTS];
    CHECK_EQ(gfx_damage_rects(&d, r, GFX_DAMAGE_MAX_RECTS), 1);
    CHECK_EQ(r[0].h, 1100);
}

TEST_CASE(nothing_attached_counts_nothing)
{
    gfx_canvas_t root;
    gfx_damage_t d;
    fresh(&root, &d);
    gfx_damage_attach(NULL);
    gfx_fill_rect(&root, 0, 0, W, H, 0x0F0Fu);
    gfx_text(&root, 4, 4, "ABC", &gfx_font_8x16, 0xFFFFu, 1);
    CHECK(gfx_damage_empty(&d));
    CHECK(gfx_damage_sink == NULL);
}

/* Every call takes a NULL record or canvas and does nothing with it. */
TEST_CASE(a_null_record_does_nothing)
{
    gfx_canvas_t root;
    gfx_damage_t d;
    fresh(&root, &d);
    gfx_damage_init(NULL, &root);
    gfx_damage_init(&d, NULL);
    gfx_damage_reset(NULL);
    gfx_damage_all(NULL);
    gfx_damage_mark(NULL, &root, 0, 0, 1, 1);
    gfx_damage_mark(&d, NULL, 0, 0, 1, 1);
    gfx_damage_mark(&d, &root, 0, 0, 0, 1);
    gfx_damage_mark(&d, &root, W + 5, 0, 4, 4);     /* off the buffer */
    CHECK(gfx_damage_empty(NULL));
    CHECK(gfx_damage_empty(&d));
    gfx_rect_t r[1];
    CHECK_EQ(gfx_damage_rects(NULL, r, 1), 0);
    CHECK_EQ(gfx_damage_rects(&d, NULL, 1), 0);
    CHECK_EQ(gfx_damage_rects(&d, r, 0), 0);
}

TEST_CASE(a_turned_rectangle_lands_where_the_turned_buffer_has_it)
{
    const gfx_rect_t r = gfx_rect_make(10, 20, 30, 40);
    const gfx_rect_t t = gfx_damage_turn(r, 1024, 600);
    CHECK_EQ(t.x, 1024 - 40);
    CHECK_EQ(t.y, 600 - 60);
    CHECK_EQ(t.w, 30);
    CHECK_EQ(t.h, 40);
    const gfx_rect_t whole = gfx_damage_turn(gfx_rect_make(0, 0, 1024, 600),
                                             1024, 600);
    CHECK_EQ(whole.x, 0);
    CHECK_EQ(whole.y, 0);
    /* Turned twice is where it was. */
    const gfx_rect_t back = gfx_damage_turn(t, 1024, 600);
    CHECK_EQ(back.x, r.x);
    CHECK_EQ(back.y, r.y);
}

/* -------------------------------------------------------------- the oracle */

/** @brief One random drawing call of kind @p k on @p c. */
static void draw_one(gfx_canvas_t *c, int k)
{
    const gfx_color_t col = (gfx_color_t)rnd();
    const int x = irnd(-40, W + 10), y = irnd(-40, H + 10);
    const int w = irnd(-5, 120), h = irnd(-5, 90), r = irnd(0, 40);
    switch (k) {
    case 0:  gfx_fill_rect(c, x, y, w, h, col); break;
    case 1:  gfx_pixel(c, x, y, col); break;
    case 2:  gfx_hline(c, x, y, w, col); break;
    case 3:  gfx_vline(c, x, y, h, col); break;
    case 4:  gfx_line(c, x, y, irnd(-40, W + 40), irnd(-40, H + 40), col);
             break;
    case 5:  gfx_thick_line(c, x, y, x + w, y + h, irnd(1, 9), col); break;
    case 6:  gfx_draw_circle(c, x, y, r, col); break;
    case 7:  gfx_fill_circle(c, x, y, r, col); break;
    case 8:  gfx_fill_circle_aa(c, x, y, r, col); break;
    case 9:  gfx_capsule_aa(c, x, y, x + w, y + h, irnd(1, 20), col); break;
    case 10: gfx_arc(c, x, y, r + 5, irnd(1, 12), (float)irnd(-180, 180),
                     (float)irnd(-180, 360), col);
             break;
    case 11: gfx_arc_fade(c, x, y, r + 5, irnd(1, 12), 0.0f,
                          (float)irnd(10, 300), col, (gfx_color_t)rnd(),
                          (float)irnd(0, 40));
             break;
    case 12: gfx_draw_round_rect(c, x, y, w, h, r, col); break;
    case 13: gfx_fill_round_rect(c, x, y, w, h, r, col); break;
    case 14: gfx_fill_chamfer_rect_ex(c, x, y, w, h, irnd(0, 9), irnd(0, 9),
                                      irnd(0, 9), irnd(0, 9), col);
             break;
    case 15: gfx_draw_chamfer_rect(c, x, y, w, h, irnd(0, 9), col); break;
    case 16: gfx_fill_rect_gradient(c, x, y, w, h, col, (gfx_color_t)rnd());
             break;
    case 17: gfx_blit(c, x, y, s_src, irnd(1, 48), irnd(1, 40), 48); break;
    case 18: gfx_blit_key(c, x, y, s_src, irnd(1, 48), irnd(1, 40), 48,
                          s_src[0]);
             break;
    case 19: {
        static const uint8_t bits[4 * 12] = { 0xF0, 0x0F, 0xAA, 0x55 };
        gfx_blit_1bpp(c, x, y, bits, 30, 12, col);
        break;
    }
    case 20: gfx_text(c, x, y, "Bilddrehung 180\xc2\xb0", &gfx_font_10x20,
                      col, irnd(1, 2));
             break;
    case 21: gfx_text_bg(c, x, y, "NORMAL", &gfx_font_16x28, col,
                         (gfx_color_t)rnd(), 1);
             break;
    case 22: gfx_text_in(c, gfx_rect_make(x, y, w, h), "STOP",
                         &gfx_font_20x36, col, 1, GFX_ALIGN_CENTER);
             break;
    case 23: gfx_text_rotated(c, x, y, "SIM", &gfx_font_12x24, col, 2,
                              (uint8_t)irnd(1, 255), (float)irnd(0, 359));
             break;
    case 24: {
        const gfx_seg_style_t st = { 14, 26, 4, 3, irnd(0, 5), true };
        (void)gfx_seg_text(c, x, y, "12.5", &st, col, (gfx_color_t)rnd());
        break;
    }
    case 25: gfx_fill_circle_aaf(c, (float)x + 0.3f, (float)y + 0.6f,
                                 (float)r + 0.4f, col);
             break;
    case 26: gfx_capsule_aaf(c, (float)x, (float)y, (float)(x + w),
                             (float)(y + h), (float)irnd(1, 14) + 0.5f, col);
             break;
    case 27: gfx_ring_aaf(c, (float)x, (float)y, (float)r + 8.0f,
                          (float)irnd(2, 10), (float)irnd(-90, 90),
                          (float)irnd(90, 400), col, (gfx_color_t)rnd(),
                          (float)irnd(0, 30));
             break;
    case 28: gfx_clear(c, col); break;
    default: gfx_char(c, x, y, 'Q', &gfx_font_num_32x44, col, 1); break;
    }
}

/** @brief Kinds draw_one() knows. */
#define KINDS 30

/* Every primitive, many times over, on the root canvas and on clipped
 * sub-canvases: no pixel changes outside what it reported. */
TEST_CASE(every_primitive_reports_every_pixel_it_writes)
{
    for (size_t i = 0; i < sizeof(s_src) / sizeof(s_src[0]); ++i) {
        s_src[i] = (gfx_color_t)rnd();
    }
    gfx_canvas_t root;
    gfx_damage_t d;
    for (int round = 0; round < 1500; ++round) {
        const int k = round % KINDS;
        fresh(&root, &d);
        for (size_t i = 0; i < (size_t)W * H; ++i) {
            s_buf[i] = (gfx_color_t)rnd();
        }
        memcpy(s_before, s_buf, sizeof(s_buf));
        gfx_canvas_t c = root;
        if (round % 3 == 1) {
            (void)gfx_canvas_sub(&root, gfx_rect_make(irnd(0, W - 20),
                                                      irnd(0, H - 20),
                                                      irnd(1, W), irnd(1, H)),
                                 &c);
        }
        if (round % 4 == 2) {
            (void)gfx_clip_set(&c, gfx_rect_make(irnd(0, 60), irnd(0, 60),
                                                 irnd(1, 200), irnd(1, 150)));
        }
        gfx_damage_attach(&d);
        draw_one(&c, k);
        gfx_damage_attach(NULL);
        const long tight = uncovered(&d, GFX_DAMAGE_MAX_BANDS);
        const long merged = uncovered(&d, GFX_DAMAGE_MAX_RECTS);
        if (tight != 0 || merged != 0) {
            T_FAIL("round %d kind %d: %ld pixels outside the bands, %ld "
                   "outside the rectangles", round, k, tight, merged);
            return;
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    RUN(a_fresh_record_is_empty);
    RUN(a_rectangle_comes_back_widened_to_its_bands);
    RUN(the_last_band_stops_at_the_buffer);
    RUN(far_apart_writes_stay_apart_and_neighbours_merge);
    RUN(too_many_rectangles_merge_down_to_the_most);
    RUN(a_record_marked_all_is_the_buffer);
    RUN(a_sub_canvas_counts_where_it_lies);
    RUN(another_buffer_counts_nothing);
    RUN(another_stride_inside_the_buffer_counts_all);
    RUN(a_buffer_taller_than_the_bands_counts_all);
    RUN(nothing_attached_counts_nothing);
    RUN(a_null_record_does_nothing);
    RUN(a_turned_rectangle_lands_where_the_turned_buffer_has_it);
    RUN(every_primitive_reports_every_pixel_it_writes);
    return test_summary("gfx_damage");
}
