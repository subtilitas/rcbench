/*
 * Core 1: the sensor bus, read on a 1 ms tick, and the hand-over to core 0.
 *
 * Core 0 runs the link, the outputs, the supply and the flash store, as
 * before; it never touches the I2C (Inter-Integrated Circuit) block.
 * Core 1 runs one loop and nothing else: each 1 ms it takes core 0's
 * latest order, runs one step of sense_svc (shared/sense/sense_svc.h) --
 * the schedule, the parts, the bus clear -- and hands back a snapshot.
 * It then runs one pass of the phase tap's tone service (tone_core1.h),
 * which reads the capture ring and has no part in the bus.
 *
 * The hand-over is two structs, one each way, each copied whole under one
 * hardware-sync spin lock with interrupts off on the core that holds it.
 * The lock covers a memcpy and nothing else: the order is 64 bytes, the
 * snapshot 232 (arm-none-eabi, this build), so a core waits for the other
 * at most one copy.
 * Neither core holds the lock across anything that can block.  Core 0
 * checks a counter before it takes the lock, so a pass with nothing new
 * costs one load.
 *
 * The flash store's windows stop core 1: out_store.c runs its erases and
 * programs through flash_safe_execute(), and core 1 registers as the
 * lock-out victim before its first tick.  sense_core1_start() returns once
 * it has, so no save can find it unregistered; if it never does, the
 * caller switches the store off (out_store_off()) and no window opens.
 *
 * Time: both cores read the same 64-bit microsecond timer (time_us_64()),
 * which is the clock sense_sched stamps its samples with and the clock
 * the PWM edge is stamped on.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_SENSE_CORE1_H
#define RCBENCH_SENSE_CORE1_H

#include <stdbool.h>
#include <stdint.h>

#include "sense_svc.h"

/** The tick. */
#define SENSE_TICK_US 1000u

/** How long core 0 waits at launch for core 1 to register for the flash
 *  lock-out.  It takes microseconds; the bound only keeps a core 1 that
 *  never starts from stopping core 0. */
#define SENSE_CORE1_START_MS 100u

/**
 * Launch core 1 with @p first as its first order.  Returns whether core 1
 * registered for the flash lock-out within SENSE_CORE1_START_MS.  False
 * means no flash window may open this boot: flash_safe_execute() would
 * refuse it, or assert where asserts are on.
 */
bool sense_core1_start(const sense_cmd_t *first);

/** A new order for core 1, taken at its next tick. */
void sense_core1_order(const sense_cmd_t *cmd);

/** The newest snapshot into @p out when one has arrived since the last
 *  call; false, and @p out untouched, when none has. */
bool sense_core1_snapshot(sense_snap_t *out);

#endif /* RCBENCH_SENSE_CORE1_H */
