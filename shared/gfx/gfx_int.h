/*
 * gfx internals shared by gfx.c and gfx_text.c.  Not part of the public
 * interface (include/gfx.h); nothing outside shared/gfx includes it.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GFX_INT_H
#define GFX_INT_H

#include "gfx.h"

/* A canvas that can be drawn on: it exists, has pixels and a non-empty clip. */
static inline bool canvas_ok(const gfx_canvas_t *c)
{
    return c && c->pixels && !gfx_rect_empty(c->clip);
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
