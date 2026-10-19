/*
 * Taking a run's number in a folder of the card, and which open file keeps
 * a listed name out of reach.
 *
 * The logger writes each run under a number from its folder's series
 * (log_name.h): one above the highest number the folder holds, under any
 * prefix, read by one directory walk the first time the folder is written
 * and remembered after that.  Before a number is taken its 10 names
 * (LOG_NAME_FORMS) are probed, so a file written since the walk is stepped
 * over and not overwritten.
 *
 * The folder is made on the way, one level at a time: "BENCH", then
 * "BENCH/LOGS".  A level that is there already is not an error; a level
 * whose name a file holds is, and so is a card that takes no new folder.
 *
 * A failed take forgets the folder's number, so the next take walks the
 * folder again: the operator's answer to a full folder is to delete a file,
 * and that deletion counts from the next run on.
 *
 * The card is reached through log_take_io_t, functions the caller
 * supplies, so the rules run on a host without a card.  Pure C, no ESP-IDF
 * (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "card_tree.h"
#include "log_name.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How many free numbers a failing create is given before the folder is
 * called full.  The walk has put the number above every one the folder
 * holds, so a refused create is the volume answering and not a name.
 */
#define LOG_TAKE_TRIES 3u

/** Receives one directory entry's name; it does not outlive the call. */
typedef void (*log_take_visit_fn)(const char *name, void *visit_ctx);

/** The card.  @p dir is a path from the root, CARD_ROOT the root itself.
 *  All 5 functions are set. */
typedef struct {
    void *ctx;
    /** Whether a card is mounted. */
    bool (*mounted)(void *ctx);
    /** Make @p dir a folder.  True when it is one now: made, or there
     *  before.  False when a file holds the name, or the card takes no new
     *  folder (write-protected or full). */
    bool (*make_dir)(void *ctx, const char *dir);
    /** Hand the name of every entry of @p dir, files and folders, to
     *  @p visit.  False when the folder does not read to its end. */
    bool (*walk)(void *ctx, const char *dir, log_take_visit_fn visit,
                 void *visit_ctx);
    /** Whether @p dir holds an entry named @p name, in any case. */
    bool (*exists)(void *ctx, const char *dir, const char *name);
    /** Create @p name in @p dir as the file of run @p number and keep it
     *  open for the caller.  True when it is open. */
    bool (*create)(void *ctx, const char *dir, const char *name, int number);
} log_take_io_t;

/** One folder's numbering. */
typedef struct {
    char dir[CARD_DIR_MAX];  /**< "" with the slot unused, or the folder */
    int  next;               /**< the first number to try                */
    bool numbered;           /**< next is above what the folder holds    */
} log_series_t;

/** The folders a run can be written to: the auto logs' and the user's. */
#define LOG_SERIES_SLOTS 2

typedef struct {
    log_series_t slot[LOG_SERIES_SLOTS];
    unsigned     evict;      /**< the slot a third folder takes next */
} log_series_set_t;

/** Every folder's numbering forgotten: the next take walks its folder. */
void log_series_init(log_series_set_t *set);

/**
 * The numbering of folder @p dir, case folded: its slot, or a free one, or
 * the slot @p set gives up next, cleared.  Two kinds of run in one folder
 * share one series.  NULL for a NULL argument or a @p dir that does not
 * fit CARD_DIR_MAX.
 */
log_series_t *log_series_of(log_series_set_t *set, const char *dir);

/** What a take came to. */
typedef enum {
    LOG_TAKE_OK = 0,
    LOG_TAKE_ARG,          /**< a NULL, a prefix not written, a long dir */
    LOG_TAKE_NO_CARD,      /**< no card is mounted                       */
    LOG_TAKE_NO_FOLDER,    /**< a level of the folder cannot be made     */
    LOG_TAKE_UNREADABLE,   /**< the folder does not read to its end      */
    LOG_TAKE_FULL,         /**< no free number, or LOG_TAKE_TRIES creates
                                refused                                  */
} log_take_t;

/**
 * Make folder @p dir, take the next free number of its series and create
 * the run's file under @p prefix: the CSV, or with @p report the TXT.
 * @p number takes the number on LOG_TAKE_OK and -1 otherwise.
 *
 * @p prefix is one this firmware writes (below LOG_PREFIX_WRITTEN).
 */
log_take_t log_take(log_series_set_t *set, const log_take_io_t *io,
                    const char *dir, log_prefix_t prefix, bool report,
                    int *number);

/**
 * The files the logger has open, each with the folder it is in: the
 * automatic log's run in @p run_dir, and a test's CSV and report in
 * @p test_dir.  A number of 0 is no file open.
 */
typedef struct {
    const char *run_dir;
    unsigned    run;
    const char *test_dir;
    unsigned    test;
    unsigned    txt;
} log_marks_t;

/**
 * Whether @p name in folder @p dir is a file the logger is still writing,
 * or one whose deletion would remove such a file (log_name_busy()), by the
 * marks of that folder alone: an open USER/LOGS/ATEST005.CSV does not hold
 * back BENCH/LOGS/SERVO005.CSV.  Folders compare with case folded.  With
 * both kinds in one folder, both kinds' marks apply.
 */
bool log_marks_busy(const log_marks_t *marks, const char *dir,
                    const char *name);

#ifdef __cplusplus
}
#endif
