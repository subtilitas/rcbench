/**
 * @file render_scanner.h
 * @brief SETUP > SENSORS > SCANNER's views and SETUP CODE for
 *        tools/render_ui.py (render_scanner.c).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_SCANNER_H
#define RCBENCH_RENDER_SCANNER_H

#include <stdbool.h>

/**
 * @brief Whether @p view is one of this file's.
 * @param view The view's name.
 * @return true for setup-scanner-*.
 */
bool render_scanner_view(const char *view);

/**
 * @brief Pose @p view: the scanner's state, a line taken, and the screen.
 * @param view The view's name; another file's does nothing.
 */
void render_scanner_pose(const char *view);

#endif
