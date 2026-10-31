/**
 * @file
 * @brief OUTPUTS' board view: the board itself, with a button on every pin an
 * output may have.  The router reaches it through outputs_view_screen().
 *
 * A grid of GPIO (general-purpose input/output) numbers answers "which GPIO
 * is bound" and not "where do I put the lead".  This view answers the second
 * one: a picture of the board with a straight trace from each pad to a button
 * large enough to press.
 *
 * The buttons are not the pads. At any size that fits a 1024 x 600 panel a pad
 * is under 40 pixels across, which is smaller than a fingertip; so the pads
 * are drawn where they are and the touching happens on staggered rows of
 * buttons beside the board, each wired to its own pad.
 *
 * Where the pads are comes from the board, not from this file: the shape page
 * carries the outline, the pitch and the corner pad 1 sits at, and
 * outbind_pad_xy() turns a pad number into a position on it; the board's
 * drawing carries every pad's position itself. A board with neither is not
 * drawn at all, because a picture from a guessed shape points at the wrong
 * pad as confidently as the right one; OUTPUTS draws its grid view then.
 *
 * The IOmcu's drawing of its board is optional in the same way. With
 * one, the board on screen shows the parts of the board in the operator's
 * hands, to scale; without one, its outline and pads are drawn from the
 * shape. The drawing is checked against the catalogue before it is shown
 * (board_drawing_agrees()), so the pads it draws are the pads the buttons
 * bind.
 *
 * The protocol a tap binds to is chosen in the list; outputs_screen.c holds
 * the selection for both views.  A visit from SETUP opens with OFF selected,
 * the overview: no key acts until a protocol is picked.  A TARGET row opens it
 * on what its workbench drives (outputs_screen_open_on()).
 *
 * The rest of what OUTPUTS shows stands around the board: with a PROG PORT
 * entry picked its rows in the column left of it, on a protocol LAST WRITE
 * right of the list's face, and after a read no binding describes UNBIND ALL
 * PINS under IN USE.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "link_pages.h"
#include "out_bind.h"
#include "outputs_screen.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

const ui_screen_t *picker_screen(void);

/** Repaint the cached chrome in every framebuffer. */
void picker_screen_invalidate(void);

/** The current choice, for anything that has to write it. */
const outbind_t *picker_screen_binding(void);

/** Set the choice: this screen and the outputs screen are two views of one.
 *  A binding that names another board asks the drawing source
 *  (picker_screen_set_drawing_source()) for that board's drawing. */
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
 * nothing; the apply function is not called for it.  Entering this screen
 * calls it with OFF (0), so every visit opens on the overview: OFF
 * selected on both screens, and a tap on a key changes nothing until a
 * protocol is picked.
 */
typedef void (*picker_select_fn)(uint8_t proto);
void picker_screen_set_select(picker_select_fn fn);

/** No pin, for picker_screen_set_prog_pin(). */
#define PICKER_NO_PIN 0xFFu

/**
 * The GPIO the IOmcu's programming port is bound to, as its page reads
 * it, or PICKER_NO_PIN (the start value): picker_screen_set_prog_ports()
 * with that one port.  While no protocol holds that pin its button is
 * drawn held, a tap on it does nothing, and IN USE lists it first under
 * PROG PORT.  A pin a slot holds is that slot's: the IOmcu gives the port
 * no pin then.  It stays across a reset of the screen.
 */
void picker_screen_set_prog_pin(uint8_t gpio);

/**
 * Every programming port's pin: @p count of them (at most
 * LINK_PG_PORTS_MAX are kept) from @p pins, PICKER_NO_PIN for a port
 * without one; NULL or 0 for none.  Each pin is held as
 * picker_screen_set_prog_pin() holds one, IN USE lists them all under the
 * one PROG PORT name, and a PWM pin is refused beside any of them
 * (outbind_pwm_clash_ports()).  It stays across a reset of the screen.
 */
void picker_screen_set_prog_ports(uint8_t count, const uint8_t *pins);

/*
 * With a port picked, in this list or by outputs_screen_open_on_port(), a
 * button is the port's pin: its own pin is ticked and a tap on it asks the
 * IOmcu for no pin, a tap on a free pin asks for that pin (port_view_ask()).
 * A SERVO PWM port is refused the slice of a bound PWM output and the compare
 * register of another SERVO PWM port; a NONE port takes any free pin.  The
 * pins are the IOmcu's to take: the ask waits for a bench at rest, and the
 * amber line says how it stands.
 */

/** The port picked, 0 for port 1, or -1 for a protocol. */
int picker_screen_port_shown(void);

/** The first GPIO IN USE lists under PROG PORT, or PICKER_NO_PIN. */
uint8_t picker_screen_prog_listed(void);

/** What a pin's button shows and does. */
typedef enum {
    PICKER_PIN_NONE = 0,  /**< no such pin, or a board this view cannot draw   */
    PICKER_PIN_FREE,      /**< ivory or ebony: a tap binds it                 */
    PICKER_PIN_TICKED,    /**< accented: bound in the protocol selected; a tap
                               frees it                                       */
    PICKER_PIN_HELD,      /**< grey: another protocol or the programming port
                               has it, or the PWM generator cannot take it
                               beside the port's slice or a bound pin's
                               compare register (outbind_pwm_clash())         */
    PICKER_PIN_REFUSED,   /**< grey: free, and the protocol selected can take
                               no more pins; the reason line says why         */
    PICKER_PIN_LOCKED,    /**< grey: the binding is not confirmed, or the
                               board's pins are soldered                      */
    PICKER_PIN_RESERVED,  /**< no button: the IOmcu's own, crossed red
                               on the pad                                     */
} picker_pin_t;

/**
 * @brief The state of catalogue entry @p index, as the screen draws it.
 * @param index   the pin's catalogue entry
 * @param holder  NULL, or takes what holds the pin: the protocol's name,
 *                PROG PORT, or the pin a PWM pin clashes with
 *                (outputs_clash_label(): "SLICE OF GPIO 13", "PWM CH OF
 *                GPIO 0") for PICKER_PIN_HELD, the reserving function for
 *                PICKER_PIN_RESERVED, and an empty string otherwise
 * @param cap     bytes at @p holder
 * @return the pin's state
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
 * @brief The kinds of a line of the IN USE table.
 *
 * A line of the IN USE table, right of the board: the programming ports'
 * pins first, under one PROG PORT name, then the pins SETUP > INTERFACES
 * holds, each function's under its name (outputs_iface_name()), then each
 * other protocol's pins under its name, groups in the order their first
 * pin falls.
 *
 * The rows are 20px for a name and 28px for a pin, with the GPIO in the
 * 16 x 28 face: OFF selected, the port listed and 8 pins over 6 protocols
 * take 392px of the 432 the table has under its heading.  When that runs
 * past the table's foot, as with 4 ports beside that binding (7 names and
 * 12 pins), every pin row is 20px with the GPIO in the label face, and
 * 16px in 8 x 16 where 20px still runs past it.  Where 16px pin rows
 * still run past it, the names are 16px too: 4 ports, 6 protocols and
 * every interface on take 27 lines, the whole 432px.  Where even that
 * runs past it the table holds the lines that fit and ends in a
 * PICKER_USE_MORE line, "+N", N the lines left out.
 */
typedef enum {
    PICKER_USE_NAME = 0,  /**< a protocol's name, PROG PORT or a function
                               of SETUP > INTERFACES                         */
    PICKER_USE_PIN,       /**< a pin of the group named above it            */
    PICKER_USE_MORE,      /**< "+N": gpio holds N, the lines left out       */
} picker_use_kind_t;

/** The group of the programming port's lines, in picker_use_t.proto. */
#define PICKER_USE_PROG 0xFFu
/** The group of an interface's lines, outputs_iface_t @p f. */
#define PICKER_USE_IFACE(f) (0xF0u + (unsigned)(f))
/** Whether group @p g is an interface's, and which. */
#define PICKER_USE_IS_IFACE(g) ((g) > 0xF0u && (g) < 0xF0u + OUTPUTS_IFACE_COUNT)

/** Pins SETUP > INTERFACES can hold: SDA and SCL, TX and RX, the tap. */
#define PICKER_USE_IFACE_PINS 5u

/** The most lines the table can list: the PROG PORT name and a pin for
 *  each port, a name and the pins for each interface, a name for each
 *  protocol but OFF, and a pin for each slot. */
#define PICKER_USE_MAX                                                   \
    (1u + LINK_PG_PORTS_MAX + (OUTPUTS_IFACE_COUNT - 1u)                 \
     + PICKER_USE_IFACE_PINS + (OUTBIND_PROTOS - 1u) + LINK_OUT_SLOTS)

typedef struct {
    picker_use_kind_t kind;
    uint8_t    proto;    /**< the group: a protocol, PICKER_USE_PROG or
                              PICKER_USE_IFACE()                          */
    uint8_t    gpio;     /**< PICKER_USE_PIN: the pin's GPIO                */
    int8_t     channel;  /**< PICKER_USE_PIN: its channel, -1 for the port  */
    bool       compact;  /**< drawn in the 20 or 16px pin rows              */
    gfx_rect_t rect;     /**< the line's box, inside PICKER_AREA_IN_USE     */
} picker_use_t;

/** Line @p k of the IN USE table; false past the last. */
bool picker_screen_in_use(uint8_t k, picker_use_t *out);

/**
 * @brief The count under the protocol list: slots in use of 8, every protocol;
 *        an empty string with a port picked.
 * @param buf takes the line
 * @param cap bytes at @p buf
 */
void picker_screen_count(char *buf, size_t cap);

/**
 * The amber line under the count: NOT READ while the binding is not
 * confirmed, else the reason the protocol selected can take no more pins
 * (outputs_reason_of()), else why the PWM generator cannot take the pin
 * last tapped (outputs_clash_reason()), until another pick or tick.
 * False and an empty string when there is none.
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

/**
 * @brief Where LAST WRITE stands on this board.
 * @return the box
 */
gfx_rect_t picker_screen_last_rect(void);

/** Whether a tap on a button can change the binding. */
bool picker_screen_editable(void);

/** Called after any change the operator makes, as on the outputs screen. */
typedef void (*picker_apply_fn)(const outbind_t *b);
void picker_screen_set_apply(picker_apply_fn fn);

/**
 * The IOmcu's drawing of the board the binding names (LINK_ART_DRAWING
 * bytes, link_drawing.h), or NULL to draw its outline instead.
 *
 * Parsed and checked against the board's catalogue, pads and shape here;
 * a drawing that fails either is not shown, and @p why (may be NULL) names
 * the first thing found. The bytes are not copied and must stay unchanged
 * until the next call. Returns whether the drawing is shown.
 */
bool picker_screen_set_drawing(const uint8_t *data, uint32_t bytes,
                               const char **why);

/**
 * @brief Asked on entering OUTPUTS, for the board the binding names.
 *
 * The function calls picker_screen_set_drawing(), with NULL when there is
 * no drawing for that board, so a drawing that arrived since the last visit
 * is the one shown and one for a board that has been swapped is not.
 */
typedef void (*picker_drawing_fn)(uint16_t board);
void picker_screen_set_drawing_source(picker_drawing_fn fn);

/**
 * @brief The size in pixels a board outline draws at.
 *
 * 600 wide and the height its aspect gives, or, when that is taller than
 * 240, 240 high and the width its aspect gives.  A zero side gives 600 x 240.
 * @param ow The outline's width, any unit.
 * @param oh Its height, in the same unit.
 * @param w  The width drawn, px.
 * @param h  The height drawn, px.
 */
void picker_board_fit(uint16_t ow, uint16_t oh, int *w, int *h);

/**
 * @brief The cat the last frame drew (picker_cat.c): one pose, picked at
 *        random on a visit's first frame among the ones with a clear spot,
 *        bottom left or bottom right.  It takes no touch.
 * @param box the box it covers, empty for none; may be NULL
 * @param pose its index into ui_cat_poses[], UI_CAT_NONE for none; may
 *        be NULL
 * @return true when a cat is drawn
 */
bool picker_screen_cat(gfx_rect_t *box, int *pose);

/** Whether the board is drawn from the IOmcu's drawing. */
bool picker_screen_has_drawing(void);

/** Whether this board can be drawn at all: it has a catalogue, and a
 *  drawing or a shape. */
bool picker_screen_can_draw(void);

#ifdef __cplusplus
}
#endif
