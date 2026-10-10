/*
 * The PD mini's UART on two PIO (programmable input/output) state
 * machines, on whichever two pins the caller names.
 *
 * pd_uart_open() claims the state machines and leaves the pins at rest:
 * inputs with a pull-down.  The driver attaches the UART for a transaction
 * and detaches it after; pdmini_io_t's callbacks are the three below.
 *
 * Received bytes wait in the receive machine's FIFO, 8 deep, until the main
 * loop takes them: 4.2 ms of reply at 19200 baud, 0.17 ms at 460800.  A
 * pass longer than that loses bytes, and the transaction fails and is
 * tried again.  The pass time is not measured.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Claim the state machines for @p tx and @p rx at @p baud.  False, and
 *  nothing held, when no PIO block can reach the pins or has room. */
bool pd_uart_open(uint8_t tx, uint8_t rx, uint32_t baud);

/** Another rate for both machines, between transactions only: nothing
 *  happens while the UART is attached. */
void pd_uart_baud(uint32_t baud);

/** Release them; the pins stay at rest. */
void pd_uart_close(void);

void pd_uart_attach(void *ctx);
void pd_uart_detach(void *ctx);
void pd_uart_send(void *ctx, const uint8_t *p, size_t n);

/** A received byte, or false for none. */
bool pd_uart_getc(uint8_t *b);

#ifdef __cplusplus
}
#endif
