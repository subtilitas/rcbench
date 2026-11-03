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
 *        counted.
 * @param d      the state
 * @param cuts   the cuts the time starts with
 * @param now_ms the time, ms
 */
static void restart(prog5v_lost_t *d, uint32_t cuts, uint32_t now_ms)
{
    d->settled = false;
    d->low     = 0u;
    d->cuts    = cuts;
    d->on_ms   = now_ms;
}

bool prog5v_lost_step(prog5v_lost_t *d, bool on, uint32_t cuts,
                      uint32_t now_ms, const prog5v_window_t *w)
{
    if (d == NULL) {
        return false;
    }
    if (!on) {
        /* Off: the next ON is a new one, with a trip of its own. */
        d->on      = false;
        d->tripped = false;
        restart(d, cuts, now_ms);
    } else if (!d->on || cuts != d->cuts) {
        /* An ON, or an OFF asked under one that the page still reads on. */
        d->on = true;
        restart(d, cuts, now_ms);
    }
    /* Latched once passed: an ON held past the clock's wrap stays settled. */
    if (d->on && (uint32_t)(now_ms - d->on_ms) >= PROG5V_LOST_SETTLE_MS) {
        d->settled = true;
    }
    if (w == NULL || !w->ok) {
        d->low = 0u;
        return false;
    }
    const bool fresh = !d->seen || w->window != d->window;
    d->seen   = true;
    d->window = w->window;
    if (!fresh) {
        return false;                  /* the same window read again */
    }
    if (!d->settled || d->tripped || w->mv >= PROG5V_LOST_MV) {
        d->low = 0u;
        return false;
    }
    d->mv[d->low] = w->mv;
    ++d->low;
    if (d->low < PROG5V_LOST_WINDOWS) {
        return false;
    }
    d->tripped = true;
    d->trip_ms = now_ms - d->on_ms;
    return true;
}

uint16_t prog5v_lost_mv(const prog5v_lost_t *d)
{
    if (d == NULL || !d->tripped) {
        return 0u;
    }
    return d->mv[PROG5V_LOST_WINDOWS - 1u];
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
    const unsigned a = d->mv[0];
    const unsigned b = d->mv[PROG5V_LOST_WINDOWS - 1u];
    snprintf(buf, n,
             "PROG 5V lost: INA3221 0x%02X CH%u read %u.%02uV, %u.%02uV in "
             "%u windows, %lums after ON. All outputs off.",
             (unsigned)(LINK_SN_I3221_ADDR_MIN + ch / 3u), ch % 3u + 1u,
             a / 1000u, (a % 1000u) / 10u, b / 1000u, (b % 1000u) / 10u,
             (unsigned)PROG5V_LOST_WINDOWS, (unsigned long)d->trip_ms);
    return buf;
}
