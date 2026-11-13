/**
 * @file
 * @brief The index pulse found in a sampled channel: a comparator with
 *        hysteresis and a stamp interpolated between two samples.
 *
 * The balance node samples the TCRT5000 module's analogue output in the
 * same ADC (analogue-to-digital converter) pattern as the vibration
 * channel, so an edge lands at a known sample.  The mark starts when the
 * channel crosses @c hi (rising) or @c lo (falling) and ends when it
 * crosses back past the other threshold; only the start is an edge.  The
 * stamp is where the straight line between the sample before and the
 * sample at the crossing meets the midpoint of the two thresholds, in
 * 1/256 sample (Q8).  For a step, which is what a 10us edge looks like at
 * 100us a sample, that is half a sample before the first sample past it.
 *
 * Positions count samples since vibe_edge_init(), on the same clock as
 * vibe_demod_push() when both are fed the same block lengths.  The
 * demodulator adds the channel's offset in the pattern
 * (vibe_cfg_t::edge_shift_q8).
 *
 * Pure C, no SDK.  Host-tested in test_vibe.
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

/** @brief The comparator's state. */
typedef struct {
    int32_t lo;       /**< lower threshold, in the polarity's sense */
    int32_t hi;       /**< upper threshold, in the polarity's sense */
    int32_t sign;     /**< +1 for a rising mark, -1 for a falling one */
    bool mark;        /**< inside a mark */
    bool have_prev;   /**< prev holds */
    int32_t prev;     /**< the last sample, in the polarity's sense */
    uint32_t pos_q8;  /**< position of the next sample, Q8 */
} vibe_edge_t;

/**
 * @brief Starts the comparator at position 0, outside a mark.
 *
 * @param e       The state.
 * @param lo      Lower threshold, counts.
 * @param hi      Upper threshold, counts; swapped with @p lo if below it.
 * @param rising  true: the mark is the high level; false: the low level.
 */
void vibe_edge_init(vibe_edge_t *e, int16_t lo, int16_t hi, bool rising);

/**
 * @brief Finds the marks' starts in a block of samples.
 *
 * @param e    The state.
 * @param x    The samples.
 * @param n    How many.
 * @param out  The edges' stamps, Q8 samples.
 * @param max  Room in @p out; edges past it are lost.
 * @return     Edges written.
 */
size_t vibe_edge_scan(vibe_edge_t *e, const int16_t *x, size_t n,
                      uint32_t *out, size_t max);

#ifdef __cplusplus
}
#endif
