/**
 * @file
 * @brief An antialiased ring 1px wide, drawn from a coverage table built
 *        once: the pixels the ring touches, in the order of their angle.
 *
 * A pixel whose center lies at distance d from the ring's center takes
 * the ring's colour with coverage clamp(1 - |d - r|, 0, 1), so the ring is
 * never stair-stepped and is exactly 1px wide.  The table holds every pixel
 * with a coverage above 0: the two pixel rows either side of the radius,
 * about 4 x pi x r pixels, 1,483 at r = 118, 3 bytes each.
 *
 * Angles run from 12 o'clock clockwise, GFX_RING_TURN units a turn.  A draw
 * paints the pixels whose angle lies in a span: those before the arc's end
 * in the arc's colour, the rest in the track's, each blended onto a fixed
 * background colour rather than onto what the canvas holds.  A repaint is
 * then idempotent, and a move of the end from a to b repaints only the
 * pixels between a and b.
 *
 * The table's coordinates are bytes: a ring lies inside a 256 x 256 canvas.
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

/** @brief Angle units a turn: 65,536, so 16,384 is 3 o'clock. */
#define GFX_RING_TURN 65536u
/** @brief The largest radius a table takes, pixels. */
#define GFX_RING_R_MAX 126
/** @brief Table entries enough for a ring of radius @p r: a ring touches
 *         about 4 x pi x r pixels, at most 12.9 x r at a radius of 1 to
 *         GFX_RING_R_MAX (test_gfx_ring holds the cap at every one). */
#define GFX_RING_CAP(r) (13 * (r) + 16)

/** @brief One pixel of a ring's table. */
typedef struct {
    uint8_t x;   /**< column */
    uint8_t y;   /**< row */
    uint8_t cov; /**< coverage, 1 to 255 */
} gfx_ring_px_t;

/** @brief A ring's coverage table. */
typedef struct {
    gfx_ring_px_t *px; /**< the pixels, by angle, the caller's storage */
    uint16_t n;        /**< pixels in the table */
    int16_t  cx2;      /**< the center's column in half pixels: 239 is the
                            point between columns 119 and 120 */
    int16_t  cy2;      /**< the center's row in half pixels */
    int16_t  r;        /**< the radius, pixels */
} gfx_ring_t;

/**
 * @brief Build a ring's table.
 * @param ring the table to fill
 * @param cx2  the center's column in half pixels; a pixel's center is its
 *             column, so 239 is the middle of a 240px canvas
 * @param cy2  the center's row in half pixels
 * @param r    the radius, 1 to GFX_RING_R_MAX
 * @param px   storage for @p cap pixels
 * @param keys scratch of @p cap words for the sort, free again on return
 * @param cap  entries in @p px and @p keys
 * @return the pixels in the table, or 0 when an argument is out of range,
 *         a pixel falls outside 0 to 255 or @p cap is too small
 */
int gfx_ring_build(gfx_ring_t *ring, int cx2, int cy2, int r,
                   gfx_ring_px_t *px, uint32_t *keys, int cap);

/**
 * @brief The angle of a point seen from the ring's center, from 12 o'clock
 *        clockwise: the order of the table.
 * @param ring the table, for its center
 * @param x    the column
 * @param y    the row
 * @return 0 to GFX_RING_TURN - 1
 */
uint32_t gfx_ring_angle(const gfx_ring_t *ring, int x, int y);

/**
 * @brief The first entry whose angle is @p ang or more.
 * @param ring the table
 * @param ang  the angle, 0 to GFX_RING_TURN
 * @return 0 to ring->n
 */
uint32_t gfx_ring_find(const gfx_ring_t *ring, uint32_t ang);

/**
 * @brief The end angle of an arc that covers @p num / @p den of the turn,
 *        rounded, held to 0 to GFX_RING_TURN.
 * @param num the part; above @p den is the whole turn
 * @param den the whole; 0 gives 0
 * @return the end angle
 */
uint32_t gfx_ring_end(uint64_t num, uint64_t den);

/**
 * @brief Paint the table's pixels whose angle lies in [@p from, @p to):
 *        before @p end in @p on, from @p end on in @p track, each blended
 *        by its coverage onto @p bg.  Pixels outside the canvas's clip are
 *        left; the painted area is reported to the damage record once, as
 *        the box around the painted pixels.
 * @param c     the canvas
 * @param ring  the table
 * @param from  the span's first angle, 0 to GFX_RING_TURN
 * @param to    one past its last angle, @p from to GFX_RING_TURN
 * @param end   the arc's end angle, 0 (track only) to GFX_RING_TURN (all arc)
 * @param on    the arc's colour
 * @param track the track's colour
 * @param bg    the colour under the ring
 * @return the pixels painted
 */
int gfx_ring_aa(gfx_canvas_t *c, const gfx_ring_t *ring, uint32_t from,
                uint32_t to, uint32_t end, gfx_color_t on, gfx_color_t track,
                gfx_color_t bg);

/**
 * @brief Repaint a ring after its arc's end moved from @p was to @p now:
 *        only the pixels between the two angles change colour, and only
 *        they are painted.
 * @param c     the canvas
 * @param ring  the table
 * @param was   the end the canvas shows
 * @param now   the new end
 * @param on    the arc's colour
 * @param track the track's colour
 * @param bg    the colour under the ring
 * @return the pixels painted
 */
int gfx_ring_move(gfx_canvas_t *c, const gfx_ring_t *ring, uint32_t was,
                  uint32_t now, gfx_color_t on, gfx_color_t track,
                  gfx_color_t bg);

#ifdef __cplusplus
}
#endif
