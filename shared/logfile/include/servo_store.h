/**
 * @file
 * @brief Servo configuration files on the card for a programmer named by a
 *        stem: names, saving, listing, loading.
 *
 * The files of one programmer live in one folder of the card, which the
 * caller's functions (servo_store_io_t) reach.  A file is
 * "<base name>.TXT".  The base name is a person's choice;
 * servo_store_preset() proposes the stem and 3 digits, "HITEC001" to
 * "HITEC999" for the stem "HITEC".  The stem alone is the base name of the
 * programmer's record of a write under way: no save writes it, no load
 * reads it and no list holds it.  This layer adds the extension and holds
 * every name to what the card's file system and a desktop computer both
 * take.
 *
 * The panel's FAT (File Allocation Table) file system is built with long
 * file names, 255 characters (CONFIG_FATFS_MAX_LFN).  Its directory walk
 * passes names of 63 characters or fewer.  SERVO_STORE_BASE_MAX is 32: the
 * length a list row and an entry field show.  FAT keeps the case of a name
 * and compares without it, so "Wing.TXT" and "WING.TXT" are one file; this
 * layer compares the same way.
 *
 * A save never replaces a file.
 *
 * The layer knows nothing of a file's content.  It is kst_store.h's layer
 * with the stem as a parameter.
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

/** @brief The extension of every file, added by this layer. */
#define SERVO_STORE_EXT ".TXT"

/** @brief Characters of a base name at most. */
#define SERVO_STORE_BASE_MAX 32

/** @brief Room for a file name: base name, extension and terminator. */
#define SERVO_STORE_NAME_MAX (SERVO_STORE_BASE_MAX + 4 + 1)

/** @brief Characters of a stem at most. */
#define SERVO_STORE_STEM_MAX 5

/** @brief The first and the last number of a proposed name. */
#define SERVO_STORE_FIRST 1
/** @brief The last number of a proposed name. */
#define SERVO_STORE_LAST  999

/** @brief Room for a proposed name: the longest stem, 3 digits and the
 *         terminator. */
#define SERVO_STORE_PRESET_MAX (SERVO_STORE_STEM_MAX + 3 + 1)

/** @brief Whether a base name is taken.  The numbers are stable. */
typedef enum {
    SERVO_STORE_NAME_OK = 0,
    SERVO_STORE_NAME_EMPTY = 1,    /**< no character, or NULL */
    SERVO_STORE_NAME_TOO_LONG = 2, /**< more than SERVO_STORE_BASE_MAX
                                        characters */
    SERVO_STORE_NAME_BAD_CHAR = 3, /**< a character outside 0x20 to 0x7E, or
                                        one of  " * / : < > ? \ |  */
    SERVO_STORE_NAME_RESERVED = 4, /**< CON, PRN, AUX, NUL, COM1 to COM9 or
                                        LPT1 to LPT9, in any case, alone or
                                        in front of a full stop: a device
                                        name on a desktop computer.  Or the
                                        stem alone, in any case */
    SERVO_STORE_NAME_EDGE = 5,     /**< a full stop or a space at the start
                                        or at the end */
} servo_store_name_t;

/** @brief The result of a call.  The numbers are stable. */
typedef enum {
    SERVO_STORE_OK = 0,
    SERVO_STORE_ARG = 1,          /**< a NULL argument, a buffer too small or
                                       a stem that is not 1 to
                                       SERVO_STORE_STEM_MAX letters */
    SERVO_STORE_NO_CARD = 2,      /**< no card is mounted */
    SERVO_STORE_LIST_ERROR = 3,   /**< the directory does not read to its
                                       end */
    SERVO_STORE_NO_NUMBER = 4,    /**< numbers 001 to 999 are all taken */
    SERVO_STORE_BAD_NAME = 5,     /**< the name fails the name check */
    SERVO_STORE_EXISTS = 6,       /**< the name is taken; nothing is
                                       written */
    SERVO_STORE_CARD_FULL = 7,
    SERVO_STORE_WRITE_ERROR = 8,
    SERVO_STORE_NOT_FOUND = 9,    /**< no file has that name */
    SERVO_STORE_TOO_LARGE = 10,   /**< the file is larger than the buffer */
    SERVO_STORE_READ_ERROR = 11,
    SERVO_STORE_DENIED = 12,      /**< the card takes no file: it is write
                                       protected, or a file has the name of
                                       the directory */
} servo_store_t;

/** @brief What the create function reports. */
typedef enum {
    SERVO_STORE_IO_OK = 0,
    SERVO_STORE_IO_EXISTS,   /**< the name is taken; nothing was written */
    SERVO_STORE_IO_FULL,     /**< no space; the function removed what it
                                  created */
    SERVO_STORE_IO_ERROR,    /**< any other failure; likewise removed */
    SERVO_STORE_IO_DENIED,   /**< the card takes no file: it is write
                                  protected, or the directory cannot be made
                                  because a file has its name.  Nothing was
                                  written */
} servo_store_io_result_t;

/** @brief Receives one directory entry.  @p name does not outlive the
 *         call. */
typedef void (*servo_store_visit_fn)(const char *name, bool is_dir,
                                     void *visit_ctx);

/**
 * @brief The card, and the programmer's stem.
 *
 * Every name is a name inside the programmer's folder.  All 5 functions are
 * set.
 */
typedef struct {
    void *ctx;                  /**< passed to every function               */
    /** @brief The stem: 1 to SERVO_STORE_STEM_MAX letters A to Z. */
    const char *stem;
    /** @brief Whether a card is mounted. */
    bool (*mounted)(void *ctx);
    /**
     * @brief Call @p visit for every entry of the directory, files and
     *        directories, once each.  A directory that does not exist has
     *        no entry.  False when the directory does not read to its end.
     */
    bool (*walk)(void *ctx, servo_store_visit_fn visit, void *visit_ctx);
    /** @brief Size of a file in bytes; -1 when no file has that name. */
    int32_t (*size)(void *ctx, const char *name);
    /** @brief Up to @p cap bytes from the start of the file: the count, -1
     *         on a failure. */
    int32_t (*read)(void *ctx, const char *name, char *buf, size_t cap);
    /**
     * @brief Create the directory when it is missing, create the file and
     *        fail when the name exists, write @p len bytes, commit them to
     *        the card and close.
     */
    servo_store_io_result_t (*create)(void *ctx, const char *name,
                                      const char *data, size_t len);
} servo_store_io_t;

/** @brief One row of a list. */
typedef struct {
    char name[SERVO_STORE_NAME_MAX];  /**< the file name with its extension */
} servo_store_entry_t;

/**
 * @brief Whether @p base is a base name this layer writes for @p stem.
 *
 * @param stem  The programmer's stem.
 * @param base  The base name, without extension.
 * @return SERVO_STORE_NAME_OK, or the first rule the name breaks.
 */
servo_store_name_t servo_store_name_check(const char *stem, const char *base);

/**
 * @brief The file name of @p base: the base name and SERVO_STORE_EXT.
 *
 * False, with an empty string in @p out, for a base name that fails
 * servo_store_name_check() and for an @p out_size under
 * SERVO_STORE_NAME_MAX.
 *
 * @param stem      The programmer's stem.
 * @param base      The base name.
 * @param out       Receives the file name.
 * @param out_size  Size of @p out, bytes.
 * @return true when @p out holds the file name.
 */
bool servo_store_file_name(const char *stem, const char *base, char *out,
                           size_t out_size);

/**
 * @brief The number of a proposed name: "<stem>nnn.TXT", in any case.
 *
 * @param stem  The programmer's stem.
 * @param file  The file name, with the extension.
 * @return The number, 1 to 999; 0 for any other name.
 */
int servo_store_number(const char *stem, const char *file);

/**
 * @brief Propose a base name: the stem and 3 digits, one above the highest
 *        number the directory holds.
 *
 * A gap below the highest number is not filled, so a higher number is a
 * later file.  With number 999 taken the lowest free number is proposed.
 * Files and directories count, in any case; a file with another name counts
 * for nothing.
 *
 * @param io         The card functions.
 * @param base       Receives the proposed base name.
 * @param base_size  Size of @p base, SERVO_STORE_PRESET_MAX or more.
 * @return SERVO_STORE_OK, or the reason no name was proposed.
 */
servo_store_t servo_store_preset(const servo_store_io_t *io, char *base,
                                 size_t base_size);

/**
 * @brief Write @p len bytes of @p text as the file of @p base.
 *
 * SERVO_STORE_BAD_NAME leaves the reason in @p why, when not NULL.  A name
 * the directory holds, compared without case, gives SERVO_STORE_EXISTS and
 * the file stays as it is.
 *
 * @param io    The card functions.
 * @param base  The base name of the file.
 * @param text  The bytes to write.
 * @param len   Number of bytes in @p text.
 * @param why   Receives the name reason when refused, or NULL.
 * @return SERVO_STORE_OK, or the reason the file was not written.
 */
servo_store_t servo_store_save(const servo_store_io_t *io, const char *base,
                               const char *text, size_t len,
                               servo_store_name_t *why);

/**
 * @brief List the files with SERVO_STORE_EXT from the file behind @p after
 *        on.
 *
 * The order: by name without case.  The proposed names, the stem and 3
 * digits, stand in one run at the place of the stem and "000", by falling
 * number: the run starts with the latest of them.
 *
 * @p entries receives the first @p cap files the order has behind the name
 * @p after, a file name with its extension that need not exist; NULL lists
 * from the first file on.  @p count is the number written, @p total the
 * number of files that qualify in the whole directory, @p skipped the
 * number of files with the extension whose base name fails the name check:
 * those are not listed and not loaded.  The record, the stem alone, is in
 * none of the 3 numbers.  Each of the 3 may be NULL.  Directories are not
 * listed.  A directory without a file gives SERVO_STORE_OK and a count of
 * 0.
 *
 * @param io       The card functions.
 * @param after    The file name to list after, or NULL.
 * @param entries  Receives the listed names.
 * @param cap      Capacity of @p entries, in entries.
 * @param count    Receives the number of entries written, or NULL.
 * @param total    Receives the number of qualifying files, or NULL.
 * @param skipped  Receives the number of files with a bad name, or NULL.
 * @return SERVO_STORE_OK, or the reason the directory did not list.
 */
servo_store_t servo_store_list_after(const servo_store_io_t *io,
                                     const char *after,
                                     servo_store_entry_t *entries, size_t cap,
                                     size_t *count, size_t *total,
                                     size_t *skipped);

/**
 * @brief Read the file @p name, a name as servo_store_list_after() gives it,
 *        into @p buf.  @p len receives the number of bytes.  No terminator
 *        is written.
 *
 * @param io    The card functions.
 * @param name  The file name.
 * @param buf   Receives the bytes.
 * @param cap   Capacity of @p buf, bytes.
 * @param len   Receives the number of bytes read.
 * @return SERVO_STORE_OK, or the reason the file was not read.
 */
servo_store_t servo_store_load(const servo_store_io_t *io, const char *name,
                               char *buf, size_t cap, size_t *len);

#ifdef __cplusplus
}
#endif
