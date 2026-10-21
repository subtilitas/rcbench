/**
 * @file
 * @brief PPM (pulse-position modulation) out: one pin, several channels, driven
 *        by a PIO (programmable input/output) state machine fed by a pair of
 *        DMA (direct memory access) channels that restart each other.
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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Number of pins that can be bound at one time.
 *
 * A project that needs another number defines it for the whole build.
 */
#ifndef OUT_PPM_MAX_PINS
#define OUT_PPM_MAX_PINS 8u
#endif

/**
 * @brief Take a pin for a set of channels at a frame rate.
 *
 * Refuses a frame rate that cannot carry that many channels at their widest
 * (see ppm_min_frame_us), a pin no PIO block can reach alongside its other
 * programs, and a shortage of state machines or DMA channels.  Nothing is
 * emitted until the first out_ppm_write().
 *
 * A pin that is already bound with the same channel count and frame period
 * stays as it is and the call returns true.  With a different count or
 * period it returns false and the old binding keeps running; release the pin
 * first to change it.
 *
 * @param pin       GPIO number to drive.
 * @param channels  Channel count, 1 to PPM_MAX_CHANNELS.
 * @param rate_hz   Frames per second; the frame period is 1000000 / rate_hz
 *                  microseconds, which must be 2 to 65535.
 * @return true when the pin is bound with these parameters, false when the
 *         request is refused.
 */
bool out_ppm_bind(uint8_t pin, uint8_t channels, uint16_t rate_hz);

/**
 * @brief Give the pin, the state machine and both DMA channels back.
 *
 * @param pin  GPIO number bound with out_ppm_bind(). A pin that is not bound
 *             is ignored.
 */
void out_ppm_release(uint8_t pin);

/**
 * @brief Lay channel pulse widths into the frame and keep sending it.
 *
 * Returns false, and stops the output, for a set of widths that cannot be
 * laid out in the frame this pin was bound with.  A refused frame is not
 * half-written: ppm_frame() builds it whole or not at all.
 *
 * @param pin         GPIO number bound with out_ppm_bind().
 * @param channel_us  Pulse width of each channel, in microseconds.
 * @param channels    Must equal the channel count the pin was bound with.
 * @return true when the frame is laid out and sent, false when the pin is not
 *         bound, the count differs, or the widths cannot be laid out.
 */
bool out_ppm_write(uint8_t pin, const uint16_t *channel_us, uint8_t channels);

/**
 * @brief Stop emitting; the pin is left low.
 *
 * The next write starts it again.
 *
 * @param pin  GPIO number bound with out_ppm_bind(). A pin that is not bound
 *             or not running is ignored.
 */
void out_ppm_stop(uint8_t pin);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OUT_PPM_H */
