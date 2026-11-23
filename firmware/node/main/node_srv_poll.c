/**
 * @file node_srv_poll.c
 * @brief The servo node's poll of the Core2350B (node_srv_poll.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "node_srv_poll.h"

#include "link_pages.h"
#include "mstime.h"

const node_srv_row_def_t node_srv_rows[NODE_SRV_ROWS] = {
    [NODE_SRV_ROW_CONTROL] = { LINK_PAGE_CONTROL, 2u, 50u, 0u },
    [NODE_SRV_ROW_STATUS]  = { LINK_PAGE_STATUS, LINK_ST_COUNT, 500u, 50u },
    [NODE_SRV_ROW_POWER]   = { LINK_PAGE_POWER, LINK_PW_COUNT, 100u, 0u },
    [NODE_SRV_ROW_SUPPLY]  = { LINK_PAGE_SUPPLY, LINK_SP_COUNT, 100u, 0u },
    [NODE_SRV_ROW_WIN_A]   = { LINK_PAGE_SENSE_WIN_A, LINK_MAX_REGS, 50u, 0u },
    [NODE_SRV_ROW_WIN_B]   = { LINK_PAGE_SENSE_WIN_B, LINK_MAX_REGS, 50u, 0u },
};

/**
 * @brief A row's period now.
 * @param row  The row.
 * @param fast Whether fast periods apply.
 * @return ms.
 */
static uint32_t period_of(node_srv_row_t row, bool fast)
{
    const node_srv_row_def_t *d = &node_srv_rows[row];
    return fast && d->fast_ms != 0u ? d->fast_ms : d->period_ms;
}

void node_srv_poll_init(node_srv_poll_t *p)
{
    for (unsigned k = 0; k < (unsigned)NODE_SRV_ROWS; ++k) {
        p->last_ms[k] = 0u;
        p->taken[k] = false;
    }
}

node_srv_row_t node_srv_poll_next(const node_srv_poll_t *p, uint32_t now_ms,
                                  bool fast)
{
    for (unsigned k = 0; k < (unsigned)NODE_SRV_ROWS; ++k) {
        const node_srv_row_t row = (node_srv_row_t)k;
        if (!p->taken[k]
            || mstime_elapsed(now_ms, p->last_ms[k], period_of(row, fast))) {
            return row;
        }
    }
    return NODE_SRV_ROW_NONE;
}

void node_srv_poll_taken(node_srv_poll_t *p, node_srv_row_t row,
                         uint32_t now_ms)
{
    if (row >= 0 && row < NODE_SRV_ROWS) {
        p->last_ms[row] = now_ms;
        p->taken[row] = true;
    }
}

void node_srv_rx_start(link_uart_rx_t *rx)
{
    link_uart_rx_init(rx);
    link_can_frame_t none;
    (void)link_uart_feed(rx, LINK_UART_DELIM, &none, NULL);
}

void node_srv_poll_now(node_srv_poll_t *p, node_srv_row_t row)
{
    if (row >= 0 && row < NODE_SRV_ROWS) {
        p->taken[row] = false;
    }
}
