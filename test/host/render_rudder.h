/**
 * @file render_rudder.h
 * @brief SERVO's RUDDER views for render_screen.c: the task opened by a tap
 *        on its tab after the router opened SERVO, the angle source fed,
 *        ZERO taken and the surface moved.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_RUDDER_H
#define RCBENCH_RENDER_RUDDER_H

#include <stdbool.h>

/**
 * @brief Whether @p view is one of RUDDER's.
 * @param view the view's name
 * @return true for servo-rudder, servo-rudder-... and help-servo-rudder...
 */
bool render_rudder_view(const char *view);

/**
 * @brief Whether @p view shows an ARMED bench.
 * @param view the view's name
 * @return true for servo-rudder-armed and servo-rudder-auto
 */
bool render_rudder_armed(const char *view);

/**
 * @brief Tap SERVO into @p view.  Called after the router opened it and
 *        before a guide view opens the guide.
 * @param view the view's name
 */
void render_rudder_pose(const char *view);

#endif
