/*
 * A canvas inside a larger buffer, for a host suite that renders a screen:
 * GUARD_PX rows above and below the canvas and GUARD_PX columns between two
 * rows hold a sentinel, so a pixel written off the canvas is found by the
 * plain build and not only under AddressSanitizer.
 *
 * The sentinel is a colour like any other.  A pixel drawn off the canvas
 * in exactly that colour is not seen; no theme colour has that value.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

#define GUARD_PX       8
#define GUARD_SENTINEL ((gfx_color_t)0x5AA5u)

/* The pixels a canvas of @p w by @p h needs with its guards. */
#define GUARD_CANVAS_PIXELS(w, h) \
    ((size_t)((w) + GUARD_PX) * (size_t)((h) + 2 * GUARD_PX))

typedef struct {
    gfx_color_t *buf;       /* GUARD_CANVAS_PIXELS(w, h) of them */
    int          w, h;
} guard_canvas_t;

/* Fills @p buf with the sentinel and binds @p c to the canvas inside it. */
static inline void guard_canvas_init(guard_canvas_t *g, gfx_canvas_t *c,
                                     gfx_color_t *buf, int w, int h)
{
    g->buf = buf;
    g->w   = w;
    g->h   = h;
    const size_t n = GUARD_CANVAS_PIXELS(w, h);
    for (size_t i = 0; i < n; ++i) {
        buf[i] = GUARD_SENTINEL;
    }
    gfx_canvas_init(c, buf + (size_t)GUARD_PX * (size_t)(w + GUARD_PX),
                    w, h, w + GUARD_PX);
}

/* How many guard pixels hold another value than the sentinel. */
static inline unsigned guard_canvas_touched(const guard_canvas_t *g)
{
    const int stride = g->w + GUARD_PX;
    const int rows   = g->h + 2 * GUARD_PX;
    unsigned n = 0u;
    for (int y = 0; y < rows; ++y) {
        const bool in_rows = y >= GUARD_PX && y < GUARD_PX + g->h;
        for (int x = 0; x < stride; ++x) {
            if (in_rows && x < g->w) {
                continue;
            }
            if (g->buf[(size_t)y * (size_t)stride + (size_t)x]
                != GUARD_SENTINEL) {
                ++n;
            }
        }
    }
    return n;
}

/* How many of the canvas's own pixels still hold the sentinel: 0 once a
 * render has painted all of it. */
static inline unsigned guard_canvas_unpainted(const guard_canvas_t *g)
{
    const int stride = g->w + GUARD_PX;
    unsigned n = 0u;
    for (int y = 0; y < g->h; ++y) {
        for (int x = 0; x < g->w; ++x) {
            if (g->buf[(size_t)(y + GUARD_PX) * (size_t)stride + (size_t)x]
                == GUARD_SENTINEL) {
                ++n;
            }
        }
    }
    return n;
}

/* A 32-bit FNV-1a sum of the canvas's own pixels: two frames with the same
 * sum are taken as the same picture. */
static inline uint32_t guard_canvas_sum(const guard_canvas_t *g)
{
    const int stride = g->w + GUARD_PX;
    uint32_t sum = 2166136261u;
    for (int y = 0; y < g->h; ++y) {
        for (int x = 0; x < g->w; ++x) {
            const gfx_color_t px =
                g->buf[(size_t)(y + GUARD_PX) * (size_t)stride + (size_t)x];
            sum = (sum ^ (px & 0xFFu)) * 16777619u;
            sum = (sum ^ (uint32_t)(px >> 8)) * 16777619u;
        }
    }
    return sum;
}
