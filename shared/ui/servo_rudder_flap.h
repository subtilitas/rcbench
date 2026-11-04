/**
 * @file
 * @brief RUDDER's side view of the control surface (servo_rudder_flap.c):
 *        its geometry from the area it is drawn in, and the drawing.
 *
 * The view is a section through the surface, the leading edge on the
 * left: a grey fixed surface from a rounded leading edge to the hinge, the
 * hinge as a dot, a thin neutral line, the flap as a cyan wedge from the
 * hinge at the deflection shown, and ticks at 0, ±30, ±60 and ±90° just
 * past the flap's reach.  Positive deflection draws the trailing edge up.
 * Dashed lines mark the MAX of each side and, in AUTO's view, the two
 * target angles in amber.
 *
 * The flap's length is fixed by the area, not by the angle: half the
 * area's height less 14px, so the flap and its ticks stay inside the area
 * at every deflection from -90 to +90°.  An angle beyond ±90° is drawn at ±90°; the
 * readout's numbers stay as they are.
 *
 * The geometry is pure; the drawing takes everything it shows as
 * arguments and reads no screen state.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The largest deflection drawn either side, degrees. */
#define RUD_FLAP_MAX_DEG 90.0f
/** @brief The flap's length: half the area's height less this, px. */
#define RUD_FLAP_LEN_INSET 14.0f
/** @brief The fixed surface's length as a multiple of the flap's. */
#define RUD_FLAP_FIXED_SHARE 1.15f
/** @brief The least margin between the drawing and the area's left and
 *         right edges, px. */
#define RUD_FLAP_PAD 6.0f
/** @brief The leading edge's radius, half the fixed surface's 14px
 *         thickness there, px. */
#define RUD_FLAP_LE_R 7.0f
/** @brief The fixed surface's thickness at the hinge, px. */
#define RUD_FLAP_FIXED_END_W 8.0f
/** @brief The flap's thickness at the hinge, px. */
#define RUD_FLAP_ROOT_W 12.0f
/** @brief The flap's thickness at the trailing edge, px. */
#define RUD_FLAP_TIP_W 2.0f
/** @brief The hinge dot's radius, px. */
#define RUD_FLAP_HINGE_R 3.5f
/** @brief The scale's ticks, at 0, ±30, ±60 and ±90°: from this far
 *         past the trailing edge's reach, px ... */
#define RUD_FLAP_TICK_IN 4.0f
/** @brief ... to this far past it, px. */
#define RUD_FLAP_TICK_OUT 10.0f

/** @brief The drawing's places, px, in the canvas's coordinates. */
typedef struct {
    float hinge_x;   /**< the hinge's column                          */
    float hinge_y;   /**< its row: the neutral line                   */
    float flap_len;  /**< hinge to trailing edge                      */
    float fixed_len; /**< the leading edge's centre to the hinge       */
} rudder_flap_t;

/** @brief What the drawing shows. */
typedef struct {
    bool  live;          /**< a deflection on show                    */
    float defl_deg;      /**< it, degrees, positive trailing edge up  */
    bool  have_max[2];   /**< a MAX: 0 the negative side, 1 positive  */
    float max_deg[2];    /**< their magnitudes, degrees               */
    bool  targets;       /**< AUTO's targets shown                    */
    float target_deg[2]; /**< at 1000us and 2000us, signed, degrees   */
} rudder_flap_view_t;

/**
 * @brief The drawing's places in @p area: the hinge at the area's
 *        centre; the flap half the area's height less RUD_FLAP_LEN_INSET
 *        long, shortened where the half width less the margin would not
 *        hold the fixed surface and its leading edge on the left or the
 *        ticks on the right, rounded down to a whole pixel; the fixed
 *        surface RUD_FLAP_FIXED_SHARE x the flap.
 * @param area the area, px
 * @return the places; a flap and a fixed surface 0px long for an area too
 *         small to hold them
 */
rudder_flap_t rudder_flap_layout(gfx_rect_t area);

/**
 * @brief A deflection as drawn: held to -90 to +90°, 0 for a value that
 *        is no number.
 * @param deg the deflection, degrees
 * @return the angle drawn, degrees
 */
float rudder_flap_clamp(float deg);

/**
 * @brief The point @p r px from the hinge along a flap at @p deg.
 * @param f the places
 * @param deg the deflection, degrees, held by rudder_flap_clamp()
 * @param r the distance from the hinge, px
 * @param x where the column goes
 * @param y where the row goes
 */
void rudder_flap_at(const rudder_flap_t *f, float deg, float r, float *x,
                    float *y);

/**
 * @brief The box the flap at @p deg paints, its blended edge and the
 *        hinge dot included: two of them, the flap as drawn and as it is
 *        to be drawn, hold every pixel a change of the angle alone
 *        changes.
 * @param f the places
 * @param deg the deflection, degrees, held by rudder_flap_clamp()
 * @return the box, px, whole pixels
 */
gfx_rect_t rudder_flap_bounds(const rudder_flap_t *f, float deg);

/**
 * @brief The drawing into @p area: the area filled with the card's
 *        colour, the neutral line, the ticks, the dashed MAX and target
 *        lines, the fixed surface, the flap and the hinge.  Without a live deflection
 *        the flap lies dim on the neutral line.
 * @param c the canvas
 * @param area the area, px
 * @param v what it shows; NULL draws the empty area
 */
void rudder_flap_draw(gfx_canvas_t *c, gfx_rect_t area,
                      const rudder_flap_view_t *v);

#ifdef __cplusplus
}
#endif
