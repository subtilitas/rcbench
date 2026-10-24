/**
 * @file
 * @brief The render loop's watchdog.
 *
 * See frame_watch.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "frame_watch.h"

#include <stddef.h>

void frame_watch_init(frame_watch_t *w, uint32_t count, uint32_t now_ms)
{
    if (w == NULL) {
        return;
    }
    w->seen     = count;
    w->moved_ms = now_ms;
    w->fired    = false;
}

bool frame_watch_step(frame_watch_t *w, uint32_t count, bool armed,
                      bool supply_on, uint32_t now_ms)
{
    if (w == NULL) {
        return false;
    }
    if (count != w->seen) {
        w->seen     = count;
        w->moved_ms = now_ms;
        w->fired    = false;
        return false;
    }
    if (w->fired || (!armed && !supply_on)
        || frame_watch_still_ms(w, now_ms) < FRAME_WATCH_MS) {
        return false;
    }
    w->fired = true;
    return true;
}

uint32_t frame_watch_still_ms(const frame_watch_t *w, uint32_t now_ms)
{
    return (w != NULL) ? (uint32_t)(now_ms - w->moved_ms) : 0u;
}
