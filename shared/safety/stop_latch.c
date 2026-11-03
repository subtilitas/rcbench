/**
 * @file
 * @brief SPDX-License-Identifier: MIT
 */

#include "stop_latch.h"

#include <string.h>

#include "link_pages.h"

/**
 * @brief Wrap-safe: true from the millisecond @p deadline_ms on, for half
 *        the range.
 * @param now_ms      Current time, in milliseconds.
 * @param deadline_ms The deadline.
 * @return true at the deadline and after it.
 */
static bool reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(deadline_ms - now_ms) <= 0;
}

/**
 * @brief Wrap-safe: true from the millisecond after @p deadline_ms on.
 * @param now_ms      Current time, in milliseconds.
 * @param deadline_ms The deadline.
 * @return true after the deadline.
 */
static bool past(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(deadline_ms - now_ms) < 0;
}

void stop_latch_init(stop_latch_t *l, uint32_t pressed)
{
    if (l == NULL) {
        return;
    }
    memset(l, 0, sizeof(*l));
    l->pressed_seen = pressed;
}

bool stop_latch_follow(stop_latch_t *l, uint32_t pressed, uint32_t now_ms)
{
    if (l == NULL || pressed == l->pressed_seen) {
        return false;
    }
    l->pressed_seen = pressed;
    l->stop_ms      = now_ms;
    l->stop_known   = true;
    l->grace        = false;
    const bool was  = l->waiting;
    l->waiting      = false;
    return was;
}

bool stop_latch_ask(stop_latch_t *l, bool latched, uint32_t pressed,
                    uint32_t now_ms)
{
    if (l == NULL || !latched || l->waiting || pressed != l->pressed_seen) {
        return false;
    }
    l->waiting  = true;
    l->ask_ms   = now_ms;
    l->read_any = false;
    return true;
}

bool stop_latch_error(stop_latch_t *l)
{
    if (l == NULL || !l->waiting) {
        return false;
    }
    l->waiting = false;
    return true;
}

bool stop_latch_waiting(const stop_latch_t *l)
{
    return l != NULL && l->waiting;
}

bool stop_latch_read_due(stop_latch_t *l, uint32_t now_ms)
{
    if (l == NULL || !l->waiting) {
        return false;
    }
    if (l->read_any && !reached(now_ms, l->read_ms + STOP_LATCH_ASK_MS)) {
        return false;
    }
    l->read_any = true;
    l->read_ms  = now_ms;
    return true;
}

stop_latch_act_t stop_latch_step(stop_latch_t *l, uint32_t now_ms,
                                 bool latched, bool confirmed)
{
    if (l == NULL || !l->waiting) {
        return STOP_LATCH_NONE;
    }
    if (!latched) {
        /* Released some other way: nothing is left to reset. */
        l->waiting = false;
        return STOP_LATCH_NONE;
    }
    /* The bound first: a confirmation read past it is an exchange that
     * came back late, and it resets nothing. */
    if (past(now_ms, l->ask_ms + STOP_LATCH_CONFIRM_MS)) {
        l->waiting = false;
        return STOP_LATCH_GIVE_UP;
    }
    const bool floor = !l->stop_known
                       || reached(now_ms, l->stop_ms + STOP_LATCH_FLOOR_MS);
    if (confirmed && floor) {
        l->waiting  = false;
        l->grace    = true;
        l->grace_ms = now_ms;
        ++l->resets;
        return STOP_LATCH_RESUME;
    }
    return STOP_LATCH_NONE;
}

uint16_t stop_latch_faults(const stop_latch_t *l, uint32_t now_ms,
                           bool latched, uint16_t faults)
{
    const bool hide =
        latched
        || (l != NULL && l->grace
            && !past(now_ms, l->grace_ms + STOP_LATCH_GRACE_MS));
    return hide ? (uint16_t)(faults & (uint16_t)~LINK_FAULT_HEARTBEAT)
                : faults;
}

bool stop_latch_all_off(const stop_latch_reading_t *r)
{
    if (r == NULL || r->bank_armed || r->supply_on || r->prog_on) {
        return false;
    }
    if (!r->link_up) {
        return true;
    }
    return r->far_read && !r->far_armed && !r->far_supply && !r->far_prog;
}

uint32_t stop_latch_resets(const stop_latch_t *l)
{
    return (l != NULL) ? l->resets : 0u;
}
