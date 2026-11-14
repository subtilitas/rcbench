/**
 * @file
 * @brief The antialiased 1px ring from a coverage table.  See
 *        include/gfx_ring.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gfx_ring.h"

#include <math.h>
#include <stdlib.h>

#include "gfx_int.h"

/** @brief The smaller and the larger of two values. @{ */
#define RING_MIN(a, b) ((a) < (b) ? (a) : (b))
#define RING_MAX(a, b) ((a) > (b) ? (a) : (b)) /**< the larger */
/** @} */
/** @brief Pi, for the angle; math.h's M_PI is not ISO C. */
#define RING_PI 3.14159265358979323846

uint32_t gfx_ring_angle(const gfx_ring_t *ring, int x, int y)
{
    const double dx = (double)x - (double)ring->cx2 / 2.0;
    const double dy = (double)y - (double)ring->cy2 / 2.0;
    /* From 12 o'clock clockwise: x right, y down, so up is -y. */
    double a = atan2(dx, -dy);
    if (a < 0.0) {
        a += 2.0 * RING_PI;
    }
    const double q = a * (double)GFX_RING_TURN / (2.0 * RING_PI);
    if (q >= (double)(GFX_RING_TURN - 1u)) {
        return GFX_RING_TURN - 1u;
    }
    return (uint32_t)q;
}

/**
 * @brief qsort's order of two sort keys: angle, then scan position.
 * @param a the first key
 * @param b the second
 * @return below, at or above 0 as a sorts before, with or after b
 */
static int key_cmp(const void *a, const void *b)
{
    const uint32_t ka = *(const uint32_t *)a;
    const uint32_t kb = *(const uint32_t *)b;
    return (ka > kb) - (ka < kb);
}

int gfx_ring_build(gfx_ring_t *ring, int cx2, int cy2, int r,
                   gfx_ring_px_t *px, uint32_t *keys, int cap)
{
    int n = 0;
    if (ring == NULL || px == NULL || keys == NULL || r < 1
        || r > GFX_RING_R_MAX || cap < 1 || cap > 0xFFFF) {
        return 0;
    }
    ring->px = px;
    ring->n = 0;
    ring->cx2 = gfx_i16(cx2);
    ring->cy2 = gfx_i16(cy2);
    ring->r = (int16_t)r;
    const double cx = (double)cx2 / 2.0;
    const double cy = (double)cy2 / 2.0;
    const int x0 = (int)floor(cx - (double)r - 1.0);
    const int x1 = (int)ceil(cx + (double)r + 1.0);
    const int y0 = (int)floor(cy - (double)r - 1.0);
    const int y1 = (int)ceil(cy + (double)r + 1.0);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double d = hypot((double)x - cx, (double)y - cy);
            const double cov = 1.0 - fabs(d - (double)r);
            if (cov <= 0.0) {
                continue;
            }
            const long c8 = lround(cov * 255.0);
            if (c8 <= 0) {
                continue;
            }
            if (x < 0 || x > 255 || y < 0 || y > 255 || n >= cap) {
                return 0;
            }
            px[n].x = (uint8_t)x;
            px[n].y = (uint8_t)y;
            px[n].cov = (uint8_t)(c8 > 255 ? 255 : c8);
            keys[n] = (gfx_ring_angle(ring, x, y) << 16) | (uint32_t)n;
            ++n;
        }
    }
    qsort(keys, (size_t)n, sizeof keys[0], key_cmp);
    /* Put px into the keys' order in place: position k takes the pixel
     * that was at the key's index, following each cycle once.  A key is
     * spent (UINT32_MAX) once its position holds its pixel. */
    for (int j = 0; j < n; ++j) {
        if (keys[j] == UINT32_MAX) {
            continue;
        }
        const gfx_ring_px_t first = px[j];
        int k = j;
        for (;;) {
            const int src = (int)(keys[k] & 0xFFFFu);
            keys[k] = UINT32_MAX;
            if (src == j) {
                px[k] = first;
                break;
            }
            px[k] = px[src];
            k = src;
        }
    }
    ring->n = (uint16_t)n;
    return n;
}

uint32_t gfx_ring_find(const gfx_ring_t *ring, uint32_t ang)
{
    uint32_t lo = 0;
    uint32_t hi;
    if (ring == NULL || ring->px == NULL) {
        return 0;
    }
    hi = ring->n;
    if (ang >= GFX_RING_TURN) {
        return hi;
    }
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2u;
        const gfx_ring_px_t *p = &ring->px[mid];
        if (gfx_ring_angle(ring, p->x, p->y) < ang) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo;
}

uint32_t gfx_ring_end(uint64_t num, uint64_t den)
{
    if (den == 0u) {
        return 0;
    }
    if (num >= den) {
        return GFX_RING_TURN;
    }
    return (uint32_t)((num * GFX_RING_TURN + den / 2u) / den);
}

int gfx_ring_aa(gfx_canvas_t *c, const gfx_ring_t *ring, uint32_t from,
                uint32_t to, uint32_t end, gfx_color_t on, gfx_color_t track,
                gfx_color_t bg)
{
    int painted = 0;
    int bx0 = INT_MAX;
    int by0 = INT_MAX;
    int bx1 = INT_MIN;
    int by1 = INT_MIN;
    if (!canvas_ok(c) || ring == NULL || ring->px == NULL || to <= from) {
        return 0;
    }
    const uint32_t i0 = gfx_ring_find(ring, from);
    const uint32_t i1 = gfx_ring_find(ring, to);
    const uint32_t ie = gfx_ring_find(ring, end);
    for (uint32_t i = i0; i < i1; ++i) {
        const gfx_ring_px_t *p = &ring->px[i];
        const int x = p->x;
        const int y = p->y;
        if (!gfx_rect_contains(c->clip, x, y)) {
            continue;
        }
        const gfx_color_t col = i < ie ? on : track;
        c->pixels[(ptrdiff_t)y * c->stride + x] =
            p->cov == 255u ? col : gfx_lerp(bg, col, p->cov);
        bx0 = RING_MIN(bx0, x);
        by0 = RING_MIN(by0, y);
        bx1 = RING_MAX(bx1, x);
        by1 = RING_MAX(by1, y);
        ++painted;
    }
    if (painted > 0) {
        GFX_DAMAGE(c, bx0, by0, bx1 - bx0 + 1, by1 - by0 + 1);
    }
    return painted;
}

int gfx_ring_move(gfx_canvas_t *c, const gfx_ring_t *ring, uint32_t was,
                  uint32_t now, gfx_color_t on, gfx_color_t track,
                  gfx_color_t bg)
{
    const uint32_t lo = was < now ? was : now;
    const uint32_t hi = was < now ? now : was;
    return gfx_ring_aa(c, ring, lo, hi, now, on, track, bg);
}
