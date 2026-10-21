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
 * @brief Pins that can be bound at one time.
 *
 * A project that needs another number defines it for the whole build.
 */
#ifndef OUT_PPM_MAX_PINS
#define OUT_PPM_MAX_PINS 8u
#endif

/**
 * @brief Take @p pin for @p channels channels at @p rate_hz frames a second.
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
 * @param pin       GPIO number.
 * @param channels  Number of channels.
 * @param rate_hz   Frames per second; refused when rate_hz is 0 or the frame
 *                  period is below 2 us or above 65535 us.
 * @return true when the pin is bound with these parameters, false otherwise.
 */
bool out_ppm_bind(uint8_t pin, uint8_t channels, uint16_t rate_hz);

/**
 * @brief Give the pin, the state machine and both DMA channels back.
 *
 * @param pin  GPIO number.  A pin that is not bound is ignored.
 */
void out_ppm_release(uint8_t pin);

/**
 * @brief Lay @p channels pulse widths into the frame and keep sending it.
 *
 * Returns false, and stops the output, for a set of widths that cannot be
 * laid out in the frame this pin was bound with.  A refused frame is not
 * half-written: ppm_frame() builds it whole or not at all.
 *
 * @param pin         GPIO number.
 * @param channel_us  Pulse width of each channel, in us.
 * @param channels    Number of channels, which must match the binding.
 * @return true when the frame is sent, false otherwise.
 */
bool out_ppm_write(uint8_t pin, const uint16_t *channel_us, uint8_t channels);

/**
 * @brief Stop emitting; the pin is left low.
 *
 * The next write starts it again.
 *
 * @param pin  GPIO number.  A pin that is not running is ignored.
 */
void out_ppm_stop(uint8_t pin);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OUT_PPM_H */
