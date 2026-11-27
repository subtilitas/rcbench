/**
 * @file render_imu.h
 * @brief The Bluetooth views for render_screen.c: SETUP > WIRELESS on each
 *        entry with a view handed to it, and SEARCH opened from an entry's
 *        rows.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_IMU_H
#define RCBENCH_RENDER_IMU_H

#include <stdbool.h>

/**
 * @brief Whether @p view is one of the IMU's.
 * @param view the view's name
 * @return true for setup-wireless-...
 */
bool render_imu_view(const char *view);

/**
 * @brief Pose @p view.  Called after the router opened its screen and
 *        after SETUP's own views were posed.
 * @param view the view's name
 */
void render_imu_pose(const char *view);

#endif
