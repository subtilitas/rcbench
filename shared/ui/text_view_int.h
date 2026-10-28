/**
 * @file text_view_int.h
 * @brief The text viewer's private seam between text_view.c, the state,
 *        the scrolling and the touch events, and text_view_draw.c, the
 *        drawing: the layout both work on, the state and the functions one
 *        calls in the other.  Nothing outside shared/ui includes this
 *        header; the viewer's interface is text_view.h.
 *
 * Included last: its layout macros have short names and it names the
 * state tv, which no header included after it may meet.  What one file
 * calls in the other carries the prefix tv_.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fm_io.h"
#include "gfx.h"
#include "text_buf.h"
#include "text_hl.h"
#include "text_view.h"
#include "ui_layout.h"

/** @brief The faces: the 10 x 20 face for the text, the header and the
 *         buttons; the 8 x 16 face for SMALL's text and the wrap mark; the
 *         12 x 24 face for the sentence in place of a text. */
#define TV_FONT_LABEL (&gfx_font_10x20)
#define TV_FONT_FINE  (&gfx_font_8x16)   /**< 8 x 16: SMALL's text. */
#define TV_FONT_BODY  (&gfx_font_12x24)  /**< 12 x 24: a sentence. */

/** @brief The body's columns: UI_MARGIN (8px) in from either edge. */
#define TV_L UI_MARGIN
#define TV_R (UI_PANEL_W - UI_MARGIN)  /**< x 1016. */
#define TV_W (TV_R - TV_L)             /**< 1008px. */

/** @brief The header row, body y 4 to 52. */
#define TV_HDR_Y   4
#define TV_HDR_H   48                  /**< Its height. */
#define TV_HDR_TY  (TV_HDR_Y + (TV_HDR_H - 20) / 2)  /**< Its text's top. */
#define TV_NAME_CELLS 44               /**< The name's cells at most. */
#define TV_PILL_H  32                  /**< READ-ONLY's height. */

/**
 * @brief The text's panel, 1008 x 372px from body y 60; the rows from
 *        TV_TEXT_Y, the wrap mark's column at TV_MARK_X, the text from
 *        TV_TEXT_X; the scroll bar's track TV_BAR_W wide, 4px in from the
 *        panel's right edge.
 */
#define TV_AREA_Y  60
#define TV_AREA_H  372                 /**< The panel's height. */
/** @brief The panel. */
#define TV_AREA    gfx_rect_make(TV_L, TV_AREA_Y, TV_W, TV_AREA_H)
#define TV_TEXT_Y  (TV_AREA_Y + 6)     /**< The first row's top, y 66. */
#define TV_MARK_X  (TV_L + 4)          /**< The wrap mark's left, x 12. */
#define TV_TEXT_X  (TV_L + 24)         /**< The text's left, x 32. */
#define TV_BAR_W   56                  /**< The bar's width: 8.4mm. */
#define TV_BAR_MIN 32                  /**< Its thumb's least height. */
/** @brief The bar's track. */
#define TV_BAR     gfx_rect_make(TV_R - 4 - TV_BAR_W, TV_AREA_Y + 4, \
                                 TV_BAR_W, TV_AREA_H - 8)
/** @brief The text's right edge, 4px short of the bar: x 952. */
#define TV_TEXT_R  (TV_R - 4 - TV_BAR_W - 4)
#define TV_PITCH_NORMAL 20             /**< A row's pitch, 10 x 20 face. */
#define TV_PITCH_SMALL  16             /**< A row's pitch, 8 x 16 face. */
/** @brief The text, the rows and the bar's left edge: what a drag on the
 *         text scrolls. */
#define TV_TEXT_HIT gfx_rect_make(TV_L, TV_AREA_Y, \
                                  TV_W - 4 - TV_BAR_W, TV_AREA_H)

/**
 * @brief The footer, body y 440 to 492, 4px above the alert band: SMALL
 *        at the left, PAGE UP and PAGE DOWN at the right, 140 x 52px, 6px
 *        apart; a warning in the 10 x 20 face between.
 */
#define TV_FOOT_H  52
#define TV_FOOT_Y  (UI_BODY_H - UI_ALERT_H - 4 - TV_FOOT_H)  /**< y 440. */
#define TV_FOOT_W  140                 /**< A button's width. */
#define TV_FOOT_PAD 6                  /**< Room kept at each side of a label. */
/** @brief SMALL. */
#define TV_SMALL   gfx_rect_make(TV_L, TV_FOOT_Y, TV_FOOT_W, TV_FOOT_H)
/** @brief PAGE UP. */
#define TV_PAGE_UP gfx_rect_make(TV_R - 2 * TV_FOOT_W - 6, TV_FOOT_Y, \
                                 TV_FOOT_W, TV_FOOT_H)
/** @brief PAGE DOWN. */
#define TV_PAGE_DN gfx_rect_make(TV_R - TV_FOOT_W, TV_FOOT_Y, TV_FOOT_W, \
                                 TV_FOOT_H)
/** @brief The warning's box between SMALL and PAGE UP. */
#define TV_WARN    gfx_rect_make(TV_L + TV_FOOT_W + 16, TV_FOOT_Y, \
                                 TV_W - 3 * TV_FOOT_W - 6 - 32, TV_FOOT_H)

/** @brief A press that moves further than this, px, in x or in y, is a
 *         drag, not a tap: FILES' slop. */
#define TV_SLOP 8

/** @brief Spans a drawn line is classed into; a line with more has its
 *         last span reach to its end as TEXT (text_hl_line()). */
#define TV_SPANS 128

_Static_assert(TV_TEXT_X + TEXT_VIEW_CELLS_NORMAL * 10 <= TV_TEXT_R,
               "a row of the 10 x 20 face ends short of the bar");
_Static_assert(TV_TEXT_X + TEXT_VIEW_CELLS_SMALL * 8 <= TV_TEXT_R,
               "a row of the 8 x 16 face ends short of the bar");
_Static_assert(TV_TEXT_Y + TEXT_VIEW_ROWS_NORMAL * TV_PITCH_NORMAL
                   <= TV_AREA_Y + TV_AREA_H - 4,
               "the rows of the 10 x 20 face end in the panel");
_Static_assert(TV_TEXT_Y + TEXT_VIEW_ROWS_SMALL * TV_PITCH_SMALL
                   <= TV_AREA_Y + TV_AREA_H - 4,
               "the rows of the 8 x 16 face end in the panel");
_Static_assert(TV_AREA_Y + TV_AREA_H + 8 <= TV_FOOT_Y,
               "the panel ends 8px above the footer");
_Static_assert(TV_HDR_Y + TV_HDR_H + 8 <= TV_AREA_Y,
               "the header ends 8px above the panel");
_Static_assert(TV_FOOT_H >= UI_TAP_MIN && TV_BAR_W >= UI_TAP_MIN,
               "the buttons and the bar are a finger wide");
_Static_assert(TV_FOOT_Y + TV_FOOT_H + 4 <= UI_BODY_H - UI_ALERT_H,
               "the footer ends 4px above the alert band");
_Static_assert(TV_MARK_X + 2 * 8 <= TV_TEXT_X - 4,
               "the wrap mark ends 4px before the text");

/** @brief What a press came down on. */
typedef enum {
    TV_HIT_NONE = -1,
    TV_HIT_SMALL = 0,   /**< SMALL or NORMAL     */
    TV_HIT_PAGE_UP,     /**< PAGE UP             */
    TV_HIT_PAGE_DN,     /**< PAGE DOWN           */
    TV_HIT_TEXT,        /**< the text: a drag    */
    TV_HIT_BAR          /**< the scroll bar      */
} tv_hit_t;

/** @brief The viewer's state. */
typedef struct {
    text_view_state_t state;  /**< where it stands                       */
    fm_err_t err;             /**< FAILED: the card's answer             */
    text_buf_err_t tb_err;    /**< FAILED: the text's refusal            */
    char dir[FM_PATH_SIZE];   /**< the file's folder                     */
    char name[FM_NAME_BUF];   /**< its name                              */
    uint64_t size;            /**< its bytes as listed                   */
    text_hl_kind_t kind;      /**< how its lines are classed             */
    bool small;               /**< SMALL: the 8 x 16 face                */
    size_t top_line;          /**< the top row's line                    */
    size_t top_row;           /**< and its row in that line              */

    /* The contact that holds a press, where it came down, and what on. */
    bool tap_have;            /**< a press is held                       */
    bool tap_moved;           /**< it went past the slop                 */
    uint8_t tap_id;           /**< its contact                           */
    int16_t tap_x0;           /**< where it came down                    */
    int16_t tap_y0;           /**< where it came down                    */
    int press;                /**< a tv_hit_t                            */
    size_t press_line;        /**< the top line at the press             */
    size_t press_row;         /**< and its row                           */
    int bar_grab;             /**< px below the thumb's top it was taken */
} tv_state_t;

/** @brief The viewer's state, defined in text_view.c. */
extern tv_state_t tv_s;
#define tv tv_s   /**< The state by its short name. */

/** @brief The text, defined in text_view.c over its static buffers. */
extern text_buf_t tv_tb;

/* --- text_view.c -------------------------------------------------------- */

/**
 * @brief The pitch of a row in the face in use.
 * @return TV_PITCH_NORMAL or TV_PITCH_SMALL
 */
int tv_pitch(void);

/**
 * @brief The face of the text in use.
 * @return TV_FONT_LABEL or TV_FONT_FINE
 */
const gfx_font_t *tv_face(void);

/**
 * @brief The last top position: the line and row that put the text's last
 *        row at the foot of the screen; (0, 0) when the text fits.
 * @param row  takes the row in the line
 * @return the line
 */
size_t tv_max_top(size_t *row);

/**
 * @brief The scroll bar's thumb.
 * @param y  takes its top
 * @param h  takes its height
 */
void tv_bar_thumb(int *y, int *h);

/* --- text_view_draw.c --------------------------------------------------- */

/**
 * @brief Walk the rows on screen from the top row, cell by cell, as they
 *        are drawn.  For each run of cells of one class on one row,
 *        @p run is called with the row on screen, the run's first cell,
 *        its bytes (NUL-terminated) and its class.  A row that a line
 *        continues is called once more with @p cell -1 and no bytes: the
 *        wrap mark.
 * @param run  the call, or NULL to walk only
 * @param ctx  passed to @p run
 * @return the rows on screen that hold text
 */
int tv_walk(void (*run)(void *ctx, int row, int cell, const char *bytes,
                        int cls),
            void *ctx);
