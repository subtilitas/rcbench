/*
 * Servo configuration files on the card: names, saving, listing, loading.
 *
 * The files live in the directory KST_STORE_DIR of the card, apart from the
 * run logs and reports in its root.  A file is "<base name>.TXT".  The base
 * name is a person's choice; kst_store_preset() proposes "KST001" to
 * "KST999".  This layer adds the extension and holds every name to what the
 * card's file system and a desktop computer both take.
 *
 * The panel's FAT (File Allocation Table) file system is built with long
 * file names, 255 characters (CONFIG_FATFS_MAX_LFN).  Its directory walk
 * passes names of 63 characters or fewer, which leaves 59 for a base name.
 * KST_STORE_BASE_MAX is 32: the length a list row and an entry field show.
 * FAT keeps the case of a name and compares without it, so "Wing.TXT" and
 * "WING.TXT" are one file; this layer compares the same way.
 *
 * A save never replaces a file.
 *
 * The card is reached through kst_store_io_t, 5 functions the caller
 * supplies, so the layer runs on a host without a card.  It handles bytes
 * and names and knows nothing of the file's content.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The directory of the card that holds the files. */
#define KST_STORE_DIR "KST"

/** The extension of every file, added by this layer. */
#define KST_STORE_EXT ".TXT"

/** Characters of a base name at most. */
#define KST_STORE_BASE_MAX 32

/** Room for a file name: base name, extension and terminator. */
#define KST_STORE_NAME_MAX (KST_STORE_BASE_MAX + 4 + 1)

/** The first and the last number of a proposed name. */
#define KST_STORE_FIRST 1
#define KST_STORE_LAST  999

/** Room for "KST999" and its terminator. */
#define KST_STORE_PRESET_MAX 7

/**
 * The base name this layer keeps free, in any case: "KST.TXT" in the
 * directory is the caller's record of a write under way and no servo file.
 * The name is refused as KST_STORE_NAME_RESERVED, so no save writes it and
 * no load reads it; kst_store_list() leaves the file out and does not count
 * it.  The layer itself never reads, writes or removes the file.
 */
#define KST_STORE_RECORD "KST"

/** Whether a base name is taken.  The numbers are stable. */
typedef enum {
    KST_STORE_NAME_OK = 0,
    KST_STORE_NAME_EMPTY = 1,    /**< no character, or NULL */
    KST_STORE_NAME_TOO_LONG = 2, /**< more than KST_STORE_BASE_MAX characters */
    KST_STORE_NAME_BAD_CHAR = 3, /**< a character outside 0x20 to 0x7E, or one
                                      of  " * / : < > ? \ |  */
    KST_STORE_NAME_RESERVED = 4, /**< CON, PRN, AUX, NUL, COM1 to COM9 or LPT1
                                      to LPT9, in any case, alone or in front
                                      of a full stop: a device name on a
                                      desktop computer.  Or
                                      KST_STORE_RECORD, in any case */
    KST_STORE_NAME_EDGE = 5,     /**< a full stop or a space at the start or
                                      at the end */
} kst_store_name_t;

/** The result of a call.  The numbers are stable. */
typedef enum {
    KST_STORE_OK = 0,
    KST_STORE_ARG = 1,          /**< a NULL argument or a buffer too small */
    KST_STORE_NO_CARD = 2,      /**< no card is mounted */
    KST_STORE_LIST_ERROR = 3,   /**< the directory does not read to its end */
    KST_STORE_NO_NUMBER = 4,    /**< KST001 to KST999 are all taken */
    KST_STORE_BAD_NAME = 5,     /**< the name fails kst_store_name_check() */
    KST_STORE_EXISTS = 6,       /**< the name is taken; nothing is written */
    KST_STORE_CARD_FULL = 7,
    KST_STORE_WRITE_ERROR = 8,
    KST_STORE_NOT_FOUND = 9,    /**< no file has that name */
    KST_STORE_TOO_LARGE = 10,   /**< the file is larger than the buffer */
    KST_STORE_READ_ERROR = 11,
    KST_STORE_DENIED = 12,      /**< the card takes no file: it is write
                                     protected, or a file has the name of
                                     the directory */
} kst_store_t;

/** What the create function reports. */
typedef enum {
    KST_STORE_IO_OK = 0,
    KST_STORE_IO_EXISTS,   /**< the name is taken; nothing was written */
    KST_STORE_IO_FULL,     /**< no space; the function removed what it created */
    KST_STORE_IO_ERROR,    /**< any other failure; likewise removed */
    KST_STORE_IO_DENIED,   /**< the card takes no file: it is write
                                protected, or the directory cannot be made
                                because a file has its name.  Nothing was
                                written */
} kst_store_io_result_t;

/** Receives one directory entry.  @p name does not outlive the call. */
typedef void (*kst_store_visit_fn)(const char *name, bool is_dir,
                                   void *visit_ctx);

/** The card.  Every name is a name inside KST_STORE_DIR.  All 5 functions
 *  are set. */
typedef struct {
    void *ctx;
    /** Whether a card is mounted. */
    bool (*mounted)(void *ctx);
    /** Call @p visit for every entry of the directory, files and
     *  directories, once each.  A directory that does not exist has no
     *  entry.  False when the directory does not read to its end. */
    bool (*walk)(void *ctx, kst_store_visit_fn visit, void *visit_ctx);
    /** Size of a file in bytes; -1 when no file has that name. */
    int32_t (*size)(void *ctx, const char *name);
    /** Up to @p cap bytes from the start of the file.  Returns the count,
     *  -1 on a failure. */
    int32_t (*read)(void *ctx, const char *name, char *buf, size_t cap);
    /** Create the directory when it is missing, create the file and fail
     *  when the name exists, write @p len bytes, commit them to the card
     *  and close. */
    kst_store_io_result_t (*create)(void *ctx, const char *name,
                                    const char *data, size_t len);
} kst_store_io_t;

/** One row of a list. */
typedef struct {
    char name[KST_STORE_NAME_MAX];  /**< the file name with its extension */
} kst_store_entry_t;

/** Whether @p base is a base name this layer writes. */
kst_store_name_t kst_store_name_check(const char *base);

/**
 * The file name of @p base: the base name and KST_STORE_EXT.  False, with
 * an empty string in @p out, for a base name that fails
 * kst_store_name_check() and for an @p out_size under KST_STORE_NAME_MAX.
 */
bool kst_store_file_name(const char *base, char *out, size_t out_size);

/**
 * Propose a base name: "KST" and 3 digits, one above the highest number
 * the directory holds.  A gap below the highest number is not filled, so a
 * higher number is a later file.  With KST999 taken the lowest free number
 * is proposed.  Files and directories count, in any case; a file with
 * another name counts for nothing.
 *
 * @p base_size is KST_STORE_PRESET_MAX or more.
 */
kst_store_t kst_store_preset(const kst_store_io_t *io, char *base,
                             size_t base_size);

/**
 * Write @p len bytes of @p text as the file of @p base.
 *
 * KST_STORE_BAD_NAME leaves the reason in @p why, when not NULL.  A name
 * the directory holds, compared without case, gives KST_STORE_EXISTS and
 * the file stays as it is.
 */
kst_store_t kst_store_save(const kst_store_io_t *io, const char *base,
                           const char *text, size_t len,
                           kst_store_name_t *why);

/**
 * List the files with KST_STORE_EXT.
 *
 * The order: by name without case.  The files named "KST" and 3 digits
 * stand in one run at the place of "KST000", by falling number.
 * kst_store_preset() gives a later file the higher number until KST999 is
 * taken, so the run starts with the latest of them.
 *
 * @p entries receives the first @p cap of them.  @p count is the number
 * written, @p total the number of files that qualify, @p skipped the number
 * of files with the extension whose base name fails kst_store_name_check():
 * those are not listed and not loaded.  The file named KST_STORE_RECORD is
 * in none of the 3 numbers.  Each of the 3 may be NULL.
 * Directories are not listed.  A directory without a file gives
 * KST_STORE_OK and a count of 0.
 */
kst_store_t kst_store_list(const kst_store_io_t *io,
                           kst_store_entry_t *entries, size_t cap,
                           size_t *count, size_t *total, size_t *skipped);

/**
 * kst_store_list() from the file behind @p after on: @p entries receives
 * the first @p cap files the order has behind the name @p after, a file
 * name with its extension that need not exist.  NULL lists from the first
 * file on.  @p total and @p skipped count the whole directory as
 * kst_store_list() does.
 *
 * A caller reaches every file of a directory in a buffer of @p cap entries
 * by passing the last name of one call to the next; each call walks the
 * directory once.
 */
kst_store_t kst_store_list_after(const kst_store_io_t *io, const char *after,
                                 kst_store_entry_t *entries, size_t cap,
                                 size_t *count, size_t *total,
                                 size_t *skipped);

/**
 * Read the file @p name, a name as kst_store_list() gives it, into @p buf.
 * @p len receives the number of bytes.  No terminator is written.
 */
kst_store_t kst_store_load(const kst_store_io_t *io, const char *name,
                           char *buf, size_t cap, size_t *len);

#ifdef __cplusplus
}
#endif
