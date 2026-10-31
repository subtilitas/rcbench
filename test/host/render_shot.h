/**
 * @file render_shot.h
 * @brief The screenshot popup's views for render_screen.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"

/**
 * @brief Whether @p view is one of the screenshot's.
 * @param view  the view's name
 * @return true for "shot-..."
 */
bool render_shot_view(const char *view);

/**
 * @brief Take the screen drawn as the picture and open the popup on it.
 * @param c     the panel's canvas, the screen under it posed
 * @param view  the view's name
 */
void render_shot_pose(gfx_canvas_t *c, const char *view);
