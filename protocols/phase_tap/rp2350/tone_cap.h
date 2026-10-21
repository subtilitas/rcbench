/**
 * @file
 * @brief The phase tap's capture: one PIO (programmable input/output) state
 *        machine on one input pin (tone_cap.pio) and a DMA (direct memory
 *        access) channel that collects its words in a ring of TONE_RING_WORDS
 *        words, endlessly, with the write address wrapping on the ring.
 *
 * Core 0 starts and stops it.  Core 1 reads the ring and the channel's
 * write position, with tone_svc_step() or a reader of its own, and the
 * state machine's overflow flag.  The ring is cleared when the capture
 * starts.  Nothing here drives the pin: it is an input for the PIO, and an
 * input with its pull-down on while the tap is off.
 *
 * Pull-down.  The pin's pull-down is on while the tap is disabled, so a
 * tap wire connected to nothing reads low, and stays on while the tap
 * runs: against the series resistor of 4.7 kOhm on the phase it divides a
 * floating 3.7 V phase to 3.2 V or more, over the input's 2.0 V threshold.
 *
 * Time.  The counter starts at 0 when the state machine is enabled.
 * tone_cap_start_us() is the microsecond timer at that instant, to about
 * 1 us: the present tick is the timer less this, 37.5 ticks a microsecond
 * (tone_svc_ticks()).  The image is built for the 150 MHz system clock; the
 * capture is refused at any other.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_TONE_CAP_H
#define RCBENCH_TONE_CAP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Claim a state machine and a DMA channel, load the program, start the
 *        capture on @p pin.
 *
 * False, with nothing claimed, when no PIO block that reaches the pin has room
 * for the program and a state machine, no DMA channel is free, or the system
 * clock is not 150 MHz.
 *
 * @param pin  GPIO number of the phase input.
 * @return     true when the capture runs.
 */
bool tone_cap_start(uint8_t pin);

/**
 * @brief Mark the capture not running, so the next order tells core 1 to read
 *        nothing from it; the state machine and the channel stay, and
 *        tone_cap_wr() and tone_cap_stalled() still read them for a pass
 *        already under way.
 *
 * True when it was running.  The first step of taking the capture down: tell
 * core 1, wait for it to acknowledge, then tone_cap_stop().
 *
 * @return  true when it was running.
 */
bool tone_cap_pause(void);

/**
 * @brief Mark a paused capture running again, when core 1 did not acknowledge
 *        the pause and nothing was taken down.
 */
void tone_cap_resume(void);

/**
 * @brief Stop the capture and give back what it claimed; the pin is left an
 *        input with its pull-down on.
 *
 * Safe when it is not running.  Core 1 is not reading the ring or the channel
 * at that moment (tone_cap_pause()).
 */
void tone_cap_stop(void);

/**
 * @brief Whether the capture runs.
 *
 * @return  true while the capture runs.
 */
bool tone_cap_running(void);

/**
 * @brief The microsecond timer when the counter began.
 *
 * @return The microsecond timer value when the counter began.
 */
uint64_t tone_cap_start_us(void);

/**
 * @brief The pin as an input with its pull-down on, driven by nothing: a tap
 *        that is off.
 *
 * Not while the capture runs.
 *
 * @param pin  GPIO number of the phase input.
 */
void tone_cap_rest(uint8_t pin);

/**
 * @brief The ring.
 *
 * @return The ring of edge words.
 */
const uint32_t *tone_cap_ring(void);

/**
 * @brief The slot the DMA writes next; 0 when the capture does not run.
 *
 * @return The slot the DMA writes next, 0 when the capture does not run.
 */
uint32_t tone_cap_wr(void);

/**
 * @brief Whether the state machine dropped a word (its FIFO was full) since the
 *        last call; clears the flag.
 *
 * Called from core 1 only.
 *
 * @return  true when a word was dropped since the last call.
 */
bool tone_cap_stalled(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_TONE_CAP_H */
