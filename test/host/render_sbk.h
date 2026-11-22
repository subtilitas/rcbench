/**
 * @file render_sbk.h
 * @brief The settings backup's views for render_screen.c: SETUP > SD CARD
 *        at the end of its list, and BACKUP with the card's files, as the
 *        restore offer, and after a load that refused lines.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_SBK_H
#define RCBENCH_RENDER_SBK_H

#include <stdbool.h>

/**
 * @brief Whether @p view is one of the settings backup's.
 * @param view the view's name
 * @return true for setup-card-backup and setup-backup...
 */
bool render_sbk_view(const char *view);

/**
 * @brief Pose @p view.  Called after the router opened its screen and after
 *        SETUP's own views were posed.
 * @param view the view's name
 */
void render_sbk_pose(const char *view);

#endif
