/**
 * @file ew_pio.h
 * @brief The numbers and words the PIO (programmable input/output) pin driver
 *        of the signal wire works with.
 *
 * rp2350/ew_line.pio is one half-duplex 8N1 UART (universal asynchronous
 * receiver-transmitter) on one pin.  The clock divider makes one bit
 * EW_PIO_CYCLES_PER_BIT state machine cycles, so a transaction's rate is its
 * divider.  This file computes the dividers and the wait counts, lays a
 * transaction out as words, and turns the received words into a capture.  It
 * has no hardware access, so the host suite runs it against a model of the
 * state machine.
 *
 * The word stream of one transaction, each word shifted out least significant
 * bit first:
 *
 *   count    bits to send, less one
 *   first    ticks to wait for the first start bit
 *   bits     1 bit per bit time, 32 to a word; the rest of the last word unused
 *   gap      ticks to wait for each later start bit
 *
 * A tick is EW_PIO_TICK_CYCLES cycles, 1/8 of a bit: 6.5us at 52us a bit,
 * 3.26us at 38400baud.  The first wait runs from the release, the end of the
 * last stop bit.  A later wait runs from the sample of the stop bit, half a bit
 * before its end, so EW_PIO_HALF_BIT_TICKS are added and it holds from the end
 * of the stop bit.  Both round up and last at most 2 ticks more than asked.
 *
 * Every received byte is one FIFO word; the driver's channel reads its upper
 * half.  In those 16 bits: bits 7 to 14 the data, bit 15 the stop bit as
 * sampled.  The channel takes 2B a byte, 3072B for the longest reply.
 *
 * Receive limits at 16 cycles a bit, computed: a sender's bit time up to 4%
 * off keeps every sample inside its bit, also with no gap between bytes, for
 * any number of bytes.
 *
 * The module includes nothing outside the C standard headers and its own
 * folder, and holds no global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_PIO_H
#define EW_PIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ew_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief State machine cycles in one bit. */
#define EW_PIO_CYCLES_PER_BIT 16u
/** @brief Cycles of one tick of the receiver's wait. */
#define EW_PIO_TICK_CYCLES 2u
/** @brief Half a bit, in ticks: the sample of the stop bit to its end. */
#define EW_PIO_HALF_BIT_TICKS 4u
/** @brief The rate of the text line, baud. */
#define EW_PIO_TEXT_BAUD 38400u
/**
 * @brief Smallest clock divider: the input synchroniser's 2 system clock cycles
 *        are then half a state machine cycle or less.
 */
#define EW_PIO_MIN_DIV 4u
/** @brief Largest bit time error a clock may give, ppm: 0.05%. */
#define EW_PIO_MAX_ERROR_PPM 500u
/** @brief Words of the program, under the 32 of a PIO block. */
#define EW_PIO_PROGRAM_WORDS 25u
/** @brief The `pull` the machine stalls on between transactions. */
#define EW_PIO_IDLE_PC 0u
/** @brief The `set pindirs, 0` that releases the pad. */
#define EW_PIO_RELEASE_PC 10u
/** @brief The first instruction of the wait for the idle level. */
#define EW_PIO_WAIT_HI_PC 11u
/** @brief The first instruction of the wait for a start bit. */
#define EW_PIO_WAIT_LO_PC 15u
/** @brief The words of the longest transaction. */
#define EW_PIO_TX_WORDS (3u + (EW_TX_MAX * 10u + EW_PAUSE_BITS_MAX + 31u) / 32u)
/** @brief The most words a transaction receives. */
#define EW_PIO_RX_WORDS EW_RX_TEXT_MAX
/**
 * @brief The driver ends a transaction whose state machine still runs this long
 *        after ew_xfer_span_us(), inside the session's own limit, us.
 */
#define EW_PIO_GRACE_US 1000u

/** @brief The divider of one rate at one system clock. */
typedef struct {
    uint32_t sys_hz;  /**< the system clock                      */
    uint32_t div256;  /**< the divider times 256                 */
    uint32_t err_ppm; /**< the bit time's error, rounded up      */
} ew_pio_clock_t;

/** @brief One transaction as the driver keeps it. */
typedef struct {
    uint32_t n_words;     /**< words of the stream                     */
    uint32_t first_ticks; /**< the first wait, ticks                   */
    uint32_t gap_ticks;   /**< the later wait, ticks                   */
    uint32_t span_us;     /**< ew_xfer_span_us() of the transaction    */
    uint16_t max_rx;      /**< bytes to receive                        */
    uint8_t  rate;        /**< an ew_rate_t                            */
    uint8_t *rx;          /**< takes the bytes                         */
} ew_pio_run_t;

/**
 * @brief The divider for a rate: 487 + 128/256 for 52us a bit and 244 + 36/256
 *        for 38400baud at 150MHz.
 * @param sys_hz the system clock, Hz
 * @param rate   an ew_rate_t
 * @param out    takes the divider
 * @return false for a clock that gives a divider below EW_PIO_MIN_DIV or an
 *         error above EW_PIO_MAX_ERROR_PPM; no clock of 32 bits reaches the
 *         divider's upper limit of 65535
 */
bool ew_pio_clock(uint32_t sys_hz, uint8_t rate, ew_pio_clock_t *out);

/**
 * @brief The bit time a divider gives.
 * @param clock the divider
 * @return the time in ns, rounded
 */
uint32_t ew_pio_bit_ns(const ew_pio_clock_t *clock);

/**
 * @brief A wait in ticks, rounded up.
 * @param clock the divider
 * @param us    the wait, us, up to EW_FIRST_US_MAX
 * @return the ticks
 */
uint32_t ew_pio_ticks(const ew_pio_clock_t *clock, uint32_t us);

/**
 * @brief Lay a transaction out as words.
 * @param clock the divider of the transaction's rate
 * @param x     the transaction
 * @param words takes up to EW_PIO_TX_WORDS words
 * @param run   takes what the driver keeps
 * @return the words of the stream, 0 for a transaction ew_xfer_valid() refuses
 */
uint32_t ew_pio_stream(const ew_pio_clock_t *clock, const ew_xfer_t *x,
                       uint32_t *words, ew_pio_run_t *run);

/**
 * @brief Whether a transaction has run past its time.
 * @param run the transaction
 * @param now the clock now, us; it may have wrapped
 * @param t0  the clock at its start, us
 * @return true more than ew_xfer_span_us() + EW_PIO_GRACE_US after @p t0
 */
bool ew_pio_late(const ew_pio_run_t *run, uint32_t now, uint32_t t0);

/**
 * @brief Turn the received words into the bytes and the capture.
 * @param run     the transaction; its bytes go to @c run->rx
 * @param words   the channel's words
 * @param n       words the channel wrote, held to @c run->max_rx
 * @param stalled the machine found the receive FIFO full
 * @param cut     the driver ended it past its time
 * @param out     takes the capture
 *
 * A stall with every byte in counts for nothing: the bytes after the last have
 * no place and no use.
 */
void ew_pio_capture(const ew_pio_run_t *run, const uint16_t *words, uint32_t n,
                    bool stalled, bool cut, ew_capture_t *out);

#ifdef __cplusplus
}
#endif

#endif /* EW_PIO_H */
