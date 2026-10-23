/**
 * @file
 * @brief The file manager's names and paths: which name a person may give
 *        a file or a folder, and the paths built from names.
 *
 * A name a person types (RENAME, NEW) is 1 to FM_NAME_MAX bytes of
 * printable ASCII (American Standard Code for Information Interchange,
 * 0x20 to 0x7E) and the German letters Ä Ö Ü ä ö ü ß (2 bytes each in
 * UTF-8), without any of  " * / : < > ? \ |  , with no full stop or space
 * at either end, and with no device name of a desktop computer (CON, PRN,
 * AUX, NUL, COM1 to COM9, LPT1 to LPT9, in any case, alone or in front of a
 * full stop).  These are kst_store_name_check()'s rules for any file, and
 * the text faces draw every character they take.
 *
 * FM_NAME_MAX is 59 bytes: the card's walk lists names of at most
 * FM_LIST_NAME_MAX bytes (STORAGE_NAME_MAX 64 with its terminator), and a
 * save writes the name with ".NEW" behind it first (fm_name_new()), which
 * has to be listed too.  A name a computer wrote can be longer, up to
 * FM_LIST_NAME_MAX, and hold any character: it is listed, opened and
 * joined to a path, but a save refuses it.
 *
 * A path is relative to the card's root: names separated by "/", no "/"
 * at either end, "" for the root, no "." or ".." name.  It holds at most
 * FM_PATH_MAX bytes, so that the card's walk, which builds the drive, a
 * "/" and the path in 128 bytes (STORAGE_NAME_MAX * 2) with a drive of
 * "0:", takes every folder path; and at most FM_DEPTH_MAX folders, the depth the Wi-Fi
 * page's walk goes to (WA_WALK_DEPTH).
 *
 * Pure C: no card access, no heap.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bytes of a name the card's walk lists at most. */
#define FM_LIST_NAME_MAX 63u
/** @brief What a save puts behind a name while it writes. */
#define FM_NEW_SUFFIX ".NEW"
/** @brief Bytes of FM_NEW_SUFFIX. */
#define FM_NEW_SUFFIX_LEN 4u
/** @brief Bytes of a name a person gives at most. */
#define FM_NAME_MAX (FM_LIST_NAME_MAX - FM_NEW_SUFFIX_LEN)
/** @brief Bytes of a path from the root at most. */
#define FM_PATH_MAX 120u
/** @brief Room for a path and its terminator. */
#define FM_PATH_SIZE (FM_PATH_MAX + 1u)
/** @brief Folders below the root a path holds at most. */
#define FM_DEPTH_MAX 16u

/** @brief What fm_name_check() says of a name.  The numbers are stable. */
typedef enum {
    FM_NAME_OK = 0,        /**< taken */
    FM_NAME_EMPTY = 1,     /**< no character, or NULL */
    FM_NAME_LONG = 2,      /**< more than FM_NAME_MAX bytes */
    FM_NAME_CHAR = 3,      /**< a byte outside 0x20 to 0x7E and the German
                                letters, or one of  " * / : < > ? \ |  */
    FM_NAME_EDGE = 4,      /**< a full stop or a space at the start or at
                                the end */
    FM_NAME_RESERVED = 5,  /**< a device name, alone or in front of a full
                                stop, in any case */
} fm_name_t;

/** @brief What a path call says.  The numbers are stable. */
typedef enum {
    FM_PATH_OK = 0,    /**< done */
    FM_PATH_ARG = 1,   /**< a NULL, or an @p out shorter than the path */
    FM_PATH_BAD = 2,   /**< an empty name, a "/" at an end or twice, a name
                            "." or "..", or a "/" in a name to join */
    FM_PATH_LONG = 3,  /**< over FM_PATH_MAX bytes */
    FM_PATH_DEEP = 4,  /**< more than FM_DEPTH_MAX folders */
} fm_path_t;

/**
 * @brief Whether a person may give a file or a folder the name @p name.
 *
 * @param name The name, UTF-8.
 * @return FM_NAME_OK, or the first rule it breaks, in the order of
 *         fm_name_t.
 */
fm_name_t fm_name_check(const char *name);

/**
 * @brief The name a save writes first: @p name and FM_NEW_SUFFIX.
 *
 * @param name The file's name.
 * @param out  Out: the name; left as it was when refused.
 * @param n    Room in @p out.
 * @return false for a NULL, an empty name, a name over FM_NAME_MAX bytes
 *         or an @p out without room.
 */
bool fm_name_new(const char *name, char *out, size_t n);

/**
 * @brief Whether @p path is a folder path: "" or names separated by one
 *        "/", at most FM_PATH_MAX bytes and FM_DEPTH_MAX folders.
 *
 * @param path The path.
 * @return FM_PATH_OK, or the first fault: ARG, LONG, BAD, DEEP.
 */
fm_path_t fm_path_check(const char *path);

/**
 * @brief Folders in @p path.
 *
 * @param path A path fm_path_check() takes.
 * @return 0 for the root and NULL.
 */
size_t fm_path_depth(const char *path);

/**
 * @brief The path of @p name in folder @p dir: "name" in the root,
 *        "dir/name" elsewhere.
 *
 * @p out may be @p dir itself, so a pane goes down into a folder in place.
 * The name may be any name the card lists; fm_name_check() is the rule for
 * names a person types.
 *
 * @param dir    A folder path.
 * @param name   A name without "/".
 * @param is_dir Whether @p name is a folder: the path then counts it in
 *               its depth.
 * @param out    Out: the path; left as it was when refused.
 * @param n      Room in @p out.
 * @return FM_PATH_OK, or the first fault: ARG, then @p dir's, then BAD for
 *         the name, LONG, DEEP, and ARG for an @p out without room.
 */
fm_path_t fm_path_join(const char *dir, const char *name, bool is_dir,
                       char *out, size_t n);

/**
 * @brief Take the last name off @p path, in place: up one folder.
 *
 * @param path A path fm_path_check() takes.
 * @return false at the root and for NULL; @p path is unchanged then.
 */
bool fm_path_up(char *path);

/**
 * @brief The last name of @p path.
 *
 * @param path A path.
 * @return Inside @p path; "" for the root and NULL.
 */
const char *fm_path_base(const char *path);

#ifdef __cplusplus
}
#endif
