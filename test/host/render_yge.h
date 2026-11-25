/**
 * @file render_yge.h
 * @brief The views of programming for YGE ESCs for render_screen.c: the
 *        CONNECT page and its editors, the entry, BY HAND, every tab of a
 *        read ESC, an edit with its CHECK line, WRITE n VALUES held, the
 *        LEAVE page, a read-only table, a stop, a pin held, another
 *        writer's pin and one not back.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_YGE_H
#define RCBENCH_RENDER_YGE_H

/**
 * @brief Poses the session for a view: `motor-yge-*`.
 * @param view The view's name.
 */
void render_yge_view(const char *view);

#endif /* RCBENCH_RENDER_YGE_H */
