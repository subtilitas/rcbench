/**
 * @file
 * @brief The print job: the steps a printer file lists, run over one link,
 *        one write at a time (prn_job.h).
 *
 * Rules the file cannot change:
 * - one write is out at a time; its answer from the link is booked
 *   against that write, so the printer's answer may come before or after
 *   it;
 * - the joiner is emptied when a command is handed out;
 * - an end inside the abort range, after a write of that range went out,
 *   sends the abort template once, after the write in flight is answered;
 * - a link drop in a step with `relink`, after a write of such a step went
 *   out, waits relink_ms and goes on at the target; a second drop ends
 *   the job with PRN_LINK and no verdict;
 * - every job ends within PRN_JOB_MS.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "prn_job.h"

#define OUT_CMD   1u /**< the write out is a command */
#define OUT_DATA  2u /**< the write out is a data fragment */
#define OUT_ABORT 3u /**< the write out is the abort template */

_Static_assert(sizeof(prn_job_t) <= 2048u, "prn_job_t is over 2,048 bytes");

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
 * @brief The job's step list and its length.
 * @param j the job
 * @param n set to the length
 * @return the list
 */
static const uint8_t *list(const prn_job_t *j, unsigned *n)
{
    const prn_spec_t *s = j->spec;
    *n = (j->kind == PRN_JOB_PRINT) ? s->nprint : s->nquery;
    return (j->kind == PRN_JOB_PRINT) ? s->print : s->query;
}

/**
 * @brief The step at a position of the job's list.
 * @param j   the job
 * @param pos the position
 * @return the step
 */
static const prn_step_t *step_at(const prn_job_t *j, unsigned pos)
{
    unsigned n;
    const uint8_t *l = list(j, &n);
    if (n == 0u || n > PRN_STEPS) {
        return &j->spec->step[0]; /* prn_spec_check() refuses such a spec */
    }
    return &j->spec->step[l[(pos < n) ? pos : n - 1u] % PRN_STEPS];
}

/**
 * @brief End the job.
 * @param j   the job
 * @param err how
 */
static void end(prn_job_t *j, prn_err_t err)
{
    j->phase = PRN_PH_END;
    j->err = (uint8_t)err;
    j->want = false;
    j->await = false;
    j->data_due = false;
}

/**
 * @brief End the job on an error, through the abort template when a write
 *        of the abort range went out and the job is inside it.
 * @param j   the job
 * @param err the error
 * @param now the clock
 */
static void fail(prn_job_t *j, prn_err_t err, uint32_t now)
{
    if (j->armed && j->link && j->phase == PRN_PH_STEP
        && j->pos <= j->spec->abort_to) {
        j->end_err = (uint8_t)err;
        j->await = false;
        j->data_due = false;
        j->phase = PRN_PH_STOP;
        j->t_step = now;
        j->want = true;
        j->t_due = now;
        return;
    }
    end(j, err);
}

/**
 * @brief Enter the step at a position.
 * @param j      the job
 * @param pos    the position
 * @param now    the clock
 * @param relink entered by a relink: no delay
 */
static void enter(prn_job_t *j, unsigned pos, uint32_t now, bool relink)
{
    const prn_step_t *st = step_at(j, pos);
    j->phase = PRN_PH_STEP;
    j->pos = (uint8_t)pos;
    j->t_step = now;
    j->tries = 0u;
    j->await = false;
    j->want = false;
    j->data_due = false;
    switch (st->kind) {
    case PRN_S_SEND:
        j->want = true;
        j->t_due = now + (relink ? 0u : st->delay_ms);
        break;
    case PRN_S_DATA:
        j->frame = 0u;
        j->frag_at = 0u;
        j->data_due = true;
        j->t_due = now;
        break;
    default: /* PRN_S_WAIT */
        j->t_due = now + st->wait_ms;
        break;
    }
}

/**
 * @brief Start the verdict, or end a job whose verdict needs no read.
 * @param j   the job
 * @param now the clock
 */
static void verdict(prn_job_t *j, uint32_t now)
{
    const prn_spec_t *s = j->spec;
    switch (s->verdict) {
    case PRN_VD_COUNTER:
        j->phase = PRN_PH_VERDICT;
        j->reads = 0u;
        j->await = false;
        j->want = true;
        j->t_due = now;
        j->t_step = now;
        return;
    case PRN_VD_RESULT: {
        const bool have = (j->fields_have >> s->vd_field) & 1u;
        for (unsigned i = 0; have && i < s->nresult; ++i) {
            if (j->field[s->vd_field] == s->result[i]) {
                end(j, PRN_OK);
                return;
            }
        }
        end(j, PRN_NOT_PRINTED);
        return;
    }
    case PRN_VD_DONE:
        end(j, PRN_OK);
        return;
    default:
        end(j, PRN_SENT);
        return;
    }
}

/**
 * @brief Go on after a step: the next step, or the job's end.
 * @param j   the job
 * @param now the clock
 */
static void next(prn_job_t *j, uint32_t now)
{
    unsigned n;
    (void)list(j, &n);
    if ((unsigned)j->pos + 1u < n) {
        enter(j, j->pos + 1u, now, false);
    } else if (j->kind == PRN_JOB_QUERY) {
        end(j, PRN_OK);
    } else {
        verdict(j, now);
    }
}

/**
 * @brief A variable's value in the job.
 * @param j   the job
 * @param var prn_var_t
 * @return the value
 */
static uint64_t var_of(const prn_job_t *j, uint8_t var)
{
    return (var < PRN_V_COUNT) ? j->ctx.var[var] : 0u;
}

/**
 * @brief Fill the template values from the job.
 * @param j the job
 */
static void ctx_fill(prn_job_t *j)
{
    const prn_spec_t *s = j->spec;
    uint64_t *v = j->ctx.var;
    v[PRN_V_STREAM_LEN] = j->z.len;
    v[PRN_V_PAGES] = j->z.pages;
    v[PRN_V_FRAMES] = j->frames;
    v[PRN_V_LINES] = j->z.lines;
    v[PRN_V_LINE_BYTES] = s->line_bytes;
    v[PRN_V_HEAD_DOTS] = s->head_dots;
    v[PRN_V_DENSITY] = s->density;
    const uint32_t mean = (j->z.pages != 0u) ? j->z.len / j->z.pages : 0u;
    v[PRN_V_SPEED] = prn_speed(s, s->speed_stream ? j->z.len : mean);
    v[PRN_V_MLEAD] = s->mlead;
    v[PRN_V_MTRAIL] = s->mtrail;
    v[PRN_V_LAY_W] = j->z.w_mm;
    v[PRN_V_LAY_H] = j->z.h_mm;
    const uint8_t names[3] = { PRN_F_ROLL_W, PRN_F_ROLL_H, PRN_F_ROLL_GAP };
    for (unsigned i = 0; i < 3u; ++i) {
        v[PRN_V_ROLL_W + i] = ((j->fields_have >> names[i]) & 1u)
                                  ? j->field[names[i]] : 0u;
    }
}

/**
 * @brief Whether a condition holds on the fields.
 * @param j the job
 * @param c the condition
 * @return true when it holds
 */
static bool holds(const prn_job_t *j, const prn_cond_t *c)
{
    const uint64_t v = j->field[c->field];
    const uint64_t w = var_of(j, c->var);
    switch (c->op) {
    case PRN_OP_SET:
        return v != 0u;
    case PRN_OP_CLEAR:
        return v == 0u;
    case PRN_OP_EQ_N:
        return v == c->n;
    case PRN_OP_NE_N:
        return v != c->n;
    case PRN_OP_EQ_V:
        if ((c->var == PRN_V_LAY_W || c->var == PRN_V_LAY_H) && w == 0u) {
            return true;
        }
        return v == w;
    case PRN_OP_EQ_V1:
        return v == w - 1u;
    default: /* PRN_OP_NE_V */
        return v != w;
    }
}

/**
 * @brief Read the fields of an answer.
 * @param j the job
 * @param f the frame
 */
static void read_fields(prn_job_t *j, const uint8_t *f)
{
    const prn_spec_t *s = j->spec;
    const uint8_t key = f[s->key_off];
    for (unsigned i = 0; i < s->nfield; ++i) {
        const prn_field_t *d = &s->field[i];
        if (d->key != key) {
            continue;
        }
        const uint8_t *b = f + d->off;
        uint64_t v = 0u;
        switch (d->kind) {
        case PRN_FK_BIT:
            v = (b[0] >> d->n) & 1u;
            break;
        case PRN_FK_U8:
            v = b[0];
            break;
        case PRN_FK_LE16:
            v = (uint64_t)b[0] | ((uint64_t)b[1] << 8);
            break;
        case PRN_FK_BE16:
            v = ((uint64_t)b[0] << 8) | b[1];
            break;
        case PRN_FK_LE32:
            v = (uint64_t)b[0] | ((uint64_t)b[1] << 8) | ((uint64_t)b[2] << 16)
                | ((uint64_t)b[3] << 24);
            break;
        case PRN_FK_BE32:
            v = ((uint64_t)b[0] << 24) | ((uint64_t)b[1] << 16)
                | ((uint64_t)b[2] << 8) | b[3];
            break;
        default: /* PRN_FK_BYTES */
            for (unsigned k = 0; k < d->n; ++k) {
                v |= (uint64_t)b[k] << (8u * k);
            }
            break;
        }
        j->field[d->name] = v;
        j->fields_have |= 1u << d->name;
    }
}

/**
 * @brief The error an answer's flags report: the first error line in file
 *        order whose bit field is set in this answer.
 * @param j   the job
 * @param key the answer's key
 * @return PRN_OK when none
 */
static prn_err_t flag_error(const prn_job_t *j, uint8_t key)
{
    const prn_spec_t *s = j->spec;
    for (unsigned i = 0; i < s->nerror; ++i) {
        const int f = prn_field_find(s, key, s->error_field[i]);
        if (f >= 0 && s->field[f].kind == PRN_FK_BIT
            && j->field[s->error_field[i]] != 0u) {
            return (prn_err_t)s->error_code[i];
        }
    }
    return PRN_OK;
}

/**
 * @brief The roll an answer reports, against the file's limits.
 * @param j the job
 * @return PRN_OK, PRN_NO_LABEL for a width or height of 0, and in a PRINT
 *         PRN_SMALL or PRN_LARGE outside the limits
 */
static prn_err_t roll_error(const prn_job_t *j)
{
    const uint64_t w = j->field[PRN_F_ROLL_W];
    const uint64_t h = j->field[PRN_F_ROLL_H];
    const uint8_t *lim = j->spec->roll_lim;
    if (w == 0u || h == 0u) {
        return PRN_NO_LABEL;
    }
    if (j->kind != PRN_JOB_PRINT) {
        return PRN_OK;
    }
    if (w < lim[0] || h < lim[2]) {
        return PRN_SMALL;
    }
    if (w > lim[1] || h > lim[3]) {
        return PRN_LARGE;
    }
    return PRN_OK;
}

/**
 * @brief An answer in the counter verdict.
 * @param j   the job
 * @param now the clock
 */
static void on_counter(prn_job_t *j, uint32_t now)
{
    const prn_spec_t *s = j->spec;
    ++j->reads;
    const uint64_t left = j->field[s->vd_field];
    const uint64_t c0 = j->ctx.var[PRN_V_C0];
    const bool same = prn_field_find(s, s->vd_key, PRN_F_UID) < 0
                      || j->field[PRN_F_UID]
                             == j->ctx.var[PRN_V_UID0];
    if (same && c0 != 0u && left == c0 - 1u) {
        end(j, PRN_OK);
    } else if (!same || left != c0) {
        end(j, PRN_ROLL);
    } else if (j->reads >= s->vd_reads) {
        j->counted = true;
        end(j, j->relinked ? PRN_LINK : PRN_NOT_PRINTED);
    } else {
        j->want = true;
        j->t_due = now + s->vd_gap_ms;
    }
}

/**
 * @brief An answer in a step.
 * @param j   the job
 * @param key its key
 * @param now the clock
 */
static void on_step(prn_job_t *j, uint8_t key, uint32_t now)
{
    const prn_step_t *st = step_at(j, j->pos);
    prn_err_t e = st->flags ? flag_error(j, key) : PRN_OK;
    if (e == PRN_OK && st->roll) {
        e = roll_error(j);
    }
    for (unsigned i = 0; e == PRN_OK && i < st->ncheck; ++i) {
        if (!holds(j, &st->check[i])) {
            e = (prn_err_t)st->check[i].err;
        }
    }
    if (e != PRN_OK) {
        fail(j, e, now);
        return;
    }
    for (unsigned i = 0; i < st->nsave; ++i) {
        j->ctx.var[st->save_var[i]] = j->field[st->save_field[i]];
    }
    if (st->poll_ms == 0u) {
        next(j, now);
        return;
    }
    bool ok = true;
    for (unsigned i = 0; i < st->nuntil; ++i) {
        ok = ok && holds(j, &st->until[i]);
    }
    if (ok) {
        next(j, now);
    } else if (passed(now, j->t_step, st->within_ms)) {
        fail(j, (prn_err_t)st->else_err, now);
    } else {
        j->want = true;
        j->t_due = now + st->poll_ms;
    }
}

/**
 * @brief One whole answer frame.
 * @param j   the job
 * @param f   the frame
 * @param n   its length
 * @param now the clock
 */
static void on_frame(prn_job_t *j, const uint8_t *f, uint16_t n, uint32_t now)
{
    const prn_spec_t *s = j->spec;
    if (!j->await || n <= s->key_off || f[s->key_off] != j->await_key) {
        return;
    }
    for (unsigned i = 0; i < s->nanswer; ++i) {
        if (s->answer_key[i] == j->await_key && n < s->answer_min[i]) {
            return;
        }
    }
    j->await = false;
    read_fields(j, f);
    if (j->phase == PRN_PH_VERDICT) {
        on_counter(j, now);
    } else {
        on_step(j, f[s->key_off], now);
    }
}

/**
 * @brief Drop the first @p k bytes the joiner holds.
 * @param j the job
 * @param k bytes, at most rx_n
 */
static void drop(prn_job_t *j, uint16_t k)
{
    for (uint16_t i = k; i < j->rx_n; ++i) {
        j->rx[i - k] = j->rx[i];
    }
    j->rx_n = (uint16_t)(j->rx_n - k);
}

/**
 * @brief Drop held bytes until they start a frame that can be whole.
 * @param j the job
 * @return the frame's length once its header is in, else 0
 */
static uint16_t resync(prn_job_t *j)
{
    const prn_spec_t *s = j->spec;
    const unsigned w = prn_enc_width(s->len_enc);
    while (j->rx_n > 0u) {
        bool next_byte = false;
        for (unsigned i = 0; i < s->magic_len && !next_byte; ++i) {
            if (j->rx_n <= i) {
                return 0u;
            }
            next_byte = j->rx[i] != s->magic[i];
        }
        if (next_byte) {
            drop(j, 1u);
            continue;
        }
        if (j->rx_n < s->len_off + w) {
            return 0u;
        }
        const uint8_t *b = j->rx + s->len_off;
        uint32_t len = b[0];
        if (s->len_enc == PRN_E_LE16) {
            len = (uint32_t)b[0] | ((uint32_t)b[1] << 8);
        } else if (s->len_enc == PRN_E_BE16) {
            len = ((uint32_t)b[0] << 8) | b[1];
        }
        const uint32_t total = len + s->len_add;
        if (total < s->rx_min || total > s->rx_max) {
            drop(j, 1u);
            continue;
        }
        return (uint16_t)total;
    }
    return 0u;
}

/**
 * @brief Take bytes until a frame is whole (the sv_rx_feed() rule): bytes
 *        before the magic are dropped, a length outside min..max drops the
 *        first byte and searches again.
 * @param j     the job
 * @param in    the bytes
 * @param n     their count
 * @param frame set to the whole frame, or NULL
 * @param len   set to its length
 * @return the bytes of @p in taken
 */
static size_t feed(prn_job_t *j, const uint8_t *in, size_t n,
                   const uint8_t **frame, uint16_t *len)
{
    *frame = NULL;
    *len = 0u;
    if (j->rx_done != 0u) {
        drop(j, j->rx_done);
        j->rx_done = 0u;
    }
    size_t used = 0u;
    for (;;) {
        const uint16_t total = resync(j);
        if (total != 0u && j->rx_n >= total) {
            j->rx_done = total;
            *frame = j->rx;
            *len = total;
            return used;
        }
        if (used >= n) {
            return used;
        }
        size_t take = (total != 0u) ? (size_t)(total - j->rx_n) : 1u;
        if (take > n - used) {
            take = n - used;
        }
        memcpy(j->rx + j->rx_n, in + used, take);
        j->rx_n = (uint16_t)(j->rx_n + take);
        used += take;
    }
}

/**
 * @brief Whether a joined frame passes the file's tail and check.
 * @param s the spec
 * @param f the frame
 * @param n its length
 * @return true when it does
 */
static bool frame_ok(const prn_spec_t *s, const uint8_t *f, uint16_t n)
{
    if (s->tail_len != 0u
        && (n < s->tail_len
            || memcmp(f + n - s->tail_len, s->tail, s->tail_len) != 0)) {
        return false;
    }
    if (!s->has_rx_check) {
        return true;
    }
    const prn_tok_t *t = &s->rx_check;
    const long from = (t->a < 0) ? (long)n + t->a : t->a;
    const long to = (t->b < 0) ? (long)n + t->b : t->b;
    const unsigned w = prn_ck_width(t->x);
    if (from < 0 || to < from || to + (long)w >= (long)n) {
        return false;
    }
    const uint32_t v = prn_ck(t->x, f + from, (size_t)(to - from + 1));
    const bool be = t->x == PRN_CK_SUM16BE || t->x == PRN_CK_CRC16CCITT_BE;
    for (unsigned k = 0; k < w; ++k) {
        const uint8_t b = f[to + 1 + (be ? (long)(w - 1u - k) : (long)k)];
        if (b != (uint8_t)(v >> (8u * k))) {
            return false;
        }
    }
    return true;
}

bool prn_job_start(prn_job_t *j, const prn_spec_t *s, prn_job_kind_t kind,
                   const prn_stream_t *z, uint32_t now_ms)
{
    if (s == NULL || prn_spec_check(s) != 0u) {
        return false;
    }
    uint32_t frames = 0u;
    if (kind == PRN_JOB_PRINT) {
        if (z == NULL || z->data == NULL || z->len == 0u) {
            return false;
        }
        frames = prn_frame_count(s, z->len);
        if (frames == 0u || (s->nspeed != 0u && !s->speed_stream
                             && z->pages == 0u)) {
            return false;
        }
    }
    memset(j, 0, sizeof(*j));
    j->spec = s;
    j->kind = (uint8_t)kind;
    j->link = true;
    j->t_job = now_ms;
    j->frames = frames;
    if (kind == PRN_JOB_PRINT) {
        j->z = *z;
    }
    enter(j, 0u, now_ms, false);
    return true;
}

void prn_job_rx(prn_job_t *j, const uint8_t *data, size_t n, uint32_t now_ms)
{
    const prn_spec_t *s = j->spec;
    if (s == NULL) {
        return;
    }
    if (s->join == PRN_J_NOTIFY) {
        if (n >= s->rx_min && n <= s->rx_max) {
            memcpy(j->rx, data, n);
            if (!frame_ok(s, j->rx, (uint16_t)n)) {
                j->rx_bad++;
            } else if (j->phase != PRN_PH_IDLE && j->phase != PRN_PH_END) {
                on_frame(j, j->rx, (uint16_t)n, now_ms);
            }
        }
        return;
    }
    size_t used = 0u;
    while (used < n || j->rx_done != 0u) {
        const uint8_t *f = NULL;
        uint16_t len = 0u;
        used += feed(j, data + used, n - used, &f, &len);
        if (f == NULL) {
            break;
        }
        if (!frame_ok(s, f, len)) {
            j->rx_bad++;
        } else if (j->phase != PRN_PH_IDLE && j->phase != PRN_PH_END) {
            on_frame(j, f, len, now_ms);
        }
    }
}

/**
 * @brief The length of the data fragment at frag_at.
 * @param j the job
 * @return its bytes
 */
static uint16_t frag_len(const prn_job_t *j)
{
    const uint16_t left = (uint16_t)(j->frame_len - j->frag_at);
    return (left < j->spec->frag) ? left : j->spec->frag;
}

void prn_job_written(prn_job_t *j, bool ok, uint32_t now_ms)
{
    if (!j->in_flight) {
        return;
    }
    j->in_flight = false;
    const uint8_t kind = j->out_kind;
    j->out_kind = 0u;
    if (j->phase == PRN_PH_END || j->phase == PRN_PH_IDLE) {
        return;
    }
    if (j->phase == PRN_PH_STOP) {
        if (kind == OUT_ABORT) {
            end(j, (prn_err_t)j->end_err);
        }
        return;
    }
    if (!ok) {
        fail(j, PRN_WRITE, now_ms);
        return;
    }
    if (j->phase != PRN_PH_STEP || j->pos != j->out_pos) {
        return;
    }
    const prn_step_t *st = step_at(j, j->pos);
    if (kind == OUT_CMD) {
        /* A step without expect is done once its write is taken; with
         * expect its answer moves it, and may have come first. */
        if (st->kind == PRN_S_SEND && !st->has_expect) {
            next(j, now_ms);
        }
        return;
    }
    if (kind != OUT_DATA || st->kind != PRN_S_DATA) {
        return;
    }
    j->frag_at = (uint16_t)(j->frag_at + frag_len(j));
    if (j->frag_at >= j->frame_len) {
        j->frag_at = 0u;
        if (++j->frame >= j->frames) {
            next(j, now_ms);
            return;
        }
    }
    j->data_due = true;
    j->t_due = now_ms + j->spec->gap_ms;
}

/**
 * @brief The relink target of the step the job is in: for the verdict,
 *        that of the job's last step.
 * @param j the job
 * @return a step index, PRN_RELINK_VERDICT or PRN_RELINK_NONE
 */
static uint8_t relink_target(const prn_job_t *j)
{
    unsigned n;
    (void)list(j, &n);
    if (j->phase == PRN_PH_VERDICT) {
        return step_at(j, n - 1u)->relink;
    }
    return (j->phase == PRN_PH_STEP) ? step_at(j, j->pos)->relink
                                     : PRN_RELINK_NONE;
}

void prn_job_link(prn_job_t *j, bool up, uint32_t now_ms)
{
    if (j->phase == PRN_PH_IDLE || j->phase == PRN_PH_END || j->link == up) {
        return;
    }
    j->link = up;
    if (up) {
        if (j->phase != PRN_PH_RELINK) {
            return;
        }
        j->relinked = true;
        j->rx_n = 0u;
        j->rx_done = 0u;
        if (j->relink_to == PRN_RELINK_VERDICT) {
            verdict(j, now_ms);
            return;
        }
        unsigned n;
        const uint8_t *l = list(j, &n);
        for (unsigned i = 0; i < n; ++i) {
            if (l[i] == j->relink_to) {
                enter(j, i, now_ms, true);
                return;
            }
        }
        end(j, PRN_LINK);
        return;
    }
    const uint8_t target = relink_target(j);
    j->in_flight = false;
    j->out_kind = 0u;
    j->await = false;
    j->want = false;
    j->data_due = false;
    if (j->phase == PRN_PH_STOP) {
        end(j, (prn_err_t)j->end_err);
    } else if (j->relink_armed && !j->relinked && target != PRN_RELINK_NONE
               && j->kind == PRN_JOB_PRINT) {
        j->phase = PRN_PH_RELINK;
        j->relink_to = target;
        j->t_step = now_ms;
    } else {
        end(j, PRN_LINK);
    }
}

void prn_job_abort(prn_job_t *j, uint32_t now_ms)
{
    switch (j->phase) {
    case PRN_PH_IDLE:
    case PRN_PH_END:
    case PRN_PH_STOP:
        return;
    case PRN_PH_VERDICT:
    case PRN_PH_RELINK:
        end(j, PRN_ABORTED);
        return;
    default:
        fail(j, PRN_ABORTED, now_ms);
        return;
    }
}

/**
 * @brief Render the command the job wants into j->cmd.
 * @param j the job
 * @return false when it does not render
 */
static bool build_cmd(prn_job_t *j)
{
    const prn_spec_t *s = j->spec;
    const prn_send_t *snd = &s->abort;
    if (j->phase == PRN_PH_VERDICT) {
        snd = &s->vd_send;
    } else if (j->phase == PRN_PH_STEP) {
        snd = &step_at(j, j->pos)->send;
    }
    ctx_fill(j);
    for (unsigned i = 0; i < PRN_ARGS; ++i) {
        const uint64_t v = (snd->var[i] != PRN_V_NONE) ? var_of(j, snd->var[i])
                                                        : snd->n[i];
        if (v > 0xFFFFFFFFu) {
            return false;
        }
        j->ctx.arg[i] = (uint32_t)v;
    }
    j->ctx.data = NULL;
    j->ctx.data_len = 0u;
    j->ctx.data_pad = 0u;
    size_t n = 0u;
    if (!prn_tmpl_render(s, s->tmpl[snd->tmpl].seq, &j->ctx, j->cmd,
                         PRN_CMD_MAX, &n)) {
        return false;
    }
    j->cmd_len = (uint16_t)n;
    return true;
}

/**
 * @brief Mark what a write handed out in the current step arms.
 * @param j the job
 */
static void arm(prn_job_t *j)
{
    const prn_spec_t *s = j->spec;
    if (j->phase != PRN_PH_STEP || j->kind != PRN_JOB_PRINT) {
        return;
    }
    if (s->has_abort && j->pos >= s->abort_from && j->pos <= s->abort_to) {
        j->armed = true;
    }
    if (step_at(j, j->pos)->relink != PRN_RELINK_NONE) {
        j->relink_armed = true;
    }
}

bool prn_job_tx(prn_job_t *j, uint32_t now_ms, const uint8_t **data,
                uint16_t *n, prn_write_t *kind)
{
    prn_job_tick(j, now_ms);
    if ((j->phase != PRN_PH_STEP && j->phase != PRN_PH_VERDICT
         && j->phase != PRN_PH_STOP)
        || !j->link || j->in_flight) {
        return false;
    }
    while (j->phase == PRN_PH_STEP
           && step_at(j, j->pos)->kind == PRN_S_WAIT) {
        if (!due(now_ms, j->t_due)) {
            return false;
        }
        next(j, now_ms);
    }
    if (j->phase == PRN_PH_END) {
        return false;
    }
    const prn_spec_t *s = j->spec;
    if (j->data_due) {
        if (!due(now_ms, j->t_due)) {
            return false;
        }
        if (j->frag_at == 0u) {
            ctx_fill(j);
            size_t len = 0u;
            if (!prn_frame_build(s, &j->ctx, j->z.data, j->z.len, j->frame,
                                 j->buf, &len)) {
                fail(j, PRN_ENGINE, now_ms);
                return false;
            }
            j->frame_len = (uint16_t)len;
        }
        j->data_due = false;
        j->out_kind = OUT_DATA;
        *data = j->buf + j->frag_at;
        *n = frag_len(j);
        *kind = s->data_nr ? PRN_W_NR : PRN_W_REQ;
    } else {
        if (!j->want || !due(now_ms, j->t_due)) {
            return false;
        }
        if (!build_cmd(j)) {
            if (j->phase == PRN_PH_STOP) {
                end(j, (prn_err_t)j->end_err);
            } else {
                fail(j, PRN_ENGINE, now_ms);
            }
            return false;
        }
        j->out_kind = (j->phase == PRN_PH_STOP) ? OUT_ABORT : OUT_CMD;
        if (j->phase == PRN_PH_STEP) {
            const prn_step_t *st = step_at(j, j->pos);
            j->await = st->has_expect;
            j->await_key = st->expect;
        } else {
            j->await = j->phase == PRN_PH_VERDICT;
            j->await_key = s->vd_key;
        }
        j->want = false;
        ++j->tries;
        j->rx_n = 0u;
        j->rx_done = 0u;
        *data = j->cmd;
        *n = j->cmd_len;
        *kind = s->cmd_nr ? PRN_W_NR : PRN_W_REQ;
    }
    arm(j);
    j->out_pos = j->pos;
    j->in_flight = true;
    j->t_sent = now_ms;
    return true;
}

/**
 * @brief The write in flight got no answer in time.
 * @param j   the job
 * @param now the clock
 */
static void write_lost(prn_job_t *j, uint32_t now)
{
    const uint8_t kind = j->out_kind;
    j->in_flight = false;
    j->out_kind = 0u;
    if (j->phase == PRN_PH_STOP) {
        if (kind == OUT_ABORT) {
            end(j, (prn_err_t)j->end_err);
        }
        return;
    }
    const bool data = kind == OUT_DATA && j->phase == PRN_PH_STEP;
    fail(j, data ? (prn_err_t)step_at(j, j->pos)->else_err : PRN_SILENT, now);
}

void prn_job_tick(prn_job_t *j, uint32_t now_ms)
{
    if (j->phase == PRN_PH_IDLE || j->phase == PRN_PH_END) {
        return;
    }
    const prn_spec_t *s = j->spec;
    if (passed(now_ms, j->t_job, PRN_JOB_MS)) {
        if (j->phase == PRN_PH_STOP) {
            end(j, (prn_err_t)j->end_err);
        } else {
            fail(j, PRN_ENGINE, now_ms);
        }
        return;
    }
    if (j->phase == PRN_PH_RELINK) {
        if (passed(now_ms, j->t_step, s->relink_ms)) {
            end(j, PRN_LINK);
        }
        return;
    }
    const prn_step_t *st = (j->phase == PRN_PH_STEP) ? step_at(j, j->pos)
                                                      : NULL;
    if (j->in_flight) {
        const uint32_t limit = (j->out_kind == OUT_DATA && st != NULL)
                                   ? st->within_ms : s->answer_ms;
        if (passed(now_ms, j->t_sent, limit)) {
            write_lost(j, now_ms);
            return;
        }
    }
    const bool poll = st != NULL && st->poll_ms != 0u;
    if (j->await) {
        const uint32_t limit = (st != NULL && !poll) ? st->within_ms
                                                      : s->answer_ms;
        if (passed(now_ms, j->t_sent, limit)) {
            j->await = false;
            if (st != NULL && !poll && j->tries < st->tries) {
                j->want = true;
                j->t_due = now_ms;
            } else {
                fail(j, (st != NULL && !poll) ? (prn_err_t)st->else_err
                                              : PRN_SILENT, now_ms);
            }
            return;
        }
    }
    if (poll && passed(now_ms, j->t_step, st->within_ms)) {
        fail(j, (prn_err_t)st->else_err, now_ms);
    }
}

bool prn_job_ended(const prn_job_t *j)
{
    return j->phase == PRN_PH_END;
}

bool prn_job_field(const prn_job_t *j, uint8_t name, uint64_t *v)
{
    if (name >= PRN_F_NAMES || ((j->fields_have >> name) & 1u) == 0u) {
        return false;
    }
    *v = j->field[name];
    return true;
}
