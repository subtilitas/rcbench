/**
 * @file hitec_pio.h
 * @brief The numbers and words a PIO pin driver for the Hitec wire works
 *        with.
 *
 * Implements the signal-wire programming protocols of Hitec servos.  Hitec
 * is a name or trademark of its owner; it is used here only to say which
 * servos this module works with (README.md, "Legal notice and disclaimer").
 *
 * rp2350/hitec_line.pio is one PIO (programmable input/output) program for
 * both families: a half-duplex 8N1 UART (universal asynchronous
 * receiver-transmitter) on one pin.  The clock divider makes one bit
 * HITEC_PIO_CYCLES_PER_BIT state machine cycles, so the bit rate is the
 * divider's.  The input override inverts the pin for the D series, so the
 * program reads the idle level as 1 in both families.  The program sends
 * every bit time as two bits of its stream: the pad's direction and its
 * output level.  An open-drain line sends a level of 0 and switches the
 * direction; a push-pull line holds the direction and switches the level.
 * This file lays a transaction out as that stream, computes the divider and
 * the counts from the system clock, and turns the words the program pushes
 * into a hitec_capture_t.  It has no hardware access, so the host suite
 * runs it against a model of the state machine.
 *
 * The word stream of one transaction, in the order the program reads it,
 * every word shifted out least significant bit first:
 *
 *   lead      bit 0 the lead's level, bit 1 its direction, bits 2 to 31 the
 *             lead count L: the lead lasts 2 L + HITEC_PIO_LEAD_CYCLES
 *   window    W, ticks the receiver waits for a start bit
 *   bits      B, bit times sent
 *   B / 16    words of 16 bit times, each bit time the direction (bit 2 i)
 *             and the level (bit 2 i + 1)
 *   answer    LISTEN with an answer only: 16 bit times, sent after the
 *             first byte received
 *
 * Bit times at the lead's level fill the front of the first word, so the
 * last bit time ends a word and nothing of the stream is left when the
 * receiver starts.  The receiver answers when the answer word is still in
 * the output shift register after a byte; at the end of the window the
 * program drains the register, so an answer no byte called for is dropped.
 *
 * A tick is HITEC_PIO_TICK_CYCLES cycles, 1/8 of a bit.  The receiver samples
 * the pin every 1 or 2 cycles while it waits, and every byte it takes is one
 * pushed word: bits 0 to 7 the data, bit 8 the stop bit as sampled (1 is
 * the idle level), bits 9 to 31 the tick counter at the start bit.  The
 * counter counts down from W only while the receiver waits, so the time of
 * byte k is
 *
 *   2 + 2 (W - X_k) + HITEC_PIO_BYTE_CYCLES k (+ HITEC_PIO_ANSWER_CYCLES
 *   for k > 0 after an answer)
 *
 * cycles after the release, X_k the counter it pushed.  The start bit's edge
 * lies within the 2 cycles before that sample; the capture puts it 1 cycle
 * before, so a time is right to 1 cycle, 1/16 of a bit.
 *
 * The module includes nothing outside the C standard headers and holds no
 * global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HITEC_PIO_H
#define HITEC_PIO_H

#include <stdbool.h>
#include <stdint.h>

#include "hitec_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** State machine cycles in one bit. */
#define HITEC_PIO_CYCLES_PER_BIT 16u
/** Cycles of one tick of the receiver's counter: 1/8 of a bit. */
#define HITEC_PIO_TICK_CYCLES 2u
/** Smallest clock divider: the input synchroniser's 2 system clock cycles
 *  are then half a state machine cycle or less. */
#define HITEC_PIO_MIN_DIV 4u
/** Largest integer part of the clock divider. */
#define HITEC_PIO_MAX_DIV 65535u
/** Cycles the input synchroniser delays the pin, in system clock cycles. */
#define HITEC_PIO_SYNC_SYS_CYCLES 2u
/** Largest bit time error a clock may give, in parts per million: 0.05 %.
 *  The 8 fractional bits of the divider and a divider of 4 or more keep
 *  every clock within 489 ppm. */
#define HITEC_PIO_MAX_ERROR_PPM 500u

/** Cycles of a lead beyond 2 per count. */
#define HITEC_PIO_LEAD_CYCLES 8u
/** Cycles of the receive path of one byte, start bit to start bit, beyond
 *  the waiting: detection, 9.5 bits of samples and the push. */
#define HITEC_PIO_BYTE_CYCLES 157u
/** Cycles an answer adds to the receive path of the first byte. */
#define HITEC_PIO_ANSWER_CYCLES 261u
/** Cycles from the sample that sees the first byte's start bit to the
 *  answer's first change of direction: 10 bit times. */
#define HITEC_PIO_ANSWER_CYCLES_TO_EDGE 160u
/** Bit times of the answer word. */
#define HITEC_PIO_ANSWER_BITS 16u
/** Cycles of the drain on the way from the end of the window to idle. */
#define HITEC_PIO_DRAIN_CYCLES 2u

/** Words of rp2350/hitec_line.pio. */
#define HITEC_PIO_PROGRAM_WORDS 29u
/** Addresses in the program, from its first word.  IDLE: the `out` the
 *  machine stalls on between transactions.  BIT to RELEASE - 1: the bit
 *  times.  RELEASE: the release before the receiver.  ASK: the `jmp !osre`
 *  after a byte.  ANSWER and ANSWER + 1: the way into the answer's bit
 *  times.  DRAIN: the end of the window, and where a restart starts. */
#define HITEC_PIO_IDLE_PC    1u
#define HITEC_PIO_BIT_PC     8u
#define HITEC_PIO_RELEASE_PC 11u
#define HITEC_PIO_ASK_PC     22u
#define HITEC_PIO_ANSWER_PC  24u
#define HITEC_PIO_DRAIN_PC   27u

/** Bits of the tick counter a pushed word carries; the window is shorter. */
#define HITEC_PIO_STAMP_BITS 23u
#define HITEC_PIO_MAX_WINDOW_TICKS ((1u << HITEC_PIO_STAMP_BITS) - 1u)
/** Largest lead count. */
#define HITEC_PIO_MAX_LEAD ((1u << 30) - 1u)

/** Words of the longest stream: 3 counts and 8 words of bit times (8 bytes
 *  of 10 bits with 6 idle bits between two, and the D series' settle bit,
 *  padded to 128 bit times). */
#define HITEC_PIO_TX_WORDS 11u

/** The divider and the counts a system clock gives for one family. */
typedef struct {
    uint32_t sys_hz;
    uint32_t div256;   /**< clock divider times 256: integer part and 8
                        *   fractional bits */
    uint32_t err_ppm;  /**< bit time error, parts per million */
    uint8_t  family;   /**< hitec_family_t */
} hitec_pio_clock_t;

/** One transaction, as hitec_pio_tx() laid it out. */
typedef struct {
    uint32_t window_ns;     /**< as asked */
    uint32_t window_ticks;  /**< W */
    /** From the lead's first change of direction to the release, in ns. */
    uint32_t lead_ns;
    /** An upper bound of the transaction's time from the hand-over of the
     *  stream: lead, bits, window, max_rx bytes and the answer, in us. */
    uint32_t span_us;
    uint8_t  family;
    uint8_t  max_rx;
    uint8_t  answer;        /**< 1: the stream carries an answer word */
} hitec_pio_run_t;

/**
 * The divider for @p family at a system clock of @p sys_hz.  False when the
 * divider is below HITEC_PIO_MIN_DIV (a clock below 1.2282 MHz classic, 7.3692 MHz
 * D series: 64 bit rates less half a step of the divider), its integer part above HITEC_PIO_MAX_DIV,
 * or the bit time it gives more than HITEC_PIO_MAX_ERROR_PPM from the
 * family's.  @p out is left as it was on false.
 */
bool hitec_pio_clock(uint32_t sys_hz, uint8_t family, hitec_pio_clock_t *out);

/** The bit time @p clock gives, in ns, rounded to nearest. */
uint32_t hitec_pio_bit_ns(const hitec_pio_clock_t *clock);

/** @p cycles state machine cycles of @p clock, in ns, rounded down. */
uint32_t hitec_pio_cycles_ns(const hitec_pio_clock_t *clock, uint64_t cycles);

/**
 * The word stream for @p xfer on a line with @p drive (hitec_drive_t).
 * Returns the number of words written to @p words, at most
 * HITEC_PIO_TX_WORDS, or 0 when @p xfer is not valid, is of another family
 * than @p clock, asks classic for push-pull, or asks an open-drain line to
 * hold a high level.
 */
unsigned hitec_pio_tx(const hitec_pio_clock_t *clock, uint8_t drive,
                      const hitec_xfer_t *xfer, uint32_t *words,
                      hitec_pio_run_t *run);

/**
 * Whether the answer of @p run is still to go out with @p n bytes received
 * and the state machine at @p pc, counted from the program's first word.
 * True only for a run with an answer, @p n of 1 and @p pc at ASK, at
 * ANSWER or ANSWER + 1, or in the bit times.  A receive that has its count
 * waits for this to turn false before the driver restarts the machine, so
 * a LISTEN for 1 byte sends its whole answer.
 */
bool hitec_pio_answer_due(const hitec_pio_run_t *run, unsigned n,
                          unsigned pc);

/**
 * The capture from the @p n words the program pushed, in their order.
 * @p full says the receive ended at max_rx bytes, @p lost that the driver
 * ended a program that ran past run->span_us, @p aborted that the
 * transaction was cut.  A counter above the window or above the one before
 * sets overflow and ends the capture there; so does @p lost.
 */
void hitec_pio_capture(const hitec_pio_clock_t *clock,
                       const hitec_pio_run_t *run, const uint32_t *stamps,
                       unsigned n, bool full, bool lost, bool aborted,
                       hitec_capture_t *out);

#ifdef __cplusplus
}
#endif

#endif /* HITEC_PIO_H */
