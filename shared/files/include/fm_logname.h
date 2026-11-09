/**
 * @file fm_logname.h
 * @brief RENAME LOG on MOTOR & ESC: the run log the bench closed last given
 *        a label behind its number, as a FILES rename.
 *
 * MOTOR042.CSV with the label "kv1200" becomes MOTOR042_kv1200.CSV
 * (log_name_label()); an empty label gives MOTOR042.CSV back.  The run
 * keeps its number, so LOGS ranks it as before and the next run takes one
 * above it (log_name.h).
 *
 * The rename is fm_job_rename() and nothing else: the write gate (SAFE,
 * no log file open), the file's place, the typed-name rules of
 * fm_name_check() on the whole new name, and FAT's refusal of a taken
 * name (FM_ERR_EXISTS).  The job runs on the mount the log was written on:
 * a card taken out since is FM_ERR_NO_CARD, another card in its place
 * FM_ERR_CARD_GONE, and nothing is written to either.  A log renamed or
 * deleted elsewhere since is FM_ERR_NOT_FOUND.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "card_tree.h"
#include "fm_io.h"
#include "fm_ops.h"
#include "fm_rules.h"
#include "log_name.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The run log a bench closed last: where it is and on which mount.
 */
typedef struct {
    char dir[CARD_DIR_MAX];          /**< its folder, a path from the root */
    char name[LOG_LABELLED_SIZE];    /**< its name now; "" for none        */
    unsigned mount;                  /**< the mount it was written on      */
} fm_logname_t;

/**
 * @brief Start the rename of @p last to its name with label @p label.
 *
 * @param job       The job; it holds the refusal as FM_JOB_FAILED.
 * @param bench     The gate and the rules; may be NULL.
 * @param last      The log closed last.
 * @param label     The label; "" or NULL for the plain name.
 * @param new_name  Takes the new name, "" when refused before it is built.
 * @param n         Bytes at @p new_name: LOG_LABELLED_SIZE or more.
 * @return FM_OK when the job runs; FM_ERR_ARG for a NULL, no log
 *         (@p last's name "") or a name that is not a run log, and for a
 *         new name the same as the old; FM_ERR_NAME for a name over
 *         LOG_LABELLED_MAX bytes or one fm_name_check() refuses;
 *         FM_ERR_ARMED, FM_ERR_LOGGING and the other refusals of
 *         fm_job_rename().
 */
fm_err_t fm_logname_start(fm_job_t *job, const fm_bench_t *bench,
                          const fm_logname_t *last, const char *label,
                          char *new_name, size_t n);

/**
 * @brief fm_logname_start(), then the job's steps to its end on @p io.
 *
 * @param job       The job.
 * @param io        The card.
 * @param bench     The gate and the rules as they stand now; may be NULL.
 * @param last      The log closed last.
 * @param label     The label; "" or NULL for the plain name.
 * @param new_name  Takes the new name.
 * @param n         Bytes at @p new_name.
 * @return FM_OK when the log carries the new name; else the refusal or the
 *         failure, and both names are as they were.
 */
fm_err_t fm_logname_run(fm_job_t *job, const fm_io_t *io,
                        const fm_bench_t *bench, const fm_logname_t *last,
                        const char *label, char *new_name, size_t n);

#ifdef __cplusplus
}
#endif
