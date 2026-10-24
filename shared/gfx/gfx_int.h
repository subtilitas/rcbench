/**
 * @file
 * @brief gfx internals shared by gfx.c and gfx_text.c.  Not part of the
 *        public interface (include/gfx.h); nothing outside shared/gfx
 *        includes it.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GFX_INT_H
#define GFX_INT_H

#include "gfx.h"
#include "gfx_damage.h"

#include <limits.h>
#include <stddef.h>

/**
 * @brief Report a clipped rectangle written through @p c to the attached
 *        damage record (gfx_damage.h); one load and one branch without one.
 */
#define GFX_DAMAGE(c, x, y, w, h)                                           \
    do {                                                                    \
        if (gfx_damage_sink != NULL) {                                      \
            gfx_damage_mark(gfx_damage_sink, (c), (x), (y), (w), (h));      \
        }                                                                   \
    } while (0)

/**
 * @brief GFX_DAMAGE() for a rectangle not yet clipped: intersected with
 *        @p c's clip first, and only while a record is attached.
 */
#define GFX_DAMAGE_CLIP(cv, rx, ry, rw, rh)                                 \
    do {                                                                    \
        if (gfx_damage_sink != NULL) {                                      \
            gfx_rect_t dmg_;                                                \
            if (gfx_rect_intersect(gfx_rect_make((rx), (ry), (rw), (rh)),   \
                                   (cv)->clip, &dmg_)) {                    \
                gfx_damage_mark(gfx_damage_sink, (cv), dmg_.x, dmg_.y,      \
                                dmg_.w, dmg_.h);                            \
            }                                                               \
        }                                                                   \
    } while (0)

/**
 * @brief Whether @p c can be drawn on: it exists, has pixels and a
 *        non-empty clip.
 * @param c the canvas, or NULL
 * @return true when it can
 */
static inline bool canvas_ok(const gfx_canvas_t *c)
{
    return c && c->pixels && !gfx_rect_empty(c->clip);
}

/*
 * The bodies of gfx_pixel(), gfx_pixel_get(), gfx_hline() and
 * gfx_clip_intersect().  The public functions in gfx.c are one-line wrappers
 * around these, and gfx_text.c calls these directly, so that the glyph loop
 * has them inline: across files the compiler cannot inline the public
 * functions.
 *
 * gfx.c defines GFX_INL_FORCE before including this header.  The wrappers
 * then compile to the body itself, and the callers in gfx.c inline or call
 * them as they did when the bodies were the public functions.  gfx_text.c
 * leaves the macro undefined; the plain static inline gives it the smaller
 * glyph loop.
 */
#ifdef GFX_INL_FORCE
/** @brief How the primitives' bodies are declared: forced inline in gfx.c. */
#define GFX_INL static inline __attribute__((always_inline))
#else
/** @brief How the primitives' bodies are declared: plain inline. */
#define GFX_INL static inline
#endif
/**
 * @brief The body of gfx_pixel(): one pixel inside the clip, reported to
 *        the damage record.
 * @param c the canvas
 * @param x the pixel's column
 * @param y its row
 * @param color its colour
 */
GFX_INL void gfx_pixel_inl(gfx_canvas_t *c, int x, int y,
                                 gfx_color_t color)
{
    if (!canvas_ok(c) || !gfx_rect_contains(c->clip, x, y)) {
        return;
    }
    GFX_DAMAGE(c, x, y, 1, 1);
    c->pixels[(ptrdiff_t)y * c->stride + x] = color;
}

/**
 * @brief One pixel inside the clip, not reported to the damage record: for
 *        a caller that reported the area it writes in already.
 * @param c the canvas
 * @param x the pixel's column
 * @param y its row
 * @param color its colour
 */
GFX_INL void gfx_pixel_put_inl(gfx_canvas_t *c, int x, int y,
                               gfx_color_t color)
{
    if (!canvas_ok(c) || !gfx_rect_contains(c->clip, x, y)) {
        return;
    }
    c->pixels[(ptrdiff_t)y * c->stride + x] = color;
}

/**
 * @brief The body of gfx_pixel_get().
 * @param c the canvas
 * @param x the pixel's column
 * @param y its row
 * @return its colour, 0 outside the canvas
 */
GFX_INL gfx_color_t gfx_pixel_get_inl(const gfx_canvas_t *c, int x, int y)
{
    if (!c || !c->pixels || x < 0 || y < 0 || x >= c->width || y >= c->height) {
        return 0;
    }
    return c->pixels[(ptrdiff_t)y * c->stride + x];
}

/**
 * @brief The body of gfx_hline(): a horizontal line through
 *        gfx_fill_rect(), drawn leftwards for a negative @p w.
 * @param c the canvas
 * @param x the line's first column
 * @param y its row
 * @param w its length, pixels
 * @param color its colour
 */
GFX_INL void gfx_hline_inl(gfx_canvas_t *c, int x, int y, int w,
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

/**
 * @brief The body of gfx_clip_intersect().
 * @param c the canvas
 * @param r the rectangle the clip narrows to
 * @return false when the clip is now empty
 */
GFX_INL bool gfx_clip_intersect_inl(gfx_canvas_t *c, gfx_rect_t r)
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
/** @brief The fit check's hold depth: 0 reports, more holds back. */
extern int s_trace_hold;
/** @brief Report a shape's box to the fit check unless held back. */
#define TRACE_BOX(c, x, y, w, h)                                            \
    do {                                                                    \
        if (s_trace_hold == 0) {                                            \
            gfx_trace_box((c), (x), (y), (w), (h));                         \
        }                                                                   \
    } while (0)
/** @brief Hold back the boxes of the spans that fill one shape. */
#define TRACE_HOLD()    (++s_trace_hold)
/** @brief End one TRACE_HOLD(). */
#define TRACE_RELEASE() (--s_trace_hold)
#else
/** @brief Report a shape's box: nothing without GFX_TEXT_TRACE. */
#define TRACE_BOX(c, x, y, w, h) ((void)0)
/** @brief Hold back boxes: nothing without GFX_TEXT_TRACE. */
#define TRACE_HOLD()    ((void)0)
/** @brief End a hold: nothing without GFX_TEXT_TRACE. */
#define TRACE_RELEASE() ((void)0)
#endif

#endif /* GFX_INT_H */
