/**
 * @file
 * @brief The QR scanner's port: the bytes of its UART through the framer
 *        (scan_line.h) to the sequencer (gm861.h) and to a queue of code
 *        lines, and the sequencer's requests and rate back to the UART.
 *
 * The panel's `scan` task owns one port and the UART.  Each pass it hands
 * the bytes read to scan_port_rx(), a framing or parity error to
 * scan_port_error(), lost input to scan_port_flush(), then calls
 * scan_port_step() and writes the bytes it returns, after re-opening the UART at the rate it
 * names when that rate changed.  The frame task takes the lines with
 * scan_port_take() (under the caller's lock).
 *
 * - An answer frame goes to gm861_answer() and nowhere else.
 * - A code line, or a line past SCAN_LINE_MAX with its first SCAN_LINE_MAX
 *   bytes, goes to a queue of SCAN_PORT_QUEUE events.  A full queue drops
 *   its oldest event and counts it in drops.
 * - Lost input (a FIFO or buffer overflow, a flush) and a UART error drop
 *   the open line or frame and every byte after it until SCAN_GAP_MS
 *   (100ms) pass without one: the bad byte, read already or still to come,
 *   falls in what is dropped, and no prefix joins a later suffix.  A line
 *   that follows within 100ms of the burst is dropped with it.
 * - Stopped, the port sends nothing, takes no byte and keeps its queue.
 *
 * Pure C, no SDK.  Host-tested in test_scan_port.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gm861.h"
#include "scan_line.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Events the line queue holds. */
#define SCAN_PORT_QUEUE 4u

/** @brief What an event carries. */
typedef enum {
    SCAN_EV_LINE = 1, /**< a code line: text */
    SCAN_EV_LONG      /**< a line past SCAN_LINE_MAX: its first SCAN_LINE_MAX bytes */
} scan_ev_kind_t;

/** @brief One line for the frame task. */
typedef struct {
    uint8_t  kind;                     /**< scan_ev_kind_t */
    uint32_t at_ms;                    /**< when its end arrived */
    char     text[SCAN_LINE_MAX + 1u]; /**< NUL ended */
} scan_event_t;

/** @brief The port.  Fields are private but for the tests. */
typedef struct {
    scan_line_t  line;                   /**< the framer */
    gm861_t      g;                      /**< the sequencer */
    bool         on;                     /**< started */
    uint32_t     baud;                   /**< the rate the UART is open at, 0 closed */
    scan_event_t q[SCAN_PORT_QUEUE];     /**< the lines not taken */
    uint8_t      q_head;                 /**< the oldest of them */
    uint8_t      q_n;                    /**< their count */
    uint32_t     drops;                  /**< events a full queue dropped */
    uint32_t     errors;                 /**< UART errors */
    bool         discard;                /**< input was lost: bytes are dropped until SCAN_GAP_MS of quiet */
    uint32_t     quiet_from;             /**< the flush or the last byte dropped since */
    uint32_t     flushes;                /**< losses of input: flushes and UART errors */
    uint32_t     discarded;              /**< bytes dropped after them */
} scan_port_t;

/**
 * @brief Clear: stopped, the queue empty, every count 0.
 * @param p The port; NULL does nothing.
 */
void scan_port_init(scan_port_t *p);

/**
 * @brief Start the framer and the sequencer at @p mode; the queue stays.
 *        The next scan_port_step() asks for the UART at 9600 baud.
 * @param p    The port; NULL does nothing.
 * @param mode The read mode (gm861_mode_t).
 */
void scan_port_start(scan_port_t *p, gm861_mode_t mode);

/**
 * @brief Stop: the sequencer OFF, nothing more sent; the UART is the
 *        caller's to close.
 * @param p The port; NULL does nothing.
 */
void scan_port_stop(scan_port_t *p);

/**
 * @brief Change the read mode of a started port (gm861_set_mode()).
 * @param p    The port; NULL or stopped does nothing.
 * @param mode The read mode.
 */
void scan_port_set_mode(scan_port_t *p, gm861_mode_t mode);

/**
 * @brief Take bytes read from the UART.
 * @param p      The port; NULL or stopped takes nothing.
 * @param b      The bytes; NULL takes nothing.
 * @param n      Their count.
 * @param now_ms The millisecond clock.
 */
void scan_port_rx(scan_port_t *p, const uint8_t *b, size_t n, uint32_t now_ms);

/**
 * @brief The UART received a byte with a framing or parity error: counted,
 *        and taken as lost input (scan_port_flush()).
 * @param p      The port; NULL or stopped does nothing.
 * @param now_ms The millisecond clock.
 */
void scan_port_error(scan_port_t *p, uint32_t now_ms);

/**
 * @brief UART input was lost (a FIFO or buffer overflow, the input flushed):
 *        the open line or frame is dropped, and every byte after it until
 *        SCAN_GAP_MS pass without one.  No part of a line around the loss
 *        is queued or answers the sequencer.
 * @param p      The port; NULL or stopped does nothing.
 * @param now_ms The millisecond clock.
 */
void scan_port_flush(scan_port_t *p, uint32_t now_ms);

/**
 * @brief One pass: the framer's gap, then the sequencer's step.
 * @param p       The port; NULL or stopped sends nothing.
 * @param now_ms  The millisecond clock.
 * @param rx_high The RX pin reads high.
 * @param tx      Receives the bytes to send, after the re-open.
 * @param tx_max  Room in @p tx.
 * @param reopen  Receives the rate to open the UART at when it differs
 *                from the rate open, else 0; may be NULL.  The open line
 *                or frame, read at the old rate, is dropped then; the
 *                caller flushes the UART's input as it re-opens.
 * @return Bytes to send, 0 or GM861_REQ_LEN.
 */
size_t scan_port_step(scan_port_t *p, uint32_t now_ms, bool rx_high,
                      uint8_t *tx, size_t tx_max, uint32_t *reopen);

/**
 * @brief Take the oldest line.
 * @param p   The port.
 * @param out Receives it.
 * @return false when none waits, or for NULL.
 */
bool scan_port_take(scan_port_t *p, scan_event_t *out);

/**
 * @brief Queue @p text as a code line, as if the scanner had sent it: the
 *        console build's `scan <text>`.  Printable ASCII only, 1 to
 *        SCAN_LINE_MAX bytes.
 * @param p      The port.
 * @param text   The text.
 * @param now_ms The millisecond clock.
 * @return false, and nothing queued, for NULL, an empty text, one past
 *         SCAN_LINE_MAX or a byte outside 0x20 to 0x7E.
 */
bool scan_port_inject(scan_port_t *p, const char *text, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
