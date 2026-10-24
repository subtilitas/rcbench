/**
 * @file
 * @brief A sprite: an image with alpha, run-length encoded, blended onto a
 *        canvas.
 *
 * The table is what tools/gen_cats.py writes: row by row from the top, each
 * row exactly the sprite's width, as runs.  A run starts with one byte, its
 * top two bits the kind and its low six bits the length less one (1 to 64
 * pixels):
 *
 *   GFX_RUN_SKIP   n transparent pixels; no data
 *   GFX_RUN_FILL   n opaque pixels of one colour; 2 bytes, RGB565
 *                  little-endian
 *   GFX_RUN_COPY   n opaque pixels; 2n bytes
 *   GFX_RUN_BLEND  n partly covered pixels; ceil(n/2) bytes of 4-bit alpha
 *                  (1 to 14 of 15), the first pixel in the low nibble, then
 *                  2n bytes of RGB565 premultiplied by that alpha
 *
 * A partly covered pixel is out = src + dst x (15 - a) / 15 per channel,
 * the destination's weight rounded down to 32nds so no channel carries
 * into the next.  Opaque runs are stored and copied; a transparent run
 * reads nothing.  Drawing the same sprite twice on the same pixels gives
 * a darker edge than once: a screen draws it once onto a background it
 * has just filled.
 *
 * Clipping is to canvas->clip, as for every primitive (gfx.h).  The area
 * written is reported to the damage record once (gfx_damage.h) and to the
 * fit check as a shape (GFX_TEXT_TRACE), so a string under a sprite shows
 * there as painted over.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The kinds of a run, in the top two bits of its first byte. */
enum {
    GFX_RUN_SKIP  = 0, /**< transparent pixels, no data */
    GFX_RUN_FILL  = 1, /**< one opaque colour, 2 bytes */
    GFX_RUN_COPY  = 2, /**< opaque pixels, 2 bytes each */
    GFX_RUN_BLEND = 3, /**< partly covered pixels, alpha nibbles then
                            premultiplied colours */
};

/** @brief The longest run, 64 pixels. */
#define GFX_RUN_MAX 64

/** @brief An encoded image with alpha. */
typedef struct {
    int16_t        w;     /**< width, pixels                               */
    int16_t        h;     /**< height, rows                                */
    uint32_t       bytes; /**< the table's length                          */
    const uint8_t *rle;   /**< the table                                   */
} gfx_sprite_t;

/**
 * @brief Blend @p sp onto @p c with its top-left corner at (x, y).
 *
 * Stops at the end of the table, or at a run that would cross the end of
 * its row, so a short or damaged table draws less and reads nothing past
 * its @c bytes.
 *
 * @param c the canvas
 * @param x the sprite's left edge on the canvas
 * @param y its top edge
 * @param sp the sprite, or NULL for none
 * @return true when the whole table was read, row by row, to its last
 *         byte; false for NULL, an empty sprite or a table that does not
 *         hold w x h pixels in exactly @c bytes
 */
bool gfx_sprite_draw(gfx_canvas_t *c, int x, int y, const gfx_sprite_t *sp);

/**
 * @brief The sprite's box with its top-left corner at (x, y).
 * @param sp the sprite
 * @param x its left edge
 * @param y its top edge
 * @return the box, empty for NULL
 */
static inline gfx_rect_t gfx_sprite_rect(const gfx_sprite_t *sp, int x, int y)
{
    return (sp != NULL) ? gfx_rect_make(x, y, sp->w, sp->h)
                        : gfx_rect_make(x, y, 0, 0);
}

#ifdef __cplusplus
}
#endif
