/**
 * @file fm_fake.h
 * @brief A card in memory for the file manager's core (fm_io.h), with FAT's
 *        rules and failures on request.
 *
 * Names compare with case folded and nothing is renamed over a taken name,
 * as on FAT (File Allocation Table).  A folder lists its entries in the
 * order they were made.  The card can be pulled and another put in (a new
 * mount number); a chosen call can fail with a chosen answer, and a
 * CARD_GONE answer pulls the card with it; the card can fill up, and a
 * folder can take a fixed number of entries.  The walk leaves out names
 * starting with "." and counts them, as the panel's walk does, unless the
 * test asks for them.  What a pulled card holds
 * stays readable to the test, so a ".NEW" left behind can be looked at.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_FM_FAKE_H
#define RCBENCH_FM_FAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fm_io.h"

/** @brief The calls of fm_io_t the fake counts and can fail. */
typedef enum {
    FK_ANY = 0,  /**< every call below                              */
    FK_HOLD,
    FK_WALK,
    FK_STAT,
    FK_MKDIR,
    FK_UNLINK,
    FK_RMDIR,
    FK_RENAME,
    FK_OPEN,
    FK_READ,
    FK_WRITE,
    FK_SYNC,
    FK_CLOSE,
    FK_COUNT
} fk_call_t;

/** @brief One entry of the card. */
typedef struct {
    char *path;          /**< from the root, as made                 */
    bool dir;            /**< a folder                               */
    uint8_t *data;       /**< a file's bytes                         */
    size_t size;         /**< bytes in @p data                       */
    size_t cap;          /**< room in @p data                        */
} fk_node_t;

/** @brief An open file. */
typedef struct {
    bool used;           /**< the handle is open                     */
    int node;            /**< the file's entry                       */
    size_t pos;          /**< the next byte read                     */
    bool write;          /**< opened FM_OPEN_NEW                     */
} fk_file_t;

/** @brief Room for a path in the fake: more than any the core builds, so a
 *         test can make the paths the core refuses. */
#define FK_PATH 512

/** @brief Most files open at once. */
#define FK_FILES 4

/** @brief The card. */
typedef struct {
    fk_node_t *n;               /**< entries, in the order made          */
    int count;                  /**< entries in @p n                     */
    int cap;                    /**< room in @p n                        */
    bool in;                    /**< a card is in and mounted            */
    unsigned mount;             /**< its mount number                    */
    int holds;                  /**< holds not released                  */
    long calls[FK_COUNT];       /**< calls of each kind                  */
    fk_call_t fail_call;        /**< the call that fails                 */
    long fail_nth;              /**< its count that fails, 1 the first; 0
                                     for none                            */
    fm_err_t fail_err;          /**< its answer                          */
    uint64_t free_bytes;        /**< bytes the card still takes          */
    int dir_cap;                /**< entries a folder takes; 0 any       */
    bool hand_long;             /**< the walk hands over names of 64
                                     characters and more                 */
    bool show_dots;             /**< the walk hands over names starting
                                     with "."; false counts them as
                                     unlisted, as the panel's walk       */
    long gone_walk;             /**< the walk of this count finds the card
                                     gone at its first entry and mounts
                                     it again, once; 0 never             */
    int gone_at_entry;          /**< the walk finds the card gone after
                                     this many entries and mounts it
                                     again, once; -1 never               */
    fk_file_t files[FK_FILES];  /**< open files                          */
    int64_t mtime;              /**< every entry's time as stat() answers
                                     it; -1, none, from fk_init()        */
} fk_card_t;

/**
 * @brief Start an empty card, in and mounted as mount 1.
 * @param c  the card
 */
void fk_init(fk_card_t *c);

/**
 * @brief Free what the card holds.
 * @param c  the card
 */
void fk_free(fk_card_t *c);

/**
 * @brief The table of functions over @p c.
 * @param c  the card
 * @return the io
 */
fm_io_t fk_io(fk_card_t *c);

/**
 * @brief Make folder @p path and every folder above it.
 * @param c     the card
 * @param path  the folder's path
 */
void fk_dir(fk_card_t *c, const char *path);

/**
 * @brief Make file @p path, and the folders above it, holding @p size
 *        bytes: @p data, or a pattern of the path when NULL.
 * @param c     the card
 * @param path  the file's path
 * @param data  the bytes, or NULL
 * @param size  how many
 */
void fk_file(fk_card_t *c, const char *path, const uint8_t *data,
             size_t size);

/**
 * @brief The entry at @p path, case folded.
 * @param c     the card
 * @param path  the path
 * @return the entry, or NULL
 */
const fk_node_t *fk_get(const fk_card_t *c, const char *path);

/**
 * @brief The byte at @p i of the pattern fk_file() writes for @p path.
 * @param path  the file's path
 * @param i     the offset
 * @return the byte
 */
uint8_t fk_pattern(const char *path, size_t i);

/**
 * @brief Whether file @p path holds fk_pattern() of @p of for @p size bytes.
 * @param c     the card
 * @param path  the file
 * @param of    the path whose pattern it holds
 * @param size  bytes expected
 * @return true when it does
 */
bool fk_holds_pattern(const fk_card_t *c, const char *path, const char *of,
                      size_t size);

/**
 * @brief Make the @p nth call of kind @p call answer @p err.
 * @param c     the card
 * @param call  the kind of call, FK_ANY for any
 * @param nth   counted from the next call, 1 the next
 * @param err   the answer; FM_ERR_CARD_GONE pulls the card too
 */
void fk_fail(fk_card_t *c, fk_call_t call, long nth, fm_err_t err);

/**
 * @brief Pull the card: every call answers FM_ERR_CARD_GONE.
 * @param c  the card
 */
void fk_pull(fk_card_t *c);

/**
 * @brief Put the card in again: mounted under the next mount number.
 * @param c  the card
 */
void fk_insert(fk_card_t *c);

/**
 * @brief Files open now.
 * @param c  the card
 * @return the count
 */
int fk_open_files(const fk_card_t *c);

#endif
