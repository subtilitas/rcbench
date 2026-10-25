/**
 * @file fm_req.c
 * @brief The runner of the FILES screen's requests on the card's task
 *        (fm_req.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "fm_req.h"

#include <string.h>

/**
 * @brief What one walk of a folder counts for INFO.
 */
typedef struct {
    unsigned entries;  /**< entries handed over       */
    uint64_t bytes;    /**< their files' bytes        */
} info_walk_t;

/**
 * @brief Count one entry of a folder INFO looks into.
 * @param e     the entry, borrowed for the call
 * @param vctx  the walk's info_walk_t
 */
static void info_take(const fm_dirent_t *e, void *vctx)
{
    info_walk_t *w = (info_walk_t *)vctx;
    if (e == NULL || e->name == NULL || e->name[0] == '\0'
        || strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0) {
        return;
    }
    ++w->entries;
    if (!e->is_dir) {
        w->bytes += e->size;
    }
}

void fm_req_bench(const fm_req_t *req, fm_bench_t *live)
{
    if (req == NULL || live == NULL) {
        return;
    }
    live->user_logs = (req->user_logs[0] != '\0') ? req->user_logs : NULL;
    live->auto_logs = (req->auto_logs[0] != '\0') ? req->auto_logs : NULL;
}

/**
 * @brief The bench as it stands, with the request's log folders.
 * @param req    the request
 * @param run    the runner
 * @param bench  takes the bench
 */
static void bench_now(const fm_req_t *req, const fm_runner_t *run,
                      fm_bench_t *bench)
{
    memset(bench, 0, sizeof(*bench));
    run->bench(bench, run->ctx);
    fm_req_bench(req, bench);
}

/**
 * @brief INFO: the entry's size and time; for a folder its entries and
 *        their bytes from one walk.
 * @param req  the request, which takes the answer
 * @param io   the card
 * @return FM_OK or why not
 */
static fm_err_t info(fm_req_t *req, const fm_io_t *io)
{
    memset(&req->info, 0, sizeof(req->info));
    req->info.mtime = -1;
    char path[FM_PATH_SIZE];
    const fm_path_t p = fm_path_join(req->dir, req->name, req->is_dir, path,
                                     sizeof(path));
    if (p != FM_PATH_OK) {
        return fm_path_err(p);
    }
    if (!io->mounted(io->ctx)) {
        return FM_ERR_NO_CARD;
    }
    /* The card the name was listed from, or none: another card holds
     * another entry under the name. */
    if (!io->hold(io->ctx, req->mount)) {
        return FM_ERR_CARD_GONE;
    }
    fm_stat_t st;
    memset(&st, 0, sizeof(st));
    st.mtime = -1;
    fm_err_t err = io->stat(io->ctx, path, &st);
    if (err == FM_OK && st.is_dir != req->is_dir) {
        /* Another kind of entry under the name since the listing. */
        err = FM_ERR_NOT_FOUND;
    }
    if (err == FM_OK) {
        req->info.size = st.is_dir ? 0u : st.size;
        req->info.mtime = st.mtime;
    }
    if (err == FM_OK && req->is_dir) {
        info_walk_t w;
        memset(&w, 0, sizeof(w));
        unsigned unlisted = 0u;
        err = io->walk(io->ctx, path, info_take, &w, &unlisted);
        req->info.entries = w.entries;
        req->info.unlisted = unlisted;
        req->info.size = w.bytes;
    }
    io->release(io->ctx);
    return err;
}

/**
 * @brief JOB: the job stepped until it stops, the bench and CANCEL asked
 *        before each step.
 * @param req  the request
 * @param run  the runner
 * @return FM_OK for a job done, else the job's err
 */
static fm_err_t job(fm_req_t *req, const fm_runner_t *run)
{
    fm_job_t *j = &req->job;
    fm_bench_t bench;
    while (j->state == FM_JOB_RUNNING) {
        bench_now(req, run, &bench);
        if (run->cancelled != NULL && run->cancelled(run->ctx)) {
            fm_job_cancel(j);
        }
        (void)fm_job_step(j, run->io, &bench, run->chunk, run->size);
        if (run->progress != NULL) {
            run->progress(j->done, j->total, run->ctx);
        }
    }
    if (j->state == FM_JOB_DONE) {
        return FM_OK;
    }
    return (j->err != FM_OK) ? j->err : FM_ERR_ARG;
}

fm_err_t fm_req_run(fm_req_t *req, const fm_runner_t *run)
{
    if (req == NULL) {
        return FM_ERR_ARG;
    }
    if (run == NULL || run->io == NULL || run->bench == NULL) {
        req->err = FM_ERR_ARG;
        return req->err;
    }
    if (req->look && run->look != NULL) {
        run->look(run->ctx);
    }
    fm_bench_t bench;
    fm_err_t err;
    switch (req->kind) {
    case FM_REQ_PAGE:
        bench_now(req, run, &bench);
        err = fm_tree_page(&req->tree, run->io, &bench, req->go);
        break;
    case FM_REQ_ENTER:
        bench_now(req, run, &bench);
        err = fm_tree_enter(&req->tree, run->io, &bench, req->row);
        break;
    case FM_REQ_UP:
        bench_now(req, run, &bench);
        err = fm_tree_up(&req->tree, run->io, &bench);
        break;
    case FM_REQ_INFO:
        err = info(req, run->io);
        break;
    case FM_REQ_JOB:
        err = job(req, run);
        break;
    case FM_REQ_NONE:
    default:
        err = FM_ERR_ARG;
        break;
    }
    req->err = err;
    return err;
}
