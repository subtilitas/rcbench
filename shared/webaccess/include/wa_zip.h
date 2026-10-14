/*
 * The whole card as one ZIP archive, written as it is sent.
 *
 * Stored, not compressed (method 0): deflate would cost the panel's cores
 * and time, and log files are what the card mostly holds.  No temporary
 * file and no copy of the archive: each file is read off the card once,
 * a piece at a time, and its CRC-32 (cyclic redundancy check) is taken on
 * the way and written after it in a data descriptor (general purpose bit 3).
 * Names are UTF-8 (bit 11), paths relative to the card's root, a folder an
 * entry of its own ending in "/", times the files' own as DOS (Disk
 * Operating System) date and time, 1980-01-01 00:00:00 for anything older.
 *
 * Three walks of the card (wa_walk.h).  The first adds up the archive's
 * length, which is known before a byte is read because stored sizes are
 * the files' sizes: the response announces it, and the browser shows
 * progress.  The second sends the entries.  The third sends the central
 * directory, with the CRCs the second kept, 4 bytes an entry.
 *
 * ZIP64 where the format runs out: an entry of 0xFFFFFFFF bytes or more, a
 * local header at or past that offset, 0xFFFF entries or more, and a central
 * directory that large or that far in.  The value itself is the format's
 * "see ZIP64" mark, so it counts as past the limit.
 *
 * A read error, a card pulled, or a card that no longer walks as it did
 * ends the archive as failed; the response is cut off and the browser
 * reports the download failed.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_ZIP_H
#define RCBENCH_WA_ZIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wa_walk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WA_ZIP_MAX32 0xFFFFFFFFu
#define WA_ZIP_MAX16 0xFFFFu

/** CRC-32 as ZIP takes it, continued from @p crc (0 to start). */
uint32_t wa_crc32(uint32_t crc, const void *data, size_t n);

/** @p t seconds since 1970 as DOS time and date; before 1980 is 1980-01-01
 *  00:00:00, after 2107 is 2107-12-31 23:59:58. */
void wa_dos_time(int64_t t, uint16_t *time, uint16_t *date);

/* -------------------------------------------------- the pieces, by size */

/** Whether an entry of @p size bytes needs ZIP64 in its own headers. */
bool wa_zip_entry64(uint64_t size);
/** Whether the end of an archive needs the ZIP64 records. */
bool wa_zip_end64(uint64_t entries, uint64_t cd_bytes, uint64_t cd_offset);

uint32_t wa_zip_local_bytes(size_t name_len, uint64_t size);
uint32_t wa_zip_desc_bytes(uint64_t size);
uint32_t wa_zip_central_bytes(size_t name_len, uint64_t size,
                              uint64_t offset);
uint32_t wa_zip_end_bytes(uint64_t entries, uint64_t cd_bytes,
                          uint64_t cd_offset);

/* Each writes the piece into @p out, which holds the size above, and
 * returns that size. */
size_t wa_zip_local(uint8_t *out, const char *name, size_t name_len,
                    uint64_t size, int64_t mtime, bool dir);
size_t wa_zip_desc(uint8_t *out, uint32_t crc, uint64_t size);
size_t wa_zip_central(uint8_t *out, const char *name, size_t name_len,
                      uint64_t size, uint32_t crc, uint64_t offset,
                      int64_t mtime, bool dir);
size_t wa_zip_end(uint8_t *out, uint64_t entries, uint64_t cd_bytes,
                  uint64_t cd_offset);

/** The largest any one piece is. */
#define WA_ZIP_PIECE_MAX (46u + (WA_PATH_MAX + 1u) + 28u + 56u + 20u + 22u)

/* -------------------------------------------------------- the length */

/** An archive's length, an entry at a time. */
typedef struct {
    uint64_t entries;
    uint64_t at;        /**< where the next local header goes */
    uint64_t cd;        /**< bytes of central directory       */
} wa_zip_len_t;

void wa_zip_len_init(wa_zip_len_t *l);
/** One entry, its name @p name_len bytes (a folder's with its "/"). */
void wa_zip_len_add(wa_zip_len_t *l, size_t name_len, uint64_t size);
/** The archive's length with what was added. */
uint64_t wa_zip_len_total(const wa_zip_len_t *l);

/* -------------------------------------------------------- the archive */

typedef enum {
    WA_ZIP_OK = 0,
    WA_ZIP_NO_CARD,
    WA_ZIP_READ,       /**< a read failed, or the card changed under it */
    WA_ZIP_MEMORY,     /**< too few CRC slots for the entries            */
} wa_zip_err_t;

typedef struct {
    uint64_t      entries;     /**< files and folders               */
    uint64_t      files;
    uint64_t      file_bytes;  /**< the files' sizes added up        */
    uint64_t      bytes;       /**< the archive's length             */
    uint64_t      cd_offset;   /**< where the central directory goes */
    unsigned long left_out;    /**< see wa_walk.h                    */
} wa_zip_plan_t;

/** The first walk.  WA_ZIP_OK with @p out filled, or why not. */
wa_zip_err_t wa_zip_plan(const wa_fs_t *fs, wa_zip_plan_t *out);

typedef struct {
    const wa_fs_t *fs;
    wa_zip_plan_t  plan;
    uint32_t      *crc;
    uint64_t       index;     /**< the entry being sent                   */
    uint64_t       at;        /**< bytes of archive produced              */
    uint64_t       cd_at;     /**< the third walk's local header offset   */
    uint64_t       cd_bytes;
    int            phase;
    bool           in_file;
    void          *file;
    uint64_t       left;      /**< bytes of the open file still to send   */
    uint64_t       size;
    uint32_t       file_crc;
    uint8_t        piece[WA_ZIP_PIECE_MAX];
    size_t         piece_len;
    size_t         piece_off;
    wa_zip_err_t   err;
    wa_walk_t      walk;
} wa_zip_t;

/**
 * Begin the archive @p plan describes, with @p crc slots for its entries,
 * @p slots of them.  WA_ZIP_MEMORY when that is fewer than the entries.
 */
wa_zip_err_t wa_zip_begin(wa_zip_t *z, const wa_fs_t *fs,
                          const wa_zip_plan_t *plan, uint32_t *crc,
                          uint64_t slots);

/**
 * The next bytes into @p buf of @p cap: how many, 0 when the archive is
 * whole, -1 when it failed (wa_zip_t.err).  The bytes add up to
 * plan.bytes exactly, or the archive fails.
 */
int32_t wa_zip_next(wa_zip_t *z, uint8_t *buf, uint32_t cap);

/** Close what is open; for an archive given up part way. */
void wa_zip_end_early(wa_zip_t *z);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_ZIP_H */
