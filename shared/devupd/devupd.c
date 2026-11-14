/**
 * @file devupd.c
 * @brief The wireless device update's transfer engine and its row
 *        (devupd.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "devupd.h"

#include <string.h>

/**
 * @brief Queue a control write; a full queue drops it (it holds the three
 *        writes of a start and END or POLL or ABORT, never more).
 * @param e the transfer
 * @param b the bytes
 * @param n their count, 1 to BLEOTA_CTRL_MAX
 */
static void queue(devupd_t *e, const uint8_t *b, size_t n)
{
    if (e->q_n == DEVUPD_CTRL_Q || n == 0u || n > BLEOTA_CTRL_MAX) {
        return;
    }
    const uint8_t i = (uint8_t)((e->q_head + e->q_n) % DEVUPD_CTRL_Q);
    memcpy(e->q[i], b, n);
    e->q_len[i] = (uint8_t)n;
    ++e->q_n;
}

/**
 * @brief Queue END, ABORT or POLL.
 * @param e  the transfer
 * @param op BLEOTA_OP_*
 */
static void queue_op(devupd_t *e, uint8_t op)
{
    uint8_t b[BLEOTA_LEN_OP];
    const size_t n = bleota_op_encode(op, b);
    queue(e, b, n);
}

/**
 * @brief Enter a phase.
 * @param e     the transfer
 * @param phase devupd_phase_t
 * @param now   the clock
 */
static void go(devupd_t *e, devupd_phase_t phase, uint32_t now)
{
    e->phase = (uint8_t)phase;
    e->t = now;
}

/**
 * @brief End without an install; control writes not yet sent are dropped.
 * @param e    the transfer
 * @param err  why
 * @param code DEVUPD_E_DEVICE: the device's BLEOTA_E_*
 * @param now  the clock
 */
static void fail(devupd_t *e, devupd_err_t err, uint8_t code, uint32_t now)
{
    e->err = (uint8_t)err;
    e->dev_code = code;
    e->q_n = 0u;
    go(e, DEVUPD_FAILED, now);
}

void devupd_init(devupd_t *e)
{
    if (e != NULL) {
        memset(e, 0, sizeof *e);
    }
}

bool devupd_busy(const devupd_t *e)
{
    return e != NULL && e->phase >= DEVUPD_LINK
           && e->phase <= DEVUPD_RESTART;
}

bool devupd_start(devupd_t *e, const devupd_image_t *im, uint32_t now)
{
    if (e == NULL || devupd_busy(e)) {
        return false;
    }
    const bool up = e->up;
    devupd_init(e);
    e->up = up;
    if (im == NULL || im->img == NULL || im->size == 0u) {
        fail(e, DEVUPD_E_IMAGE, 0u, now);
        return true;
    }
    e->im = *im;
    go(e, DEVUPD_LINK, now);
    return true;
}

/**
 * @brief The link is up in DEVUPD_LINK: queue BEGIN and the SHA-256's two
 *        halves.
 * @param e   the transfer
 * @param mtu the agreed MTU
 * @param now the clock
 */
static void begin(devupd_t *e, uint16_t mtu, uint32_t now)
{
    uint8_t b[BLEOTA_CTRL_MAX];
    const uint16_t ver[3] = { e->im.version.major, e->im.version.minor,
                              e->im.version.patch };
    e->chunk = bleota_chunk_bytes(mtu);
    size_t n = bleota_begin_encode(e->im.kind, e->im.size, ver,
                                   e->im.reinstall ? BLEOTA_F_REINSTALL : 0u,
                                   b);
    queue(e, b, n);
    n = bleota_hash_encode(e->im.sha, 0u, b);
    queue(e, b, n);
    n = bleota_hash_encode(e->im.sha, 1u, b);
    queue(e, b, n);
    go(e, DEVUPD_BEGIN, now);
}

void devupd_link(devupd_t *e, bool up, uint16_t mtu, uint32_t now)
{
    if (e == NULL) {
        return;
    }
    e->up = up;
    if (up) {
        if (e->phase == DEVUPD_LINK) {
            begin(e, mtu, now);
        }
        return;
    }
    e->ctrl_busy = false;
    if (e->phase == DEVUPD_RESTART) {
        go(e, DEVUPD_DONE, now);
    } else if (e->phase == DEVUPD_END) {
        /* The device may be installing: the result is not known. */
        fail(e, DEVUPD_E_UNSURE, 0u, now);
    } else if (e->phase >= DEVUPD_BEGIN && e->phase <= DEVUPD_SEND) {
        fail(e, DEVUPD_E_LOST, 0u, now);
    }
}

void devupd_written(devupd_t *e, uint16_t status, uint32_t now)
{
    if (e == NULL || !e->ctrl_busy) {
        return;
    }
    e->ctrl_busy = false;
    if (status != 0u && devupd_busy(e)) {
        fail(e, DEVUPD_E_WRITE, 0u, now);
    }
}

/**
 * @brief A CREDIT: the device's next offset and limit.
 *
 * A routine CREDIT's next lies behind what was sent by whatever is in
 * flight, so it moves nothing back.  The sending goes back to next only
 * when the device marks bytes as lost (BLEOTA_CREDIT_GAP), or when the
 * same next and limit come twice: the device took nothing in between.
 * @param e   the transfer
 * @param s   the CREDIT
 * @param now the clock
 */
static void credit(devupd_t *e, const bleota_status_t *s, uint32_t now)
{
    if (s->next > e->im.size || s->limit > e->im.size) {
        return;
    }
    if (e->phase == DEVUPD_BEGIN || e->phase == DEVUPD_CONFIRM) {
        go(e, DEVUPD_SEND, now);
        e->sent = s->next;
        e->next = s->next;
        e->limit = s->limit;
        e->t_credit = now;
        e->t_moved = now;
        e->polls = 0u;
        return;
    }
    if (e->phase != DEVUPD_SEND) {
        return;
    }
    const bool stuck = s->next == e->next && s->limit == e->limit;
    if (s->next < e->sent && (s->code == BLEOTA_CREDIT_GAP || stuck)) {
        ++e->rewinds;
        e->sent = s->next;
    }
    if (s->next > e->next) {
        e->t_moved = now;
    }
    e->next = s->next;
    e->limit = s->limit;
    e->t_credit = now;
    e->polls = 0u;
}

void devupd_notify(devupd_t *e, const uint8_t *b, size_t n, uint32_t now)
{
    bleota_status_t s;
    if (e == NULL || !devupd_busy(e) || !bleota_status_decode(b, n, &s)) {
        return;
    }
    switch (s.ev) {
    case BLEOTA_EV_WAIT:
        if (e->phase == DEVUPD_BEGIN) {
            e->secs = s.secs;
            go(e, DEVUPD_CONFIRM, now);
        }
        break;
    case BLEOTA_EV_CREDIT:
        credit(e, &s, now);
        break;
    case BLEOTA_EV_DONE:
        if (e->phase == DEVUPD_END) {
            e->next = e->im.size;
            go(e, DEVUPD_RESTART, now);
        }
        break;
    case BLEOTA_EV_FAIL:
        if (e->phase != DEVUPD_RESTART) {
            fail(e, DEVUPD_E_DEVICE, s.code, now);
        }
        break;
    default:
        break;
    }
}

/**
 * @brief The phase's time ran out.
 * @param e   the transfer
 * @param now the clock
 */
static void times(devupd_t *e, uint32_t now)
{
    const uint32_t age = now - e->t;
    if (e->ctrl_busy && now - e->t_ctrl >= DEVUPD_ANSWER_MS) {
        /* END's answer may be lost after the device took it. */
        fail(e, e->phase == DEVUPD_END ? DEVUPD_E_UNSURE : DEVUPD_E_WRITE, 0u,
             now);
        return;
    }
    switch (e->phase) {
    case DEVUPD_LINK:
        if (age >= DEVUPD_LINK_MS) {
            fail(e, DEVUPD_E_LINK, 0u, now);
        }
        break;
    case DEVUPD_BEGIN:
        /* The answer is due once the three writes are taken. */
        if (e->q_n == 0u && !e->ctrl_busy && age >= DEVUPD_ANSWER_MS) {
            fail(e, DEVUPD_E_TIMEOUT, 0u, now);
        }
        break;
    case DEVUPD_CONFIRM:
        if (age >= DEVUPD_CONFIRM_MS) {
            fail(e, DEVUPD_E_TIMEOUT, 0u, now);
        }
        break;
    case DEVUPD_SEND:
        /* Answers that never move the device on end it as well: each
         * CREDIT clears the polls, and each POLL the device's own timer. */
        if (now - e->t_moved >= DEVUPD_STALL_MS) {
            fail(e, DEVUPD_E_STALLED, 0u, now);
            break;
        }
        if (now - e->t_credit >= DEVUPD_POLL_MS) {
            if (e->polls >= DEVUPD_POLLS) {
                fail(e, DEVUPD_E_TIMEOUT, 0u, now);
                break;
            }
            ++e->polls;
            ++e->polls_sent;
            e->t_credit = now;
            queue_op(e, BLEOTA_OP_POLL);
        }
        break;
    case DEVUPD_END:
        if (age >= DEVUPD_END_MS) {
            fail(e, DEVUPD_E_UNSURE, 0u, now);
        }
        break;
    case DEVUPD_RESTART:
        if (age >= DEVUPD_RESTART_MS) {
            go(e, DEVUPD_DONE, now);
        }
        break;
    default:
        break;
    }
}

bool devupd_step(devupd_t *e, uint32_t now, bool can_ctrl, devupd_out_t *out)
{
    if (e == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    if (devupd_busy(e)) {
        times(e, now);
    }
    if (e->phase == DEVUPD_SEND && e->next == e->im.size && e->q_n == 0u
        && !e->ctrl_busy) {
        queue_op(e, BLEOTA_OP_END);
        go(e, DEVUPD_END, now);
    }
    if (e->q_n > 0u && can_ctrl && !e->ctrl_busy && e->up) {
        out->kind = DEVUPD_OUT_CTRL;
        out->len = e->q_len[e->q_head];
        memcpy(out->ctrl, e->q[e->q_head], out->len);
        e->q_head = (uint8_t)((e->q_head + 1u) % DEVUPD_CTRL_Q);
        --e->q_n;
        e->ctrl_busy = true;
        e->t_ctrl = now;
        return true;
    }
    if (e->phase != DEVUPD_SEND || !e->up || e->sent >= e->limit
        || e->chunk == 0u) {
        return false;
    }
    uint32_t n = e->chunk;
    if (e->sent + n > e->limit) {
        n = e->limit - e->sent;
    }
    out->kind = DEVUPD_OUT_CHUNK;
    out->off = e->sent;
    out->n = (uint16_t)n;
    e->sent += n;
    ++e->chunks;
    return true;
}

void devupd_unsend(devupd_t *e, const devupd_out_t *out)
{
    if (e == NULL || out == NULL || out->kind != DEVUPD_OUT_CHUNK
        || e->phase != DEVUPD_SEND || out->off + out->n != e->sent) {
        return;
    }
    e->sent = out->off;
    --e->chunks;
}

bool devupd_abortable(const devupd_t *e)
{
    return e != NULL && e->phase >= DEVUPD_LINK && e->phase <= DEVUPD_SEND;
}

void devupd_abort(devupd_t *e, uint32_t now)
{
    if (!devupd_abortable(e)) {
        return;
    }
    fail(e, DEVUPD_E_ABORTED, 0u, now);
}

unsigned devupd_percent(const devupd_t *e)
{
    if (e == NULL || e->im.size == 0u) {
        return 0u;
    }
    return (unsigned)(((uint64_t)e->next * 100u) / e->im.size);
}

devupd_row_t devupd_row(const devupd_row_in_t *in)
{
    if (in == NULL) {
        return DEVUPD_ROW_NO_IMAGE;
    }
    if (in->busy) {
        return DEVUPD_ROW_UPDATING;
    }
    if (!in->have_image) {
        return DEVUPD_ROW_NO_IMAGE;
    }
    if (in->image_due) {
        return DEVUPD_ROW_CHECKING;
    }
    if (!in->image_ok) {
        return DEVUPD_ROW_BAD_IMAGE;
    }
    if (in->armed) {
        return DEVUPD_ROW_ARMED;
    }
    if (in->other) {
        return DEVUPD_ROW_BUSY;
    }
    if (!in->linked || !in->known) {
        return DEVUPD_ROW_NOT_LINKED;
    }
    return fw_version_cmp(in->running, in->carried) == 0
               ? DEVUPD_ROW_REINSTALL : DEVUPD_ROW_READY;
}

devupd_settle_t devupd_settle(bool known, fw_version_t running,
                              fw_version_t want, uint32_t age)
{
    if (known) {
        return fw_version_cmp(running, want) == 0 ? DEVUPD_SETTLE_DONE
                                                  : DEVUPD_SETTLE_FAILED;
    }
    return age >= DEVUPD_SETTLE_MS ? DEVUPD_SETTLE_UNKNOWN
                                   : DEVUPD_SETTLE_WAIT;
}

void devupd_gate_ask(devupd_gate_t *g, unsigned dev)
{
    if (g != NULL) {
        atomic_store(&g->dev, dev);
        atomic_store(&g->req, true);
    }
}

devupd_take_t devupd_gate_take(devupd_gate_t *g, bool armed, unsigned *dev)
{
    if (g == NULL || !atomic_load(&g->req)) {
        return DEVUPD_TAKE_NONE;
    }
    if (dev != NULL) {
        *dev = atomic_load(&g->dev);
    }
    atomic_store(&g->run, true);
    atomic_store(&g->req, false);
    if (armed) {
        atomic_store(&g->run, false);
        return DEVUPD_TAKE_ARMED;
    }
    return DEVUPD_TAKE_START;
}

void devupd_gate_end(devupd_gate_t *g)
{
    if (g != NULL) {
        atomic_store(&g->run, false);
    }
}

bool devupd_gate_busy(devupd_gate_t *g)
{
    /* The request first: the running task stores the run before it clears
     * the request, so a reader that misses the request sees the run. */
    return g != NULL && (atomic_load(&g->req) || atomic_load(&g->run));
}

void devupd_settler_arm(devupd_settler_t *s, unsigned dev, fw_version_t want,
                        uint32_t now)
{
    if (s != NULL) {
        s->pending = true;
        s->dev = dev;
        s->want = want;
        s->t = now;
    }
}

bool devupd_settler_step(devupd_settler_t *s, const devupd_seen_t *seen,
                         unsigned n, uint32_t now, bool now_start,
                         devupd_settled_t *out)
{
    if (s == NULL || out == NULL || !s->pending) {
        return false;
    }
    const bool own = seen != NULL && s->dev < n;
    const bool known = own && seen[s->dev].known;
    const fw_version_t run = known ? seen[s->dev].running
                                   : (fw_version_t){ 0u, 0u, 0u };
    devupd_settle_t r = devupd_settle(known, run, s->want, now - s->t);
    if (r == DEVUPD_SETTLE_WAIT && now_start) {
        r = DEVUPD_SETTLE_UNKNOWN;
    }
    if (r == DEVUPD_SETTLE_WAIT) {
        return false;
    }
    s->pending = false;
    out->dev = s->dev;
    out->r = r;
    out->running = run;
    return true;
}
