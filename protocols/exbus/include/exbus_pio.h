/**
 * @file exbus_pio.h
 * @brief The numbers and words a PIO pin driver for an EX Bus line works
 * with.
 *
 * Implements the EX Bus protocol of JETI receivers and devices.  JETI and
 * EX Bus are names or trademarks of their respective owner; they are used
 * here only to say which devices this module works with (README.md, "Legal
 * notice and disclaimer").
 *
 * rp2350/exbus_line.pio is one PIO (programmable input/output) program on
 * one pin: it sends bytes as 8N1, releases the pin right after the last
 * stop bit, receives 8N1 bytes until the line has been quiet long enough,
 * and drives the pin high again.  It runs at 8 cycles a bit, so the clock
 * divider is the system clock over 8 times the rate.  This file computes
 * the divider, lays a transmission out as the words the program reads and
 * turns the words it pushes into an exbus_capture_t.  It has no hardware
 * access, so the host suite runs it against a model of the state machine.
 *
 * The word stream of one transaction, in the order the program reads it:
 *
 *   count     bytes to send, less 1
 *   window    ticks the program waits for the first byte
 *   bytes     one word per byte: the byte in bits 7:0, the quiet time
 *             (EXBUS_PIO_QUIET_TICKS) in bits 31:8
 *
 * A tick is one pass of the program's watch loop, 2 cycles: a sample of
 * the pin and a decrement of the count.  A falling edge there starts a
 * byte: the count is taken as the byte's stamp, the count is set to the
 * quiet time, and 8 data bits and the stop bit are sampled in the middle
 * of each bit.  The byte is pushed as:
 *
 *   bit  31    the stop bit (1 when it was high)
 *   bits 30:23 the byte, least significant bit first on the wire
 *   bits 22:0  the count when the start bit was seen (23 bits)
 *
 * A byte takes EXBUS_PIO_BYTE_CYCLES from the sample that saw its start bit
 * to the next sample.  The line stays released until the count runs out on
 * a high sample: 2 * window + 3 cycles without a byte; with bytes, 78 + 2 *
 * EXBUS_PIO_QUIET_TICKS cycles after the sample that saw the last one.  A
 * reply that starts in the window is received whole as long as no gap in
 * it reaches the quiet time, 20 bits.
 *
 * Rate tolerance.  78 cycles a byte against 80 on the wire: the program is
 * back in the watch loop before the next start bit of a device up to
 * 80 / 78 - 1 = 2.5 % fast.  Above that each byte starts later than the
 * last, and a long reply loses bits.  A device 3 % slow still has every
 * sample inside its bit.  test/host/test_exbus_pio.c holds both bounds at
 * every phase.
 *
 * The module includes nothing outside the C standard headers and holds no
 * global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EXBUS_PIO_H
#define EXBUS_PIO_H

#include <stdbool.h>
#include <stdint.h>

#include "exbus_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** State machine cycles per bit. */
#define EXBUS_PIO_BIT_CYCLES     8u
/** Cycles of one tick of the watch loop. */
#define EXBUS_PIO_TICK_CYCLES    2u
/** Cycles from the sample that sees a start bit to the next sample. */
#define EXBUS_PIO_BYTE_CYCLES    78u
/** Ticks the line must stay high after a byte for the release to end:
 *  160 cycles, EXBUS_QUIET_BITS. */
#define EXBUS_PIO_QUIET_TICKS    (EXBUS_QUIET_BITS * EXBUS_PIO_BIT_CYCLES \
                                  / EXBUS_PIO_TICK_CYCLES)
/** Cycles of a release without a byte beyond 2 per tick. */
#define EXBUS_PIO_RELEASE_EXTRA  3u
/** The most cycles one received byte adds to the release: its own 78 and
 *  a gap one tick short of the quiet time, 236. */
#define EXBUS_PIO_BYTE_MAX_CYCLES \
    (EXBUS_PIO_BYTE_CYCLES - EXBUS_PIO_TICK_CYCLES \
     + EXBUS_PIO_TICK_CYCLES * EXBUS_PIO_QUIET_TICKS)
/** Cycles one transmitted byte takes: start, 8 data bits, stop. */
#define EXBUS_PIO_TX_BYTE_CYCLES (EXBUS_BITS_PER_BYTE * EXBUS_PIO_BIT_CYCLES)
/** Cycles the input synchroniser delays the pin. */
#define EXBUS_PIO_SYNC_CYCLES    2u
/** Bits of the count a received word carries. */
#define EXBUS_PIO_STAMP_BITS     23u
/** The largest window count. */
#define EXBUS_PIO_WINDOW_MAX     ((1u << EXBUS_PIO_STAMP_BITS) - 1u)

/** Words of the longest stream: 2 counts and EXBUS_TX_MAX bytes. */
#define EXBUS_PIO_TX_WORDS       (2u + EXBUS_TX_MAX)
/** Words a transaction keeps: EXBUS_RX_MAX bytes and one that shows an
 *  overflow. */
#define EXBUS_PIO_RX_WORDS       (EXBUS_RX_MAX + 1u)

/** The divider a system clock gives for a rate. */
typedef struct {
    uint32_t sys_hz;
    uint32_t baud;
    uint32_t div256;     /**< the clock divider in 1/256: integer part
                              div256 >> 8, fraction div256 & 0xFF */
    uint32_t error_ppm;  /**< the rate's error: at most 1954 ppm (0.2 %),
                              half a step of 1/256 of a divider of 1 */
    uint64_t cycle_ps;   /**< one state machine cycle in ps, rounded */
} exbus_pio_clock_t;

/** One transaction, as exbus_pio_tx() laid it out. */
typedef struct {
    uint32_t window_ticks;  /**< the stream's `window` */
    uint32_t tx_bytes;
} exbus_pio_run_t;

/**
 * The divider for @p baud from a system clock of @p sys_hz, rounded to
 * the nearest 1/256.  False for a rate or a clock of 0 and a divider below
 * 1 or above 65535.  At 150 MHz both rates are exact: 150 for 125 kbaud
 * and 75 for 250 kbaud.
 */
bool exbus_pio_clock(uint32_t sys_hz, uint32_t baud, exbus_pio_clock_t *out);

/** @p cycles of the state machine in ns, rounded to nearest. */
uint32_t exbus_pio_cycles_ns(const exbus_pio_clock_t *clock, uint64_t cycles);

/** The window count that keeps the line released for at least
 *  @p window_us without a byte; 0 when the clock is missing, the window is
 *  shorter than 2 cycles or the count exceeds EXBUS_PIO_WINDOW_MAX. */
uint32_t exbus_pio_window_ticks(const exbus_pio_clock_t *clock,
                                uint32_t window_us);

/** The longest the line can stay released for @p window_ticks and
 *  @p bytes received bytes: 2 * window + 3 cycles and
 *  EXBUS_PIO_BYTE_MAX_CYCLES a byte, in us, rounded up. */
uint32_t exbus_pio_release_us(const exbus_pio_clock_t *clock,
                              uint32_t window_ticks, uint32_t bytes);

/**
 * The word stream for @p n bytes from @p tx and a window of @p window_us.
 * Returns the number of words written to @p words, n + 2, or 0 for no
 * bytes, more than EXBUS_TX_MAX, a missing argument or a window the count
 * cannot hold.
 */
unsigned exbus_pio_tx(const exbus_pio_clock_t *clock, const uint8_t *tx,
                      unsigned n, uint32_t window_us, uint32_t *words,
                      exbus_pio_run_t *run);

/**
 * The capture from the @p n words a transaction pushed, in their order.
 * @p lost says the program pushed more words than were kept.  A byte's
 * time is the first of the cycles its start bit can have fallen in, at
 * most 1 cycle early.  More than EXBUS_RX_MAX bytes, @p lost, and a count
 * no run can give (the first byte's above the window, a later one's at or
 * above the quiet time) set overflow; the bytes up to that word are kept.
 * release_us is the release the bytes kept give: with overflow it ends
 * with the last of them.
 */
void exbus_pio_capture(const exbus_pio_clock_t *clock,
                       const exbus_pio_run_t *run, const uint32_t *words,
                       unsigned n, bool lost, exbus_capture_t *out);

#ifdef __cplusplus
}
#endif

#endif /* EXBUS_PIO_H */
