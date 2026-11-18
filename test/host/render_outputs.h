/**
 * @file render_outputs.h
 * @brief OUTPUTS' views of a pin another protocol holds, for
 *        render_screen.c: the question card over the board and beside the
 *        grid, and the reason a tap on an interface's pin gives.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_OUTPUTS_H
#define RCBENCH_RENDER_OUTPUTS_H

#include <stdbool.h>

/**
 * @brief Tap OUTPUTS into @p view.  Called after the router opened the
 *        screen and before a guide view opens the guide.
 * @param view the view's name
 * @return true when @p view is one of these
 */
bool render_outputs_pose(const char *view);

#endif
