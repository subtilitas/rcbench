/*
 * The output configuration, kept in the coprocessor's own flash.
 *
 * It lives here rather than on the panel because a binding describes wiring,
 * and this is the board the wires are in.  A panel that remembered one would
 * reapply it to whatever is on the bench now; a coprocessor that remembers
 * one reapplies it to the leads still plugged into it.
 *
 * Restoring at boot configures the slots.  It does not drive them: every
 * driver is gated by outputs_driving(), which needs the bench armed, the
 * heartbeat trusted and a command arriving, so a restored binding claims its
 * pins and holds them at idle until somebody arms.
 *
 * Writing to flash stops the processor for tens of milliseconds with
 * interrupts off, which is longer than the heartbeat's window.  The save is
 * therefore only ever taken while the bank is not driving, and out_store_tick()
 * is where that is decided; asking to save while armed defers it rather than
 * refusing it.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_OUT_STORE_H
#define RCBENCH_OUT_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"

/** What is kept: the two pages that describe the outputs, and nothing else. */
typedef struct {
    uint16_t slots[LINK_OS_COUNT];
    uint16_t chan_cfg[LINK_CC_COUNT];
} out_store_t;

/**
 * Read what was saved.
 *
 * Returns false when the sector has never been written, holds a version this
 * build does not know, or fails its check -- and then @p out is untouched, so
 * the caller keeps the defaults it already had.
 */
bool out_store_load(out_store_t *out);

/**
 * Ask for @p cfg to be written.
 *
 * Returns immediately.  Nothing reaches flash until out_store_tick() finds
 * the bank idle, and a request that matches what is already saved is dropped:
 * a page rewritten with the same content would spend an erase cycle to change
 * nothing.
 */
void out_store_save(const out_store_t *cfg);

/**
 * Take a deferred save if it is safe to.
 *
 * Call every pass with whether the bank is driving.  Returns true on the pass
 * that actually wrote, which is the pass that also lost its heartbeat edges.
 */
bool out_store_tick(bool driving);

/** Whether a save is waiting for the bench to stop driving. */
bool out_store_pending(void);

#endif /* RCBENCH_OUT_STORE_H */
