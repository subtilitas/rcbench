/**
 * @file fw_run.h
 * @brief The run note: an update run that goes on after a restart.
 *
 * An update run has two restarts inside it: after the panel's install and
 * after the C6 radio's update.  What lies on the other side of each is not
 * stored (the steps are computed from live facts), with one exception: the
 * fact that a run stands.  The run note keeps it in a store that a
 * restart, a panic and a power cut keep: on the panel an NVS (non-volatile
 * storage) blob beside the restart note (fw_boot.h).
 *
 * Written before a restart (fw_run_begin()): by the install once the image
 * is written whole and checked (fw_run_after_install()), and by the C6
 * update on success with the running version.  Read once per start
 * (fw_run_settle()): each start spends one of FW_RUN_OPENS openings, a
 * start of another build than the note expects (a rollback) ends the run,
 * and so does the last opening.  Erased by fw_run_end().
 *
 * The entry is FW_RUN_BYTES bytes: FW_RUN_LAYOUT, the expected version
 * text in FW_RUN_TEXT_MAX bytes padded with zeros (no terminator at full
 * length), and the openings left, 1 to FW_RUN_OPENS.  An entry of another
 * size, another layout byte, an opening count outside 1 to FW_RUN_OPENS,
 * or a text that is empty, holds a byte outside 0x21 to 0x7E, or has a
 * byte after its padding starts reads as no note and is erased.
 *
 * The store's calls are fw_note_ops_t's: load, save, erase.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_RUN_H
#define RCBENCH_FW_RUN_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_boot.h"
#include "fw_install.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The layout byte: magic and layout 1 in one. */
#define FW_RUN_LAYOUT   0xB1u
/** @brief Bytes of the expected version text, without a terminator. */
#define FW_RUN_TEXT_MAX 16u
/** @brief Openings a run starts with: the bound on a restart loop. */
#define FW_RUN_OPENS    2u
/** @brief Bytes in an entry: layout, version text, openings left. */
#define FW_RUN_BYTES    (1u + FW_RUN_TEXT_MAX + 1u)

/** @brief The store that keeps the run note: the restart note's shape. */
typedef fw_note_ops_t fw_run_ops_t;

/** @brief What a start's fw_run_settle() found. */
typedef enum {
    FW_RUN_NONE = 0,  /**< no entry: no run                                */
    FW_RUN_STANDS,    /**< a run stands; this start spent one opening      */
    FW_RUN_FOREIGN,   /**< an entry that is not a note: erased, no run     */
    FW_RUN_MISMATCH,  /**< another build runs than expected: erased, no run */
    FW_RUN_UNSPENT,   /**< the store refused to spend an opening: no run   */
} fw_run_seen_t;

/**
 * @brief One start's view of the run.  Zeroed at the start; filled by
 *        fw_run_settle(), changed by fw_run_begin() and fw_run_end().
 */
typedef struct {
    bool          settled;   /**< fw_run_settle() ran in this start        */
    bool          active;    /**< a run stands in this start               */
    bool          clear;     /**< the store is known to hold no entry      */
    fw_run_seen_t seen;      /**< what fw_run_settle() found               */
    uint8_t       opens_left; /**< FW_RUN_STANDS: openings after this one  */
    char          expect[FW_RUN_TEXT_MAX + 1u]; /**< the note's version,
                                                     terminated; else ""  */
} fw_run_t;

/**
 * @brief Note a run before a restart: @p expect with FW_RUN_OPENS
 *        openings.
 *
 * A text that is empty, longer than FW_RUN_TEXT_MAX bytes or holds a byte
 * outside 0x21 to 0x7E is refused: no entry is written and an entry that
 * stands is erased, so the next start finds no run.
 *
 * @param ops    The store.
 * @param run    This start's view; its @c clear follows the store.
 * @param expect The version the next start is expected to run.
 * @return False when the text or the store refused the entry.
 */
bool fw_run_begin(const fw_run_ops_t *ops, fw_run_t *run, const char *expect);

/**
 * @brief After an install's last step: fw_run_begin() when the image is
 *        written whole and checked (FW_INSTALL_DONE).  Any other status
 *        writes and erases nothing.
 *
 * @param ops     The store.
 * @param run     This start's view.
 * @param st      The install's last fw_install_step() status.
 * @param version The written build's version.
 * @return True when the run was noted.
 */
bool fw_run_after_install(const fw_run_ops_t *ops, fw_run_t *run,
                          fw_install_status_t st, const char *version);

/**
 * @brief Once per start: read the note and spend one opening.
 *
 * In order: no entry is FW_RUN_NONE.  An entry that is not a note is
 * erased: FW_RUN_FOREIGN.  A note whose version is not @p running is
 * erased: FW_RUN_MISMATCH.  Otherwise one opening is spent: the count less
 * one is saved, or the entry erased when that is 0; the run stands
 * (FW_RUN_STANDS) only when the store took the spend, else FW_RUN_UNSPENT.
 * A later call in the same start returns the first one's result and calls
 * nothing.
 *
 * @param ops     The store.
 * @param run     This start's view, zeroed at the start.
 * @param running The running build's version.
 * @return What the start found; @c run->active is true for FW_RUN_STANDS.
 */
fw_run_seen_t fw_run_settle(const fw_run_ops_t *ops, fw_run_t *run,
                            const char *running);

/**
 * @brief End the run: no run stands for the rest of this start, and the
 *        entry is erased unless the store is known to hold none.
 *
 * A store that refuses the erase still ends the run in this start; the
 * entry then left spends its openings at the next starts.
 *
 * @param ops The store.
 * @param run This start's view.
 * @return True when the store is known to hold no entry afterwards.
 */
bool fw_run_end(const fw_run_ops_t *ops, fw_run_t *run);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_RUN_H */
