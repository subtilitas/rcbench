/**
 * @file files_host_io.c
 * @brief The FILES screen's card task on the host (files_host_io.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "files_host_io.h"

#include <string.h>

/** @brief The chunk a copy runs in, FM_CHUNK as on the panel. */
static uint8_t s_chunk[FM_CHUNK];

/**
 * @brief The bench now, for the runner.
 * @param out  takes it
 * @param ctx  the task
 */
static void fh_bench(fm_bench_t *out, void *ctx)
{
    fh_t *h = (fh_t *)ctx;
    ++h->steps;
    out->armed = h->armed
                 || (h->arm_after_steps >= 0 && h->steps > h->arm_after_steps);
    out->link_up = h->link_up;
    h->marks.run_dir = h->run_dir;
    h->marks.test_dir = h->test_dir;
    out->marks = &h->marks;
}

/**
 * @brief Whether CANCEL was asked.
 * @param ctx  the task
 * @return the flag
 */
// cppcheck-suppress constParameterCallback
static bool fh_cancelled(void *ctx)
{
    return ((const fh_t *)ctx)->cancel;
}

/**
 * @brief A job's bytes after a step.
 * @param done   bytes done
 * @param total  bytes to do
 * @param ctx    the task
 */
static void fh_progress_set(uint64_t done, uint64_t total, void *ctx)
{
    fh_t *h = (fh_t *)ctx;
    if (!h->fixed_progress) {
        h->done = done;
        h->total = total;
    }
}

/**
 * @brief The look at the slot, counted.
 * @param ctx  the task
 */
static void fh_look(void *ctx)
{
    ++((fh_t *)ctx)->looks;
}

/**
 * @brief Run @p req to its end.
 * @param h    the task
 * @param req  the request
 */
static void run_req(fh_t *h, fm_req_t *req)
{
    const fm_runner_t run = {
        .io = &h->io, .bench = fh_bench, .cancelled = fh_cancelled,
        .progress = fh_progress_set, .look = fh_look, .chunk = s_chunk,
        .size = sizeof(s_chunk), .ctx = h,
    };
    (void)fm_req_run(req, &run);
}

/**
 * @brief The io's submit().
 * @param req  the request
 * @param ctx  the task
 * @return false while one is held, or with fail_submit
 */
static bool fh_submit(fm_req_t *req, void *ctx)
{
    fh_t *h = (fh_t *)ctx;
    if (h->fail_submit || h->held != NULL) {
        return false;
    }
    ++h->submits;
    h->last_kind = req->kind;
    h->cancel = false;
    if (!h->fixed_progress) {
        h->done = 0u;
        h->total = 0u;
    }
    if (h->defer) {
        h->held = req;
        return true;
    }
    run_req(h, req);
    return true;
}

/**
 * @brief The io's idle().
 * @param ctx  the task
 * @return true with nothing held
 */
// cppcheck-suppress constParameterCallback
static bool fh_idle(void *ctx)
{
    return ((const fh_t *)ctx)->held == NULL;
}

/**
 * @brief The io's progress().
 * @param done   takes the bytes done
 * @param total  takes the bytes to do
 * @param ctx    the task
 */
// cppcheck-suppress constParameterCallback
static void fh_progress(uint64_t *done, uint64_t *total, void *ctx)
{
    const fh_t *h = (const fh_t *)ctx;
    *done = h->done;
    *total = h->total;
}

/**
 * @brief The io's cancel().
 * @param ctx  the task
 */
static void fh_cancel(void *ctx)
{
    fh_t *h = (fh_t *)ctx;
    h->cancel = true;
    ++h->cancels;
}

/**
 * @brief The io's bench(), as the screen reads it each frame.
 * @param out  takes it
 * @param ctx  the task
 */
static void fh_screen_bench(fm_bench_t *out, void *ctx)
{
    fh_t *h = (fh_t *)ctx;
    out->armed = h->armed;
    out->link_up = h->link_up;
    h->marks.run_dir = h->run_dir;
    h->marks.test_dir = h->test_dir;
    out->marks = &h->marks;
}

/**
 * @brief The io's volume().
 * @param ctx  the task
 * @return the name
 */
// cppcheck-suppress constParameterCallback
static const char *fh_volume(void *ctx)
{
    const fh_t *h = (const fh_t *)ctx;
    return (h->volume != NULL) ? h->volume : "";
}

/**
 * @brief The io's card().
 * @param ctx  the task
 * @return the slot's state
 */
// cppcheck-suppress constParameterCallback
static card_state_t fh_card(void *ctx)
{
    return ((const fh_t *)ctx)->state;
}

void fh_init(fh_t *h)
{
    memset(h, 0, sizeof(*h));
    fk_init(&h->card);
    h->io = fk_io(&h->card);
    h->link_up = true;
    h->arm_after_steps = -1;
    memcpy(h->run_dir, CARD_AUTO_LOGS, sizeof(CARD_AUTO_LOGS));
    memcpy(h->test_dir, CARD_USER_LOGS, sizeof(CARD_USER_LOGS));
}

void fh_free(fh_t *h)
{
    fk_free(&h->card);
}

files_io_t fh_files_io(fh_t *h)
{
    const files_io_t io = {
        .submit = fh_submit, .idle = fh_idle, .progress = fh_progress,
        .cancel = fh_cancel, .bench = fh_screen_bench, .volume = fh_volume,
        .card = fh_card, .ctx = h,
    };
    return io;
}

bool fh_run(fh_t *h)
{
    if (h->held == NULL) {
        return false;
    }
    fm_req_t *req = h->held;
    run_req(h, req);
    h->held = NULL;
    return true;
}
