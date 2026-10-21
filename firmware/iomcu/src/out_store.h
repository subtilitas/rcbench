/**
 * @file
 * @brief The output configuration, kept in the IOmcu's own flash.
 *
 * It lives here rather than on the panel because a binding describes wiring,
 * and this is the board the wires are in.  A panel that remembered one would
 * reapply it to whatever is on the bench now; an IOmcu that remembers
 * one reapplies it to the leads still plugged into it.
 *
 * Restoring at boot configures the slots.  It does not drive them: every
 * driver is gated by outputs_driving(), which is the bank's armed flag, and
 * main.c sets that flag only while the ARM register is set, the link is out
 * of failsafe and the heartbeat is trusted, so a restored binding claims its
 * pins and holds them at idle until somebody arms.
 *
 * What a write costs.  The flash cannot be read while it is written and this
 * core executes from it, so a write runs with interrupts off and this end
 * answers nothing for its length.  On the bring-up module an erase and program
 * together measured 19,178 us: a CAN (Controller Area Network) frame at 1
 * Mbit/s is about 130 us and the XL2515 holds two of them, so a request
 * arriving in that window is lost with nothing wrong on the wire.  That
 * measurement is the two operations inside one window; neither half of it was
 * timed on its own.  It is 1 sample on the bring-up module, with the flash part
 * not yet identified; the window is to be argued again against the part's
 * datasheet maximum once the part is known.  A lost request costs the panel
 * LINK_HOST_TIMEOUT_MS (1000 ms) of waiting, and 1000 ms of silence latches
 * this end's LINK_DEV_SILENCE_MS (200 ms) failsafe.  One lost frame is enough
 * for that, which is what makes the length of these windows a safety number
 * rather than a performance one.
 *
 * What this store does about it:
 *
 *   The sector is not erased per save.  Two sectors hold eight record
 *   slots each, a slot two 256-byte flash pages (record version 8; one page
 *   a slot and sixteen a sector before it); a save programs the next erased
 *   slot, and a sector is erased only once its records are all superseded.
 *   Seven saves in eight therefore cost one program of two pages rather
 *   than an erase and a program.
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
 * the erase covers 4,096 bytes and the program 512, and on serial NOR (not-or)
 * flash a page program takes one to two orders of magnitude less than an erase,
 * which puts the two pages somewhere between 400 and 4,000 us against the 260
 * us of frames the controller holds.  out_store_last_program_us() is what will
 * settle it. Removing the loss rather than shrinking it needs the receive
 * buffers emptied during the window, which means reading the controller over
 * SPI (Serial Peripheral Interface) from a routine held in RAM while the flash
 * is busy; that is not written.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_OUT_STORE_H
#define RCBENCH_OUT_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "out_store_rec.h"

/**
 * @brief Read what was saved.
 *
 * Takes the valid record with the highest sequence number across both
 * sectors.  A record of an earlier version (out_store_rec.h) is read too,
 * with what it does not carry -- the supply's wiring before version 4, the
 * current monitors' set-up before version 5 -- disabled.  Returns false
 * when no slot holds one -- a store never written, one written by a build
 * with a record this one does not read, or one whose records are all torn
 * -- and then @p out is untouched, so the caller keeps the defaults it
 * already had.
 *
 * @param out  Receives the configuration. Untouched when the function returns
 *             false.
 * @return True when a record was read. False when no slot holds a valid one.
 */
bool out_store_load(out_store_t *out);

/**
 * @brief Ask for @p cfg to be written.
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
 *
 * @param cfg  The configuration to save.
 * @param now_ms  The pass's clock in milliseconds. It restarts the settle
 *                window.
 */
void out_store_save(const out_store_t *cfg, uint32_t now_ms);

/**
 * @brief How long after the last write the save is taken.
 */
#define OUT_STORE_SETTLE_MS  400u

/**
 * @brief How long the bus is quiet before a window is opened in it.
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

/**
 * @brief How many sectors the store spans, so a caller reclaiming at boot
 * can bound its loop by the store's shape rather than by the flash
 * answering.
 */
#define OUT_STORE_SECTORS  2u

/**
 * @brief How long a settled save waits for a gap before taking one anyway.
 *
 * A save that waits for ever is a binding an operator set that the next boot
 * does not have.  One second is twenty of the panel's poll periods.
 */
#define OUT_STORE_GAP_WAIT_MS  1000u

/*
 * Where core 1 stands decides how a window opens.  Until either call below,
 * core 1 is taken as not launched: a window is interrupts off and the
 * operation run directly, which is what the boot's reclaims use.  The
 * caller makes one of the two calls straight after launching core 1, with
 * no store call in between.
 */

/**
 * @brief Core 1 registered for the flash lock-out: every window from now on
 * through flash_safe_execute(), which parks it in RAM.
 */
void out_store_core1_parkable(void);

/**
 * @brief Switch the store's writing off for this boot: core 1 was launched
 * and did not register for the flash lock-out.
 *
 * A direct window would fault core 1 executing from flash, and
 * flash_safe_execute() would refuse, or in a build with
 * PICO_FLASH_ASSERT_ON_UNSAFE and asserts on, stop the core. Afterwards no
 * erase or program runs and flash_safe_execute() is never called:
 * out_store_save() takes nothing, so nothing waits for a save that cannot
 * come, out_store_tick() is idle and out_store_reclaim() false. What was
 * loaded stays readable.  Nothing switches it on again.
 */
void out_store_off(void);

/**
 * @brief Whether out_store_off() has been called.
 *
 * @return True when out_store_off() has been called.
 */
bool out_store_is_off(void);

/**
 * @brief How long a window waits for core 1 to stop, and to start again
 * after.
 *
 * Every erase and program runs through flash_safe_execute(): interrupts
 * off on this core, and core 1 -- the sensor bus -- parked in RAM by the
 * multicore lock-out, because a core executing from flash while it is
 * written faults.  Core 1 answers the lock-out between two I2C
 * transactions, each held to 1 ms (sense_i2c.h), so it stops within about
 * 1.2 ms; 10 ms is that with room.  The wait for it is spent before the
 * window opens and is not counted in it.
 */
#define OUT_STORE_LOCKOUT_MS  10u

/**
 * @brief After a window core 1 did not stop for, how long before the next is
 * tried.
 */
#define OUT_STORE_REFUSED_WAIT_MS  1000u

/**
 * @brief What a pass of out_store_tick() did.
 *
 * What a pass of out_store_tick() did.
 */
typedef enum {
    OUT_STORE_IDLE = 0,   /**< nothing to do, or not yet the moment for it */
    OUT_STORE_ERASED,     /**< a sector was erased; the record follows */
    OUT_STORE_WROTE,      /**< the record is in flash */
    /** Core 1 did not stop within OUT_STORE_LOCKOUT_MS: nothing was
     *  written, the save stays asked for, and no window is tried for
     *  OUT_STORE_REFUSED_WAIT_MS. */
    OUT_STORE_REFUSED
} out_store_step_t;

/**
 * @brief Take a deferred save if it is safe to.
 *
 * Call every pass with whether the bank is driving, how long the bus has been
 * quiet, and the clock of the pass.  A save that needs an erase first erases
 * on one pass and writes on a later one, so the caller services the CAN
 * controller between the two windows rather than holding both off at once.
 *
 * @param driving  True while the bank is driving.
 * @param quiet_ms  Milliseconds since the last frame on the bus.
 * @param now_ms  The pass's clock in milliseconds.
 * @return What the pass did.
 */
out_store_step_t out_store_tick(bool driving, uint32_t quiet_ms,
                                uint32_t now_ms);

/**
 * @brief Erase a sector that holds nothing still wanted.
 *
 * The window is the same length wherever it is taken; the point is that the
 * caller picks the moment, and that the save which later finds the sector
 * ready costs a page program alone.  Returns true on the pass that erased.
 *
 * A window core 1 did not stop for erases nothing and returns false.
 *
 * One call erases at most one sector, and the call after it looks again.
 * Nothing to do is the ordinary answer: one save in eight leaves a sector
 * behind.  A store whose sectors all hold records this build cannot read --
 * an earlier record version -- has one to reclaim per sector, so a caller
 * that wants them all takes them in as many calls.
 *
 * @param driving  True while the bank is driving.
 * @param quiet_ms  Milliseconds since the last frame on the bus.
 * @return True on the pass that erased a sector.
 */
bool out_store_reclaim(bool driving, uint32_t quiet_ms);

/**
 * @brief Whether a save has been asked for and not yet reached flash.
 *
 * @return True while a save is pending.
 */
bool out_store_pending(void);

/**
 * @brief How long the last sector erase held interrupts off, in
 * microseconds.
 *
 * Zero until one has run.  The erase on its own is not measured: what the
 * bring-up module printed was 19,174 to 19,186 us over eight saves, and each
 * of those windows held an erase and a page program together.  This is what
 * separates the two.
 *
 * @return The duration of the last erase window, in microseconds.
 */
uint32_t out_store_last_erase_us(void);

/**
 * @brief How long the last page program held interrupts off, in
 * microseconds.
 *
 * Zero until one has run.  It is not measured.  It is the window
 * every save pays, so it is the number that says whether a save can still
 * cost a frame.
 *
 * @return The duration of the last program window, in microseconds.
 */
uint32_t out_store_last_program_us(void);

/**
 * @brief Which of the 16 record slots the store last used (of the 32
 * one-page slots before version 8 while the boot read a record of that
 * layout).
 *
 * The slot the last save was programmed into.  out_store_load() sets it as
 * well, so before the first save of a run it is the slot the boot read the
 * binding from, and it is zero when neither has happened.  The console line
 * prints it: it is what shows the store advancing towards its next erase.
 *
 * @return The record slot index.
 */
uint8_t out_store_last_record(void);

/*
 * The firmware update's flash access (link_update_dev.h), through the same
 * windows as the store's own: interrupts off on this core and core 1 parked,
 * or nothing at all while the store is off.  An erase or a program here
 * holds this core for its length; the panel waits for the answer to the
 * COMMIT that caused it, so no request is in flight to be lost.
 */

/**
 * @brief Whether a window may open now: the store is not off, no save is
 * waiting, and core 1 did not refuse the last one.
 *
 * @return True when a window may open.
 */
bool out_store_window_open(void);

/**
 * @brief Erase the 4 KiB sector at flash offset @p off, below the store.
 *
 * True when the erase ran.
 *
 * @param off  Flash offset of the sector, in bytes. An offset that is not a
 *             sector boundary, or that is at or past the store, is refused.
 * @return True when the erase ran.
 */
bool out_store_flash_erase(uint32_t off);

/**
 * @brief Program @p len bytes, whole 256-byte pages up to one sector, at
 * flash offset @p off, below the store.
 *
 * True when the program ran.
 *
 * @param off  Flash offset, in bytes. A null @p data, a @p len that is not a
 *             whole number of pages, or a range past the store is refused.
 * @param data  The bytes to program.
 * @param len  Number of bytes, at most one sector.
 * @return True when the program ran.
 */
bool out_store_flash_program(uint32_t off, const uint8_t *data, uint32_t len);

/**
 * @brief Run @p fn inside a window: the boot ROM's BUY, which rewrites
 * flash.
 *
 * True when it ran.
 *
 * @param fn  The function to run inside the window. NULL is refused.
 * @param arg  Passed to @p fn.
 * @return True when @p fn ran. False when no window may open.
 */
bool out_store_flash_call(void (*fn)(void *), void *arg);

#endif /* RCBENCH_OUT_STORE_H */
