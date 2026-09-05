/*
 * The pin picker: the board itself, with a button on every pin an output may
 * have.
 *
 * The outputs screen lists pins in a grid, which answers "which GPIO
 * (general-purpose input/output) is bound" and not "where do I put the lead".
 * This screen answers the second one: a picture of the board with a straight
 * trace from each pad to a button large enough to press.
 *
 * The buttons are not the pads. At any size that fits a 480-pixel panel a pad
 * is under 40 pixels across, which is smaller than a fingertip; so the pads
 * are drawn where they are and the touching happens on staggered rows of
 * buttons beside the board, each wired to its own pad.
 *
 * Where the pads are comes from the board, not from this file: the shape page
 * carries the outline, the pitch and the corner pad 1 sits at, and
 * outbind_pad_xy() turns a pad number into a position on it. A board that
 * does not say is not drawn, and the outputs screen still offers its pins.
 *
 * The photograph is optional in the same way. With one, the board on screen
 * is the board in the operator's hands; without one, its outline and pads are
 * drawn from the shape. The picture is cropped to the outline, so a pad's
 * position in millimetres is its position in the picture.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "out_bind.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

const ui_screen_t *picker_screen(void);

/** Repaint the cached chrome in every framebuffer. */
void picker_screen_invalidate(void);

/** The current choice, for anything that has to write it. */
const outbind_t *picker_screen_binding(void);

/** Set the choice: this screen and the outputs screen are two views of one. */
void picker_screen_set_binding(const outbind_t *b);

/** Called after any change the operator makes, as on the outputs screen. */
typedef void (*picker_apply_fn)(const outbind_t *b);
void picker_screen_set_apply(picker_apply_fn fn);

/**
 * The board's photograph, or NULL to draw its outline instead.
 *
 * RGB565 (red, green, blue, five, six and five bits), @p w by @p h, cropped
 * to the board's outline -- which is what makes a pad's position in
 * millimetres its position in the picture. The pixels are not copied and
 * must outlive the screen; on the panel they are the flash the store keeps
 * them in.
 */
void picker_screen_set_artwork(const gfx_color_t *px, uint16_t w, uint16_t h);

/**
 * Asked on entering the screen, for the board the binding names.
 *
 * The photograph lives in the panel's flash and is two hundred kilobytes;
 * reading it on entry rather than holding it is what keeps that out of the
 * screen. The function calls picker_screen_set_artwork(), with NULL when
 * there is nothing kept for that board.
 */
typedef void (*picker_artwork_fn)(uint16_t board);
void picker_screen_set_artwork_source(picker_artwork_fn fn);

/** Whether this board can be drawn at all: it has a catalogue and a shape. */
bool picker_screen_can_draw(void);

#ifdef __cplusplus
}
#endif
