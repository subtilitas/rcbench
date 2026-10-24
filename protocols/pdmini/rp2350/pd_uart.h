/**
 * @file
 * @brief The PD mini's UART on two PIO (programmable input/output) state
 *        machines, on whichever two pins the caller names.
 *
 * Implements the UART protocol of the WeAct PD Power Mini V1 Buck module.
 * WeAct is a name or trademark of its owner; it is used here only to say
 * which module this driver works with (NOTICE, "Trademarks").
 *
 * pd_uart_open() claims the state machines and leaves the pins at rest:
 * inputs with a pull-down.  The driver attaches the UART for a transaction
 * and detaches it after; pdmini_io_t's callbacks are the three below.
 *
 * Received bytes wait in the receive machine's FIFO, 8 deep, until the main
 * loop takes them: 4.2ms of reply at 19200 baud, 0.17ms at 460800.  A
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

/**
 * @brief Claim the state machines for @p tx and @p rx at @p baud.
 *
 * False, and nothing held, when no PIO block can reach the pins or has room.
 *
 * @param tx    GPIO number of the transmit pin.
 * @param rx    GPIO number of the receive pin.
 * @param baud  Baud rate.
 * @return      true when both state machines are claimed.
 */
bool pd_uart_open(uint8_t tx, uint8_t rx, uint32_t baud);

/**
 * @brief Another rate for both machines, between transactions only: nothing
 *        happens while the UART is attached.
 *
 * @param baud  Baud rate.
 */
void pd_uart_baud(uint32_t baud);

/** @brief Release them; the pins stay at rest. */
void pd_uart_close(void);

/**
 * @brief Attach the UART to the pins for one transaction.
 *
 * @param ctx  Unused; passed by the driver's callback table.
 */
void pd_uart_attach(void *ctx);
/**
 * @brief Detach the UART; the pins go back to pulled-down inputs.
 *
 * @param ctx  Unused; passed by the driver's callback table.
 */
void pd_uart_detach(void *ctx);
/**
 * @brief Send bytes on the attached UART.
 *
 * @param ctx  Unused; passed by the driver's callback table.
 * @param p    Bytes to send.
 * @param n    Number of bytes in @p p.
 */
void pd_uart_send(void *ctx, const uint8_t *p, size_t n);

/**
 * @brief A received byte, or false for none.
 *
 * @param b  Receives the byte when true is returned.
 * @return   true when a byte was taken from the FIFO.
 */
bool pd_uart_getc(uint8_t *b);

#ifdef __cplusplus
}
#endif
