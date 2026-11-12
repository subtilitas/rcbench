/**
 * @file
 * @brief Dim after: the backlight's rule and the wake's touch filter
 *        (screen_dim.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "screen_dim.h"

#include <stddef.h>
#include <string.h>

#include "gfx.h"
#include "mstime.h"
#include "ui_band.h"

void screen_dim_init(screen_dim_t *d, uint32_t now_ms)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    d->last_ms = now_ms;
}

unsigned screen_dim_step(screen_dim_t *d, uint32_t now_ms, int dim_after,
                         const shot_facts_t *facts)
{
    if (d == NULL) {
        return SCREEN_FULL_PCT;
    }
    if (dim_after <= 0 || !shot_gate_idle(facts)) {
        d->dimmed = false;
        d->last_ms = now_ms;
        return SCREEN_FULL_PCT;
    }
    const uint32_t limit = (uint32_t)dim_after * SCREEN_DIM_MS_PER_MIN;
    if (mstime_elapsed(now_ms, d->last_ms, limit)) {
        d->dimmed = true;
    }
    return d->dimmed ? SCREEN_DIM_PCT : SCREEN_FULL_PCT;
}

/**
 * @brief Where @p id is in the held contacts.
 * @param d   the state
 * @param id  the contact
 * @return its index, or -1
 */
static int held_find(const screen_dim_t *d, uint8_t id)
{
    for (uint8_t i = 0u; i < d->held_n; ++i) {
        if (d->held[i] == id) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Let go of @p id, if held.
 * @param d   the state
 * @param id  the contact
 */
static void held_drop(screen_dim_t *d, uint8_t id)
{
    const int at = held_find(d, id);
    if (at >= 0) {
        d->held[at] = d->held[d->held_n - 1u];
        --d->held_n;
    }
}

bool screen_dim_touch(screen_dim_t *d, const touch_event_t *evt,
                      bool stop_live, const shot_facts_t *facts,
                      uint32_t now_ms)
{
    if (d == NULL || evt == NULL) {
        return false;
    }
    const bool was_dim = d->dimmed;
    const uint8_t id = evt->point.id;
    d->dimmed = false;
    d->last_ms = now_ms;
    if (evt->type == TOUCH_EVENT_DOWN) {
        /* A DOWN is a new contact: whatever this id named before is over. */
        held_drop(d, id);
        const bool on_stop =
            stop_live && gfx_rect_contains(ui_band_stop_rect(), evt->point.x,
                                           evt->point.y);
        /* A bench that is not idle is lit in this frame whatever the
         * last step left: nothing it drives waits for a second press. */
        if (was_dim && !on_stop && shot_gate_idle(facts)) {
            /* Full only with releases lost: the first entry goes, which a
             * DOWN of its own id would have dropped. */
            if (d->held_n >= TOUCH_MAX_POINTS) {
                held_drop(d, d->held[0]);
            }
            d->held[d->held_n++] = id;
            return false;
        }
        return true;
    }
    if (held_find(d, id) < 0) {
        return true;
    }
    if (evt->type == TOUCH_EVENT_UP) {
        held_drop(d, id);
    }
    return false;
}

bool screen_dim_is_dim(const screen_dim_t *d)
{
    return d != NULL && d->dimmed;
}
