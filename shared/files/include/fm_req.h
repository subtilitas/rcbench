/**
 * @file fm_req.h
 * @brief One request of the FILES screen to the card's task, and the runner
 *        that answers it: a page of a pane, a folder entered or left, the
 *        facts of one entry for INFO, or a job run to its end.
 *
 * The screen makes no card call.  It fills an fm_req_t, hands it to the
 * card's task and does not touch it again until the task says it is
 * answered; then it reads the answer from the same request.  One request
 * is under way at a time.  The card's task calls fm_req_run(), which makes
 * every card call of the request through the runner's fm_io_t:
 *
 *   PAGE   fm_tree_page() of the pane the request carries
 *   ENTER  fm_tree_enter() of its row
 *   UP     fm_tree_up()
 *   INFO   the size and time of one entry (fm_io_t stat()); for a folder
 *          also its entries and their bytes, from one walk
 *   JOB    fm_job_step() until the job is no longer FM_JOB_RUNNING
 *
 * The bench the rules and the write gate read is asked of the runner
 * before each step, so a job learns of an ARM, a log run opening or the
 * IOmcu link going at its next chunk.  CANCEL is asked the same way.  The
 * log folders in force come from the request: the screen copies them in
 * when it fills it, since they change only on SETUP.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "card_tree.h"
#include "fm_io.h"
#include "fm_ops.h"
#include "fm_rules.h"
#include "fm_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief What a request asks for.
 */
typedef enum {
    FM_REQ_NONE = 0,  /**< nothing: answered FM_ERR_ARG                  */
    FM_REQ_PAGE,      /**< a page of the pane: fm_tree_page()            */
    FM_REQ_ENTER,     /**< the folder on a row: fm_tree_enter()          */
    FM_REQ_UP,        /**< the folder above: fm_tree_up()                */
    FM_REQ_INFO,      /**< one entry's facts for INFO                    */
    FM_REQ_JOB        /**< a job of fm_ops.h, run to its end             */
} fm_req_kind_t;

/**
 * @brief What INFO reads of one entry.
 */
typedef struct {
    uint64_t size;      /**< a file's bytes; a folder's files' bytes     */
    int64_t mtime;      /**< the time FAT stamped, as fm_stat_t's; -1
                             when the card gives none                    */
    unsigned entries;   /**< a folder's entries the walk handed over     */
    unsigned unlisted;  /**< a folder's entries it counted, not listed   */
} fm_info_t;

/**
 * @brief One request.  The screen fills the fields of its kind, the
 *        runner writes the answer into the same request.
 */
typedef struct {
    fm_req_kind_t kind;           /**< what is asked                       */
    /** @brief The slot is looked at before the request: a card put in,
     *         taken out or swapped since the last look is found
     *         (fm_runner_t look). */
    bool look;
    fm_tree_t tree;               /**< PAGE, ENTER, UP: the pane, read and
                                       written in place                    */
    fm_go_t go;                   /**< PAGE: which page                    */
    int row;                      /**< ENTER: the row of the folder        */
    char dir[FM_PATH_SIZE];       /**< INFO: the entry's folder            */
    char name[FM_NAME_BUF];       /**< INFO: its name                      */
    bool is_dir;                  /**< INFO: it is a folder                */
    unsigned mount;               /**< INFO: the mount it was listed from  */
    fm_job_t job;                 /**< JOB: started by an fm_job_*() call  */
    char user_logs[CARD_DIR_MAX]; /**< USER LOG FOLDER in force; "" is
                                       CARD_USER_LOGS                      */
    char auto_logs[CARD_DIR_MAX]; /**< AUTO LOG FOLDER in force; "" is
                                       CARD_AUTO_LOGS                      */
    fm_info_t info;               /**< INFO's answer                       */
    fm_err_t err;                 /**< the answer: FM_OK or why not        */
} fm_req_t;

/**
 * @brief What the card's task gives the runner: the card, the bench as it
 *        stands, CANCEL, the progress, the look at the slot, and the room
 *        of one chunk.
 */
typedef struct {
    const fm_io_t *io;  /**< the card                                    */
    /**
     * @brief The bench now: ARMED, the logger's open files, the IOmcu link.
     *        The runner sets the log folders from the request after it.
     * @param out  takes the bench; its pointers have to outlive the step
     * @param ctx  the runner's context
     */
    void (*bench)(fm_bench_t *out, void *ctx);
    /**
     * @brief Whether CANCEL was pressed since the request was handed over.
     * @param ctx  the runner's context
     * @return true to stop the job at its next chunk
     */
    bool (*cancelled)(void *ctx);
    /**
     * @brief A job's bytes done after a step, for the progress box; may be
     *        NULL.
     * @param done   bytes done
     * @param total  bytes to do; 0 for a single call
     * @param ctx    the runner's context
     */
    void (*progress)(uint64_t done, uint64_t total, void *ctx);
    /**
     * @brief Look at the slot, for a request with @p look set; may be NULL.
     * @param ctx  the runner's context
     */
    void (*look)(void *ctx);
    uint8_t *chunk;     /**< room for one chunk of a copy                */
    size_t size;        /**< bytes @p chunk takes; FM_CHUNK on the panel */
    void *ctx;          /**< passed to every function                    */
} fm_runner_t;

/**
 * @brief Answer @p req: every card call it needs, to its end.
 *
 * A JOB is stepped until it is no longer FM_JOB_RUNNING, with the bench
 * and CANCEL asked before each step; its answer is FM_OK when it is
 * FM_JOB_DONE and the job's err otherwise.  A JOB that is not running when
 * it arrives, a refusal of its start, keeps that answer.
 *
 * @param req  the request
 * @param run  the card and the rest; @p run->io and @p run->bench set
 * @return the answer, also in @p req->err; FM_ERR_ARG for a NULL or an
 *         unknown kind
 */
fm_err_t fm_req_run(fm_req_t *req, const fm_runner_t *run);

/**
 * @brief The bench the rules read for @p req: @p live, the bench as it
 *        stands, with the log folders the request carries.
 * @param req   the request
 * @param live  the bench now, filled by the runner's bench()
 */
void fm_req_bench(const fm_req_t *req, fm_bench_t *live);

#ifdef __cplusplus
}
#endif
