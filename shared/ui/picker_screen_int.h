/**
 * @file picker_screen_int.h
 * @brief PICK A PIN's private seam between picker_screen.c, the state, the
 *        touch handling and the screen's interface, picker_geometry.c, where
 *        the board, its pads, buttons and marks and the protocol list sit, and
 *        picker_draw.c, the drawing: the layout they work on, the state and
 *        the functions one calls in another.  Nothing outside shared/ui
 *        includes this header; the screen's interface is picker_screen.h.
 *
 * Included last: its layout macros have short names (GAP, ROW_H, INK) and
 * it names the state s, which no header included after it may meet.  The
 * functions one file calls in another carry the prefix pk_.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "link_drawing.h"
#include "out_bind.h"
#include "outputs_screen.h"
#include "picker_screen.h"
#include "ui_screen.h"

#define SCREEN_W 800
#define SCREEN_H (480 - UI_BAND_H)
#define MAX_FBS  3

/*
 * The board is drawn as wide as the columns beside it leave room for, and as
 * tall as its own outline makes it.  500 is what fits with a table either
 * side; the height follows the aspect so a board is never stretched.
 */
#define BOARD_W  500
#define BOARD_MAX_H 240

/*
 * Two staggered rows of buttons above the board and two below.  A button is
 * 43 wide because that is a fingertip and because twenty of them at 2.54 mm
 * pitch do not fit in one row at any board size that fits the panel.
 */
#define TIERS 2
#define TGAP  5
#define BTNW  43
#define GAP   4

/*
 * The marks inside the board.  A ground is a round marker 36 px across, 40
 * px in from its pad: a servo lead needs a ground as much as a signal, so it
 * is as legible as a button, and grounds are five pads apart, so it has
 * nothing to collide with.  A rail is 32 px across, enough for "5V0", at one
 * of two depths that alternate along the row: rails sit on neighbouring
 * pads, 25 px apart, and a rail beside a ground goes below the ground's
 * marker -- 78 px is the ground's 40 plus both radii and both rings.
 */
#define GND_R       18
#define GND_DROP    40
#define RAIL_R      16
#define RAIL_DROP_0 30
#define RAIL_DROP_1 78
#define RING        2       /* the dark ring around a marker */

/*
 * Above the PINS table, left of the first button above the board: the
 * protocol list's face, the count, and the reason no pin can be added.  The
 * face is the outputs screen's: 266 px holds the longest protocol name,
 * "DSHOT600 BIDIR", in the head face.  The reason takes a second line where
 * it needs one, above the PINS table: what is left of it, 17 cells at most
 * (test_picker_screen holds every reason to it), so it ends left of the
 * widest board's edge at 150 px.
 */
#define HEAD_X      6
#define HEAD_Y      8
#define HEAD_W      280
#define FACE_W      266
#define FACE_H      48
#define COUNT_Y     (HEAD_Y + FACE_H + 8)
#define REASON_Y    (COUNT_Y + 20)
#define HEAD_H      (REASON_Y + 18 + 16 - HEAD_Y)

/* The open list covers the board, as it covers the pins on OUTPUTS. */
#define POP_W       300
#define POP_ROW     46

/*
 * The PINS table, left of the board: 128 px, "CH7" then "GP28" in the head
 * face from 40 px in, and the pad number right-aligned.  It starts below the
 * reason line and ends above nothing: the first button below the board is
 * right of it.
 */
#define PINS_X      6
#define PINS_Y      (HEAD_Y + HEAD_H + 10)
#define PINS_W      128
#define ROW_H       28

/*
 * The IN USE table, right of the board.  Its lines start under the heading,
 * the rule and the BY OTHERS line, and end at the area's foot.  A name is
 * 20 px; a pin is 28 px with the GPIO in the head face, or 20 px with it in
 * the label face when the 28 px rows run past the foot, and 16 px when the
 * 20 px rows do.
 */
#define USE_X       (SCREEN_W - 132)
#define USE_Y       8
#define USE_W       126
#define USE_TOP     (USE_Y + 60)
#define USE_FOOT    (SCREEN_H - 4)
#define USE_NAME_H  20
#define USE_PIN_H   28
#define USE_PIN_H_C 20
#define USE_PIN_H_D 16      /* where 20 px rows still pass the foot */

/* With no interface on, every line fits at the 16 px pin rows: every port,
 * every protocol's name and every slot's pin. */
_Static_assert(USE_TOP
                   + (1 + (int)OUTBIND_PROTOS - 1) * USE_NAME_H
                   + ((int)LINK_PG_PORTS_MAX + (int)LINK_OUT_SLOTS)
                         * USE_PIN_H_D
                   <= USE_FOOT,
               "the IN USE table holds a binding beside 4 ports");

#define IVORY GFX_RGB(236, 235, 228)
#define EBONY GFX_RGB(22, 23, 28)
#define INK   GFX_RGB(10, 10, 14)
#define HALO  GFX_RGB(6, 6, 8)

enum { HIT_NONE = 0, HIT_FACE, HIT_ROW, HIT_BUTTON };

typedef struct {
    outbind_t         bind;
    picker_apply_fn   apply;
    picker_select_fn  select;
    picker_drawing_fn source;
    /* The IOmcu's drawing, for the board drawing_board names; it
     * agreed with that board's catalogue when it was set. */
    link_drawing_t    drawing;
    uint16_t          drawing_board;
    bool              has_drawing;

    int      hit_kind;
    int      hit;            /**< row or catalogue index under the finger    */
    bool     open;           /**< the protocol list is open                  */
    /* The binding shown is not one a read confirmed: no button acts. */
    bool     unread;
    /* The pin whose tap a PWM clash refused last, or -1: the reason line
     * says why, until another pick or tick. */
    int      refused;
    /* Each interface's pins at the last frame. */
    uint64_t iface[OUTPUTS_IFACE_COUNT];
    /* The PROG PORT picked, -1 for a protocol (picker_port.c), and the
     * ports' view drawn. */
    int      port;
    uint32_t view_rev;
    bool     chrome_valid[MAX_FBS];
} pk_state_t;

/* Defined in picker_screen.c. */
extern pk_state_t pk_s;
#define s pk_s

/* --- picker_screen.c: the state the other two read -------------------- */

void pk_touched(void);
const outbind_shape_t *pk_shape(void);
const link_drawing_t *pk_drawing(void);
uint8_t pk_proto_shown(void);
bool pk_prog_taken(uint8_t index);
outputs_iface_t pk_iface_taken(uint8_t index);
int8_t pk_channel_of(uint8_t index);

/* --- picker_geometry.c ------------------------------------------------- */

void pk_board_box(int *x, int *y, int *w, int *h);
bool pk_pad_xy(uint8_t pad, int *px, int *py);
bool pk_pad_is_top(uint8_t pad);
bool pk_button_at(int i, gfx_rect_t *out, int *tier_out);
bool pk_marker_at(uint8_t e, int *mx, int *my, int *r);
int pk_pop_rows(void);
gfx_rect_t pk_pop_rect(void);

/* --- picker_draw.c ------------------------------------------------------ */

void pk_render(gfx_canvas_t *c, int buffer_index);
