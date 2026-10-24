/**
 * @file
 * @brief The IOmcu's drawing of its own board, on a screen: whether it
 *        describes the board the panel knows, and drawing it into a box.
 *
 * The drawing comes from the IOmcu (link_drawing.h has the layout),
 * the pins the panel offers come from its catalogue.  A drawing whose pads
 * say something else than the catalogue -- another GPIO on a pad, a ground
 * where the catalogue has a rail, a pad off the shape's grid -- is refused
 * whole: a screen that draws one board and binds another sends a lead to
 * the wrong pad.
 *
 * Every position is scaled from its own value in 0.01mm, never from a
 * neighbour's screen position, so rounding does not add up along a row of
 * forty pads.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "link_drawing.h"
#include "out_bind.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Whether @p d describes @p board: every catalogue pin is a GPIO pad of the
 * drawing with the same GPIO and no other pad is one; every pad the board
 * lists that is not a pin has the same kind and rail, and the drawing has
 * no other; with a shape, the outline is the shape's and every pad sits
 * where outbind_pad_xy() places it.
 *
 * False for a NULL board -- one this build neither ships nor learned -- with
 * @p why (may be NULL) naming the first disagreement for a log line.
 */
bool board_drawing_agrees(const link_drawing_t *d,
                          const outbind_board_t *board, const char **why);

/**
 * @brief A length of the drawing as pixels, rounded to the nearest.
 * @param v the length, 0.01mm
 * @param px the pixels @p full takes
 * @param full the whole, 0.01mm
 * @return @p v of @p full as pixels of @p px; 0 for a @p full of 0
 */
int board_drawing_scale(uint32_t v, int px, uint16_t full);

/**
 * Draw @p d into the box @p x, @p y, @p w by @p h: the outline in the
 * board colour, the shapes in order, then every pad ring with its hole in
 * @p hole, the colour behind the board.
 */
void board_drawing_render(gfx_canvas_t *c, const link_drawing_t *d, int x,
                          int y, int w, int h, gfx_color_t hole);

#ifdef __cplusplus
}
#endif
