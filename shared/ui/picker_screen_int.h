/**
 * @file picker_screen_int.h
 * @brief OUTPUTS' board view's private seam between picker_screen.c, the state, the
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
#include "outputs_screen_int.h"
#include "picker_screen.h"
#include "ui_cats.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_widgets.h"

#define SCREEN_W UI_PANEL_W   /**< The panel's width, px. */
#define SCREEN_H UI_BODY_H    /**< The body under the band, px. */
#define MAX_FBS  3     /**< Framebuffers the chrome is cached for. */

/**
 * @brief The lowest row the screen draws or takes a press on, in the
 *        screen's coordinates: 4px above the alert band, body y 492.
 */
#define FOOT (UI_ALERT_Y - UI_BAND_H - 4)

/**
 * @brief The faces of the 1024 x 600 type scale the board view draws in.
 *
 * PK_FONT_LABEL (10 x 20) for the count, the reason, the PAD head, a
 * channel, a pad number, a name in IN USE and a pin there in the 20px
 * rows; PK_FONT_FINE (8 x 16) for a rail's voltage, BY OTHERS and the
 * 16px rows of IN USE; PK_FONT_PIN (16 x 28) for a ground's G and a pin
 * in IN USE's 28px rows and a pin name under PINS; PK_FONT_HEAD (20 x
 * 36) for PINS, IN USE and a key's GPIO number.
 */
#define PK_FONT_LABEL (&gfx_font_10x20)
#define PK_FONT_FINE  (&gfx_font_8x16)    /**< 8 x 16: rails, 16px rows. */
#define PK_FONT_PIN   (&gfx_font_16x28)   /**< 16 x 28: IN USE's pins. */
#define PK_FONT_HEAD  (&gfx_font_20x36)   /**< 20 x 36: heads, key numbers. */
#define PK_CELL       10 /**< PK_FONT_LABEL's cell width, px. */

/**
 * @brief The board's box: 600px wide and as tall as its outline makes
 *        it, up to 240px, centred between the body's top and FOOT.
 *
 * 240px leaves 126px above and below for two tiers of 56px keys with
 * their gaps.  The RP2350-CAN's outline, 51 x 21mm, draws 582 x 240px,
 * a 0.1 in pitch at 29px.
 */
#define BOARD_W  600
#define BOARD_MAX_H 240   /**< The box's tallest, px. */

/**
 * @brief Two staggered tiers of keys above the board and two below.
 *
 * A key is 50 x 56px (7.5 x 8.4mm).  The tier alternates along a pad
 * row, so two keys in one tier stand two pads apart at least: 58px at
 * the RP2350-CAN's pitch, 8px between them.  TGAP is between the two
 * tiers, GAP between the board's edge and the nearer tier.
 */
#define TIERS 2               /**< Tiers above and below. */
#define TGAP  6               /**< Between the two tiers, px. */
#define BTNW  50              /**< A key's width, px. */
#define KEY_H UI_BTN_H_TALL   /**< A key's height, px. */
#define GAP   6         /**< Board edge to the nearer tier, px. */

_Static_assert((FOOT - BOARD_MAX_H) / 2 >= TIERS * KEY_H + TGAP + GAP,
               "two tiers of keys fit above and below the tallest board");
_Static_assert(BTNW >= UI_TAP_MIN && KEY_H >= UI_TAP_MIN,
               "a key is a finger wide and tall");

/*
 * The marks inside the board.  A ground is a round marker 36px across, 40
 * px in from its pad: a servo lead needs a ground as much as a signal, so it
 * is as legible as a button, and grounds are five pads apart, so it has
 * nothing to collide with.  A rail is 32px across, enough for "5V0", at one
 * of two depths that alternate along the row: rails sit on neighbouring
 * pads, 29px apart, and a rail beside a ground goes below the ground's
 * marker -- 78px is the ground's 40 plus both radii and both rings.
 */
#define GND_R       18                  /**< radius of a ground marker, px */
#define GND_DROP    40                  /**< distance of a ground marker from its pad, px */
#define RAIL_R      16                  /**< radius of a rail marker, px */
/** @brief Distance of a rail marker from its pad at the first depth, px */
#define RAIL_DROP_0 30
#define RAIL_DROP_1 78                  /**< distance at the second depth, px */
#define RING        2                   /**< width of the dark ring around a marker, px */

/**
 * @brief Over the PINS table, left of the first key above the board: the
 *        protocol list's face, the count, and the reason no pin can be
 *        added.
 *
 * The face is 280 x 56px: the longest protocol name, "DSHOT600 BIDIR",
 * draws in the 16 x 28 face there (224px of the 225px left of its
 * chevron), every other name in 20 x 36.  The reason takes a second line
 * where it needs one: the first holds 35 cells (350px), beside the keys
 * above the board; the second, beside the board itself, what is left of
 * it, 17 cells at most (test_picker_screen_table holds every reason to
 * it), so it ends at x 178, left of the widest board's edge at x 212.
 */
#define HEAD_X      8         /**< The head's left edge. */
#define HEAD_Y      8         /**< Its top. */
#define HEAD_W      350       /**< The reason's first line: 35 cells. */
#define FACE_W      280       /**< The list's face, px. */
#define FACE_H      56        /**< Its height, px. */
#define COUNT_Y     (HEAD_Y + FACE_H + 8) /**< The count's line. */
#define REASON_Y    (COUNT_Y + 24) /**< The reason's first line. */
#define REASON_PITCH 22   /**< The reason's second line under its first. */
/** @brief The head's height, px. */
#define HEAD_H      (REASON_Y + REASON_PITCH + 20 - HEAD_Y)

/*
 * The open list covers the board, as it covers the pins on OUTPUTS: the
 * protocols in a column of 48px rows from the face's top, the PROG PORT
 * entries in a second column beside it.  With its margins and shadow it
 * ends at body y 404.
 */
#define POP_W       300       /**< A column's width, px. */
#define POP_ROW     UI_TAP_MIN /**< A row, px. */

_Static_assert(HEAD_Y + (int)OUTBIND_PROTOS * POP_ROW + 8 + 4 <= FOOT,
               "the protocol list ends above the alert band");

/**
 * @brief The PINS table, left of the board: 171px, "CH7" in the label
 *        face (30px), "GPIO 28" in PK_FONT_PIN from PINS_NAME_X in (112px,
 *        to 146px), and the pad number right-aligned (20px, from 151px).
 *
 * It starts under the reason's second line; its 8 rows of 36px, the
 * most a protocol holds, end at body y 484.  A row is read, not tapped.
 * It ends at x 179, 8px left of the leftmost a key can stand: half a key
 * left of the widest board's edge (x 212), where a board that reports its
 * own shape may put its first pad.
 */
#define PINS_X      8         /**< The table's left edge. */
#define PINS_Y      (HEAD_Y + HEAD_H + 10) /**< Its heading's top. */
#define PINS_W      171       /**< Its width, px. */
#define PINS_NAME_X 34        /**< A pin name's left edge in it, px. */
#define PINS_TOP    (PINS_Y + 48)   /**< The first row's top. */
#define ROW_H       36        /**< A row, px. */

_Static_assert(PINS_TOP + (int)LINK_OUT_SLOTS * ROW_H <= FOOT,
               "the PINS table holds 8 pins above the alert band");
_Static_assert(PINS_NAME_X + 7 * 16 + 4 <= PINS_W - 2 * PK_CELL,
               "a pin name (GPIO 28) ends 4px left of a two-digit pad");
_Static_assert(PINS_X + PINS_W + 8 <= (SCREEN_W - BOARD_W) / 2 - BTNW / 2,
               "the PINS table ends 8px left of any key");

/**
 * @brief The IN USE table, right of the board: 168px from x 848, 8px
 *        right of the rightmost a key can stand (x 837).
 *
 * Its lines start under the heading, the rule and the BY OTHERS line and
 * end at FOOT.  A name is 20px; a pin is 28px with the GPIO in
 * PK_FONT_PIN, or 20px with it in the label face when the 28px rows run
 * past the foot, and 16px in 8 x 16 when the 20px rows do.
 */
#define USE_W       168       /**< Its width, px. */
#define USE_X       (SCREEN_W - 8 - USE_W) /**< The table's left edge. */
#define USE_Y       4         /**< Its heading's top. */
#define USE_TOP     (USE_Y + 56) /**< Its first line. */
#define USE_FOOT    FOOT      /**< Where its lines end. */
#define USE_NAME_H  20        /**< A name's line, px. */
#define USE_PIN_H   28        /**< A pin's line, px. */
#define USE_PIN_H_C 20        /**< A pin's line past the foot at 28. */
#define USE_PIN_H_D 16        /**< And where 20px does. */

_Static_assert(USE_X >= (SCREEN_W + BOARD_W) / 2 + BTNW / 2 + 8,
               "the IN USE table starts 8px right of any key");

/* With no interface on, every line fits at the 16px pin rows: every port,
 * every protocol's name and every slot's pin. */
_Static_assert(USE_TOP
                   + (1 + (int)OUTBIND_PROTOS - 1) * USE_NAME_H
                   + ((int)LINK_PG_PORTS_MAX + (int)LINK_OUT_SLOTS)
                         * USE_PIN_H_D
                   <= USE_FOOT,
               "the IN USE table holds a binding beside 4 ports");
/* And with every interface on as well, at 16px a line: 27 lines.  PPM's
 * one pin takes all 8 slots, so at most 6 protocols share them, one name
 * fewer than PICKER_USE_MAX counts. */
_Static_assert(USE_TOP + ((int)PICKER_USE_MAX - 1) * USE_PIN_H_D
                   <= USE_FOOT,
               "the IN USE table holds its fullest list");

/**
 * @brief LAST WRITE, right of the list's face and left of the first key of
 *        the upper tier on the RP2350-CAN (x 444): 140 x 56px.  The label in
 *        8 x 16 on one line or two, the result in the largest face of 12 x
 *        24, 10 x 20 and 8 x 16 that holds it on one line: "NICHT
 *        GESCHRIEBEN" is 17 cells of 8 x 16.  Both cat spots stay free.
 *
 * A board with a key there gets it at the foot of the IN USE column instead,
 * above UNBIND ALL PINS while that is offered (pk_last_rect()).
 */
#define LAST_X (HEAD_X + FACE_W + 8)
#define LAST_Y 4              /**< Its top. */
#define LAST_W 140            /**< Its width, px. */
#define LAST_H 56             /**< Its height, px. */

_Static_assert(OB_COL_X == PINS_X && OB_COL_W == PINS_W,
               "the PROG PORT entry's column is the PINS table's");
_Static_assert(OB_CLR_X == USE_X && OB_CLR_W == USE_W,
               "UNBIND ALL PINS stands in the IN USE column");
_Static_assert(OB_FN_X + OB_FN_W == HEAD_X + FACE_W && OB_LIST_W == FACE_W,
               "FUNCTION's face ends where the list's does");
_Static_assert(OB_FN_Y >= HEAD_Y + FACE_H, "FUNCTION's face is under the list's");

#define IVORY GFX_RGB(236, 235, 228)    /**< board colour of the ivory (white) key */
#define EBONY GFX_RGB(22, 23, 28)       /**< board colour of the ebony (black) key */
#define INK   GFX_RGB(10, 10, 14)       /**< text colour on a marker */
#define HALO  GFX_RGB(6, 6, 8)          /**< halo colour around a marker's text */

enum { HIT_NONE = 0, HIT_FACE, HIT_ROW, HIT_BUTTON };

/**
 * @brief The board view's state: the binding shown, the callbacks that apply
 *        and select, the IOmcu's drawing of the board, the press, the open
 *        protocol list, the pins of each interface and the cat.
 */
typedef struct {
    outbind_t         bind;             /**< the binding shown */
    picker_apply_fn   apply;            /**< called to apply a change of the binding */
    picker_select_fn  select;           /**< called to select a pin */
    picker_drawing_fn source;           /**< called to fetch the board's drawing */
    /* The IOmcu's drawing, for the board drawing_board names; it
     * agreed with that board's catalogue when it was set. */
    /**
     * @brief The IOmcu's drawing, for the board drawing_board names; it agreed
     *        with that board's catalogue when it was set
     */
    link_drawing_t    drawing;
    uint16_t          drawing_board;    /**< board the drawing is for */
    bool              has_drawing;      /**< whether drawing holds a drawing */

    int      hit_kind;                  /**< what the press is on, HIT_* */
    int      hit;            /**< row or catalogue index under the finger    */
    bool     open;           /**< the protocol list is open                  */
    /* The binding shown is not one a read confirmed: no button acts. */
    /**
     * @brief Whether the binding shown is not one a read confirmed: no button
     *        acts
     */
    bool     unread;
    /* The pin whose tap a PWM clash refused last, or -1: the reason line
     * says why, until another pick or tick. */
    /**
     * @brief Pin whose tap a PWM clash refused last, -1 for none; the reason
     *        line says why
     */
    int      refused;
    /* Each interface's pins at the last frame. */
    uint64_t iface[OUTPUTS_IFACE_COUNT];  /**< each interface's pins at the last frame */
    /* The PROG PORT picked, -1 for a protocol (picker_port.c), and the
     * ports' view drawn. */
    int      port;                      /**< PROG PORT picked, -1 for a protocol */
    uint32_t view_rev;                  /**< revision of the ports' view as drawn */
    uint32_t out_rev;                   /**< outputs_screen_revision() as drawn */
    bool     chrome_valid[MAX_FBS];     /**< per frame buffer: whether its chrome is current */
    /* The cat (picker_cat.c): picked on the first frame of a visit from
     * the roll entering took, and drawn while its box stays clear. */
    bool          cat_pending; /**< the pick is still to make           */
    uint32_t      cat_roll;    /**< the roll entering took               */
    ui_cat_pick_t cat;         /**< the pose and spot picked             */
    gfx_rect_t    cat_drawn;  /**< the box drawn last, empty for none */
    int           cat_view;   /**< the selection the pick was last held to */
} pk_state_t;

/* Defined in picker_screen.c. */
extern pk_state_t pk_s;
/** @brief Short name for pk_s inside the picker's files. */
#define s pk_s

/* --- picker_screen.c: the state the other two read -------------------- */

void pk_touched(void);
const outbind_shape_t *pk_shape(void);
const link_drawing_t *pk_drawing(void);
uint8_t pk_proto_shown(void);
bool pk_prog_taken(uint8_t index);
outputs_iface_t pk_iface_taken(uint8_t index);
int8_t pk_channel_of(uint8_t index);
/**
 * @brief Where LAST WRITE stands: right of the list's face, or at the foot of
 *        the IN USE column on a board with a key there.
 * @return the box
 */
gfx_rect_t pk_last_rect(void);
/**
 * @brief Whether LAST WRITE is drawn: on a protocol's view, not a PROG PORT
 *        entry's, whose PIN and FUNCTION asks report on their own line.
 * @return true when it is drawn
 */
bool pk_last_shown(void);
/**
 * @brief Where the IN USE table's lines end: at the foot, or 8px above UNBIND
 *        ALL PINS and above LAST WRITE when they stand in its column.
 * @return the row, px
 */
int pk_use_foot(void);

/* --- picker_geometry.c ------------------------------------------------- */

/**
 * @brief Gives the box the board occupies: the drawing's outline or else the
 *        shape's, centred across the screen between the body's top and FOOT.
 * @param x takes the left edge, px
 * @param y takes the top edge, px
 * @param w takes the width, px
 * @param h takes the height, px
 */
void pk_board_box(int *x, int *y, int *w, int *h);
/**
 * @brief Gives where a pad sits on screen, from the drawing's pad record or
 *        else the shape's grid.
 * @param pad the pad
 * @param px takes the x of the pad's ring centre, px
 * @param py takes the y of the pad's ring centre, px
 * @return true when the pad is on the board
 */
bool pk_pad_xy(uint8_t pad, int *px, int *py);
/**
 * @brief Returns which of the two pad rows a pad is in.
 * @param pad the pad
 * @return true for the upper row
 */
bool pk_pad_is_top(uint8_t pad);
/**
 * @brief Gives the button of catalogue entry @p i and its tier; only a pin an
 *        output may have gets one.
 * @param i catalogue entry
 * @param out takes the button's rectangle
 * @param tier_out takes the tier, 0 for the row nearer the board
 * @return true when the entry has a button
 */
bool pk_button_at(int i, gfx_rect_t *out, int *tier_out);
/**
 * @brief Gives the centre and radius of the marker of pad-list entry @p e.
 * @param e pad-list entry
 * @param mx takes the centre x, px
 * @param my takes the centre y, px
 * @param r takes the radius, px
 * @return true when the pad has a marker
 */
bool pk_marker_at(uint8_t e, int *mx, int *my, int *r);
/**
 * @brief Returns the rows of the protocol list: the protocols, then one PROG
 *        PORT entry per port.
 * @return the row count
 */
int pk_pop_rows(void);
/**
 * @brief Returns the protocol list's rectangle: one column of POP_W px, two
 *        with PROG PORT entries.
 * @return the rectangle
 */
gfx_rect_t pk_pop_rect(void);

/* --- picker_draw.c ------------------------------------------------------ */

/**
 * @brief Draws the screen into frame buffer @p buffer_index.
 * @param c canvas drawn on
 * @param buffer_index the frame buffer
 */
void pk_render(gfx_canvas_t *c, int buffer_index);

/* --- picker_cat.c ------------------------------------------------------- */

/** @brief A visit begins: roll for the cat, picked on its first frame. */
void pk_cat_enter(void);
/** @brief Forget the cat: none until the next visit. */
void pk_cat_reset(void);
/**
 * @brief Pick the cat on a visit's first frame and draw it while its box
 *        is clear; called last in a frame that draws the static parts.
 * @param c the screen's canvas
 */
void pk_cat_render(gfx_canvas_t *c);
