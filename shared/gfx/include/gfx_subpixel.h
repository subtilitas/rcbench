/**
 * @file gfx_subpixel.h
 * @brief gfx's anti-aliased disc, capsule and ring placed in fractions of a
 *        pixel.
 *
 * The whole-pixel shapes of gfx.h jump a pixel at a time when the thing
 * they draw moves: a servo horn turning by a tenth of a degree moves its tip
 * by a sixth of a pixel and is drawn where it was, then a pixel further.
 * These take their centres, ends, radii and widths as floats and blend
 * their edge pixels from the exact distance, so a shape that moves by a
 * tenth of a pixel changes its edge pixels' coverage by about a tenth.  At
 * whole numbers the disc and the capsule give the same pixels as
 * gfx_fill_circle_aa() and gfx_capsule_aa(), which are drawn through them.
 *
 * They follow gfx.h's contracts: everything clips to canvas->clip, and
 * coverage is blended per call.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A disc of radius @p r centred on (@p cx, @p cy), anti-aliased from
 *        each pixel's distance to the centre.
 * @param c the canvas
 * @param cx the centre's column, px
 * @param cy the centre's row, px
 * @param r the radius, px; 0 or less draws nothing
 * @param col the colour
 */
void gfx_fill_circle_aaf(gfx_canvas_t *c, float cx, float cy, float r,
                         gfx_color_t col);

/**
 * @brief A capsule, a thick line with round ends, from (@p ax, @p ay) to
 *        (@p bx, @p by), anti-aliased from each pixel's distance to its
 *        spine.
 * @param c the canvas
 * @param ax one end's column, px
 * @param ay its row, px
 * @param bx the other end's column, px
 * @param by its row, px
 * @param thickness the width, px; 0 or less draws nothing
 * @param col the colour
 */
void gfx_capsule_aaf(gfx_canvas_t *c, float ax, float ay, float bx, float by,
                     float thickness, gfx_color_t col);

/**
 * @brief A ring, or an arc of one, solved per pixel from its distance and
 *        direction to the centre.
 *
 * gfx_arc_fade() walks its arc and rounds each step to a pixel.  This one
 * blends the band's two edges and the arc's two ends over half a pixel
 * either side, the ends measured along the arc at each pixel's radius.  A
 * span of 360deg or more is the whole ring.  A pixel whose centre lies
 * half a pixel or more beyond the band or past an end is not touched.
 * @param c the canvas
 * @param cx the centre's column, px
 * @param cy the centre's row, px
 * @param r the band's middle radius, px
 * @param thickness the band's width, px
 * @param a0_deg one end, degrees counter-clockwise from +x (90 is up)
 * @param a1_deg the other end, degrees
 * @param col the colour
 * @param into the colour each end fades from over @p fade_deg
 * @param fade_deg the fade's length, degrees; 0 for none
 */
void gfx_ring_aaf(gfx_canvas_t *c, float cx, float cy, float r,
                  float thickness, float a0_deg, float a1_deg,
                  gfx_color_t col, gfx_color_t into, float fade_deg);

#ifdef __cplusplus
}
#endif
