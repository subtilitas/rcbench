/**
 * @file
 * @brief What WI-FI's view of HOTSPOT PASSWORD or WEB CODE draws: what the
 *        value is for, the value kept or RANDOM, its rule and when a change
 *        takes effect.  wifi_screen.c holds the view's buttons.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_KEPT_H
#define RCBENCH_WIFI_KEPT_H

#include "gfx.h"
#include "wifi_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The card of the view.
 * @param c     Canvas.
 * @param card  Where.
 * @param which Which of the two.
 * @param v     What the application handed in.
 */
void wifi_kept_draw(gfx_canvas_t *c, gfx_rect_t card, wifi_kept_t which,
                    const wifi_view_t *v);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WIFI_KEPT_H */
