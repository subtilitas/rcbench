/**
 * @file
 * @brief Programming for YGE ESCs as a screen runs it.  See yge_prog.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "yge_prog.h"

#include <string.h>

#include "openyge_guard.h"
#include "openyge_session.h"
#include "yge_prog_int.h"

void yge_prog_gate_init(yge_prog_gate_t *g)
{
    atomic_store(&g->pass, 0u);
}

bool yge_prog_gate_claim(yge_prog_gate_t *g, uint32_t id)
{
    uint32_t want = id;

    return id != 0u && atomic_compare_exchange_strong(&g->pass, &want, 0u);
}

void yge_prog_init(yge_prog_t *p)
{
    if (p != NULL) {
        memset(p, 0, sizeof(*p));
        p->next_id = 1u;
    }
}

void yge_prog_gate_set(yge_prog_t *p, yge_prog_gate_t *g)
{
    if (p != NULL) {
        p->gate = g;
    }
}

yge_prog_msg_t yge_prog_say(yge_prog_t *p, yge_prog_msg_t m, uint16_t detail)
{
    p->msg = m;
    p->detail = detail;
    return m;
}

yge_prog_cmd_t *yge_prog_issue(yge_prog_t *p, yge_prog_cmd_kind_t kind)
{
    yge_prog_cmd_t *c = &p->out;

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
 * @return YGE_PROG_MSG_NONE, or why not
 */
static yge_prog_msg_t common_gate(const yge_prog_t *p)
{
    const yge_link_phase_t ph = p->f.rd.phase;

    if (p->f.armed) {
        return YGE_PROG_MSG_ARMED;
    }
    if (p->f.stopped) {
        return YGE_PROG_MSG_STOPPED;
    }
    if (ph == YGE_LINK_DOWN) {
        return YGE_PROG_MSG_NO_LINK;
    }
    if (ph == YGE_LINK_NO_PAGE) {
        return YGE_PROG_MSG_NO_PAGE;
    }
    if (ph != YGE_LINK_READY || p->wait_id != 0u
        || p->flow != YGE_PROG_FLOW_NONE) {
        return YGE_PROG_MSG_BUSY;
    }
    return YGE_PROG_MSG_NONE;
}

/**
 * @brief The gate of an operation on an ESC that answered.
 * @param p the model
 * @return YGE_PROG_MSG_NONE, or why not
 */
static yge_prog_msg_t mode_gate(const yge_prog_t *p)
{
    const yge_prog_msg_t m = common_gate(p);

    if (m != YGE_PROG_MSG_NONE) {
        return m;
    }
    return p->f.rd.state == (uint8_t)LINK_YGE_READY ? YGE_PROG_MSG_NONE
                                                    : YGE_PROG_MSG_NOT_READY;
}

/**
 * @brief The step after an OPEN, ENTER or HAND_DONE that ended well.
 * @param p the model
 * @param o the outcome
 */
static void connect_goes_on(yge_prog_t *p, const yge_link_outcome_t *o)
{
    /* A stop between two steps ends the flow: no entry starts under it. */
    if (p->f.stopped || p->f.armed) {
        p->flow = YGE_PROG_FLOW_NONE;
        (void)yge_prog_say(p, p->f.stopped ? YGE_PROG_MSG_STOPPED
                                           : YGE_PROG_MSG_ARMED, 0u);
        return;
    }
    if (o->op == (uint8_t)LINK_YGE_OP_OPEN) {
        yge_prog_issue(p, YGE_PROG_CMD_ENTER)->power = p->conn.power;
    } else if (o->state == (uint8_t)LINK_YGE_HAND_WAIT) {
        p->flow = YGE_PROG_FLOW_NONE;
        (void)yge_prog_say(p, YGE_PROG_MSG_HAND_WAIT, 0u);
    } else if (o->op == (uint8_t)LINK_YGE_OP_READ) {
        p->flow = YGE_PROG_FLOW_NONE;
        (void)yge_prog_say(p, YGE_PROG_MSG_READ_DONE, 0u);
    } else {
        (void)yge_prog_issue(p, YGE_PROG_CMD_READ);
    }
}

/**
 * @brief An operation that ended, DONE, in what it was asked for.
 * @param p the model
 * @param o the outcome
 */
static void ended_well(yge_prog_t *p, const yge_link_outcome_t *o)
{
    switch (p->flow) {
    case YGE_PROG_FLOW_CONNECT:
        connect_goes_on(p, o);
        return;
    case YGE_PROG_FLOW_LEAVE:
        if (o->op == (uint8_t)LINK_YGE_OP_LEAVE) {
            (void)yge_prog_issue(p, YGE_PROG_CMD_CLOSE);
            return;
        }
        p->flow = YGE_PROG_FLOW_NONE;
        (void)yge_prog_say(p, YGE_PROG_MSG_LEFT, 0u);
        return;
    default:
        p->flow = YGE_PROG_FLOW_NONE;
        break;
    }
    switch (o->op) {
    case LINK_YGE_OP_READ:
        (void)yge_prog_say(p, YGE_PROG_MSG_READ_DONE, 0u);
        break;
    case LINK_YGE_OP_WRITE:
        (void)yge_prog_say(p, YGE_PROG_MSG_WRITE_DONE, 0u);
        break;
    case LINK_YGE_OP_CLEAR:
        (void)yge_prog_say(p, YGE_PROG_MSG_CLEARED, 0u);
        break;
    case LINK_YGE_OP_CLOSE:
    case LINK_YGE_OP_LEAVE:
        (void)yge_prog_say(p, YGE_PROG_MSG_LEFT, 0u);
        break;
    default:
        break;
    }
}

/**
 * @brief What a result says.
 * @param p      the model
 * @param result a link_yge_result_t
 * @param ses    the session's last result
 * @return true for a result that ended well: nothing was said
 */
static bool say_result(yge_prog_t *p, uint8_t result, uint8_t ses)
{
    switch (result) {
    case LINK_YGE_RESULT_NO_LINE:
        (void)yge_prog_say(p, YGE_PROG_MSG_NO_LINE, 0u);
        return false;
    case LINK_YGE_RESULT_SUPPLY:
        (void)yge_prog_say(p, YGE_PROG_MSG_SUPPLY, 0u);
        return false;
    case LINK_YGE_RESULT_TIMEOUT:
        (void)yge_prog_say(p, YGE_PROG_MSG_TIMEOUT, 0u);
        return false;
    case LINK_YGE_RESULT_CUT:
        (void)yge_prog_say(p, YGE_PROG_MSG_CUT, 0u);
        return false;
    case LINK_YGE_RESULT_SESSION:
        (void)yge_prog_say(p, YGE_PROG_MSG_SESSION, ses);
        return false;
    case LINK_YGE_RESULT_NOT_BACK:
        (void)yge_prog_say(p, YGE_PROG_MSG_NOT_BACK, 0u);
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
static void take_outcome(yge_prog_t *p, const yge_link_outcome_t *o)
{
    bool well = false;

    switch (o->kind) {
    case YGE_LINK_OUT_DONE:
        /* CLEAR moves no result: what stands there is what it cleared. */
        well = o->op == (uint8_t)LINK_YGE_OP_CLEAR
               || say_result(p, o->result, o->ses);
        break;
    case YGE_LINK_OUT_REFUSED:
        (void)yge_prog_say(p, YGE_PROG_MSG_REFUSED, o->refusal);
        break;
    case YGE_LINK_OUT_NACK:
        (void)yge_prog_say(p, YGE_PROG_MSG_NACK, o->nack);
        break;
    case YGE_LINK_OUT_NO_ANSWER:
        (void)yge_prog_say(p, YGE_PROG_MSG_NO_ANSWER, 0u);
        break;
    case YGE_LINK_OUT_RESTARTED:
        (void)yge_prog_say(p, YGE_PROG_MSG_RESTARTED, 0u);
        break;
    case YGE_LINK_OUT_DROPPED:
        (void)yge_prog_say(p, YGE_PROG_MSG_DROPPED, 0u);
        break;
    default:
        break;
    }
    /* What an ABORT cut is that ABORT's: a stop's word would name the wrong
     * hand. */
    if (p->aborted != 0u) {
        p->aborted = 0u;
        if (p->msg == YGE_PROG_MSG_CUT || p->msg == YGE_PROG_MSG_DROPPED
            || (p->msg == YGE_PROG_MSG_SESSION
                && p->detail == (uint16_t)OY_ABORTED)) {
            (void)yge_prog_say(p, YGE_PROG_MSG_ABORTED, 0u);
        }
    }
    if (well) {
        ended_well(p, o);
    } else {
        p->flow = YGE_PROG_FLOW_NONE;
    }
}

/**
 * @brief A stop or fault that came with no request running: a hand wait run
 *        out, a stop edge, a supply gone.  The page's result says which.
 * @param p the model
 */
static void watch_flags(yge_prog_t *p)
{
    const yge_link_readout_t *rd = &p->f.rd;
    const uint16_t bits = (uint16_t)(LINK_YP_F_FAULT | LINK_YP_F_STOPPED);
    const uint16_t now = rd->known ? (uint16_t)(rd->flags & bits) : 0u;
    const bool rose = (now & (uint16_t)~p->flags_seen) != 0u;

    p->flags_seen = now;
    /* The STOP the link sends after a page that stopped answering is that
     * error's own: its word stays. */
    if (rose && p->flow == YGE_PROG_FLOW_NONE && p->wait_id == 0u
        && rd->phase == YGE_LINK_READY && p->msg != YGE_PROG_MSG_NO_ANSWER) {
        (void)say_result(p, rd->result, rd->ses);
    }
}

/**
 * @brief Whether a message gives way to the link's state.  WRITE_DONE does
 *        not: it is the only word of a finished write, and an error says what
 *        ended a flow.
 * @param m The message.
 * @return true then.
 */
static bool msg_is_good_word(yge_prog_msg_t m)
{
    return m == YGE_PROG_MSG_NONE || m == YGE_PROG_MSG_READ_DONE
           || m == YGE_PROG_MSG_LEFT || m == YGE_PROG_MSG_CLEARED
           || m == YGE_PROG_MSG_LOADED || m == YGE_PROG_MSG_HAND_WAIT
           || m == YGE_PROG_MSG_ABORTED;
}

/**
 * @brief What the link said to the command out, or that it never reached the
 *        link's task.
 * @param p the model
 * @param f the feed
 */
static void follow_answer(yge_prog_t *p, const yge_prog_feed_t *f)
{
    if (p->wait_id == 0u) {
        return;
    }
    if (f->answer_id == p->wait_id) {
        p->wait_id = 0u;
        if (f->answer != (uint8_t)YGE_LINK_START_OK) {
            p->flow = YGE_PROG_FLOW_NONE;
            (void)yge_prog_say(p, f->answer == (uint8_t)YGE_LINK_START_DOWN
                                      ? YGE_PROG_MSG_NO_LINK
                                  : f->answer == (uint8_t)YGE_LINK_START_NO_PAGE
                                      ? YGE_PROG_MSG_NO_PAGE
                                      : YGE_PROG_MSG_BUSY, 0u);
        }
    } else if ((uint32_t)(f->now_ms - p->wait_ms) >= YGE_PROG_CMD_WAIT_MS) {
        /* Never reached the link's task.  The pass is taken back, so it is
         * not carried out later. */
        if (p->gate == NULL || yge_prog_gate_claim(p->gate, p->wait_id)) {
            p->wait_id = 0u;
            p->flow = YGE_PROG_FLOW_NONE;
            p->out_new = 0u;
            (void)yge_prog_say(p, YGE_PROG_MSG_NO_ANSWER, 0u);
        }
    }
}

void yge_prog_feed(yge_prog_t *p, const yge_prog_feed_t *f)
{
    if (p == NULL || f == NULL) {
        return;
    }
    p->f = *f;
    follow_answer(p, f);
    if (f->outcomes != p->outcomes_seen) {
        p->outcomes_seen = f->outcomes;
        if (p->wait_id == 0u) {
            take_outcome(p, &f->outcome);
        }
    }
    watch_flags(p);
    /* The link's state stands in for a good word while it is gone, and goes
     * when it is back.  An error that said what ended a flow stays. */
    if ((f->rd.phase == YGE_LINK_DOWN || f->rd.phase == YGE_LINK_NO_PAGE)
        && msg_is_good_word(p->msg)) {
        (void)yge_prog_say(p, f->rd.phase == YGE_LINK_DOWN
                                  ? YGE_PROG_MSG_NO_LINK : YGE_PROG_MSG_NO_PAGE,
                           0u);
    } else if (f->rd.phase == YGE_LINK_READY
               && (p->msg == YGE_PROG_MSG_NO_LINK
                   || p->msg == YGE_PROG_MSG_NO_PAGE)) {
        (void)yge_prog_say(p, YGE_PROG_MSG_NONE, 0u);
    }
    yge_prog_backup_follow(p);
}

bool yge_prog_next_cmd(yge_prog_t *p, yge_prog_cmd_t *out)
{
    if (p == NULL || out == NULL || !p->out_new) {
        return false;
    }
    *out = p->out;
    p->out_new = 0u;
    return true;
}

/**
 * @brief Whether the values of a CONNECT or an ENTER are out of range: the
 *        boot wait, and for SWITCHED the voltage and the limit, the module's
 *        or SUPPLY's for this board and setting.
 * @param p The model.
 * @param w The power.
 * @return true then.
 */
static bool power_out_of_range(const yge_prog_t *p, const yge_link_power_t *w)
{
    if (w->power >= (uint8_t)LINK_YGE_POWER_COUNT || w->boot_ms < OY_BOOT_MS_MIN
        || w->boot_ms > OY_BOOT_MS_MAX) {
        return true;
    }
    return w->power == (uint8_t)LINK_YGE_SWITCHED
           && (w->mv < LINK_YGE_SUPPLY_MIN_MV || w->mv > YGE_PROG_MAX_MV
               || w->ma < YGE_PROG_MIN_MA || w->ma > YGE_PROG_MAX_MA
               || (p->limit_mv != 0u && w->mv > p->limit_mv)
               || (p->limit_ma != 0u && w->ma > p->limit_ma));
}

yge_prog_msg_t yge_prog_connect(yge_prog_t *p, const yge_prog_connect_t *c)
{
    if (p == NULL || c == NULL) {
        return YGE_PROG_MSG_RANGE;
    }
    yge_prog_msg_t m = common_gate(p);

    if (m == YGE_PROG_MSG_NONE && p->f.rd.held) {
        m = YGE_PROG_MSG_HELD;
    }
    if (m == YGE_PROG_MSG_NONE
        && (c->port >= 4u || power_out_of_range(p, &c->power))) {
        m = YGE_PROG_MSG_RANGE;
    }
    if (m != YGE_PROG_MSG_NONE) {
        return yge_prog_say(p, m, 0u);
    }
    p->conn = *c;
    p->flow = YGE_PROG_FLOW_CONNECT;
    (void)yge_prog_say(p, YGE_PROG_MSG_NONE, 0u);
    yge_prog_issue(p, YGE_PROG_CMD_OPEN)->port = c->port;
    return YGE_PROG_MSG_NONE;
}

yge_prog_msg_t yge_prog_hand_done(yge_prog_t *p)
{
    if (p == NULL) {
        return YGE_PROG_MSG_BUSY;
    }
    yge_prog_msg_t m = common_gate(p);

    if (m == YGE_PROG_MSG_NONE
        && p->f.rd.state != (uint8_t)LINK_YGE_HAND_WAIT) {
        m = YGE_PROG_MSG_NOT_READY;
    }
    if (m != YGE_PROG_MSG_NONE) {
        return yge_prog_say(p, m, 0u);
    }
    p->flow = YGE_PROG_FLOW_CONNECT;
    (void)yge_prog_say(p, YGE_PROG_MSG_NONE, 0u);
    (void)yge_prog_issue(p, YGE_PROG_CMD_HAND_DONE);
    return YGE_PROG_MSG_NONE;
}

yge_prog_msg_t yge_prog_enter_again(yge_prog_t *p, const yge_prog_connect_t *c)
{
    if (p == NULL || c == NULL) {
        return YGE_PROG_MSG_BUSY;
    }
    yge_prog_msg_t m = common_gate(p);

    if (m == YGE_PROG_MSG_NONE && p->f.rd.state != (uint8_t)LINK_YGE_OPEN) {
        m = YGE_PROG_MSG_NOT_READY;
    }
    if (m == YGE_PROG_MSG_NONE && power_out_of_range(p, &c->power)) {
        m = YGE_PROG_MSG_RANGE;
    }
    if (m != YGE_PROG_MSG_NONE) {
        return yge_prog_say(p, m, 0u);
    }
    p->conn = *c;
    p->flow = YGE_PROG_FLOW_CONNECT;
    (void)yge_prog_say(p, YGE_PROG_MSG_NONE, 0u);
    yge_prog_issue(p, YGE_PROG_CMD_ENTER)->power = c->power;
    return YGE_PROG_MSG_NONE;
}

yge_prog_msg_t yge_prog_read(yge_prog_t *p)
{
    if (p == NULL) {
        return YGE_PROG_MSG_BUSY;
    }
    const yge_prog_msg_t m = mode_gate(p);

    if (m != YGE_PROG_MSG_NONE) {
        return yge_prog_say(p, m, 0u);
    }
    p->flow = YGE_PROG_FLOW_ONE;
    (void)yge_prog_issue(p, YGE_PROG_CMD_READ);
    return YGE_PROG_MSG_NONE;
}

yge_prog_msg_t yge_prog_write_gate(const yge_prog_t *p, uint16_t n,
                                   uint16_t reads, uint16_t read_crc)
{
    const yge_link_image_t *im = &p->f.image;
    const uint16_t fl = p->f.rd.flags;
    const yge_prog_msg_t m = mode_gate(p);

    if (m != YGE_PROG_MSG_NONE) {
        return m;
    }
    if ((fl & LINK_YP_F_HAVE_READ) == 0u || im->gen == 0u) {
        return YGE_PROG_MSG_NOT_READ;
    }
    if ((fl & LINK_YP_F_WRITABLE) == 0u) {
        return YGE_PROG_MSG_READ_ONLY;
    }
    if (im->reads != p->f.rd.reads || im->read_crc != p->f.rd.read_crc
        || n != im->entries || reads != im->reads || read_crc != im->read_crc) {
        return YGE_PROG_MSG_STALE;
    }
    return YGE_PROG_MSG_NONE;
}

void yge_prog_write_go(yge_prog_t *p, const uint16_t *draft, uint16_t n,
                       uint16_t reads, uint16_t read_crc)
{
    p->flow = YGE_PROG_FLOW_ONE;
    yge_prog_cmd_t *c = yge_prog_issue(p, YGE_PROG_CMD_WRITE);

    c->reads = reads;
    c->read_crc = read_crc;
    c->n = n;
    memcpy(c->draft, draft, (size_t)n * sizeof(draft[0]));
}

/**
 * @brief The guard's word on a draft, as the IOmcu's guard would judge it.
 * @param p     the model
 * @param draft the draft
 * @param n     its values
 * @return YGE_PROG_MSG_NONE, or YGE_PROG_MSG_GUARD with the reason said
 */
static yge_prog_msg_t guard_judge(yge_prog_t *p, const uint16_t *draft,
                                  uint16_t n)
{
    oy_guard_t g;
    uint16_t bad = 0u;

    if (!yge_prog_guard_of(p, &g)) {
        return yge_prog_say(p, YGE_PROG_MSG_NOT_READ, 0u);
    }
    const oy_ref_t r = oy_guard_approve(&g, draft, n, g.read_no, g.read_crc,
                                        &bad);

    if (r == OY_REF_NONE) {
        return YGE_PROG_MSG_NONE;
    }
    p->bad = bad;
    return yge_prog_say(p, YGE_PROG_MSG_GUARD, (uint16_t)r);
}

yge_prog_msg_t yge_prog_write(yge_prog_t *p, const uint16_t *draft, uint16_t n)
{
    if (p == NULL || draft == NULL) {
        return YGE_PROG_MSG_BUSY;
    }
    const yge_link_image_t *im = &p->f.image;
    yge_prog_msg_t m = yge_prog_write_gate(p, n, im->reads, im->read_crc);

    if (m == YGE_PROG_MSG_READ_ONLY) {
        return yge_prog_say(p, m, p->f.rd.ro_why);
    }
    if (m == YGE_PROG_MSG_NONE
        && memcmp(draft, im->value, (size_t)n * sizeof(draft[0])) == 0) {
        m = YGE_PROG_MSG_NO_CHANGE;
    }
    if (m == YGE_PROG_MSG_NONE
        && p->backup_state == (uint8_t)YGE_PROG_BACKUP_NONE) {
        m = YGE_PROG_MSG_NOT_READ;
    }
    if (m != YGE_PROG_MSG_NONE) {
        return yge_prog_say(p, m, 0u);
    }
    m = guard_judge(p, draft, n);
    if (m != YGE_PROG_MSG_NONE) {
        return m;
    }
    (void)yge_prog_say(p, YGE_PROG_MSG_NONE, 0u);
    if (p->backup_state != (uint8_t)YGE_PROG_BACKUP_HELD) {
        yge_prog_write_go(p, draft, n, im->reads, im->read_crc);
        return YGE_PROG_MSG_NONE;
    }
    /* The first write of the session: the backup to the card first. */
    p->flow = YGE_PROG_FLOW_BACKUP;
    memcpy(p->pend, draft, (size_t)n * sizeof(draft[0]));
    p->pend_n = n;
    p->pend_reads = im->reads;
    p->pend_crc = im->read_crc;
    p->card_ms = p->f.now_ms;
    p->card_owed = (uint8_t)(p->card_out != 0u ? 0u : 1u);
    return YGE_PROG_MSG_NONE;
}

yge_prog_msg_t yge_prog_leave(yge_prog_t *p)
{
    if (p == NULL) {
        return YGE_PROG_MSG_BUSY;
    }
    /* STOP latched does not hold back a CLOSE; an ARM cannot stand under a
     * hold. */
    if (p->f.rd.phase != YGE_LINK_READY || p->wait_id != 0u
        || p->flow != YGE_PROG_FLOW_NONE) {
        return yge_prog_say(p, YGE_PROG_MSG_BUSY, 0u);
    }
    p->flow = YGE_PROG_FLOW_LEAVE;
    (void)yge_prog_issue(p, p->f.rd.state == (uint8_t)LINK_YGE_READY
                                ? YGE_PROG_CMD_LEAVE : YGE_PROG_CMD_CLOSE);
    return YGE_PROG_MSG_NONE;
}

yge_prog_msg_t yge_prog_clear(yge_prog_t *p)
{
    if (p == NULL) {
        return YGE_PROG_MSG_BUSY;
    }
    const yge_prog_msg_t m = common_gate(p);

    if (m != YGE_PROG_MSG_NONE) {
        return yge_prog_say(p, m, 0u);
    }
    p->flow = YGE_PROG_FLOW_ONE;
    (void)yge_prog_issue(p, YGE_PROG_CMD_CLEAR);
    return YGE_PROG_MSG_NONE;
}

yge_prog_msg_t yge_prog_close(yge_prog_t *p)
{
    if (p == NULL) {
        return YGE_PROG_MSG_BUSY;
    }
    if (p->f.rd.phase != YGE_LINK_READY || p->wait_id != 0u
        || p->flow != YGE_PROG_FLOW_NONE) {
        return yge_prog_say(p, YGE_PROG_MSG_BUSY, 0u);
    }
    p->flow = YGE_PROG_FLOW_ONE;
    (void)yge_prog_issue(p, YGE_PROG_CMD_CLOSE);
    return YGE_PROG_MSG_NONE;
}

void yge_prog_abort(yge_prog_t *p)
{
    if (p != NULL) {
        p->flow = YGE_PROG_FLOW_NONE;
        p->card_owed = 0u;
        (void)yge_prog_issue(p, YGE_PROG_CMD_ABORT);
        p->wait_id = 0u;
        p->aborted = 1u;
        (void)yge_prog_say(p, YGE_PROG_MSG_ABORTED, 0u);
    }
}

void yge_prog_limits(yge_prog_t *p, uint16_t max_mv, uint16_t max_ma)
{
    if (p != NULL) {
        p->limit_mv = max_mv;
        p->limit_ma = max_ma;
    }
}

yge_prog_msg_t yge_prog_message(const yge_prog_t *p, uint16_t *detail)
{
    if (p == NULL) {
        return YGE_PROG_MSG_NONE;
    }
    if (detail != NULL) {
        *detail = p->detail;
    }
    return p->msg;
}

uint16_t yge_prog_bad(const yge_prog_t *p)
{
    return p != NULL ? p->bad : 0u;
}

bool yge_prog_busy(const yge_prog_t *p)
{
    return p != NULL && (p->flow != YGE_PROG_FLOW_NONE || p->wait_id != 0u);
}

bool yge_prog_cmd_allowed(uint8_t kind, bool stopped)
{
    return !stopped || kind == (uint8_t)YGE_PROG_CMD_CLOSE
           || kind == (uint8_t)YGE_PROG_CMD_LEAVE
           || kind == (uint8_t)YGE_PROG_CMD_ABORT;
}

bool yge_prog_idle(const yge_prog_t *p)
{
    if (p == NULL) {
        return false;
    }
    if (p->f.rd.phase == YGE_LINK_DOWN || p->f.rd.phase == YGE_LINK_NO_PAGE) {
        return true;
    }
    return p->f.rd.phase == YGE_LINK_READY && !yge_prog_holds_port(p)
           && p->wait_id == 0u;
}

bool yge_prog_port_taken(const yge_link_readout_t *rd)
{
    return rd != NULL && rd->known && rd->held;
}

int yge_prog_taken_port(const yge_link_readout_t *rd)
{
    return yge_prog_port_taken(rd) && rd->port < 4u ? (int)rd->port : -1;
}

bool yge_prog_holds_port(const yge_prog_t *p)
{
    return p != NULL
           && (yge_prog_port_taken(&p->f.rd)
               || p->flow == YGE_PROG_FLOW_CONNECT);
}
