/**
 * @file
 * @brief The folders of the panel's SD (Secure Digital) card, and the files in
 *        them with a fixed name.
 *
 * Every path the panel spells on the card is here.
 *
 *   /BENCH/                    what the bench writes or reads for itself
 *     LOGS/                    automatic logs: SERVOnnn, MOTORnnn, SUPPLnnn
 *                              (AUTO LOG FOLDER, the default)
 *     ESC/CONF/                ESC (electronic speed controller) profiles
 *     KST/CONF/                KST and Chaservo servo files
 *     HITEC/CONF/              Hitec servo files and the record of a
 *                              write, HITEC.TXT
 *     ESCW/CONF/               ESC settings files read over the signal wire
 *                              and the record of a write, ESCW.TXT
 *     RCBENCH.LOG              the fault log, at most 1MiB
 *     RCBENCH.OLD              the fault log before the last 1MiB
 *   /USER/
 *     LOGS/                    user-started tests: ATESTnnn, LIMITnnn
 *                              (USER LOG FOLDER, the default)
 *   /                          run logs of firmware 0.21.0 and older
 *
 * Every folder name is upper case, 1 to 8 characters of A-Z, 0-9, "_" and
 * "-", without an extension: a FAT (File Allocation Table) 8.3 short name,
 * the same on every computer.  A path is relative to the card's root, its
 * folders separated by "/", with no "/" at either end; "" is the root.
 *
 * The two log folders are settings (SETUP > SD CARD): any folder
 * card_folder_check() takes, the defaults CARD_AUTO_LOGS and
 * CARD_USER_LOGS.  The other places are fixed.
 *
 * The panel reads and writes these places only, and one more of the
 * operator's own: "screenshots" in the root, a name of 11 lower-case letters
 * that no file of the bench is in (shot_file.h).  Folders of firmware 0.21
 * and older, /ESC and /KST, and its /RCBENCH.LOG are neither read nor
 * moved: their files are moved on a computer.
 *
 * A folder is made the first time something is written there, one level at
 * a time (card_make_dirs()).
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The card's root.
 */
#define CARD_ROOT        ""

/**
 * @brief The bench's own folder.
 */
#define CARD_BENCH       "BENCH"

/**
 * @brief The automatic logs: a log of each ARMED run and each supply run; the
 *        default of AUTO LOG FOLDER.
 */
#define CARD_AUTO_LOGS   "BENCH/LOGS"

/**
 * @brief The user's folder.
 */
#define CARD_USER        "USER"

/**
 * @brief The files of the tests a user starts: AUTOMATIC TEST and LIMIT RUN;
 *        the default of USER LOG FOLDER.
 */
#define CARD_USER_LOGS   "USER/LOGS"

/**
 * @brief ESC profile overrides, \<id\>.json.
 */
#define CARD_ESC_CONF    "BENCH/ESC/CONF"

/**
 * @brief KST and Chaservo servo files, their backups and the write record.
 */
#define CARD_KST_CONF    "BENCH/KST/CONF"

/**
 * @brief Hitec servo files, their backups and the write record.
 */
#define CARD_HITEC_CONF  "BENCH/HITEC/CONF"

/**
 * @brief ESC settings files read over the signal wire, their backups and the
 *        write record (EW_FILE_STEM).
 */
#define CARD_ESCW_CONF   "BENCH/ESCW/CONF"

/**
 * @brief The fault log: a line every 5s while the link to the IOmcu is down.
 */
#define CARD_FAULT_LOG   "BENCH/RCBENCH.LOG"

/**
 * @brief The fault log as it was when it reached FAULT_LOG_CAP (fault_log.h).
 */
#define CARD_FAULT_OLD   "BENCH/RCBENCH.OLD"

/**
 * @brief RCBENCH.OLD moved aside while the full fault log takes its name.
 */
#define CARD_FAULT_TMP   "BENCH/RCBENCH.TMP"

/**
 * @brief Room for a folder path and its terminator: two folders of 8 characters
 *        and the "/" between them are 17, and the room is the settings store's
 *        string, SETTINGS_TEXT_MAX (24).
 */
#define CARD_DIR_MAX     24

/**
 * @brief What making a folder came to.
 */
typedef enum {
    CARD_DIR_OK = 0,   /**< a folder now: made, or there before         */
    CARD_DIR_ARG,      /**< a NULL, or a path longer than CARD_DIR_MAX  */
    CARD_DIR_FILE,     /**< a file holds the name of a level            */
    CARD_DIR_DENIED,   /**< the card takes no folder: write-protected   */
    CARD_DIR_FAILED,   /**< any other failure: a full card, a read error */
} card_dir_t;

/**
 * @brief Make the one folder @p level, a path from the root, whose parent is
 *        there.
 *
 * A folder that is there already is CARD_DIR_OK.
 *
 * @param ctx    Context the caller passed to card_make_dirs().
 * @param level  The folder path to make, from the card's root.
 * @return The result of making the folder.
 */
typedef card_dir_t (*card_make_fn)(void *ctx, const char *level);

/**
 * @brief Make folder @p dir one level at a time, the shortest first: for
 *        "BENCH/KST/CONF", "BENCH", then "BENCH/KST", then "BENCH/KST/CONF".
 *
 * The first level that is not a folder afterwards ends it, and its answer is
 * the result.  CARD_ROOT makes nothing and is CARD_DIR_OK.
 *
 * @param dir   The folder path, from the card's root.
 * @param make  Makes one level.
 * @param ctx   Context passed to @p make.
 * @return The result of the last level made, or CARD_DIR_OK for the root.
 */
card_dir_t card_make_dirs(const char *dir, card_make_fn make, void *ctx);

/**
 * @brief The longest folder name a log folder takes, and how deep it goes.
 */
#define CARD_FOLDER_NAME_MAX  8
/** @brief The most folders a log folder path holds. */
#define CARD_FOLDER_DEPTH_MAX 2

/**
 * @brief What card_folder_check() says of a log folder.
 */
typedef enum {
    CARD_FOLDER_OK = 0,
    CARD_FOLDER_ARG,       /**< a NULL, or an @p out shorter than the path */
    CARD_FOLDER_EMPTY,     /**< "", or a leading, trailing or double "/"   */
    CARD_FOLDER_LONG,      /**< a folder over CARD_FOLDER_NAME_MAX         */
    CARD_FOLDER_DEEP,      /**< more than CARD_FOLDER_DEPTH_MAX folders    */
    CARD_FOLDER_CHAR,      /**< a character outside A-Z, 0-9, "_" and "-"  */
    CARD_FOLDER_RESERVED,  /**< a device name: CON, PRN, AUX, NUL, COM1 to
                                COM9, LPT1 to LPT9                         */
    CARD_FOLDER_BENCH,     /**< the bench's own: BENCH/ESC, BENCH/KST,
                                BENCH/HITEC, BENCH/ESCW and below                     */
} card_folder_t;

/**
 * @brief Whether @p in names a folder the logs can be written to: 1 or 2
 *        folders separated by "/", each 1 to 8 characters of A-Z, 0-9, "_" and
 *        "-", lower case taken as upper case.
 *
 * A device name is refused in either place, and so is a folder the bench keeps
 * its own files in.  Both log folders may be one folder.  The first fault from
 * the left is the answer.
 *
 * On CARD_FOLDER_OK, @p out (when not NULL) takes the path in upper case;
 * at most 17 characters and the terminator.  Otherwise @p out is left as
 * it was.
 *
 * @param in  The folder path to check.
 * @param out Receives the path in upper case when accepted, or NULL.
 * @param n   Size of @p out, bytes.
 * @return CARD_FOLDER_OK, or the first fault found.
 */
card_folder_t card_folder_check(const char *in, char *out, size_t n);

/**
 * @brief The folder a log is written to: @p text in upper case when
 *        card_folder_check() takes it, @p fallback otherwise.
 *
 * Written into @p out, CARD_DIR_MAX bytes or more, and returned.  A stored
 * folder that no longer passes the check is never written to.
 *
 * @param text      The stored folder path.
 * @param fallback  The folder to use when @p text does not pass.
 * @param out       Receives the chosen folder path.
 * @param n         Size of @p out, bytes.
 * @return @p out, holding the chosen folder path.
 */
const char *card_folder_use(const char *text, const char *fallback,
                            char *out, size_t n);

#ifdef __cplusplus
}
#endif
