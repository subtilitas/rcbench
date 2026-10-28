/**
 * @file
 * @brief The panel's rule for an error.
 *
 * See error_stop.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "error_stop.h"

#include <stddef.h>

error_stop_t error_stop(bool live, bool supply_on, bool port_holds)
{
    const error_stop_t e = { live, supply_on && !port_holds, port_holds };
    return e;
}

void error_watch_init(error_watch_t *w, uint32_t count)
{
    if (w != NULL) {
        w->seen = count;
    }
}

error_stop_t error_stop_take(error_watch_t *w, uint32_t count, bool live,
                             bool supply_on, bool port_holds)
{
    const error_stop_t none = { false, false, false };
    if (w == NULL || count == w->seen) {
        return none;
    }
    w->seen = count;
    return error_stop(live, supply_on, port_holds);
}

void port_cut_ask(port_cut_t *c, uint32_t now_ms)
{
    if (c != NULL && !c->armed) {
        c->armed = true;
        c->acted = false;
        c->t0    = now_ms;
    }
}

bool port_cut_step(port_cut_t *c, bool holds, bool writing, bool supply_on,
                   uint32_t now_ms)
{
    if (c == NULL || !c->armed) {
        return false;
    }
    if (!holds && !supply_on) {
        c->armed = false;
        c->acted = false;
        return false;
    }
    /* A write is waited for until the cut has acted once: one that runs
     * after it reaches a servo whose supply this cut switched off. */
    if (!supply_on
        || (writing && !c->acted
            && (uint32_t)(now_ms - c->t0) < ERROR_PORT_CUT_MS)) {
        return false;
    }
    c->acted = true;
    return true;
}

bool port_cut_blocks(const port_cut_t *c)
{
    return c != NULL && c->armed && c->acted;
}
