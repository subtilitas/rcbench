/**
 * @file render_help.h
 * @brief The screen guide's views for render_screen.c: the guide opened by
 *        a tap on the band's "?" and turned to a page, and the band's
 *        widest row of chips.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_HELP_H
#define RCBENCH_RENDER_HELP_H

#include <stdbool.h>

/**
 * @brief Whether @p view is one of the guide's.
 * @param view  the view's name
 * @return true for help-...
 */
bool render_help_view(const char *view);

/**
 * @brief Pose @p view after the router opened its screen: help-<screen>
 *        and help-<screen>-<n> open the guide at page 1 or n;
 *        help-band-worst puts the widest row of chips on the band.
 * @param view  the view's name
 */
void render_help_pose(const char *view);

#endif
