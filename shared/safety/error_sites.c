/**
 * @file
 * @brief Where the panel's control task finds an error, and what it does
 *        there.
 *
 * See error_sites.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "error_sites.h"

#include <stddef.h>

#include "link_pages.h"

bool error_port_defers(bool supply_page, uint16_t far_minor)
{
    return supply_page && far_minor >= LINK_MINOR_PORT_CUT;
}

/**
 * @brief error_stop()'s answer as the steps the control task takes: DISARM,
 *        the output off, and to an IOmcu of 4.31 with the PD mini in use
 *        the page's OFF whatever the panel reads of the output.
 *
 * @param b The bench the answer was judged on.
 * @param e The answer.
 * @return The act, with no OFF first.
 */
static error_act_t act_of(const error_bench_t *b, error_stop_t e)
{
    error_act_t a = { false, false, false, false };

    a.disarm = e.disarm;
    a.supply_off = e.supply_off;
    a.cut = e.supply_off && b->supply_real
            && error_port_defers(b->supply_page, b->far_minor);
    return a;
}

error_act_t error_sites_stop(const error_bench_t *b)
{
    const error_act_t none = { false, false, false, false };

    if (b == NULL) {
        return none;
    }
    return act_of(b, error_stop(b->live, b->supply_on, b->port_holds,
                                error_port_defers(b->supply_page,
                                                  b->far_minor)));
}

error_act_t error_sites_follow(error_watch_t *watch, uint32_t count,
                               const error_bench_t *b)
{
    const error_act_t none = { false, false, false, false };

    if (watch == NULL || b == NULL) {
        return none;
    }
    return act_of(b, error_stop_take(watch, count, b->live, b->supply_on,
                                     b->port_holds,
                                     error_port_defers(b->supply_page,
                                                       b->far_minor)));
}

error_act_t error_sites_cut(const error_bench_t *b)
{
    const error_act_t none = { false, false, false, false };

    if (b == NULL) {
        return none;
    }
    /* The rule on the bench as the OFF leaves it. */
    error_bench_t off = *b;
    off.supply_on = false;
    error_act_t a = error_sites_stop(&off);
    a.off_first = true;
    return a;
}

error_reading_t error_sites_reading(const error_bench_t *b, bool online,
                                    bool blind, error_act_t *act)
{
    const error_act_t none = { false, false, false, false };

    if (act != NULL) {
        *act = none;
    }
    if (b == NULL || act == NULL) {
        return ERROR_READING_NONE;
    }
    /* The model always answers, and a blind reading is the PD mini's. */
    error_reading_t r = ERROR_READING_NONE;
    if (!online && b->supply_on) {
        r = ERROR_READING_SILENT;
    } else if (b->supply_real && b->supply_on && blind) {
        r = ERROR_READING_BLIND;
    }
    if (r != ERROR_READING_NONE) {
        *act = error_sites_cut(b);
    }
    return r;
}

bool error_card_rows_edge(bool open, bool was_failed, bool failed)
{
    return open && !was_failed && failed;
}

bool error_card_csv_edge(bool bad, bool failed_before, bool txt)
{
    return bad && !failed_before && !txt;
}
