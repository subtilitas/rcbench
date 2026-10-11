/**
 * @file kst_pio.h
 * @brief The numbers and words a PIO pin driver for the KST wire works with.
 *
 * rp2350/kst_line.pio is one PIO (programmable input/output) program that
 * clocks a frame out, releases the pad, stamps every level change of the
 * reply window and drives the pad low again.  It takes its counts from the
 * transmit FIFO and gives its stamps to the receive FIFO.  This file
 * computes the counts from the system clock, lays a frame out as the words
 * the program reads, and turns the stamps into a kst_capture_t.  It has no
 * hardware access, so the host suite runs it against a model of the state
 * machine.
 *
 * The word stream of one transaction, in the order the program reads it:
 *
 *   hold      half-cell length in cycles, less KST_PIO_BIT_OVERHEAD
 *   count     half-cells clocked out, less 1
 *   levels    1 to 4 words, most significant bit first, one bit a half-cell
 *   low       cycles the pad stays driven after the last half-cell
 *   window    ticks the program waits for a first rising edge
 *   quiet     ticks without a rising edge that end the capture
 *
 * The levels are the frame's half-cells followed by one low half-cell,
 * whose start is the frame's last edge.  Low half-cells in front fill the
 * first word, so the levels end on a word boundary: the frame starts up to
 * 31 half-cells (787 us) after the stream is handed over.
 *
 * A tick is KST_PIO_TICK_CYCLES system clock cycles, 26.7 ns at 150 MHz.
 * The program stamps the tick counter at every level change and reloads it
 * with `quiet` at every rising edge:
 *
 *   - before the first rising edge the counter runs down from `window`;
 *   - a rising edge is stamped, then the counter restarts from `quiet`;
 *   - a falling edge is stamped and the counter runs on.
 *
 * When the counter runs out the pad drives low: at the end of the window
 * without a reply, and KST_PIO_QUIET_NS after the last rising edge of one.
 * Two rising edges of a reply are at most 2 cells (102.3 us) apart, and a
 * last high ends within 59 us of its rise, so the pad drives low 60 us to
 * 165 us after the reply's last level change.
 *
 * The module includes nothing outside the C standard headers and holds no
 * global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef KST_PIO_H
#define KST_PIO_H

#include <stdbool.h>
#include <stdint.h>

#include "kst_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** System clock cycles between two samples of the pin. */
#define KST_PIO_TICK_CYCLES 4u
/** Cycles one half-cell takes beyond its hold count. */
#define KST_PIO_BIT_OVERHEAD 4u
/** Cycles from the last edge to the release beyond the half-cell and the
 *  low count. */
#define KST_PIO_LOW_OVERHEAD 2u
/** Cycles from the release to the first sample. */
#define KST_PIO_SAMPLE_LEAD 3u
/** Cycles the input synchroniser delays the pin. */
#define KST_PIO_SYNC_CYCLES 2u

/** Time without a rising edge that ends a capture once one was seen. */
#define KST_PIO_QUIET_NS 165000u

/** Longest tick, and with it the coarsest stamp, a clock may give. */
#define KST_PIO_MAX_TICK_NS 500u

/** Largest half-cell error a clock may give, in parts per million. */
#define KST_PIO_MAX_ERROR_PPM 2000u

/** Words of the longest stream: 3 counts in front of 4 level words, 3
 *  behind. */
#define KST_PIO_TX_WORDS 9u
/** Stamps a transaction keeps: one for a line that is high at the start,
 *  KST_CAP_MAX_EDGES edges and one that shows an overflow. */
#define KST_PIO_RX_WORDS (KST_CAP_MAX_EDGES + 2u)

/** The counts a system clock gives. */
typedef struct {
    uint32_t sys_hz;
    uint32_t half_cycles;     /**< cycles of one transmit half-cell */
    uint32_t release_cycles;  /**< last edge to the release of the pad */
    uint32_t quiet_ticks;
} kst_pio_clock_t;

/** One transaction, as kst_pio_tx() laid it out. */
typedef struct {
    uint32_t window_ns;      /**< as asked: end of the capture */
    uint32_t window_ticks;   /**< the stream's `window` */
    uint32_t lead_ns;        /**< hand-over of the stream to the last edge */
} kst_pio_run_t;

/**
 * The counts for a system clock of @p sys_hz.  False when a tick is longer
 * than KST_PIO_MAX_TICK_NS, which it is below 8 MHz, and when the half-cell
 * the clock gives is more than 0.2 % from KST_HALF_CELL_NS.  Every clock
 * from 9.9 MHz up gives one within 0.2 %, and a multiple of 5 MHz gives it
 * exactly.
 */
bool kst_pio_clock(uint32_t sys_hz, kst_pio_clock_t *out);

/** The transmit half-cell of @p clock in ns, rounded to nearest. */
uint32_t kst_pio_half_cell_ns(const kst_pio_clock_t *clock);

/** One tick of @p clock in ns, rounded up. */
uint32_t kst_pio_tick_ns(const kst_pio_clock_t *clock);

/**
 * The word stream for @p frame and a capture until @p window_ns after its
 * last edge.  Returns the number of words written to @p words, at most
 * KST_PIO_TX_WORDS, or 0 for a frame without half-cells, one longer than
 * KST_FRAME_MAX_HALF_CELLS, or a window that ends before the release.
 */
unsigned kst_pio_tx(const kst_pio_clock_t *clock, const kst_frame_t *frame,
                    uint32_t window_ns, uint32_t *words, kst_pio_run_t *run);

/**
 * The capture from the @p n stamps of a transaction, in the order the
 * program gave them.  @p lost says the program gave more stamps than were
 * kept.  A first stamp equal to the window count is a line that was high at
 * the first sample: it sets level0 and is no edge.  More than
 * KST_CAP_MAX_EDGES edges, @p lost, and a stamp no count of the stream can
 * give set overflow.
 */
void kst_pio_capture(const kst_pio_clock_t *clock, const kst_pio_run_t *run,
                     const uint32_t *stamps, unsigned n, bool lost,
                     kst_capture_t *out);

#ifdef __cplusplus
}
#endif

#endif /* KST_PIO_H */
