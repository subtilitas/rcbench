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
 * What a write costs.  The flash cannot be read while it is written and this
 * core executes from it, so a write runs with interrupts off and this end
 * answers nothing for its length.  On the bring-up module an erase and
 * program together measured 19,178 us: a CAN (Controller Area Network) frame
 * at 1 Mbit/s is about 130 us and the XL2515 holds two of them, so a request
 * arriving in that window is lost with nothing wrong on the wire.  That
 * measurement is the two operations inside one window; neither half of it was
 * timed on its own.  A lost request costs the panel LINK_HOST_TIMEOUT_MS
 * (1000 ms) of waiting, and 1000 ms of silence latches this end's
 * LINK_DEV_SILENCE_MS (200 ms) failsafe.  One lost frame is enough for that,
 * which is what makes the length of these windows a safety number rather than
 * a performance one.
 *
 * What this store does about it:
 *
 *   The sector is not erased per save.  Two sectors hold sixteen record
 *   slots each; a save programs the next erased slot, and a sector is erased
 *   only once its records are all superseded.  Fifteen saves in sixteen
 *   therefore cost one page program rather than an erase and a program.
 *
 *   The erase is taken before the save that needs it, by out_store_reclaim():
 *   at boot before the CAN controller is started, and otherwise on the first
 *   pass after the save that left a sector behind.  That pass is milliseconds
 *   after the record, not a lull chosen later; what it buys is that an erase
 *   and the record it makes room for are never one window, and that the save
 *   which finds the sector ready costs a page program alone.
 *
 *   Both wait for a gap in the traffic; see OUT_STORE_QUIET_MS.
 *
 * The window is not eliminated.  A page program is not measured on this part:
 * the erase covers 4,096 bytes and the program 256, and the two differ by one
 * to two orders of magnitude on serial NOR (not-or) flash, which puts it
 * somewhere between 200 and 2,000 us against the 260 us of frames the
 * controller holds.  out_store_last_program_us() is what will settle it.
 * Removing the loss rather than shrinking it needs the receive buffers
 * emptied during the window, which means reading the controller over SPI
 * (Serial Peripheral Interface) from a routine held in RAM while the flash is
 * busy; that is not written.
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
 * Takes the valid record with the highest sequence number across both
 * sectors.  Returns false when no slot holds one -- a store never written,
 * one written by a build with a different record in it, or one whose records
 * are all torn -- and then @p out is untouched, so the caller keeps the
 * defaults it already had.
 */
bool out_store_load(out_store_t *out);

/**
 * Ask for @p cfg to be written.
 *
 * Returns immediately.  Nothing reaches flash until out_store_tick() finds
 * the bank idle, the request settled and the bus quiet, and a request that
 * matches what is already saved is dropped: a record written with the same
 * content would spend a slot to change nothing.
 *
 * @p now_ms restarts the settle window.  The two pages that describe the
 * outputs are written in two transactions -- CHAN_CFG, then OUTPUTS -- and
 * saving between them would record a new channel configuration against the
 * slots from before it, which is what would come back at the next boot.
 * Waiting for the writes to stop is what keeps the pair together.
 */
void out_store_save(const out_store_t *cfg, uint32_t now_ms);

/** How long after the last write the save is taken. */
#define OUT_STORE_SETTLE_MS  400u

/**
 * How long the bus is quiet before a window is opened in it.
 *
 * The panel polls every 50 ms and each poll is a run of back-to-back
 * transactions, so silence longer than any gap inside a run means the run is
 * over.  Five milliseconds is that.
 *
 * It is a minimum and nothing more.  The test says the last frame was 5 ms
 * ago; it does not say how much of the gap is left, so a window can open in
 * front of the next run as well as behind the last one.  A run that has just
 * ended leaves up to 45 ms before the next, which is more than the 19,178 us
 * an erase and a program together measured; a 5 ms lull inside a run leaves
 * whatever the run has left.  Nothing enforces the pattern either -- a write
 * from a screen arrives when a finger moves.  This narrows the odds rather
 * than removing them.
 */
#define OUT_STORE_QUIET_MS  5u

/*
 * How many sectors the store spans, so a caller reclaiming at boot can bound
 * its loop by the store's shape rather than by the flash answering.
 */
#define OUT_STORE_SECTORS  2u

/**
 * How long a settled save waits for a gap before taking one anyway.
 *
 * A save that waits for ever is a binding an operator set that the next boot
 * does not have.  One second is twenty of the panel's poll periods.
 */
#define OUT_STORE_GAP_WAIT_MS  1000u

/** What a pass of out_store_tick() did. */
typedef enum {
    OUT_STORE_IDLE = 0,   /**< nothing to do, or not yet the moment for it */
    OUT_STORE_ERASED,     /**< a sector was erased; the record follows */
    OUT_STORE_WROTE       /**< the record is in flash */
} out_store_step_t;

/**
 * Take a deferred save if it is safe to.
 *
 * Call every pass with whether the bank is driving, how long the bus has been
 * quiet, and the clock of the pass.  A save that needs an erase first erases
 * on one pass and writes on a later one, so the caller services the CAN
 * controller between the two windows rather than holding both off at once.
 */
out_store_step_t out_store_tick(bool driving, uint32_t quiet_ms,
                                uint32_t now_ms);

/**
 * Erase a sector that holds nothing still wanted.
 *
 * The window is the same length wherever it is taken; the point is that the
 * caller picks the moment, and that the save which later finds the sector
 * ready costs a page program alone.  Returns true on the pass that erased.
 *
 * One call erases at most one sector, and the call after it looks again.
 * Nothing to do is the ordinary answer: one save in sixteen leaves a sector
 * behind.  A store whose sectors all hold records this build cannot read --
 * an earlier record version -- has one to reclaim per sector, so a caller
 * that wants them all takes them in as many calls.
 */
bool out_store_reclaim(bool driving, uint32_t quiet_ms);

/** Whether a save has been asked for and not yet reached flash. */
bool out_store_pending(void);

/**
 * How long the last sector erase held interrupts off, in microseconds.
 *
 * Zero until one has run.  The erase on its own is not measured: what the
 * bring-up module printed was 19,174 to 19,186 us over eight saves, and each
 * of those windows held an erase and a page program together.  This is what
 * separates the two.
 */
uint32_t out_store_last_erase_us(void);

/**
 * How long the last page program held interrupts off, in microseconds.
 *
 * Zero until one has run, and not measured on hardware.  It is the window
 * every save pays, so it is the number that says whether a save can still
 * cost a frame.
 */
uint32_t out_store_last_program_us(void);

/**
 * Which of the 32 record slots the store last used.
 *
 * The slot the last save was programmed into.  out_store_load() sets it as
 * well, so before the first save of a run it is the slot the boot read the
 * binding from, and it is zero when neither has happened.  The console line
 * prints it: it is what shows the store advancing towards its next erase.
 */
uint8_t out_store_last_record(void);

#endif /* RCBENCH_OUT_STORE_H */
