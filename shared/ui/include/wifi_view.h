/**
 * @file
 * @brief How WI-FI takes the view the application hands over: whether a new
 *        view changes what the screen draws.
 *
 * The idle time arrives in seconds and the screen writes it in whole
 * minutes, rounded up.  A view that differs only in the seconds of one
 * shown minute is kept and repaints nothing.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_VIEW_H
#define RCBENCH_WIFI_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "wifi_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The idle time as the screen writes it.
 * @param s The time left, s.
 * @return Whole minutes, rounded up: 1s to 60s are 1, 0s is 0.
 */
uint32_t wifi_view_idle_min(uint32_t s);

/**
 * @brief Take @p v as the view held at @p have.
 * @param have The view held; takes @p v.
 * @param v    The view handed over; NULL takes nothing.
 * @return true when what the screen draws may have changed: any member
 *         other than the idle time, or the idle time's minutes; false for
 *         NULL, the same view, or the same view with the idle time in the
 *         same minute.
 */
bool wifi_view_take(wifi_view_t *have, const wifi_view_t *v);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WIFI_VIEW_H */
