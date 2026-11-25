/**
 * @file
 * @brief What LOGS' graph of an XY file shows, for the host tests and the
 *        renderer: whether it shows, its traces and markers, the focused and
 *        the hidden series, and where a value and a row stand on screen.
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
 * @return true on the plot view with an XY file loaded.
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
 * @return the column, -1 for a row the graph does not hold.
 */
int log_graph_row_px(int i);

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
