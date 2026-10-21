/**
 * @file
 * @brief The panel's half of the Hitec receive mode.
 *
 * See hitec_rx_link.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_rx_link.h"

#include <stddef.h>
#include <string.h>

#include "link_pages.h"

void hitec_rx_link_init(hitec_rx_link_t *l)
{
    if (l != NULL) {
        memset(l, 0, sizeof(*l));
    }
}

void hitec_rx_link_want(hitec_rx_link_t *l, uint8_t mode)
{
    const uint8_t m = mode == (uint8_t)LINK_HITEC_RX_PRECHARGE
                          ? (uint8_t)LINK_HITEC_RX_PRECHARGE
                          : (uint8_t)LINK_HITEC_RX_PULL_UP;

    if (l == NULL || m == l->want) {
        return;
    }
    l->want = m;
    l->owed = 1;
    l->refused = 0;
    l->waiting = 0;
}

void hitec_rx_link_came_up(hitec_rx_link_t *l, uint16_t minor)
{
    if (l == NULL) {
        return;
    }
    l->up = 1;
    l->minor = minor > 0xFFu ? 0xFFu : (uint8_t)minor;
    l->owed = 1;
    l->refused = 0;
    l->waiting = 0;
}

void hitec_rx_link_lost(hitec_rx_link_t *l)
{
    if (l != NULL) {
        l->up = 0;
        l->waiting = 0;
    }
}

/**
 * @brief Whether the IOmcu has the HITEC RX register (LINK_MINOR_HITEC_RX or
 *         later).
 *
 * @param l The link state.
 * @return true when the IOmcu has the register.
 */
static bool knows_register(const hitec_rx_link_t *l)
{
    return l->minor >= LINK_MINOR_HITEC_RX;
}

bool hitec_rx_link_next(const hitec_rx_link_t *l, uint32_t now_ms,
                        uint16_t *value)
{
    if (l == NULL || value == NULL || l->up == 0u || l->owed == 0u
        || !knows_register(l)
        || (l->waiting != 0u
            && (uint32_t)(now_ms - l->sent_ms) < HITEC_RX_LINK_RETRY_MS)) {
        return false;
    }
    *value = l->want;
    return true;
}

void hitec_rx_link_done(hitec_rx_link_t *l, int result, uint32_t now_ms)
{
    if (l == NULL) {
        return;
    }
    if (result == HITEC_RX_LINK_NO_ANSWER) {
        l->waiting = 1;
        l->sent_ms = now_ms;
        return;
    }
    l->waiting = 0;
    l->owed = 0;
    l->refused = (uint8_t)(result == HITEC_RX_LINK_ACK ? 0u : 1u);
}

hitec_rx_link_mark_t hitec_rx_link_mark(const hitec_rx_link_t *l)
{
    if (l == NULL || l->up == 0u) {
        return HITEC_RX_LINK_HELD;
    }
    if (!knows_register(l)) {
        if (l->want != (uint8_t)LINK_HITEC_RX_PRECHARGE) {
            return HITEC_RX_LINK_HELD;
        }
        return l->minor >= LINK_MINOR_HITEC ? HITEC_RX_LINK_OLD
                                            : HITEC_RX_LINK_NO_LINE;
    }
    if (l->refused != 0u) {
        return HITEC_RX_LINK_REFUSED;
    }
    return l->owed != 0u ? HITEC_RX_LINK_PENDING : HITEC_RX_LINK_HELD;
}
