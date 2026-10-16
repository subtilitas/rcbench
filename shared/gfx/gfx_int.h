/*
 * gfx internals shared by gfx.c and gfx_text.c.  Not part of the public
 * interface (include/gfx.h); nothing outside shared/gfx includes it.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GFX_INT_H
#define GFX_INT_H

#include "gfx.h"

#include <limits.h>
#include <stddef.h>

/* A canvas that can be drawn on: it exists, has pixels and a non-empty clip. */
static inline bool canvas_ok(const gfx_canvas_t *c)
{
    return c && c->pixels && !gfx_rect_empty(c->clip);
}

/*
 * Copies of the bodies of gfx_pixel(), gfx_pixel_get(), gfx_hline() and
 * gfx_clip_intersect() in gfx.c, for gfx_text.c.  Beside them in gfx.c the
 * compiler inlined them into the glyph loop; across files it cannot, so the
 * text code takes these.  Each must match its public twin line for line.
 */
static inline void gfx_pixel_inl(gfx_canvas_t *c, int x, int y,
                                 gfx_color_t color)
{
    if (!canvas_ok(c) || !gfx_rect_contains(c->clip, x, y)) {
        return;
    }
    c->pixels[(ptrdiff_t)y * c->stride + x] = color;
}

static inline gfx_color_t gfx_pixel_get_inl(const gfx_canvas_t *c, int x, int y)
{
    if (!c || !c->pixels || x < 0 || y < 0 || x >= c->width || y >= c->height) {
        return 0;
    }
    return c->pixels[(ptrdiff_t)y * c->stride + x];
}

static inline void gfx_hline_inl(gfx_canvas_t *c, int x, int y, int w,
                                 gfx_color_t color)
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

static inline bool gfx_clip_intersect_inl(gfx_canvas_t *c, gfx_rect_t r)
{
    if (!c) {
        return false;
    }
    return gfx_rect_intersect(r, c->clip, &c->clip);
}

/*
 * The fit check's hooks (gfx.h), compiled in only with GFX_TEXT_TRACE.  A
 * shape is reported once, as the caller drew it: the spans a rounded
 * rectangle or a glyph is filled with are held back.  s_trace_hold is
 * defined in gfx.c.
 */
#ifdef GFX_TEXT_TRACE
extern int s_trace_hold;
#define TRACE_BOX(c, x, y, w, h)                                            \
    do {                                                                    \
        if (s_trace_hold == 0) {                                            \
            gfx_trace_box((c), (x), (y), (w), (h));                         \
        }                                                                   \
    } while (0)
#define TRACE_HOLD()    (++s_trace_hold)
#define TRACE_RELEASE() (--s_trace_hold)
#else
#define TRACE_BOX(c, x, y, w, h) ((void)0)
#define TRACE_HOLD()    ((void)0)
#define TRACE_RELEASE() ((void)0)
#endif

#endif /* GFX_INT_H */
