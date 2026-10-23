/**
 * @file render_files.h
 * @brief FILES' views for render_screen.c: the card task and its card set
 *        up before the screen opens, the taps that reach a view after.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_FILES_H
#define RCBENCH_RENDER_FILES_H

#include <stdbool.h>

/**
 * @brief Point FILES at a card task over the card of @p view.  Called
 *        before the router opens the screen.
 * @param view  the view's name, "files" or "files-..."
 */
void render_files_prepare(const char *view);

/**
 * @brief Whether @p view shows an ARMED bench.
 * @param view  the view's name
 * @return true for files-armed
 */
bool render_files_armed(const char *view);

/**
 * @brief Tap FILES into @p view.  Called after the router opened it.
 * @param view  the view's name
 */
void render_files_pose(const char *view);

#endif
