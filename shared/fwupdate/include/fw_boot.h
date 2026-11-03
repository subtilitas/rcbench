/**
 * @file fw_boot.h
 * @brief The first start after an update: keep the new build, or go back.
 *
 * The bootloader is built with rollback: a build's first start is a trial
 * (pending verify), and a restart while it still is starts the build
 * before it.  fw_boot_decide() is the order of the calls that settle the
 * trial; the calls themselves are the panel's (fw_ota.c), so the host
 * suite runs the order against fakes.
 *
 * A failed start is never kept.  When marking it invalid returns -- the
 * call restarts on success -- the panel restarts anyway, which leaves the
 * trial unconfirmed and so goes back too.  Marking a good start valid can
 * fail: the build then runs this once, and the next start goes back.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_BOOT_H
#define RCBENCH_FW_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What the start check found. */
typedef enum {
    FW_BOOT_NORMAL = 0,   /**< no trial, and nothing to report            */
    FW_BOOT_UPDATED,      /**< this build's first start, now kept            */
    FW_BOOT_ROLLED_BACK,  /**< the written build did not start; this one runs */
    FW_BOOT_UNCONFIRMED,  /**< first start not kept: the next start goes back */
} fw_boot_t;

/** @brief The calls that settle a start; the panel's are fw_ota.c's. */
typedef struct {
    /** True when the running build is on its first start (a trial). */
    bool (*pending)(void *ctx);
    /** Mark the running build invalid and restart; returns only on failure. */
    void (*mark_invalid)(void *ctx);
    /** Keep the running build; false when otadata could not be written. */
    bool (*mark_valid)(void *ctx);
    /** Restart; does not return on the panel. */
    void (*restart)(void *ctx);
    /** True when a slot is marked invalid by a start that went back. */
    bool (*last_invalid)(void *ctx);
    void *ctx;   /**< passed to each call */
} fw_boot_ops_t;

/**
 * @brief Settle the start.
 *
 * A trial with @p ui_up false is marked invalid; mark_valid is never called
 * after that, whatever mark_invalid and restart return.  Without a trial,
 * FW_BOOT_ROLLED_BACK needs both @p noted and a slot marked invalid.
 *
 * @param ops   The calls that settle the start.
 * @param ui_up Whether the display and touch came up.
 * @param noted Whether a restart into a written build was noted before
 *              this start (fw_note_write()).
 * @return What the check found.
 */
fw_boot_t fw_boot_decide(const fw_boot_ops_t *ops, bool ui_up, bool noted);

/*
 * The restart note: what the start after an install reports when the
 * written build does not stay.  The installer writes it before the restart
 * into the written slot (fw_note_write()); the next start's check reads it
 * (fw_boot_settle()).  It is one entry in a store that keeps it through a
 * restart, a panic and a power cut: on the panel an NVS (non-volatile
 * storage) blob.
 *
 * The entry is FW_NOTE_BYTES bytes, little-endian: FW_NOTE_MAGIC, the
 * version text in FW_TEXT_MAX bytes with its terminator, and the FNV-1a
 * (Fowler-Noll-Vo) hash of those FW_TEXT_MAX bytes.  An entry of another
 * size, another magic, a wrong hash or a text without its terminator is
 * FW_NOTE_FOREIGN and reads as no note.
 */

/** @brief The entry's magic word, "RCN1": layout 1 of the note. */
#define FW_NOTE_MAGIC 0x314E4352uL
/** @brief Bytes in an entry: magic, version text, hash. */
#define FW_NOTE_BYTES (4u + FW_TEXT_MAX + 4u)

/** @brief What the store held. */
typedef enum {
    FW_NOTE_NONE = 0,  /**< no entry */
    FW_NOTE_FOUND,     /**< an entry in this layout with a right hash */
    FW_NOTE_FOREIGN,   /**< an entry that is not one: reads as no note */
} fw_note_state_t;

/** @brief The store that keeps the note; the panel's is fw_ota.c's. */
typedef struct {
    /** @brief Copy the entry into @p buf when it fits @p cap; return its
     *  bytes, 0 when there is none.  A larger entry returns its size
     *  uncopied. */
    uint32_t (*load)(void *ctx, uint8_t *buf, uint32_t cap);
    /** @brief Replace the entry with @p len bytes and commit; false when
     *  refused. */
    bool (*save)(void *ctx, const uint8_t *buf, uint32_t len);
    /** @brief Remove the entry and commit; false when refused. */
    bool (*erase)(void *ctx);
    void *ctx;   /**< passed to each call */
} fw_note_ops_t;

/** @brief The note a start read: its state, and the version when found. */
typedef struct {
    fw_note_state_t state;                /**< what the store held */
    char            version[FW_TEXT_MAX]; /**< FW_NOTE_FOUND: the written
                                               build's version; else "" */
} fw_note_seen_t;

/**
 * @brief Note a restart into a written build, before the restart.
 * @param ops     The store.
 * @param version The written build's version; cut to FW_TEXT_MAX - 1
 *                bytes.
 * @return False when the store refused the entry.
 */
bool fw_note_write(const fw_note_ops_t *ops, const char *version);

/**
 * @brief Read the note.
 * @param ops  The store.
 * @param seen Where the state and, for FW_NOTE_FOUND, the version go.
 * @return The state.
 */
fw_note_state_t fw_note_read(const fw_note_ops_t *ops, fw_note_seen_t *seen);

/**
 * @brief Once per start: read the note, settle the start
 *        (fw_boot_decide()) and erase the note.
 *
 * @p seen is filled before any call of @p boot, so a call that restarts
 * can report it.  The note is erased after every outcome but one: a trial
 * that goes back (FW_BOOT_UNCONFIRMED with @p ui_up false) keeps it for the
 * build before, which reports FW_BOOT_ROLLED_BACK from it.  A store that
 * held nothing is not written.
 *
 * @param boot  The calls that settle the start.
 * @param note  The store.
 * @param ui_up Whether the display and touch came up.
 * @param seen  Where the note read goes; for FW_BOOT_ROLLED_BACK its
 *              version is the written build's.
 * @return What the check found.
 */
fw_boot_t fw_boot_settle(const fw_boot_ops_t *boot, const fw_note_ops_t *note,
                         bool ui_up, fw_note_seen_t *seen);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_BOOT_H */
