/**
 * @file render_printer.h
 * @brief SETUP > WIRELESS > PRINTER's views and SEARCH for the printer,
 *        for tools/render_ui.py (render_printer.c).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_PRINTER_H
#define RCBENCH_RENDER_PRINTER_H

#include <stdbool.h>

/**
 * @brief Whether @p view is one of this file's.
 * @param view The view's name.
 * @return true for printer-*.
 */
bool render_printer_view(const char *view);

/**
 * @brief Pose @p view: the printer's view, its bind, and the screen.
 * @param view The view's name; another file's does nothing.
 */
void render_printer_pose(const char *view);

#endif
