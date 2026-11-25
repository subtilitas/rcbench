/**
 * @file log_graph_int.h
 * @brief LOGS' graph of an XY file: the state, and what log_graph.c (the
 *        load, the cursor and the touches), log_graph_xy.c (the scales and
 *        the rows' places) and log_graph_draw.c (the drawing) call in one
 *        another and the log viewer calls in them.
 *
 * A file whose preset is an XY plot (log_preset.h: LIMIT and RUDDR's two
 * pictures) is drawn over its x column instead of its row number: a trace a
 * search and side (log_marks.h), the markers, a chip a series with its
 * value at the cursor.  A tap on a chip focuses its series, a second hides
 * it, a third brings it back; the focused series' scale stands at the plot's
 * left edge.  RUDDR's file shows SEARCH and STEPS on two tabs in the chips'
 * row.  The graph shows the whole file: the window two fingers set on a
 * time plot does not apply.
 *
 * Included after log_viewer_int.h, whose layout it uses.  What one file
 * calls in another carries the prefix lg_.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "log_marks.h"
#include "log_viewer_int.h"

/**
 * @brief The chips' row in the graph: the series' chips from the left, a
 *        chip slot LG_SLOT(j) apart; the markers' legend in the slots the
 *        series leave; RUDDR's tabs in the last slot, 3px inside it.
 */
#define LG_SLOT(j)  gfx_rect_make(CHIP_X + (j) * (CHIP_W + CHIP_GAP), CHIP_Y, \
                                  CHIP_W, CHIP_H)
/** @brief RUDDR's SEARCH and STEPS tabs, in the fourth slot. */
#define LG_TABS     gfx_rect_make(CHIP_X + 3 * (CHIP_W + CHIP_GAP) + 3,      \
                                  CHIP_Y + 3, CHIP_W - 6, CHIP_H - 6)
/** @brief A marker's tag: one letter of the 8 x 16 face on a box. */
#define LG_TAG_W    14
#define LG_TAG_H    18  /**< Its height. */
/** @brief A dashed line's dash and gap, px. */
#define LG_DASH     6
#define LG_GAP      4   /**< The gap. */
/** @brief A point of STEPS and of the verifies, px a side. */
#define LG_DOT      5
/** @brief A write's tick at the plot's foot, px tall. */
#define LG_TICK_H   10

_Static_assert(LG_TAG_H * 2 + 4 < PV_H / 4,
               "two rows of tags stand in the plot's top grid row");
_Static_assert(CHIP_H - 6 >= UI_TABS_LABEL_H,
               "the tabs draw their labels in the 10 x 20 face");

/** @brief What a press in the graph is on. */
typedef enum {
    LG_PRESS_NONE = 0,  /**< no press                       */
    LG_PRESS_CHIP,      /**< a series' chip: lg.press_chip  */
    LG_PRESS_PLOT       /**< the plot: the cursor follows   */
} lg_press_t;

/** @brief The graph's state, beside the log viewer's. */
typedef struct {
    bool on;                        /**< the open file shows as a graph */
    log_markset_t mk;                 /**< its traces and markers         */
    float xlo;                      /**< the x scale's left end         */
    float xhi;                      /**< its right end                  */
    float lo[LOG_MAX_SERIES];       /**< per series, the scale's foot   */
    float hi[LOG_MAX_SERIES];       /**< per series, its top            */
    /** @brief Per row, its pixel column. */
    int16_t px[LOG_MARKS_ROWS];
    /** @brief Per series and row, its pixel row; PV_EMPTY without a
     *         number.  Series 0 is the x column and unused. */
    int16_t py[LOG_MAX_SERIES][LOG_MARKS_ROWS];
    int focus;                      /**< the focused series, -1 none    */
    bool hidden[LOG_MAX_SERIES];    /**< per series, hidden             */
    bool tabbed;                    /**< RUDDR: SEARCH and STEPS shown  */
    ui_tabs_t tabs;                 /**< those tabs                     */
    lg_press_t press;               /**< the press under way            */
    int press_chip;                 /**< the chip pressed               */
    uint8_t press_id;               /**< the contact that holds it      */
} lg_state_t;

/** @brief The graph's state, in log_graph.c. */
extern lg_state_t lg;

/* --- log_graph.c -------------------------------------------------------- */

/**
 * @brief Read the traces and markers of the file the plot just built, when
 *        its preset is an XY plot and the build holds the x column first.
 * @param src  The file, still open.
 * @return true when the graph takes the file; lg_start() then sets it up.
 */
bool lg_read(log_source_t *src);

/** @brief Set the graph up for the file lg_read() took: the scales, the
 *         cursor on the last row drawn, the tabs, no series focused. */
void lg_start(void);

/** @brief The plot's data is dropped: the graph shows nothing. */
void lg_drop(void);

/** @brief A press under way is abandoned. */
void lg_cancel(void);

/**
 * @brief Whether the plot shows the graph.
 * @return true with an XY file loaded.
 */
bool lg_on(void);

/**
 * @brief Move the cursor to the next row drawn, @p dir from the one it is
 *        on, in the file's order.
 * @param dir  -1 or 1.
 * @return true when the graph is on: the step is taken or there is none.
 */
bool lg_step(int dir);

/**
 * @brief Take a touch for the graph: a chip, the tabs, the plot.
 * @param e  The event.
 * @return true when the graph took it; the footer's keys are not its.
 */
bool lg_event(const touch_event_t *e);

/**
 * @brief The summary line of the graph: the file, its rows and its kind.
 * @param out  Takes the line.
 * @param n    Bytes at @p out.
 * @return true when the graph is on and wrote it.
 */
bool lg_summary(char *out, size_t n);

/**
 * @brief The footer's cursor line of the graph: the x column's name and
 *        value, and the row.
 * @param out  Takes the line.
 * @param n    Bytes at @p out.
 * @return true when the graph is on and wrote it.
 */
bool lg_cursor_line(char *out, size_t n);

/* --- log_graph_xy.c ----------------------------------------------------- */

/** @brief The x scale over the rows drawn and the lines, each series'
 *         scale over its rows and its levels, and every row's place. */
void lg_scale(void);

/**
 * @brief The pixel column of @p x on the x scale.
 * @param x  The value.
 * @return the column, PV_X to PV_X + PV_W - 1.
 */
int lg_x_px(float x);

/**
 * @brief The pixel row of @p v on series @p k's scale.
 * @param k  The series.
 * @param v  The value.
 * @return the row, PV_Y to PV_Y + PV_H - 1.
 */
int lg_y_px(int k, float v);

/**
 * @brief The series a level of analysis column @p column is drawn on.
 * @param column  The column.
 * @return the series, -1 when the plot holds none of that column.
 */
int lg_series_of(int column);

/**
 * @brief The row drawn nearest the point (@p x, @p y): nearest in x, and of
 *        rows at one column the one whose first shown series lies nearest
 *        in y.
 * @param x  Pixel column.
 * @param y  Pixel row.
 * @return the row, -1 with no row drawn.
 */
int lg_row_at(int x, int y);

/**
 * @brief Whether row @p i is drawn: in a trace.
 * @param i  The row.
 * @return true for a row of a trace.
 */
bool lg_drawn(int i);

/* --- log_graph_draw.c --------------------------------------------------- */

/**
 * @brief Draw the graph's chips, legend, tabs, plot, markers, cursor and x
 *        labels: the plot view between its summary line and its footer.
 * @param c  The canvas.
 */
void lg_render(gfx_canvas_t *c);

/* --- log_viewer_draw.c -------------------------------------------------- */

/**
 * @brief Draw the plot's grid into the band of @p rows rows that starts at
 *        row @p band_y0.
 * @param band     The band's canvas.
 * @param band_y0  Its first row.
 * @param rows     Its rows.
 */
void lv_grid_band(gfx_canvas_t *band, int band_y0, int rows);
