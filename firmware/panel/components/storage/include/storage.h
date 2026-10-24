/**
 * @file storage.h
 * @brief The SD (Secure Digital) card: mounting it, and listing what is on
 *        it.
 *
 * The card is on SDMMC (the SD card host) slot 0, 4 data lines, its
 * supply on the ESP32-P4's on-chip LDO (low-dropout regulator) channel 4
 * (board_pins.h).
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
#include <stdio.h>

#include "card_state.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_MOUNT_POINT "/sdcard"
#define STORAGE_NAME_MAX 64

/*
 * A file's size is 64-bit: an exFAT (extended File Allocation Table) volume
 * holds files of 4GiB and more.  FAT12, FAT16 and FAT32 stop at
 * 4GiB less 1 byte.
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
 * @brief Switch the card's supply on and mount the card at /sdcard.
 *
 * Never formats.  A card that starts and does not mount is looked at once
 * more, so storage_card_state() can tell a card with no FAT (File
 * Allocation Table) volume from one that fails another way.  The first
 * call makes the lock every other call takes: the start-up's, on
 * app_main, before any task that reaches the card exists.
 *
 * @return ESP_OK when a volume is mounted, also when one already was.
 *         ESP_ERR_NOT_FOUND when no card answers, which is a normal state:
 *         the bench runs without a card.  ESP_FAIL for a card FatFs does not
 *         mount, ESP-IDF's code for a card that does not start or a supply
 *         that does not come up.  ESP_ERR_INVALID_STATE while a card found
 *         gone is still held (storage_hold()).
 */
esp_err_t storage_init(void);

/**
 * What the slot holds now, at an event that reads the card: LOGS opening,
 * RESCAN, a page of LOGS, a file opened in LOGS, a run's or a servo test's
 * start, the servo programmer looking for its card.  Not once a frame.
 *
 * A mounted volume is asked whether its card still answers: one SEND_STATUS
 * (CMD13) command through FatFs.  A card that does not is taken out or
 * swapped; the volume is let go and the slot mounted again, so the volume
 * label, the list and the run alerts are the card's in the slot now.  Held
 * (storage_hold()), the volume is let go at the last release instead, and
 * reads as no card until the next call.  An empty slot or one whose card
 * did not mount is tried again.  ESP-IDF's E lines for a card that is not
 * there are quiet for the call; the outcome is one I or W line of its own.
 *
 * @return storage_card_state() after the look.
 */
card_state_t storage_check(void);

/**
 * Keep the mounted volume from being let go: around every path call
 * (fopen(), stat(), mkdir(), unlink(), rename(), opendir()) and for as long
 * as a file or folder stays open.  Counted; every true is matched by one
 * storage_release().
 *
 * @return false with nothing mounted, or with a card found gone: the
 *         caller makes no card call.
 */
bool storage_hold(void);

/** storage_hold() of the volume of mount @p mount (storage_mount_count())
 *  only: false once another volume is mounted, so a name read from one
 *  card is not opened or deleted on the next. */
bool storage_hold_mount(unsigned mount);

/** The end of a storage_hold() that answered true.  The last release of a
 *  volume whose card was found gone lets the volume go. */
void storage_release(void);

/** fopen() under a hold that the file keeps until storage_fclose(); NULL,
 *  with no hold kept, when nothing is mounted or the open fails. */
FILE *storage_fopen(const char *path, const char *mode);

/** fclose() of a storage_fopen() file, and its hold given back.  The
 *  close is made also on a card found gone: FatFs refuses it without card
 *  traffic, and the handle and its descriptor are freed. */
int storage_fclose(FILE *f);

/** @brief Unmount and switch the card's supply off.  Safe to call when
 *         nothing was mounted.
 *  Clears storage_status() and storage_card_state().  The end of the
 *  card's use: nothing may still be open, and no hold is waited for. */
void storage_deinit(void);

/** A volume is mounted and its card was not found gone.  Lock-free. */
bool storage_mounted(void);

/** The mounts that worked since the start: another number after a
 *  storage_check() is another volume, the same card's or a new one's.
 *  Lock-free. */
unsigned storage_mount_count(void);

/** Card size and free space in bytes; 0 when nothing is mounted. */
uint64_t storage_total_bytes(void);
uint64_t storage_free_bytes(void);

/**
 * The card's name, into @p out of @p n bytes (40 hold every name): its
 * volume label, or the product name in its CID (card identification
 * register) when it has no label.  Read once per mount.  "" when nothing
 * is mounted.  A copy, so that a mount on another task cannot change it
 * under the caller.
 */
void storage_card_name(char *out, size_t n);

/**
 * @brief What went wrong, in a few words for the console and the start-up line:
 * "no card", "no FAT volume", "FatFs <n>", "LDO <error>" or
 * ESP-IDF's error name.  "" when all is well.  At most 47 characters.
 */
const char *storage_status(void);

/**
 * What the slot holds as the last mount or look found it, or
 * CARD_STATE_MOUNTED while a volume is mounted.  CARD_STATE_NONE before the
 * first try, after storage_deinit() and for a card found gone.
 * CARD_STATE_UNREADABLE for a card that mounted, answers and fails a read.
 * Lock-free: the control task and every frame may ask.
 */
card_state_t storage_card_state(void);

/** Called once per matching entry, in the order the volume holds them. */
typedef void (*storage_visit_fn)(const storage_entry_t *entry, void *ctx);

typedef void (*storage_tick_fn)(void);

/** storage_walk()'s answer for a folder that is not there. */
#define STORAGE_WALK_NO_DIR (-2)
/** storage_walk()'s answer when the walk found the card gone and the slot
 *  was mounted again: what was visited is the old card's, and a walk of
 *  the card in the slot now starts over. */
#define STORAGE_WALK_REMOUNTED (-3)

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
 * A read that fails the way a card taken out does (FatFs FR_DISK_ERR,
 * FR_NOT_READY or FR_INVALID_OBJECT) is followed by one SEND_STATUS
 * (CMD13).  A card that does not answer is gone: the volume is let go,
 * and with nothing else holding it the slot is mounted again once.  A card
 * that answers fails its reads on its own: CARD_STATE_UNREADABLE, the
 * volume let go, and no second mount.  Any other failure leaves the volume
 * mounted.
 *
 * @return the number of matching entries; STORAGE_WALK_NO_DIR (-2) when
 *         the volume is mounted and has no folder @p dir, or a file holds
 *         its name; STORAGE_WALK_REMOUNTED (-3) when the walk found the
 *         card gone and a card in the slot is mounted now; -1 when the
 *         directory cannot be opened or read to its end, or nothing is
 *         mounted.
 */
int storage_walk(const char *dir, const char *suffixes, storage_visit_fn visit,
                 void *ctx, storage_tick_fn tick);

/**
 * @brief storage_walk(), and the count of the entries its name filters
 *        left out: names starting with "." and names of STORAGE_NAME_MAX
 *        characters or more.  Entries the suffix filter leaves out are not
 *        counted.
 *
 * The file manager's walk (fm_io.h) reads a folder with no suffix filter
 * and needs every entry either visited or counted: a folder holding only
 * .DS_Store is not empty, and is neither moved nor deleted as one.
 *
 * @param dir       as storage_walk()
 * @param suffixes  as storage_walk()
 * @param visit     as storage_walk()
 * @param ctx       as storage_walk()
 * @param tick      as storage_walk()
 * @param left_out  takes the count; may be NULL.  0 when nothing is read.
 * @return as storage_walk()
 */
int storage_walk_counted(const char *dir, const char *suffixes,
                         storage_visit_fn visit, void *ctx,
                         storage_tick_fn tick, unsigned *left_out);

/**
 * @brief The size, kind and time of one file or folder, by its full path under
 * STORAGE_MOUNT_POINT, e.g. "/sdcard/BENCH/RCBENCH.LOG".
 *
 * FatFs's own f_stat(), for its 64-bit size.  stat() through the VFS
 * (virtual file system) puts the size in st_size, an off_t of 32 bits on
 * the panel: a file of 4GiB and more comes back as its size less a
 * multiple of 4GiB, and one of 2GiB and more as a negative number.
 *
 * A failure that says the card is gone lets the volume go as
 * storage_walk() does, without mounting again.
 *
 * @return ESP_OK; ESP_ERR_NOT_FOUND when the volume has no such name;
 *         ESP_ERR_INVALID_STATE when nothing is mounted or @p path is not
 *         under the mount point; ESP_ERR_NO_MEM; ESP_FAIL when the card
 *         does not read.
 */
esp_err_t storage_stat(const char *path, storage_stat_t *out);

/** @brief A folder open for storage_dir_read(). */
typedef struct storage_dir storage_dir_t;

/**
 * @brief Open a folder by its full path under STORAGE_MOUNT_POINT, "/sdcard"
 *        for the root, to read it one entry at a time.
 *
 * FatFs's own f_opendir() and f_readdir(), as storage_walk() reads: each
 * entry comes with its size, kind and time, so a listing costs one pass and
 * no f_stat() per entry.  The volume is held (storage_hold()) until
 * storage_dir_close().  The handle is in PSRAM (pseudo-static random-access
 * memory).
 *
 * @param path  the folder's full path
 * @return the folder, or NULL when nothing is mounted, @p path is not under
 *         the mount point or names no folder, or no memory is left
 */
storage_dir_t *storage_dir_open(const char *path);

/**
 * @brief The next entry of @p d.
 * @param d     the folder
 * @param name  takes the entry's name, NUL-terminated
 * @param cap   bytes of @p name
 * @param out   takes its size (0 for a folder), kind and time
 * @return 1 for an entry, 0 at the end, -1 on a failed read or a name of
 *         @p cap bytes or more
 */
int storage_dir_read(storage_dir_t *d, char *name, size_t cap,
                     storage_stat_t *out);

/**
 * @brief Close @p d and give its hold back.  A failed read is weighed as
 *        storage_walk() weighs one, without mounting again.
 * @param d  the folder; NULL does nothing
 */
void storage_dir_close(storage_dir_t *d);

/** Full path for a name inside @p dir, ready for fopen(). */
void storage_path(const char *dir, const char *name, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
