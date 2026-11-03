/**
 * @file render_esc_wire.h
 * @brief The ESC signal-wire session's views for render_screen.c.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_ESC_WIRE_H
#define RCBENCH_RENDER_ESC_WIRE_H

/**
 * @brief Poses view @p view of the session: fed by hand, with no bench behind it.
 * @param view The view's name, `motor-wire-...`.
 */
void render_esc_wire_view(const char *view);

#endif
