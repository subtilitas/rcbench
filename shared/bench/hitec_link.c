/**
 * @file
 * @brief The panel's half of the HITEC link page.
 *
 * See hitec_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hitec_link.h"

#include <stddef.h>
#include <string.h>

/* The exchange in flight. */
enum {
    X_NONE = 0,
    X_STATUS,   /* read 0 to 15 */
    X_STAGE,    /* write a window of 4 to 22 */
    X_CMD,      /* write 0 to 3 */
    X_RUN,      /* read 0 to 31: the capture again and VIEW 2 */
};

/** @brief The staged registers and VIEW go out in 5 windows: 4 whole ones
 *         and one of 3. */
#define STAGE_WINDOWS 5u
/** @brief Registers of a whole window: one CAN frame. */
#define STAGE_W 4u
/** @brief A staged register of @p l by its write offset @p reg. */
#define SG(l, reg) ((l)->staged[(unsigned)(reg) - (unsigned)LINK_HT_W_LINE])
/** @brief Registers 0 to 15: the status and the capture. */
#define STATUS_N ((unsigned)LINK_HT_DATA)
/** @brief Registers 0 to 31: the read of the run, with VIEW and the
 *         capture in the same read. */
#define RUN_N ((unsigned)LINK_HT_COUNT)
/** @brief LINK_HT_STATE's flag bits. */
#define FLAG_MASK 0x0FF8u

_Static_assert(LINK_HT_W_LINE + HITEC_LINK_STAGED - 1u == LINK_HT_W_VIEW,
               "the view closes the staged registers");
_Static_assert((STAGE_WINDOWS - 1u) * STAGE_W + 3u == HITEC_LINK_STAGED,
               "stage windows");
_Static_assert(STATUS_N == 16u && RUN_N == 32u && RUN_N <= LINK_MAX_REGS,
               "the page's halves");
_Static_assert((int)HITEC_DEC_LISTEN == (int)LINK_HITEC_DEC_LISTEN
               && (int)HITEC_DEC_D == (int)LINK_HITEC_DEC_D
               && (int)HITEC_DEC_C == (int)LINK_HITEC_DEC_C,
               "the session's decoders are the page's");

/* --- the request -------------------------------------------------------- */

/**
 * @brief Whether @p op is a transaction, one that leaves a capture.
 *
 * @param op The operation.
 * @return true for SEND, LISTEN, LEVEL and POWER.
 */
static bool op_transaction(uint8_t op)
{
    return op == (uint8_t)LINK_HITEC_OP_SEND
           || op == (uint8_t)LINK_HITEC_OP_LISTEN
           || op == (uint8_t)LINK_HITEC_OP_LEVEL
           || op == (uint8_t)LINK_HITEC_OP_POWER;
}

/**
 * @brief The step's result for the session from how a request ended.
 *
 * @param result link_hitec_result_t the page gave.
 * @param end    Registers 0 to 15 as the request ended.
 * @param out    Receives the result.
 */
static void ses_of(uint8_t result, const uint16_t *end,
                   hitec_ses_result_t *out)
{
    memset(out, 0, sizeof(*out));
    if (result == (uint8_t)LINK_HITEC_RESULT_DONE
        || result == (uint8_t)LINK_HITEC_RESULT_CUT) {
        out->run = result == (uint8_t)LINK_HITEC_RESULT_DONE
                       ? (uint8_t)HITEC_SES_RAN
                       : (uint8_t)HITEC_SES_CUT;
        out->rx = (uint8_t)(end[LINK_HT_DEC] & 0xFFu);
        out->value = end[LINK_HT_VALUE];
    } else {
        out->run = (uint8_t)HITEC_SES_FAILED;
    }
}

/**
 * @brief The capture of a step's end into the outcome.
 *
 * @param l The module.
 * @param o The outcome.
 */
static void capture_of(const hitec_link_t *l, hitec_link_outcome_t *o)
{
    const uint16_t *end = l->end;

    if (!op_transaction(l->op)
        || (end[LINK_HT_STATE] & LINK_HT_F_CAPTURE) == 0u) {
        return;
    }
    o->have_cap = true;
    o->cap = end[LINK_HT_CAP];
    o->answer_id = (uint8_t)(end[LINK_HT_DEC] >> 8);
    o->n = (uint8_t)(end[LINK_HT_CAP] & 0x1Fu);
    if (o->n > HITEC_RX_MAX) {
        o->n = HITEC_RX_MAX;
    }
    for (unsigned k = 0; k < o->n; ++k) {
        o->bytes[k] =
            (uint8_t)(end[LINK_HT_BYTES + k / 2u] >> (8u * (k & 1u)));
    }
}

/**
 * @brief A register of VIEW 2 from a read of registers 16 to 31.
 *
 * @param run The registers read.
 * @param reg The register, LINK_HT_I_*.
 * @return Its value.
 */
static uint16_t run_reg(const uint16_t *run, unsigned reg)
{
    return run[reg - (unsigned)LINK_HT_DATA];
}

/**
 * @brief A 32-bit value of VIEW 2, low half first.
 *
 * @param run The registers read.
 * @param lo  The register of the low half, LINK_HT_I_*.
 * @return The value.
 */
static uint32_t run32(const uint16_t *run, unsigned lo)
{
    return run_reg(run, lo) | ((uint32_t)run_reg(run, lo + 1u) << 16);
}

/**
 * @brief The request on its way ends here, as @p kind.
 *
 * @param l    The module.
 * @param kind The outcome kind.
 * @param nack The NACK reason of the last answer, or 0.
 * @param run  Registers 16 to 31 of the step, or NULL.
 */
static void finish(hitec_link_t *l, hitec_link_outcome_kind_t kind,
                   uint8_t nack, const uint16_t *run)
{
    if (l->active != 0u) {
        hitec_link_outcome_t *o = &l->outcome;

        memset(o, 0, sizeof(*o));
        o->kind = kind;
        o->op = (link_hitec_op_t)l->op;
        o->port = l->port;
        o->id = l->id;
        o->nack = nack;
        o->taken = l->taken != 0u;
        o->ses.run = (uint8_t)HITEC_SES_FAILED;
        if (kind == HITEC_LINK_OUT_REFUSED) {
            o->refusal =
                (link_hitec_refusal_t)(l->end[LINK_HT_RESULT] >> 8);
        } else if (kind == HITEC_LINK_OUT_DONE) {
            /* CLEAR starts no operation: RESULT is the last one's. */
            o->result = l->op == (uint8_t)LINK_HITEC_OP_CLEAR
                            ? (uint8_t)LINK_HITEC_RESULT_DONE
                            : (uint8_t)(l->end[LINK_HT_RESULT] & 0xFFu);
            ses_of(o->result, l->end, &o->ses);
            capture_of(l, o);
            if (run != NULL) {
                o->have_run = true;
                o->delay_ns = run32(run, LINK_HT_I_DELAY_LO);
                o->t_us = run32(run, LINK_HT_I_T_LO);
                o->window_us = run32(run, LINK_HT_I_WINDOW_LO);
                o->framing = run_reg(run, LINK_HT_I_FRAMING);
            }
        }
        l->outcome_new = 1;
    }
    l->active = 0;
    l->verdict = 0;
    l->cmd_doubt = 0;
    if (l->phase != (uint8_t)HITEC_LINK_DOWN
        && l->phase != (uint8_t)HITEC_LINK_NO_PAGE) {
        l->phase = (uint8_t)(l->known != 0u ? HITEC_LINK_READY
                                            : HITEC_LINK_SYNC);
    }
}

void hitec_link_init(hitec_link_t *l)
{
    if (l != NULL) {
        memset(l, 0, sizeof(*l));
    }
}

void hitec_link_lost(hitec_link_t *l)
{
    if (l == NULL) {
        return;
    }
    l->phase = (uint8_t)HITEC_LINK_DOWN;
    finish(l, HITEC_LINK_OUT_NO_ANSWER, 0u, NULL);
    l->up = 0;
    l->page = 0;
    l->minor = 0;
    l->known = 0;
    l->pending = X_NONE;
    l->tries = 0;
    l->polled = 0;
    l->poll_now = 0;
    l->cmd_live = 0;
    l->cmd_abort = 0;
    l->abort_wanted = 0;
    l->ours = 0;
    l->have_line = 0;
}

void hitec_link_came_up(hitec_link_t *l, uint16_t minor)
{
    if (l == NULL) {
        return;
    }
    hitec_link_lost(l);
    l->up = 1;
    l->page = minor >= HITEC_LINK_MINOR ? 1u : 0u;
    l->minor = minor;
    l->phase = (uint8_t)(l->page != 0u ? HITEC_LINK_SYNC
                                       : HITEC_LINK_NO_PAGE);
}

bool hitec_link_has_page(const hitec_link_t *l)
{
    return l != NULL && l->up != 0u && l->page != 0u;
}

/**
 * @brief Whether a request is taken now.
 *
 * @param l The module.
 * @return HITEC_LINK_START_OK when it is, else the reason it is not.
 */
static hitec_link_start_t ready(const hitec_link_t *l)
{
    if (l->up == 0u) {
        return HITEC_LINK_START_DOWN;
    }
    if (l->page == 0u) {
        return HITEC_LINK_START_NO_PAGE;
    }
    /* An exchange still out belongs to the request before: its answer
     * comes first. */
    if (l->phase != (uint8_t)HITEC_LINK_READY || l->pending != X_NONE) {
        return HITEC_LINK_START_BUSY;
    }
    return HITEC_LINK_START_OK;
}

/**
 * @brief Put a request on its way.
 *
 * @param l      The module.
 * @param op     The operation.
 * @param port   The port.
 * @param id     The caller's number of it.
 * @param key    LINK_HT_W_KEY of its command.
 * @param staged true when the staged registers go out first.
 */
static void begin(hitec_link_t *l, link_hitec_op_t op, uint8_t port,
                  uint32_t id, uint16_t key, bool staged)
{
    l->op = (uint8_t)op;
    l->port = port;
    l->id = id;
    l->cmd_proto[LINK_HT_W_CMD] = LINK_HT_W_CMD_OF(op, port);
    l->cmd_proto[LINK_HT_W_SEQ] = 0u;
    l->cmd_proto[LINK_HT_W_KEY] = key;
    l->cmd_proto[LINK_HT_W_CRC] =
        staged ? link_kst_crc(l->staged, LINK_HT_W_STAGED) : 0u;
    l->stage_at = staged ? 0u : (uint8_t)STAGE_WINDOWS;
    l->active = 1;
    l->verdict = 0;
    l->taken = 0;
    l->limit_cut = 0;
    l->cmd_tries = 0;
    l->phase = (uint8_t)HITEC_LINK_SENDING;
}

/**
 * @brief Clear the staged registers and set VIEW to the run.
 *
 * @param l The module.
 */
static void stage_clear(hitec_link_t *l)
{
    memset(l->staged, 0, sizeof(l->staged));
    SG(l, LINK_HT_W_VIEW) = (uint16_t)LINK_HITEC_VIEW_RUN;
}

/**
 * @brief Store a 32-bit value in two staged registers, low half first.
 *
 * @param l  The module.
 * @param lo The write offset of the low half.
 * @param v  The value.
 */
static void stage32(hitec_link_t *l, unsigned lo, uint32_t v)
{
    SG(l, lo) = (uint16_t)(v & 0xFFFFu);
    SG(l, lo + 1u) = (uint16_t)(v >> 16);
}

hitec_link_start_t hitec_link_open(hitec_link_t *l, uint8_t port,
                                   uint8_t family, uint8_t drive,
                                   uint32_t id)
{
    hitec_link_start_t s;

    if (l == NULL) {
        return HITEC_LINK_START_ARG;
    }
    s = ready(l);
    if (s != HITEC_LINK_START_OK) {
        return s;
    }
    if (port >= LINK_PG_PORTS_MAX || family > (uint8_t)HITEC_D
        || drive > (uint8_t)HITEC_DRIVE_PUSH_PULL) {
        return HITEC_LINK_START_ARG;
    }
    stage_clear(l);
    SG(l, LINK_HT_W_LINE) = LINK_HT_LINE_OF(family, drive);
    begin(l, LINK_HITEC_OP_OPEN, port, id, 0u, true);
    return HITEC_LINK_START_OK;
}

/**
 * @brief A request with no staged registers: CLOSE or CLEAR.
 *
 * @param l    The module.
 * @param op   The operation.
 * @param port The port.
 * @param id   The caller's number of it.
 * @return As hitec_link_open().
 */
static hitec_link_start_t bare(hitec_link_t *l, link_hitec_op_t op,
                               uint8_t port, uint32_t id)
{
    hitec_link_start_t s;

    if (l == NULL) {
        return HITEC_LINK_START_ARG;
    }
    s = ready(l);
    if (s != HITEC_LINK_START_OK) {
        return s;
    }
    if (port >= LINK_PG_PORTS_MAX) {
        return HITEC_LINK_START_ARG;
    }
    begin(l, op, port, id, 0u, false);
    return HITEC_LINK_START_OK;
}

hitec_link_start_t hitec_link_close(hitec_link_t *l, uint8_t port,
                                    uint32_t id)
{
    return bare(l, LINK_HITEC_OP_CLOSE, port, id);
}

hitec_link_start_t hitec_link_clear(hitec_link_t *l, uint8_t port,
                                    uint32_t id)
{
    return bare(l, LINK_HITEC_OP_CLEAR, port, id);
}

/**
 * @brief The operation that runs a step, and whether the step is one.
 *
 * @param st The step.
 * @param op Receives the operation.
 * @return true for an XFER or a POWER whose transaction the page can carry.
 */
static bool step_op(const hitec_ses_step_t *st, link_hitec_op_t *op)
{
    const hitec_xfer_t *x = &st->xfer;

    if (!hitec_xfer_valid(x) || st->decoder > (uint8_t)HITEC_DEC_LISTEN) {
        return false;
    }
    if (st->kind == (uint8_t)HITEC_SES_STEP_POWER) {
        *op = LINK_HITEC_OP_POWER;
        return x->kind == (uint8_t)HITEC_XFER_LISTEN;
    }
    if (st->kind != (uint8_t)HITEC_SES_STEP_XFER) {
        return false;
    }
    switch ((hitec_kind_t)x->kind) {
    case HITEC_XFER_SEND:
        *op = LINK_HITEC_OP_SEND;
        return true;
    case HITEC_XFER_LISTEN:
        *op = LINK_HITEC_OP_LISTEN;
        return true;
    case HITEC_XFER_LEVEL:
    default:
        *op = LINK_HITEC_OP_LEVEL;
        return true;
    }
}

hitec_link_start_t hitec_link_step(hitec_link_t *l, uint8_t port,
                                   const hitec_ses_step_t *st, uint16_t mv,
                                   uint16_t ma, uint32_t id)
{
    hitec_link_start_t s;
    link_hitec_op_t op = LINK_HITEC_OP_NONE;
    const hitec_xfer_t *x;

    if (l == NULL || st == NULL) {
        return HITEC_LINK_START_ARG;
    }
    s = ready(l);
    if (s != HITEC_LINK_START_OK) {
        return s;
    }
    if (port >= LINK_PG_PORTS_MAX || !step_op(st, &op)) {
        return HITEC_LINK_START_ARG;
    }
    x = &st->xfer;
    stage_clear(l);
    SG(l, LINK_HT_W_NTX) = (uint16_t)(x->n_tx | ((unsigned)x->gap_bits << 8));
    for (size_t k = 0; k < HITEC_TX_MAX / 2u; ++k) {
        SG(l, LINK_HT_W_TX + k) =
            (uint16_t)(x->tx[2u * k] | ((unsigned)x->tx[2u * k + 1u] << 8));
    }
    stage32(l, LINK_HT_W_LEAD_LO, x->lead_us);
    stage32(l, LINK_HT_W_WINDOW_LO, x->window_us);
    SG(l, LINK_HT_W_RX) =
        (uint16_t)(x->max_rx | (x->answer != 0u ? 0x100u : 0u));
    SG(l, LINK_HT_W_ANSWER) =
        (uint16_t)(x->answer_byte | (x->level != 0u ? 0x100u : 0u));
    stage32(l, LINK_HT_W_LEVEL_LO, x->level_us);
    SG(l, LINK_HT_W_DECODE) =
        (uint16_t)(st->decoder | ((unsigned)st->dec_arg << 8));
    if (op == LINK_HITEC_OP_POWER) {
        SG(l, LINK_HT_W_SUPPLY_MV) = mv;
        SG(l, LINK_HT_W_SUPPLY_MA) = ma;
        SG(l, LINK_HT_W_OFF_MS) = st->off_ms;
    }
    begin(l, op, port, id,
          op == LINK_HITEC_OP_SEND || x->answer != 0u
              ? (uint16_t)LINK_HITEC_WRITE_KEY : 0u,
          true);
    return HITEC_LINK_START_OK;
}

/**
 * @brief The command went and no answer came: it is not sent again.
 *
 * Whether the page took it, the next status read says (on_status()).  That
 * read has HITEC_LINK_RETRIES tries of its own.
 *
 * @param l     The module.
 * @param abort true when an abort asked for it.
 */
static void cmd_in_doubt(hitec_link_t *l, bool abort)
{
    l->cmd_live = 0;
    l->cmd_doubt = 1;
    l->doubt_abort = abort ? 1u : 0u;
    l->doubt_nack = 0;
    l->tries = 0;
}

void hitec_link_abort(hitec_link_t *l)
{
    if (l == NULL || l->up == 0u || l->page == 0u) {
        return;
    }
    if (l->active != 0u && l->phase == (uint8_t)HITEC_LINK_SENDING
        && l->verdict == 0u) {
        if (l->cmd_doubt != 0u) {
            l->doubt_abort = 1;
        } else if (l->cmd_live == 0u || l->cmd_abort != 0u) {
            /* The command has not gone: nothing of the request reaches
             * the page.  A live command here is an ABORT before it. */
            finish(l, HITEC_LINK_OUT_DROPPED, 0u, NULL);
        } else if (l->pending == X_NONE) {
            cmd_in_doubt(l, true);
        }
        /* On its way: hitec_link_done() decides with its answer. */
    }
    l->abort_wanted = 1;
}

/* --- the exchanges ------------------------------------------------------ */

/**
 * @brief Sets up one exchange with the page.
 *
 * @param l     The module.
 * @param x     Receives the exchange.
 * @param kind  The exchange kind.
 * @param write true for a write.
 * @param off   The first register.
 * @param n     The number of registers.
 * @param regs  The registers to write, or NULL for a read.
 * @return Always true.
 */
static bool ask(hitec_link_t *l, hitec_link_xfer_t *x, uint8_t kind,
                bool write, unsigned off, unsigned n, const uint16_t *regs)
{
    memset(x, 0, sizeof(*x));
    x->write = write;
    x->page = (uint8_t)LINK_PAGE_HITEC;
    x->off = (uint8_t)off;
    x->n = (uint8_t)n;
    if (regs != NULL) {
        memcpy(x->regs, regs, n * sizeof(regs[0]));
    }
    l->pending = kind;
    return true;
}

/**
 * @brief Sets up a read of registers 0 to 15.
 *
 * @param l The module.
 * @param x Receives the exchange.
 * @return Always true.
 */
static bool ask_status(hitec_link_t *l, hitec_link_xfer_t *x)
{
    l->polled = 1;
    l->poll_now = 0;
    l->status_ms = l->now_ms;
    return ask(l, x, X_STATUS, false, LINK_HT_STATE, STATUS_N, NULL);
}

/**
 * @brief Whether a periodic read is due.
 *
 * @param l      The module.
 * @param period The period, ms.
 * @return true when no read was made yet or @p period has passed since the
 *         last one.
 */
static bool due(const hitec_link_t *l, uint32_t period)
{
    return l->polled == 0u || (uint32_t)(l->now_ms - l->status_ms) >= period;
}

/**
 * @brief Sets up the write of the command on its way.
 *
 * @param l The module.
 * @param x Receives the exchange.
 * @return Always true.
 */
static bool ask_cmd(hitec_link_t *l, hitec_link_xfer_t *x)
{
    if (l->cmd_abort == 0u) {
        l->cmd_tries++;
    }
    return ask(l, x, X_CMD, true, LINK_HT_W_CMD, LINK_HT_W_CMD_FRAME,
               l->cmd);
}

/**
 * @brief The next exchange of a request on its way out or being fetched.
 *
 * @param l The module.
 * @param x Receives the exchange.
 * @return true when one is owed.
 */
static bool next_request(hitec_link_t *l, hitec_link_xfer_t *x)
{
    if (l->phase == (uint8_t)HITEC_LINK_SENDING) {
        if (l->stage_at < STAGE_WINDOWS) {
            const unsigned at = STAGE_W * l->stage_at;
            const unsigned n = at + STAGE_W <= HITEC_LINK_STAGED
                                   ? STAGE_W : HITEC_LINK_STAGED - at;

            return ask(l, x, X_STAGE, true, LINK_HT_W_LINE + at, n,
                       &l->staged[at]);
        }
        memcpy(l->cmd, l->cmd_proto, sizeof(l->cmd));
        l->cmd[LINK_HT_W_SEQ] = (uint16_t)(l->seq + 1u);
        l->cmd_live = 1;
        return ask_cmd(l, x);
    }
    if (l->phase == (uint8_t)HITEC_LINK_FETCHING) {
        return ask(l, x, X_RUN, false, LINK_HT_STATE, RUN_N, NULL);
    }
    /* HITEC_LINK_RUNNING */
    return due(l, HITEC_LINK_POLL_MS) && ask_status(l, x);
}

bool hitec_link_next(hitec_link_t *l, uint32_t now_ms, hitec_link_xfer_t *x)
{
    bool held;

    if (l == NULL || x == NULL || l->up == 0u || l->page == 0u
        || l->pending != X_NONE) {
        return false;
    }
    l->now_ms = now_ms;
    if (l->known == 0u) {
        return due(l, HITEC_LINK_POLL_MS) && ask_status(l, x);
    }
    /* A command without an answer goes again as it was, unless an abort
     * came for it (cmd_doubt). */
    if (l->cmd_live != 0u) {
        return ask_cmd(l, x);
    }
    if (l->verdict != 0u || l->poll_now != 0u || l->cmd_doubt != 0u) {
        return ask_status(l, x);
    }
    if (l->phase == (uint8_t)HITEC_LINK_RUNNING && l->limit_cut == 0u
        && (uint32_t)(now_ms - l->taken_ms) >= HITEC_LINK_RUN_MAX_MS) {
        l->limit_cut = 1;
        l->abort_wanted = 1;
    }
    if (l->abort_wanted != 0u) {
        l->cmd[LINK_HT_W_CMD] = LINK_HT_W_CMD_OF(
            LINK_HITEC_OP_ABORT, LINK_HT_PORT_OF(l->status[LINK_HT_STATE]));
        l->cmd[LINK_HT_W_SEQ] = (uint16_t)(l->seq + 1u);
        l->cmd[LINK_HT_W_KEY] = 0u;
        l->cmd[LINK_HT_W_CRC] = 0u;
        l->cmd_live = 1;
        l->cmd_abort = 1;
        return ask_cmd(l, x);
    }
    if (l->active != 0u) {
        return next_request(l, x);
    }
    held = LINK_HT_STATE_OF(l->status[LINK_HT_STATE])
           != (uint8_t)LINK_HITEC_FREE;
    return due(l, held ? HITEC_LINK_HELD_MS : HITEC_LINK_IDLE_MS)
           && ask_status(l, x);
}

/**
 * @brief Whether the page @p regs is in a state the request's command leaves,
 *        against the status read before it (l->status).
 *
 * The SEQ alone does not name the writer: another writer's command at the
 * same SEQ, ahead of a try of this one, leaves it too.  A refusal fits: the
 * outcome is REFUSED and claims no run.  CLOSE leaves the port free or
 * another port held; CLEAR leaves the port held with STOPPED clear.  An
 * operation that starts names itself in LINK_HT_OP on the request's port
 * and changes the page: the operation, the transaction count, the state or
 * the result differ from the read before.  A transaction the driver
 * refused that repeats the operation and result before it does not fit;
 * the request ends FAILED either way.
 *
 * @param l    The module.
 * @param regs Registers 0 to 15 as read.
 * @return true when the state fits the request's command.
 */
static bool page_fits_cmd(const hitec_link_t *l, const uint16_t *regs)
{
    const uint16_t *was = l->status;
    const uint8_t st = LINK_HT_STATE_OF(regs[LINK_HT_STATE]);
    const bool here = st != (uint8_t)LINK_HITEC_FREE
                      && LINK_HT_PORT_OF(regs[LINK_HT_STATE]) == l->port;

    if ((regs[LINK_HT_RESULT] >> 8) != 0u) {
        return true;
    }
    if (l->op == (uint8_t)LINK_HITEC_OP_CLOSE) {
        return !here;
    }
    if (l->op == (uint8_t)LINK_HITEC_OP_CLEAR) {
        return here && (regs[LINK_HT_STATE] & LINK_HT_F_STOPPED) == 0u;
    }
    if (LINK_HT_OP_OF(regs[LINK_HT_OP]) != l->op
        || LINK_HT_PORT_OF(regs[LINK_HT_STATE]) != l->port) {
        return false;
    }
    return regs[LINK_HT_OP] != was[LINK_HT_OP]
           || st != LINK_HT_STATE_OF(was[LINK_HT_STATE])
           || (regs[LINK_HT_RESULT] & 0xFFu) != (was[LINK_HT_RESULT] & 0xFFu);
}

/**
 * @brief Whether the page @p regs took the command that was not sent again.
 *
 * A command taken leaves its SEQ.  A page that started again reads SEQ 0,
 * operation NONE and no refusal: at SEQ 0, the command after 0xFFFF, the
 * SEQ alone does not tell the two apart, and such a page counts as one that
 * started again.
 *
 * @param l    The module.
 * @param regs Registers 0 to 15 as read.
 * @return true when the page shows the command's SEQ, is no fresh page
 *         and is in a state the command leaves (page_fits_cmd()).
 */
static bool page_took_cmd(const hitec_link_t *l, const uint16_t *regs)
{
    return regs[LINK_HT_SEQ] == l->cmd[LINK_HT_W_SEQ]
           && (regs[LINK_HT_SEQ] != 0u || (regs[LINK_HT_RESULT] >> 8) != 0u
               || LINK_HT_OP_OF(regs[LINK_HT_OP])
                      != (uint8_t)LINK_HITEC_OP_NONE)
           && page_fits_cmd(l, regs);
}

/**
 * @brief Whether the page @p regs started again since the status last read:
 *        it names no operation after a read that named one.
 *
 * @param l    The module.
 * @param regs Registers 0 to 15 as read.
 * @return true when LINK_HT_OP went back to NONE.
 */
static bool op_went_back(const hitec_link_t *l, const uint16_t *regs)
{
    return l->known != 0u
           && LINK_HT_OP_OF(l->status[LINK_HT_OP])
                  != (uint8_t)LINK_HITEC_OP_NONE
           && LINK_HT_OP_OF(regs[LINK_HT_OP]) == (uint8_t)LINK_HITEC_OP_NONE;
}

/**
 * @brief A free port in the read @p regs is no longer this module's.
 *
 * Another port held, or the same one taken again, comes with another SEQ,
 * which on_status() takes as a restart.
 *
 * @param l    The module.
 * @param regs Registers 0 to 15 as read.
 */
static void port_look(hitec_link_t *l, const uint16_t *regs)
{
    if (LINK_HT_STATE_OF(regs[LINK_HT_STATE]) == (uint8_t)LINK_HITEC_FREE) {
        l->ours = 0;
        l->have_line = 0;
    }
}

/**
 * @brief A request that runs: whether the read @p regs ends it, and how.
 *
 * @param l    The module.
 * @param regs Registers 0 to 15 as read.
 */
static void on_running(hitec_link_t *l, const uint16_t *regs)
{
    const uint8_t st = LINK_HT_STATE_OF(regs[LINK_HT_STATE]);
    const bool open = l->op == (uint8_t)LINK_HITEC_OP_OPEN;
    bool run;

    if (open ? st == (uint8_t)LINK_HITEC_STOPPING
             : st == (uint8_t)LINK_HITEC_BUSY
                   || st == (uint8_t)LINK_HITEC_POWER_OFF) {
        return;
    }
    memcpy(l->end, regs, sizeof(l->end));
    if (open && st == (uint8_t)LINK_HITEC_OPEN
        && LINK_HT_PORT_OF(regs[LINK_HT_STATE]) == l->port) {
        l->ours = 1;
    }
    run = regs[LINK_HT_VIEW] == (uint16_t)LINK_HITEC_VIEW_RUN
          && (open ? st == (uint8_t)LINK_HITEC_OPEN
                   : (regs[LINK_HT_STATE] & LINK_HT_F_CAPTURE) != 0u);
    if (run) {
        l->phase = (uint8_t)HITEC_LINK_FETCHING;
    } else {
        finish(l, HITEC_LINK_OUT_DONE, 0u, NULL);
    }
}

/**
 * @brief The command's verdict: refused, done at once, or running.
 *
 * @param l    The module.
 * @param regs Registers 0 to 15 as read.
 */
static void on_verdict(hitec_link_t *l, const uint16_t *regs)
{
    l->verdict = 0;
    memcpy(l->end, regs, sizeof(l->end));
    if ((regs[LINK_HT_RESULT] >> 8) != 0u) {
        finish(l, HITEC_LINK_OUT_REFUSED, 0u, NULL);
        return;
    }
    if (l->op == (uint8_t)LINK_HITEC_OP_CLOSE
        || l->op == (uint8_t)LINK_HITEC_OP_CLEAR) {
        finish(l, HITEC_LINK_OUT_DONE, 0u, NULL);
        return;
    }
    l->phase = (uint8_t)HITEC_LINK_RUNNING;
    on_running(l, regs);
}

/**
 * @brief Takes a read of registers 0 to 15 into the state.
 *
 * @param l    The module.
 * @param regs Registers 0 to 15 as read.
 */
static void on_status(hitec_link_t *l, const uint16_t *regs)
{
    const bool went_back = op_went_back(l, regs);
    bool restarted = l->known != 0u
                     && (regs[LINK_HT_SEQ] != l->seq || went_back);
    bool not_taken = false;

    if (l->cmd_doubt != 0u) {
        /* The page says whether it took the command that was not sent
         * again: its SEQ, taken; the SEQ before it, not.  Any other page
         * started again or heard another writer. */
        l->cmd_doubt = 0;
        if (!went_back && page_took_cmd(l, regs)) {
            restarted = false;
            l->verdict = 1;
            l->taken = 1;
            l->taken_ms = l->now_ms;
        } else if (!restarted) {
            not_taken = true;
        }
    }
    memcpy(l->status, regs, sizeof(l->status));
    l->seq = regs[LINK_HT_SEQ];
    l->known = 1;
    l->held_last = LINK_HT_STATE_OF(regs[LINK_HT_STATE])
                           != (uint8_t)LINK_HITEC_FREE
                       ? 1u
                       : 0u;
    port_look(l, regs);
    if (l->phase == (uint8_t)HITEC_LINK_SYNC) {
        l->phase = (uint8_t)HITEC_LINK_READY;
    }
    if (restarted) {
        /* Not the SEQ this module left there: nothing it knows of the
         * page holds, the port included. */
        l->ours = 0;
        l->have_line = 0;
        finish(l, HITEC_LINK_OUT_RESTARTED, 0u, NULL);
        return;
    }
    if (not_taken) {
        finish(l, l->doubt_abort != 0u ? HITEC_LINK_OUT_DROPPED
                  : l->doubt_nack != 0u ? HITEC_LINK_OUT_NACK
                                        : HITEC_LINK_OUT_NO_ANSWER,
               l->doubt_nack, NULL);
        return;
    }
    if (l->active == 0u) {
        return;
    }
    if (l->verdict != 0u) {
        on_verdict(l, regs);
    } else if (l->phase == (uint8_t)HITEC_LINK_RUNNING) {
        on_running(l, regs);
    }
}

/**
 * @brief Handles an exchange the page refused.
 *
 * @param l    The module.
 * @param kind The exchange kind.
 * @param nack The NACK reason.
 */
static void on_refused(hitec_link_t *l, uint8_t kind, uint8_t nack)
{
    switch (kind) {
    case X_STATUS:
        /* BAD_PAGE: an IOmcu that reports the minor and has no page.
         * hitec_link_done() takes every other refusal of a status read as
         * an exchange without an answer. */
        l->page = 0;
        l->known = 0;
        finish(l, HITEC_LINK_OUT_NACK, nack, NULL);
        l->phase = (uint8_t)HITEC_LINK_NO_PAGE;
        break;
    case X_CMD:
        l->cmd_live = 0;
        if (l->cmd_abort != 0u) {
            /* The page is read before anything else goes; an abort
             * refused with BAD_VALUE goes again after the read, with the
             * page's SEQ. */
            l->cmd_abort = 0;
            l->poll_now = 1;
            if (nack != (uint8_t)LINK_NACK_BAD_VALUE) {
                l->abort_wanted = 0;
            }
            if (l->active == 0u) {
                l->phase = (uint8_t)HITEC_LINK_SYNC;
            }
        } else if (l->cmd_tries > 1u) {
            /* An earlier try went without an answer and may have been
             * taken: the page's SEQ says. */
            cmd_in_doubt(l, false);
            l->doubt_nack = nack;
        } else {
            /* The SEQ is not the page's: read it again. */
            l->known = 0;
            finish(l, HITEC_LINK_OUT_NACK, nack, NULL);
        }
        break;
    case X_STAGE:
    case X_RUN:
    default:
        finish(l, HITEC_LINK_OUT_NACK, nack, NULL);
        break;
    }
}

/**
 * @brief An exchange went without an answer.
 *
 * @param l    The module.
 * @param kind The exchange kind.
 */
static void on_silence(hitec_link_t *l, uint8_t kind)
{
    if (kind == X_CMD && l->cmd_abort == 0u && l->active != 0u) {
        /* The request's command: sent again as it was, unless an abort
         * came; after the last try the page's SEQ tells. */
        if (l->abort_wanted != 0u) {
            cmd_in_doubt(l, true);
        } else if (++l->tries >= HITEC_LINK_RETRIES) {
            cmd_in_doubt(l, false);
        }
        return;
    }
    if (++l->tries >= HITEC_LINK_RETRIES) {
        /* Given up: what the page holds is read again before anything
         * else is sent. */
        l->tries = 0;
        l->known = 0;
        l->cmd_live = 0;
        l->cmd_abort = 0;
        finish(l, HITEC_LINK_OUT_NO_ANSWER, 0u, NULL);
    }
}

/**
 * @brief Takes the read of registers 0 to 31 that ends a request.
 *
 * VIEW is written without a command, so another writer can change it
 * without a new SEQ.  Registers 16 to 31 are the run's when the same read
 * shows VIEW at LINK_HITEC_VIEW_RUN and the SEQ, the operation with its
 * count and the capture of the read that ended the request; otherwise the
 * outcome carries no run and the line read before stays.  The step's
 * result is that of the read that ended the request either way.
 *
 * @param l    The module.
 * @param regs Registers 0 to 31 as read.
 */
static void on_run(hitec_link_t *l, const uint16_t *regs)
{
    const uint16_t *run = &regs[LINK_HT_DATA];
    const bool same =
        regs[LINK_HT_VIEW] == (uint16_t)LINK_HITEC_VIEW_RUN
        && regs[LINK_HT_SEQ] == l->end[LINK_HT_SEQ]
        && regs[LINK_HT_OP] == l->end[LINK_HT_OP]
        && regs[LINK_HT_CAP] == l->end[LINK_HT_CAP];

    /* Only a lost link ends a request in FETCHING, and it drops the
     * exchange that is out: this read is the request's. */
    if (same) {
        l->line[0] = run_reg(run, LINK_HT_I_BIT_NS);
        l->line[1] = run_reg(run, LINK_HT_I_LINE);
        l->line[2] = run_reg(run, LINK_HT_I_PIN);
        l->have_line = 1;
    }
    finish(l, HITEC_LINK_OUT_DONE, 0u, same ? run : NULL);
}

void hitec_link_done(hitec_link_t *l, int result, const uint16_t *regs)
{
    uint8_t kind;
    bool reads;

    if (l == NULL || l->pending == X_NONE) {
        return;
    }
    kind = l->pending;
    l->pending = X_NONE;
    reads = kind == X_STATUS || kind == X_RUN;
    if (result == HITEC_LINK_NO_ANSWER
        || (result == HITEC_LINK_ACK && reads && regs == NULL)
        || (kind == X_STATUS && result != HITEC_LINK_ACK
            && result != (int)LINK_NACK_BAD_PAGE)) {
        /* A status read refused for another reason than "no such page"
         * says nothing about the page: it is asked again. */
        on_silence(l, kind);
        return;
    }
    l->tries = 0;
    if (result != HITEC_LINK_ACK) {
        on_refused(l, kind, (uint8_t)result);
        return;
    }
    switch (kind) {
    case X_STATUS:
        on_status(l, regs);
        break;
    case X_STAGE:
        /* The answer of a window of a request dropped while it was out
         * changes nothing. */
        if (l->active != 0u) {
            l->stage_at++;
        }
        break;
    case X_CMD:
        l->seq = l->cmd[LINK_HT_W_SEQ];
        l->cmd_live = 0;
        if (l->cmd_abort != 0u) {
            l->cmd_abort = 0;
            l->abort_wanted = 0;
            l->poll_now = 1;
        } else {
            l->verdict = 1;
            l->taken = 1;
            l->taken_ms = l->now_ms;
        }
        break;
    case X_RUN:
    default:
        on_run(l, regs);
        break;
    }
}

unsigned hitec_link_service(hitec_link_t *l, uint32_t now_ms,
                            hitec_link_xchg_fn xchg, void *ctx)
{
    unsigned made = 0;

    if (l == NULL || xchg == NULL) {
        return 0u;
    }
    while (made < HITEC_LINK_PASS_MAX) {
        hitec_link_xfer_t x;
        uint16_t regs[LINK_HT_COUNT];
        int r;

        if (!hitec_link_next(l, now_ms, &x)) {
            break;
        }
        made++;
        memset(regs, 0, sizeof(regs));
        r = xchg(ctx, &x, x.write ? NULL : regs);
        hitec_link_done(l, r, !x.write && r == HITEC_LINK_ACK ? regs : NULL);
        if (r == HITEC_LINK_NO_ANSWER) {
            break;
        }
    }
    return made;
}

/* --- what the screen reads ---------------------------------------------- */

hitec_link_phase_t hitec_link_phase(const hitec_link_t *l)
{
    return l != NULL ? (hitec_link_phase_t)l->phase : HITEC_LINK_DOWN;
}

bool hitec_link_outcome(hitec_link_t *l, hitec_link_outcome_t *out)
{
    if (l == NULL || out == NULL || l->outcome_new == 0u) {
        return false;
    }
    *out = l->outcome;
    l->outcome_new = 0;
    return true;
}

void hitec_link_readout(const hitec_link_t *l, hitec_link_readout_t *r)
{
    if (r == NULL) {
        return;
    }
    memset(r, 0, sizeof(*r));
    r->pin = HITEC_LINK_NO_PIN;
    if (l == NULL) {
        return;
    }
    r->phase = (hitec_link_phase_t)l->phase;
    r->minor = l->minor;
    r->hold_supply = l->up != 0u && l->minor >= LINK_MINOR_HITEC_HOLD;
    r->held_last = l->held_last != 0u;
    if (l->known != 0u) {
        const uint16_t st = l->status[LINK_HT_STATE];

        r->known = true;
        r->state = (link_hitec_state_t)LINK_HT_STATE_OF(st);
        r->port_no = LINK_HT_PORT_OF(st);
        r->flags = (uint16_t)(st & FLAG_MASK);
        r->held = r->state != LINK_HITEC_FREE;
        r->stopped = (st & LINK_HT_F_STOPPED) != 0u;
        r->ours = l->ours != 0u;
        r->seq = l->seq;
        r->op = (link_hitec_op_t)LINK_HT_OP_OF(l->status[LINK_HT_OP]);
        r->count = (uint16_t)(l->status[LINK_HT_OP] & 0x0FFFu);
    }
    if (l->have_line != 0u) {
        r->have_line = true;
        r->bit_ns = l->line[0];
        r->line = l->line[1];
        r->pin = l->line[2];
    }
}
