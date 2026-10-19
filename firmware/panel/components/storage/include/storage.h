/*
 * The SD (Secure Digital) card: mounting it, and listing what is on it.
 *
 * The card is alone on SPI2 (Serial Peripheral Interface), and its chip
 * select is on the I/O (input/output) expander rather than on a GPIO
 * (general-purpose input/output), so the SPI driver is configured with no
 * CS (chip select) pin and board_sd_cs() holds the line asserted while the
 * card is mounted.
 *
 * Everything above this file works in names and sizes, not paths and FILE
 * pointers, so a screen can be driven from a fake list on the host.  This
 * file holds no policy either: it reads a directory to its end and reports
 * what is in it, and which entries a caller with less room than that keeps,
 * and in what order, is decided in shared/ where the host suite tests it.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_MOUNT_POINT "/sdcard"
#define STORAGE_NAME_MAX 64

typedef struct {
    char name[STORAGE_NAME_MAX];
    uint32_t size;
    bool is_dir;
} storage_entry_t;

/**
 * Bring up SPI2 and mount the card at /sdcard.
 *
 * @return ESP_ERR_NOT_FOUND when no card answers, which is a normal state:
 *         the bench runs without a card.
 */
esp_err_t storage_init(void);

/** Unmount and free the bus.  Safe to call when nothing was mounted. */
void storage_deinit(void);

bool storage_mounted(void);

/** Card size and free space in bytes; 0 when nothing is mounted. */
uint64_t storage_total_bytes(void);
uint64_t storage_free_bytes(void);

/**
 * The card's name: its volume label, or the product name in its CID (card
 * identification register) when it has no label.  Read once per mount.
 * "" when nothing is mounted.
 */
const char *storage_card_name(void);

/** What went wrong, for the splash line.  "" when all is well. */
const char *storage_status(void);

/** Called once per matching entry, in the order the volume holds them. */
typedef void (*storage_visit_fn)(const storage_entry_t *entry, void *ctx);

typedef void (*storage_tick_fn)(void);

/** storage_walk()'s answer for a folder that is not there. */
#define STORAGE_WALK_NO_DIR (-2)

/**
 * Walk a directory, handing every matching entry to @p visit, with its size
 * (0 for a directory).
 *
 * One pass over the directory's entries and nothing else: the size comes
 * with the entry, so a caller that keeps a few does not look each of them up
 * again from the directory's start.
 *
 * The whole directory is read, and nothing here decides what is worth
 * keeping: a caller with room for fewer entries than the card holds chooses
 * which ones as they arrive.  It cannot be done by stopping the read early.
 * FAT (File Allocation Table) hands entries back in the order its directory
 * table holds them, which is creation order until a file is deleted and its
 * slot filled again, so the first entries read are the oldest files and
 * everything written after that is never seen.
 *
 * @param dir       relative to the mount point; "" or "/" for the root
 * @param suffixes  space-separated, lower case, e.g. ".csv .txt"; NULL takes
 *                  everything.  Applied to files; directories always match
 * @param visit     called with a borrowed entry that does not outlive the
 *                  call; NULL counts the matches without reporting them
 * @param ctx       passed to @p visit untouched
 * @param tick      called once per directory entry read, before any filter,
 *                  or NULL.  A caller on a task with a deadline passes the
 *                  thing that meets it: the filters reject dot-names, wrong
 *                  suffixes and over-long names without reaching @p visit, so
 *                  a root of unrelated files is read with nothing else
 *                  running.  It takes no argument and must be cheap.
 * @return the number of matching entries; STORAGE_WALK_NO_DIR (-2) when
 *         the volume is mounted and has no folder @p dir, or a file holds
 *         its name; -1 when the directory cannot be opened or read to its
 *         end, or nothing is mounted.
 */
int storage_walk(const char *dir, const char *suffixes, storage_visit_fn visit,
                 void *ctx, storage_tick_fn tick);

/** Full path for a name inside @p dir, ready for fopen(). */
void storage_path(const char *dir, const char *name, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
