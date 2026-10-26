/**
 * @file render_hitec.h
 * @brief The Hitec programmer's views for render_screen.c: the overlay on
 *        SERVO driven by taps against the loopback of hitec_prog_rig.h, a
 *        D485HW or an HS-5645MG on programming port 1.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos these views show.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RENDER_HITEC_H
#define RCBENCH_RENDER_HITEC_H

#include <stdbool.h>

/**
 * @brief Open SERVO, PROGRAM and its Hitec row, and drive the overlay into
 *        @p view.  Exits with status 1 for a view it does not know or a
 *        state the bench does not reach.
 * @param view  the view's name, "servo-hitec" or "servo-hitec-..."
 */
void render_hitec_view(const char *view);

/**
 * @brief The bench up and the overlay's model fed, the tester on PROG
 *        PORT 1 at GP13: SERVO > PROGRAM's Hitec row names the pin.
 * @param held  true: the HITEC page holds port 1 for another session
 */
void render_hitec_feed(bool held);

#endif
