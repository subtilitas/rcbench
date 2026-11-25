/**
 * @file log_graph_int.h
 * @brief LOGS' graph of a file the bench wrote: the state, and what
 *        log_graph.c (the load, the cursor and the touches), log_graph_xy.c
 *        (an XY file's scales and rows' places), log_graph_time.c (a time
 *        file's places, gaps and strips) and log_graph_draw.c (the drawing)
 *        call in one another and the log viewer calls in them.
 *
 * A file whose preset is an XY plot (log_preset.h: LIMIT and RUDDR's two
 * pictures) is drawn over its x column instead of its row number: a trace a
 * search and side (log_marks.h), the markers, a chip a series with its
 * value at the cursor.  A tap on a chip focuses its series, a second hides
 * it, a third brings it back; the focused series' scale stands at the plot's
 * left edge.  RUDDR's file shows SEARCH and STEPS on two tabs in the chips'
 * row.  The XY graph shows the whole file: the window two fingers set on a
 * time plot does not apply.
 *
 * A file whose preset is a time plot (ATEST, SERVO, MOTOR, SUPPL) is drawn
 * over its time: a row stands at its time in the window two fingers set,
 * not at its row number.  A step in time longer than the larger of 3
 * median row intervals and LG_GAP_MIN_S breaks the traces and is hatched.
 * Under the traces stand the file's strips (log_marks.h), LG_STRIP_ROW px
 * each; over them ATEST's step lines and the OVER and CLIP spans.  The
 * chips are the series' own, from the first slot; a tap focuses and hides
 * as on an XY graph.  One finger, two fingers, < and > work as on the
 * plain time plot (log_viewer_events.c).
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

/** @brief A strip's row under a time plot's traces, px: its 8 x 16 label
 *         and, in its middle, the strip LG_STRIP_H tall. */
#define LG_STRIP_ROW 18
#define LG_STRIP_H   6   /**< The strip's height. */
/** @brief A span's bar at a time plot's top, px tall. */
#define LG_BAR_H     4
/** @brief A hatch's pitch: one pixel in LG_HATCH along each diagonal. */
#define LG_HATCH     8
/** @brief The shortest step in time that is a gap, whatever the rate, s. */
#define LG_GAP_MIN_S 0.5
/** @brief A step in time is a gap past this many median row intervals. */
#define LG_GAP_MEDIANS 3.0

/** @brief lg_state_t.col: the column is in a gap, hatched. */
#define LG_COL_GAP   0x01u
#define LG_COL_BREAK 0x02u  /**< A gap ends just before the column. */
#define LG_COL_ROW   0x04u  /**< A row stands in the column. */

_Static_assert(LG_TAG_H * 2 + 4 < PV_H / 4,
               "two rows of tags stand in the plot's top grid row");
_Static_assert(LG_STRIP_ROW >= 16 + 2 && LG_STRIP_H < LG_STRIP_ROW,
               "a strip's row holds its label of the 8 x 16 face");
_Static_assert(PV_H - LOG_STRIPS * LG_STRIP_ROW >= 3 * PV_H / 4,
               "two strips leave the traces three quarters of the plot");
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
    bool time;                      /**< a time plot, else an XY plot   */
    int  plot_h;                    /**< the traces' height, px         */
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
    /* A time plot. */
    bool   has_time;                /**< rows stand at their time       */
    long   gap_ms;                  /**< a longer step in time is a gap,
                                         in whole milliseconds          */
    float  t0;                      /**< the window's first time        */
    float  t1;                      /**< its last                       */
    bool   req;                     /**< two fingers set the range      */
    float  req_lo;                  /**< its first time, s              */
    float  req_hi;                  /**< its last time, s               */
    int    req_w0;                  /**< the window it was set for      */
    int    req_n;                   /**< and that window's rows         */
    /** @brief Per pixel column, LG_COL_ flags. */
    uint8_t col[PV_W];
    /** @brief Per strip and pixel column, the strip's code. */
    uint8_t strip_col[LOG_STRIPS][PV_W];
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

/** @brief The plot's data is dropped: the graph shows nothing, and the
 *         strips' cells are freed. */
void lg_drop(void);

/**
 * @brief The first series a chip stands for: 0 on a time plot, 1 on an XY
 *        plot, whose series 0 is the x column.
 * @return 0 or 1.
 */
int lg_k0(void);

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
 * @return the row, PV_Y to PV_Y + lg.plot_h - 1.
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

/* --- log_graph_time.c --------------------------------------------------- */

/**
 * @brief Whether the plot shows a time file's graph.
 * @return true with a time preset's file loaded.
 */
bool lg_time_on(void);

/**
 * @brief Set a time file's graph up from the data just built: the scales
 *        over each series' whole run, the gap threshold, the traces'
 *        height under the strips.
 */
void lg_time_setup(void);

/**
 * @brief Place the window's rows by their time: the traces' spans in
 *        lv_plot, the gaps and the strips' codes per pixel column.  Called
 *        for lv_compute_spans() while lg_time_on().
 */
void lg_time_spans(void);

/**
 * @brief The pixel column of row @p i in the window, from the plot's left.
 * @param i  The row.
 * @return 0 to PV_W - 1; -1 for a row outside the window or left or right
 *         of the plot's range of time.
 */
int lg_time_col(int i);

/**
 * @brief Row @p i's place on the plot, unrounded and unclamped: below 0
 *        left of the plot, above PV_W - 1 right of it.
 * @param i  The row, in the window.
 * @return the column, from the plot's left.
 */
float lg_time_colf(int i);

/**
 * @brief The row whose time stands nearest pixel column @p x, of the rows
 *        the plot draws.
 * @param x  The column, in the screen's coordinates.
 * @return a row on the plot.
 */
int lg_time_row_at(int x);

/**
 * @brief Whether two fingers zoom and pan a time file in time: a time
 *        graph whose file has a time column that rises over the run.
 *        Otherwise they zoom and pan in rows.
 * @return true for gestures in time.
 */
bool lg_time_gestures(void);

/**
 * @brief The time under pixel column @p x in the window.
 * @param x  The column, in the screen's coordinates; clamped to the plot.
 * @return the time, s from the run's first row.
 */
float lg_time_at(int x);

/**
 * @brief Show the time from @p t_lo to @p t_hi across the plot: the
 *        window from the last row at or before @p t_lo to the first at or
 *        after @p t_hi.  A range past either end of the run is moved inside
 *        it, one longer than the run is the whole run; one that brackets
 *        fewer than PV_MIN_WIN rows widens to PV_MIN_WIN rows at their own
 *        times.
 * @param t_lo  The range's first time, s.
 * @param t_hi  Its last time, s.
 */
void lg_time_window(float t_lo, float t_hi);

/**
 * @brief The row the plot draws nearest row @p row: @p row itself on the
 *        plot, else the first or the last row on it.
 * @param row  The row.
 * @return a row on the plot.
 */
int lg_time_drawn(int row);

/**
 * @brief Bring the cursor's row onto a time file's plot after < or >: a
 *        row past the range of time moves the range so the row stands on
 *        its nearer edge.
 * @return true when the time graph took the step, the plain plot's rows
 *         otherwise.
 */
bool lg_time_follow(void);

/**
 * @brief Whether the step in time from row @p i - 1 to row @p i is a gap.
 * @param i  The row.
 * @return true past the threshold, false for the first row and without a
 *         time column.
 */
bool lg_time_gap_before(int i);

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
