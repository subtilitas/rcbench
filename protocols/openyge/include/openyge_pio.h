/**
 * @file openyge_pio.h
 * @brief The numbers and words the PIO (programmable input/output) pin driver
 *        of programming for YGE ESCs works with.
 *
 * YGE is a name or trademark of its owner; it is used here only to say which
 * ESCs (electronic speed controllers) this module works with.
 *
 * rp2350/oy_line.pio is one half-duplex UART (universal asynchronous
 * receiver-transmitter) at 115200 baud, 8N1, on one pin.  The clock divider
 * makes one bit OY_PIO_CYCLES_PER_BIT state machine cycles: 81 + 97/256 at
 * 150MHz, a bit time of 8680ns, 16ppm (0.002%) off.  This file computes the divider,
 * lays a frame out as words, and turns the received words into bytes.  It has
 * no hardware access, so the host suite runs it against a model of the state
 * machine.
 *
 * The program receives until a stream is in its transmit FIFO.  It looks for a
 * stream only while the line is high between bytes, so a byte being received
 * is taken whole first.  It then drives the pad high, sends the frame push-pull
 * and releases the pad OY_PIO_CYCLES_PER_BIT cycles after the last stop bit's
 * edge, at the end of the stop bit.  It receives again from the next cycle.
 * The receiver does not run while the frame goes out, so the frame's own bytes
 * are not received: there is no echo to remove.
 *
 * The word stream of one frame, each word shifted out least significant bit
 * first:
 *
 *   count    bits to send, less one: 10 n - 1 for n bytes
 *   bits     1 bit per bit time, 32 to a word, each byte as start bit 0, the
 *            8 data bits least significant first, stop bit 1; the rest of
 *            the last word unused
 *
 * Every received byte is one FIFO word; the driver's channel reads its upper
 * half.  In those 16 bits: bits 7 to 14 the data, bit 15 the stop bit as
 * sampled (1 the idle level, 0 a framing error).  The channel writes
 * OY_PIO_RX_ENTRIES of them, 2B each, before it stops; the machine drops the
 * words it cannot push after that.
 *
 * Receive limits at 16 cycles a bit, computed: the receiver sees a start bit
 * within 3 cycles of its edge and samples each bit 22 + 16 k cycles after
 * that, the stop bit at 150.  A sender's bit time up to 4% off keeps every
 * sample inside its bit, with no gap between bytes, for any number of bytes.
 * A low shorter than 8 cycles is no start bit.
 *
 * The module includes nothing outside the C standard headers and its own
 * folder, and holds no global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef OPENYGE_PIO_H
#define OPENYGE_PIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openyge.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief State machine cycles in one bit. */
#define OY_PIO_CYCLES_PER_BIT 16u
/** @brief The wire's rate, baud (docs/OpenYGE.md section 1). */
#define OY_PIO_BAUD 115200u
/**
 * @brief Smallest clock divider: the input synchroniser's 2 system clock cycles
 *        are then half a state machine cycle or less.
 */
#define OY_PIO_MIN_DIV 4u
/** @brief Largest bit time error a clock may give, ppm: 0.05%. */
#define OY_PIO_MAX_ERROR_PPM 500u
/** @brief Words of the program, under the 32 of a PIO block. */
#define OY_PIO_PROGRAM_WORDS 25u
/** @brief The `mov x, status` of the line high: the program's first word. */
#define OY_PIO_IDLE_HI_PC 0u
/** @brief The `jmp pin` that looks for a start bit: the wrap target. */
#define OY_PIO_WAIT_LO_PC 2u
/** @brief The first word of the frame's way out: the `pull` of the count. */
#define OY_PIO_SEND_PC 13u
/** @brief The `set pindirs, 0` that releases the pad. */
#define OY_PIO_RELEASE_PC 21u
/** @brief The `in y, 1` of a high stop bit. */
#define OY_PIO_STOP_HIGH_PC 23u
/** @brief Cycles from the sample that saw a start bit to data bit 0's. */
#define OY_PIO_FIRST_SAMPLE 22u
/** @brief Cycles from the sample that saw a start bit to the stop bit's. */
#define OY_PIO_STOP_SAMPLE 150u
/**
 * @brief Cycles from the sample that saw a start bit to the first sample for
 *        the next one, after a high stop bit.
 */
#define OY_PIO_NEXT_SAMPLE 153u
/** @brief The longest frame sent, bytes: the longest frame there is. */
#define OY_PIO_TX_MAX OPENYGE_MAX_FRAME
/** @brief The words of the longest frame's stream. */
#define OY_PIO_TX_WORDS (1u + (OY_PIO_TX_MAX * 10u + 31u) / 32u)
/** @brief The entries the receive channel writes before it stops: 256 bytes. */
#define OY_PIO_RX_ENTRIES 256u
/** @brief Where a received entry keeps the data. */
#define OY_PIO_RX_DATA_SHIFT 7u
/** @brief Where a received entry keeps the stop bit. */
#define OY_PIO_RX_STOP_SHIFT 15u
/**
 * @brief The driver drops a stream still not sent this long after
 *        oy_pio_span_us(), us: the line was held low all that time.
 */
#define OY_PIO_GRACE_US 1000u

/** @brief The divider at one system clock. */
typedef struct {
    uint32_t sys_hz;  /**< the system clock, Hz                   */
    uint32_t div256;  /**< the divider times 256                  */
    uint32_t err_ppm; /**< the bit time's error, ppm, rounded up  */
} oy_pio_clock_t;

/** @brief The receive side's count of what it handed over. */
typedef struct {
    uint32_t rd;      /**< entries handed over since the channel's start  */
    uint32_t bytes;   /**< bytes handed over since the reset             */
    uint32_t framing; /**< of those, bytes with a low stop bit           */
    uint32_t full;    /**< times the channel stopped at its last entry   */
    bool     filled;  /**< the channel stopped at OY_PIO_RX_ENTRIES      */
} oy_pio_rx_t;

/**
 * @brief The divider at a system clock: 81 + 97/256 at 150MHz.
 * @param sys_hz the system clock, Hz
 * @param out    takes the divider; left as it was on false
 * @return false for a clock that gives a divider below OY_PIO_MIN_DIV or an
 *         error above OY_PIO_MAX_ERROR_PPM; no clock of 32 bits reaches the
 *         divider's upper limit of 65535
 */
bool oy_pio_clock(uint32_t sys_hz, oy_pio_clock_t *out);

/**
 * @brief The bit time a divider gives.
 * @param clock the divider
 * @return the time in ns, rounded
 */
uint32_t oy_pio_bit_ns(const oy_pio_clock_t *clock);

/**
 * @brief Lay a frame out as words.
 * @param frame the bytes
 * @param n     how many, 1 to OY_PIO_TX_MAX
 * @param words takes up to OY_PIO_TX_WORDS words
 * @return the words of the stream; 0 for a NULL, no bytes or more than
 *         OY_PIO_TX_MAX, with nothing written
 */
uint32_t oy_pio_stream(const uint8_t *frame, size_t n, uint32_t *words);

/**
 * @brief The longest time from a stream's hand-over to the pad's release, with
 *        the line idle or carrying bytes at 4% off at most: a byte being
 *        received, the drive before the first bit, @p n bytes and the release.
 * @param clock the divider
 * @param n     bytes of the frame, up to OY_PIO_TX_MAX
 * @return the time in us, rounded up
 */
uint32_t oy_pio_span_us(const oy_pio_clock_t *clock, size_t n);

/**
 * @brief Whether a stream handed over at @p t0 is past its time.
 * @param span_us oy_pio_span_us() of the stream
 * @param now     the clock now, us; it may have wrapped
 * @param t0      the clock at the hand-over, us
 * @return true more than @p span_us + OY_PIO_GRACE_US after @p t0
 */
bool oy_pio_late(uint32_t span_us, uint32_t now, uint32_t t0);

/**
 * @brief Whether the state machine is on the frame's way out.
 * @param pc the state machine's address, counted from the program's first word
 * @return true from OY_PIO_SEND_PC to OY_PIO_RELEASE_PC
 */
bool oy_pio_sending(unsigned pc);

/**
 * @brief Clear the counts, for a channel started at entry 0.
 * @param r the receive side
 */
void oy_pio_rx_reset(oy_pio_rx_t *r);

/**
 * @brief Hand over the entries the channel has written and nobody has taken.
 *
 * Each entry becomes one byte; a low stop bit counts in @c framing and the byte
 * is handed over all the same, for the frame's CRC (cyclic redundancy check)
 * to judge.  The first call that finds every entry written sets @c filled and
 * counts one @c full.
 *
 * @param r       the receive side
 * @param ent     the channel's entries
 * @param written entries the channel has written, held to OY_PIO_RX_ENTRIES
 * @param buf     takes the bytes
 * @param cap     room at @p buf
 * @return the bytes written to @p buf
 */
size_t oy_pio_take(oy_pio_rx_t *r, const uint16_t *ent, uint32_t written,
                   uint8_t *buf, size_t cap);

/**
 * @brief Where the channel starts again, called with the channel stopped and
 *        @p written read after the stop.
 *
 * Every entry handed over: entry 0, and the counts of the next run start.
 * Entries still to hand over: entry @p written, the rest of the buffer.
 *
 * @param r       the receive side
 * @param written entries the channel has written
 * @param drop    takes true when the buffer had filled: the words still in
 *                the FIFO came after a gap and are dropped
 * @return the entry to start at; OY_PIO_RX_ENTRIES leaves the channel stopped
 */
uint32_t oy_pio_rearm(oy_pio_rx_t *r, uint32_t written, bool *drop);

#ifdef __cplusplus
}
#endif

#endif /* OPENYGE_PIO_H */
