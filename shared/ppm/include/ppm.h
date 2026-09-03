/*
 * PPM (pulse-position modulation): several channels multiplexed onto one pin.
 *
 * A frame is a run of marks.  The time from the start of one mark to the
 * start of the next is one channel's pulse width, so a channel is carried by
 * where its mark falls rather than by how long it is, and the mark itself is
 * a fixed 300 us that belongs to no channel.  After the last channel comes a
 * terminating mark and then a gap long enough that a receiver cannot mistake
 * it for a channel; that gap is what locates the start of the next frame.
 *
 * The consequence is that channels and frame rate are not independent.  Eight
 * channels at 2000 us each already spend 16 ms of a 22.5 ms frame, and the
 * sync gap is whatever is left.  Asking for more channels, longer travel or a
 * faster frame eats the gap, and a frame whose gap has shrunk into the range
 * a receiver reads as a channel does not fail visibly: it decodes as a
 * different number of channels, shifted.  So this refuses rather than emits
 * a frame whose sync gap is too short.
 *
 * The polarity is not decided here.  Positive and negative shift are the same
 * frame with the levels exchanged, and on the coprocessor that is a pad
 * inversion rather than a different program.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_PPM_H
#define RCBENCH_PPM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "outputs.h"   /* OUT_FLOOR_US and OUT_CEILING_US: one range, not two */

#ifdef __cplusplus
extern "C" {
#endif

/** Eight in a 22.5 ms frame is the convention; more fits only by shortening
 *  the frame, which is what ppm_cfg_t.frame_us is for. */
#define PPM_MAX_CHANNELS   8u

/** Runs a frame needs: a mark and a space per channel, plus the terminating
 *  mark and the sync gap. */
#define PPM_MAX_RUNS       ((PPM_MAX_CHANNELS + 1u) * 2u)

#define PPM_DEFAULT_MARK_US      300u
#define PPM_DEFAULT_FRAME_US   22500u

/**
 * The shortest gap that is unambiguously a gap.
 *
 * A receiver frames on the first interval longer than any channel can be.
 * The ceiling on a channel is OUT_CEILING_US (2500 us), so a gap under about
 * 3 ms is one that some receiver will read as a very long channel.
 */
#define PPM_SYNC_MIN_US         3000u

typedef struct {
    uint16_t mark_us;       /**< the fixed pulse; 0 takes the default      */
    uint16_t frame_us;      /**< start of frame to start of frame          */
    uint16_t sync_min_us;   /**< below this the frame is refused           */
} ppm_cfg_t;

#define PPM_CFG_DEFAULT()               \
    (ppm_cfg_t) {                       \
        .mark_us     = PPM_DEFAULT_MARK_US,  \
        .frame_us    = PPM_DEFAULT_FRAME_US, \
        .sync_min_us = PPM_SYNC_MIN_US,      \
    }

/**
 * Lay a frame out as alternating run lengths in microseconds.
 *
 * @p runs receives mark, space, mark, space, ... and ends with the
 * terminating mark and the sync gap, so it always holds an even number of
 * entries and always begins with a mark.  A driver toggles the pin at each
 * one and does not need to know which is which.
 *
 * Returns the number of runs written, or 0 when the frame cannot be built:
 * no channels or too many, a channel shorter than the mark it has to
 * contain, a channel outside OUT_FLOOR_US..OUT_CEILING_US, or a sync gap
 * below @p cfg->sync_min_us once the channels have been paid for.
 */
size_t ppm_frame(const uint16_t *channel_us, uint8_t channels,
                 const ppm_cfg_t *cfg, uint16_t *runs, size_t max_runs);

/**
 * The shortest frame that can carry @p channels channels whatever they are
 * commanded to, in microseconds: every channel at OUT_CEILING_US, plus the
 * terminating mark, plus the smallest gap that is still a gap.
 *
 * A configuration screen wants this before it offers a frame rate, so the
 * refusal above becomes a range the operator never leaves rather than an
 * error they hit.  Returns 0 for a channel count this module cannot carry.
 */
uint32_t ppm_min_frame_us(uint8_t channels, const ppm_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PPM_H */
