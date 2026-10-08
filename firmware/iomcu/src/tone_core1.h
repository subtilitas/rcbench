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

/** Core 1's pass, once a tick. */
void tone_core1_step(void);

/**
 * Core 0: take what core 1 has handed over into @p page: the beeps in the
 * queue and the newest status.  Cheap when nothing is new.
 */
void tone_core1_sync(tone_page_t *page);

#endif /* RCBENCH_TONE_CORE1_H */
