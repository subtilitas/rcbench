/**
 * @file
 * @brief The pass-through on the RP2350B: MIRROR and CAPTURE (pass.pio) in
 *        PIO0, the capture's two chained DMA (direct memory access)
 *        channels and their ring, and each servo output's pad.
 *
 * PIO0's GPIO base is set to 16 and both programs are loaded there at
 * boot, before any output slot binds: MIRROR (1 word) and CAPTURE (5
 * words), 2 state machines.  The block's other 2 state machines and 26
 * words stay for the PD mini UART.  The pins are fixed by the board:
 * output k on GPIO 16 + 2k, input k on GPIO 17 + 2k, k = 0 to 8.
 *
 * The capture.  CAPTURE pushes the 18 pins on every change.  DMA channel A
 * moves each state from the RX FIFO (first in, first out) to states[i];
 * when it completes it triggers channel B, which copies TIMER0's raw low
 * word, the microsecond timer, to stamps[i], and triggers A again.  Both
 * write addresses wrap on 32KiB rings of PASS_RING_EVENTS words, aligned to
 * their size.  pass_cap_head() is B's index: every event before it is
 * whole.  The stamp is taken when B runs, within about 1us of the push.
 *
 * The inputs.  The 9 input pins are SIO (software-controlled I/O) inputs
 * with the pad's pull-down; the carrier's input buffers drive them.  They
 * are captured in every state, SAFE included.
 *
 * The outputs.  pass_cap_route() gives an output pad to MIRROR (function
 * PIO0) or drives it low from SIO.  Nothing here touches an output pad the
 * caller does not route: a pad a PWM slice or a slot drives keeps its
 * function.  The capture reads each output pad's input, so it sees what
 * the servo receives whatever drives it.
 *
 * Called from core 0 only.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PASS_CAP_H
#define RCBENCH_PASS_CAP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The first pin of the capture: output 0. */
#define PASS_CAP_PIN0 16u

/**
 * @brief PIO0 at GPIO base 16, MIRROR and CAPTURE loaded and running, the
 *        two DMA channels claimed and the ring cleared; the input pins
 *        inputs with their pull-downs.
 *
 * False, with nothing claimed, when PIO0 already holds a program (its GPIO
 * base cannot change then), has no 2 state machines or 6 words free, or no
 * 2 DMA channels are free.
 *
 * @return true when the capture and MIRROR run.
 */
bool pass_cap_start(void);

/**
 * @brief Whether pass_cap_start() succeeded.
 *
 * @return true while the capture runs.
 */
bool pass_cap_running(void);

/**
 * @brief The states, PASS_RING_EVENTS of them.
 *
 * @return The ring.
 */
const volatile uint32_t *pass_cap_states(void);

/**
 * @brief The stamps, PASS_RING_EVENTS of them.
 *
 * @return The ring.
 */
const volatile uint32_t *pass_cap_stamps(void);

/**
 * @brief The index the stamp channel writes next; 0 when not running.
 *
 * @return An index into the ring.
 */
uint32_t pass_cap_head(void);

/**
 * @brief Whether CAPTURE dropped a state (its FIFO full) since the last
 *        call; clears the flag.
 *
 * @return true when a state was dropped.
 */
bool pass_cap_stalled(void);

/**
 * @brief Output @p k's pad: MIRROR's (function PIO0), or driven low from
 *        SIO.
 *
 * @param k       Output 0 to 8; others are ignored.
 * @param mirror  true for MIRROR, false for low.
 */
void pass_cap_route(unsigned k, bool mirror);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PASS_CAP_H */
