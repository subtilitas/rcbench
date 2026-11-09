/**
 * @file
 * @brief The PROG 5V watch.  See prog5v_lost.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "prog5v_lost.h"

#include <stdio.h>
#include <string.h>

#include "link_pages.h"

void prog5v_lost_init(prog5v_lost_t *d)
{
    if (d != NULL) {
        memset(d, 0, sizeof(*d));
    }
}

/**
 * @brief Start the settle time again at @p now_ms and drop the low windows
 *        counted and a finding that waits.
 * @param d      the state
 * @param cuts   the cuts the time starts with
 * @param now_ms the time, ms
 */
static void restart(prog5v_lost_t *d, uint32_t cuts, uint32_t now_ms)
{
    d->settled = false;
    d->low     = 0u;
    d->waits   = false;
    d->cuts    = cuts;
    d->on_ms   = now_ms;
}

/**
 * @brief A finding made: a trip now, or, while a programming port holds
 *        the 5V, one that waits for a page read made after it.
 * @param d    the state
 * @param kind what was found
 * @param on   the ON as the page reads it
 * @return true for a trip now
 */
static bool found(prog5v_lost_t *d, prog5v_trip_t kind, const prog5v_on_t *on)
{
    d->kind = (uint8_t)kind;
    d->low  = 0u;
    if (on->port) {
        d->waits      = true;
        d->wait_reads = on->reads;
        return false;
    }
    d->tripped = true;
    return true;
}

/**
 * @brief A finding that waits under a port's hold, confirmed: a page read
 *        made since it still reads the pin on.
 * @param d  the state
 * @param on the ON as the page reads it, on
 * @return true for the trip now
 */
static bool confirm(prog5v_lost_t *d, const prog5v_on_t *on)
{
    if (!d->waits || on->reads == d->wait_reads) {
        return false;
    }
    d->waits   = false;
    d->tripped = true;
    return true;
}

bool prog5v_lost_step(prog5v_lost_t *d, const prog5v_on_t *on,
                      uint32_t now_ms, const prog5v_window_t *w)
{
    if (d == NULL) {
        return false;
    }
    const bool is_on = on != NULL && on->on;
    const uint32_t cuts = (on != NULL) ? on->cuts : d->cuts;
    if (!is_on) {
        /* Off: the next ON is a new one, with a trip of its own. */
        d->on      = false;
        d->tripped = false;
        d->reached = false;
        d->cut     = false;
        d->kind    = (uint8_t)PROG5V_TRIP_NONE;
        restart(d, cuts, now_ms);
    } else if (!d->on) {
        d->on       = true;
        d->start_ms = now_ms;
        restart(d, cuts, now_ms);
    } else if (cuts != d->cuts) {
        /* An OFF asked under the ON that the page still reads on. */
        d->cut = true;
        restart(d, cuts, now_ms);
    }
    /* Latched once passed: an ON held past the clock's wrap stays settled. */
    if (d->on && (uint32_t)(now_ms - d->on_ms) >= PROG5V_LOST_SETTLE_MS) {
        d->settled = true;
    }
    /* The window before a waiting finding is confirmed: the panel reads
     * PROG_SUPPLY before SENSE_WIN in a pass, so the page read that would
     * confirm it and a window that drops it can come in the same step. */
    if (w == NULL || !w->ok) {
        d->low   = 0u;
        d->waits = false;
        return false;
    }
    const bool fresh = !d->seen || w->window != d->window;
    d->seen   = true;
    d->window = w->window;
    if (!fresh || !d->on || d->tripped) {
        /* Read again, off, or tripped: a finding under a port's hold is
         * confirmed by a page read since that still reads on. */
        return confirm(d, on);
    }
    /* Not low: PROG5V_MIN_MV before the 5V came, PROG5V_LOST_MV after. */
    if (w->mv >= (d->reached ? PROG5V_LOST_MV : PROG5V_MIN_MV)) {
        d->reached = true;
        d->low     = 0u;
        d->waits   = false;
        return false;
    }
    if (d->waits) {
        return confirm(d, on);
    }
    if (!d->reached) {
        if ((uint32_t)(now_ms - d->start_ms) < PROG5V_REACH_MS
            || (d->cut && !d->settled)) {
            return false;
        }
        d->mv[0]   = w->mv;
        d->trip_ms = now_ms - d->start_ms;
        return found(d, PROG5V_TRIP_MISSING, on);
    }
    if (!d->settled) {
        d->low = 0u;
        return false;
    }
    d->mv[d->low] = w->mv;
    ++d->low;
    if (d->low < PROG5V_LOST_WINDOWS) {
        return false;
    }
    d->trip_ms = now_ms - d->on_ms;
    return found(d, PROG5V_TRIP_LOST, on);
}

prog5v_trip_t prog5v_lost_kind(const prog5v_lost_t *d)
{
    if (d == NULL || !d->tripped) {
        return PROG5V_TRIP_NONE;
    }
    return (prog5v_trip_t)d->kind;
}

uint16_t prog5v_lost_mv(const prog5v_lost_t *d)
{
    switch (prog5v_lost_kind(d)) {
    case PROG5V_TRIP_MISSING:
        return d->mv[0];
    case PROG5V_TRIP_LOST:
        return d->mv[PROG5V_LOST_WINDOWS - 1u];
    default:
        return 0u;
    }
}

const char *prog5v_lost_line(const prog5v_lost_t *d, unsigned ch, char *buf,
                             size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    if (d == NULL || ch >= LINK_SM_CHANNELS) {
        return buf;
    }
    const unsigned addr = LINK_SN_I3221_ADDR_MIN + ch / 3u;
    const unsigned a = d->mv[0];
    const unsigned b = d->mv[PROG5V_LOST_WINDOWS - 1u];
    if (d->kind == (uint8_t)PROG5V_TRIP_MISSING) {
        snprintf(buf, n,
                 "PROG 5V missing: INA3221 0x%02X CH%u read %u.%02uV %lums "
                 "after ON. All outputs off.",
                 addr, ch % 3u + 1u, a / 1000u, (a % 1000u) / 10u,
                 (unsigned long)d->trip_ms);
        return buf;
    }
    snprintf(buf, n,
             "PROG 5V lost: INA3221 0x%02X CH%u read %u.%02uV, %u.%02uV in "
             "%u windows, %lums after ON. All outputs off.",
             addr, ch % 3u + 1u,
             a / 1000u, (a % 1000u) / 10u, b / 1000u, (b % 1000u) / 10u,
             (unsigned)PROG5V_LOST_WINDOWS, (unsigned long)d->trip_ms);
    return buf;
}
