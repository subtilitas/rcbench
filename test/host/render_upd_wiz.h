/**
 * @file render_upd_wiz.h
 * @brief UPDATES' step views for render_screen.c: each step current, the
 *        Dial asking for its press, a step waiting in a run, a step held,
 *        the last card with a step skipped, and the fit check's views of
 *        every tag and card line.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_UPD_WIZ_H
#define RCBENCH_RENDER_UPD_WIZ_H

#include <stdbool.h>

/**
 * @brief Whether @p view is one of the steps' own views.
 * @param view the view's name
 * @return true for updates-step-*, updates-held, updates-done and
 *         updates-wiz-*
 */
bool render_upd_wiz_view(const char *view);

/**
 * @brief Pose @p view over UPDATES: the IOmcu section, the C6 row, the
 *        device rows, the run flag and the results handed in as the panel
 *        hands them, and SKIP tapped where the view has a step skipped.
 *        Called after render_screen.c posed UPDATES' own view.
 * @param view the view's name
 */
void render_upd_wiz_pose(const char *view);

#endif
