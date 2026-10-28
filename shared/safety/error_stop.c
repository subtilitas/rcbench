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
    const error_stop_t e = { live, supply_on && !port_holds };
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
    const error_stop_t none = { false, false };
    if (w == NULL || count == w->seen) {
        return none;
    }
    w->seen = count;
    return error_stop(live, supply_on, port_holds);
}
