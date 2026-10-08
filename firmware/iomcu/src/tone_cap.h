/*
 * The phase tap's capture: one PIO (programmable input/output) state
 * machine on one input pin (tone_cap.pio) and a DMA (direct memory access)
 * channel that collects its words in a ring of TONE_RING_WORDS words,
 * endlessly, with the write address wrapping on the ring.
 *
 * Core 0 starts and stops it, from the TONE page's set-up.  Core 1 reads
 * the ring and the channel's write position (tone_core1.c), and the state
 * machine's overflow flag.  The ring is cleared when the capture starts.  Nothing here drives the pin: it is an input
 * for the PIO, and an input with its pull-down on while the tap is off.
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

/**
 * Claim a state machine and a DMA channel, load the program, start the
 * capture on @p pin.  False, with nothing claimed, when no PIO block that
 * reaches the pin has room for the program and a state machine, no DMA
 * channel is free, or the system clock is not 150 MHz.
 */
bool tone_cap_start(uint8_t pin);

/** Mark the capture not running, so core 1 reads nothing from it from its
 *  next pass on; the state machine and the channel stay.  The first step
 *  of taking the capture down: tell core 1, wait for it
 *  (tone_core1_quiesce()), then tone_cap_stop(). */
void tone_cap_pause(void);

/** Stop the capture and give back what it claimed; the pin is left an
 *  input with its pull-down on.  Safe when it is not running.  Core 1 is
 *  not reading the ring or the channel at that moment (tone_cap_pause()). */
void tone_cap_stop(void);

bool tone_cap_running(void);

/** The microsecond timer when the counter began. */
uint64_t tone_cap_start_us(void);

/** The pin as an input with its pull-down on, driven by nothing: a tap
 *  that is off.  Not while the capture runs. */
void tone_cap_rest(uint8_t pin);

/** The ring. */
const uint32_t *tone_cap_ring(void);

/** The slot the DMA writes next; 0 when the capture does not run. */
uint32_t tone_cap_wr(void);

/** Whether the state machine dropped a word (its FIFO was full) since the
 *  last call; clears the flag.  Called from core 1 only. */
bool tone_cap_stalled(void);

#endif /* RCBENCH_TONE_CAP_H */
