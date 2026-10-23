/*
 * Icons drawn with primitives rather than stored as bitmaps.
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

/* An icon centred on (x, y), inside a box of size x size px around it. */
typedef void (*ui_icon_fn)(gfx_canvas_t *c, int x, int y, int size,
                           gfx_color_t color);

void ui_icon_motor(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
void ui_icon_servo(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
void ui_icon_chart(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
void ui_icon_record(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
void ui_icon_chip(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
void ui_icon_sliders(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
void ui_icon_battery(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
void ui_icon_balance(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
void ui_icon_supply(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
void ui_icon_list(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
void ui_icon_board(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
void ui_icon_wifi(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/** @brief A folder: its tab and its body. */
void ui_icon_folder(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);
/** @brief A page of a file, its corner folded. */
void ui_icon_file(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color);

#ifdef __cplusplus
}
#endif
