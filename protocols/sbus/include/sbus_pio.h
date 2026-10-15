/**
 * @file sbus_pio.h
 * @brief The numbers and words a PIO pin driver for S.BUS works with.
 *
 * Implements the S.BUS and S.BUS2 protocols of Futaba.  Futaba, S.BUS and
 * S.BUS2 are names or trademarks of their respective owners; they are used
 * here only to say which servos and sensors this module works with (NOTICE,
 * "Trademarks").
 *
 * rp2350/sbus_line.pio is one PIO (programmable input/output) program that
 * sends a frame as an 8E2 UART (universal asynchronous
 * receiver-transmitter) at 100 kbaud, releases the pad, reads every byte of
 * a listening window with a stamp and drives the pad again.  It takes its
 * counts and bits from the transmit FIFO and gives one word per received
 * byte to the receive FIFO.  This file computes the clock divider, lays a
 * frame out as the words the program reads and turns the received words
 * into an sbus2_capture_t.  It has no hardware access, so the host suite
 * runs it against a model of the state machine.
 *
 * The program runs at 8 cycles per bit.  Its polarity is a plain UART's,
 * idle 1; the pin driver inverts the pad in both directions with the GPIO
 * (general-purpose input/output) overrides, which gives the S.BUS wire:
 * idle low, start bit high.
 *
 * The word stream of one transaction, in the order the program reads it:
 *
 *   count     bits clocked out, less 1
 *   bits      1 to 10 words, least significant bit first, one bit 8 cycles
 *   window    samples the program listens for, 0 for no listening
 *
 * Each byte is 12 bits: a start bit 0, 8 data bits least significant first,
 * even parity and 2 stop bits 1.  Idle bits 1 in front fill the first word,
 * so the bits end on a word boundary: a frame of 25 bytes is 300 bits behind
 * 20 idle bits, and starts 200 us after the stream is handed over.
 *
 * A received word holds, from bit 0: the 8 data bits, the parity bit, the
 * first stop bit, then the program's sample counter at the start bit in
 * 22 bits.  The program takes a sample every SBUS_PIO_SAMPLE_CYCLES cycles
 * while the pin idles and counts it; a byte takes SBUS_PIO_BYTE_CYCLES from
 * the sample that saw its start bit to the next sample and counts 1.  The
 * time of a byte follows from the counter and the number of bytes before it.
 *
 * The module includes nothing outside the C standard headers and its own
 * folder, and holds no global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SBUS_PIO_H
#define SBUS_PIO_H

#include <stdbool.h>
#include <stdint.h>

#include "sbus2.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SBUS_PIO_BAUD         100000u
/** PIO cycles per bit. */
#define SBUS_PIO_BIT_CYCLES   8u
/** Bits per byte on the wire: start, 8 data, parity, 2 stop. */
#define SBUS_PIO_BYTE_BITS    12u
/** Cycles between two samples while the pin idles. */
#define SBUS_PIO_SAMPLE_CYCLES 2u
/** Cycles a received byte takes, from the sample that saw its start bit to
 *  the next sample. */
#define SBUS_PIO_BYTE_CYCLES  94u
/** Cycles from the end of the last stop bit sent to the release of the pad. */
#define SBUS_PIO_RELEASE_CYCLES 2u
/** Cycles from the end of the last stop bit sent to the first sample. */
#define SBUS_PIO_FIRST_SAMPLE 3u
/** Bits of the sample counter a received word carries. */
#define SBUS_PIO_STAMP_BITS   22u
/** Longest window, in samples: the counter's range. */
#define SBUS_PIO_MAX_SAMPLES  ((1u << SBUS_PIO_STAMP_BITS) - 1u)
/** Bytes one stream sends at most: a frame. */
#define SBUS_PIO_MAX_BYTES    25u
/** Words of the longest stream: count, 10 bit words, window. */
#define SBUS_PIO_TX_WORDS     12u
/** Words a transaction keeps: a full capture and one that shows an
 *  overflow. */
#define SBUS_PIO_RX_WORDS     (SBUS2_CAPTURE_MAX + 1u)
/** Largest bit time error a clock may give, in parts per million. */
#define SBUS_PIO_MAX_ERROR_PPM 1000u

/** The divider a system clock gives. */
typedef struct {
    uint32_t sys_hz;
    uint16_t div_int;      /**< integer part */
    uint8_t  div_frac;     /**< 1/256 parts */
    uint32_t cycle_ps;     /**< one PIO cycle, rounded to nearest */
} sbus_pio_clock_t;

/** One transaction, as sbus_pio_tx() laid it out. */
typedef struct {
    uint32_t window_samples;  /**< the stream's `window` */
    uint32_t window_us;       /**< as asked */
    /** Hand-over of the stream to the end of the last stop bit. */
    uint32_t lead_ns;
} sbus_pio_run_t;

/**
 * The divider for a system clock of @p sys_hz, rounded to the nearest
 * 1/256.  False when the clock is below 800 kHz or the bit time is more
 * than SBUS_PIO_MAX_ERROR_PPM (0.1 %) from 10 us; every clock from
 * 1.5625 MHz up gives one within that, and 150 MHz gives 187.5 exactly.
 */
bool sbus_pio_clock(uint32_t sys_hz, sbus_pio_clock_t *out);

/**
 * The word stream for the @p n bytes at @p bytes and a listening window of
 * @p window_us after the last stop bit, 0 for none.  Returns the number of
 * words written to @p words, at most SBUS_PIO_TX_WORDS, or 0 for no bytes,
 * more than SBUS_PIO_MAX_BYTES, or a window longer than
 * SBUS_PIO_MAX_SAMPLES samples.
 */
unsigned sbus_pio_tx(const sbus_pio_clock_t *clock, const uint8_t *bytes,
                     unsigned n, uint32_t window_us, uint32_t *words,
                     sbus_pio_run_t *run);

/** The even parity bit of @p b: 1 when @p b has an odd number of 1 bits. */
unsigned sbus_pio_parity(uint8_t b);

/**
 * The capture from the @p n words of a transaction, in the order the
 * program pushed them.  @p lost says the program pushed more words than were
 * kept.  More than SBUS2_CAPTURE_MAX words, @p lost, and a counter no
 * sequence of samples can give set overflow; the bytes before such a
 * counter are kept.
 */
void sbus_pio_capture(const sbus_pio_clock_t *clock, const sbus_pio_run_t *run,
                      const uint32_t *words, unsigned n, bool lost,
                      sbus2_capture_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SBUS_PIO_H */
