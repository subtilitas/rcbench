/**
 * @file
 * @brief The STATUS read period: 500ms, and every bench poll while HWSTOP
 *        stands (status_poll.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "status_poll.h"

#include <stddef.h>

#include "link_pages.h"

bool status_poll_fast(bool hw_stop, uint16_t faults)
{
    return hw_stop && (faults & (uint16_t)LINK_FAULT_HEARTBEAT) != 0u;
}

bool status_poll_due(uint32_t *last_ms, uint32_t now_ms, bool fast)
{
    if (last_ms == NULL) {
        return false;
    }
    if (!fast && (uint32_t)(now_ms - *last_ms) < STATUS_POLL_MS) {
        return false;
    }
    *last_ms = now_ms;
    return true;
}
