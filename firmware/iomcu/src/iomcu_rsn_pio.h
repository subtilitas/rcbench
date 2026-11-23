/**
 * @file
 * @brief Where a PIO program goes on the remote servo node (board 3): the
 *        placement of shared/outputs/rsn_pio.h on the SDK's claim calls.
 *
 * Each driver bind that claims a PIO state machine sits between
 * place_begin() and place_end(): an output (DShot, bidirectional DShot,
 * PPM) in outputs_hw.c, a session's line (KST, Hitec, ESC wire), and the
 * PD mini UART through pd_open().  On boards 1 and 2 place_begin() is true
 * and the rest do nothing: the SDK's own search places every program, as
 * before.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_IOMCU_RSN_PIO_H
#define RCBENCH_IOMCU_RSN_PIO_H

#include <stdbool.h>
#include <stdint.h>

#include "rsn_pio.h"

/**
 * @brief Every PIO block's GPIO base at 16, before any program is loaded;
 *        nothing placed.  Board 3 only.
 */
void place_boot(void);

/**
 * @brief The fence for one bind (rsn_pio_begin()).
 *
 * @param use What is bound, rsn_pio_use_t.
 * @param pin The output's GPIO; any GPIO for a line or the fixed load.
 * @return true when the bind may go ahead, and place_end() follows it;
 *         false when no block may take it (nothing to undo).  Always true
 *         on boards 1 and 2.
 */
bool place_begin(rsn_pio_use_t use, uint8_t pin);

/**
 * @brief After the bind: the fence let go, the output or line recorded on
 *        @p ok (rsn_pio_end()).
 *
 * @param ok Whether the driver bound.
 */
void place_end(bool ok);

/**
 * @brief An output's driver released its pin (rsn_pio_gone()).
 *
 * @param pin The GPIO.
 */
void place_gone(uint8_t pin);

/**
 * @brief The session's line closed (rsn_pio_line_gone()).
 */
void place_line_gone(void);

/**
 * @brief pd_uart_open() on the SUPPLY page's pins and rate, into PIO0 on
 *        board 3 (RSN_PIO_FIXED).
 *
 * @return pd_uart_open()'s answer; false on board 3 also when PIO0 is
 *         refused.
 */
bool pd_open(void);

#endif /* RCBENCH_IOMCU_RSN_PIO_H */
