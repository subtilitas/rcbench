/**
 * @file render_servo_label.h
 * @brief The SERVO LABEL views for render_screen.c: the hub's list at its
 *        SERVO LABEL block, SERVO ID's choice list of 8, and TYPE ID's
 *        keyboard with an id typed and with one refused.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_SERVO_LABEL_H
#define RCBENCH_RENDER_SERVO_LABEL_H

#include <stdbool.h>

/**
 * @brief Whether @p view is one of SERVO LABEL's.
 * @param view the view's name
 * @return true for servo-label-...
 */
bool render_servo_label_view(const char *view);

/**
 * @brief Pose @p view on the hub's list, open already.
 * @param view the view's name; any other does nothing
 */
void render_servo_label_pose(const char *view);

#endif
