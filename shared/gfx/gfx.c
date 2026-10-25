/**
 * @file
 * @brief gfx: the RGB565 (16-bit red, green, blue) rasteriser.  See
 * include/gfx.h.
 *
 * SPDX-License-Identifier: MIT
 */

#define GFX_INL_FORCE   /* gfx_int.h: force the primitives inline (see there) */
#include "gfx_int.h"
#include "gfx_subpixel.h"
#include "gfx_work.h"

#include <limits.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define GFX_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GFX_MAX(a, b) ((a) > (b) ? (a) : (b))

/* The nesting depth of the fit check's hold (gfx_int.h); gfx_text.c counts it
 * too. */
#ifdef GFX_TEXT_TRACE
int s_trace_hold;
#endif

#ifdef GFX_WORK_COUNT
unsigned long gfx_work_visits;

unsigned long gfx_work(void)
{
    return gfx_work_visits;
}
#endif

/* The framebuffer is in PSRAM (pseudo-static random-access memory) behind a
 * write-back, write-allocate cache with 64-byte lines, so every byte written
 * costs two bytes on the bus: the line is fetched before it is overwritten,
 * then written back.  Filling a span 32 bits at a time halves the store count
 * and lets the write buffer coalesce.  may_alias keeps this legal without
 * disabling strict aliasing globally. */
typedef uint32_t gfx_u32_alias __attribute__((may_alias));

static inline void fill_span(gfx_color_t *p, int n, gfx_color_t color)
{
    if (n <= 0) {
        return;
    }
    /* A colour whose two bytes are equal (black, white, greys on the 565
     * diagonal) can go through memset, which is usually hand-tuned. */
    if ((uint8_t)(color & 0xFFu) == (uint8_t)(color >> 8)) {
        memset(p, (int)(color & 0xFFu), (size_t)n * sizeof(gfx_color_t));
        return;
    }
    if (((uintptr_t)p & 3u) != 0u) {
        *p++ = color;
        if (--n == 0) {
            return;
        }
    }
    /* Deliberate: two 16-bit pixels written as one 32-bit store, which is
     * what makes a fill worth having on a bandwidth-bound panel.  gfx_u32_alias
     * is declared may_alias for exactly this. */
    /* NOLINTNEXTLINE(bugprone-casting-through-void) */
    gfx_u32_alias *q = (gfx_u32_alias *)(void *)p;
    uint32_t pair = ((uint32_t)color << 16) | (uint32_t)color;
    int words = n >> 1;
    for (int i = 0; i < words; ++i) {
        q[i] = pair;
    }
    if (n & 1) {
        p[n - 1] = color;
    }
}

/* ------------------------------------------------------------------ colour */

gfx_color_t gfx_lerp(gfx_color_t a, gfx_color_t b, uint8_t t)
{
    uint8_t ar, ag, ab, br, bg, bb;
    gfx_unpack(a, &ar, &ag, &ab);
    gfx_unpack(b, &br, &bg, &bb);
    unsigned it = 255u - t;
    uint8_t r = (uint8_t)((ar * it + br * t + 127u) / 255u);
    uint8_t g = (uint8_t)((ag * it + bg * t + 127u) / 255u);
    uint8_t bl = (uint8_t)((ab * it + bb * t + 127u) / 255u);
    return GFX_RGB(r, g, bl);
}

/* ---------------------------------------------------------------- geometry */

bool gfx_rect_intersect(gfx_rect_t a, gfx_rect_t b, gfx_rect_t *out)
{
    int x0 = GFX_MAX(a.x, b.x);
    int y0 = GFX_MAX(a.y, b.y);
    int x1 = GFX_MIN(a.x + a.w, b.x + b.w);
    int y1 = GFX_MIN(a.y + a.h, b.y + b.h);

    if (x1 <= x0 || y1 <= y0) {
        if (out) {
            *out = gfx_rect_make(0, 0, 0, 0);
        }
        return false;
    }
    if (out) {
        *out = gfx_rect_make(x0, y0, x1 - x0, y1 - y0);
    }
    return true;
}

/* ------------------------------------------------------------------ canvas */

void gfx_canvas_init(gfx_canvas_t *c, gfx_color_t *pixels,
                     int width, int height, int32_t stride)
{
    if (!c) {
        return;
    }
    if (width < 0) { width = 0; }
    if (height < 0) { height = 0; }
    if (stride <= 0) { stride = width; }
    /* A stride narrower than the canvas would put the tail of every row in the
     * next row's memory, and past the end of the last one. */
    if (stride < width) { width = stride; }
    c->pixels = pixels;
    c->width = gfx_i16(width);
    c->height = gfx_i16(height);
    c->stride = stride;
    gfx_clip_reset(c);
}

bool gfx_canvas_sub(const gfx_canvas_t *src, gfx_rect_t window, gfx_canvas_t *out)
{
    if (!src || !out) {
        return false;
    }
    gfx_rect_t r;
    /* Against the parent's clip, not only its bounds: a window carved from a
     * narrowed canvas otherwise gets write access the parent had given up. */
    if (!gfx_rect_intersect(window, src->clip, &r) ||
        !gfx_rect_intersect(r, gfx_rect_make(0, 0, src->width, src->height), &r)) {
        gfx_canvas_init(out, src->pixels, 0, 0, src->stride);
        return false;
    }
    out->pixels = src->pixels + (ptrdiff_t)r.y * src->stride + r.x;
    out->width = r.w;
    out->height = r.h;
    out->stride = src->stride;
    gfx_clip_reset(out);
    return true;
}

void gfx_clip_reset(gfx_canvas_t *c)
{
    if (c) {
        c->clip = gfx_rect_make(0, 0, c->width, c->height);
    }
}

bool gfx_clip_set(gfx_canvas_t *c, gfx_rect_t r)
{
    if (!c) {
        return false;
    }
    return gfx_rect_intersect(r, gfx_rect_make(0, 0, c->width, c->height), &c->clip);
}

bool gfx_clip_intersect(gfx_canvas_t *c, gfx_rect_t r)
{
    return gfx_clip_intersect_inl(c, r);
}

/* -------------------------------------------------------------- primitives */

void gfx_pixel(gfx_canvas_t *c, int x, int y, gfx_color_t color)
{
    GFX_VISIT(1u);
    gfx_pixel_inl(c, x, y, color);
}

gfx_color_t gfx_pixel_get(const gfx_canvas_t *c, int x, int y)
{
    return gfx_pixel_get_inl(c, x, y);
}

void gfx_fill_rect(gfx_canvas_t *c, int x, int y, int w, int h, gfx_color_t color)
{
    if (!canvas_ok(c)) {
        return;
    }
    TRACE_BOX(c, x, y, w, h);
    gfx_rect_t r;
    if (!gfx_rect_intersect(gfx_rect_make(x, y, w, h), c->clip, &r)) {
        return;
    }
    GFX_VISIT((unsigned long)r.w * (unsigned long)r.h);
    GFX_DAMAGE(c, r.x, r.y, r.w, r.h);
    /* A full-width fill over a tightly packed canvas is one contiguous run,
     * which is worth a lot more than it looks: it is the difference between
     * one long burst and h separate ones. */
    if (r.w == c->stride && r.x == 0) {
        fill_span(c->pixels + (ptrdiff_t)r.y * c->stride, (int)((ptrdiff_t)r.h * c->stride),
                  color);
        return;
    }

    for (int row = 0; row < r.h; ++row) {
        fill_span(c->pixels + (ptrdiff_t)(r.y + row) * c->stride + r.x, r.w, color);
    }
}

void gfx_clear(gfx_canvas_t *c, gfx_color_t color)
{
    if (!canvas_ok(c)) {
        return;
    }
    gfx_fill_rect(c, c->clip.x, c->clip.y, c->clip.w, c->clip.h, color);
}

void gfx_hline(gfx_canvas_t *c, int x, int y, int w, gfx_color_t color)
{
    gfx_hline_inl(c, x, y, w, color);
}

void gfx_vline(gfx_canvas_t *c, int x, int y, int h, gfx_color_t color)
{
    if (h == INT_MIN) {
        return;
    }
    if (h < 0) {
        y += h + 1;
        h = -h;
    }
    gfx_fill_rect(c, x, y, 1, h, color);
}

void gfx_draw_rect(gfx_canvas_t *c, int x, int y, int w, int h, gfx_color_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    gfx_hline(c, x, y, w, color);
    gfx_hline(c, x, y + h - 1, w, color);
    gfx_vline(c, x, y, h, color);
    gfx_vline(c, x + w - 1, y, h, color);
}

void gfx_line(gfx_canvas_t *c, int x0, int y0, int x1, int y1, gfx_color_t color)
{
    if (y0 == y1) {
        int x = GFX_MIN(x0, x1);
        gfx_hline(c, x, y0, (x0 > x1 ? x0 - x1 : x1 - x0) + 1, color);
        return;
    }
    if (x0 == x1) {
        int y = GFX_MIN(y0, y1);
        gfx_vline(c, x0, y, (y0 > y1 ? y0 - y1 : y1 - y0) + 1, color);
        return;
    }

    /*
     * Bound the walk before starting it.  Every pixel is clipped on the way
     * out, so an out-of-range endpoint is safe, but Bresenham steps once per
     * pixel along the whole span, and an endpoint near INT_MIN is billions of
     * iterations inside the render loop.
     */
    if (!canvas_ok(c)) {
        return;
    }
    /* Two bounds, neither of which moves a visible line: a line entirely off
     * one side of the clip is not walked at all, and a coordinate outside the
     * int16 range cannot belong to a line on a 1024 x 600 panel, so it is
     * saturated.  Clamping to the clip instead would change the slope of
     * lines that are drawn. */
    if ((x0 < c->clip.x && x1 < c->clip.x) ||
        (y0 < c->clip.y && y1 < c->clip.y) ||
        (x0 >= c->clip.x + c->clip.w && x1 >= c->clip.x + c->clip.w) ||
        (y0 >= c->clip.y + c->clip.h && y1 >= c->clip.y + c->clip.h)) {
        return;
    }
    x0 = gfx_i16(x0); x1 = gfx_i16(x1);
    y0 = gfx_i16(y0); y1 = gfx_i16(y1);

    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        gfx_pixel(c, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/**
 * @brief A line @p thickness px wide: a disc of half that radius stamped
 *        at each pixel of the line from (@p x0, @p y0) to (@p x1, @p y1).
 * @param c the canvas
 * @param x0 one end's column, px
 * @param y0 its row, px
 * @param x1 the other end's column, px
 * @param y1 its row, px
 * @param thickness the width, px; 1 or less is gfx_line()
 * @param color the colour
 */
void gfx_thick_line(gfx_canvas_t *c, int x0, int y0, int x1, int y1,
                    int thickness, gfx_color_t color)
{
    if (thickness <= 1) {
        gfx_line(c, x0, y0, x1, y1, color);
        return;
    }
    int r = thickness / 2;
    /*
     * Bound the walk before starting it.  Every pixel is clipped on the way
     * out, so an out-of-range endpoint is safe, but Bresenham steps once per
     * pixel along the whole span, and an endpoint near INT_MIN is billions of
     * iterations inside the render loop.
     */
    if (!canvas_ok(c)) {
        return;
    }
    /* Two bounds, neither of which moves a visible line: a line further
     * off one side of the clip than its stamp reaches is not walked at all,
     * and a coordinate outside the int16 range cannot belong to a line on a
     * 1024 x 600 panel, so it is saturated.  Clamping to the clip instead
     * would change the slope of lines that are drawn.  The stamp's reach
     * counts: a line just outside a clip still lays its edge inside it, and
     * a repaint clipped to a box must draw what the whole frame draws
     * there. */
    if ((x0 < c->clip.x - r && x1 < c->clip.x - r) ||
        (y0 < c->clip.y - r && y1 < c->clip.y - r) ||
        (x0 >= c->clip.x + c->clip.w + r && x1 >= c->clip.x + c->clip.w + r) ||
        (y0 >= c->clip.y + c->clip.h + r && y1 >= c->clip.y + c->clip.h + r)) {
        return;
    }
    x0 = gfx_i16(x0); x1 = gfx_i16(x1);
    y0 = gfx_i16(y0); y1 = gfx_i16(y1);

    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        gfx_fill_circle(c, x0, y0, r, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_draw_circle(gfx_canvas_t *c, int cx, int cy, int r, gfx_color_t color)
{
    if (r < 0) {
        return;
    }
    if (r == 0) {
        gfx_pixel(c, cx, cy, color);
        return;
    }
    int x = 0;
    int y = r;
    int d = 1 - r;

    while (x <= y) {
        gfx_pixel(c, cx + x, cy + y, color);
        gfx_pixel(c, cx - x, cy + y, color);
        gfx_pixel(c, cx + x, cy - y, color);
        gfx_pixel(c, cx - x, cy - y, color);
        gfx_pixel(c, cx + y, cy + x, color);
        gfx_pixel(c, cx - y, cy + x, color);
        gfx_pixel(c, cx + y, cy - x, color);
        gfx_pixel(c, cx - y, cy - x, color);
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            --y;
        }
        ++x;
    }
}

void gfx_fill_circle(gfx_canvas_t *c, int cx, int cy, int r, gfx_color_t color)
{
    if (r < 0) {
        return;
    }
    if (r == 0) {
        gfx_pixel(c, cx, cy, color);
        return;
    }
    int x = 0;
    int y = r;
    int d = 1 - r;

    gfx_hline(c, cx - r, cy, 2 * r + 1, color);
    while (x <= y) {
        if (y != 0) {
            gfx_hline(c, cx - x, cy + y, 2 * x + 1, color);
            gfx_hline(c, cx - x, cy - y, 2 * x + 1, color);
        }
        if (x != 0) {
            gfx_hline(c, cx - y, cy + x, 2 * y + 1, color);
            gfx_hline(c, cx - y, cy - x, 2 * y + 1, color);
        }
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            --y;
        }
        ++x;
    }
}

static int clamp_radius(int w, int h, int r)
{
    int limit = GFX_MIN(w, h) / 2;
    if (r > limit) {
        r = limit;
    }
    if (r < 0) {
        r = 0;
    }
    return r;
}

void gfx_fill_round_rect(gfx_canvas_t *c, int x, int y, int w, int h,
                         int r, gfx_color_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    TRACE_BOX(c, x, y, w, h);
    TRACE_HOLD();
    r = clamp_radius(w, h, r);
    if (r == 0) {
        gfx_fill_rect(c, x, y, w, h, color);
        TRACE_RELEASE();
        return;
    }

    /* Middle slab, then the two rounded caps. */
    gfx_fill_rect(c, x, y + r, w, h - 2 * r, color);

    int cx0 = x + r;
    int cx1 = x + w - r - 1;
    int cy0 = y + r;
    int cy1 = y + h - r - 1;

    int px = 0;
    int py = r;
    int d = 1 - r;
    while (px <= py) {
        /* top and bottom spans of the corner circles */
        gfx_hline(c, cx0 - px, cy0 - py, (cx1 - cx0) + 2 * px + 1, color);
        gfx_hline(c, cx0 - py, cy0 - px, (cx1 - cx0) + 2 * py + 1, color);
        gfx_hline(c, cx0 - px, cy1 + py, (cx1 - cx0) + 2 * px + 1, color);
        gfx_hline(c, cx0 - py, cy1 + px, (cx1 - cx0) + 2 * py + 1, color);
        if (d < 0) {
            d += 2 * px + 3;
        } else {
            d += 2 * (px - py) + 5;
            --py;
        }
        ++px;
    }
    TRACE_RELEASE();
}

void gfx_draw_round_rect(gfx_canvas_t *c, int x, int y, int w, int h,
                         int r, gfx_color_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    r = clamp_radius(w, h, r);
    if (r == 0) {
        gfx_draw_rect(c, x, y, w, h, color);
        return;
    }

    gfx_hline(c, x + r, y, w - 2 * r, color);
    gfx_hline(c, x + r, y + h - 1, w - 2 * r, color);
    gfx_vline(c, x, y + r, h - 2 * r, color);
    gfx_vline(c, x + w - 1, y + r, h - 2 * r, color);

    int cx0 = x + r;
    int cx1 = x + w - r - 1;
    int cy0 = y + r;
    int cy1 = y + h - r - 1;

    int px = 0;
    int py = r;
    int d = 1 - r;
    while (px <= py) {
        gfx_pixel(c, cx1 + px, cy1 + py, color);
        gfx_pixel(c, cx1 + py, cy1 + px, color);
        gfx_pixel(c, cx0 - px, cy1 + py, color);
        gfx_pixel(c, cx0 - py, cy1 + px, color);
        gfx_pixel(c, cx1 + px, cy0 - py, color);
        gfx_pixel(c, cx1 + py, cy0 - px, color);
        gfx_pixel(c, cx0 - px, cy0 - py, color);
        gfx_pixel(c, cx0 - py, cy0 - px, color);
        if (d < 0) {
            d += 2 * px + 3;
        } else {
            d += 2 * (px - py) + 5;
            --py;
        }
        ++px;
    }
}

/* ------------------------------------------------------------- chamfers */

/*
 * Half the shorter edge, not all of it.  Two cuts share every edge, so a limit
 * of min(w, h) lets tl + tr exceed w: the fill then drops whole rows and the
 * outline hands a negative length to gfx_hline, whose documented backwards run
 * draws it outside the rectangle.  Round rects already clamp this way.
 */
static int clamp_cut(int w, int h, int cut)
{
    int limit = GFX_MIN(w, h) / 2;
    if (cut > limit) { cut = limit; }
    if (cut < 0) { cut = 0; }
    return cut;
}

void gfx_fill_chamfer_rect_ex(gfx_canvas_t *c, int x, int y, int w, int h,
                              int tl, int tr, int br, int bl, gfx_color_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    TRACE_BOX(c, x, y, w, h);
    TRACE_HOLD();
    tl = clamp_cut(w, h, tl);
    tr = clamp_cut(w, h, tr);
    br = clamp_cut(w, h, br);
    bl = clamp_cut(w, h, bl);

    /* Row-major on purpose: one span per scanline keeps the framebuffer's
     * cache lines hot, which matters far more here than the arithmetic. */
    for (int row = 0; row < h; ++row) {
        int left = 0;
        int right = 0;
        if (row < tl) { left = tl - row; }
        if (row < tr) { right = tr - row; }
        int from_bottom = h - 1 - row;
        if (from_bottom < bl) { left = GFX_MAX(left, bl - from_bottom); }
        if (from_bottom < br) { right = GFX_MAX(right, br - from_bottom); }

        int span = w - left - right;
        if (span > 0) {
            gfx_hline(c, x + left, y + row, span, color);
        }
    }
    TRACE_RELEASE();
}

void gfx_fill_chamfer_rect(gfx_canvas_t *c, int x, int y, int w, int h,
                           int cut, gfx_color_t color)
{
    gfx_fill_chamfer_rect_ex(c, x, y, w, h, cut, cut, cut, cut, color);
}

void gfx_draw_chamfer_rect_ex(gfx_canvas_t *c, int x, int y, int w, int h,
                              int tl, int tr, int br, int bl, gfx_color_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    tl = clamp_cut(w, h, tl);
    tr = clamp_cut(w, h, tr);
    br = clamp_cut(w, h, br);
    bl = clamp_cut(w, h, bl);

    int x1 = x + w - 1;
    int y1 = y + h - 1;

    if (w - tl - tr > 0) { gfx_hline(c, x + tl, y, w - tl - tr, color); }
    if (w - bl - br > 0) { gfx_hline(c, x + bl, y1, w - bl - br, color); }
    if (h - tl - bl > 0) { gfx_vline(c, x, y + tl, h - tl - bl, color); }
    if (h - tr - br > 0) { gfx_vline(c, x1, y + tr, h - tr - br, color); }

    if (tl) { gfx_line(c, x, y + tl, x + tl, y, color); }
    if (tr) { gfx_line(c, x1 - tr, y, x1, y + tr, color); }
    if (br) { gfx_line(c, x1, y1 - br, x1 - br, y1, color); }
    if (bl) { gfx_line(c, x + bl, y1, x, y1 - bl, color); }
}

void gfx_draw_chamfer_rect(gfx_canvas_t *c, int x, int y, int w, int h,
                           int cut, gfx_color_t color)
{
    gfx_draw_chamfer_rect_ex(c, x, y, w, h, cut, cut, cut, cut, color);
}

void gfx_fill_rect_gradient(gfx_canvas_t *c, int x, int y, int w, int h,
                            gfx_color_t a, gfx_color_t b)
{
    if (!canvas_ok(c) || w <= 0 || h <= 0) {
        return;
    }
    for (int row = 0; row < h; ++row) {
        uint8_t t = (h > 1) ? (uint8_t)((row * 255) / (h - 1)) : 0;
        gfx_hline(c, x, y + row, w, gfx_lerp(a, b, t));
    }
}

/* ------------------------------------------------------------------- blits */

void gfx_blit(gfx_canvas_t *c, int x, int y, const gfx_color_t *src,
              int w, int h, int32_t src_stride)
{
    if (!canvas_ok(c) || !src || w <= 0 || h <= 0) {
        return;
    }
    TRACE_BOX(c, x, y, w, h);
    if (src_stride <= 0) {
        src_stride = w;
    }
    gfx_rect_t r;
    if (!gfx_rect_intersect(gfx_rect_make(x, y, w, h), c->clip, &r)) {
        return;
    }
    GFX_DAMAGE(c, r.x, r.y, r.w, r.h);
    int sx = r.x - x;
    int sy = r.y - y;
    for (int row = 0; row < r.h; ++row) {
        const gfx_color_t *s = src + (ptrdiff_t)(sy + row) * src_stride + sx;
        gfx_color_t *d = c->pixels + (ptrdiff_t)(r.y + row) * c->stride + r.x;
        memcpy(d, s, (size_t)r.w * sizeof(gfx_color_t));
    }
}

void gfx_blit_key(gfx_canvas_t *c, int x, int y, const gfx_color_t *src,
                  int w, int h, int32_t src_stride, gfx_color_t key)
{
    if (!canvas_ok(c) || !src || w <= 0 || h <= 0) {
        return;
    }
    if (src_stride <= 0) {
        src_stride = w;
    }
    gfx_rect_t r;
    if (!gfx_rect_intersect(gfx_rect_make(x, y, w, h), c->clip, &r)) {
        return;
    }
    GFX_DAMAGE(c, r.x, r.y, r.w, r.h);
    int sx = r.x - x;
    int sy = r.y - y;
    for (int row = 0; row < r.h; ++row) {
        const gfx_color_t *s = src + (ptrdiff_t)(sy + row) * src_stride + sx;
        gfx_color_t *d = c->pixels + (ptrdiff_t)(r.y + row) * c->stride + r.x;
        for (int col = 0; col < r.w; ++col) {
            if (s[col] != key) {
                d[col] = s[col];
            }
        }
    }
}

void gfx_blit_1bpp(gfx_canvas_t *c, int x, int y, const uint8_t *bits,
                   int w, int h, gfx_color_t fg)
{
    if (!canvas_ok(c) || !bits || w <= 0 || h <= 0) {
        return;
    }
    int bytes_per_row = (w + 7) / 8;
    for (int row = 0; row < h; ++row) {
        const uint8_t *line = bits + (size_t)row * bytes_per_row;
        for (int col = 0; col < w; ++col) {
            if (line[col >> 3] & (0x80u >> (col & 7))) {
                gfx_pixel(c, x + col, y + row, fg);
            }
        }
    }
}

/*
 * Coverage-filled shapes: a disc and a capsule, both from the distance to
 * their own skeleton.
 *
 * Everything else on the servo card is antialiased -- the segment numerals,
 * the arcs, the text -- and a hard-edged arm in the middle of it looks like a
 * different drawing pasted in.  Working from distance rather than from a
 * scanline span means the round caps and the straight sides come out of one
 * expression, so there is no seam where a cap meets a side, which is what a
 * disc drawn over a rectangle produces: two soft edges blended twice.
 *
 * The bounding box is clipped before the loop, not inside it, because the
 * grip's breathing repaints a 72-pixel box and must not pay for the whole
 * arm's extent to do it.
 */
/**
 * @brief One antialiased pixel: @p col over what is there, by @p cov.
 *
 * Not reported to the damage record pixel by pixel: each caller reports
 * the row's span once (GFX_DAMAGE), which in NORMAL saves a test per pixel.
 *
 * @param c the canvas
 * @param x the pixel's column
 * @param y its row
 * @param col the colour
 * @param cov the coverage, 0 to 1; 1 and above writes @p col
 */
static void cov_blend(gfx_canvas_t *c, int x, int y, gfx_color_t col, float cov)
{
    if (cov <= 0.0f) {
        return;
    }
    GFX_VISIT(1u);
    if (!canvas_ok(c) || !gfx_rect_contains(c->clip, x, y)) {
        return;
    }
    gfx_color_t *p = &c->pixels[(ptrdiff_t)y * c->stride + x];
    *p = (cov >= 1.0f) ? col : gfx_lerp(*p, col, (uint8_t)(cov * 255.0f));
}

/* The box to walk, already intersected with the clip.  False if nothing of
 * it survives, so the caller can skip the shape entirely. */
static bool cov_box(const gfx_canvas_t *c, float cx, float cy, float reach,
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

/** @brief A run of pixels of one row, from @c lo up to @c hi. */
typedef struct {
    int lo;   /**< the first column */
    int hi;   /**< one past the last */
} span_t;

/**
 * @brief Narrow @p sp to the columns of the row @p dy from a centre at
 *        @p cx that can lie nearer the centre than @p reach: those more
 *        than a pixel further are left out, so a loop over a shape's box
 *        walks the shape's own pixels.
 * @param cx the centre's column, px
 * @param dy the row's distance from the centre's row, px
 * @param reach the distance, px
 * @param sp the run, narrowed in place
 * @return false when nothing of the row is left
 */
static bool span_round(float cx, float dy, float reach, span_t *sp)
{
    const float r2 = (reach + 1.0f) * (reach + 1.0f) - dy * dy;
    if (!(r2 > 0.0f)) {
        return false;
    }
    const float hs = sqrtf(r2);
    sp->lo = GFX_MAX(sp->lo, (int)floorf(cx - hs) - 1);
    sp->hi = GFX_MIN(sp->hi, (int)ceilf(cx + hs) + 2);
    return sp->hi > sp->lo;
}

/**
 * @brief Set @p sp to columns of the row @p dy from a centre at @p cx that
 *        lie at least a pixel nearer the centre than @p reach: inside it
 *        whatever a float's rounding does to the distance.
 * @param cx the centre's column, px
 * @param dy the row's distance from the centre's row, px
 * @param reach the distance, px
 * @param sp the run: left empty, at @c sp->hi, when there is none;
 *           kept within the columns it held
 * @return true when the run holds a column
 */
static bool span_inside(float cx, float dy, float reach, span_t *sp)
{
    const int lo = sp->lo, hi = sp->hi;
    sp->lo = hi;
    const float r2 = reach * reach - dy * dy;
    if (!(reach > 0.0f) || !(r2 > 0.0f)) {
        return false;
    }
    const float hs = sqrtf(r2) - 1.0f;
    if (!(hs > 0.0f)) {
        return false;
    }
    const int a = GFX_MAX(lo, (int)ceilf(cx - hs));
    const int b = GFX_MIN(hi, (int)floorf(cx + hs) + 1);
    if (b <= a) {
        return false;
    }
    sp->lo = a;
    sp->hi = b;
    return true;
}

void gfx_fill_circle_aaf(gfx_canvas_t *c, float cx, float cy, float r,
                         gfx_color_t col)
{
    if (!canvas_ok(c) || !(r > 0.0f)) {
        return;
    }
    int x0, y0, x1, y1;
    if (!cov_box(c, cx, cy, r + 1.0f, &x0, &y0, &x1, &y1)) {
        return;
    }
    for (int y = y0; y < y1; ++y) {
        const float dy = (float)y - cy;
        span_t sp = { x0, x1 };
        if (!span_round(cx, dy, r + 0.5f, &sp)) {
            continue;
        }
        /* The pixels the disc covers wholly, as one run: the same colour
         * cov_blend() writes there, without a root each. */
        span_t in = sp;
        (void)span_inside(cx, dy, r - 0.5f, &in);
        GFX_VISIT(sp.hi - sp.lo - (in.hi - in.lo));
        GFX_DAMAGE(c, sp.lo, y, sp.hi - sp.lo, 1);
        for (int x = sp.lo; x < sp.hi; ++x) {
            if (x == in.lo && in.hi > in.lo) {
                gfx_fill_rect(c, in.lo, y, in.hi - in.lo, 1, col);
                x = in.hi - 1;
                continue;
            }
            const float dx = (float)x - cx;
            const float d  = sqrtf(dx * dx + dy * dy);
            cov_blend(c, x, y, col, r + 0.5f - d);
        }
    }
}

/**
 * @brief gfx_fill_circle_aaf() on whole pixels.
 * @param c the canvas
 * @param cx the centre's column, px
 * @param cy the centre's row, px
 * @param r the radius, px; 0 or less draws nothing
 * @param col the colour
 */
void gfx_fill_circle_aa(gfx_canvas_t *c, int cx, int cy, int r,
                        gfx_color_t col)
{
    if (r <= 0) {
        return;
    }
    gfx_fill_circle_aaf(c, (float)cx, (float)cy, (float)r, col);
}

/**
 * @brief Narrow @p sp to the columns of row @p y that can lie nearer than
 *        @p reach to the segment from (@p ax, @p ay) along (@p ex, @p ey):
 *        the part of the segment within a pixel more than @p reach of the
 *        row, widened by that much and a pixel either side.
 * @param ax the segment's start column, px
 * @param ay its start row, px
 * @param ex its run in columns, px
 * @param ey its run in rows, px
 * @param reach the distance, px
 * @param y the row, px
 * @param sp the run, narrowed in place
 * @return false when nothing of the row is left
 */
static bool span_capsule(float ax, float ay, float ex, float ey, float reach,
                         float y, span_t *sp)
{
    const float wide = reach + 1.0f;
    float t0 = 0.0f, t1 = 1.0f;
    if (fabsf(ey) > 1e-6f) {
        float a = (y - wide - ay) / ey, b = (y + wide - ay) / ey;
        if (a > b) {
            const float k = a;
            a = b;
            b = k;
        }
        t0 = fmaxf(a, 0.0f);
        t1 = fminf(b, 1.0f);
        if (t0 > t1) {
            return false;
        }
    } else if (fabsf(y - ay) >= wide) {
        return false;
    }
    const float xa = ax + t0 * ex, xb = ax + t1 * ex;
    sp->lo = GFX_MAX(sp->lo, (int)floorf(fminf(xa, xb) - wide) - 1);
    sp->hi = GFX_MIN(sp->hi, (int)ceilf(fmaxf(xa, xb) + wide) + 2);
    return sp->hi > sp->lo;
}

void gfx_capsule_aaf(gfx_canvas_t *c, float ax, float ay, float bx, float by,
                     float thickness, gfx_color_t col)
{
    if (!canvas_ok(c) || !(thickness > 0.0f)) {
        return;
    }
    const float hw = thickness / 2.0f;
    const float ex = bx - ax, ey = by - ay;
    const float len2 = ex * ex + ey * ey;

    const float midx = (ax + bx) / 2.0f;
    const float midy = (ay + by) / 2.0f;
    const float reach = sqrtf(len2) / 2.0f + hw + 1.0f;
    int x0, y0, x1, y1;
    if (!cov_box(c, midx, midy, reach, &x0, &y0, &x1, &y1)) {
        return;
    }

    for (int y = y0; y < y1; ++y) {
        span_t sp = { x0, x1 };
        if (!span_capsule(ax, ay, ex, ey, hw + 0.5f, (float)y, &sp)) {
            continue;
        }
        GFX_VISIT(sp.hi - sp.lo);
        GFX_DAMAGE(c, sp.lo, y, sp.hi - sp.lo, 1);
        for (int x = sp.lo; x < sp.hi; ++x) {
            const float px = (float)x - ax, py = (float)y - ay;
            /* How far along the segment the nearest point is, clamped to its
             * ends -- which is what turns the sides and the caps into one
             * distance. */
            float t = (len2 > 0.0f) ? (px * ex + py * ey) / len2 : 0.0f;
            if (t < 0.0f) { t = 0.0f; }
            if (t > 1.0f) { t = 1.0f; }
            const float qx = px - t * ex, qy = py - t * ey;
            const float d  = sqrtf(qx * qx + qy * qy);
            cov_blend(c, x, y, col, hw + 0.5f - d);
        }
    }
}

/**
 * @brief gfx_capsule_aaf() on whole pixels.
 * @param c the canvas
 * @param ax one end's column, px
 * @param ay its row, px
 * @param bx the other end's column, px
 * @param by its row, px
 * @param thickness the width, px; 0 or less draws nothing
 * @param col the colour
 */
void gfx_capsule_aa(gfx_canvas_t *c, int ax, int ay, int bx, int by,
                    int thickness, gfx_color_t col)
{
    if (thickness <= 0) {
        return;
    }
    gfx_capsule_aaf(c, (float)ax, (float)ay, (float)bx, (float)by,
                    (float)thickness, col);
}

/**
 * @brief How far into the arc from @p a0_deg over @p span degrees the
 *        direction @p ang_deg lies, in degrees: positive inside, from the
 *        nearer end; negative outside, to the nearer end.
 * @param ang_deg the pixel's direction, -180 to 180deg
 * @param a0_deg where the arc starts
 * @param span its length, 0 to 360deg
 * @return the signed distance in degrees
 */
static float arc_depth(float ang_deg, float a0_deg, float span)
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

/**
 * @brief The directions of a partial ring that need no angle worked out:
 *        the middle of its lit part, drawn in the full colour at the full
 *        coverage, and the middle of its gap, drawn not at all.
 *
 * A pixel's distance from the arc's middle direction is read off the dot
 * product with that direction, without atan2f(): within @c in_cos of it
 * the pixel is further than the fade and half a pixel of arc inside both
 * ends, beyond @c out_cos it is half a pixel of arc outside them, each with
 * 0.5deg to spare.  Between the two the angle is worked out as before, so
 * every pixel comes out as it did.
 */
typedef struct {
    float mx;         /**< the middle direction's x */
    float my;         /**< its y, up the screen as +y */
    bool  has_in;     /**< in_cos holds */
    float in_cos;     /**< cosine within which a pixel is lit whole */
    bool  has_out;    /**< out_cos holds */
    float out_cos;    /**< cosine beyond which a pixel is in the gap */
} ring_cut_t;

/**
 * @brief The cut of a ring from @p lo over @p span degrees whose band
 *        starts at @p inner px, fading over @p fade_deg.
 * @param lo the arc's first direction, degrees
 * @param span its length, under 360 degrees
 * @param inner the band's inner edge, px
 * @param fade_deg the fade at each end, degrees, 0 for none
 * @return the cut; neither part when @p inner is under a pixel
 */
static ring_cut_t ring_cut(float lo, float span, float inner, float fade_deg)
{
    const float k = 3.14159265358979f / 180.0f;
    ring_cut_t q = { 0 };
    const float mid = (lo + span / 2.0f) * k;
    q.mx = cosf(mid);
    q.my = sinf(mid);
    if (!(inner >= 1.0f)) {
        return q;
    }
    /* Half a pixel of arc at the band's inner edge, in degrees. */
    const float edge = 0.5f / (k * inner);
    const float lit = span / 2.0f - (fmaxf(fade_deg, edge) + 0.5f);
    const float gap = span / 2.0f + edge + 0.5f;
    if (lit >= 2.0f) {
        q.has_in = true;
        q.in_cos = cosf(lit * k);
    }
    if (gap <= 178.0f) {
        q.has_out = true;
        q.out_cos = cosf(gap * k);
    }
    return q;
}

void gfx_ring_aaf(gfx_canvas_t *c, float cx, float cy, float r,
                  float thickness, float a0_deg, float a1_deg,
                  gfx_color_t col, gfx_color_t into, float fade_deg)
{
    if (!canvas_ok(c) || !(r > 0.0f) || !(thickness > 0.0f)) {
        return;
    }
    const float k     = 3.14159265358979f / 180.0f;
    const float half  = thickness / 2.0f;
    const float outer = r + half + 0.5f;    /* where the coverage is 0 */
    const float inner = r - half - 0.5f;
    const float lo = (a0_deg < a1_deg) ? a0_deg : a1_deg;
    const float span = fabsf(a1_deg - a0_deg);
    const bool whole = span >= 360.0f;
    int x0, y0, x1, y1;
    if (!cov_box(c, cx, cy, outer + 0.5f, &x0, &y0, &x1, &y1)) {
        return;
    }
    const float out2 = outer * outer;
    /* A band thicker than 2r - 1 reaches over the centre: no hole. */
    const bool  has_hole = inner > 0.0f;
    const float in2  = has_hole ? inner * inner : 0.0f;
    ring_cut_t cut = { 0 };
    if (!whole) {
        cut = ring_cut(lo, span, inner, fade_deg);
    }
    /* What a pixel lit whole is drawn in: lit at 1, as below. */
    const gfx_color_t full = (fade_deg > 0.0f) ? gfx_lerp(into, col, 255u)
                                               : col;
    for (int y = y0; y < y1; ++y) {
        const float dy = (float)y - cy;
        /* The band's columns of this row, less the hole inside it. */
        span_t sp = { x0, x1 };
        if (!span_round(cx, dy, outer, &sp)) {
            continue;
        }
        span_t hole = { sp.lo, sp.hi };
        (void)span_inside(cx, dy, inner, &hole);
        GFX_VISIT(sp.hi - sp.lo - (hole.hi - hole.lo));
        GFX_DAMAGE(c, sp.lo, y, sp.hi - sp.lo, 1);
        for (int x = sp.lo; x < sp.hi; ++x) {
            if (x == hole.lo && hole.hi > hole.lo) {
                x = hole.hi - 1;
                continue;
            }
            const float dx = (float)x - cx;
            const float d2 = dx * dx + dy * dy;
            if (d2 >= out2 || (has_hole && d2 <= in2)) {
                continue;               /* off the band: no root, no angle */
            }
            const float d = sqrtf(d2);
            float cov = half + 0.5f - fabsf(d - r);
            if (cov > 1.0f) {
                cov = 1.0f;
            }
            gfx_color_t here = col;
            /* The cosine of the pixel's direction from the arc's middle,
             * times d. */
            const float dot = dx * cut.mx - dy * cut.my;
            if (!whole && cut.has_in && dot >= d * cut.in_cos) {
                here = full;
            } else if (!whole && cut.has_out && dot <= d * cut.out_cos) {
                continue;
            } else if (!whole) {
                /* Up the screen is +y in the arc's angles. */
                const float e = arc_depth(atan2f(-dy, dx) / k, lo, span);
                /* The end blended over half a pixel either side of it,
                 * measured along the arc at this radius. */
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
            cov_blend(c, x, y, here, cov);
        }
    }
}

/*
 * An arc, walked rather than solved.
 *
 * Stepping by one pixel of arc length at the *outer* radius is what stops it
 * dashing: step by the inner radius instead and the outside of a thick arc
 * comes out as a row of dots with the gaps growing as the radius does.
 *
 * The two radial edges are blended the same way a segment bar's ends are, so
 * an arc and a numeral on this screen have the same kind of edge.
 *
 * @p fade_deg, when non-zero, brings each end up out of @p into and takes it
 * back down again -- an arc that stops dead has two bright full-width ends
 * that read as breakage, where one that dissolves reads as an arc that
 * happens to be lit over part of its length.
 */
/** @brief The directions an arc is walked in, as arc_window() finds them:
 *         from @c lo over @c span degrees, 360 for all of them. */
typedef struct {
    float lo;     /**< the first direction, degrees */
    float span;   /**< how far on from it, 0 to 360 degrees */
} arc_dirs_t;

/** @brief The direction of (@p x, @p y) from (@p cx, @p cy), degrees, up
 *         the screen as +90, as arc_walk() places its pixels. */
static float dir_of(int x, int y, int cx, int cy)
{
    return atan2f((float)(cy - y), (float)(x - cx)) * (180.0f / 3.14159265f);
}

/**
 * @brief Where an arc round (@p cx, @p cy) between radii @p r0 and @p r1
 *        can reach the clip of @p c.
 *
 * A pixel arc_walk() lays lies within 1.5px of its step's point on the
 * ray: its coordinates are truncated, not rounded, on the negative side.
 * So a clip wholly nearer the centre than r0 - 3 or wholly further than
 * r1 + 2 gets nothing, and a clip off the centre is reached only from the
 * directions of its corners, widened by 3px of arc at r0 - 1.  A repaint
 * clipped to a box beside a dial walks the part of the dial's arc that
 * passes the box, not all of it.
 *
 * @param c the canvas
 * @param cx the centre's column, px
 * @param cy its row, px
 * @param r0 the inner radius, px
 * @param r1 the outer radius, px
 * @param out the directions to walk, when the clip can be reached
 * @return false when no pixel of the arc can land in the clip
 */
static bool arc_window(const gfx_canvas_t *c, int cx, int cy, float r0,
                       float r1, arc_dirs_t *out)
{
    out->lo   = 0.0f;
    out->span = 360.0f;
    /* The clip's pixel centres. */
    const int x0 = c->clip.x, x1 = c->clip.x + c->clip.w - 1;
    const int y0 = c->clip.y, y1 = c->clip.y + c->clip.h - 1;
    const float nx = (float)((cx < x0) ? x0 - cx : (cx > x1) ? cx - x1 : 0);
    const float ny = (float)((cy < y0) ? y0 - cy : (cy > y1) ? cy - y1 : 0);
    const float fx = (float)GFX_MAX(abs(cx - x0), abs(x1 - cx));
    const float fy = (float)GFX_MAX(abs(cy - y0), abs(y1 - cy));
    const float near = sqrtf(nx * nx + ny * ny);
    const float far  = sqrtf(fx * fx + fy * fy);
    if (far < r0 - 3.0f || near > r1 + 2.0f) {
        return false;
    }
    /* The centre in the clip or next to it, or a radius too small for a
     * margin of 3px of arc: every direction. */
    if (near < 2.0f || r0 < 8.0f) {
        return true;
    }
    const int kx[4] = { x0, x1, x0, x1 };
    const int ky[4] = { y0, y0, y1, y1 };
    const float ref = dir_of(kx[0], ky[0], cx, cy);
    float lo = 0.0f, hi = 0.0f;
    for (int i = 1; i < 4; ++i) {
        const float d = remainderf(dir_of(kx[i], ky[i], cx, cy) - ref,
                                   360.0f);
        lo = fminf(lo, d);
        hi = fmaxf(hi, d);
    }
    const float m = 3.0f / (r0 - 1.0f) * (180.0f / 3.14159265f) + 0.1f;
    out->lo   = ref + lo - m;
    out->span = hi - lo + 2.0f * m;
    if (out->span >= 360.0f) {
        out->span = 360.0f;
    }
    return true;
}

/**
 * @brief Whether the direction @p a_deg is one of @p w's.
 * @param w the directions
 * @param a_deg the direction, degrees, any turn
 * @return true when it is
 */
static bool arc_dir_in(const arc_dirs_t *w, float a_deg)
{
    if (w->span >= 360.0f) {
        return true;
    }
    float u = fmodf(a_deg - w->lo, 360.0f);
    if (u < 0.0f) {
        u += 360.0f;
    }
    return u <= w->span;
}

static void arc_walk(gfx_canvas_t *c, int cx, int cy, int r, int thickness,
                     float a0_deg, float a1_deg, gfx_color_t col,
                     gfx_color_t into, float fade_deg)
{
    if (!canvas_ok(c) || r <= 0 || thickness <= 0) {
        return;
    }
    const float k    = 3.14159265358979f / 180.0f;
    const float half = (float)thickness / 2.0f;
    const float r0   = (float)r - half;
    const float r1   = (float)r + half;
    if (r0 <= 0.0f) {
        return;
    }
    arc_dirs_t dirs;
    if (!arc_window(c, cx, cy, r0, r1, &dirs)) {
        return;
    }

    const float span  = fabsf(a1_deg - a0_deg);
    const float sweep = span * k;
    int steps = (int)(sweep * r1) + 1;
    if (steps > 4096) {
        steps = 4096;      /* a full turn at any radius this panel can hold */
    }

    for (int i = 0; i <= steps; ++i) {
        const float f  = (float)i / (float)steps;
        const float a_deg = a0_deg + (a1_deg - a0_deg) * f;
        if (!arc_dir_in(&dirs, a_deg)) {
            continue;
        }
        GFX_VISIT(1u);
        const float a  = a_deg * k;
        const float ca = cosf(a), sa = sinf(a);

        gfx_color_t here = col;
        if (fade_deg > 0.0f) {
            const float from_end = fminf(f, 1.0f - f) * span;
            float lit = from_end / fade_deg;
            if (lit > 1.0f) {
                lit = 1.0f;
            }
            here = gfx_lerp(into, col, (uint8_t)(lit * 255.0f));
        }

        const int in  = (int)ceilf(r0);
        const int out = (int)floorf(r1);
        if ((float)in > r0) {
            const float cov = (float)in - r0;
            const int px = cx + (int)((float)(in - 1) * ca + 0.5f);
            const int py = cy - (int)((float)(in - 1) * sa + 0.5f);
            gfx_pixel(c, px, py, gfx_lerp(gfx_pixel_get(c, px, py), here,
                                          (uint8_t)(cov * 255.0f)));
        }
        for (int rr = in; rr < out; ++rr) {
            gfx_pixel(c, cx + (int)((float)rr * ca + 0.5f),
                         cy - (int)((float)rr * sa + 0.5f), here);
        }
        if (r1 > (float)out) {
            const float cov = r1 - (float)out;
            const int px = cx + (int)((float)out * ca + 0.5f);
            const int py = cy - (int)((float)out * sa + 0.5f);
            gfx_pixel(c, px, py, gfx_lerp(gfx_pixel_get(c, px, py), here,
                                          (uint8_t)(cov * 255.0f)));
        }
    }
}

void gfx_arc(gfx_canvas_t *c, int cx, int cy, int r, int thickness,
             float a0_deg, float a1_deg, gfx_color_t col)
{
    arc_walk(c, cx, cy, r, thickness, a0_deg, a1_deg, col, col, 0.0f);
}

void gfx_arc_fade(gfx_canvas_t *c, int cx, int cy, int r, int thickness,
                  float a0_deg, float a1_deg, gfx_color_t col,
                  gfx_color_t into, float fade_deg)
{
    arc_walk(c, cx, cy, r, thickness, a0_deg, a1_deg, col, into, fade_deg);
}

