/**
 * @file motor_balance.h
 * @brief MOTOR & ESC > BALANCE, the telemetry card's third pane: the
 *        balance node's live line, a TAKE key per run, each run's result,
 *        the method keys, CLEAR and the correction.
 *
 * The pane draws in the rectangle under PLOT | TABLE | BALANCE that the
 * table uses, 600 x 150px, on its own revision (node_balance_rev() and its
 * keys').  The keys act on the press.  Nothing on the pane drives an
 * output: the balance node is a measurement.
 *
 * Used by motor_draw.c, motor_events.c and motor_screen.c, and by the host
 * suite's test_motor_balance.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The pane's keys, for the tests. */
typedef enum {
    MBAL_KEY_TAKE0 = 0, /**< TAKE the baseline */
    MBAL_KEY_TAKE1,     /**< TAKE the trial at 0° */
    MBAL_KEY_TAKE2,     /**< four runs: TAKE the trial at 120° */
    MBAL_KEY_TAKE3,     /**< four runs: TAKE the trial at 240° */
    MBAL_KEY_TWO,       /**< 2 RUNS */
    MBAL_KEY_FOUR,      /**< 4 RUNS */
    MBAL_KEY_CLEAR,     /**< CLEAR */
    MBAL_KEY_COUNT      /**< the number of keys */
} mbal_key_t;

/** @brief Forget what was drawn: the next render paints the pane whole. */
void mbal_forget(void);

/**
 * @brief Where a key sits in @p area; a TAKE key the method does not use
 *        has width 0.
 * @param key  mbal_key_t.
 * @param area The pane.
 * @return The key's rectangle.
 */
gfx_rect_t mbal_key_rect(int key, gfx_rect_t area);

/**
 * @brief Paint the pane into frame buffer @p buf when its revision moved.
 * @param c    The canvas.
 * @param buf  The frame buffer, 0 or 1.
 * @param area The pane.
 * @param fill The card's colour, the pane is cleared to.
 */
void mbal_render(gfx_canvas_t *c, int buf, gfx_rect_t area, gfx_color_t fill);

/**
 * @brief One touch event on the pane: a press on a key acts.
 * @param evt  The event, NULL does nothing.
 * @param area The pane.
 * @return true when a key took it.
 */
bool mbal_event(const touch_event_t *evt, gfx_rect_t area);

#ifdef __cplusplus
}
#endif
