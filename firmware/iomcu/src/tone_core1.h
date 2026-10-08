/*
 * Core 1's half of the phase tap, and the hand-over to core 0.
 *
 * Each tick of core 1's loop (sense_core1.c) runs one pass of the tone
 * service (shared/sense/tone_svc.h) on the capture ring (tone_cap.h): the
 * words written since the last pass into the detector, and the beeps it
 * finished and a status back to core 0.
 *
 * The hand-over.  The order, core 0 to core 1, and the status, core 1 to
 * core 0, are copied whole under one hardware spin lock, as the sensor
 * bus's are.  The beeps go the other way through a single-producer,
 * single-consumer queue of TONE_Q_LEN entries: core 1 writes the entry and
 * then the head index, core 0 reads the entry and then writes the tail
 * index, each behind a memory barrier.  A beep that finds the queue full is
 * counted and dropped.  Each entry carries the set-up and capture
 * generations it was made under, so the page drops a beep from an earlier
 * one.
 *
 * Tearing the capture down.  Core 1 reads the ring and the DMA channel's
 * write address in its pass.  After core 0 posts an order with run clear,
 * tone_core1_quiesce() waits until core 1 has finished a pass under it, so
 * no pass that saw the capture running is still reading when core 0
 * aborts the channel, releases the state machine or clears the ring.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_TONE_CORE1_H
#define RCBENCH_TONE_CORE1_H

#include <stdbool.h>
#include <stdint.h>

#include "tone_page.h"
#include "tone_svc.h"

/** Beeps the queue holds between the cores. */
#define TONE_Q_LEN 32u

/** Claim the spin lock; before core 1 starts. */
void tone_core1_init(void);

/** A new order for core 1, taken at its next tick.  @p t0_us is the
 *  microsecond timer when the capture's counter began. */
void tone_core1_order(const tone_cmd_t *cmd, uint64_t t0_us);

/** Core 0: wait until core 1 has finished a pass under the latest order.
 *  Bounded by TONE_CORE1_WAIT_US; returns at once when core 1 has not
 *  taken an order yet (at boot: the first it takes is the latest), and,
 *  after the bound, when it does not answer (parked for a flash write). */
void tone_core1_quiesce(void);

/** The longest core 0 waits in tone_core1_quiesce(), 5 ms: core 1's tick
 *  is 1 ms and the sensor pass before the tone pass takes up to 1.1 ms; the
 *  tone pass is not measured. */
#define TONE_CORE1_WAIT_US 5000u

/** Core 1's pass, once a tick. */
void tone_core1_step(void);

/**
 * Core 0: take what core 1 has handed over into @p page: the beeps in the
 * queue and the newest status.  Cheap when nothing is new.
 */
void tone_core1_sync(tone_page_t *page);

#endif /* RCBENCH_TONE_CORE1_H */
