/**
 * @file fm_io.h
 * @brief The card as the file manager's core sees it: a table of functions
 *        the caller supplies, and the answers every part of the core gives.
 *
 * The core (fm_tree.h, fm_ops.h) holds no file system.  The panel points
 * fm_io_t at its SD (Secure Digital) card through the storage component;
 * the host suite points it at a card in memory that fails on request.
 *
 * Every path is from the card's root: folders separated by "/", no "/" at
 * either end, "" the root itself (card_tree.h).  A name is one level of a
 * path.  Names compare with case folded, as FAT (File Allocation Table)
 * compares them: "report.txt" and "REPORT.TXT" are one file.
 *
 * A volume is named by its mount number (mounts()): another number after a
 * card is taken out and one is put in.  A page of a folder is read from one
 * mount, and a job on its names runs on that mount or not at all, so a
 * card swapped since the listing is never written under a stale name.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fm_name.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Room for a listed name and its terminator: FM_LIST_NAME_MAX (63)
 *        bytes, as the panel's directory walk keeps them
 *        (STORAGE_NAME_MAX).  A longer name is not listed and is counted
 *        as unlisted.  The limits of names and paths are fm_name.h's;
 *        a path buffer is FM_PATH_SIZE.
 */
#define FM_NAME_BUF (FM_LIST_NAME_MAX + 1u)

/**
 * @brief What a call of the core or of the card came to.  FM_OK is 0; every
 *        other value is a refusal or a failure, and the first one found is
 *        the answer.
 */
typedef enum {
    FM_OK = 0,
    FM_ERR_NO_CARD,     /**< nothing is mounted                          */
    FM_ERR_CARD_GONE,   /**< the card read from is gone or swapped       */
    FM_ERR_NOT_FOUND,   /**< no such name, or a folder not made yet      */
    FM_ERR_EXISTS,      /**< the target name is taken: no overwrite      */
    FM_ERR_FULL,        /**< the card or the folder takes no more        */
    FM_ERR_DENIED,      /**< the card refuses the write: write-protected */
    FM_ERR_BUSY,        /**< the logger writes the file                  */
    FM_ERR_FIXED,       /**< a place the bench keeps (fm_rules.h)        */
    FM_ERR_LOCKED,      /**< the servo programmer's journal              */
    FM_ERR_NAME,        /**< a name or path the card or a .NEW cannot take */
    FM_ERR_ARG,         /**< a NULL, or an operation on the wrong kind   */
    FM_ERR_IO,          /**< any other failure of the card               */
    FM_ERR_NOT_EMPTY,   /**< a folder that holds entries                 */
    FM_ERR_SAME,        /**< the target folder is the source's, or in it */
    FM_ERR_TOO_BIG,     /**< larger than the buffer it is loaded into    */
    FM_ERR_ARMED,       /**< the bench is ARMED: the card is read-only   */
    FM_ERR_LOGGING,     /**< a run is being logged: the card is read-only */
    FM_ERR_CANCELLED,   /**< stopped by CANCEL                           */
    FM_ERR_COUNT
} fm_err_t;

/**
 * @brief One directory entry as a walk hands it over, borrowed for the
 *        call.
 */
typedef struct {
    const char *name;   /**< the entry's name, any length               */
    uint64_t size;      /**< bytes, 64-bit as exFAT's; 0 for a folder   */
    bool is_dir;        /**< a folder                                   */
} fm_dirent_t;

/**
 * @brief What stat() reads of one name.
 */
typedef struct {
    uint64_t size;      /**< bytes; 0 for a folder                      */
    bool is_dir;        /**< a folder                                   */
} fm_stat_t;

/**
 * @brief How open() opens a file.
 */
typedef enum {
    FM_OPEN_READ = 0,   /**< an existing file, to read from its start   */
    FM_OPEN_NEW         /**< a new file; a taken name is FM_ERR_EXISTS  */
} fm_mode_t;

/**
 * @brief Called once per entry of a walk.
 * @param entry  the entry, borrowed for the call
 * @param vctx   the walk's context, passed through untouched
 */
typedef void (*fm_visit_fn)(const fm_dirent_t *entry, void *vctx);

/**
 * @brief The card: every function set, each answering FM_OK or the reason
 *        it failed.  @p ctx is passed to each of them.
 *
 * A call that finds the card gone answers FM_ERR_CARD_GONE; one with
 * nothing mounted FM_ERR_NO_CARD.  A file handle from open() is closed by
 * close(), also after a failure; close() of a card gone frees the handle.
 */
typedef struct {
    /**
     * @brief Whether a volume is mounted now.
     * @param ctx  the card's context
     * @return true while a volume is mounted
     */
    bool (*mounted)(void *ctx);
    /**
     * @brief The mount number: another one after a card is put in.
     * @param ctx  the card's context
     * @return the number of the volume mounted now
     */
    unsigned (*mounts)(void *ctx);
    /**
     * @brief Keep the volume of mount @p mount for the calls that follow,
     *        until release().
     * @param ctx    the card's context
     * @param mount  the mount number the caller read its names from
     * @return false when that volume is no longer mounted; then nothing
     *         is held and release() is not called
     */
    bool (*hold)(void *ctx, unsigned mount);
    /**
     * @brief The end of a hold() that answered true.
     * @param ctx  the card's context
     */
    void (*release)(void *ctx);
    /**
     * @brief Hand every entry of folder @p dir to @p visit, in the order the
     *        volume holds them.
     *
     * Every entry is either handed to @p visit or counted in
     * @p unlisted: a name the walk cannot hand over whole, over
     * FM_LIST_NAME_MAX bytes, and a name starting with "." that the walk
     * leaves out, as the panel's walk leaves out the files a Mac writes.
     * A folder whose walk visits nothing and counts nothing is empty.
     *
     * @param ctx       the card's context
     * @param dir       the folder, a path from the root
     * @param visit     called per entry
     * @param vctx      passed to @p visit
     * @param unlisted  takes the count of entries not handed over; may be
     *                  NULL
     * @return FM_OK at the folder's end; FM_ERR_NOT_FOUND for no such
     *         folder; FM_ERR_CARD_GONE when the card was found gone part
     *         way, and what was visited is the old card's
     */
    fm_err_t (*walk)(void *ctx, const char *dir, fm_visit_fn visit,
                     void *vctx, unsigned *unlisted);
    /**
     * @brief The size and kind of one name.
     * @param ctx   the card's context
     * @param path  the name's path from the root
     * @param out   takes the answer
     * @return FM_OK, or FM_ERR_NOT_FOUND for no such name
     */
    fm_err_t (*stat)(void *ctx, const char *path, fm_stat_t *out);
    /**
     * @brief Make folder @p path, whose parent is there.
     * @param ctx   the card's context
     * @param path  the new folder's path
     * @return FM_OK, or FM_ERR_EXISTS for a taken name
     */
    fm_err_t (*mkdir)(void *ctx, const char *path);
    /**
     * @brief Delete file @p path.
     * @param ctx   the card's context
     * @param path  the file's path
     * @return FM_OK, or FM_ERR_NOT_FOUND
     */
    fm_err_t (*unlink)(void *ctx, const char *path);
    /**
     * @brief Delete the empty folder @p path.
     * @param ctx   the card's context
     * @param path  the folder's path
     * @return FM_OK; FM_ERR_NOT_EMPTY or FM_ERR_DENIED for a folder that
     *         holds entries
     */
    fm_err_t (*rmdir)(void *ctx, const char *path);
    /**
     * @brief Give @p from the path @p to, on the one volume: no data moves.
     *
     * FAT renames over no existing name: a taken @p to is FM_ERR_EXISTS,
     * except the same entry with its case changed.
     *
     * @param ctx   the card's context
     * @param from  the entry's path now
     * @param to    its new path
     * @return FM_OK, or why not
     */
    fm_err_t (*rename)(void *ctx, const char *from, const char *to);
    /**
     * @brief Open file @p path.
     * @param ctx   the card's context
     * @param path  the file's path
     * @param mode  read an existing file, or make a new one
     * @param file  takes the handle on FM_OK
     * @return FM_OK, or why not
     */
    fm_err_t (*open)(void *ctx, const char *path, fm_mode_t mode,
                     void **file);
    /**
     * @brief Read up to @p cap bytes from the file's position on.
     * @param ctx   the card's context
     * @param file  an open() handle
     * @param buf   takes the bytes
     * @param cap   room in @p buf
     * @param got   takes the count read, 0 at the file's end
     * @return FM_OK, or why not
     */
    fm_err_t (*read)(void *ctx, void *file, uint8_t *buf, size_t cap,
                     size_t *got);
    /**
     * @brief Write @p len bytes at the file's end, all of them or fail.
     * @param ctx   the card's context
     * @param file  an FM_OPEN_NEW handle
     * @param buf   the bytes
     * @param len   how many
     * @return FM_OK; FM_ERR_FULL for a full card
     */
    fm_err_t (*write)(void *ctx, void *file, const uint8_t *buf, size_t len);
    /**
     * @brief Put what was written on the card: fflush() and fsync().
     * @param ctx   the card's context
     * @param file  an FM_OPEN_NEW handle
     * @return FM_OK, or why not
     */
    fm_err_t (*sync)(void *ctx, void *file);
    /**
     * @brief Close an open() handle.
     * @param ctx   the card's context
     * @param file  the handle; invalid afterwards whatever the answer
     * @return FM_OK, or why the last bytes did not reach the card
     */
    fm_err_t (*close)(void *ctx, void *file);
    void *ctx;  /**< passed to every function */
} fm_io_t;

#ifdef __cplusplus
}
#endif
