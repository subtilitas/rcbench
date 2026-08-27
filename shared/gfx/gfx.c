/*
 * gfx -- RGB565 rasteriser.  See include/gfx.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gfx.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define GFX_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GFX_MAX(a, b) ((a) > (b) ? (a) : (b))

/* The framebuffer is in PSRAM behind a write-back, write-allocate cache with
 * 64-byte lines, so every byte written costs two bytes on the bus: the line is
 * fetched before it is overwritten, then written back.  Filling a span 32 bits
 * at a time halves the store count and lets the write buffer coalesce.
 * may_alias keeps this legal without disabling strict aliasing globally. */
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
    /* Against the parent's clip, not just its bounds: a window carved from a
     * narrowed canvas otherwise gets write access the parent had given up. */
    if (!gfx_rect_intersect(window, src->clip, &r) ||
        !gfx_rect_intersect(r, gfx_rect_make(0, 0, src->width, src->height), &r)) {
        gfx_canvas_init(out, src->pixels, 0, 0, src->stride);
        return false;
    }
    out->pixels = src->pixels + (int32_t)r.y * src->stride + r.x;
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
    if (!c) {
        return false;
    }
    return gfx_rect_intersect(r, c->clip, &c->clip);
}

/* -------------------------------------------------------------- primitives */

static inline bool canvas_ok(const gfx_canvas_t *c)
{
    return c && c->pixels && !gfx_rect_empty(c->clip);
}

void gfx_pixel(gfx_canvas_t *c, int x, int y, gfx_color_t color)
{
    if (!canvas_ok(c) || !gfx_rect_contains(c->clip, x, y)) {
        return;
    }
    c->pixels[(int32_t)y * c->stride + x] = color;
}

gfx_color_t gfx_pixel_get(const gfx_canvas_t *c, int x, int y)
{
    if (!c || !c->pixels || x < 0 || y < 0 || x >= c->width || y >= c->height) {
        return 0;
    }
    return c->pixels[(int32_t)y * c->stride + x];
}

void gfx_fill_rect(gfx_canvas_t *c, int x, int y, int w, int h, gfx_color_t color)
{
    if (!canvas_ok(c)) {
        return;
    }
    gfx_rect_t r;
    if (!gfx_rect_intersect(gfx_rect_make(x, y, w, h), c->clip, &r)) {
        return;
    }
    /* A full-width fill over a tightly packed canvas is one contiguous run,
     * which is worth a lot more than it looks: it is the difference between
     * one long burst and h separate ones. */
    if (r.w == c->stride && r.x == 0) {
        fill_span(c->pixels + (int32_t)r.y * c->stride, (int)((int32_t)r.h * c->stride),
                  color);
        return;
    }

    for (int row = 0; row < r.h; ++row) {
        fill_span(c->pixels + (int32_t)(r.y + row) * c->stride + r.x, r.w, color);
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
    if (w == INT_MIN) {
        return;             /* -INT_MIN is undefined, and no line is this long */
    }
    if (w < 0) {
        x += w + 1;
        w = -w;
    }
    gfx_fill_rect(c, x, y, w, 1, color);
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
     * out, so a wildly out-of-range endpoint is safe -- but Bresenham still
     * steps once per pixel along the whole span, and an endpoint near INT_MIN
     * means billions of iterations inside the render loop.  A caller that
     * produces such a coordinate has a bug; the rasteriser should not turn it
     * into a hang.  Clamping the endpoints changes the slope slightly, which
     * only affects lines that were never going to be visible anyway.
     */
    if (!canvas_ok(c)) {
        return;
    }
    /* Two bounds on the walk, neither of which moves a line you can see.
     *
     * A line entirely off one side of the clip draws nothing, so do not walk
     * it at all.  And a coordinate outside the int16 range cannot belong to a
     * line on an 800x480 panel, so saturate it: every pixel is clipped on the
     * way out anyway, but Bresenham still steps once per pixel along the whole
     * span, and an endpoint near INT_MIN is billions of iterations inside the
     * render loop.  Clamping to the clip instead would be tighter and would
     * also change the slope of lines that really are drawn -- 47 pixels of the
     * bench screen, as the golden images pointed out. */
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
     * out, so a wildly out-of-range endpoint is safe -- but Bresenham still
     * steps once per pixel along the whole span, and an endpoint near INT_MIN
     * means billions of iterations inside the render loop.  A caller that
     * produces such a coordinate has a bug; the rasteriser should not turn it
     * into a hang.  Clamping the endpoints changes the slope slightly, which
     * only affects lines that were never going to be visible anyway.
     */
    if (!canvas_ok(c)) {
        return;
    }
    /* Two bounds on the walk, neither of which moves a line you can see.
     *
     * A line entirely off one side of the clip draws nothing, so do not walk
     * it at all.  And a coordinate outside the int16 range cannot belong to a
     * line on an 800x480 panel, so saturate it: every pixel is clipped on the
     * way out anyway, but Bresenham still steps once per pixel along the whole
     * span, and an endpoint near INT_MIN is billions of iterations inside the
     * render loop.  Clamping to the clip instead would be tighter and would
     * also change the slope of lines that really are drawn -- 47 pixels of the
     * bench screen, as the golden images pointed out. */
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
    r = clamp_radius(w, h, r);
    if (r == 0) {
        gfx_fill_rect(c, x, y, w, h, color);
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
    if (src_stride <= 0) {
        src_stride = w;
    }
    gfx_rect_t r;
    if (!gfx_rect_intersect(gfx_rect_make(x, y, w, h), c->clip, &r)) {
        return;
    }
    int sx = r.x - x;
    int sy = r.y - y;
    for (int row = 0; row < r.h; ++row) {
        const gfx_color_t *s = src + (int32_t)(sy + row) * src_stride + sx;
        gfx_color_t *d = c->pixels + (int32_t)(r.y + row) * c->stride + r.x;
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
    int sx = r.x - x;
    int sy = r.y - y;
    for (int row = 0; row < r.h; ++row) {
        const gfx_color_t *s = src + (int32_t)(sy + row) * src_stride + sx;
        gfx_color_t *d = c->pixels + (int32_t)(r.y + row) * c->stride + r.x;
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

/* -------------------------------------------------------------------- text */

static const uint8_t *glyph_rows(const gfx_font_t *font, unsigned char ch)
{
    if (ch < font->first || ch > font->last) {
        ch = '?';
        if (ch < font->first || ch > font->last) {
            return NULL;
        }
    }
    size_t stride = font->bytes_per_row ? font->bytes_per_row : 1u;
    return font->glyphs + (size_t)(ch - font->first) * font->height * stride;
}

int gfx_text_height(const gfx_font_t *font, int scale)
{
    if (!font) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    return font->height * scale;
}

int gfx_text_width(const gfx_font_t *font, const char *s, int scale)
{
    if (!font || !s) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    int n = 0;
    for (const char *p = s; *p; ++p) {
        ++n;
    }
    return n * font->width * scale;
}

int gfx_char(gfx_canvas_t *c, int x, int y, char ch, const gfx_font_t *font,
             gfx_color_t fg, int scale)
{
    if (!font) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    const uint8_t *rows = glyph_rows(font, (unsigned char)ch);
    if (!rows) {
        /* '?' is the documented fallback, but the numeric face does not
         * contain one -- and a glyph that silently renders as whitespace makes
         * a fault look like a gap.  Draw the missing-glyph box instead. */
        int gw = font->width * scale;
        int gh = font->height * scale;
        if (gw > 2 && gh > 2) {
            gfx_draw_rect(c, x + 1, y + 1, gw - 2, gh - 2, fg);
        }
        return gw;
    }
    int stride = font->bytes_per_row ? font->bytes_per_row : 1;
    for (int row = 0; row < font->height; ++row) {
        const uint8_t *bits = rows + (size_t)row * stride;
        /* Runs of set bits become one fill_rect instead of N pixel writes --
         * worth it for the large faces, where a stem is 3-4 pixels wide. */
        int col = 0;
        while (col < font->width) {
            if (!(bits[col >> 3] & (0x80u >> (col & 7)))) {
                ++col;
                continue;
            }
            int start = col;
            while (col < font->width &&
                   (bits[col >> 3] & (0x80u >> (col & 7)))) {
                ++col;
            }
            int run = col - start;
            if (scale == 1) {
                gfx_hline(c, x + start, y + row, run, fg);
            } else {
                gfx_fill_rect(c, x + start * scale, y + row * scale,
                              run * scale, scale, fg);
            }
        }
    }
    return font->width * scale;
}

int gfx_text(gfx_canvas_t *c, int x, int y, const char *s,
             const gfx_font_t *font, gfx_color_t fg, int scale)
{
    if (!font || !s) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    int advance = 0;
    for (const char *p = s; *p; ++p) {
        advance += gfx_char(c, x + advance, y, *p, font, fg, scale);
    }
    return advance;
}

int gfx_text_bg(gfx_canvas_t *c, int x, int y, const char *s,
                const gfx_font_t *font, gfx_color_t fg, gfx_color_t bg, int scale)
{
    if (!font || !s) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    int w = gfx_text_width(font, s, scale);
    gfx_fill_rect(c, x, y, w, gfx_text_height(font, scale), bg);
    return gfx_text(c, x, y, s, font, fg, scale);
}

void gfx_text_in(gfx_canvas_t *c, gfx_rect_t box, const char *s,
                 const gfx_font_t *font, gfx_color_t fg, int scale,
                 gfx_align_t align)
{
    /* Every other primitive tolerates a NULL canvas through canvas_ok(); this
     * one reads c->clip below, so it has to say so itself. */
    if (!c || !font || !s) {
        return;
    }
    if (scale < 1) {
        scale = 1;
    }
    int tw = gfx_text_width(font, s, scale);
    int th = gfx_text_height(font, scale);
    int x = box.x;
    switch (align) {
    case GFX_ALIGN_CENTER: x = box.x + (box.w - tw) / 2; break;
    case GFX_ALIGN_RIGHT:  x = box.x + box.w - tw;       break;
    case GFX_ALIGN_LEFT:
    default:                                             break;
    }
    int y = box.y + (box.h - th) / 2;

    gfx_rect_t saved = c->clip;
    if (gfx_clip_intersect(c, box)) {
        gfx_text(c, x, y, s, font, fg, scale);
    }
    c->clip = saved;
}

/* ------------------------------------------------------------- rotated text */

void gfx_text_rotated(gfx_canvas_t *c, int cx, int cy, const char *s,
                      const gfx_font_t *font, gfx_color_t fg, int scale,
                      uint8_t alpha, float angle_deg)
{
    if (!canvas_ok(c) || s == NULL || font == NULL || alpha == 0) {
        return;
    }
    if (scale < 1) {
        scale = 1;
    }

    const int len = (int)strlen(s);
    if (len <= 0) {
        return;
    }
    const int tw = len * font->width * scale;
    const int th = font->height * scale;

    const float rad = angle_deg * 3.14159265358979f / 180.0f;
    const float ca = cosf(rad);
    const float sa = sinf(rad);

    /* The rotated box, so the scan covers the glyphs and little else. */
    const float hw = (fabsf((float)tw * ca) + fabsf((float)th * sa)) * 0.5f;
    const float hh = (fabsf((float)tw * sa) + fabsf((float)th * ca)) * 0.5f;

    int x0 = cx - (int)hw - 1, x1 = cx + (int)hw + 1;
    int y0 = cy - (int)hh - 1, y1 = cy + (int)hh + 1;
    if (x0 < c->clip.x) { x0 = c->clip.x; }
    if (y0 < c->clip.y) { y0 = c->clip.y; }
    if (x1 > c->clip.x + c->clip.w) { x1 = c->clip.x + c->clip.w; }
    if (y1 > c->clip.y + c->clip.h) { y1 = c->clip.y + c->clip.h; }

    for (int dy = y0; dy < y1; ++dy) {
        const float v = (float)(dy - cy);
        for (int dx = x0; dx < x1; ++dx) {
            const float u = (float)(dx - cx);

            /* Rotate the destination point back into the string's own frame. */
            const float fx = u * ca + v * sa + (float)tw * 0.5f;
            const float fy = -u * sa + v * ca + (float)th * 0.5f;
            if (fx < 0.0f || fy < 0.0f) {
                continue;
            }
            const int tx = (int)fx;
            const int ty = (int)fy;
            if (tx >= tw || ty >= th) {
                continue;
            }

            const int idx = tx / (font->width * scale);
            const unsigned char ch = (unsigned char)s[idx];
            if (ch < font->first || ch > font->last) {
                continue;
            }
            const int col = (tx % (font->width * scale)) / scale;
            const int row = ty / scale;

            const uint8_t *glyph = font->glyphs
                + (size_t)(ch - font->first) * font->height * font->bytes_per_row;
            const uint8_t byte = glyph[(size_t)row * font->bytes_per_row
                                       + (size_t)(col >> 3)];
            if ((byte & (uint8_t)(0x80u >> (col & 7))) == 0) {
                continue;
            }

            gfx_pixel(c, dx, dy,
                      gfx_lerp(gfx_pixel_get(c, dx, dy), fg, alpha));
        }
    }
}
