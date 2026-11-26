/**
 * @file render_about.h
 * @brief ABOUT's views for tools/render_ui.py (render_about.c).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_ABOUT_H
#define RCBENCH_RENDER_ABOUT_H

#include <stdbool.h>

/**
 * @brief Whether @p view is one of this file's.
 * @param view The view's name.
 * @return true for setup-about*.
 */
bool render_about_view(const char *view);

/**
 * @brief Pose @p view: the IOmcu's version named, ABOUT scrolled.
 * @param view The view's name; another file's does nothing.
 */
void render_about_pose(const char *view);

#endif
