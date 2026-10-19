/*
 * The folders of the panel's SD (Secure Digital) card, and the files in them
 * with a fixed name.  Every path the panel spells on the card is here.
 *
 *   /BENCH/                    what the bench writes or reads for itself
 *     LOGS/                    automatic logs: SERVOnnn, MOTORnnn, SUPPLnnn
 *     ESC/CONF/                ESC (electronic speed controller) profiles
 *     KST/CONF/                KST and Chaservo servo files
 *     HITEC/CONF/              Hitec servo files
 *     RCBENCH.LOG              the fault log, at most 1 MiB
 *     RCBENCH.OLD              the fault log before the last 1 MiB
 *   /USER/
 *     LOGS/                    user-started tests: ATESTnnn, LIMITnnn
 *   /                          run logs of firmware 0.21 and older
 *
 * Every folder name is upper case, 1 to 8 characters of A-Z, 0-9, "_" and
 * "-", without an extension: a FAT (File Allocation Table) 8.3 short name,
 * the same on every computer.  A path is relative to the card's root, its
 * folders separated by "/", with no "/" at either end; "" is the root.
 *
 * The panel reads and writes these places only.  Folders of firmware 0.21
 * and older, /ESC and /KST, and its /RCBENCH.LOG are neither read nor
 * moved: their files are moved on a computer.  CARD_HITEC_CONF is reserved
 * for the Hitec programmer's files; nothing reads it yet.
 *
 * A folder is made the first time something is written there, one level at
 * a time (card_make_dirs()).
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** The card's root. */
#define CARD_ROOT        ""

/** The bench's own folder. */
#define CARD_BENCH       "BENCH"

/** The automatic logs: a log of each ARMED run and each supply run. */
#define CARD_AUTO_LOGS   "BENCH/LOGS"

/** The user's folder. */
#define CARD_USER        "USER"

/** The files of the tests a user starts: AUTOMATIC TEST and LIMIT RUN. */
#define CARD_USER_LOGS   "USER/LOGS"

/** ESC profile overrides, <id>.json. */
#define CARD_ESC_CONF    "BENCH/ESC/CONF"

/** KST and Chaservo servo files, their backups and the write record. */
#define CARD_KST_CONF    "BENCH/KST/CONF"

/** The Hitec programmer's files. */
#define CARD_HITEC_CONF  "BENCH/HITEC/CONF"

/** The fault log: a line every 5 s while the link to the IOmcu is down. */
#define CARD_FAULT_LOG   "BENCH/RCBENCH.LOG"

/** The fault log as it was when it reached FAULT_LOG_CAP (fault_log.h). */
#define CARD_FAULT_OLD   "BENCH/RCBENCH.OLD"

/** RCBENCH.OLD moved aside while the full fault log takes its name. */
#define CARD_FAULT_TMP   "BENCH/RCBENCH.TMP"

/**
 * Room for a folder path and its terminator: two folders of 8 characters
 * and the "/" between them are 17, and the room is the settings store's
 * string, SETTINGS_TEXT_MAX (24).
 */
#define CARD_DIR_MAX     24

/** What making a folder came to. */
typedef enum {
    CARD_DIR_OK = 0,   /**< a folder now: made, or there before         */
    CARD_DIR_ARG,      /**< a NULL, or a path longer than CARD_DIR_MAX  */
    CARD_DIR_FILE,     /**< a file holds the name of a level            */
    CARD_DIR_DENIED,   /**< the card takes no folder: write-protected   */
    CARD_DIR_FAILED,   /**< any other failure: a full card, a read error */
} card_dir_t;

/** Make the one folder @p level, a path from the root, whose parent is
 *  there.  A folder that is there already is CARD_DIR_OK. */
typedef card_dir_t (*card_make_fn)(void *ctx, const char *level);

/**
 * Make folder @p dir one level at a time, the shortest first: for
 * "BENCH/KST/CONF", "BENCH", then "BENCH/KST", then "BENCH/KST/CONF".  The
 * first level that is not a folder afterwards ends it, and its answer is
 * the result.  CARD_ROOT makes nothing and is CARD_DIR_OK.
 */
card_dir_t card_make_dirs(const char *dir, card_make_fn make, void *ctx);

#ifdef __cplusplus
}
#endif
