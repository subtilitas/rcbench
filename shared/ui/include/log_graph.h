/**
 * @file
 * @brief What LOGS' graph of a file the bench wrote shows, for the host
 *        tests and the renderer: whether it shows, its traces and markers,
 *        the focused and the hidden series, where a value and a row stand
 *        on screen, and a time file's gaps and strips.
 *        The graph itself is part of LOGS' plot view (log_viewer_screen.h);
 *        log_graph_int.h describes it.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "log_marks.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Whether the plot view shows the graph.
 * @return true on the plot view with a file of a preset loaded: an XY file
 *         or a time file.
 */
bool log_graph_shown(void);

/**
 * @brief The traces and markers of the file the graph shows.
 * @return them, NULL while no graph is loaded.
 */
const log_markset_t *log_graph_marks(void);

/**
 * @brief The series whose chip was tapped once: focused.
 * @return its index in the plot's data (1 to n_fields - 1), -1 for none.
 */
int log_graph_focus(void);

/**
 * @brief Whether series @p k is hidden.
 * @param k  Its index in the plot's data.
 * @return true while hidden.
 */
bool log_graph_hidden(int k);

/**
 * @brief The pixel column of @p x on the graph's x scale, in the screen's
 *        coordinates (the body under the band).
 * @param x  The value.
 * @return the column inside the plot.
 */
int log_graph_x_px(float x);

/**
 * @brief Whether a finger holds the graph's chip or plot: a press its
 *        release ends.
 * @return true while one does.
 */
bool log_graph_pressed(void);

/**
 * @brief The pixel column of row @p i.
 * @param i  The row.
 * @return the column, -1 for a row the graph does not hold or, on a time
 *         file, a row outside the window.
 */
int log_graph_row_px(int i);

/**
 * @brief Whether the graph is a time file's: its rows at their time.
 * @return true with ATEST, SERVO, MOTOR or SUPPL loaded.
 */
bool log_graph_time(void);

/**
 * @brief Whether the step in time to row @p i from the row before is a gap:
 *        longer than the larger of 3 median row intervals and 0.5s.
 * @param i  The row.
 * @return true for a gap; false for the first row and on no time file.
 */
bool log_graph_gap_before(int i);

/**
 * @brief Whether pixel column @p x lies in a gap, hatched.
 * @param x  The column, in the screen's coordinates.
 * @return true in a gap.
 */
bool log_graph_gap_col(int x);

/**
 * @brief The top row of strip @p k under a time file's traces.
 * @param k  The strip, 0 or 1.
 * @return the row, -1 for a strip the file has not.
 */
int log_graph_strip_y(int k);

/**
 * @brief Strip @p k's code at pixel column @p x (log_marks.h).
 * @param k  The strip.
 * @param x  The column, in the screen's coordinates.
 * @return the code; LOG_STRIP_NONE outside the plot and without the strip.
 */
unsigned log_graph_strip_at(int k, int x);

/**
 * @brief The footer's cursor line of the graph: the x column's name, its
 *        value at the cursor and the row.
 * @param out  Takes the line.
 * @param n    Bytes at @p out.
 * @return true when the graph is loaded and wrote it.
 */
bool log_graph_cursor_line(char *out, size_t n);

#ifdef __cplusplus
}
#endif
