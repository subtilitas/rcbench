/*
 * PPM (pulse-position modulation) out: one pin, several channels, driven by a
 * PIO (programmable input/output) state machine fed by a pair of DMA (direct
 * memory access) channels that restart each other.
 *
 * The processor is not in the timing path at all.  One DMA channel plays the
 * frame into the state machine and chains to a second, whose only transfer
 * writes the buffer address back into the first and retriggers it, so the
 * frame repeats forever whatever the loop is doing.  A frame is 22.5 ms and
 * the state machine's queue is eight words deep; anything that fed it from
 * the loop would produce a stretched frame every time the loop was busy, and
 * a stretched frame is a servo that twitches.
 *
 * The frame buffer is written in place while it is being played.  A channel
 * updated mid-frame therefore takes effect on the next frame and the rest of
 * that frame carries the previous values, which is one frame of staleness on
 * a signal that is a stream of frames.  The alternative, swapping buffers,
 * buys nothing: the receiver acts on whole frames either way.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_OUT_PPM_H
#define RCBENCH_OUT_PPM_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Take @p pin for @p channels channels at @p rate_hz frames a second.
 *
 * Refuses a frame rate that cannot carry that many channels at their widest
 * (see ppm_min_frame_us), a pin no PIO block can reach alongside its other
 * programs, and a shortage of state machines or DMA channels.  Nothing is
 * emitted until the first out_ppm_write().
 */
bool out_ppm_bind(uint8_t pin, uint8_t channels, uint16_t rate_hz);

/** Give the pin, the state machine and both DMA channels back. */
void out_ppm_release(uint8_t pin);

/**
 * Lay @p channels pulse widths into the frame and keep sending it.
 *
 * Returns false, and stops the output, for a set of widths that cannot be
 * laid out in the frame this pin was bound with.  A refused frame is not
 * half-written: shared/ppm builds it whole or not at all.
 */
bool out_ppm_write(uint8_t pin, const uint16_t *channel_us, uint8_t channels);

/** Stop emitting; the pin is left low.  The next write starts it again. */
void out_ppm_stop(uint8_t pin);

#endif /* RCBENCH_OUT_PPM_H */
