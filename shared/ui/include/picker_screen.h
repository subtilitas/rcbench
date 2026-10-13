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
 * does not carry a shape page is not drawn at all, because a picture from a
 * guessed one points at the wrong pad as confidently as the right one; its
 * pins are still offered on the outputs screen.
 *
 * The photograph is optional in the same way. With one, the board on screen
 * is the board in the operator's hands; without one, its outline and pads are
 * drawn from the shape. The picture is cropped to the outline, so a pad's
 * position in millimetres is its position in the picture.
 *
 * The protocol a tap binds to is chosen here or on the outputs screen, with
 * the same list; the two screens show one selection.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
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

/**
 * Follow the outputs screen: the protocol selected there is the group a tap
 * here joins, and a binding that is not confirmed there
 * (outputs_screen_editable() false) is not edited here either.  Called
 * every frame; it repaints only on a change.
 *
 * While @p editable is false every button is drawn as not offered, NOT READ
 * stands under the protocol list, and a tap changes nothing and calls
 * nothing.
 */
void picker_screen_follow(uint8_t proto, bool editable);

/**
 * Called with the protocol picked in this screen's list, so the outputs
 * screen selects the same one (outputs_screen_select()).  A pick writes
 * nothing; the apply function is not called for it.  OFF picked here ends
 * when this screen is left, on both screens (outputs_screen_end_off()).
 */
typedef void (*picker_select_fn)(uint8_t proto);
void picker_screen_set_select(picker_select_fn fn);

/** No pin, for picker_screen_set_prog_pin(). */
#define PICKER_NO_PIN 0xFFu

/**
 * The GPIO the coprocessor's programming port is bound to, as its page reads
 * it, or PICKER_NO_PIN (the start value).  While no protocol holds that pin
 * its button is drawn held, a tap on it does nothing, and IN USE lists it
 * first under PROG PORT.  A pin a slot holds is that slot's: the coprocessor
 * gives the port no pin then.  It stays across a reset of the screen.
 */
void picker_screen_set_prog_pin(uint8_t gpio);

/** The GPIO IN USE lists under PROG PORT, or PICKER_NO_PIN. */
uint8_t picker_screen_prog_listed(void);

/** What a pin's button shows and does. */
typedef enum {
    PICKER_PIN_NONE = 0,  /**< no such pin, or a board this screen cannot draw */
    PICKER_PIN_FREE,      /**< ivory or ebony: a tap binds it                 */
    PICKER_PIN_TICKED,    /**< accented: bound in the protocol selected; a tap
                               frees it                                       */
    PICKER_PIN_HELD,      /**< grey: another protocol or the programming port
                               has it                                         */
    PICKER_PIN_REFUSED,   /**< grey: free, and the protocol selected can take
                               no more pins; the reason line says why         */
    PICKER_PIN_LOCKED,    /**< grey: the binding is not confirmed, or the
                               board's pins are soldered                      */
    PICKER_PIN_RESERVED,  /**< no button: the coprocessor's own, crossed red
                               on the pad                                     */
} picker_pin_t;

/**
 * The state of catalogue entry @p index, as the screen draws it.  @p holder,
 * when not NULL, gets what holds the pin: the protocol's name or PROG PORT
 * for PICKER_PIN_HELD, the reserving function for PICKER_PIN_RESERVED, and
 * an empty string otherwise.
 */
picker_pin_t picker_screen_pin(uint8_t index, char *holder, size_t cap);

/** The rectangle of the button of catalogue entry @p index; false for a pin
 *  with none. */
bool picker_screen_button_rect(uint8_t index, gfx_rect_t *out);

/**
 * The round marker of entry @p entry of the board's pad list
 * (outbind_pads()), with its dark ring: a G for a ground, the voltage for a
 * rail.  False for a pad with no marker.
 */
bool picker_screen_marker_rect(uint8_t entry, gfx_rect_t *out);

/** One line of the PINS table: a pin of the protocol selected. */
typedef struct {
    uint8_t index;    /**< catalogue entry                                  */
    uint8_t gpio;
    int8_t  channel;  /**< the channel the OUTPUTS page gives it, -1 none   */
    uint8_t pad;      /**< the pad number printed on the board              */
} picker_row_t;

/** Line @p k of the PINS table, in catalogue order; false past the last. */
bool picker_screen_row(uint8_t k, picker_row_t *out);

/**
 * A line of the IN USE table, right of the board: the programming port's
 * pin first, then each other protocol's pins under its name, groups in the
 * order their first pin falls.
 *
 * Every line is drawn.  The rows are 20 px for a name and 28 px for a pin,
 * with the GPIO in the head face; when that runs past the table's foot, as
 * with OFF selected, the port listed and 8 pins over 6 protocols, every pin
 * row is 20 px with the GPIO in the label face.  At 20 px the largest
 * table any binding gives -- the port, 7 names, 8 pins -- is 340 px of the
 * 360 the table has under its heading.
 */
typedef enum {
    PICKER_USE_NAME = 0,  /**< a protocol's name, or PROG PORT              */
    PICKER_USE_PIN,       /**< a pin of the group named above it            */
} picker_use_kind_t;

/** The group of the programming port's lines, in picker_use_t.proto. */
#define PICKER_USE_PROG 0xFFu

/** The most lines the table can have: the port's two, 7 names, 8 pins. */
#define PICKER_USE_MAX  17u

typedef struct {
    picker_use_kind_t kind;
    uint8_t    proto;    /**< the group: a protocol, or PICKER_USE_PROG     */
    uint8_t    gpio;     /**< PICKER_USE_PIN: the pin's GPIO                */
    int8_t     channel;  /**< PICKER_USE_PIN: its channel, -1 for the port  */
    bool       compact;  /**< drawn in the 20 px pin rows                   */
    gfx_rect_t rect;     /**< the line's box, inside PICKER_AREA_IN_USE     */
} picker_use_t;

/** Line @p k of the IN USE table; false past the last. */
bool picker_screen_in_use(uint8_t k, picker_use_t *out);

/** The count under the protocol list: slots in use of 8, every protocol. */
void picker_screen_count(char *buf, size_t cap);

/**
 * The amber line under the count: NOT READ while the binding is not
 * confirmed, else the reason the protocol selected can take no more pins
 * (outputs_reason_of()).  False and an empty string when there is none.
 */
bool picker_screen_reason(char *buf, size_t cap);

/** The areas the screen draws text in, for tests and the renderer. */
typedef enum {
    PICKER_AREA_HEAD = 0, /**< the protocol list's face, the count, the reason */
    PICKER_AREA_PINS,     /**< the PINS table, left of the board               */
    PICKER_AREA_IN_USE,   /**< the IN USE table, right of the board            */
} picker_area_t;
gfx_rect_t picker_screen_area(picker_area_t area);

/** The protocol list: its face, and the row of @p proto when it is open. */
gfx_rect_t picker_screen_list_face(void);
gfx_rect_t picker_screen_list_row(uint8_t proto);
bool picker_screen_list_open(void);

/** Whether a tap on a button can change the binding. */
bool picker_screen_editable(void);

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
