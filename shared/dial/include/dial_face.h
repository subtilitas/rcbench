/**
 * @file
 * @brief The M5Stack Dial's faces: what its 240 x 240 round display shows,
 *        drawn from the panel's model (dk_face_t) and the Dial's own state
 *        into an RGB565 canvas with the bench's rasteriser, fonts and dark
 *        palette.
 *
 * Every face carries the same furniture: the panel screen's name on the
 * chamfered title tag at y 42, the state pill SAFE or ARMED (with the run
 * clock) at y 66, the link bars to its left and the battery to its right,
 * each 6px clear of the pill, and two press hints at y 181 and 195.  The
 * reading faces (MOTOR & ESC, SERVO) show six readings in a grid of equal
 * cells, three rows of two, and a stack of six antialiased 1px rings near
 * the bezel, one per reading, from radius 118 inward at a pitch of 3px,
 * each from 12 o'clock clockwise to the reading's share of its full scale
 * over a full-circle track of its colour mixed 22% onto the background.
 *
 * Every string's ink lies inside radius 100 of the display's center
 * (DIAL_R_TEXT): a line is drawn in the largest font of its chain in which
 * it fits there at its height, wrapped onto its block's lines, and cut as
 * the last resort.  Every cell lies inside radius 100.
 *
 * dial_render() draws only what changed since the frame before: a cell,
 * the pill's row, the body, the hints, and of a ring only the pixels
 * between its old and its new end angle.  The canvas then holds exactly
 * what a full draw of the same model would hold (test_dial_face), and a
 * damage record attached to the canvas (gfx_damage.h) names what to send.
 *
 * Pure C: no clock read, no I/O, no heap.  M5Stack is the name of its
 * owner, used only to say which device this draws for.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dialknob.h"
#include "gfx.h"
#include "gfx_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The display's width and height, pixels. */
#define DIAL_FACE_SIZE 240
/** @brief The display's center in half pixels: between rows and columns
 *         119 and 120. */
#define DIAL_FACE_C2 239
/** @brief Every string's ink lies inside this radius, pixels. */
#define DIAL_R_TEXT 100
/** @brief Shapes (the title tag, the pill, the icons) lie inside this
 *         radius, clear of the innermost ring's coverage, pixels. */
#define DIAL_R_SHAPE 101
/** @brief A repaint clears only pixels inside this radius, so it never
 *         touches a ring, a rim tick or a rim decoration, pixels. */
#define DIAL_R_CLEAR 102
/** @brief The ring stack: rings, the outermost radius and the pitch
 *         inward, pixels. @{ */
#define DIAL_RINGS      6
#define DIAL_RING_R0    118 /**< the first reading's ring */
#define DIAL_RING_PITCH 3   /**< from one ring to the next inward */
/** @} */
/** @brief Table entries of all six rings (gfx_ring.h). */
#define DIAL_RING_PX (13 * (6 * DIAL_RING_R0 - 15 * DIAL_RING_PITCH) + 16 * 6)
/** @brief The sort scratch dial_render_init() needs, words: the largest
 *         ring's table. */
#define DIAL_RING_KEYS GFX_RING_CAP(DIAL_RING_R0)
/** @brief The reading faces' cells. @{ */
#define DIAL_CELLS 6
#define DIAL_CELL_W 78  /**< a cell's width, pixels */
#define DIAL_CELL_H 31  /**< a cell's height: the label's cap top to the
                             unit's descender, pixels */
/** @} */
/** @brief The full scale of a temperature ring, °C. */
#define DIAL_TEMP_FS_C 120

/** @brief What the Dial knows besides the panel's model. */
typedef struct {
    bool        lost;    /**< no panel packet for DK_DIAL_LOST_MS */
    bool        ready;   /**< connected and subscribed */
    bool        bound;   /**< a panel is bound */
    const char *name;    /**< the Dial's name, "DIAL-3A10"; NULL "" */
    const char *addr;    /**< its address as text; NULL "" */
    int8_t      rssi;    /**< the link's RSSI, dBm; DK_RSSI_NONE */
    uint16_t    batt_mv; /**< the cell's voltage, mV; 0 unknown */
    bool        usb;     /**< a USB host reads the Dial's port */
} dial_face_view_t;

/** @brief The faces. */
typedef enum {
    DIAL_FACE_WAITING = 1, /**< bound, no panel packet on this link */
    DIAL_FACE_BIND,        /**< no bound panel */
    DIAL_FACE_LOST,        /**< the panel's stream stopped: LINK LOST */
    DIAL_FACE_NAV,         /**< a list */
    DIAL_FACE_VALUE,       /**< a value being set */
    DIAL_FACE_READING,     /**< MOTOR & ESC or SERVO: six readings */
    DIAL_FACE_OTHER,       /**< a screen the knob does nothing on, HOME */
} dial_face_id_t;

/** @brief Kinds of body block. @{ */
#define DIAL_B_TEXT   0u /**< centered lines */
#define DIAL_B_FOCUS  1u /**< a line in the accent's corner brackets */
#define DIAL_B_NUMBER 2u /**< a numeral with its unit beside it */
/** @} */

/** @brief The longest text of a block or a cell, bytes with the NUL. */
#define DIAL_TXT 48

/** @brief A block of centered text: one line or a few. */
typedef struct {
    char    text[DIAL_TXT]; /**< the text; "" draws nothing */
    char    unit[8];        /**< DIAL_B_NUMBER: the unit, "" none */
    int16_t y;              /**< the block's middle row */
    uint8_t font;           /**< the chain's first font, DIAL_FONT_* */
    uint8_t color;          /**< ui_color_id_t */
    uint8_t lines;          /**< the most lines it wraps onto, 1 to 3 */
    uint8_t kind;           /**< DIAL_B_* */
} dial_block_t;

/** @brief Font chains: a block's first font; the chain runs down to the
 *         8x16 face. @{ */
#define DIAL_FONT_FINE  0u /**< 8x16 */
#define DIAL_FONT_SMALL 1u /**< 10x20 */
#define DIAL_FONT_BODY  2u /**< 12x24 */
#define DIAL_FONT_MID   3u /**< 16x28 */
#define DIAL_FONT_BIG   4u /**< 20x36 */
#define DIAL_FONT_NUM   5u /**< 32x44 numerals, then 24x30, then 16x28 */
/** @} */

/** @brief A reading's cell. */
typedef struct {
    char    label[16]; /**< the reading's name */
    char    value[16]; /**< its value, "--" without its valid bit */
    char    unit[8];   /**< its unit, "" with "--" */
    uint8_t color;     /**< ui_color_id_t: the reading's type */
    bool    used;      /**< the cell is drawn */
} dial_cell_t;

/** @brief A ring of the stack. */
typedef struct {
    uint32_t end;   /**< the arc's end, 0 to GFX_RING_TURN */
    uint8_t  color; /**< ui_color_id_t */
    bool     used;  /**< the ring is drawn */
} dial_ring_t;

/** @brief Rim decorations, drawn on a full draw only. @{ */
#define DIAL_DECO_NONE   0u /**< none */
#define DIAL_DECO_DASHED 1u /**< 24 DANGER dashes: LINK LOST */
#define DIAL_DECO_BIND   2u /**< an EDGE ring and a GRID ring: bind */
#define DIAL_DECO_WAIT   3u /**< a GRID ring: waiting */
/** @} */

/** @brief The state pill. @{ */
#define DIAL_PILL_NONE  0u /**< none: a list, a value, the link faces */
#define DIAL_PILL_SAFE  1u /**< SAFE */
#define DIAL_PILL_ARMED 2u /**< ARMED, with the run clock on MOTOR & ESC */
/** @} */

/** @brief The most body blocks a face has. */
#define DIAL_BLOCKS 6
/** @brief The most rim ticks a list shows; a longer one shows a ring. */
#define DIAL_TICKS_MAX 24

/** @brief Everything a face shows, built from the model and the view:
 *         two scenes that compare equal draw the same pixels. */
typedef struct {
    uint8_t      face;                    /**< dial_face_id_t */
    uint8_t      lang;                    /**< DK_LANG_* */
    uint8_t      deco;                    /**< DIAL_DECO_* */
    bool         has_title;               /**< the title tag is drawn */
    bool         title_dim;               /**< dimmed: the link is lost */
    char         title[DK_TITLE_MAX + 1]; /**< the tag's text */
    uint8_t      pill;                    /**< DIAL_PILL_* */
    char         pill_text[16];           /**< "ARMED 0:42" */
    uint8_t      bars;                    /**< link bars lit, 0 to 4 */
    uint8_t      bars_color;              /**< ui_color_id_t of a lit bar */
    bool         icons_dim;               /**< the bars and battery dim */
    bool         batt_known;              /**< batt_pct is a reading */
    uint8_t      batt_pct;                /**< 0 to 100 */
    bool         usb;                     /**< USB in the battery's place */
    bool         dim;                     /**< readings at half strength */
    dial_cell_t  cell[DIAL_CELLS];        /**< the reading faces' grid */
    dial_block_t block[DIAL_BLOCKS];      /**< the other faces' body */
    dial_block_t hint[2];                 /**< the press hints */
    dial_ring_t  ring[DIAL_RINGS];        /**< by radius, outermost first */
    uint8_t      ticks;                   /**< rim ticks, 0 none */
    uint8_t      tick_cur;                /**< the accent's tick */
} dial_scene_t;

/** @brief A string as drawn, for a test. */
typedef struct {
    const char       *s;    /**< the text */
    const gfx_font_t *font; /**< the font */
    int               x;    /**< its cells' left edge */
    int               y;    /**< their top row */
    int               cell; /**< the grid cell it belongs to, or -1 */
} dial_seen_t;

/** @brief Told of every string drawn. @param ctx the caller's
 *         @param t the string */
typedef void (*dial_seen_fn)(void *ctx, const dial_seen_t *t);

/** @brief The renderer: the ring tables and what the canvas shows. */
typedef struct {
    gfx_ring_t    ring[DIAL_RINGS];  /**< the rings' tables */
    gfx_ring_px_t px[DIAL_RING_PX];  /**< their pixels */
    dial_scene_t  shown;             /**< what the canvas holds */
    bool          valid;             /**< shown is on the canvas */
    uint8_t       lang;              /**< the last language the panel gave */
    dial_seen_fn  seen;              /**< told of each string, or NULL */
    void         *seen_ctx;          /**< seen's argument */
} dial_render_t;

/**
 * @brief Build the ring tables; the next dial_render() draws in full.
 * @param r    the renderer
 * @param keys DIAL_RING_KEYS words of scratch, free again on return
 * @return true when every table was built
 */
bool dial_render_init(dial_render_t *r, uint32_t *keys);

/** @brief Draw in full next time: the canvas no longer holds the face.
 *  @param r the renderer */
void dial_render_invalidate(dial_render_t *r);

/**
 * @brief Make the canvas show the face for the model and the view,
 *        drawing only what changed since the last call.
 * @param r the renderer
 * @param c the canvas, DIAL_FACE_SIZE square
 * @param f the panel's model
 * @param v the Dial's state
 * @return true when a pixel may have changed
 */
bool dial_render(dial_render_t *r, gfx_canvas_t *c, const dk_face_t *f,
                 const dial_face_view_t *v);

/**
 * @brief Build the scene a model and a view show.
 * @param s         the scene, cleared first
 * @param f         the panel's model
 * @param v         the Dial's state
 * @param lang_last the language when the model carries none, DK_LANG_*
 */
void dial_scene_build(dial_scene_t *s, const dk_face_t *f,
                      const dial_face_view_t *v, uint8_t lang_last);

/**
 * @brief Where grid cell @p i lies: row by row, left to right.
 * @param i 0 to DIAL_CELLS - 1
 * @return the cell; empty for another @p i
 */
gfx_rect_t dial_cell_rect(int i);

/**
 * @brief A ring's radius.
 * @param i 0 (the outermost) to DIAL_RINGS - 1
 * @return pixels
 */
int dial_ring_radius(int i);

/**
 * @brief The run clock as the pill shows it: "m:ss" below 1h, "XhYY"
 *        from it ("1h05").
 * @param s   the clock, seconds
 * @param out the text
 * @param cap its bytes, 8 or more
 */
void dial_clock_text(uint16_t s, char *out, unsigned cap);

/**
 * @brief The ink of a string drawn with its cells' top-left corner at
 *        (@p x, @p y): the box around its glyphs' pixels of any coverage.
 * @param font the font
 * @param s    the text
 * @param x    the left edge of its first cell
 * @param y    the top row of its cells
 * @param out  the box; empty when the string shows nothing
 * @return true when the string shows a pixel
 */
bool dial_ink(const gfx_font_t *font, const char *s, int x, int y,
              gfx_rect_t *out);

/**
 * @brief Whether a box lies inside a circle about the display's center:
 *        every corner pixel's center within @p r.
 * @param b the box
 * @param r the radius, pixels
 * @return true when it does; true for an empty box
 */
bool dial_box_inside(gfx_rect_t b, int r);

#ifdef __cplusplus
}
#endif
