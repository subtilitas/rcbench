/**
 * @file fm_logname.c
 * @brief RENAME LOG's rename of the run log closed last (fm_logname.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "fm_logname.h"

#include <string.h>

#include "fm_name.h"

_Static_assert(LOG_LABELLED_MAX == FM_NAME_MAX,
               "a labelled name is as long as FILES lets a person type");

/** @brief Steps a rename takes at most: its one call, and a margin. */
#define LOGNAME_STEPS 4

fm_err_t fm_logname_start(fm_job_t *job, const fm_bench_t *bench,
                          const fm_logname_t *last, const char *label,
                          char *new_name, size_t n)
{
    if (job == NULL) {
        return FM_ERR_ARG;
    }
    memset(job, 0, sizeof(*job));
    job->state = FM_JOB_FAILED;
    job->err = FM_ERR_ARG;
    if (new_name == NULL || n == 0u) {
        return FM_ERR_ARG;
    }
    new_name[0] = '\0';
    if (last == NULL || log_run_number(last->name) < 0) {
        return FM_ERR_ARG;
    }
    if (!log_name_label(new_name, n, last->name, label)) {
        job->err = FM_ERR_NAME;
        return FM_ERR_NAME;
    }
    /* The gate, the place, the typed-name rules and the paths:
     * fm_job_rename() asks them in that order. */
    return fm_job_rename(job, bench, last->mount, last->dir, last->name,
                         false, new_name);
}

fm_err_t fm_logname_run(fm_job_t *job, const fm_io_t *io,
                        const fm_bench_t *bench, const fm_logname_t *last,
                        const char *label, char *new_name, size_t n)
{
    const fm_err_t err = fm_logname_start(job, bench, last, label, new_name,
                                          n);
    if (err != FM_OK) {
        return err;
    }
    for (int i = 0; i < LOGNAME_STEPS; ++i) {
        if (fm_job_step(job, io, bench, NULL, 0u) != FM_JOB_RUNNING) {
            break;
        }
    }
    if (fm_job_state(job, NULL, NULL) == FM_JOB_RUNNING) {
        fm_job_cancel(job);
        (void)fm_job_step(job, io, bench, NULL, 0u);
    }
    return (fm_job_state(job, NULL, NULL) == FM_JOB_DONE) ? FM_OK : job->err;
}
