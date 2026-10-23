/**
 * @file fm_rules.h
 * @brief What the file manager may do to a name: the places the bench
 *        keeps, the files the logger writes, and the write gate of the
 *        bench's state.
 *
 * Reads are allowed in every bench state: browsing, entering a folder,
 * viewing a file, INFO.  Writes (COPY, MOVE, RENAME, DELETE, NEW, SAVE) are
 * allowed only while the bench is SAFE and no log file is open
 * (fm_gate()).
 *
 * The places (fm_place_of()):
 *
 *   FIXED   the bench's folders of card_tree.h (BENCH, BENCH/LOGS,
 *           BENCH/ESC, BENCH/ESC/CONF, BENCH/KST, BENCH/KST/CONF,
 *           BENCH/HITEC, BENCH/HITEC/CONF, USER, USER/LOGS), the two log
 *           folders SETUP > SD CARD names and every folder above them: no
 *           RENAME, MOVE or DELETE.  What they hold is the person's.
 *   LOCKED  the servo programmer's journal, BENCH/KST/CONF/KST.TXT and
 *           KST.NEW: INFO only.  A hand edit can send a wrong image to a
 *           servo.
 *   FAULT   the fault log, BENCH/RCBENCH.LOG, .OLD and .TMP: OPEN and
 *           INFO; COPY and DELETE while the link to the IOmcu is up, since
 *           the fault log is written while it is down; no EDIT, RENAME or
 *           MOVE, since its writer renames the three among themselves.
 *   FREE    everything else.
 *
 * A file the logger has open (log_marks_busy()) takes INFO only, whatever
 * its place.  A name that is LOCKED or FAULT is not made by COPY, MOVE,
 * RENAME or NEW either (fm_target_check()).
 *
 * Folders and names compare with case folded, as FAT (File Allocation
 * Table) compares long names: a to z with A to Z, and ä ö ü with Ä Ö Ü;
 * ß has no upper case there and matches only itself.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "fm_io.h"
#include "fm_name.h"
#include "log_take.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief What a person asks of a name.
 */
typedef enum {
    FM_OP_OPEN = 0,  /**< enter a folder, view a file      (read)  */
    FM_OP_INFO,      /**< the INFO box                      (read)  */
    FM_OP_EDIT,      /**< switch the viewer to the editor   (read)  */
    FM_OP_COPY,      /**< copy to the other pane's folder   (write) */
    FM_OP_MOVE,      /**< move to the other pane's folder   (write) */
    FM_OP_RENAME,    /**< give it another name              (write) */
    FM_OP_DELETE,    /**< delete it                         (write) */
    FM_OP_NEW,       /**< make a folder or a file in it     (write) */
    FM_OP_SAVE,      /**< write the editor's text to it     (write) */
    FM_OP_COUNT
} fm_op_t;

/**
 * @brief Where a name stands (the file's comment).
 */
typedef enum {
    FM_PLACE_FREE = 0,
    FM_PLACE_FIXED,
    FM_PLACE_LOCKED,
    FM_PLACE_FAULT
} fm_place_t;

/**
 * @brief What an entry is, for its icon and kind word.
 */
typedef enum {
    FM_KIND_FOLDER = 0,  /**< a folder                                   */
    FM_KIND_RUN,         /**< a run's log: ATEST042.CSV, SERVO007.CSV    */
    FM_KIND_REPORT,      /**< a test's report: ATEST042.TXT, LIMIT003.TXT */
    FM_KIND_KST,         /**< a ".TXT" in BENCH/KST/CONF: a servo file    */
    FM_KIND_PROFILE,     /**< a ".json" in BENCH/ESC/CONF: an ESC profile */
    FM_KIND_FAULT,       /**< the fault log and its two other names      */
    FM_KIND_CSV,         /**< any other ".csv"                           */
    FM_KIND_JSON,        /**< any other ".json"                          */
    FM_KIND_TEXT,        /**< any other ".txt"                           */
    FM_KIND_OTHER,       /**< anything else                              */
    FM_KIND_COUNT
} fm_kind_t;

/**
 * @brief The bench as the rules see it, filled by the caller each time it
 *        asks.  Every pointer may be NULL.
 */
typedef struct {
    bool armed;                /**< the bench is ARMED                    */
    const log_marks_t *marks;  /**< the files the logger has open         */
    bool link_up;              /**< the link to the IOmcu is up           */
    const char *user_logs;     /**< USER LOG FOLDER in force; NULL is
                                    CARD_USER_LOGS                        */
    const char *auto_logs;     /**< AUTO LOG FOLDER in force; NULL is
                                    CARD_AUTO_LOGS                        */
} fm_bench_t;

/**
 * @brief Whether @p op writes to the card.
 * @param op  the operation
 * @return true for COPY, MOVE, RENAME, DELETE, NEW and SAVE
 */
bool fm_op_writes(fm_op_t op);

/**
 * @brief Whether a log file is open: any of the logger's three marks.
 * @param marks  the open files; NULL is none
 * @return true while a run, a test's CSV or a report is open
 */
bool fm_logging(const log_marks_t *marks);

/**
 * @brief The write gate: whether the bench's state lets @p op run.
 *
 * Reads pass in every state.  A write is refused while the bench is ARMED,
 * and while a log file is open; ARMED is the answer when both hold.
 *
 * @param op     the operation
 * @param bench  the bench; NULL is SAFE with nothing open
 * @return FM_OK, FM_ERR_ARMED or FM_ERR_LOGGING
 */
fm_err_t fm_gate(fm_op_t op, const fm_bench_t *bench);

/**
 * @brief Where @p name in folder @p dir stands.
 * @param bench   the log folders in force; may be NULL
 * @param dir     the folder, a path from the root
 * @param name    the entry's name
 * @param is_dir  the entry is a folder
 * @return its place
 */
fm_place_t fm_place_of(const fm_bench_t *bench, const char *dir,
                       const char *name, bool is_dir);

/**
 * @brief Whether the logger writes @p name in folder @p dir now
 *        (log_marks_busy()).
 * @param bench   the open files; NULL is none
 * @param dir     the folder
 * @param name    the file's name
 * @return true for a busy file
 */
bool fm_busy(const fm_bench_t *bench, const char *dir, const char *name);

/**
 * @brief Whether @p op may run on @p name in folder @p dir: the gate, the
 *        file's place and whether the logger writes it, in that order.
 * @param bench   the bench; may be NULL
 * @param op      the operation
 * @param dir     the folder, a path from the root
 * @param name    the entry's name
 * @param is_dir  the entry is a folder
 * @return FM_OK; FM_ERR_ARMED or FM_ERR_LOGGING for a write the gate
 *         stops; FM_ERR_BUSY for a file the logger writes, and for the
 *         fault log's COPY and DELETE while the link is down;
 *         FM_ERR_FIXED, FM_ERR_LOCKED for the places; FM_ERR_ARG for COPY,
 *         EDIT or SAVE of a folder and NEW in a file
 */
fm_err_t fm_allowed(const fm_bench_t *bench, fm_op_t op, const char *dir,
                    const char *name, bool is_dir);

/**
 * @brief Whether a COPY, a SAVE, a RENAME or a NEW may make @p name in
 *        folder @p dir.
 *
 * A name a person types (@p typed: RENAME, NEW) follows fm_name_check().
 * A name a copy or a save carries over from the card is any name of 1 to
 * FM_NAME_MAX bytes without "/" that is not "." or "..": it may hold any
 * character a computer wrote, and only its ".NEW" has to stay listed
 * (fm_name_new()).  Either way neither the name nor its ".NEW" is a LOCKED
 * or FAULT name (a file named KST in BENCH/KST/CONF would write the
 * programmer's KST.NEW), and the paths of both fit fm_path_join().
 *
 * @param bench  the log folders in force; may be NULL
 * @param dir    the target folder
 * @param name   the name to make
 * @param typed  the name is typed by a person
 * @return FM_OK, FM_ERR_LOCKED, FM_ERR_FIXED, FM_ERR_NAME or FM_ERR_ARG
 */
fm_err_t fm_target_check(const fm_bench_t *bench, const char *dir,
                         const char *name, bool typed);

/**
 * @brief The answer of a path call (fm_name.h) as the core's.
 * @param r  what fm_path_join() or fm_path_check() said
 * @return FM_OK; FM_ERR_ARG for FM_PATH_ARG; FM_ERR_NAME for a path that
 *         is bad, too long or too deep
 */
fm_err_t fm_path_err(fm_path_t r);

/**
 * @brief The kind of @p name in folder @p dir.
 * @param dir     the folder, a path from the root
 * @param name    the entry's name
 * @param is_dir  the entry is a folder
 * @return its kind
 */
fm_kind_t fm_kind_of(const char *dir, const char *name, bool is_dir);

/**
 * @brief Whether a file saved as @p name in @p dir is read only at the
 *        next start of the panel: an ESC profile.
 * @param dir   the folder
 * @param name  the file's name
 * @return true for a ".json" in BENCH/ESC/CONF
 */
bool fm_read_at_start(const char *dir, const char *name);

/**
 * @brief Whether two paths name one place, case folded.
 * @param a  a path
 * @param b  a path
 * @return true for one place
 */
bool fm_same_path(const char *a, const char *b);

/**
 * @brief Whether path @p inner is @p outer or a place below it, case folded.
 * @param outer  a folder's path
 * @param inner  a path
 * @return true when @p inner is in @p outer or is @p outer
 */
bool fm_path_within(const char *outer, const char *inner);

#ifdef __cplusplus
}
#endif
