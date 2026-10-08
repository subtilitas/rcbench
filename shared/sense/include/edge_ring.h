/*
 * The words the tap's PIO (programmable input/output) program pushes, and
 * the ring a DMA (direct memory access) channel collects them in.
 *
 * Word format.  Bits 31 to 1 are the low 31 bits of the state machine's
 * down counter X when the edge was seen; bit 0 is the level after the edge,
 * 1 for a rise.  X starts at 0 when the capture starts and counts down once
 * per tick (4 system clocks, 26.7 ns at 150 MHz), so the ticks elapsed
 * since the start are (0 - count) mod 2^31.  The counter wraps every
 * 2^31 ticks, 57.3 s.
 *
 * Extension.  A word carries 31 bits of a 64-bit tick count.  The rest
 * comes from an estimate of the present tick the caller has from another
 * clock: the word's tick is the one congruent to its count modulo 2^31
 * nearest the estimate.  The estimate has to be within 2^30 ticks (28.6 s)
 * of the truth; microseconds are what it is held to.  Chaining the
 * differences between words instead would lose a multiple of 57.3 s across
 * any silence longer than that.
 *
 * The ring.  The DMA channel writes the words into a ring of a power of two
 * in size, endlessly; the caller reads the channel's write position as a
 * word index.  edge_ring_take() takes the words written since the last
 * call.  An overrun, the DMA having lapped the reader, shows as the slot of
 * the last word taken holding another word: counts do not repeat within
 * 57.3 s, so a lap always shows.  The reader then discards what is in the
 * ring and starts again after the newest word.
 *
 * Pure C, no SDK (software development kit).  Host-tested in
 * test_edge_ring.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tone.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The counter's bits a word carries. */
#define EDGE_COUNT_BITS 31u
#define EDGE_COUNT_MASK 0x7FFFFFFFu

/** The word the program pushes for an edge at counter value @p count. */
uint32_t edge_word(uint32_t count, bool level);

/** A word's 31-bit count and level. */
uint32_t edge_word_count(uint32_t word);
bool     edge_word_level(uint32_t word);

/** The tick of the counter value @p count nearest @p est, with a counter
 *  that started at 0 and counts down.  Ticks before the start do not
 *  exist: the result is at least 0. */
uint64_t edge_tick(uint32_t count, uint64_t est);

typedef struct {
    uint32_t rd;         /**< the next slot to read                       */
    uint32_t last;       /**< the word taken last, in slot rd - 1         */
    bool     have_last;  /**< a word has been taken since the start       */
    uint32_t overruns;   /**< laps seen                                   */
} edge_ring_t;

/** Start reading a ring from slot 0, as after the DMA channel restarts. */
void edge_ring_init(edge_ring_t *r);

/**
 * Take the words in @p ring ( @p size words, a power of two) written since
 * the last call, @p wr being the slot the DMA writes next, as edges
 * stamped against @p est (the present tick, edge_tick()).  At most @p max
 * edges go to @p out; words past that stay in the ring for the next call.
 *
 * Returns the edges written.  *@p overrun is set when the DMA lapped the
 * reader: nothing is returned, the reader is moved after the newest word,
 * and the caller ends the beep under way (tone_flush()).
 */
size_t edge_ring_take(edge_ring_t *r, const uint32_t *ring, uint32_t size,
                      uint32_t wr, uint64_t est, tone_edge_t *out, size_t max,
                      bool *overrun);

#ifdef __cplusplus
}
#endif
