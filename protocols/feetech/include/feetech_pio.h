/**
 * @file feetech_pio.h
 * @brief The numbers and words a PIO pin driver for the Feetech bus works
 *        with.
 *
 * Implements the serial bus protocol of Feetech SCS and STS servos.
 * Feetech is a name or trademark of its owner; it is used here only to say
 * which servos this module works with (README.md, "Trademarks").
 *
 * rp2350/ft_line.pio is one PIO (programmable input/output) program that
 * sends a frame on one pin as 8N1 bytes, releases the pin after the last
 * stop bit and receives 8N1 bytes on the same pin until it is restarted.
 * A bit is FEETECH_PIO_CYCLES_PER_BIT cycles of the state machine; the
 * clock divider makes them one bit time of the baud rate.  This file
 * computes the divider, lays a frame out as the words the program reads
 * and turns the words it pushes into bytes.  It has no hardware access, so
 * the host suite runs it against a model of the state machine.
 *
 * The word stream of one transaction, in the order the program reads it:
 *
 *   count     bytes to send, less 1
 *   bytes     one word each, the byte in bits 0 to 7
 *
 * Each word the program pushes holds one received byte: data bit 0 in bit
 * 23 to data bit 7 in bit 30, and the stop bit as sampled in bit 31.
 *
 * The module includes nothing outside the C standard headers and holds no
 * global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FEETECH_PIO_H
#define FEETECH_PIO_H

#include <stdbool.h>
#include <stdint.h>

#include "feetech_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** State machine cycles a bit takes. */
#define FEETECH_PIO_CYCLES_PER_BIT 8u
/** Largest error of the bit time a divider may give, in parts per
 *  million: 0.5 %. */
#define FEETECH_PIO_MAX_ERROR_PPM 5000u
/** Words of the longest stream: the count and FEETECH_FRAME_MAX bytes. */
#define FEETECH_PIO_TX_WORDS (FEETECH_FRAME_MAX + 1u)
/** State machine cycles from the start of the program to the first start
 *  bit; the pin drives high for the last FEETECH_PIO_HIGH_CYCLES of
 *  them. */
#define FEETECH_PIO_LEAD_CYCLES 14u
#define FEETECH_PIO_HIGH_CYCLES 10u
/** State machine cycles between the stop bit of one byte and the start bit
 *  of the next, beyond the stop bit's 8. */
#define FEETECH_PIO_GAP_CYCLES 2u

/** The divider for one baud rate on one system clock. */
typedef struct {
    uint32_t sys_hz;
    uint32_t baud;
    uint16_t div_int;     /**< whole part, 1 to 65535               */
    uint8_t  div_frac;    /**< 256ths                               */
    uint32_t error_ppm;   /**< of the bit time it gives             */
} feetech_pio_clock_t;

/**
 * The divider for @p baud at @p sys_hz, rounded to the nearest 256th.
 * False for 0 in either, a divider below 1 or above 65535, and an error
 * above FEETECH_PIO_MAX_ERROR_PPM.  Every Feetech rate from 38,400 to
 * 1,000,000 baud is within 0.01 % at 150 MHz and 125 MHz.
 */
bool feetech_pio_clock(uint32_t sys_hz, uint32_t baud,
                       feetech_pio_clock_t *out);

/** The word stream for @p n bytes; returns the words written, n + 1, or 0
 *  for no byte or more than FEETECH_FRAME_MAX. */
unsigned feetech_pio_tx(const uint8_t *bytes, unsigned n, uint32_t *words);

/** The byte of a pushed word; false when its stop bit was low. */
bool feetech_pio_rx(uint32_t word, uint8_t *byte);

/** Microseconds from the start of the program to the release of the pin
 *  for a stream of @p n bytes, rounded up. */
uint32_t feetech_pio_tx_us(const feetech_pio_clock_t *clock, unsigned n);

#ifdef __cplusplus
}
#endif

#endif /* FEETECH_PIO_H */
