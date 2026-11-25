/**
 * @file
 * @brief The Supvan print job: QUERY and PRINT over one link, one write
 *        at a time, with the label counter as the proof of a print
 *        (supvan.h).
 *
 * The printer acknowledges and completes a job it cannot decode, with no
 * error flag; only its label counter shows whether a label came out.  So a
 * PRINT reads the counter c0 before START and again after the printer is
 * idle: c0 - 1 is a print, c0 three times is none.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "supvan.h"

/** @brief Sends of CHECK and RETURN_MAT in steps 1 and 2: one retry. */
#define FIRST_TRIES 2u
/** @brief Data fragments a frame. */
#define FRAGS (SV_FRAME_LEN / SV_FRAGMENT)

/**
 * @brief Whether @p ms have passed since @p since, across the clock's wrap.
 * @param now   the clock
 * @param since the start
 * @param ms    the time
 * @return true when they have
 */
static bool passed(uint32_t now, uint32_t since, uint32_t ms)
{
    return (uint32_t)(now - since) >= ms;
}

/**
 * @brief Whether a time has come, across the clock's wrap.
 * @param now the clock
 * @param at  the time
 * @return true from @p at on, for 2^31ms
 */
static bool due(uint32_t now, uint32_t at)
{
    return (uint32_t)(now - at) < 0x80000000u;
}

/**
 * @brief Enter a step.
 * @param j    the job
 * @param step the step
 * @param now  the clock
 */
static void enter(sv_job_t *j, sv_step_t step, uint32_t now)
{
    j->step = (uint8_t)step;
    j->t_step = now;
    j->tries = 0u;
}

/**
 * @brief Ask for a command's write at a time.
 * @param j   the job
 * @param cmd the command
 * @param at  when it is due
 */
static void ask(sv_job_t *j, uint8_t cmd, uint32_t at)
{
    j->want = cmd;
    j->t_due = at;
}

/**
 * @brief End the job.
 * @param j   the job
 * @param err how
 */
static void end(sv_job_t *j, sv_err_t err)
{
    j->step = SV_STEP_END;
    j->err = (uint8_t)err;
    j->want = 0u;
    j->await = 0u;
    j->data_due = false;
}

/**
 * @brief End the job on an error, through STOP_PRINT when START was sent
 *        and printing is not over.
 * @param j   the job
 * @param err the error
 * @param now the clock
 */
static void fail(sv_job_t *j, sv_err_t err, uint32_t now)
{
    if (j->started && j->link && j->step <= SV_STEP_DONE) {
        j->end_err = (uint8_t)err;
        j->await = 0u;
        j->data_due = false;
        enter(j, SV_STEP_STOP, now);
        ask(j, SV_C_STOP, now);
        return;
    }
    end(j, err);
}

/**
 * @brief Poll again, or give up when the step's time is over.
 * @param j     the job
 * @param limit the step's time in ms
 * @param err   the end when it is over
 * @param now   the clock
 */
static void poll_or_give_up(sv_job_t *j, uint32_t limit, sv_err_t err,
                            uint32_t now)
{
    if (passed(now, j->t_step, limit)) {
        fail(j, err, now);
    } else {
        ask(j, SV_C_INQUIRY, now + SV_POLL_MS);
    }
}

/**
 * @brief The step's deadline and the end it gives.
 * @param step the step
 * @param err  set to the end
 * @return the time in ms, 0 for a step without one
 */
static uint32_t deadline(uint8_t step, sv_err_t *err)
{
    static const uint32_t k_limit[SV_STEP_END + 1] = {
        [SV_STEP_READY] = SV_READY_MS,       [SV_STEP_PRINTING] = SV_PRINTING_MS,
        [SV_STEP_BUFFER] = SV_BUFFER_MS,     [SV_STEP_DONE] = SV_DONE_MS,
        [SV_STEP_RELINK] = SV_RELINK_MS,
    };
    *err = SV_ERR_SILENT;
    if (step == SV_STEP_READY) {
        *err = SV_ERR_BUSY;
    } else if (step == SV_STEP_RELINK) {
        *err = SV_ERR_LINK;
    }
    return (step <= SV_STEP_END) ? k_limit[step] : 0u;
}

/**
 * @brief A RETURN_MAT answer in step 2.
 * @param j   the job
 * @param m   the record
 * @param now the clock
 */
static void on_material(sv_job_t *j, const sv_material_t *m, uint32_t now)
{
    j->mat = *m;
    const sv_err_t roll = sv_roll_check(m);
    if (roll == SV_ERR_NO_LABEL) {
        end(j, roll);
        return;
    }
    if (j->kind == SV_JOB_PRINT) {
        if (roll != SV_OK) {
            end(j, roll);
            return;
        }
        if (j->z.w_mm != 0u
            && (j->z.w_mm != m->w_mm || j->z.h_mm != m->h_mm)) {
            end(j, SV_ERR_ROLL);
            return;
        }
    }
    enter(j, SV_STEP_READY, now);
    ask(j, SV_C_INQUIRY, now);
}

/**
 * @brief A RETURN_MAT answer in step 12: the counter's verdict.
 * @param j   the job
 * @param m   the record
 * @param now the clock
 */
static void on_counter(sv_job_t *j, const sv_material_t *m, uint32_t now)
{
    ++j->reads;
    const bool same_roll = memcmp(m->uid, j->mat.uid, SV_UID_LEN) == 0;
    const bool one_less = j->mat.left != 0u && m->left == j->mat.left - 1u;
    if (same_roll && one_less) {
        end(j, SV_OK);
    } else if (!same_roll || m->left != j->mat.left) {
        /* another roll, or a count that moved by other than 1 */
        end(j, SV_ERR_ROLL);
    } else if (j->reads >= SV_COUNTER_READS) {
        end(j, j->relinked ? SV_ERR_LINK : SV_ERR_NOT_PRINTED);
    } else {
        ask(j, SV_C_MATERIAL, now + SV_COUNTER_MS);
    }
}

/**
 * @brief An INQUIRY answer.
 * @param j   the job
 * @param st  the status
 * @param now the clock
 */
static void on_status(sv_job_t *j, const sv_status_t *st, uint32_t now)
{
    const sv_err_t e = sv_status_error(st);
    if (e != SV_OK) {
        fail(j, e, now);
        return;
    }
    switch (j->step) {
    case SV_STEP_READY:
        if (st->busy || st->printing) {
            poll_or_give_up(j, SV_READY_MS, SV_ERR_BUSY, now);
        } else if (j->kind == SV_JOB_QUERY) {
            end(j, SV_OK);
        } else {
            enter(j, SV_STEP_START, now);
            ask(j, SV_C_START, now);
        }
        break;
    case SV_STEP_PRINTING:
        if (st->printing) {
            enter(j, SV_STEP_BUFFER, now);
            ask(j, SV_C_INQUIRY, now);
        } else {
            poll_or_give_up(j, SV_PRINTING_MS, SV_ERR_SILENT, now);
        }
        break;
    case SV_STEP_BUFFER:
        if (!st->buf_full) {
            enter(j, SV_STEP_BULK, now);
            ask(j, SV_C_BULK, now);
        } else {
            poll_or_give_up(j, SV_BUFFER_MS, SV_ERR_SILENT, now);
        }
        break;
    default: /* SV_STEP_DONE */
        if (!st->printing && !st->busy) {
            enter(j, SV_STEP_COUNTER, now);
            j->reads = 0u;
            ask(j, SV_C_MATERIAL, now);
        } else {
            poll_or_give_up(j, SV_DONE_MS, SV_ERR_SILENT, now);
        }
        break;
    }
}

/**
 * @brief One whole answer frame.
 * @param j   the job
 * @param f   the frame
 * @param n   its length
 * @param now the clock
 */
static void on_frame(sv_job_t *j, const uint8_t *f, uint16_t n, uint32_t now)
{
    if (j->await == 0u || f[SV_CMD_AT] != j->await) {
        return;
    }
    if (j->await == SV_C_INQUIRY) {
        sv_status_t st;
        if (!sv_status_parse(f, n, &st)) {
            return;
        }
        j->await = 0u;
        j->st = st;
        on_status(j, &st, now);
        return;
    }
    if (j->await == SV_C_MATERIAL) {
        sv_material_t m;
        if (!sv_material_parse(f, n, &m)) {
            return;
        }
        j->await = 0u;
        j->last = m;
        if (j->step == SV_STEP_MATERIAL) {
            on_material(j, &m, now);
        } else {
            on_counter(j, &m, now);
        }
        return;
    }
    j->await = 0u;
    switch (j->step) {
    case SV_STEP_CHECK:
        enter(j, SV_STEP_MATERIAL, now);
        ask(j, SV_C_MATERIAL, now);
        break;
    case SV_STEP_START:
        enter(j, SV_STEP_PRINTING, now);
        ask(j, SV_C_INQUIRY, now);
        break;
    case SV_STEP_BULK:
        enter(j, SV_STEP_DATA, now);
        j->frame = 0u;
        j->frag = 0u;
        j->data_due = true;
        j->t_due = now;
        break;
    default: /* SV_STEP_FULL */
        enter(j, SV_STEP_DONE, now);
        ask(j, SV_C_INQUIRY, now + SV_POLL_MS);
        break;
    }
}

bool sv_job_start(sv_job_t *j, sv_job_kind_t kind, const sv_stream_t *z,
                  uint32_t now_ms)
{
    if (kind == SV_JOB_PRINT
        && (z == NULL || z->data == NULL || z->len == 0u
            || z->len > SV_STREAM_MAX || z->buffers == 0u)) {
        return false;
    }
    memset(j, 0, sizeof(*j));
    j->kind = (uint8_t)kind;
    j->link = true;
    if (kind == SV_JOB_PRINT) {
        j->z = *z;
        j->frames = sv_chunk_count(z->len);
    }
    sv_rx_reset(&j->rx);
    enter(j, SV_STEP_CHECK, now_ms);
    ask(j, SV_C_CHECK, now_ms);
    return true;
}

void sv_job_rx(sv_job_t *j, const uint8_t *data, size_t n, uint32_t now_ms)
{
    size_t used = 0u;
    while (used < n || j->rx.done != 0u) {
        const uint8_t *f = NULL;
        uint16_t len = 0u;
        used += sv_rx_feed(&j->rx, data + used, n - used, &f, &len);
        if (f == NULL) {
            break;
        }
        if (j->step != SV_STEP_IDLE && j->step != SV_STEP_END) {
            on_frame(j, f, len, now_ms);
        }
    }
}

void sv_job_written(sv_job_t *j, bool ok, uint32_t now_ms)
{
    if (!j->in_flight) {
        return;
    }
    j->in_flight = false;
    if (j->step == SV_STEP_END) {
        return;
    }
    if (j->step == SV_STEP_STOP) {
        /* The answer to STOP_PRINT ends the job; the answer to a write
         * that was out when the job failed lets STOP_PRINT go. */
        if (j->stop_out) {
            end(j, (sv_err_t)j->end_err);
        }
        return;
    }
    if (!ok) {
        fail(j, SV_ERR_WRITE, now_ms);
        return;
    }
    if (j->step != SV_STEP_DATA) {
        return;
    }
    if (++j->frag == FRAGS) {
        j->frag = 0u;
        if (++j->frame == j->frames) {
            enter(j, SV_STEP_PAUSE, now_ms);
            ask(j, SV_C_BUF_FULL, now_ms + SV_PAUSE_MS);
            return;
        }
    }
    j->data_due = true;
    j->t_due = now_ms + SV_FRAG_GAP_MS;
}

void sv_job_link(sv_job_t *j, bool up, uint32_t now_ms)
{
    if (j->step == SV_STEP_IDLE || j->step == SV_STEP_END || j->link == up) {
        return;
    }
    j->link = up;
    if (up) {
        if (j->step == SV_STEP_RELINK) {
            j->relinked = true;
            sv_rx_reset(&j->rx);
            enter(j, SV_STEP_COUNTER, now_ms);
            j->reads = 0u;
            ask(j, SV_C_MATERIAL, now_ms);
        }
        return;
    }
    j->in_flight = false;
    j->await = 0u;
    j->want = 0u;
    j->data_due = false;
    if (j->step == SV_STEP_STOP) {
        end(j, (sv_err_t)j->end_err);
    } else if (j->started && !j->relinked) {
        enter(j, SV_STEP_RELINK, now_ms);
    } else {
        end(j, SV_ERR_LINK);
    }
}

void sv_job_abort(sv_job_t *j, uint32_t now_ms)
{
    switch (j->step) {
    case SV_STEP_IDLE:
    case SV_STEP_END:
    case SV_STEP_STOP:
        return;
    case SV_STEP_COUNTER:
    case SV_STEP_RELINK:
        end(j, SV_ERR_ABORTED);
        return;
    default:
        fail(j, SV_ERR_ABORTED, now_ms);
        return;
    }
}

/**
 * @brief Build the command @p j wants into j->cmd.
 * @param j the job
 */
static void build_cmd(sv_job_t *j)
{
    uint16_t p0 = 0u;
    uint16_t p1 = 0u;
    if (j->want == SV_C_BULK) {
        p0 = SV_FRAME_LEN;
        p1 = j->frames;
    } else if (j->want == SV_C_BUF_FULL) {
        p0 = (uint16_t)j->z.len;
        p1 = sv_speed(j->z.len / j->z.buffers);
    }
    sv_cmd_build(j->cmd, j->want, p0, p1);
}

bool sv_job_tx(sv_job_t *j, uint32_t now_ms, const uint8_t **data,
               uint16_t *n)
{
    sv_job_tick(j, now_ms);
    if (j->step == SV_STEP_IDLE || j->step == SV_STEP_END || !j->link
        || j->in_flight || (j->want == 0u && !j->data_due)
        || !due(now_ms, j->t_due)) {
        return false;
    }
    if (j->data_due) {
        if (j->frag == 0u) {
            (void)sv_chunk_build(j->buf, j->z.data, j->z.len,
                                 (uint8_t)j->frame);
        }
        j->data_due = false;
        *data = j->buf + (size_t)j->frag * SV_FRAGMENT;
        *n = SV_FRAGMENT;
    } else {
        build_cmd(j);
        if (j->want == SV_C_START) {
            j->started = true;
        }
        j->stop_out = (j->want == SV_C_STOP);
        if (j->step == SV_STEP_PAUSE) {
            enter(j, SV_STEP_FULL, now_ms);
        }
        j->await = (j->want == SV_C_STOP) ? 0u : j->want;
        j->want = 0u;
        ++j->tries;
        sv_rx_reset(&j->rx);
        *data = j->cmd;
        *n = SV_CMD_LEN;
    }
    j->in_flight = true;
    j->t_sent = now_ms;
    return true;
}

void sv_job_tick(sv_job_t *j, uint32_t now_ms)
{
    if (j->step == SV_STEP_IDLE || j->step == SV_STEP_END) {
        return;
    }
    if (j->in_flight && passed(now_ms, j->t_sent, SV_ANSWER_MS)) {
        j->in_flight = false;
        if (j->step == SV_STEP_STOP) {
            if (j->stop_out) {
                end(j, (sv_err_t)j->end_err);
            }
        } else {
            fail(j, SV_ERR_SILENT, now_ms);
        }
        return;
    }
    if (j->await != 0u && passed(now_ms, j->t_sent, SV_ANSWER_MS)) {
        const uint8_t cmd = j->await;
        j->await = 0u;
        if (j->step <= SV_STEP_MATERIAL && j->tries < FIRST_TRIES) {
            ask(j, cmd, now_ms);
        } else {
            fail(j, SV_ERR_SILENT, now_ms);
        }
        return;
    }
    sv_err_t err;
    const uint32_t limit = deadline(j->step, &err);
    if (limit != 0u && passed(now_ms, j->t_step, limit)) {
        if (j->step == SV_STEP_RELINK) {
            end(j, err);
        } else {
            fail(j, err, now_ms);
        }
    }
}

bool sv_job_ended(const sv_job_t *j)
{
    return j->step == SV_STEP_END;
}
