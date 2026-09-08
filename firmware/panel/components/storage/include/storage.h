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

/** Card name from its CID, or "" when nothing is mounted. */
const char *storage_card_name(void);

/** What went wrong, for the splash line.  "" when all is well. */
const char *storage_status(void);

/** Called once per matching entry, in the order the volume holds them. */
typedef void (*storage_visit_fn)(const storage_entry_t *entry, void *ctx);

/**
 * Walk a directory, handing every matching entry to @p visit.
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
 * @return the number of matching entries, or -1 when the directory cannot be
 *         opened or read to its end.
 */
int storage_walk(const char *dir, const char *suffixes, storage_visit_fn visit,
                 void *ctx);

/** Full path for a name inside @p dir, ready for fopen(). */
void storage_path(const char *dir, const char *name, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
