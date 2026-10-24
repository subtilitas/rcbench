/**
 * @file
 * @brief Damage: the part of one frame buffer the rasteriser wrote since
 *        the last reset, kept as 16-row bands with one x-span each.
 *
 * The four places that write pixels (gfx_fill_rect(), gfx_pixel() and the
 * glyph loop through gfx_pixel_inl(), gfx_blit() and gfx_blit_key()) report
 * the clipped rectangle they wrote to the attached record
 * (gfx_damage_attach()).  A write through a canvas whose pixels lie outside
 * the record's buffer, an off-screen band for instance, is not counted.
 * With no record attached a write costs one load and one branch.
 *
 * Screen rotation 180° reads the record to copy only what changed from the
 * picture the UI draws to the frame buffer the panel scans, turned
 * (gfx_turn.h).  The record holds at most GFX_DAMAGE_MAX_BANDS bands,
 * 1,024 rows; a taller buffer counts as wholly written on every write.
 *
 * One drawing task: the attached record is one global pointer, written and
 * read by the task that renders.
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

/** @brief A band is 1 << GFX_DAMAGE_BAND_SHIFT rows, 16. */
#define GFX_DAMAGE_BAND_SHIFT 4
/** @brief The rows of one band, 16. */
#define GFX_DAMAGE_BAND_H     (1 << GFX_DAMAGE_BAND_SHIFT)
/** @brief The bands a record holds, 64: 1,024 rows. */
#define GFX_DAMAGE_MAX_BANDS  64
/** @brief The most rectangles gfx_damage_rects() returns, 8. */
#define GFX_DAMAGE_MAX_RECTS  8

/** @brief What one buffer has had written since gfx_damage_reset(). */
typedef struct {
    uintptr_t base;    /**< the buffer's first pixel, as an address        */
    uintptr_t end;     /**< one past its last row, as an address           */
    int32_t stride;    /**< pixels from one row to the next                */
    int16_t width;     /**< the buffer's width, pixels                     */
    int16_t height;    /**< its height, rows                               */
    int16_t bands;     /**< bands in use: height / 16, rounded up          */
    bool all;          /**< the whole buffer counts as written             */
    int16_t lo[GFX_DAMAGE_MAX_BANDS]; /**< a band's first written x, or
                                           width when nothing is          */
    int16_t hi[GFX_DAMAGE_MAX_BANDS]; /**< one past its last written x    */
    uintptr_t last;    /**< the pixels of the canvas last translated       */
    int16_t last_x;    /**< that canvas's left edge in the buffer          */
    int16_t last_y;    /**< its top edge in the buffer                     */
} gfx_damage_t;

/** @brief The record the primitives report to, or NULL; gfx_damage.c. */
extern gfx_damage_t *gfx_damage_sink;

/**
 * @brief Bind @p d to the buffer under @p root, empty.
 * @param d the record
 * @param root a canvas whose pixels are the buffer's first pixel and whose
 *        width, height and stride are the buffer's
 */
void gfx_damage_init(gfx_damage_t *d, const gfx_canvas_t *root);

/**
 * @brief Forget what @p d holds: nothing written.
 * @param d the record
 */
void gfx_damage_reset(gfx_damage_t *d);

/**
 * @brief Count the whole buffer as written.
 * @param d the record
 */
void gfx_damage_all(gfx_damage_t *d);

/**
 * @brief Count a rectangle written through @p c as written.
 *
 * Nothing when @p c's pixels lie outside @p d's buffer.  A canvas inside it
 * with another stride counts the whole buffer.
 *
 * @param d the record
 * @param c the canvas written through
 * @param x the rectangle's left edge, in @p c's coordinates
 * @param y its top edge
 * @param w its width, already clipped to @p c
 * @param h its height, already clipped
 */
void gfx_damage_mark(gfx_damage_t *d, const gfx_canvas_t *c, int x, int y,
                     int w, int h);

/**
 * @brief Whether @p d holds no write.
 * @param d the record
 * @return true when nothing was written since the last reset
 */
bool gfx_damage_empty(const gfx_damage_t *d);

/**
 * @brief The written area as at most @p max rectangles, top to bottom.
 *
 * Each band's span is one rectangle; neighbouring bands then merge, the
 * pair that adds the fewest unwritten pixels first, while a merge adds none
 * or more than @p max are left.  Every written pixel lies in one of the
 * rectangles; a rectangle may hold unwritten pixels.
 *
 * @param d the record
 * @param out the rectangles, in buffer coordinates
 * @param max room in @p out, 1 or more
 * @return the rectangles written to @p out, 0 when nothing was written
 */
int gfx_damage_rects(const gfx_damage_t *d, gfx_rect_t *out, int max);

/**
 * @brief Where @p r lies in a @p width x @p height buffer turned by 180°.
 * @param r a rectangle inside the buffer
 * @param width the buffer's width, pixels
 * @param height its height, rows
 * @return the turned rectangle, of the same size
 */
gfx_rect_t gfx_damage_turn(gfx_rect_t r, int width, int height);

/**
 * @brief Report the primitives' writes to @p d, or to nothing.
 * @param d the record, or NULL to stop counting
 */
void gfx_damage_attach(gfx_damage_t *d);

#ifdef __cplusplus
}
#endif
