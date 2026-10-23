/**
 * @file ui_icons.h
 * @brief Icons drawn with primitives rather than stored as bitmaps.
 *
 * Fourteen glyphs at any size, a few hundred bytes of code each, built from
 * filled spans and discs, which are cheap to draw into PSRAM (pseudo-static
 * random-access memory).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An icon centred on (x, y), inside a box of size x size px around
 *        it.  Every ui_icon_ function below has this shape: @p c the
 *        canvas, @p x and @p y the centre, @p size the box's side in px,
 *        @p color the colour.
 */
typedef void (*ui_icon_fn)(gfx_canvas_t *c, int x, int y, int size,
                           gfx_color_t color);

/**
 * @brief The motor glyph: a stator ring and its hub.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_motor(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief The servo glyph: a case and its horn.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_servo(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief The chart glyph: a trace over its axes.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_chart(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief The record glyph: a recorder with its tray.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_record(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief The chip glyph: a package and its pins.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_chip(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief The settings glyph: three sliders.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_sliders(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief The battery glyph: a cell with its terminal and three bars.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_battery(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief The balance glyph: a disc with an off-centre mass and two
 *        correction ticks.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_balance(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief The supply glyph: a bench supply's front.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_supply(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief The pin list glyph: three rows, each a pin and its line.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_list(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief The board glyph: a board from above with its pads.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_board(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief The Wi-Fi glyph: arcs over a dot.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_wifi(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief A folder: its tab and its body.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_folder(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/**
 * @brief A page of a file, its corner folded.
 * @param c      the canvas
 * @param x      the icon's centre, x
 * @param y      the icon's centre, y
 * @param size   the box's side, px
 * @param color  the colour
 */
void ui_icon_file(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);

#ifdef __cplusplus
}
#endif
