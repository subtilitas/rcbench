/**
 * @file
 * @brief The programmer for servos by Hitec: the session driven through the
 *        link's task, the state the overlay draws and the messages it says.
 *        See hitec_prog.h.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog.h"

#include <string.h>

#include "hitec_rx_link.h"

/** @brief The request phases: what the model waits on the link for. */
enum {
    ST_IDLE = 0,    /**< nothing of this model runs               */
    ST_OPENING,     /**< OPEN                                     */
    ST_SES,         /**< a session operation, step by step        */
    ST_CLOSING,     /**< CLOSE after a leave                      */
    ST_CLEARING,    /**< CLEAR                                    */
    ST_RELEASING,   /**< CLOSE of another session's port          */
};

/** @brief The session operations. */
enum { OP_NONE = 0, OP_ENTER, OP_READ, OP_LEAVE };

/** @brief What a command waits for. */
enum {
    W_NONE = 0,     /**< no command is out                        */
    W_ANSWER,       /**< the link's task has not answered it      */
    W_OUTCOME,      /**< the module took it; its outcome is owed   */
};

/* ------------------------------------------------------------------ gate */

void hitec_prog_gate_init(hitec_prog_gate_t *g)
{
    atomic_store(&g->pass, 0u);
}

void hitec_prog_gate_offer(hitec_prog_gate_t *g, uint32_t id)
{
    atomic_store(&g->pass, id);
}

/**
 * @brief The pass of @p id, taken by whoever asks first.
 *
 * @param g  The gate.
 * @param id The command's id.
 * @return true when the pass was taken.
 */
static bool gate_take(hitec_prog_gate_t *g, uint32_t id)
{
    uint32_t want = id;

    return id != 0u && atomic_compare_exchange_strong(&g->pass, &want, 0u);
}

bool hitec_prog_gate_claim(hitec_prog_gate_t *g, uint32_t id)
{
    return gate_take(g, id);
}

bool hitec_prog_gate_revoke(hitec_prog_gate_t *g, uint32_t id)
{
    return gate_take(g, id);
}

/* -------------------------------------------------------------- messages */

hitec_prog_level_t hitec_prog_level(unsigned id)
{
    switch (id) {
    case HITEC_PROG_MSG_NONE:
    case HITEC_PROG_MSG_READ:
    case HITEC_PROG_MSG_CLEARED:
    case HITEC_PROG_MSG_LEFT:
    case HITEC_PROG_MSG_RELEASED:
        return HITEC_PROG_INFO;
    case HITEC_PROG_MSG_NO_C:
    case HITEC_PROG_MSG_NO_D:
    case HITEC_PROG_MSG_ENTRY_BAD:
    case HITEC_PROG_MSG_READ_FAIL:
    case HITEC_PROG_MSG_UNSAFE:
    case HITEC_PROG_MSG_SESSION_ENDED:
    case HITEC_PROG_MSG_TIMEOUT:
    case HITEC_PROG_MSG_NO_ANSWER:
    case HITEC_PROG_MSG_RESTARTED:
    case HITEC_PROG_MSG_SUPPLY:
        return HITEC_PROG_DANGER;
    default:
        return HITEC_PROG_WARN;
    }
}

/**
 * @brief Say message @p id.
 *
 * @param p   The programmer.
 * @param id  hitec_prog_msg_id_t.
 * @param arg Its argument.
 * @param reg Its second argument.
 * @return @p id.
 */
static hitec_prog_msg_id_t say2(hitec_prog_t *p, hitec_prog_msg_id_t id,
                                unsigned arg, unsigned reg)
{
    p->msg.id = (uint8_t)id;
    p->msg.level = (uint8_t)hitec_prog_level(id);
    p->msg.arg = (uint16_t)arg;
    p->msg.reg = (uint16_t)reg;
    ++p->msg.seq;
    return id;
}

/**
 * @brief Say message @p id with one argument.
 *
 * @param p   The programmer.
 * @param id  hitec_prog_msg_id_t.
 * @param arg Its argument.
 * @return @p id.
 */
static hitec_prog_msg_id_t say(hitec_prog_t *p, hitec_prog_msg_id_t id,
                               unsigned arg)
{
    return say2(p, id, arg, 0u);
}

/**
 * @brief Whether a stop at either end cut what ran.
 *
 * @param p The programmer.
 * @return true when the page or the panel is stopped.
 */
static bool stop_seen(const hitec_prog_t *p)
{
    return p->f.stopped || (p->f.rd.known && p->f.rd.stopped);
}

/**
 * @brief The message of a refusal of the page.
 *
 * @param p   The programmer.
 * @param ref link_hitec_refusal_t.
 */
static void refusal_say(hitec_prog_t *p, unsigned ref)
{
    switch (ref) {
    case LINK_HITEC_REF_BUSY:
        (void)say(p, HITEC_PROG_MSG_BUSY, 0u);
        break;
    case LINK_HITEC_REF_PORT:
        (void)say(p, HITEC_PROG_MSG_HELD_OTHER, p->f.rd.port_no);
        break;
    case LINK_HITEC_REF_HELD:
        (void)say(p, HITEC_PROG_MSG_HELD_KST, 0u);
        break;
    case LINK_HITEC_REF_ARMED:
        (void)say(p, HITEC_PROG_MSG_NEED_DISARM, 0u);
        break;
    case LINK_HITEC_REF_UNSAFE:
        (void)say(p, HITEC_PROG_MSG_UNSAFE, 0u);
        break;
    case LINK_HITEC_REF_NOT_OPEN:
        (void)say(p, HITEC_PROG_MSG_SESSION_ENDED, 0u);
        break;
    case LINK_HITEC_REF_STOPPED:
        (void)say(p, HITEC_PROG_MSG_STOPPED, 0u);
        break;
    case LINK_HITEC_REF_NO_PIN:
        (void)say(p, HITEC_PROG_MSG_NO_PIN, 0u);
        break;
    case LINK_HITEC_REF_NO_SUPPLY:
        (void)say(p, HITEC_PROG_MSG_NEED_PD_C, 0u);
        break;
    default:
        (void)say(p, HITEC_PROG_MSG_REFUSED, ref);
        break;
    }
}

/**
 * @brief The message of a request's outcome that did not end as asked.
 *
 * @param p The programmer.
 * @param o The outcome.
 */
static void outcome_say(hitec_prog_t *p, const hitec_link_outcome_t *o)
{
    switch (o->kind) {
    case HITEC_LINK_OUT_DONE:
        switch (o->result) {
        case LINK_HITEC_RESULT_CUT:
            (void)say(p, stop_seen(p) ? HITEC_PROG_MSG_STOPPED
                                      : HITEC_PROG_MSG_ABORTED, 0u);
            break;
        case LINK_HITEC_RESULT_SUPPLY:
            (void)say(p, HITEC_PROG_MSG_SUPPLY, 0u);
            break;
        case LINK_HITEC_RESULT_NO_LINE:
            (void)say(p, HITEC_PROG_MSG_NO_LINE, 0u);
            break;
        default:
            (void)say(p, HITEC_PROG_MSG_UNKNOWN, o->result);
            break;
        }
        break;
    case HITEC_LINK_OUT_REFUSED:
        refusal_say(p, (unsigned)o->refusal);
        break;
    case HITEC_LINK_OUT_NACK:
        (void)say(p, HITEC_PROG_MSG_NACK, o->nack);
        break;
    case HITEC_LINK_OUT_RESTARTED:
        (void)say(p, HITEC_PROG_MSG_RESTARTED, 0u);
        break;
    case HITEC_LINK_OUT_DROPPED:
        (void)say(p, HITEC_PROG_MSG_ABORTED, 0u);
        break;
    default:
        (void)say(p, HITEC_PROG_MSG_NO_ANSWER, 0u);
        break;
    }
}

/* ---------------------------------------------------------------- basics */

void hitec_prog_init(hitec_prog_t *p)
{
    memset(p, 0, sizeof(*p));
    p->series = (uint8_t)HITEC_D;
    p->supply_mv = HITEC_PROG_SUPPLY_MV;
    p->supply_ma = HITEC_PROG_SUPPLY_MA;
    (void)hitec_session_init(&p->ses, p->series, HITEC_SES_CLOCK_MS);
}

void hitec_prog_gate_set(hitec_prog_t *p, hitec_prog_gate_t *g)
{
    p->gate = g;
}

/**
 * @brief The session ends: nothing of it is in mode any more.
 *
 * @param p The programmer.
 */
static void session_gone(hitec_prog_t *p)
{
    p->owned = false;
    p->op = OP_NONE;
    (void)hitec_session_init(&p->ses, p->series, HITEC_SES_CLOCK_MS);
}

/**
 * @brief Put a command on its way to the link's task.  Every caller has
 *        moved off ST_IDLE first, and every end of a command's wait goes
 *        back to it: a model in ST_IDLE has no command out.
 *
 * @param p   The programmer.
 * @param cmd The command; its id is given here.
 */
static void send(hitec_prog_t *p, hitec_prog_cmd_t *cmd)
{
    if (++p->next_id == 0u) {
        p->next_id = 1u;
    }
    cmd->id = p->next_id;
    p->cmd = *cmd;
    p->cmd_ready = true;
    p->wait = W_ANSWER;
    p->wait_id = cmd->id;
    p->wait_ms = p->f.now_ms;
}

/**
 * @brief A command of @p kind on the port the model works on.
 *
 * @param p    The programmer.
 * @param kind hitec_prog_cmd_kind_t.
 * @param port The programming port.
 */
static void send_bare(hitec_prog_t *p, uint8_t kind, uint8_t port)
{
    hitec_prog_cmd_t c;

    memset(&c, 0, sizeof(c));
    c.kind = kind;
    c.port = port;
    send(p, &c);
}

bool hitec_prog_take(hitec_prog_t *p, hitec_prog_cmd_t *out)
{
    if (p->abort_ready) {
        p->abort_ready = false;
        memset(out, 0, sizeof(*out));
        out->kind = HITEC_PROG_CMD_ABORT;
        return true;
    }
    if (!p->cmd_ready) {
        return false;
    }
    *out = p->cmd;
    p->cmd_ready = false;
    if (p->gate != NULL) {
        hitec_prog_gate_offer(p->gate, out->id);
    }
    p->wait_ms = p->f.now_ms;
    return true;
}

/* ------------------------------------------------------------ the session */

/**
 * @brief Start the entry into programming mode on the open port.
 *
 * Classic: the supply is cycled HITEC_SES_OFF_MS.  D series: the supply is
 * switched on, off 0ms, where a PD mini answers the page; without one the
 * servo runs on the supply as it is.
 *
 * @param p The programmer.
 */
static void enter(hitec_prog_t *p)
{
    const bool classic = p->ses.series == (uint8_t)HITEC_CLASSIC;
    const bool power = !classic && hitec_prog_rail(p);

    /* Its callers hand an idle session, and HITEC_SES_OFF_MS is within
     * HITEC_SES_OFF_MS_MAX: the session takes the entry. */
    (void)hitec_session_enter(&p->ses, power,
                              classic ? (uint16_t)HITEC_SES_OFF_MS : 0u);
    p->st = ST_SES;
    p->op = OP_ENTER;
}

/**
 * @brief Start a read of the full set.
 *
 * @param p The programmer.
 */
static void read_all(hitec_prog_t *p)
{
    /* Its callers hand an idle session in programming mode: the session
     * takes the read. */
    (void)hitec_session_read_all(&p->ses);
    p->st = ST_SES;
    p->op = OP_READ;
}

/**
 * @brief A full read ended well: the servo, its model and the message.
 *
 * @param p The programmer.
 */
static void read_done(hitec_prog_t *p)
{
    const hitec_image_t *img = &p->ses.image;

    p->servo = *img;
    p->have_servo = true;
    /* A full read holds the D series model register and the classic name
     * cells: hitec_identify() names a model, a known one or its layout's
     * guess. */
    (void)hitec_identify(img, &p->model);
    if (p->ses.series == (uint8_t)HITEC_D) {
        (void)say2(p, HITEC_PROG_MSG_READ, p->model.number,
                   hitec_image_reg(img, 0x32u));
    } else {
        (void)say2(p, HITEC_PROG_MSG_READ, 0u,
                   img->has_version != 0u ? img->version : 0u);
    }
}

/**
 * @brief The message of a session operation that did not end well.
 *
 * @param p The programmer.
 * @param r Its result.
 */
static void ses_say(hitec_prog_t *p, hitec_ses_t r)
{
    switch (r) {
    case HITEC_SES_ERR_NO_SERVO:
        (void)say(p, p->ses.series == (uint8_t)HITEC_CLASSIC
                         ? HITEC_PROG_MSG_NO_C : HITEC_PROG_MSG_NO_D, 0u);
        break;
    case HITEC_SES_ERR_ENTRY:
        (void)say(p, HITEC_PROG_MSG_ENTRY_BAD, p->ses.last_rx);
        break;
    case HITEC_SES_ERR_READ:
        (void)say(p, HITEC_PROG_MSG_READ_FAIL, p->ses.fail_addr);
        break;
    case HITEC_SES_ERR_CUT:
        (void)say(p, stop_seen(p) ? HITEC_PROG_MSG_STOPPED
                                  : HITEC_PROG_MSG_ABORTED, 0u);
        break;
    case HITEC_SES_ERR_ABORTED:
        (void)say(p, HITEC_PROG_MSG_ABORTED, 0u);
        break;
    case HITEC_SES_ERR_LINK:
        /* What the link said of the step; a step that never went has its
         * message said already. */
        if (p->have_last) {
            outcome_say(p, &p->last);
        }
        break;
    default:
        (void)say(p, HITEC_PROG_MSG_UNKNOWN, (unsigned)r);
        break;
    }
}

/**
 * @brief A session operation ended with @p r.
 *
 * @param p The programmer.
 * @param r Its result.
 */
static void ses_done(hitec_prog_t *p, hitec_ses_t r)
{
    const uint8_t op = p->op;

    p->st = ST_IDLE;
    p->op = OP_NONE;
    switch (op) {
    case OP_ENTER:
        if (r == HITEC_SES_OK) {
            read_all(p);
            return;
        }
        ses_say(p, r);
        break;
    case OP_READ:
        if (r == HITEC_SES_OK) {
            read_done(p);
        } else {
            ses_say(p, r);
        }
        break;
    default: /* OP_LEAVE */
        /* Back to PWM whatever the leave came to: a STOPPED port refuses
         * the leave's frame and takes the CLOSE. */
        p->st = ST_CLOSING;
        send_bare(p, HITEC_PROG_CMD_CLOSE, p->port);
        break;
    }
}

/**
 * @brief A command that ended without an outcome: nothing of it ran, or what
 *        ran is not known.  A step out ends as one that did not run.
 *
 * @param p The programmer.
 */
static void give_up(hitec_prog_t *p)
{
    if (p->st == ST_SES) {
        hitec_ses_result_t r;

        memset(&r, 0, sizeof(r));
        r.run = (uint8_t)HITEC_SES_FAILED;
        p->have_last = false;
        (void)hitec_session_feed(&p->ses, p->f.now_ms, &r);
        return;
    }
    p->st = ST_IDLE;
}

/**
 * @brief The link's task answered the command awaited with @p answer, not
 *        OK: it did not start.
 *
 * @param p      The programmer.
 * @param answer hitec_link_start_t.
 */
static void not_started(hitec_prog_t *p, unsigned answer)
{
    switch (answer) {
    case HITEC_LINK_START_DOWN:
        (void)say(p, HITEC_PROG_MSG_DOWN, 0u);
        break;
    case HITEC_LINK_START_NO_PAGE:
        (void)say(p, HITEC_PROG_MSG_NO_PAGE, 0u);
        break;
    case HITEC_LINK_START_BUSY:
        (void)say(p, HITEC_PROG_MSG_BUSY, 0u);
        break;
    default:
        (void)say(p, HITEC_PROG_MSG_UNKNOWN, answer);
        break;
    }
    give_up(p);
}

/**
 * @brief An outcome of the request awaited.
 *
 * @param p The programmer.
 * @param o The outcome.
 */
static void on_outcome(hitec_prog_t *p, const hitec_link_outcome_t *o)
{
    const bool done = o->kind == HITEC_LINK_OUT_DONE;
    const bool gone = o->kind == HITEC_LINK_OUT_REFUSED
                      && o->refusal == LINK_HITEC_REF_NOT_OPEN;

    switch (p->st) {
    case ST_OPENING:
        p->st = ST_IDLE;
        if (done && o->result == LINK_HITEC_RESULT_DONE) {
            p->owned = true;
            (void)hitec_session_init(&p->ses, p->series, HITEC_SES_CLOCK_MS);
            enter(p);
        } else if (done && o->result == LINK_HITEC_RESULT_CUT) {
            /* Held and stopped: CLEAR or LEAVE. */
            p->owned = true;
            (void)hitec_session_init(&p->ses, p->series, HITEC_SES_CLOCK_MS);
            outcome_say(p, o);
        } else {
            outcome_say(p, o);
        }
        break;
    case ST_SES:
        p->last = *o;
        p->have_last = true;
        (void)hitec_session_feed(&p->ses, p->f.now_ms, &o->ses);
        break;
    case ST_CLOSING:
    case ST_RELEASING:
        if (done || gone) {
            const bool release = p->st == ST_RELEASING;

            session_gone(p);
            (void)say(p, release ? HITEC_PROG_MSG_RELEASED
                                 : HITEC_PROG_MSG_LEFT, p->port);
        } else {
            outcome_say(p, o);
        }
        p->st = ST_IDLE;
        break;
    default: /* ST_CLEARING: an outcome is awaited off ST_IDLE only */
        p->st = ST_IDLE;
        if (!done) {
            outcome_say(p, o);
        } else if (p->reenter || !p->ses.in_mode) {
            (void)say(p, HITEC_PROG_MSG_CLEARED, 0u);
            enter(p);
        } else {
            (void)say(p, HITEC_PROG_MSG_CLEARED, 0u);
            read_all(p);
        }
        break;
    }
}

/* ------------------------------------------------------------------ feed */

/**
 * @brief The answer to the command awaited, and the outcome of its request.
 *
 * @param p The programmer.
 */
static void take_answers(hitec_prog_t *p)
{
    const hitec_prog_feed_t *f = &p->f;

    if (p->wait == W_ANSWER && !p->cmd_ready && f->answer_id == p->wait_id) {
        if (f->answer == (uint8_t)HITEC_LINK_START_OK) {
            p->wait = W_OUTCOME;
            p->wait_ms = f->now_ms;
        } else {
            p->wait = W_NONE;
            not_started(p, f->answer);
        }
    }
    if (f->outcomes == p->outcomes_seen) {
        return;
    }
    p->outcomes_seen = f->outcomes;
    if (p->wait == W_OUTCOME && f->outcome.id == p->wait_id) {
        p->wait = W_NONE;
        on_outcome(p, &f->outcome);
    }
}

/**
 * @brief A command nobody answered, and a request with no outcome.
 *
 * @param p The programmer.
 */
static void take_timeouts(hitec_prog_t *p)
{
    const uint32_t age = p->f.now_ms - p->wait_ms;

    if (p->wait == W_ANSWER && age >= HITEC_PROG_START_MS) {
        bool never = p->cmd_ready;

        p->cmd_ready = false;
        if (!never) {
            never = p->gate == NULL
                    || hitec_prog_gate_revoke(p->gate, p->wait_id);
            /* With no gate it may have run: an ABORT follows it. */
            p->abort_ready = p->gate == NULL;
        }
        if (never) {
            p->wait = W_NONE;
            (void)say(p, HITEC_PROG_MSG_TIMEOUT, 0u);
            give_up(p);
        }
    }
    if (p->wait == W_OUTCOME && age >= HITEC_PROG_RUN_MS) {
        p->wait = W_NONE;
        p->abort_ready = true;
        (void)say(p, HITEC_PROG_MSG_TIMEOUT, 0u);
        give_up(p);
    }
}

/**
 * @brief The session follows the page: a port that is no longer this
 *        model's ends it.
 *
 * @param p The programmer.
 */
static void follow(hitec_prog_t *p)
{
    const hitec_link_readout_t *rd = &p->f.rd;

    if (p->lost && rd->known && rd->phase == HITEC_LINK_READY
        && p->st == ST_IDLE && p->wait == W_NONE) {
        /* The link is back after it took a session: what the page holds
         * now is said once. */
        p->lost = false;
        if (rd->held) {
            (void)say(p, HITEC_PROG_MSG_HELD_OTHER, rd->port_no);
        } else {
            (void)say(p, HITEC_PROG_MSG_SESSION_ENDED, 0u);
        }
        return;
    }
    if (!p->owned || p->st != ST_IDLE || p->wait != W_NONE) {
        return;
    }
    if (rd->phase == HITEC_LINK_DOWN || rd->phase == HITEC_LINK_NO_PAGE) {
        session_gone(p);
        p->lost = rd->phase == HITEC_LINK_DOWN;
        return;
    }
    if (!rd->known || (rd->held && rd->ours)) {
        return;
    }
    session_gone(p);
    if (rd->held) {
        (void)say(p, HITEC_PROG_MSG_HELD_OTHER, rd->port_no);
    } else {
        (void)say(p, HITEC_PROG_MSG_SESSION_ENDED, 0u);
    }
}

/**
 * @brief The session's next step goes out, or its operation's end is taken.
 *
 * @param p The programmer.
 */
static void pump(hitec_prog_t *p)
{
    /* An entry that ends well starts the read: its first step goes in the
     * same frame. */
    for (int i = 0; i < 2; ++i) {
        hitec_ses_step_t st;
        hitec_ses_kind_t kind;

        if (p->st != ST_SES || p->wait != W_NONE || p->cmd_ready) {
            return;
        }
        kind = hitec_session_next(&p->ses, p->f.now_ms, &st);
        if (kind == HITEC_SES_STEP_XFER || kind == HITEC_SES_STEP_POWER) {
            hitec_prog_cmd_t c;

            memset(&c, 0, sizeof(c));
            c.kind = HITEC_PROG_CMD_STEP;
            c.port = p->port;
            c.mv = p->mv_open;
            c.ma = p->ma_open;
            c.step = st;
            send(p, &c);
            return;
        }
        if (kind != HITEC_SES_STEP_DONE) {
            return;
        }
        ses_done(p, hitec_session_result(&p->ses));
    }
}

void hitec_prog_feed(hitec_prog_t *p, const hitec_prog_feed_t *f)
{
    const bool was = p->fed;

    p->f = *f;
    p->fed = true;
    if (!was) {
        /* An outcome handed out before this model was fed is no request
         * of it. */
        p->outcomes_seen = f->outcomes;
        /* Nor is an answer to an id given before: the ids go on past the
         * control task's last ones. */
        p->next_id = f->answer_id > f->outcome.id ? f->answer_id
                                                  : f->outcome.id;
    }
    take_answers(p);
    take_timeouts(p);
    follow(p);
    pump(p);
}

/* ---------------------------------------------------------------- state */

hitec_prog_state_t hitec_prog_state(const hitec_prog_t *p)
{
    const hitec_link_readout_t *rd = &p->f.rd;

    if (!p->fed || rd->phase == HITEC_LINK_DOWN) {
        return HITEC_PROG_DOWN;
    }
    if (rd->phase == HITEC_LINK_NO_PAGE) {
        return HITEC_PROG_NO_PAGE;
    }
    switch (p->st) {
    case ST_OPENING:
        return HITEC_PROG_OPENING;
    case ST_SES:
        return p->op == OP_ENTER  ? HITEC_PROG_ENTERING
             : p->op == OP_LEAVE  ? HITEC_PROG_LEAVING
                                  : HITEC_PROG_READING;
    case ST_CLOSING:
        return HITEC_PROG_LEAVING;
    case ST_CLEARING:
        return HITEC_PROG_CLEARING;
    case ST_RELEASING:
        return HITEC_PROG_RELEASING;
    default:
        break;
    }
    if (!rd->known) {
        return HITEC_PROG_SYNC;
    }
    if (rd->held) {
        return p->owned && rd->ours ? HITEC_PROG_HELD : HITEC_PROG_OTHER;
    }
    return HITEC_PROG_PWM;
}

void hitec_prog_bench_armed(hitec_prog_t *p, bool armed)
{
    p->armed = armed;
}

bool hitec_prog_series_set(hitec_prog_t *p, uint8_t series)
{
    const hitec_prog_state_t s = hitec_prog_state(p);

    if (series > (uint8_t)HITEC_D || p->owned || p->st != ST_IDLE
        || (s != HITEC_PROG_PWM && s != HITEC_PROG_DOWN
            && s != HITEC_PROG_NO_PAGE && s != HITEC_PROG_SYNC
            && s != HITEC_PROG_OTHER)) {
        return false;
    }
    if (series != p->series) {
        /* What was read belongs to the other series' layout. */
        p->have_servo = false;
    }
    p->series = series;
    (void)hitec_session_init(&p->ses, series, HITEC_SES_CLOCK_MS);
    return true;
}

uint8_t hitec_prog_series(const hitec_prog_t *p)
{
    return p->series;
}

void hitec_prog_supply_set(hitec_prog_t *p, uint16_t mv, uint16_t ma)
{
    p->supply_mv = mv;
    p->supply_ma = ma;
}

void hitec_prog_supply_caps(hitec_prog_t *p, uint16_t max_mv,
                            uint16_t max_ma)
{
    p->caps_mv = max_mv;
    p->caps_ma = max_ma;
}

uint16_t hitec_prog_mv(const hitec_prog_t *p)
{
    return p->supply_mv > HITEC_PROG_MV_MAX ? (uint16_t)HITEC_PROG_MV_MAX
         : p->supply_mv < HITEC_PROG_MV_MIN ? (uint16_t)HITEC_PROG_MV_MIN
                                            : p->supply_mv;
}

uint16_t hitec_prog_ma(const hitec_prog_t *p)
{
    return p->supply_ma > HITEC_PROG_MA_MAX ? (uint16_t)HITEC_PROG_MA_MAX
         : p->supply_ma < HITEC_PROG_MA_MIN ? (uint16_t)HITEC_PROG_MA_MIN
                                            : p->supply_ma;
}

bool hitec_prog_mv_capped(const hitec_prog_t *p)
{
    return p->supply_mv > HITEC_PROG_MV_MAX;
}

/* ------------------------------------------------------------ operations */

/**
 * @brief Whether the bench is armed or asked to arm.
 *
 * @param p The programmer.
 * @return true when it is.
 */
static bool bench_armed(const hitec_prog_t *p)
{
    return p->armed || (p->fed && p->f.armed);
}

/**
 * @brief Whether an operation of the held port may start now.
 *
 * @param p    The programmer.
 * @param safe true for one that sends frames: not with STOP latched, not
 *             armed, not on a STOPPED port.
 * @return HITEC_PROG_MSG_NONE when it may, else why not.
 */
static hitec_prog_msg_id_t held_idle(const hitec_prog_t *p, bool safe)
{
    switch (hitec_prog_state(p)) {
    case HITEC_PROG_DOWN:
        return HITEC_PROG_MSG_DOWN;
    case HITEC_PROG_NO_PAGE:
        return HITEC_PROG_MSG_NO_PAGE;
    case HITEC_PROG_OTHER:
        return HITEC_PROG_MSG_HELD_OTHER;
    case HITEC_PROG_PWM:
        return HITEC_PROG_MSG_NOT_IN_MODE;
    case HITEC_PROG_HELD:
        /* Held and idle: no command of this model is out. */
        break;
    default:
        return HITEC_PROG_MSG_BUSY;
    }
    if (safe && p->f.stopped) {
        return HITEC_PROG_MSG_STOP_LATCHED;
    }
    if (safe && hitec_prog_stopped(p)) {
        return HITEC_PROG_MSG_STOPPED;
    }
    if (safe && bench_armed(p)) {
        return HITEC_PROG_MSG_NEED_DISARM;
    }
    return HITEC_PROG_MSG_NONE;
}

/**
 * @brief What keeps the tester's port from a CONNECT.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE, or why not.
 */
static hitec_prog_msg_id_t from_pwm(const hitec_prog_t *p)
{
    const uint8_t port = (uint8_t)(p->f.target - 1u);
    const bool classic = p->series == (uint8_t)HITEC_CLASSIC;
    const bool power = classic || hitec_prog_rail(p);

    switch (hitec_prog_state(p)) {
    case HITEC_PROG_DOWN:
        return HITEC_PROG_MSG_DOWN;
    case HITEC_PROG_NO_PAGE:
        return HITEC_PROG_MSG_NO_PAGE;
    case HITEC_PROG_OTHER:
        return HITEC_PROG_MSG_HELD_OTHER;
    case HITEC_PROG_PWM:
        /* Idle: no command of this model is out. */
        break;
    default:
        return HITEC_PROG_MSG_BUSY;
    }
    if (p->f.stopped) {
        return HITEC_PROG_MSG_STOP_LATCHED;
    }
    if (bench_armed(p)) {
        return HITEC_PROG_MSG_NEED_DISARM;
    }
    if (p->f.target == 0u || p->f.target > LINK_PG_PORTS_MAX) {
        return HITEC_PROG_MSG_NEED_TARGET;
    }
    if ((p->f.port_pin_known & (1u << port)) == 0u
        || p->f.port_pin[port] > HITEC_PROG_PIN_LAST) {
        return HITEC_PROG_MSG_NO_PIN;
    }
    if (!classic && (p->f.rx_mark == (uint8_t)HITEC_RX_LINK_PENDING
                     || p->f.rx_mark == (uint8_t)HITEC_RX_LINK_REFUSED)) {
        return HITEC_PROG_MSG_RX_PENDING;
    }
    if (classic && !hitec_prog_rail(p)) {
        return HITEC_PROG_MSG_NEED_PD_C;
    }
    if (power && ((p->caps_mv != 0u && hitec_prog_mv(p) > p->caps_mv)
                  || (p->caps_ma != 0u && hitec_prog_ma(p) > p->caps_ma))) {
        return HITEC_PROG_MSG_SUPPLY_CAPS;
    }
    return HITEC_PROG_MSG_NONE;
}

hitec_prog_msg_id_t hitec_prog_connect(hitec_prog_t *p)
{
    hitec_prog_cmd_t c;
    const hitec_prog_msg_id_t m = from_pwm(p);

    if (m != HITEC_PROG_MSG_NONE) {
        return say(p, m, m == HITEC_PROG_MSG_HELD_OTHER ? p->f.rd.port_no
                                                        : 0u);
    }
    p->port = (uint8_t)(p->f.target - 1u);
    p->lost = false;
    p->have_servo = false;
    p->have_last = false;
    p->reenter = false;
    p->mv_open = hitec_prog_mv(p);
    p->ma_open = hitec_prog_ma(p);
    p->msg.id = HITEC_PROG_MSG_NONE;
    ++p->msg.seq;
    memset(&c, 0, sizeof(c));
    c.kind = HITEC_PROG_CMD_OPEN;
    c.port = p->port;
    c.family = p->series;
    c.drive = (uint8_t)HITEC_DRIVE_OPEN_DRAIN;
    p->st = ST_OPENING;
    send(p, &c);
    return HITEC_PROG_MSG_NONE;
}

hitec_prog_msg_id_t hitec_prog_read(hitec_prog_t *p)
{
    hitec_prog_msg_id_t m = held_idle(p, true);

    if (m == HITEC_PROG_MSG_NONE && !p->ses.in_mode) {
        m = HITEC_PROG_MSG_NOT_IN_MODE;
    }
    if (m != HITEC_PROG_MSG_NONE) {
        return say(p, m, m == HITEC_PROG_MSG_HELD_OTHER ? p->f.rd.port_no
                                                        : 0u);
    }
    read_all(p);
    return HITEC_PROG_MSG_NONE;
}

hitec_prog_msg_id_t hitec_prog_leave(hitec_prog_t *p)
{
    const hitec_prog_msg_id_t m = held_idle(p, false);

    if (m != HITEC_PROG_MSG_NONE) {
        return say(p, m, m == HITEC_PROG_MSG_HELD_OTHER ? p->f.rd.port_no
                                                        : 0u);
    }
    /* Held and idle: the session is idle and takes the leave. */
    (void)hitec_session_leave(&p->ses);
    p->st = ST_SES;
    p->op = OP_LEAVE;
    pump(p);
    return HITEC_PROG_MSG_NONE;
}

hitec_prog_msg_id_t hitec_prog_clear(hitec_prog_t *p)
{
    const hitec_prog_msg_id_t m = held_idle(p, false);
    const bool supply_on = p->f.supply_read;

    if (m != HITEC_PROG_MSG_NONE) {
        return say(p, m, m == HITEC_PROG_MSG_HELD_OTHER ? p->f.rd.port_no
                                                        : 0u);
    }
    if (!hitec_prog_stopped(p)) {
        return say(p, HITEC_PROG_MSG_BUSY, 0u);
    }
    /* A STOP latched at this end withholds the heartbeat: the IOmcu would
     * refuse the CLEAR as UNSAFE.  It clears with a switch to ARMED, which
     * a held port refuses. */
    if (p->f.stopped) {
        return say(p, HITEC_PROG_MSG_STOP_LATCHED, 0u);
    }
    /* Where the stop took the supply off, the servo is out of programming
     * mode: the entry again.  Otherwise the read goes on. */
    p->reenter = !p->ses.in_mode
                 || (hitec_prog_rail(p) && !supply_on);
    p->st = ST_CLEARING;
    send_bare(p, HITEC_PROG_CMD_CLEAR, p->port);
    return HITEC_PROG_MSG_NONE;
}

void hitec_prog_abort(hitec_prog_t *p)
{
    if (p->cmd_ready) {
        /* Not handed out: nothing of it ran. */
        p->cmd_ready = false;
        p->wait = W_NONE;
        if (p->st == ST_SES) {
            (void)hitec_session_abort(&p->ses);
        }
        give_up(p);
        (void)say(p, HITEC_PROG_MSG_ABORTED, 0u);
        return;
    }
    if (p->st == ST_SES) {
        if (hitec_session_abort(&p->ses)) {
            p->abort_ready = true;
        }
        pump(p);
        return;
    }
    if (p->wait != W_NONE) {
        p->abort_ready = true;
    }
}

hitec_prog_msg_id_t hitec_prog_release(hitec_prog_t *p)
{
    if (hitec_prog_state(p) != HITEC_PROG_OTHER || p->wait != W_NONE
        || p->cmd_ready) {
        return say(p, HITEC_PROG_MSG_BUSY, 0u);
    }
    p->port = p->f.rd.port_no;
    p->st = ST_RELEASING;
    send_bare(p, HITEC_PROG_CMD_CLOSE, p->port);
    return HITEC_PROG_MSG_NONE;
}

/* --------------------------------------------------------------- queries */

const hitec_prog_msg_t *hitec_prog_message(const hitec_prog_t *p)
{
    return &p->msg;
}

uint8_t hitec_prog_port(const hitec_prog_t *p)
{
    const hitec_link_readout_t *rd = &p->f.rd;

    if (p->owned || p->st != ST_IDLE) {
        return p->port;
    }
    if (p->fed && rd->known && rd->held) {
        return rd->port_no;
    }
    return hitec_prog_on_port(p) ? (uint8_t)(p->f.target - 1u) : 0u;
}

uint8_t hitec_prog_pin(const hitec_prog_t *p)
{
    const hitec_link_readout_t *rd = &p->f.rd;
    const uint8_t port = hitec_prog_port(p);

    if (!p->fed) {
        return HITEC_PROG_NO_PIN;
    }
    if (p->owned && rd->have_line && rd->pin <= HITEC_PROG_PIN_LAST) {
        return (uint8_t)rd->pin;
    }
    if (port < LINK_PG_PORTS_MAX && (p->f.port_pin_known & (1u << port)) != 0u
        && p->f.port_pin[port] <= HITEC_PROG_PIN_LAST) {
        return p->f.port_pin[port];
    }
    return HITEC_PROG_NO_PIN;
}

bool hitec_prog_precharge(const hitec_prog_t *p)
{
    const hitec_link_readout_t *rd = &p->f.rd;

    if (rd->have_line && p->owned) {
        return (rd->line & LINK_HT_LINE_PRECHARGE) != 0u;
    }
    return (rd->flags & LINK_HT_F_PRECHARGE) != 0u;
}

bool hitec_prog_on_port(const hitec_prog_t *p)
{
    return p->fed && p->f.target != 0u && p->f.target <= LINK_PG_PORTS_MAX;
}

bool hitec_prog_port_taken(const hitec_link_readout_t *rd)
{
    switch (rd->phase) {
    case HITEC_LINK_DOWN:
    case HITEC_LINK_NO_PAGE:
        return false;
    case HITEC_LINK_READY:
        return rd->known && rd->held;
    default:
        /* Not read yet, or a request on its way. */
        return true;
    }
}

bool hitec_prog_holds_port(const hitec_prog_t *p)
{
    if (p->st != ST_IDLE || p->wait != W_NONE || p->cmd_ready) {
        return true;
    }
    return p->fed && p->f.rd.phase != HITEC_LINK_DOWN
           && p->f.rd.phase != HITEC_LINK_NO_PAGE && p->f.rd.known
           && p->f.rd.held;
}

bool hitec_prog_in_mode(const hitec_prog_t *p)
{
    return p->owned && p->ses.in_mode != 0u;
}

bool hitec_prog_stopped(const hitec_prog_t *p)
{
    return p->fed && p->f.rd.known && p->f.rd.held && p->f.rd.stopped;
}

bool hitec_prog_rail(const hitec_prog_t *p)
{
    return p->fed && (p->f.rd.flags & LINK_HT_F_RAIL) != 0u;
}

const hitec_image_t *hitec_prog_servo(const hitec_prog_t *p)
{
    return p->have_servo ? &p->servo : NULL;
}

const hitec_model_t *hitec_prog_model(const hitec_prog_t *p)
{
    return p->have_servo ? &p->model : NULL;
}

uint8_t hitec_prog_layout(const hitec_prog_t *p)
{
    if (p->have_servo) {
        return p->model.layout;
    }
    return p->series == (uint8_t)HITEC_D ? (uint8_t)HITEC_LAYOUT_D
                                         : (uint8_t)HITEC_LAYOUT_HS5;
}

bool hitec_prog_progress(const hitec_prog_t *p, unsigned *done,
                         unsigned *total)
{
    const bool reading = p->st == ST_SES && p->op == OP_READ;

    *done = reading ? p->ses.done : 0u;
    *total = reading ? p->ses.total : 0u;
    return reading;
}

unsigned hitec_prog_retry_code(const hitec_prog_t *p)
{
    if (p->st != ST_SES || p->ses.tries == 0u
        || p->ses.last_rx == (uint8_t)HITEC_RX_OK) {
        return 0u;
    }
    return p->ses.last_rx;
}
