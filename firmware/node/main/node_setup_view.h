/**
 * @file node_setup_view.h
 * @brief The boot splash's and SETUP's drawing onto a gfx canvas
 *        (node_setup.h).  Pure C, under the host suite (test_node_setup,
 *        tools/render_node.py).
 *
 * A screen is drawn whole when it is new or one of its values changed
 * (ns_t's rev); between those only the splash's bar of the time left is
 * drawn again, as it shortens.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "node_setup.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The splash's bar of the time left: its top and height, px. @{ */
#define NSV_BAR_Y 232  /**< its top */
#define NSV_BAR_H   8  /**< its height */
/** @} */

/** @brief What the frame shows now. */
typedef struct {
    bool     drawn;  /**< the frame holds a full draw of mode and rev */
    uint8_t  mode;   /**< the mode drawn */
    uint16_t rev;    /**< the rev drawn */
    int16_t  bar;    /**< the bar's width drawn, px */
} nsv_t;

/**
 * @brief Forget what the frame holds: the next nsv_render() draws all.
 * @param v The drawing's memory; NULL does nothing.
 */
void nsv_init(nsv_t *v);

/**
 * @brief Draw what changed since the last call: the whole screen on a new
 *        mode or rev, else the splash's bar when it shortened.
 * @param v      The drawing's memory.
 * @param c      The canvas, 320 x 240.
 * @param s      The state; NS_METER draws nothing.
 * @param now_ms The clock, for the splash's bar.
 * @return The parts drawn; 0 when nothing changed or an argument is NULL.
 */
int nsv_render(nsv_t *v, gfx_canvas_t *c, const ns_t *s, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
