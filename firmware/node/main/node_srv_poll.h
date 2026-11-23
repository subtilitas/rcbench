/**
 * @file node_srv_poll.h
 * @brief The servo node's poll of the Core2350B: which exchange is due
 *        next, and the start of its UART receiver.  Pure C, under the host suite (test_node_srv_poll).
 *
 * The node asks the IOmcu of board 3 over the UART as the panel asks its
 * own over CAN (Controller Area Network): one request in flight.  The rows
 * and their periods (PLAN section 2.5; the conditional rows follow with the
 * panel's packets):
 *
 * | Row | Exchange | Every |
 * | --- | --- | --- |
 * | NODE_SRV_ROW_CONTROL | write CONTROL ARM and THROTTLE | 50ms |
 * | NODE_SRV_ROW_STATUS | read STATUS, 10 registers | 500ms; 50ms while fast |
 * | NODE_SRV_ROW_POWER | read POWER, 6 registers | 100ms |
 * | NODE_SRV_ROW_SUPPLY | read SUPPLY, 18 registers | 100ms |
 * | NODE_SRV_ROW_WIN_A | read SENSE_WIN_A, 32 registers | 50ms |
 * | NODE_SRV_ROW_WIN_B | read SENSE_WIN_B, 32 registers | 50ms |
 *
 * Every row is due at the start.  A row is due again one period after it
 * was last taken, so a stalled link catches up with one exchange per row,
 * not a burst.  A row with a fast period is due one fast period after it
 * was taken as soon as fast applies.  The first row in the table that is
 * due goes first.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The rows, in the order they go when due together. */
typedef enum {
    NODE_SRV_ROW_CONTROL = 0, /**< write ARM and THROTTLE */
    NODE_SRV_ROW_STATUS,      /**< read STATUS */
    NODE_SRV_ROW_POWER,       /**< read POWER */
    NODE_SRV_ROW_SUPPLY,      /**< read SUPPLY */
    NODE_SRV_ROW_WIN_A,       /**< read SENSE_WIN_A */
    NODE_SRV_ROW_WIN_B,       /**< read SENSE_WIN_B */
    NODE_SRV_ROWS,            /**< the count */
    NODE_SRV_ROW_NONE = -1,   /**< nothing due */
} node_srv_row_t;

/** @brief What a row asks for. */
typedef struct {
    uint8_t  page;      /**< LINK_PAGE_* */
    uint8_t  count;     /**< registers from 0 */
    uint16_t period_ms; /**< its period */
    uint16_t fast_ms;   /**< its period while fast; 0 for none */
} node_srv_row_def_t;

/** @brief The table, indexed by node_srv_row_t. */
extern const node_srv_row_def_t node_srv_rows[NODE_SRV_ROWS];

/** @brief The schedule's state.  The caller writes no field. */
typedef struct {
    uint32_t last_ms[NODE_SRV_ROWS]; /**< when each row was last taken */
    bool     taken[NODE_SRV_ROWS];   /**< it was taken since the start */
} node_srv_poll_t;

/**
 * @brief Every row due.
 * @param p The schedule.
 */
void node_srv_poll_init(node_srv_poll_t *p);

/**
 * @brief The row due first.
 * @param p      The schedule.
 * @param now_ms The clock.
 * @param fast   Rows with a fast period run at it.
 * @return The row, or NODE_SRV_ROW_NONE.
 */
node_srv_row_t node_srv_poll_next(const node_srv_poll_t *p, uint32_t now_ms,
                                  bool fast);

/**
 * @brief A row was taken.
 * @param p      The schedule.
 * @param row    The row; NODE_SRV_ROW_NONE does nothing.
 * @param now_ms The clock when it was taken.
 */
void node_srv_poll_taken(node_srv_poll_t *p, node_srv_row_t row,
                         uint32_t now_ms);

/**
 * @brief Make a row due now: a write that cannot wait for its period.
 * @param p   The schedule.
 * @param row The row; NODE_SRV_ROW_NONE does nothing.
 */
void node_srv_poll_now(node_srv_poll_t *p, node_srv_row_t row);

/**
 * @brief Start the node's UART receiver as after a 0x00, as
 *        link_uart_ring_init() starts the Core2350B's: on a line idle at
 *        the start the first answer is taken (the first all-off write's
 *        ACK), and noise on the line ends as one refused frame at its
 *        0x00.  The caller flushes what the
 *        driver received before it started.
 * @param rx The decoder.
 */
void node_srv_rx_start(link_uart_rx_t *rx);

#ifdef __cplusplus
}
#endif
