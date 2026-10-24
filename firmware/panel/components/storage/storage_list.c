/**
 * @file storage_list.c
 * @brief Listing the mounted card: storage_walk(), storage_walk_counted(),
 *        storage_stat(), storage_dir_open() and its reads, and
 *        storage_path().
 *
 * Each read is made under a hold (storage_priv.h), and
 * its outcome goes back to storage.c, which decides whether the card is
 * gone.
 *
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "storage.h"
#include "storage_priv.h"
/* ff.h for FatFs's own directory read and f_stat(). */
#include "ff.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

/** @brief The log tag of the storage component. */
static const char *TAG = "storage";

void storage_path(const char *dir, const char *name, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0u) {
        return;
    }
    if (dir == NULL || dir[0] == '\0' || strcmp(dir, "/") == 0) {
        snprintf(out, out_size, "%s/%s", STORAGE_MOUNT_POINT, name);
    } else {
        snprintf(out, out_size, "%s/%s/%s", STORAGE_MOUNT_POINT, dir, name);
    }
}

/**
 * @brief Whether @p name ends in one of @p suffixes, case folded.
 * @param name      a file's name
 * @param suffixes  space-separated, lower case; NULL or "" takes every name
 * @return true for a match
 */
static bool suffix_matches(const char *name, const char *suffixes)
{
    if (suffixes == NULL || suffixes[0] == '\0') {
        return true;
    }
    size_t n = strlen(name);
    const char *p = suffixes;
    while (*p != '\0') {
        while (*p == ' ') {
            ++p;
        }
        const char *end = p;
        while (*end != '\0' && *end != ' ') {
            ++end;
        }
        size_t len = (size_t)(end - p);
        if (len > 0u && len <= n) {
            const char *tail = name + (n - len);
            size_t i = 0;
            while (i < len && tolower((unsigned char)tail[i]) == p[i]) {
                ++i;
            }
            if (i == len) {
                return true;
            }
        }
        p = end;
    }
    return false;
}

/* What one walk reads into: on the heap, because the walk runs on the task
 * that renders as well as on the run logger's, and a FILINFO with its long
 * name buffer is 280 bytes or more. */
typedef struct {
    FF_DIR dir;
    FILINFO info;
} walk_buf_t;

/* exFAT is on (firmware/panel/fatfs_exfat.cmake): a build that lost it
 * stops here rather than run without it.  A file's size then is FatFs's
 * 64-bit FSIZE_t, and goes into storage_entry_t and storage_stat_t as it
 * is. */
#if !defined(FF_FS_EXFAT) || FF_FS_EXFAT != 1
#error "FatFs without exFAT: see firmware/panel/fatfs_exfat.cmake"
#endif
_Static_assert(sizeof(FSIZE_t) <= sizeof(((storage_entry_t *)0)->size),
               "storage_entry_t.size holds every FatFs size");
_Static_assert(sizeof(FSIZE_t) <= sizeof(((storage_stat_t *)0)->size),
               "storage_stat_t.size holds every FatFs size");

/**
 * @brief The walk itself, under a hold.
 * @param dir       the folder
 * @param suffixes  the files' suffixes, or NULL
 * @param visit     the visitor, or NULL
 * @param ctx       passed to @p visit
 * @param tick      called per entry read, or NULL
 * @param fr_out    takes FatFs's answer when the walk answers -1
 * @param left_out  counts the entries the name filters left out, or NULL
 * @return storage_walk()'s answers
 */
static int walk_held(const char *dir, const char *suffixes,
                     storage_visit_fn visit, void *ctx, storage_tick_fn tick,
                     int *fr_out, unsigned *left_out)
{
    *fr_out = (int)FR_OK;
    const char *drive = storage_drive();
    if (drive[0] == '\0') {
        return -1;
    }

    /*
     * FatFs's own directory read, by drive number, rather than opendir() and
     * readdir() through the VFS (virtual file system): f_readdir() fills the
     * entry's size and attributes in the same pass, and readdir() drops
     * them.  A size taken afterwards is an f_stat(), which looks the name up
     * from the directory's first entry, one 512-byte sector per 16 entries:
     * 48 sizes on a root of 999 runs were about 2900 sector reads.
     */
    char path[STORAGE_NAME_MAX * 2];
    if (dir == NULL || dir[0] == '\0' || strcmp(dir, "/") == 0) {
        snprintf(path, sizeof(path), "%s/", drive);
    } else {
        snprintf(path, sizeof(path), "%s/%s", drive, dir);
    }

    walk_buf_t *w = malloc(sizeof(*w));
    if (w == NULL) {
        ESP_LOGW(TAG, "no memory to list %s", path);
        return -1;
    }
    FRESULT fr = f_opendir(&w->dir, path);
    if (fr != FR_OK) {
        free(w);
        /* A folder not made yet is an empty list to the caller, not a
         * volume that is gone. */
        if (fr == FR_NO_PATH || fr == FR_NO_FILE) {
            return STORAGE_WALK_NO_DIR;
        }
        *fr_out = (int)fr;
        return -1;
    }

    /*
     * The end of the directory and a read that failed are two answers: a
     * card pulled part way through a listing is a failure, and reporting it
     * as the end would tell the operator their logs had vanished rather than
     * that the card had.
     */
    int matched = 0;
    for (;;) {
        fr = f_readdir(&w->dir, &w->info);
        if (fr != FR_OK || w->info.fname[0] == '\0') {
            break;
        }
        const FILINFO *e = &w->info;
        /* Per entry read, before any filter.  What the filters reject never
         * reaches the visitor, and a root of unrelated files is the case that
         * takes longest with nothing else running. */
        if (tick != NULL) {
            tick();
        }
        if (e->fname[0] == '.') {
            /* The metadata files a Mac leaves behind: counted, so that a
             * folder holding only them is not called empty. */
            if (left_out != NULL) {
                ++*left_out;
            }
            continue;
        }
        const bool is_dir = (e->fattrib & AM_DIR) != 0;
        if (!is_dir && !suffix_matches(e->fname, suffixes)) {
            continue;
        }
        size_t len = strlen(e->fname);
        if (len >= STORAGE_NAME_MAX) {
            /* A name that does not fit cannot be reopened, so a truncated
             * version is not shown. */
            ESP_LOGW(TAG, "skipping over-long name (%u chars)", (unsigned)len);
            if (left_out != NULL) {
                ++*left_out;
            }
            continue;
        }

        storage_entry_t cur;
        memcpy(cur.name, e->fname, len + 1u);
        cur.is_dir = is_dir;
        cur.size = is_dir ? 0u : (uint64_t)e->fsize;
        ++matched;
        if (visit != NULL) {
            visit(&cur, ctx);
        }
    }
    (void)f_closedir(&w->dir);
    free(w);
    if (fr != FR_OK) {
        ESP_LOGW(TAG, "listing %s stopped: FatFs %d", path, (int)fr);
        *fr_out = (int)fr;
        return -1;              /* no volume, not an empty one */
    }

    return matched;
}

int storage_walk(const char *dir, const char *suffixes,
                 storage_visit_fn visit, void *ctx, storage_tick_fn tick)
{
    return storage_walk_counted(dir, suffixes, visit, ctx, tick, NULL);
}

int storage_walk_counted(const char *dir, const char *suffixes,
                         storage_visit_fn visit, void *ctx,
                         storage_tick_fn tick, unsigned *left_out)
{
    if (left_out != NULL) {
        *left_out = 0u;
    }
    if (!storage_hold()) {
        return -1;
    }
    int fr = (int)FR_OK;
    const int n = walk_held(dir, suffixes, visit, ctx, tick, &fr, left_out);
    /* The hold given back; a card found gone with nothing else holding it
     * is let go and the slot mounted again once, and what was walked is
     * the old card's. */
    if (storage_read_end(fr, "walk", n == -1)) {
        return STORAGE_WALK_REMOUNTED;
    }
    return n;
}

/**
 * @brief FAT's date and time stamp as local time, the way ESP-IDF's stat()
 *        reads it (components/fatfs/vfs/vfs_fat.c, update_stat_struct()):
 *        the date's bits 15..9 are years from 1980, 8..5 the month, 4..0
 *        the day; the time's 15..11 the hour, 10..5 the minute, 4..0
 *        seconds in steps of 2.
 * @param fdate  FAT's date
 * @param ftime  FAT's time
 * @return the time as mktime() gives it
 */
static int64_t fat_mtime(WORD fdate, WORD ftime)
{
    struct tm tm = {
        .tm_mday = (int)(fdate & 0x1fu),
        .tm_mon = (int)((fdate >> 5) & 0x0fu) - 1,
        .tm_year = (int)(fdate >> 9) + 80,
        .tm_sec = (int)(ftime & 0x1fu) * 2,
        .tm_min = (int)((ftime >> 5) & 0x3fu),
        .tm_hour = (int)((ftime >> 11) & 0x1fu),
        .tm_isdst = -1,
    };
    return (int64_t)mktime(&tm);
}

/**
 * @brief storage_stat() under a hold, from the path below the mount point,
 *        "" or "/...".
 * @param rel     the path below the mount point
 * @param out     takes the answer
 * @param fr_out  takes FatFs's answer
 * @return storage_stat()'s answers
 */
static esp_err_t stat_held(const char *rel, storage_stat_t *out, int *fr_out)
{
    *fr_out = (int)FR_OK;
    const char *drive = storage_drive();
    if (drive[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    if (rel[0] == '\0' || strcmp(rel, "/") == 0) {
        /* f_stat() does not take a volume's root; it is a folder. */
        memset(out, 0, sizeof(*out));
        out->is_dir = true;
        return ESP_OK;
    }

    /* On the heap, as the walk's: a FILINFO with its long name buffer is
     * 280 bytes or more, and the run logger's task calls this. */
    const size_t need = strlen(drive) + strlen(rel) + 1u;
    typedef struct {
        FILINFO info;
        char path[];
    } stat_buf_t;
    stat_buf_t *b = malloc(sizeof(*b) + need);
    if (b == NULL) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(b->path, need, "%s%s", drive, rel);
    const FRESULT fr = f_stat(b->path, &b->info);
    esp_err_t err = ESP_OK;
    if (fr == FR_OK) {
        out->is_dir = (b->info.fattrib & AM_DIR) != 0;
        out->size = out->is_dir ? 0u : (uint64_t)b->info.fsize;
        out->mtime = fat_mtime(b->info.fdate, b->info.ftime);
    } else if (fr == FR_NO_FILE || fr == FR_NO_PATH) {
        err = ESP_ERR_NOT_FOUND;
    } else {
        ESP_LOGW(TAG, "stat %s: FatFs %d", b->path, (int)fr);
        err = ESP_FAIL;
        *fr_out = (int)fr;
    }
    free(b);
    return err;
}

esp_err_t storage_stat(const char *path, storage_stat_t *out)
{
    if (path == NULL || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t mp = sizeof(STORAGE_MOUNT_POINT) - 1u;
    if (strncmp(path, STORAGE_MOUNT_POINT, mp) != 0
        || (path[mp] != '\0' && path[mp] != '/')) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!storage_hold()) {
        return ESP_ERR_INVALID_STATE;
    }
    int fr = (int)FR_OK;
    const esp_err_t err = stat_held(path + mp, out, &fr);
    (void)storage_read_end(fr, "stat", false);
    return err;
}

/** @brief An open folder: FatFs's handle, its entry, and the last read's
 *         answer for the close. */
struct storage_dir {
    FF_DIR  dir;   /**< FatFs's folder                            */
    FILINFO info;  /**< the entry read last                       */
    int     fr;    /**< the last failed read's FRESULT, or FR_OK  */
};

storage_dir_t *storage_dir_open(const char *path)
{
    const size_t mp = sizeof(STORAGE_MOUNT_POINT) - 1u;
    if (path == NULL || strncmp(path, STORAGE_MOUNT_POINT, mp) != 0
        || (path[mp] != '\0' && path[mp] != '/')) {
        return NULL;
    }
    if (!storage_hold()) {
        return NULL;
    }
    storage_dir_t *d = heap_caps_malloc(sizeof(*d), MALLOC_CAP_SPIRAM);
    if (d == NULL) {
        storage_release();
        return NULL;
    }
    d->fr = (int)FR_OK;
    /* The drive and a path of the Wi-Fi page's 384 bytes at most. */
    char full[STORAGE_NAME_MAX * 8];
    const int n = snprintf(full, sizeof(full), "%s%s", storage_drive(),
                           (path[mp] == '\0') ? "/" : path + mp);
    const FRESULT fr = (n > 0 && (size_t)n < sizeof(full))
                       ? f_opendir(&d->dir, full) : FR_INVALID_NAME;
    if (fr != FR_OK) {
        heap_caps_free(d);
        /* A folder not there is no read that failed. */
        (void)storage_read_end((fr == FR_NO_PATH || fr == FR_NO_FILE
                                || fr == FR_INVALID_NAME)
                               ? (int)FR_OK : (int)fr, "open folder", false);
        return NULL;
    }
    return d;
}

int storage_dir_read(storage_dir_t *d, char *name, size_t cap,
                     storage_stat_t *out)
{
    const FRESULT fr = f_readdir(&d->dir, &d->info);
    if (fr != FR_OK) {
        d->fr = (int)fr;
        return -1;
    }
    if (d->info.fname[0] == '\0') {
        return 0;
    }
    const size_t len = strlen(d->info.fname);
    if (len >= cap) {
        return -1;
    }
    memcpy(name, d->info.fname, len + 1u);
    out->is_dir = (d->info.fattrib & AM_DIR) != 0;
    out->size = out->is_dir ? 0u : (uint64_t)d->info.fsize;
    out->mtime = fat_mtime(d->info.fdate, d->info.ftime);
    return 1;
}

void storage_dir_close(storage_dir_t *d)
{
    if (d == NULL) {
        return;
    }
    (void)f_closedir(&d->dir);
    const int fr = d->fr;
    heap_caps_free(d);
    (void)storage_read_end(fr, "folder", false);
}
