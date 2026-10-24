/**
 * @file
 * @brief gfx_sprite_draw(): a run-length encoded image with 4-bit alpha,
 *        blended onto a canvas.  See include/gfx_sprite.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gfx_sprite.h"

#include "gfx_int.h"
#include "gfx_work.h"

#include <string.h>

/** @brief The destination's weight under alpha @p a, in 32nds, rounded
 *         down: floor(32 x (15 - a) / 15). */
#define KEEP(a) ((uint8_t)((32u * (15u - (a))) / 15u))

/** @brief KEEP() for every alpha, 0 to 15. */
static const uint8_t k_keep[16] = {
    KEEP(0),  KEEP(1),  KEEP(2),  KEEP(3),  KEEP(4),  KEEP(5),
    KEEP(6),  KEEP(7),  KEEP(8),  KEEP(9),  KEEP(10), KEEP(11),
    KEEP(12), KEEP(13), KEEP(14), KEEP(15),
};

/**
 * @brief One partly covered pixel: @p src, premultiplied, over @p dst.
 *
 * The three channels are spread over a 32-bit word with room above each
 * (green in bits 21 to 26, red 11 to 15, blue 0 to 4), scaled by one
 * multiply and folded back.  The weight is rounded down, so src plus the
 * scaled dst stays within each channel and nothing carries.
 *
 * @param dst the pixel under it
 * @param src the sprite's pixel, premultiplied by its alpha
 * @param a its alpha, 1 to 14 of 15
 * @return the blended pixel
 */
static inline gfx_color_t blend(gfx_color_t dst, gfx_color_t src, unsigned a)
{
    uint32_t d = ((uint32_t)dst | ((uint32_t)dst << 16)) & 0x07E0F81Fu;
    d = ((d * k_keep[a & 15u]) >> 5) & 0x07E0F81Fu;
    return (gfx_color_t)(src + (gfx_color_t)(d | (d >> 16)));
}

/** @brief A little-endian 16-bit value at @p p. */
static inline gfx_color_t rd16(const uint8_t *p)
{
    return (gfx_color_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}

/* cppcheck-suppress constParameterPointer
 * The canvas is written through its pixel pointer, which a pointer to a
 * const canvas still allows; non-const as every primitive in gfx.h takes
 * it, since drawing changes the canvas. */
bool gfx_sprite_draw(gfx_canvas_t *c, int x, int y, const gfx_sprite_t *sp)
{
    if (sp == NULL || sp->rle == NULL || sp->w <= 0 || sp->h <= 0) {
        return false;
    }
    const bool draw = canvas_ok(c);
    gfx_rect_t vis = { 0, 0, 0, 0 };
    const bool any = draw
        && gfx_rect_intersect(gfx_rect_make(x, y, sp->w, sp->h), c->clip,
                              &vis);
    if (draw) {
        TRACE_BOX(c, x, y, sp->w, sp->h);
    }
    if (any) {
        GFX_DAMAGE(c, vis.x, vis.y, vis.w, vis.h);
    }
    const int cx0 = vis.x, cx1 = vis.x + vis.w;
    const uint8_t *p = sp->rle;
    const uint8_t *const end = sp->rle + sp->bytes;

    for (int row = 0; row < sp->h; ++row) {
        const int dy = y + row;
        const bool on = any && dy >= vis.y && dy < vis.y + vis.h;
        gfx_color_t *line = on ? c->pixels + (ptrdiff_t)dy * c->stride
                               : NULL;
        int col = 0;
        while (col < sp->w) {
            if (p >= end) {
                return false;
            }
            const unsigned kind = (unsigned)(*p >> 6);
            const int n = (int)(*p & 0x3Fu) + 1;
            ++p;
            if (col + n > sp->w) {
                return false;       /* a run across the end of its row */
            }
            const size_t data = (kind == GFX_RUN_SKIP)   ? 0u
                                : (kind == GFX_RUN_FILL) ? 2u
                                : (kind == GFX_RUN_COPY) ? (size_t)n * 2u
                                : (size_t)((n + 1) / 2) + (size_t)n * 2u;
            if ((size_t)(end - p) < data) {
                return false;
            }
            /* The part of the run inside the clip, as sprite columns. */
            const int px = x + col;
            const int a0 = (px > cx0) ? 0 : cx0 - px;
            const int a1 = (px + n < cx1) ? n : cx1 - px;
            if (kind != GFX_RUN_SKIP && line != NULL && a0 < a1) {
                gfx_color_t *d = line + px;
                GFX_VISIT((unsigned long)(a1 - a0));
                if (kind == GFX_RUN_FILL) {
                    const gfx_color_t v = rd16(p);
                    for (int k = a0; k < a1; ++k) { d[k] = v; }
                } else if (kind == GFX_RUN_COPY) {
                    for (int k = a0; k < a1; ++k) {
                        d[k] = rd16(p + (ptrdiff_t)2 * k);
                    }
                } else if (kind == GFX_RUN_BLEND) {
                    const uint8_t *col565 = p + (n + 1) / 2;
                    for (int k = a0; k < a1; ++k) {
                        const unsigned a = (p[k >> 1] >> ((k & 1) * 4)) & 15u;
                        d[k] = blend(d[k], rd16(col565 + (ptrdiff_t)2 * k), a);
                    }
                }
            }
            p += data;
            col += n;
        }
    }
    return p == end;
}
