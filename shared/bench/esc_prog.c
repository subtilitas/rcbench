/**
 * @file
 * @brief The programmer for an ESC over its signal wire.  See esc_prog.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_prog.h"

#include <string.h>

#include "esc_prog_int.h"
#include "ew_session.h"

void esc_prog_gate_init(esc_prog_gate_t *g)
{
    atomic_store(&g->pass, 0u);
}

bool esc_prog_gate_claim(esc_prog_gate_t *g, uint32_t id)
{
    uint32_t want = id;

    return id != 0u && atomic_compare_exchange_strong(&g->pass, &want, 0u);
}

void esc_prog_init(esc_prog_t *p)
{
    if (p != NULL) {
        memset(p, 0, sizeof(*p));
        p->next_id = 1u;
    }
}

void esc_prog_gate_set(esc_prog_t *p, esc_prog_gate_t *g)
{
    if (p != NULL) {
        p->gate = g;
    }
}

esc_prog_msg_t esc_prog_say(esc_prog_t *p, esc_prog_msg_t m, uint16_t detail)
{
    p->msg = m;
    p->detail = detail;
    return m;
}

esc_prog_cmd_t *esc_prog_issue(esc_prog_t *p, esc_prog_cmd_kind_t kind)
{
    esc_prog_cmd_t *c = &p->out;

    memset(c, 0, sizeof(*c));
    c->kind = (uint8_t)kind;
    c->id = p->next_id++;
    if (p->next_id == 0u) {
        p->next_id = 1u;
    }
    p->wait_id = c->id;
    p->wait_ms = p->f.now_ms;
    p->out_new = 1u;
    p->aborted = 0u;
    if (p->gate != NULL) {
        atomic_store(&p->gate->pass, c->id);
    }
    return c;
}

/**
 * @brief The gate of everything that goes to the wire.
 * @param p the model
 * @return ESC_PROG_MSG_NONE, or why not
 */
static esc_prog_msg_t common_gate(const esc_prog_t *p)
{
    const esc_link_phase_t ph = p->f.rd.phase;

    if (p->f.armed) {
        return ESC_PROG_MSG_ARMED;
    }
    if (p->f.stopped) {
        return ESC_PROG_MSG_STOPPED;
    }
    if (ph == ESC_LINK_DOWN) {
        return ESC_PROG_MSG_NO_LINK;
    }
    if (ph == ESC_LINK_NO_PAGE) {
        return ESC_PROG_MSG_NO_PAGE;
    }
    if (ph != ESC_LINK_READY || p->wait_id != 0u || p->flow != ESC_PROG_FLOW_NONE) {
        return ESC_PROG_MSG_BUSY;
    }
    return ESC_PROG_MSG_NONE;
}

/**
 * @brief The gate of an operation on an ESC that answered.
 * @param p the model
 * @return ESC_PROG_MSG_NONE, or why not
 */
static esc_prog_msg_t mode_gate(const esc_prog_t *p)
{
    const esc_prog_msg_t m = common_gate(p);

    if (m != ESC_PROG_MSG_NONE) {
        return m;
    }
    return p->f.rd.state == (uint8_t)LINK_ESC_READY ? ESC_PROG_MSG_NONE
                                                    : ESC_PROG_MSG_NOT_IN_MODE;
}

/**
 * @brief The step after an OPEN or ENTER that ended well.
 * @param p the model
 * @param o the outcome
 */
static void connect_goes_on(esc_prog_t *p, const esc_link_outcome_t *o)
{
    /* A stop between two steps ends the flow: no entry starts under it. */
    if (p->f.stopped || p->f.armed) {
        p->flow = ESC_PROG_FLOW_NONE;
        (void)esc_prog_say(p, p->f.stopped ? ESC_PROG_MSG_STOPPED
                                           : ESC_PROG_MSG_ARMED, 0u);
        return;
    }
    if (o->op == (uint8_t)LINK_ESC_OP_OPEN) {
        esc_prog_issue(p, ESC_PROG_CMD_ENTER)->power = p->conn.power;
    } else if (o->state == (uint8_t)LINK_ESC_HAND_WAIT) {
        p->flow = ESC_PROG_FLOW_NONE;
        (void)esc_prog_say(p, ESC_PROG_MSG_HAND_WAIT, 0u);
    } else if (o->op == (uint8_t)LINK_ESC_OP_READ) {
        p->flow = ESC_PROG_FLOW_NONE;
        (void)esc_prog_say(p, ESC_PROG_MSG_READ_DONE, 0u);
    } else {
        (void)esc_prog_issue(p, ESC_PROG_CMD_READ);
    }
}

/**
 * @brief An operation that ended, DONE, in what it was asked for.
 * @param p the model
 * @param o the outcome
 */
static void ended_well(esc_prog_t *p, const esc_link_outcome_t *o)
{
    switch (p->flow) {
    case ESC_PROG_FLOW_CONNECT:
        connect_goes_on(p, o);
        return;
    case ESC_PROG_FLOW_LEAVE:
        if (o->op == (uint8_t)LINK_ESC_OP_LEAVE) {
            (void)esc_prog_issue(p, ESC_PROG_CMD_CLOSE);
            return;
        }
        p->flow = ESC_PROG_FLOW_NONE;
        (void)esc_prog_say(p, ESC_PROG_MSG_LEFT, 0u);
        return;
    default:
        p->flow = ESC_PROG_FLOW_NONE;
        break;
    }
    switch (o->op) {
    case LINK_ESC_OP_READ:
        (void)esc_prog_say(p, ESC_PROG_MSG_READ_DONE, 0u);
        break;
    case LINK_ESC_OP_WRITE:
        (void)esc_prog_say(p, ESC_PROG_MSG_WRITE_DONE, 0u);
        break;
    case LINK_ESC_OP_RESTORE:
        (void)esc_prog_say(p, ESC_PROG_MSG_RESTORE_DONE, 0u);
        break;
    case LINK_ESC_OP_CLEAR:
        (void)esc_prog_say(p, ESC_PROG_MSG_CLEARED, 0u);
        break;
    case LINK_ESC_OP_CLOSE:
    case LINK_ESC_OP_LEAVE:
        (void)esc_prog_say(p, ESC_PROG_MSG_LEFT, 0u);
        break;
    default:
        break;
    }
}

/**
 * @brief What a result says.
 * @param p      the model
 * @param result a link_esc_result_t
 * @param ses    the session's last result
 * @return true for a result that ended well: nothing was said
 */
static bool say_result(esc_prog_t *p, uint8_t result, uint8_t ses)
{
    switch (result) {
    case LINK_ESC_RESULT_NO_LINE:
        (void)esc_prog_say(p, ESC_PROG_MSG_NO_LINE, 0u);
        return false;
    case LINK_ESC_RESULT_SUPPLY:
        (void)esc_prog_say(p, ESC_PROG_MSG_SUPPLY, 0u);
        return false;
    case LINK_ESC_RESULT_TIMEOUT:
        (void)esc_prog_say(p, ESC_PROG_MSG_TIMEOUT, 0u);
        return false;
    case LINK_ESC_RESULT_CUT:
        (void)esc_prog_say(p, ESC_PROG_MSG_CUT, 0u);
        return false;
    case LINK_ESC_RESULT_SESSION:
        (void)esc_prog_say(p, ESC_PROG_MSG_SESSION, ses);
        return false;
    case LINK_ESC_RESULT_NOT_BACK:
        (void)esc_prog_say(p, ESC_PROG_MSG_NOT_BACK, 0u);
        return false;
    default:
        return true;
    }
}

/**
 * @brief An outcome.
 * @param p the model
 * @param o the outcome
 */
static void take_outcome(esc_prog_t *p, const esc_link_outcome_t *o)
{
    bool well = false;

    switch (o->kind) {
    case ESC_LINK_OUT_DONE:
        /* CLEAR moves no result: what stands there is what it cleared. */
        well = o->op == (uint8_t)LINK_ESC_OP_CLEAR
               || say_result(p, o->result, o->ses);
        break;
    case ESC_LINK_OUT_REFUSED:
        (void)esc_prog_say(p, ESC_PROG_MSG_REFUSED, o->refusal);
        break;
    case ESC_LINK_OUT_NACK:
        (void)esc_prog_say(p, ESC_PROG_MSG_NACK, o->nack);
        break;
    case ESC_LINK_OUT_NO_ANSWER:
        (void)esc_prog_say(p, ESC_PROG_MSG_NO_ANSWER, 0u);
        break;
    case ESC_LINK_OUT_RESTARTED:
        (void)esc_prog_say(p, ESC_PROG_MSG_RESTARTED, 0u);
        break;
    case ESC_LINK_OUT_DROPPED:
        (void)esc_prog_say(p, ESC_PROG_MSG_DROPPED, 0u);
        break;
    default:
        break;
    }
    /* What an ABORT cut is that ABORT's: a stop's word would name the wrong hand. */
    if (p->aborted != 0u) {
        p->aborted = 0u;
        if (p->msg == ESC_PROG_MSG_CUT || p->msg == ESC_PROG_MSG_DROPPED
            || (p->msg == ESC_PROG_MSG_SESSION
                && (p->detail == (uint16_t)EW_SES_ERR_ABORTED
                    || p->detail == (uint16_t)EW_SES_ERR_STOPPED))) {
            (void)esc_prog_say(p, ESC_PROG_MSG_ABORTED, 0u);
        }
    }
    if (well) {
        ended_well(p, o);
    } else {
        p->flow = ESC_PROG_FLOW_NONE;
    }
}

/**
 * @brief A stop or fault that came with no request running: a hand wait run out,
 *        a stop edge, a supply gone.  The page's result says which.
 * @param p the model
 */
static void watch_flags(esc_prog_t *p)
{
    const esc_link_readout_t *rd = &p->f.rd;
    const uint16_t bits = (uint16_t)(LINK_EW_F_FAULT | LINK_EW_F_STOPPED);
    const uint16_t now = rd->known ? (uint16_t)(rd->flags & bits) : 0u;
    const bool rose = (now & (uint16_t)~p->flags_seen) != 0u;

    p->flags_seen = now;
    if (rose && p->flow == ESC_PROG_FLOW_NONE && p->wait_id == 0u
        && rd->phase == ESC_LINK_READY) {
        (void)say_result(p, rd->result, rd->ses);
    }
}

/**
 * @brief Whether a message gives way to the link's state.  WRITE_DONE and RESTORE_DONE
 *        do not: they are the only word of a finished write, and an error says what
 *        ended a flow.
 * @param m The message.
 * @return true then.
 */
static bool msg_is_good_word(esc_prog_msg_t m)
{
    return m == ESC_PROG_MSG_NONE || m == ESC_PROG_MSG_READ_DONE
           || m == ESC_PROG_MSG_LEFT || m == ESC_PROG_MSG_CLEARED
           || m == ESC_PROG_MSG_LOADED || m == ESC_PROG_MSG_HAND_WAIT
           || m == ESC_PROG_MSG_ABORTED;
}

void esc_prog_feed(esc_prog_t *p, const esc_prog_feed_t *f)
{
    if (p == NULL || f == NULL) {
        return;
    }
    p->f = *f;
    /* What the link said to the command out. */
    if (p->wait_id != 0u) {
        if (f->answer_id == p->wait_id) {
            p->wait_id = 0u;
            if (f->answer != (uint8_t)ESC_LINK_START_OK) {
                p->flow = ESC_PROG_FLOW_NONE;
                (void)esc_prog_say(p, f->answer == (uint8_t)ESC_LINK_START_DOWN
                                 ? ESC_PROG_MSG_NO_LINK
                             : f->answer == (uint8_t)ESC_LINK_START_NO_PAGE
                                 ? ESC_PROG_MSG_NO_PAGE
                                 : ESC_PROG_MSG_BUSY, 0u);
            }
        } else if ((uint32_t)(f->now_ms - p->wait_ms) >= ESC_PROG_CMD_WAIT_MS) {
            /* Never reached the link's task.  The pass is taken back, so it
             * is not carried out later. */
            const uint32_t id = p->wait_id;

            if (p->gate == NULL || esc_prog_gate_claim(p->gate, id)) {
                p->wait_id = 0u;
                p->flow = ESC_PROG_FLOW_NONE;
                p->out_new = 0u;
                (void)esc_prog_say(p, ESC_PROG_MSG_NO_ANSWER, 0u);
            }
        }
    }
    if (f->outcomes != p->outcomes_seen) {
        p->outcomes_seen = f->outcomes;
        if (p->wait_id == 0u) {
            take_outcome(p, &f->outcome);
        }
    }
    watch_flags(p);
    /* The link's state stands in for a good word while it is gone, and goes when it
     * is back.  An error that said what ended a flow stays. */
    if ((f->rd.phase == ESC_LINK_DOWN || f->rd.phase == ESC_LINK_NO_PAGE)
        && msg_is_good_word(p->msg)) {
        (void)esc_prog_say(p, f->rd.phase == ESC_LINK_DOWN ? ESC_PROG_MSG_NO_LINK
                                                           : ESC_PROG_MSG_NO_PAGE, 0u);
    } else if (f->rd.phase == ESC_LINK_READY
               && (p->msg == ESC_PROG_MSG_NO_LINK || p->msg == ESC_PROG_MSG_NO_PAGE)) {
        (void)esc_prog_say(p, ESC_PROG_MSG_NONE, 0u);
    }
    esc_prog_backup_follow(p);
}

bool esc_prog_next_cmd(esc_prog_t *p, esc_prog_cmd_t *out)
{
    if (p == NULL || out == NULL || !p->out_new) {
        return false;
    }
    *out = p->out;
    p->out_new = 0u;
    return true;
}

/**
 * @brief Whether a SWITCHED power's voltage or limit is out of range, the module's or
 *        SUPPLY's for this board and setting.
 * @param p The model.
 * @param w The power.
 * @return true then.
 */
static bool supply_out_of_range(const esc_prog_t *p, const esc_link_power_t *w)
{
    return w->power == (uint8_t)LINK_ESC_SWITCHED
           && (w->mv < LINK_ESC_SUPPLY_MIN_MV || w->mv > ESC_PROG_MAX_MV
               || w->ma < ESC_PROG_MIN_MA || w->ma > ESC_PROG_MAX_MA
               || (p->limit_mv != 0u && w->mv > p->limit_mv)
               || (p->limit_ma != 0u && w->ma > p->limit_ma));
}

esc_prog_msg_t esc_prog_connect(esc_prog_t *p, const esc_prog_connect_t *c)
{
    if (p == NULL || c == NULL) {
        return ESC_PROG_MSG_RANGE;
    }
    esc_prog_msg_t m = common_gate(p);

    if (m == ESC_PROG_MSG_NONE && p->f.rd.held) {
        m = ESC_PROG_MSG_HELD;
    }
    if (m == ESC_PROG_MSG_NONE && supply_out_of_range(p, &c->power)) {
        m = ESC_PROG_MSG_RANGE;
    }
    if (m != ESC_PROG_MSG_NONE) {
        return esc_prog_say(p, m, 0u);
    }
    p->conn = *c;
    p->flow = ESC_PROG_FLOW_CONNECT;
    p->backup_state = (uint8_t)ESC_PROG_BACKUP_NONE;
    (void)esc_prog_say(p, ESC_PROG_MSG_NONE, 0u);
    esc_prog_issue(p, ESC_PROG_CMD_OPEN)->target = c->target;
    return ESC_PROG_MSG_NONE;
}

esc_prog_msg_t esc_prog_powered(esc_prog_t *p)
{
    if (p == NULL) {
        return ESC_PROG_MSG_BUSY;
    }
    esc_prog_msg_t m = common_gate(p);

    if (m == ESC_PROG_MSG_NONE && p->f.rd.state != (uint8_t)LINK_ESC_HAND_WAIT) {
        m = ESC_PROG_MSG_NOT_IN_MODE;
    }
    if (m != ESC_PROG_MSG_NONE) {
        return esc_prog_say(p, m, 0u);
    }
    p->flow = ESC_PROG_FLOW_CONNECT;
    (void)esc_prog_issue(p, ESC_PROG_CMD_POWERED);
    return ESC_PROG_MSG_NONE;
}

esc_prog_msg_t esc_prog_enter_again(esc_prog_t *p, const esc_prog_connect_t *c)
{
    if (p == NULL || c == NULL) {
        return ESC_PROG_MSG_BUSY;
    }
    esc_prog_msg_t m = common_gate(p);

    if (m == ESC_PROG_MSG_NONE && p->f.rd.state != (uint8_t)LINK_ESC_OPEN) {
        m = ESC_PROG_MSG_NOT_IN_MODE;
    }
    if (m == ESC_PROG_MSG_NONE && supply_out_of_range(p, &c->power)) {
        m = ESC_PROG_MSG_RANGE;
    }
    if (m != ESC_PROG_MSG_NONE) {
        return esc_prog_say(p, m, 0u);
    }
    p->flow = ESC_PROG_FLOW_CONNECT;
    (void)esc_prog_say(p, ESC_PROG_MSG_NONE, 0u);
    esc_prog_issue(p, ESC_PROG_CMD_ENTER)->power = c->power;
    return ESC_PROG_MSG_NONE;
}

esc_prog_msg_t esc_prog_read(esc_prog_t *p)
{
    if (p == NULL) {
        return ESC_PROG_MSG_BUSY;
    }
    const esc_prog_msg_t m = mode_gate(p);

    if (m != ESC_PROG_MSG_NONE) {
        return esc_prog_say(p, m, 0u);
    }
    p->flow = ESC_PROG_FLOW_ONE;
    (void)esc_prog_issue(p, ESC_PROG_CMD_READ);
    return ESC_PROG_MSG_NONE;
}

esc_prog_msg_t esc_prog_write(esc_prog_t *p, const uint8_t *draft, uint16_t len)
{
    if (p == NULL || draft == NULL) {
        return ESC_PROG_MSG_BUSY;
    }
    const esc_link_image_t *im = &p->f.image;
    esc_prog_msg_t m = mode_gate(p);

    if (m == ESC_PROG_MSG_NONE) {
        const uint16_t fl = p->f.rd.flags;

        if ((fl & LINK_EW_F_ERASED) != 0u) {
            m = ESC_PROG_MSG_ERASED;
        } else if ((fl & LINK_EW_F_HAVE_READ) == 0u || im->gen == 0u) {
            m = ESC_PROG_MSG_NOT_READ;
        } else if ((fl & LINK_EW_F_WRITABLE) == 0u) {
            m = ESC_PROG_MSG_READ_ONLY;
        } else if (im->reads != p->f.rd.reads
                   || im->read_crc != p->f.rd.read_crc || len != im->len) {
            m = ESC_PROG_MSG_STALE;
        } else if (memcmp(draft, im->page, len) == 0) {
            m = ESC_PROG_MSG_NO_CHANGE;
        } else if (p->backup_state != (uint8_t)ESC_PROG_BACKUP_SETTLED) {
            m = ESC_PROG_MSG_BACKUP_FIRST;
        }
    }
    if (m != ESC_PROG_MSG_NONE) {
        return esc_prog_say(p, m, 0u);
    }
    p->flow = ESC_PROG_FLOW_ONE;
    esc_prog_cmd_t *c = esc_prog_issue(p, ESC_PROG_CMD_WRITE);

    c->reads = im->reads;
    c->read_crc = im->read_crc;
    c->len = len;
    memcpy(c->draft, draft, len);
    return ESC_PROG_MSG_NONE;
}

esc_prog_msg_t esc_prog_leave(esc_prog_t *p)
{
    if (p == NULL) {
        return ESC_PROG_MSG_BUSY;
    }
    /* STOP latched does not hold back a CLOSE; an ARM cannot stand under a
     * hold. */
    if (p->f.rd.phase != ESC_LINK_READY || p->wait_id != 0u
        || p->flow != ESC_PROG_FLOW_NONE) {
        return esc_prog_say(p, ESC_PROG_MSG_BUSY, 0u);
    }
    p->flow = ESC_PROG_FLOW_LEAVE;
    p->backup_state = (uint8_t)ESC_PROG_BACKUP_NONE;
    /* An erased AM32 refuses a LEAVE: the pin goes back and the supply goes off. */
    (void)esc_prog_issue(p, p->f.rd.state == (uint8_t)LINK_ESC_READY
                                && (p->f.rd.flags & LINK_EW_F_ERASED) == 0u
                            ? ESC_PROG_CMD_LEAVE : ESC_PROG_CMD_CLOSE);
    return ESC_PROG_MSG_NONE;
}

esc_prog_msg_t esc_prog_clear(esc_prog_t *p)
{
    if (p == NULL) {
        return ESC_PROG_MSG_BUSY;
    }
    const esc_prog_msg_t m = common_gate(p);

    if (m != ESC_PROG_MSG_NONE) {
        return esc_prog_say(p, m, 0u);
    }
    p->flow = ESC_PROG_FLOW_ONE;
    (void)esc_prog_issue(p, ESC_PROG_CMD_CLEAR);
    return ESC_PROG_MSG_NONE;
}

esc_prog_msg_t esc_prog_close(esc_prog_t *p)
{
    if (p == NULL) {
        return ESC_PROG_MSG_BUSY;
    }
    if (p->f.rd.phase != ESC_LINK_READY || p->wait_id != 0u
        || p->flow != ESC_PROG_FLOW_NONE) {
        return esc_prog_say(p, ESC_PROG_MSG_BUSY, 0u);
    }
    p->flow = ESC_PROG_FLOW_ONE;
    p->backup_state = (uint8_t)ESC_PROG_BACKUP_NONE;
    (void)esc_prog_issue(p, ESC_PROG_CMD_CLOSE);
    return ESC_PROG_MSG_NONE;
}

void esc_prog_abort(esc_prog_t *p)
{
    if (p != NULL) {
        p->flow = ESC_PROG_FLOW_NONE;
        (void)esc_prog_issue(p, ESC_PROG_CMD_ABORT);
        p->wait_id = 0u;
        p->aborted = 1u;
        (void)esc_prog_say(p, ESC_PROG_MSG_ABORTED, 0u);
    }
}

void esc_prog_limits(esc_prog_t *p, uint16_t max_mv, uint16_t max_ma)
{
    if (p != NULL) {
        p->limit_mv = max_mv;
        p->limit_ma = max_ma;
    }
}

void esc_prog_word_clear(esc_prog_t *p)
{
    if (p != NULL) {
        (void)esc_prog_say(p, ESC_PROG_MSG_NONE, 0u);
    }
}

void esc_prog_word_on_free(esc_prog_t *p)
{
    if (p == NULL) {
        return;
    }
    switch (p->msg) {
    case ESC_PROG_MSG_LEFT:
    case ESC_PROG_MSG_NO_LINK:
    case ESC_PROG_MSG_NO_PAGE:
    case ESC_PROG_MSG_REFUSED:
    case ESC_PROG_MSG_NO_LINE:
    case ESC_PROG_MSG_SUPPLY:
    case ESC_PROG_MSG_TIMEOUT:
    case ESC_PROG_MSG_CUT:
    case ESC_PROG_MSG_SESSION:
    case ESC_PROG_MSG_NOT_BACK:
    case ESC_PROG_MSG_NO_ANSWER:
    case ESC_PROG_MSG_RESTARTED:
    case ESC_PROG_MSG_NACK:
    case ESC_PROG_MSG_DROPPED:
        break;
    default:
        (void)esc_prog_say(p, ESC_PROG_MSG_NONE, 0u);
        break;
    }
}

esc_prog_msg_t esc_prog_message(const esc_prog_t *p, uint16_t *detail)
{
    if (p == NULL) {
        return ESC_PROG_MSG_NONE;
    }
    if (detail != NULL) {
        *detail = p->detail;
    }
    return p->msg;
}

bool esc_prog_busy(const esc_prog_t *p)
{
    return p != NULL && (p->flow != ESC_PROG_FLOW_NONE || p->wait_id != 0u);
}

bool esc_prog_cmd_allowed(uint8_t kind, bool stopped)
{
    return !stopped || kind == (uint8_t)ESC_PROG_CMD_CLOSE
           || kind == (uint8_t)ESC_PROG_CMD_LEAVE
           || kind == (uint8_t)ESC_PROG_CMD_ABORT;
}

bool esc_prog_idle(const esc_prog_t *p)
{
    if (p == NULL) {
        return false;
    }
    if (p->f.rd.phase == ESC_LINK_DOWN || p->f.rd.phase == ESC_LINK_NO_PAGE) {
        return true;
    }
    return p->f.rd.phase == ESC_LINK_READY && !esc_prog_holds_port(p)
           && p->wait_id == 0u;
}

bool esc_prog_port_taken(const esc_link_readout_t *rd)
{
    return rd != NULL && rd->known && rd->held;
}

bool esc_prog_holds_port(const esc_prog_t *p)
{
    return p != NULL
           && (esc_prog_port_taken(&p->f.rd) || p->flow == ESC_PROG_FLOW_CONNECT);
}
