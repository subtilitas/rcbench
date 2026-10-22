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
 * Three walks of the card (wa_walk.h), after wa_zip_plan() has counted its
 * entries.  The first, in wa_zip_begin(), adds up the archive's length,
 * which is known before a byte is read because stored sizes are the files'
 * sizes: the response announces it, and the browser shows progress.  It
 * keeps each entry's size, time, name length and a hash of its path in a
 * slot, 24 bytes an entry.  The second sends the entries, the third the
 * central directory; both take each entry's size and time from its slot,
 * and the third the CRC the second kept there.
 *
 * A file that grows after the first walk is sent at the size the first
 * walk found, its first bytes as they are when the second walk reads them:
 * a file only added to, as a log is, is sent as it was when the download
 * started.  An entry whose name length or path hash is not the next
 * slot's, a file or folder added after the first walk, is left out of both
 * later walks.  A walk can meet one anywhere, since a folder lists its
 * entries in the card's own order.
 *
 * ZIP64 where the format runs out: an entry of 0xFFFFFFFF bytes or more, a
 * local header at or past that offset, 0xFFFF entries or more, and a central
 * directory that large or that far in.  The value itself is the format's
 * "see ZIP64" mark, so it counts as past the limit.
 *
 * A read error, a card pulled, a file that shrank, and an entry removed or
 * renamed after the first walk end the archive as failed; the response is
 * cut off after the bytes made up to the failure, and the browser reports
 * the download failed.
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
    WA_ZIP_MEMORY,     /**< too few slots for the entries                */
} wa_zip_err_t;

typedef struct {
    uint64_t      entries;     /**< files and folders               */
    uint64_t      files;
    uint64_t      file_bytes;  /**< the files' sizes added up        */
    uint64_t      bytes;       /**< the archive's length             */
    uint64_t      cd_offset;   /**< where the central directory goes */
    unsigned long left_out;    /**< see wa_walk.h                    */
} wa_zip_plan_t;

/**
 * @brief Walk the card and add up the archive it makes, keeping nothing per
 *        entry: the counts the page shows, and the slots wa_zip_begin()
 *        needs.
 * @param fs  the card
 * @param out the plan, filled
 * @return WA_ZIP_OK with @p out filled, WA_ZIP_NO_CARD or WA_ZIP_READ.
 */
wa_zip_err_t wa_zip_plan(const wa_fs_t *fs, wa_zip_plan_t *out);

/** One entry as the first walk found it, 24 bytes. */
typedef struct {
    uint64_t size;    /**< bytes it is sent with; 0 for a folder          */
    uint32_t crc;     /**< CRC-32 of the bytes sent, kept by the second walk */
    uint32_t path;    /**< FNV-1a hash of its path, a folder's with "/"   */
    uint32_t mtime;   /**< seconds since 1980-01-01, in the DOS range     */
    uint16_t len;     /**< bytes of its name, a folder's with "/"         */
    uint16_t spare;
} wa_zip_slot_t;

/** Slots to allocate beyond the entries wa_zip_plan() counted: entries
 *  added to the card before wa_zip_begin() walks it again. */
#define WA_ZIP_SPARE 16u

typedef struct {
    const wa_fs_t *fs;
    wa_zip_plan_t  plan;
    wa_zip_slot_t *slot;      /**< one an entry, in walk order            */
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
 * @brief Begin the archive: the first walk, which fills @p z->plan and a
 *        slot for each entry.  Nothing is sent; the response announces
 *        @p z->plan.bytes.
 * @param z     the archive
 * @param fs    the card
 * @param slot  @p slots slots, or NULL for none
 * @param slots how many @p slot holds
 * @return WA_ZIP_OK; WA_ZIP_MEMORY when the card has more entries than
 *         @p slots; WA_ZIP_NO_CARD or WA_ZIP_READ when the walk fails.  On
 *         anything but WA_ZIP_OK nothing is left open and wa_zip_next()
 *         returns -1.
 */
wa_zip_err_t wa_zip_begin(wa_zip_t *z, const wa_fs_t *fs, wa_zip_slot_t *slot,
                          uint64_t slots);

/**
 * @brief The next bytes of the archive.
 * @param z   the archive
 * @param buf where they go
 * @param cap how many @p buf holds
 * @return How many, 0 when the archive is whole, -1 when it failed
 *         (wa_zip_t.err).  A failure part way through @p buf returns the
 *         bytes before it first, and -1 on the next call.  The bytes add
 *         up to plan.bytes exactly, or the archive fails.
 */
int32_t wa_zip_next(wa_zip_t *z, uint8_t *buf, uint32_t cap);

/** Close what is open; for an archive given up part way. */
void wa_zip_end_early(wa_zip_t *z);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_ZIP_H */
