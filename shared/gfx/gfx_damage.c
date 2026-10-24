/**
 * @file
 * @brief Damage: the part of one frame buffer the rasteriser wrote, as
 *        16-row bands.  See include/gfx_damage.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gfx_damage.h"

#include <stddef.h>

gfx_damage_t *gfx_damage_sink;

void gfx_damage_init(gfx_damage_t *d, const gfx_canvas_t *root)
{
    if (d == NULL || root == NULL) {
        return;
    }
    d->base = (uintptr_t)root->pixels;
    d->stride = root->stride;
    d->width = root->width;
    d->height = root->height;
    d->end = d->base + (uintptr_t)((size_t)root->height * (size_t)root->stride
                                   * sizeof(gfx_color_t));
    const int bands = (root->height + GFX_DAMAGE_BAND_H - 1)
                      >> GFX_DAMAGE_BAND_SHIFT;
    d->bands = (int16_t)((bands > GFX_DAMAGE_MAX_BANDS) ? GFX_DAMAGE_MAX_BANDS
                                                        : bands);
    gfx_damage_reset(d);
}

void gfx_damage_reset(gfx_damage_t *d)
{
    if (d == NULL) {
        return;
    }
    /* Rows the bands cannot hold: every write counts the whole buffer. */
    d->all = (d->height > GFX_DAMAGE_MAX_BANDS * GFX_DAMAGE_BAND_H);
    for (int b = 0; b < GFX_DAMAGE_MAX_BANDS; ++b) {
        d->lo[b] = d->width;
        d->hi[b] = 0;
    }
    d->last = 0u;
    d->last_x = 0;
    d->last_y = 0;
}

void gfx_damage_all(gfx_damage_t *d)
{
    if (d != NULL) {
        d->all = true;
    }
}

/**
 * @brief Where @p c's top-left pixel lies in @p d's buffer.
 * @param d the record
 * @param c the canvas
 * @param x its left edge in the buffer, set when it lies inside
 * @param y its top edge, set when it lies inside
 * @return true when @p c's pixels lie inside the buffer with its stride
 */
static bool place(gfx_damage_t *d, const gfx_canvas_t *c, int *x, int *y)
{
    const uintptr_t p = (uintptr_t)c->pixels;
    if (p == d->last && p != 0u && c->stride == d->stride) {
        *x = d->last_x;
        *y = d->last_y;
        return true;
    }
    if (p < d->base || p >= d->end) {
        return false;
    }
    if (c->stride != d->stride || d->stride <= 0) {
        d->all = true;
        return false;
    }
    const uintptr_t px = (p - d->base) / sizeof(gfx_color_t);
    *y = (int)(px / (uintptr_t)d->stride);
    *x = (int)(px % (uintptr_t)d->stride);
    d->last = p;
    d->last_x = (int16_t)*x;
    d->last_y = (int16_t)*y;
    return true;
}

void gfx_damage_mark(gfx_damage_t *d, const gfx_canvas_t *c, int x, int y,
                     int w, int h)
{
    if (d == NULL || c == NULL || d->all || w <= 0 || h <= 0) {
        return;
    }
    int ox = 0, oy = 0;
    if (!place(d, c, &ox, &oy)) {
        return;
    }
    long x0 = (long)x + ox, y0 = (long)y + oy;
    long x1 = x0 + w, y1 = y0 + h;
    x0 = (x0 < 0) ? 0 : x0;
    y0 = (y0 < 0) ? 0 : y0;
    x1 = (x1 > d->width) ? d->width : x1;
    y1 = (y1 > d->height) ? d->height : y1;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    const int b0 = (int)(y0 >> GFX_DAMAGE_BAND_SHIFT);
    const int b1 = (int)((y1 - 1) >> GFX_DAMAGE_BAND_SHIFT);
    for (int b = b0; b <= b1; ++b) {
        if (x0 < d->lo[b]) {
            d->lo[b] = (int16_t)x0;
        }
        if (x1 > d->hi[b]) {
            d->hi[b] = (int16_t)x1;
        }
    }
}

bool gfx_damage_empty(const gfx_damage_t *d)
{
    if (d == NULL) {
        return true;
    }
    if (d->all) {
        return false;
    }
    for (int b = 0; b < d->bands; ++b) {
        if (d->lo[b] < d->hi[b]) {
            return false;
        }
    }
    return true;
}

/** @brief A rectangle's area, pixels. */
static long area(gfx_rect_t r)
{
    return (long)r.w * (long)r.h;
}

/** @brief The smallest rectangle round @p a and @p b. */
static gfx_rect_t bound(gfx_rect_t a, gfx_rect_t b)
{
    const int x0 = (a.x < b.x) ? a.x : b.x;
    const int y0 = (a.y < b.y) ? a.y : b.y;
    const int x1 = (a.x + a.w > b.x + b.w) ? a.x + a.w : b.x + b.w;
    const int y1 = (a.y + a.h > b.y + b.h) ? a.y + a.h : b.y + b.h;
    return gfx_rect_make(x0, y0, x1 - x0, y1 - y0);
}

int gfx_damage_rects(const gfx_damage_t *d, gfx_rect_t *out, int max)
{
    if (d == NULL || out == NULL || max < 1) {
        return 0;
    }
    if (d->all) {
        out[0] = gfx_rect_make(0, 0, d->width, d->height);
        return 1;
    }
    gfx_rect_t r[GFX_DAMAGE_MAX_BANDS];
    int n = 0;
    for (int b = 0; b < d->bands; ++b) {
        if (d->lo[b] < d->hi[b]) {
            const int y = b * GFX_DAMAGE_BAND_H;
            const int rows = (d->height - y < GFX_DAMAGE_BAND_H)
                                 ? d->height - y : GFX_DAMAGE_BAND_H;
            r[n++] = gfx_rect_make(d->lo[b], y, d->hi[b] - d->lo[b], rows);
        }
    }
    /* Neighbours merge, the cheapest first: free merges always, the others
     * while too many are left. */
    while (n > 1) {
        int best = 0;
        long best_waste = -1;
        for (int i = 0; i + 1 < n; ++i) {
            const long waste = area(bound(r[i], r[i + 1])) - area(r[i])
                               - area(r[i + 1]);
            if (best_waste < 0 || waste < best_waste) {
                best_waste = waste;
                best = i;
            }
        }
        if (best_waste > 0 && n <= max) {
            break;
        }
        r[best] = bound(r[best], r[best + 1]);
        for (int i = best + 1; i + 1 < n; ++i) {
            r[i] = r[i + 1];
        }
        --n;
    }
    for (int i = 0; i < n; ++i) {
        out[i] = r[i];
    }
    return n;
}

gfx_rect_t gfx_damage_turn(gfx_rect_t r, int width, int height)
{
    return gfx_rect_make(width - r.x - r.w, height - r.y - r.h, r.w, r.h);
}

void gfx_damage_attach(gfx_damage_t *d)
{
    gfx_damage_sink = d;
}
