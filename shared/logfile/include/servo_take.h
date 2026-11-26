/**
 * @file
 * @brief Taking a run's number in a labelled servo's folder, the open-file
 *        marks of such a run, and the answer the logger gives the screen.
 *
 * A run filed under servo `<id>` goes to `Servos/<id>/<kind>_NNN.csv` and
 * its report `<kind>_NNN.txt` (servo_name.h).  NNN counts per kind from 000
 * to 999: one above the highest number that kind holds in the folder, as a
 * CSV or as a report.  A gap is not filled.  The folder is walked once and
 * its numbers kept for that one folder; another id, a mount or a FILES
 * edit (log_series_forget_all()) walks it again.  Before a create both
 * names of the number are probed, so a file written since the walk is
 * stepped over.  A kind that holds 999 takes no further run
 * (LOG_TAKE_FULL); the other kinds of that servo keep counting.
 *
 * `Servos` and `Servos/<id>` are made on the way.  A name in the folder
 * that servo_run_parse() refuses moves no number.
 *
 * The card is reached through log_take_io_t (log_take.h).  Pure C, no
 * ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "card_tree.h"
#include "log_take.h"
#include "servo_id.h"
#include "servo_name.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The numbering of one servo's folder. */
typedef struct {
    char     id[SERVO_ID_SIZE];       /**< the folder's id; "" for none   */
    int      next[SERVO_KIND_COUNT];  /**< the first number to try a kind */
    bool     walked;                  /**< next is above what it holds    */
    unsigned edits;                   /**< log_series_edits() at the walk */
} servo_series_t;

/**
 * @brief No folder numbered: the next take walks its folder.
 * @param s The numbering.
 */
void servo_series_init(servo_series_t *s);

/**
 * @brief Make `Servos/<id>`, take the next free number of @p kind there and
 *        create the run's file: the CSV, or with @p report the report.
 *
 * @param s      The numbering.
 * @param io     The card functions; create() gets the folder, the name and
 *               the number.
 * @param id     The servo's id; servo_id_parse() must take it.
 * @param kind   The run's kind.
 * @param report true for the report, false for the CSV.
 * @param number Receives the number, 0 to 999, on LOG_TAKE_OK; -1
 *               otherwise.
 * @return LOG_TAKE_OK, or why no file was made: LOG_TAKE_ARG,
 *         LOG_TAKE_NO_CARD, LOG_TAKE_NO_FOLDER (`Servos` or the id's
 *         folder cannot be made), LOG_TAKE_UNREADABLE, LOG_TAKE_FULL (the
 *         kind holds 999, or LOG_TAKE_TRIES creates were refused).
 */
log_take_t servo_take(servo_series_t *s, const log_take_io_t *io,
                      const char *id, servo_kind_t kind, bool report,
                      int *number);

/** @brief How many runs' servos servo_bind_t keeps. */
#define SERVO_BIND_RUNS 4u

/**
 * @brief The servo each of the last SERVO_BIND_RUNS runs was armed with:
 *        a copy taken at the run's start, so a servo set or cleared after
 *        it, while the run's rows still wait for the logger, files no run
 *        that started before.
 */
typedef struct {
    uint32_t run[SERVO_BIND_RUNS];                  /**< 0 for unused  */
    char     id[SERVO_BIND_RUNS][SERVO_ID_SIZE];    /**< "" for none   */
    unsigned next;                                  /**< slot to write */
} servo_bind_t;

/**
 * @brief Run @p run starts with servo @p id: kept, the oldest run given up.
 * @param b   The record.
 * @param run The run's number, above 0.
 * @param id  The active servo; NULL or "" for none.
 */
void servo_bind_put(servo_bind_t *b, uint32_t run, const char *id);

/**
 * @brief The servo run @p run started with.
 * @param b   The record.
 * @param run The run's number.
 * @return The id; "" for none, for run 0 and for a run no longer kept.
 */
const char *servo_bind_get(const servo_bind_t *b, uint32_t run);

/** @brief The lowest open-file mark of a run in a servo's folder. */
#define SERVO_MARK_BASE 1000u

/**
 * @brief The open-file mark of run @p number of kind @p kind in a servo's
 *        folder: 1000 + 1000 x kind + number, 1,000 to 4,999.  A mark of a
 *        log folder is 1 to 999 and 0 is no file, so run 000 of a servo is
 *        a mark like any other.
 * @param kind   The kind.
 * @param number The number, 0 to 999.
 * @return The mark; 0 for a kind or number out of range.
 */
unsigned servo_run_mark(servo_kind_t kind, int number);

/**
 * @brief The logger's answer to a run's OPEN, for the screen: where the
 *        run's file went.
 */
typedef struct {
    int8_t   state;                    /**< 0 not yet, -1 none, 1 taken    */
    char     dir[CARD_RUN_DIR_MAX];    /**< the folder the logger wrote to */
    char     name[SERVO_NAME_SIZE];    /**< the CSV's name, or the lone
                                            report's                       */
    unsigned mount;                    /**< the mount it was written on    */
} log_test_files_t;

_Static_assert(LOG_RUN_NAME_MAX <= SERVO_NAME_SIZE,
               "a log folder's run name fits the answer's name");

#ifdef __cplusplus
}
#endif
