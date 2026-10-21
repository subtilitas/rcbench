/**
 * @file
 * @brief PPM (pulse-position modulation): several channels multiplexed onto one pin.
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
 * frame with the levels exchanged, and on the RP2350 that is a pad
 * inversion rather than a different program.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_PPM_H
#define RCBENCH_PPM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Largest channel count in one frame.
 *
 * Eight in a 22.5 ms frame is the convention; more fits only by shortening
 * the frame, which is what ppm_cfg_t.frame_us is for.
 */
#define PPM_MAX_CHANNELS   8u

/**
 * @brief Runs one frame needs.
 *
 * A mark and a space per channel, plus the terminating mark and the sync gap.
 */
#define PPM_MAX_RUNS       ((PPM_MAX_CHANNELS + 1u) * 2u)

/**
 * @brief Range a channel is carried in, in microseconds, both ends included.
 *
 * A width outside it is refused, not clamped.
 */
#define PPM_CHANNEL_MIN_US       400u
#define PPM_CHANNEL_MAX_US      2500u

/** @brief Default fixed mark width, in microseconds. */
#define PPM_DEFAULT_MARK_US      300u
/** @brief Default frame period, in microseconds. */
#define PPM_DEFAULT_FRAME_US   22500u

/**
 * @brief The shortest gap that is unambiguously a gap.
 *
 * A receiver frames on the first interval longer than any channel can be.
 * The ceiling on a channel is PPM_CHANNEL_MAX_US (2500 us), so a gap under about
 * 3 ms is one that some receiver will read as a very long channel.
 */
#define PPM_SYNC_MIN_US         3000u

/** @brief Frame timing for ppm_frame() and ppm_min_frame_us(). */
typedef struct {
    uint16_t mark_us;       /**< the fixed pulse; 0 takes the default      */
    uint16_t frame_us;      /**< start of frame to start of frame          */
    uint16_t sync_min_us;   /**< below this the frame is refused           */
} ppm_cfg_t;

/** @brief Compound literal of ppm_cfg_t holding the default timing. */
#define PPM_CFG_DEFAULT()               \
    (ppm_cfg_t) {                       \
        .mark_us     = PPM_DEFAULT_MARK_US,  \
        .frame_us    = PPM_DEFAULT_FRAME_US, \
        .sync_min_us = PPM_SYNC_MIN_US,      \
    }

/**
 * @brief Lay a frame out as alternating run lengths in microseconds.
 *
 * @p runs receives mark, space, mark, space, ... and ends with the
 * terminating mark and the sync gap, so it always holds an even number of
 * entries and always begins with a mark.  A driver toggles the pin at each
 * one and does not need to know which is which.
 *
 * @param channel_us  Pulse width of each channel, in microseconds.
 * @param channels    Number of entries in @p channel_us, 1 to PPM_MAX_CHANNELS.
 * @param cfg         Frame timing; NULL or a zero field takes the default.
 * @param runs        Receives the runs.
 * @param max_runs    Capacity of @p runs, in entries.
 * @return The number of runs written, or 0 when the frame cannot be built:
 *         no channels or too many, a channel shorter than the mark it has to
 *         contain, a channel outside PPM_CHANNEL_MIN_US..PPM_CHANNEL_MAX_US, or
 *         a sync gap below @p cfg->sync_min_us once the channels have been
 *         paid for.
 */
size_t ppm_frame(const uint16_t *channel_us, uint8_t channels,
                 const ppm_cfg_t *cfg, uint16_t *runs, size_t max_runs);

/**
 * @brief Shortest frame that carries @p channels channels at any width.
 *
 * Every channel at PPM_CHANNEL_MAX_US, plus the terminating mark, plus the
 * smallest gap that is still a gap, in microseconds.
 *
 * A configuration screen wants this before it offers a frame rate, so the
 * refusal in ppm_frame() becomes a range the operator never leaves rather
 * than an error they hit.
 *
 * @param channels  Channel count to carry.
 * @param cfg       Frame timing; NULL or a zero field takes the default.
 * @return The frame length in microseconds, or 0 for a channel count this
 *         module cannot carry.
 */
uint32_t ppm_min_frame_us(uint8_t channels, const ppm_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PPM_H */
