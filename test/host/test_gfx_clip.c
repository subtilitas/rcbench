/*
 * The rasteriser's shortcuts past what it does not draw.
 *
 * A glyph or a digit wholly outside the clip is skipped, an arc walks only
 * the directions in which the clip lies, and the antialiased disc, capsule
 * and ring walk their own pixels of each row rather than their boxes, the
 * ring working out the angle only near its ends and its fade.  Held here:
 *
 * - the disc, the capsule and the ring come out pixel for pixel as the
 *   loops over the whole box draw them (kept below as the reference), over
 *   a background they blend into;
 * - a draw clipped to a box is the whole draw inside the box and nothing
 *   outside it, for text, numerals and arcs;
 * - a draw whose clip it misses visits no pixel (gfx_work()), and an arc
 *   beside a small clip walks a small part of itself.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "gfx.h"
#include "gfx_work.h"
#include "gfx_subpixel.h"

#define W 220
#define H 180

static gfx_color_t s_a[W * H];
static gfx_color_t s_b[W * H];
static gfx_color_t s_bg[W * H];
static uint32_t s_seed = 12345u;

/** @brief The next pseudo-random number, 0 to 2^31 - 1. */
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return (s_seed >> 1) & 0x7FFFFFFFu;
}

/** @brief A pseudo-random float from @p lo to @p hi. */
static float frnd(float lo, float hi)
{
    return lo + (hi - lo) * (float)(rnd() % 100001u) / 100000.0f;
}

/** @brief A background of random colours, for a blend to read back. */
static void background(void)
{
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        s_bg[i] = (gfx_color_t)rnd();
    }
    memcpy(s_a, s_bg, sizeof(s_a));
    memcpy(s_b, s_bg, sizeof(s_b));
}

/** @brief A random clip of @p c: anywhere from a sliver to the canvas. */
static void random_clip(gfx_canvas_t *c)
{
    const int x = (int)(rnd() % (W - 4)), y = (int)(rnd() % (H - 4));
    const int w = 1 + (int)(rnd() % (unsigned)(W - x));
    const int h = 1 + (int)(rnd() % (unsigned)(H - y));
    (void)gfx_clip_set(c, gfx_rect_make(x, y, w, h));
}

/* ----------------------------------------------- the reference shapes */

/** @brief cov_blend() as the rasteriser has it. */
static void ref_blend(gfx_canvas_t *c, int x, int y, gfx_color_t col,
                      float cov)
{
    if (cov <= 0.0f) {
        return;
    }
    if (cov >= 1.0f) {
        gfx_pixel(c, x, y, col);
        return;
    }
    gfx_pixel(c, x, y,
              gfx_lerp(gfx_pixel_get(c, x, y), col, (uint8_t)(cov * 255.0f)));
}

/** @brief cov_box() as the rasteriser has it. */
static bool ref_box(const gfx_canvas_t *c, float cx, float cy, float reach,
                    int *x0, int *y0, int *x1, int *y1)
{
    int lo_x = (int)floorf(cx - reach), hi_x = (int)ceilf(cx + reach);
    int lo_y = (int)floorf(cy - reach), hi_y = (int)ceilf(cy + reach);
    if (lo_x < c->clip.x) { lo_x = c->clip.x; }
    if (lo_y < c->clip.y) { lo_y = c->clip.y; }
    if (hi_x > c->clip.x + c->clip.w) { hi_x = c->clip.x + c->clip.w; }
    if (hi_y > c->clip.y + c->clip.h) { hi_y = c->clip.y + c->clip.h; }
    *x0 = lo_x; *y0 = lo_y; *x1 = hi_x; *y1 = hi_y;
    return (hi_x > lo_x) && (hi_y > lo_y);
}

/** @brief The disc, every pixel of its box. */
static void ref_disc(gfx_canvas_t *c, float cx, float cy, float r,
                     gfx_color_t col)
{
    int x0, y0, x1, y1;
    if (!(r > 0.0f) || !ref_box(c, cx, cy, r + 1.0f, &x0, &y0, &x1, &y1)) {
        return;
    }
    for (int y = y0; y < y1; ++y) {
        const float dy = (float)y - cy;
        for (int x = x0; x < x1; ++x) {
            const float dx = (float)x - cx;
            ref_blend(c, x, y, col, r + 0.5f - sqrtf(dx * dx + dy * dy));
        }
    }
}

/** @brief The capsule, every pixel of its box. */
static void ref_capsule(gfx_canvas_t *c, float ax, float ay, float bx,
                        float by, float thickness, gfx_color_t col)
{
    if (!(thickness > 0.0f)) {
        return;
    }
    const float hw = thickness / 2.0f;
    const float ex = bx - ax, ey = by - ay;
    const float len2 = ex * ex + ey * ey;
    const float reach = sqrtf(len2) / 2.0f + hw + 1.0f;
    int x0, y0, x1, y1;
    if (!ref_box(c, (ax + bx) / 2.0f, (ay + by) / 2.0f, reach, &x0, &y0, &x1,
                 &y1)) {
        return;
    }
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float px = (float)x - ax, py = (float)y - ay;
            float t = (len2 > 0.0f) ? (px * ex + py * ey) / len2 : 0.0f;
            if (t < 0.0f) { t = 0.0f; }
            if (t > 1.0f) { t = 1.0f; }
            const float qx = px - t * ex, qy = py - t * ey;
            ref_blend(c, x, y, col, hw + 0.5f - sqrtf(qx * qx + qy * qy));
        }
    }
}

/** @brief arc_depth() as the rasteriser has it. */
static float ref_depth(float ang_deg, float a0_deg, float span)
{
    float u = fmodf(ang_deg - a0_deg, 360.0f);
    if (u < 0.0f) {
        u += 360.0f;
    }
    if (u <= span) {
        return fminf(u, span - u);
    }
    return -fminf(u - span, 360.0f - u);
}

/** @brief The ring, every pixel of its box, the angle of each. */
static void ref_ring(gfx_canvas_t *c, float cx, float cy, float r,
                     float thickness, float a0_deg, float a1_deg,
                     gfx_color_t col, gfx_color_t into, float fade_deg)
{
    if (!(r > 0.0f) || !(thickness > 0.0f)) {
        return;
    }
    const float k     = 3.14159265358979f / 180.0f;
    const float half  = thickness / 2.0f;
    const float outer = r + half + 0.5f;
    const float inner = r - half - 0.5f;
    const float lo = (a0_deg < a1_deg) ? a0_deg : a1_deg;
    const float span = fabsf(a1_deg - a0_deg);
    const bool whole = span >= 360.0f;
    int x0, y0, x1, y1;
    if (!ref_box(c, cx, cy, outer + 0.5f, &x0, &y0, &x1, &y1)) {
        return;
    }
    const float out2 = outer * outer;
    const float in2  = (inner > 0.0f) ? inner * inner : 0.0f;
    for (int y = y0; y < y1; ++y) {
        const float dy = (float)y - cy;
        for (int x = x0; x < x1; ++x) {
            const float dx = (float)x - cx;
            const float d2 = dx * dx + dy * dy;
            if (d2 >= out2 || d2 <= in2) {
                continue;
            }
            const float d = sqrtf(d2);
            float cov = half + 0.5f - fabsf(d - r);
            if (cov > 1.0f) {
                cov = 1.0f;
            }
            gfx_color_t here = col;
            if (!whole) {
                const float e = ref_depth(atan2f(-dy, dx) / k, lo, span);
                const float end = 0.5f + e * k * d;
                if (end <= 0.0f) {
                    continue;
                }
                if (end < 1.0f) {
                    cov *= end;
                }
                if (fade_deg > 0.0f) {
                    float lit = (e > 0.0f ? e : 0.0f) / fade_deg;
                    if (lit > 1.0f) {
                        lit = 1.0f;
                    }
                    here = gfx_lerp(into, col, (uint8_t)(lit * 255.0f));
                }
            }
            ref_blend(c, x, y, here, cov);
        }
    }
}

/** @brief The canvases over s_a and s_b, the same random clip on both. */
static void two_canvases(gfx_canvas_t *a, gfx_canvas_t *b)
{
    gfx_canvas_init(a, s_a, W, H, 0);
    gfx_canvas_init(b, s_b, W, H, 0);
    if (rnd() % 3u != 0u) {
        random_clip(a);
        b->clip = a->clip;
    }
}

/* ------------------------------------------------------------------ cases */

/* 400 discs, capsules and rings of every size, angle and fade, over a
 * background they blend into, under random clips: the rows' own pixels
 * give what the boxes gave, pixel for pixel. */
TEST_CASE(the_shapes_come_out_as_their_boxes_draw_them)
{
    int bad = 0;
    for (int i = 0; i < 400 && bad == 0; ++i) {
        background();
        gfx_canvas_t a, b;
        two_canvases(&a, &b);
        const gfx_color_t col = (gfx_color_t)rnd(), into = (gfx_color_t)rnd();
        const float cx = frnd(-20.0f, W + 20.0f), cy = frnd(-20.0f, H + 20.0f);
        switch (i % 3) {
        case 0: {
            const float r = frnd(0.2f, 60.0f);
            gfx_fill_circle_aaf(&a, cx, cy, r, col);
            ref_disc(&b, cx, cy, r, col);
            break;
        }
        case 1: {
            const float bx = frnd(-20.0f, W + 20.0f);
            const float by = (i % 7 == 0) ? cy : frnd(-20.0f, H + 20.0f);
            const float t = frnd(0.5f, 40.0f);
            gfx_capsule_aaf(&a, cx, cy, bx, by, t, col);
            ref_capsule(&b, cx, cy, bx, by, t, col);
            break;
        }
        default: {
            const float r = frnd(1.0f, 80.0f), t = frnd(0.5f, 8.0f);
            const float a0 = frnd(-720.0f, 720.0f);
            const float a1 = (i % 11 == 0) ? a0 + 360.0f
                                           : a0 + frnd(-400.0f, 400.0f);
            const float fade = (i % 5 == 0) ? 0.0f : frnd(0.0f, 120.0f);
            gfx_ring_aaf(&a, cx, cy, r, t, a0, a1, col, into, fade);
            ref_ring(&b, cx, cy, r, t, a0, a1, col, into, fade);
            break;
        }
        }
        if (memcmp(s_a, s_b, sizeof(s_a)) != 0) {
            T_FAIL("shape %d (kind %d) differs from its reference", i, i % 3);
            ++bad;
        }
    }
}

/* The servo card's own rings: the grip at the tip of a horn at every
 * whole degree, lit with the gap on the arm's side and faded over 78deg. */
TEST_CASE(the_grip_rings_come_out_as_their_boxes_draw_them)
{
    for (int deg = -90; deg <= 90; ++deg) {
        background();
        gfx_canvas_t a, b;
        gfx_canvas_init(&a, s_a, W, H, 0);
        gfx_canvas_init(&b, s_b, W, H, 0);
        const float tx = 110.0f + 0.37f * (float)deg, ty = 90.0f - 0.21f * (float)deg;
        for (int k = 0; k < 2; ++k) {
            const float r = k ? 33.0f : 25.0f, t = k ? 2.0f : 3.0f;
            const float g = k ? 41.5f : 52.3f;
            gfx_ring_aaf(&a, tx, ty, r, t, (float)deg + 180.0f + g,
                         (float)deg + 540.0f - g, 0xFFE0u, 0x2104u, 78.0f);
            ref_ring(&b, tx, ty, r, t, (float)deg + 180.0f + g,
                     (float)deg + 540.0f - g, 0xFFE0u, 0x2104u, 78.0f);
        }
        if (memcmp(s_a, s_b, sizeof(s_a)) != 0) {
            T_FAIL("the grip at %d deg differs from its reference", deg);
            return;
        }
    }
}

/**
 * @brief What @p draw lays clipped to random boxes is, inside each box,
 *        what it lays unclipped, and nothing outside it.
 * @param draw the drawing, onto the canvas it is given
 * @param name the drawing's name, for a failure
 */
static void clip_holds(void (*draw)(gfx_canvas_t *), const char *name)
{
    for (int i = 0; i < 60; ++i) {
        background();
        gfx_canvas_t a, b;
        gfx_canvas_init(&a, s_a, W, H, 0);
        gfx_canvas_init(&b, s_b, W, H, 0);
        random_clip(&b);
        draw(&a);
        draw(&b);
        const gfx_rect_t k = b.clip;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const size_t p = (size_t)y * W + (size_t)x;
                const bool in = gfx_rect_contains(k, x, y);
                if (s_b[p] != (in ? s_a[p] : s_bg[p])) {
                    T_FAIL("%s: clip %d,%d %dx%d, pixel %d,%d", name, k.x,
                           k.y, k.w, k.h, x, y);
                    return;
                }
            }
        }
    }
}

static void draw_text(gfx_canvas_t *c)
{
    gfx_text(c, 3, 5, "STOP 12.5V Ab", &gfx_font_10x20, 0xFFFFu, 1);
    gfx_text(c, -7, 40, "MOTOR", &gfx_font_16x28, 0xF800u, 2);
    gfx_text_in(c, gfx_rect_make(20, 120, 180, 50), "-12.3", &gfx_font_num_32x44,
                0x07E0u, 1, GFX_ALIGN_CENTER);
}

static void draw_numerals(gfx_canvas_t *c)
{
    const gfx_seg_style_t up = { 22, 40, 5, 4, 0, true };
    const gfx_seg_style_t lean = { 30, 60, 7, 5, 6, true };
    const gfx_seg_style_t back = { 18, 30, 4, 3, -5, false };
    (void)gfx_seg_text(c, 4, 4, "-88.8", &up, 0xFFE0u, 0x2104u);
    (void)gfx_seg_text(c, 10, 60, "12:30", &lean, 0x07FFu, 0x0841u);
    (void)gfx_seg_text(c, 120, 130, "4.7", &back, 0xF81Fu, 0x0000u);
}

static void draw_arcs(gfx_canvas_t *c)
{
    gfx_arc(c, 110, 160, 150, 2, 0.0f, 180.0f, 0x7BEFu);
    gfx_arc(c, 110, 160, 150, 4, 30.0f, 150.0f, 0xFFFFu);
    gfx_arc_fade(c, 60, 60, 40, 6, -30.0f, 250.0f, 0xF800u, 0x0000u, 40.0f);
    gfx_arc(c, 300, -40, 200, 3, 200.0f, 340.0f, 0x07E0u);
    gfx_arc(c, 100, 90, 9, 3, 0.0f, 360.0f, 0x001Fu);
}

/* Text, the bench's numerals and arcs, clipped to boxes anywhere from a
 * sliver to the canvas: inside each, what they draw unclipped. */
TEST_CASE(a_clipped_draw_is_the_draw_inside_its_clip)
{
    clip_holds(draw_text, "text");
    clip_holds(draw_numerals, "numerals");
    clip_holds(draw_arcs, "arcs");
}

/* A clip none of a drawing reaches costs no visit; an arc of 150px radius
 * beside a box of 40px walks under a quarter of its steps. */
TEST_CASE(a_draw_outside_its_clip_visits_nothing)
{
    gfx_canvas_t c;
    gfx_canvas_init(&c, s_a, W, H, 0);
    (void)gfx_clip_set(&c, gfx_rect_make(180, 140, 30, 30));
    unsigned long before = gfx_work();
    gfx_text(&c, 3, 5, "STOP 12.5V Ab", &gfx_font_10x20, 0xFFFFu, 1);
    const gfx_seg_style_t up = { 22, 40, 5, 4, 3, true };
    (void)gfx_seg_text(&c, 4, 4, "-88.8", &up, 0xFFE0u, 0x2104u);
    gfx_fill_circle_aaf(&c, 40.0f, 40.0f, 25.0f, 0xFFFFu);
    gfx_capsule_aaf(&c, 10.0f, 10.0f, 90.0f, 60.0f, 26.0f, 0xFFFFu);
    gfx_ring_aaf(&c, 60.0f, 60.0f, 33.0f, 2.0f, 10.0f, 300.0f, 0xFFFFu,
                 0x0000u, 78.0f);
    gfx_arc(&c, 60, 60, 40, 4, 0.0f, 360.0f, 0xFFFFu);
    CHECK_EQ(gfx_work() - before, 0);

    /* A dial's arc round (60, 170) at 150px and a box over its right
     * end: the box subtends under 20deg of the half turn. */
    gfx_canvas_init(&c, s_a, W, H, 0);
    const unsigned long whole_before = gfx_work();
    gfx_arc(&c, 60, 170, 150, 2, 0.0f, 180.0f, 0xFFFFu);
    const unsigned long whole = gfx_work() - whole_before;
    (void)gfx_clip_set(&c, gfx_rect_make(180, 140, 40, 40));
    before = gfx_work();
    gfx_arc(&c, 60, 170, 150, 2, 0.0f, 180.0f, 0xFFFFu);
    const unsigned long part = gfx_work() - before;
    CHECK(part > 0ul);
    CHECK(part * 4ul < whole);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    RUN(the_shapes_come_out_as_their_boxes_draw_them);
    RUN(the_grip_rings_come_out_as_their_boxes_draw_them);
    RUN(a_clipped_draw_is_the_draw_inside_its_clip);
    RUN(a_draw_outside_its_clip_visits_nothing);
    return test_summary("gfx_clip");
}
