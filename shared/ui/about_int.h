/**
 * @file
 * @brief ABOUT's state, geometry and column, shared by about_screen.c (the
 *        state, the touches and the screen's table) and about_draw.c (the
 *        column and the drawing).  Private to those two files.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "about_gen.h"
#include "about_screen.h"
#include "gfx.h"
#include "ui_footer.h"
#include "ui_layout.h"

/*
 * The body below the band, 1024 x 540px.  The column from x 16, 936px wide,
 * 17 rows of 24px from y 8 to 416; the scroll bar 56 x 408px at x 960, 8px
 * in from the right edge; the footer's three slots (ui_footer.h) at y 444:
 * BACK, PAGE UP, PAGE DOWN.
 */
#define AB_TOP    8     /**< the first row's top, px */
#define AB_PITCH  24    /**< a row's height, px */
#define AB_H      (ABOUT_ROWS * AB_PITCH)  /**< the column's height: 408px */
#define AB_BAR_W  56    /**< the bar's width: 8.4mm */
/** @brief The bar's left edge, px: 960. */
#define AB_BAR_X  (UI_PANEL_W - UI_MARGIN - AB_BAR_W)
#define AB_X      16    /**< the rows' left edge, px */
#define AB_W      (AB_BAR_X - 8 - AB_X)    /**< the rows' width: 936px */
#define AB_BAR_MIN 32   /**< the thumb's least height, px */
#define AB_SLOP   8     /**< a press that moves further is a drag, px */
/** @brief The most parts the column is built of (ab_seg_t). */
#define AB_SEGS   40

/** @brief A section heading's face: 12 x 24, the row's full height. */
#define AB_FONT_HEAD (&gfx_font_12x24)
/** @brief Every other row's face: 10 x 20. */
#define AB_FONT_ROW  (&gfx_font_10x20)

_Static_assert(ABOUT_GEN_CELLS * 10 <= AB_W,
               "a generated row fits the column in the 10 x 20 face");
_Static_assert(AB_TOP + AB_H <= UI_FOOTER_FLOOR,
               "the column ends 8px over the footer");

/** @brief What a press landed on. */
enum {
    AB_HIT_NONE = -1, /**< nothing that takes a press */
    AB_HIT_TEXT = 0,  /**< the rows: a drag scrolls   */
    AB_HIT_BAR,       /**< the scroll bar             */
    AB_HIT_BACK,      /**< BACK                       */
    AB_HIT_PAGE_UP,   /**< PAGE UP                    */
    AB_HIT_PAGE_DN,   /**< PAGE DOWN                  */
};

/**
 * @brief A part of the column: a line of the screen's own, wrapped to
 *        rows, or a run of generated rows (about_gen.h).
 */
typedef struct {
    char        style; /**< 'H' heading, 'N' line, 'D' dim line, 'B' empty;
                            0 for a run of generated rows */
    const char *text;  /**< the line; NULL for a run */
    int         first; /**< a run's first generated row */
    int         rows;  /**< the rows it takes */
} ab_seg_t;

/** @brief The screen's state. */
typedef struct {
    int      top;         /**< the row on top */
    bool     have_press;  /**< a contact holds a control */
    uint8_t  press_id;    /**< its touch id */
    int      hit;         /**< what it pressed, AB_HIT_* */
    int      x0;          /**< x of the DOWN, px */
    int      y0;          /**< y of the DOWN, px */
    int      top0;        /**< the top at the DOWN */
    bool     moved;       /**< it moved further than AB_SLOP */
    int      grab;        /**< where on the thumb the bar is held, px */
    bool     cu_known;    /**< the IOmcu's version as last drawn: named */
    uint32_t cu_version;  /**< and which, major << 16 | minor << 8 | patch */
    unsigned drawn_mask;  /**< per frame buffer: drawn and current */
} ab_state_t;

/** @brief The state, in about_screen.c. */
extern ab_state_t ab_s;

/**
 * @brief The column in the language showing.
 * @param segs Takes the parts, AB_SEGS at most.
 * @return The parts' count.
 */
int ab_segs(ab_seg_t *segs);

/**
 * @brief The rows the column holds.
 * @return The count.
 */
int ab_rows(void);

/**
 * @brief The largest top: the rows less a screen, or 0.
 * @return The row.
 */
int ab_max_top(void);

/**
 * @brief The thumb's top and height on the bar.
 * @param y Takes its top, body px.
 * @param h Takes its height, px.
 */
void ab_thumb(int *y, int *h);

/**
 * @brief Draw the screen into the body.
 * @param c            The canvas.
 * @param buffer_index The frame buffer.
 */
void ab_render(gfx_canvas_t *c, int buffer_index);
