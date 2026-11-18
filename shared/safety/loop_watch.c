/**
 * @file
 * @brief The IOmcu's watchdog on its own main loop: see loop_watch.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "loop_watch.h"

#include <stddef.h>

#include "link_pages.h"

/* The sums the header's comment states, and the period at least twice the
 * longest time between two feeds. */
_Static_assert(LOOP_WATCH_WINDOW_MAX_US == 42000u,
               "loop_watch.h: the longest window in the comment differs");
_Static_assert(LOOP_WATCH_ERASE_MAX_US + LOOP_WATCH_WINDOW_ENTRY_US
                   <= LOOP_WATCH_WINDOW_MAX_US,
               "a sector erase is longer than the longest window");
_Static_assert(LOOP_WATCH_WORST_GAP_US == 108250u,
               "loop_watch.h: the sum in the comment differs");
_Static_assert(2u * LOOP_WATCH_WORST_GAP_US < LOOP_WATCH_PERIOD_MS * 1000u,
               "the watchdog's period is under 2 of the longest gaps");

void loop_watch_init(loop_watch_t *w, bool console, reset_cause_t cause)
{
    if (w == NULL) {
        return;
    }
    w->console   = console;
    w->enabled   = false;
    w->restarted = cause == RESET_CAUSE_LOOP_WATCHDOG;
}

loop_watch_act_t loop_watch_pass(loop_watch_t *w, bool on_trial)
{
    if (w == NULL || w->console) {
        return LOOP_WATCH_NONE;
    }
    if (w->enabled) {
        return LOOP_WATCH_FEED;
    }
    if (on_trial) {
        return LOOP_WATCH_NONE;
    }
    w->enabled = true;
    return LOOP_WATCH_ENABLE;
}

loop_watch_act_t loop_watch_window(const loop_watch_t *w)
{
    return (w != NULL && w->enabled) ? LOOP_WATCH_FEED : LOOP_WATCH_NONE;
}

bool loop_watch_handover(const loop_watch_t *w)
{
    return w != NULL && w->enabled;
}

uint16_t loop_watch_fault(const loop_watch_t *w)
{
    return (w != NULL && w->restarted) ? (uint16_t)LINK_FAULT_WATCHDOG : 0u;
}

void loop_watch_control(loop_watch_t *w, uint8_t off, uint8_t n,
                        uint8_t nack)
{
    if (w == NULL || nack != 0u) {
        return;
    }
    if (off <= (uint8_t)LINK_CT_CLEAR
        && (unsigned)off + n > (unsigned)LINK_CT_CLEAR) {
        w->restarted = false;
    }
}
