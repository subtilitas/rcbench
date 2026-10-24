/**
 * @file
 * @brief Every file and folder on the card, one at a time.
 *
 * The walk goes folder by folder from the root: a folder, then its files,
 * then each of its folders the same way.  It holds no list of what it has
 * seen, so a card of any size is walked in the same memory, and two walks of
 * an unchanged card give the same entries in the same order -- which the
 * ZIP (wa_zip.h) relies on, walking three times.
 *
 * A folder is read twice, its files and then its folders, and each entry
 * once a pass: going down into a folder keeps the parent's handle open, and
 * coming back reads on from it.  At most one handle a level is open, the
 * root's included: WA_WALK_DEPTH + 1.
 *
 * A walk asked to stop (wa_fs_t.stopped) ends at the next entry with
 * WA_WALK_ERROR, every handle closed.
 *
 * What it leaves out, and counts in wa_walk_t.left_out: a name that is not
 * UTF-8 or longer than WA_NAME_UNITS, a path over WA_PATH_MAX bytes, and a
 * folder deeper than WA_WALK_DEPTH.
 *
 * The file system is passed in, so the walk runs on the host against
 * memory.  Pure C, no ESP-IDF (Espressif Internet-of-Things Development
 * Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_WALK_H
#define RCBENCH_WA_WALK_H

#include <stdbool.h>
#include <stdint.h>

#include "wa_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Folders below the root a walk goes into. */
#define WA_WALK_DEPTH 16u

typedef struct {
    char     name[WA_NAME_BYTES];
    bool     dir;
    uint64_t size;    /**< bytes; 0 for a folder              */
    int64_t  mtime;   /**< seconds since 1970, as the card has it */
} wa_dirent_t;

/** The card, by paths relative to its root ("" is the root). */
typedef struct {
    void   *(*opendir)(void *ctx, const char *path);
    /** The next entry: 1, 0 at the end, -1 on an error. */
    int     (*readdir)(void *ctx, void *dir, wa_dirent_t *out);
    void    (*closedir)(void *ctx, void *dir);
    void   *(*open)(void *ctx, const char *path);
    /** Up to @p len bytes: how many, 0 at the end, -1 on an error. */
    int32_t (*read)(void *ctx, void *file, uint8_t *dst, uint32_t len);
    void    (*close)(void *ctx, void *file);
    void    *ctx;
    /** @brief Whether the walk is to end now, asked before every entry;
     *         NULL never. */
    bool    (*stopped)(void *ctx);
} wa_fs_t;

/** @brief What wa_walk_next() hands out. */
typedef enum {
    WA_WALK_DIR = 0,  /**< a folder: @c path, its files follow           */
    WA_WALK_FILE,     /**< a file: @c path, @c size, @c mtime            */
    WA_WALK_END,      /**< the whole card is walked                      */
    WA_WALK_NO_CARD,  /**< the root cannot be opened                     */
    WA_WALK_ERROR,    /**< a folder could not be read part way           */
} wa_walk_ev_t;

typedef struct {
    void    *dir;     /**< the level's open handle, or NULL              */
    uint32_t len;     /**< bytes of the path up to this folder           */
    bool     dirs;    /**< the folders' pass: the files are done         */
} wa_level_t;

/** @brief A walk under way. */
typedef struct {
    const wa_fs_t *fs;        /**< the card                              */
    unsigned       depth;     /**< levels below the root                 */
    wa_level_t     lv[WA_WALK_DEPTH + 1u]; /**< the root and each level  */
    char           path[WA_PATH_MAX + 1u]; /**< the event's path        */
    wa_dirent_t    ent;       /**< the entry read last                   */
    unsigned long  left_out;  /**< entries left out, as above            */
    bool           started;   /**< the root is open                      */
    wa_walk_ev_t   last;      /**< END, NO_CARD or ERROR once over       */
    bool           over;      /**< ended: @c last for every call         */
} wa_walk_t;

/**
 * @brief A walk of @p fs from its root; nothing is opened yet.
 * @param w   the walk
 * @param fs  the card
 */
void wa_walk_begin(wa_walk_t *w, const wa_fs_t *fs);

/**
 * @brief The next event.
 *
 * WA_WALK_DIR and WA_WALK_FILE carry @c path, and a file @c ent.size and
 * @c ent.mtime; the root's WA_WALK_DIR has the path "".
 *
 * @param w  the walk
 * @return the event; after WA_WALK_END, NO_CARD or ERROR every call
 *         returns the same
 */
wa_walk_ev_t wa_walk_next(wa_walk_t *w);

/**
 * @brief Close every handle that is open; for a walk given up part way.
 * @param w  the walk
 */
void wa_walk_end(wa_walk_t *w);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_WALK_H */
