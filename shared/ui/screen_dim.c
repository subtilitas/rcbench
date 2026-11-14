/**
 * @file
 * @brief Dim after: the backlight's rule and the wake's touch filter
 *        (screen_dim.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "screen_dim.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "mstime.h"
#include "touch_merge.h"
#include "ui_band.h"

void screen_dim_init(screen_dim_t *d, uint32_t now_ms)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    d->last_ms = now_ms;
}

shot_wait_t screen_dim_wait(const shot_facts_t *facts)
{
    if (facts == NULL) {
        return shot_gate(NULL);
    }
    /* A fault log line on its way to the card keeps the screenshot off
     * the card while it is written; it says nothing about the bench.  The
     * link report writes one every 60s on a link that is up. */
    shot_facts_t f = *facts;
    f.no_card_fault = true;
    /* FAULT 01, LINK_SILENT, is what the IOmcu reports after a restart of
     * the panel, and it stands until the next ARM: it keeps the screen
     * lit no longer.  Every other FAULT bit does, and the band's FAULT
     * chip shows them all. */
    f.no_fault = f.no_fault || f.no_lit_fault;
    return shot_gate(&f);
}

bool screen_dim_idle(const shot_facts_t *facts)
{
    return screen_dim_wait(facts) == SHOT_IDLE;
}

unsigned screen_dim_step(screen_dim_t *d, uint32_t now_ms, int dim_after,
                         const shot_facts_t *facts)
{
    if (d == NULL) {
        return SCREEN_FULL_PCT;
    }
    const shot_wait_t wait = screen_dim_wait(facts);
    if (dim_after <= 0 || wait != SHOT_IDLE) {
        d->dimmed = false;
        d->last_ms = now_ms;
        d->why = (uint8_t)((dim_after <= 0) ? SCREEN_DIM_WHY_OFF
                                            : SCREEN_DIM_WHY_BUSY);
        d->why_wait = (uint8_t)wait;
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
    d->why = (uint8_t)SCREEN_DIM_WHY_TOUCH;
    d->why_type = (uint8_t)evt->type;
    d->why_id = id;
    if (evt->type == TOUCH_EVENT_DOWN) {
        /* A DOWN is a new contact: whatever this id named before is over. */
        held_drop(d, id);
        const bool on_stop =
            stop_live && gfx_rect_contains(ui_band_stop_rect(), evt->point.x,
                                           evt->point.y);
        /* A bench that is not idle is lit in this frame whatever the
         * last step left: nothing it drives waits for a second press. */
        if (was_dim && !on_stop && screen_dim_idle(facts)) {
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

bool screen_dim_wake(screen_dim_t *d, uint32_t now_ms)
{
    if (d == NULL) {
        return false;
    }
    const bool was_dim = d->dimmed;
    d->dimmed = false;
    d->last_ms = now_ms;
    d->why = (uint8_t)SCREEN_DIM_WHY_DIAL;
    return was_dim;
}

/**
 * @brief A touch event type's name for the console line.
 * @param type  touch_event_type_t
 * @return "DOWN", "MOVE", "UP" or "?"
 */
static const char *type_name(uint8_t type)
{
    switch (type) {
    case TOUCH_EVENT_DOWN:
        return "DOWN";
    case TOUCH_EVENT_MOVE:
        return "MOVE";
    case TOUCH_EVENT_UP:
        return "UP";
    default:
        return "?";
    }
}

size_t screen_dim_line(const screen_dim_t *d, uint32_t now_ms, int dim_after,
                       char *out, size_t n)
{
    if (d == NULL || out == NULL || n == 0u) {
        return 0u;
    }
    char why[48];
    switch (d->why) {
    case SCREEN_DIM_WHY_TOUCH:
        (void)snprintf(why, sizeof(why), "touch %s id 0x%02x %s",
                       type_name(d->why_type), (unsigned)d->why_id,
                       (d->why_id & TOUCH_MERGE_INJECT_ID) != 0u ? "injector"
                                                                 : "glass");
        break;
    case SCREEN_DIM_WHY_BUSY:
        (void)snprintf(why, sizeof(why), "busy %s",
                       shot_wait_name((shot_wait_t)d->why_wait));
        break;
    case SCREEN_DIM_WHY_OFF:
        (void)snprintf(why, sizeof(why), "off");
        break;
    case SCREEN_DIM_WHY_DIAL:
        (void)snprintf(why, sizeof(why), "dial");
        break;
    case SCREEN_DIM_WHY_START:
    default:
        (void)snprintf(why, sizeof(why), "start");
        break;
    }
    const int w = snprintf(out, n, "dim: after %dmin %s, %lus since %s",
                           dim_after, d->dimmed ? "dim" : "awake",
                           (unsigned long)((uint32_t)(now_ms - d->last_ms)
                                           / 1000u),
                           why);
    if (w < 0) {
        out[0] = '\0';
        return 0u;
    }
    return ((size_t)w < n) ? (size_t)w : n - 1u;
}
