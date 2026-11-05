/**
 * @file
 * @brief How WI-FI takes the view the application hands over
 *        (wifi_view.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_view.h"

#include <stddef.h>
#include <string.h>

uint32_t wifi_view_idle_min(uint32_t s)
{
    return s / 60u + ((s % 60u) != 0u ? 1u : 0u);
}

bool wifi_view_take(wifi_view_t *have, const wifi_view_t *v)
{
    if (have == NULL || v == NULL) {
        return false;
    }
    /* The bytes before the idle time and after it, padding included: a
     * member that differs always differs in its bytes, and padding that
     * differs costs one repaint, as before. */
    const size_t at = offsetof(wifi_view_t, idle_left_s);
    const size_t end = at + sizeof(v->idle_left_s);
    const unsigned char *a = (const unsigned char *)v;
    const unsigned char *b = (const unsigned char *)have;
    const bool other = memcmp(a, b, at) != 0
                       || memcmp(a + end, b + end, sizeof(*v) - end) != 0;
    const bool minute = wifi_view_idle_min(v->idle_left_s)
                        != wifi_view_idle_min(have->idle_left_s);
    *have = *v;
    return other || minute;
}
