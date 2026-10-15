/*
 * The panel's half of the BUS link page.  See bus_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bus_link.h"

#include <stddef.h>
#include <string.h>

/* The exchange in flight. */
enum {
    X_NONE = 0,
    X_STATUS,        /* read 0 to 28 */
    X_IDS,           /* read 29 to 44 */
    X_STAGE,         /* write a window of 4 to 13 */
    X_CMD,           /* write 0 to 3 */
};

/* The staged registers go out in windows of 4, 4 and 2. */
#define STAGE_WINDOWS 3u

_Static_assert(LINK_BS_W_STAGED == 10u, "stage windows");
_Static_assert(BUS_LINK_STATUS_N <= 32u && BUS_LINK_IDS_N <= 32u,
               "one read each");

static bool staged_op(link_bus_op_t op)
{
    return op == LINK_BUS_OP_WRITE || op == LINK_BUS_OP_EEPROM;
}

static void answer(bus_link_t *k, bus_link_ans_t a, uint8_t nack)
{
    if (!k->active) {
        return;
    }
    k->ans_id = k->cmd.id;
    k->ans = (uint8_t)a;
    k->ans_nack = nack;
    k->ans_seq = a == BUS_LINK_ANS_TAKEN ? k->frame[LINK_BS_W_SEQ] : 0u;
    k->answers++;
    k->active = false;
    k->framed = false;
    k->sent = false;
    k->tries = 0;
}

void bus_link_init(bus_link_t *k)
{
    if (k != NULL) {
        memset(k, 0, sizeof(*k));
    }
}

void bus_link_came_up(bus_link_t *k, uint16_t minor)
{
    if (k == NULL) {
        return;
    }
    k->up = true;
    k->minor = minor;
    k->page = minor >= BUS_LINK_MINOR;
    k->known = false;
    k->poll_now = true;
    k->pending = X_NONE;
    k->tries = 0;
    k->ids_known = false;
    if (!k->page) {
        answer(k, BUS_LINK_ANS_DOWN, 0);
    }
}

void bus_link_lost(bus_link_t *k)
{
    if (k == NULL) {
        return;
    }
    answer(k, BUS_LINK_ANS_DOWN, 0);
    k->up = false;
    k->known = false;
    k->pending = X_NONE;
    k->stop_due = false;
}

bool bus_link_has_page(const bus_link_t *k)
{
    return k != NULL && k->up && k->page;
}

bool bus_link_send(bus_link_t *k, const bus_link_cmd_t *c)
{
    if (!bus_link_has_page(k) || c == NULL || k->active
        || c->op == LINK_BUS_OP_NONE || c->op >= LINK_BUS_OP_COUNT
        || (staged_op(c->op) && (c->n == 0u || c->n > LINK_BS_BYTES))) {
        return false;
    }
    k->cmd = *c;
    k->active = true;
    k->framed = false;
    k->sent = false;
    k->reseq = false;
    k->tries = 0;
    k->stage_at = staged_op(c->op) ? 0u : (uint8_t)STAGE_WINDOWS;
    memset(k->staged, 0, sizeof(k->staged));
    if (staged_op(c->op)) {
        k->staged[0] = c->n;                    /* LINK_BS_W_N */
        k->staged[LINK_BS_W_KEY - LINK_BS_W_N] =
            c->op == LINK_BUS_OP_EEPROM && c->key
                ? (uint16_t)LINK_BUS_WRITE_KEY : 0u;
        for (unsigned i = 0; i < c->n; ++i) {
            k->staged[LINK_BS_W_DATA - LINK_BS_W_N + i / 2u] |=
                (uint16_t)((unsigned)c->data[i] << (8u * (i % 2u)));
        }
    }
    return true;
}

/* The bus by the last read: neither closed nor unknown. */
static bool bus_up(const bus_link_t *k)
{
    return k->known
           && LINK_BS_STATE_OF(k->status[LINK_BS_STATE])
                  != (uint8_t)LINK_BUS_CLOSED;
}

void bus_link_stop(bus_link_t *k)
{
    if (k == NULL) {
        return;
    }
    if (k->active && !k->sent) {
        answer(k, BUS_LINK_ANS_DROPPED, 0);
    }
    if (bus_up(k) || (k->active && k->cmd.op == LINK_BUS_OP_OPEN)) {
        k->stop_due = true;
    }
}

bool bus_link_holds(const bus_link_t *k)
{
    return k != NULL
           && (bus_up(k) || (k->active && k->cmd.op == LINK_BUS_OP_OPEN));
}

static void read_x(bus_link_xfer_t *x, uint8_t page, uint8_t off, uint8_t n)
{
    memset(x, 0, sizeof(*x));
    x->page = page;
    x->off = off;
    x->n = n;
}

static void status_x(bus_link_t *k, uint32_t now_ms, bus_link_xfer_t *x)
{
    k->pending = X_STATUS;
    k->poll_now = false;
    k->polled = true;
    k->status_ms = now_ms;
    read_x(x, (uint8_t)LINK_PAGE_BUS, 0u, (uint8_t)BUS_LINK_STATUS_N);
}

/* The command's frame, with the SEQ after the page's. */
static void frame(bus_link_t *k)
{
    const bus_link_cmd_t *c = &k->cmd;

    k->frame[LINK_BS_W_CMD] = LINK_BS_W_CMD_OF(c->op, c->port, c->proto,
                                               c->sts);
    k->frame[LINK_BS_W_SEQ] = (uint16_t)(k->status[LINK_BS_SEQ] + 1u);
    k->frame[LINK_BS_W_ARG] = c->arg;
    k->frame[LINK_BS_W_AUX] = staged_op(c->op)
                                  ? link_kst_crc(k->staged, LINK_BS_W_STAGED)
                                  : c->aux;
    k->framed = true;
}

/* A STOP to @p port. */
static void stop_cmd(bus_link_t *k, uint8_t port, uint8_t proto)
{
    memset(&k->cmd, 0, sizeof(k->cmd));
    k->cmd.op = LINK_BUS_OP_STOP;
    k->cmd.port = port;
    k->cmd.proto = proto;
    k->cmd.id = 0u;
    k->active = true;
    k->framed = false;
    k->sent = false;
    k->reseq = false;
    k->tries = 0;
    k->stage_at = (uint8_t)STAGE_WINDOWS;
}

static bool poll_due(const bus_link_t *k, uint32_t now_ms)
{
    const uint16_t st = k->status[LINK_BS_STATE];
    const bool fast = bus_up(k) || (st & LINK_BS_F_BUSY) != 0u;

    return !k->polled
           || (uint32_t)(now_ms - k->status_ms)
                  >= (fast ? BUS_LINK_POLL_MS : BUS_LINK_IDLE_MS);
}

bool bus_link_next(bus_link_t *k, uint32_t now_ms, bus_link_xfer_t *x)
{
    if (!bus_link_has_page(k) || x == NULL) {
        return false;
    }
    if (!k->known || k->poll_now) {
        status_x(k, now_ms, x);
        return true;
    }
    /* A STOP goes before any frame but its own: a command whose frame
     * went unanswered may have been taken, so it ends without an answer and
     * the STOP follows; an OPEN that may have been taken gets the STOP
     * too. */
    if (k->stop_due
        && (!k->active || !k->sent || k->cmd.op != LINK_BUS_OP_STOP)) {
        const uint16_t st = k->status[LINK_BS_STATE];
        const bool open_went = k->active && k->sent
                               && k->cmd.op == LINK_BUS_OP_OPEN;
        const uint8_t port = open_went ? k->cmd.port : LINK_BS_PORT_OF(st);
        const uint8_t proto = open_went ? k->cmd.proto
                                        : LINK_BS_PROTO_OF(st);

        k->stop_due = false;
        if (k->active) {
            answer(k, k->sent ? BUS_LINK_ANS_NO_ANSWER
                              : BUS_LINK_ANS_DROPPED, 0);
        }
        if (bus_up(k) || open_went) {
            stop_cmd(k, port, proto);
        }
    }
    if (k->active) {
        if (k->stage_at < STAGE_WINDOWS) {
            const uint8_t off = (uint8_t)(LINK_BS_W_N + 4u * k->stage_at);
            const uint8_t n = k->stage_at + 1u < STAGE_WINDOWS ? 4u : 2u;

            memset(x, 0, sizeof(*x));
            x->write = true;
            x->page = (uint8_t)LINK_PAGE_BUS;
            x->off = off;
            x->n = n;
            memcpy(x->regs, &k->staged[off - LINK_BS_W_N],
                   n * sizeof(uint16_t));
            k->pending = X_STAGE;
            return true;
        }
        if (!k->framed) {
            frame(k);
        }
        memset(x, 0, sizeof(*x));
        x->write = true;
        x->page = (uint8_t)LINK_PAGE_BUS;
        x->off = 0u;
        x->n = (uint8_t)LINK_BS_W_CMD_FRAME;
        memcpy(x->regs, k->frame, sizeof(k->frame));
        k->pending = X_CMD;
        k->sent = true;
        return true;
    }
    if (k->ids_due) {
        k->pending = X_IDS;
        read_x(x, (uint8_t)LINK_PAGE_BUS_IDS, (uint8_t)LINK_BI_IDS,
               (uint8_t)BUS_LINK_IDS_N);
        return true;
    }
    if (poll_due(k, now_ms)) {
        status_x(k, now_ms, x);
        return true;
    }
    return false;
}

/* The scan's IDs are owed: a scan whose found count or SEQ moved. */
static void ids_look(bus_link_t *k)
{
    const uint8_t op = (uint8_t)(k->status[LINK_BS_OP] & 0xFFu);
    const uint8_t found = (uint8_t)(k->status[LINK_BS_SCAN] >> 8);
    const uint8_t result = (uint8_t)(k->status[LINK_BS_RESULT] & 0xFFu);

    if (op == (uint8_t)LINK_BUS_OP_OPEN) {
        /* A new session found nothing yet. */
        k->ids_known = false;
        k->ids_due = false;
        k->ids_found = 0;
        memset(k->ids, 0, sizeof(k->ids));
        return;
    }
    if (op != (uint8_t)LINK_BUS_OP_SCAN) {
        return;
    }
    if (!k->ids_known || found != k->ids_found
        || (result != 1u && k->ids_seq != k->status[LINK_BS_SEQ])) {
        k->ids_due = true;
    }
}

/* A frame unanswered: sent again, or the command is given up. */
static void unanswered(bus_link_t *k)
{
    if (++k->tries >= BUS_LINK_RETRIES) {
        answer(k, BUS_LINK_ANS_NO_ANSWER, 0);
        k->poll_now = true;
    }
}

void bus_link_done(bus_link_t *k, int result, const uint16_t *regs)
{
    uint8_t was;

    if (k == NULL) {
        return;
    }
    was = k->pending;
    k->pending = X_NONE;
    if (result == (int)LINK_NACK_BAD_PAGE) {
        k->page = false;
        answer(k, BUS_LINK_ANS_DOWN, 0);
        return;
    }
    switch (was) {
    case X_STATUS:
        if (result == BUS_LINK_ACK && regs != NULL) {
            memcpy(k->status, regs, sizeof(k->status));
            k->known = true;
            /* BUS_LINK_RETRIES counts frames unanswered in a row: an
             * answered read ends the streak. */
            k->tries = 0;
            ids_look(k);
        } else if (result == BUS_LINK_NO_ANSWER) {
            k->poll_now = true;
            if (k->active) {
                unanswered(k);
            }
        }
        break;
    case X_IDS:
        k->ids_due = false;
        if (result == BUS_LINK_ACK && regs != NULL) {
            memcpy(k->ids, regs, sizeof(k->ids));
            k->ids_known = true;
            k->ids_seq = k->status[LINK_BS_SEQ];
            k->ids_found = (uint8_t)(k->status[LINK_BS_SCAN] >> 8);
        }
        break;
    case X_STAGE:
        if (result == BUS_LINK_ACK) {
            k->stage_at++;
            k->tries = 0;
        } else if (result == BUS_LINK_NO_ANSWER) {
            unanswered(k);
        } else {
            answer(k, BUS_LINK_ANS_NACK, (uint8_t)result);
        }
        break;
    case X_CMD:
        if (result == BUS_LINK_ACK) {
            /* The page's status is not the command's until a read shows
             * its SEQ: the read is next, before any other frame. */
            answer(k, BUS_LINK_ANS_TAKEN, 0);
            k->poll_now = true;
        } else if (result == BUS_LINK_NO_ANSWER) {
            unanswered(k);
        } else if (result == (int)LINK_NACK_BAD_VALUE && !k->reseq) {
            /* The page's SEQ may have moved: read, then once more. */
            k->reseq = true;
            k->framed = false;
            k->sent = false;
            k->poll_now = true;
        } else {
            answer(k, BUS_LINK_ANS_NACK, (uint8_t)result);
            k->poll_now = true;
        }
        break;
    default:
        break;
    }
}

void bus_link_readout(const bus_link_t *k, bus_link_readout_t *r)
{
    if (r == NULL) {
        return;
    }
    memset(r, 0, sizeof(*r));
    if (k == NULL) {
        return;
    }
    r->up = k->up;
    r->page = k->page;
    r->known = k->known;
    r->sending = k->active;
    memcpy(r->status, k->status, sizeof(r->status));
    r->ids_known = k->ids_known;
    memcpy(r->ids, k->ids, sizeof(r->ids));
    r->ans_id = k->ans_id;
    r->ans = k->ans;
    r->ans_nack = k->ans_nack;
    r->ans_seq = k->ans_seq;
    r->answers = k->answers;
}
