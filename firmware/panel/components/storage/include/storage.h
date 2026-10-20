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

#include "card_state.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_MOUNT_POINT "/sdcard"
#define STORAGE_NAME_MAX 64

/*
 * A file's size is 64-bit: an exFAT (extended File Allocation Table) volume
 * holds files of 4 GiB and more.  FAT12, FAT16 and FAT32 stop at
 * 4 GiB less 1 byte.
 */
typedef struct {
    char name[STORAGE_NAME_MAX];
    uint64_t size;
    bool is_dir;
} storage_entry_t;

/** What storage_stat() reads of one name. */
typedef struct {
    uint64_t size;      /**< bytes; 0 for a folder */
    bool is_dir;
    int64_t mtime;      /**< local time as mktime() reads FAT's stamp */
} storage_stat_t;

/**
 * Bring up SPI2 and mount the card at /sdcard.
 *
 * Never formats.  A card that starts and does not mount is looked at once
 * more, so storage_card_state() can tell a card with no FAT (File
 * Allocation Table) volume from one that fails another way.
 *
 * @return ESP_ERR_NOT_FOUND when no card answers, which is a normal state:
 *         the bench runs without a card.  ESP_FAIL for a card FatFs does not
 *         mount, ESP-IDF's code for a card that does not start or a bus
 *         that does not come up.
 */
esp_err_t storage_init(void);

/** Unmount and free the bus.  Safe to call when nothing was mounted.
 *  Clears storage_status() and storage_card_state(). */
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

/**
 * What went wrong, in a few words for the console and the start-up line:
 * "no card", "no FAT volume", "FatFs <n>", "SPI <error>", "CS <error>" or
 * ESP-IDF's error name.  "" when all is well.  At most 47 characters.
 */
const char *storage_status(void);

/**
 * What the slot holds as the last storage_init() found it, or
 * CARD_STATE_MOUNTED while a volume is mounted.  CARD_STATE_NONE before the
 * first try and after storage_deinit().
 */
card_state_t storage_card_state(void);

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

/**
 * The size, kind and time of one file or folder, by its full path under
 * STORAGE_MOUNT_POINT, e.g. "/sdcard/BENCH/RCBENCH.LOG".
 *
 * FatFs's own f_stat(), for its 64-bit size.  stat() through the VFS
 * (virtual file system) puts the size in st_size, an off_t of 32 bits on
 * the panel: a file of 4 GiB and more comes back as its size less a
 * multiple of 4 GiB, and one of 2 GiB and more as a negative number.
 *
 * @return ESP_OK; ESP_ERR_NOT_FOUND when the volume has no such name;
 *         ESP_ERR_INVALID_STATE when nothing is mounted or @p path is not
 *         under the mount point; ESP_ERR_NO_MEM; ESP_FAIL when the card
 *         does not read.
 */
esp_err_t storage_stat(const char *path, storage_stat_t *out);

/** Full path for a name inside @p dir, ready for fopen(). */
void storage_path(const char *dir, const char *name, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
