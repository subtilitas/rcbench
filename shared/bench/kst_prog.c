/*
 * The KST servo programmer's state: see kst_prog.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_prog.h"

#include <string.h>

/* What is awaited. */
enum {
    W_NONE = 0,
    W_SAVE,      /* the servo's file, before the first write      */
    W_FILE,      /* the journal's file, before the request goes   */
    W_ANSWER,    /* the control task's answer to the command      */
    W_OUTCOME,   /* the operation's outcome                       */
};

/* What a write of the file is for. */
enum {
    FILE_NONE = 0,
    FILE_KEEP,     /* the state changed; nobody waits            */
    FILE_JOURNAL,  /* a write to the servo waits                 */
};

static const char k_head[] = "RCBENCH KST 1\n";

/* ----------------------------------------------------------------- helpers */

static bool same(const kst_image_t *a, const kst_image_t *b)
{
    return memcmp(a->r, b->r, sizeof(a->r)) == 0;
}

static bool is_write(unsigned op)
{
    return op == LINK_KST_OP_WRITE || op == LINK_KST_OP_RESTORE
        || op == LINK_KST_OP_RELEASE;
}

static unsigned lowest(uint64_t set)
{
    unsigned n = 0;

    while (n < 64u && ((set >> n) & 1u) == 0u) {
        n++;
    }
    return n;
}

static unsigned ones(uint64_t set)
{
    unsigned n = 0;

    for (; set != 0u; set &= set - 1u) {
        n++;
    }
    return n;
}

static kst_prog_level_t level_of(unsigned id)
{
    switch (id) {
    case KST_PROG_MSG_NONE:
    case KST_PROG_MSG_READ:
    case KST_PROG_MSG_WRITTEN:
    case KST_PROG_MSG_VERIFIED:
    case KST_PROG_MSG_RESTORED:
    case KST_PROG_MSG_RELEASED:
    case KST_PROG_MSG_LEFT:
    case KST_PROG_MSG_REBOOTED:
    case KST_PROG_MSG_SAVED:
    case KST_PROG_MSG_LOADED:
    case KST_PROG_MSG_LOADED_EDITED:
    case KST_PROG_MSG_LOADED_UNTYPED:
    case KST_PROG_MSG_LOAD_SAME:
    case KST_PROG_MSG_JOURNAL_SAME:
    case KST_PROG_MSG_JOURNAL_DONE:
    case KST_PROG_MSG_FORGOTTEN:
        return KST_PROG_INFO;
    case KST_PROG_MSG_JOURNAL:
    case KST_PROG_MSG_JOURNAL_OTHER:
    case KST_PROG_MSG_JOURNAL_DIFF:
    case KST_PROG_MSG_SES + KST_SES_ERR_LOCKED:
    case KST_PROG_MSG_SES + KST_SES_ERR_TORN:
    case KST_PROG_MSG_SES + KST_SES_ERR_UNINTENDED:
    case KST_PROG_MSG_SES + KST_SES_ERR_VERIFY:
        return KST_PROG_DANGER;
    default:
        return KST_PROG_WARN;
    }
}

static kst_prog_msg_id_t say_reg(kst_prog_t *p, unsigned id, unsigned arg,
                                 unsigned reg)
{
    p->msg.id = (uint8_t)id;
    p->msg.level = (uint8_t)level_of(id);
    p->msg.arg = (uint16_t)arg;
    p->msg.reg = (uint8_t)reg;
    p->msg.seq++;
    return (kst_prog_msg_id_t)id;
}

static kst_prog_msg_id_t say(kst_prog_t *p, unsigned id, unsigned arg)
{
    return say_reg(p, id, arg, 0xFFu);
}

static void file_want(kst_prog_t *p, uint8_t what)
{
    p->file_wanted = true;
    if (what > p->file_next) {
        p->file_next = what;
    }
}

static void journal_clear(kst_prog_t *p)
{
    if (p->journal) {
        p->journal = false;
        p->journal_open = false;
        p->journal_other = false;
        file_want(p, FILE_KEEP);
    }
}

static bool flag(const kst_prog_t *p, uint16_t f)
{
    return p->fed && p->f.rd.known && (p->f.rd.flags & f) != 0u;
}

/* The session no longer stands: the port is in PWM, the link went, or the
 * coprocessor started again. */
static void session_gone(kst_prog_t *p)
{
    p->have_servo = false;
    p->fp_ok = false;
    p->need_read = false;
    p->write_enabled = false;
    p->written = false;
    p->backed_up = false;
    p->no_file = false;
    p->soft = false;
    p->soft_load = false;
    p->load_skipped = 0;
    p->journal_other = false;
}

/* -------------------------------------------------------------- the fields */

static bool editable(kst_field_id_t id)
{
    const kst_field_t *f = kst_field(id);

    return f != NULL && f->access == KST_ACCESS_EDITABLE
        && id != KST_F_DUTY_COPY;
}

static kst_field_set_t differ(const kst_image_t *a, const kst_image_t *b)
{
    kst_field_set_t set = 0;

    for (unsigned id = 0; id < KST_F_COUNT; ++id) {
        if (id != KST_F_DUTY_COPY
            && kst_field_get(a, (kst_field_id_t)id)
                   != kst_field_get(b, (kst_field_id_t)id)) {
            set |= KST_FIELD_BIT(id);
        }
    }
    return set;
}

/* The hard rules @p target breaks as an edit of the servo, beyond those the
 * servo's own image breaks already. */
static kst_rules_t hard_new(const kst_prog_t *p, const kst_image_t *target)
{
    const kst_rules_t was = kst_limits_edit(&p->servo, &p->servo, 0, NULL);

    return kst_limits_edit(&p->servo, target, 0, NULL) & KST_RULES_HARD
         & ~was;
}

/* The soft rules @p target breaks beyond those the servo's image breaks. */
static kst_rules_t soft_new(const kst_prog_t *p, const kst_image_t *target)
{
    return kst_limits_edit(&p->servo, target, 0, NULL) & ~KST_RULES_HARD
         & ~kst_limits_edit(&p->servo, &p->servo, 0, NULL);
}

static bool raw_taken(const kst_prog_t *p, kst_field_id_t id, uint16_t raw)
{
    kst_image_t trial = p->draft;

    return kst_field_edit(&trial, id, raw) && hard_new(p, &trial) == 0u;
}

/* ------------------------------------------------------------- the requests */

static void send(kst_prog_t *p, const kst_link_request_t *req)
{
    if (++p->next_id == 0u) {
        p->next_id = 1u;
    }
    p->cmd.kind = KST_PROG_CMD_START;
    p->cmd.id = p->next_id;
    p->cmd.req = *req;
    p->cmd_ready = true;
    p->wait = W_ANSWER;
    p->wait_id = p->next_id;
    p->wait_ms = p->f.now_ms;
    p->op = (uint8_t)req->op;
}

static kst_link_request_t request(const kst_prog_t *p, link_kst_op_t op)
{
    kst_link_request_t r;

    memset(&r, 0, sizeof(r));
    r.op = op;
    r.port = p->port;
    if (op == LINK_KST_OP_ENTER) {
        r.supply_mv = p->supply_mv;
        r.supply_ma = p->supply_ma;
    }
    r.write_enabled = p->write_enabled;
    return r;
}

static bool waits_unsent(const kst_prog_t *p)
{
    return p->wait == W_SAVE || p->wait == W_FILE
        || (p->wait == W_ANSWER && p->cmd_ready);
}

static void card_ask(kst_prog_t *p, kst_prog_card_op_t op, const char *name)
{
    if (++p->card_id == 0u) {
        p->card_id = 1u;
    }
    p->card_op = (uint8_t)op;
    p->card_ready = true;
    p->card_ms = p->f.now_ms;
    memset(p->card_name, 0, sizeof(p->card_name));
    if (name != NULL) {
        strncpy(p->card_name, name, sizeof(p->card_name) - 1u);
    }
}

/* The write that is held: the journal, its file when a card takes one, and
 * then the request. */
static void write_go(kst_prog_t *p)
{
    if (p->have_servo) {
        if (!p->journal) {
            p->journal_img = p->servo;
        }
        p->journal = true;
        p->journal_open = true;
        p->journal_port = p->port;
        p->journal_target = p->held_target;
    }
    if (p->journal && p->f.card) {
        p->wait = W_FILE;
        p->wait_ms = p->f.now_ms;
        file_want(p, FILE_JOURNAL);
    } else {
        send(p, &p->held);
    }
}

/*
 * A write to the servo.  The first of a session to a servo whose image is
 * known waits for that image in a file.  Without a card it goes with no
 * file, and the session's backup is the one copy of the servo as found.
 * A servo in doubt has no image worth a file: the session's restore, the
 * one write it takes, goes without.
 */
static void send_write(kst_prog_t *p, const kst_link_request_t *req,
                       const kst_image_t *target)
{
    const bool save = kst_prog_save_first(p) && p->f.card;

    p->held = *req;
    p->held_target = *target;
    p->held_no_file = kst_prog_save_first(p) && !p->f.card;
    p->op = (uint8_t)req->op;
    if (save) {
        p->wait = W_SAVE;
        p->wait_ms = p->f.now_ms;
        p->card_auto = true;
        card_ask(p, KST_PROG_CARD_SAVE, NULL);
    } else {
        write_go(p);
    }
}

/* Whether an operation may start now; the message when not. */
static kst_prog_msg_id_t idle(const kst_prog_t *p)
{
    if (p->wait != W_NONE || p->cmd_ready || p->card_op != KST_PROG_CARD_IDLE) {
        return (kst_prog_msg_id_t)(KST_PROG_MSG_START + KST_LINK_START_BUSY);
    }
    switch (kst_prog_state(p)) {
    case KST_PROG_DOWN:
        return (kst_prog_msg_id_t)(KST_PROG_MSG_START + KST_LINK_START_DOWN);
    case KST_PROG_NO_PAGE:
        return (kst_prog_msg_id_t)(KST_PROG_MSG_START
                                   + KST_LINK_START_NO_PAGE);
    case KST_PROG_PWM:
        return KST_PROG_MSG_NEED_PORT;
    case KST_PROG_PROGRAMMING:
        return KST_PROG_MSG_NONE;
    default:
        return (kst_prog_msg_id_t)(KST_PROG_MSG_START + KST_LINK_START_BUSY);
    }
}

/* The same for an operation that sends frames: not after STOP. */
static kst_prog_msg_id_t idle_safe(const kst_prog_t *p)
{
    const kst_prog_msg_id_t m = idle(p);

    if (m == KST_PROG_MSG_NONE && p->f.stopped) {
        return KST_PROG_MSG_STOPPED;
    }
    return m;
}

/* ---------------------------------------------------------------- outcomes */

static void adopt(kst_prog_t *p)
{
    const bool had = p->have_servo;
    const kst_image_t was = p->servo;

    p->servo = p->f.image;
    p->draft = p->servo;
    p->have_servo = true;
    p->need_read = false;
    p->soft = false;
    p->soft_load = false;
    p->load_skipped = 0;
    p->fp_ok = kst_fingerprint(&p->servo, NULL)
            && (p->f.rd.flags & LINK_KS_F_FP_OK) != 0u;
    if ((had && !same(&was, &p->servo)) || !p->fp_ok) {
        /* Another servo, or one that is not written to. */
        p->write_enabled = false;
    }
    if (had && !same(&was, &p->servo)) {
        p->backed_up = false;
        p->no_file = false;
    }
}

/*
 * Whether the servo read can be the one the journal records: on its
 * channel, and equal to it in every field that the interrupted write left
 * alone and that no edit reaches.  A register a plan writes can hold a
 * third value (kst_plan.h), so the fields the write changes say nothing.
 */
static bool journal_fits(const kst_prog_t *p)
{
    if (p->port != p->journal_port) {
        return false;
    }
    for (unsigned id = 0; id < KST_F_COUNT; ++id) {
        const kst_field_id_t fid = (kst_field_id_t)id;
        const uint16_t was = kst_field_get(&p->journal_img, fid);

        if (id != KST_F_DUTY_COPY && !editable(fid)
            && was == kst_field_get(&p->journal_target, fid)
            && was != kst_field_get(&p->servo, fid)) {
            return false;
        }
    }
    return true;
}

static void read_done(kst_prog_t *p)
{
    if (!p->f.have_image) {
        (void)say(p, KST_PROG_MSG_SES + KST_SES_ERR_READ, 0);
        return;
    }
    adopt(p);
    p->journal_other = p->journal && !journal_fits(p);
    if (p->journal_other) {
        /* Another servo: the journal stands, and nothing goes to this
         * one. */
        p->journal_open = true;
        p->write_enabled = false;
        (void)say(p, KST_PROG_MSG_JOURNAL_OTHER, p->journal_port + 1u);
    } else if (p->journal && same(&p->servo, &p->journal_img)) {
        journal_clear(p);
        (void)say(p, KST_PROG_MSG_JOURNAL_SAME, 0);
    } else if (p->journal && same(&p->servo, &p->journal_target)) {
        journal_clear(p);
        (void)say(p, KST_PROG_MSG_JOURNAL_DONE, 0);
    } else if (p->journal && p->journal_open) {
        p->journal_open = false;
        (void)say(p, KST_PROG_MSG_JOURNAL_DIFF, 0);
    } else if ((p->f.rd.flags & LINK_KS_F_LOCKED) != 0u) {
        /* Read, and still the session's restore is all that is taken. */
        (void)say(p, KST_PROG_MSG_SES + KST_SES_ERR_LOCKED, 0);
    } else {
        (void)say(p, p->fp_ok ? KST_PROG_MSG_READ : KST_PROG_MSG_READ_ONLY,
                  0);
    }
}

static void done_ok(kst_prog_t *p, unsigned op)
{
    switch (op) {
    case LINK_KST_OP_ENTER:
        if (p->chain_read) {
            const kst_link_request_t r = request(p, LINK_KST_OP_READ_ALL);

            p->chain_read = false;
            send(p, &r);
        }
        break;
    case LINK_KST_OP_READ_ALL:
        read_done(p);
        break;
    case LINK_KST_OP_WRITE:
    case LINK_KST_OP_RESTORE:
    case LINK_KST_OP_RELEASE:
        journal_clear(p);
        if (p->f.have_image) {
            const bool on = p->write_enabled;
            const bool kept = p->backed_up;
            const bool none = p->no_file;

            /* The file holds the servo as the session found it; so does
             * the lack of one. */
            adopt(p);
            p->write_enabled = on && p->fp_ok;
            p->backed_up = kept;
            p->no_file = none;
        } else {
            p->need_read = true;
        }
        if (op == LINK_KST_OP_RELEASE) {
            (void)say(p, KST_PROG_MSG_RELEASED, 0);
        } else if (op == LINK_KST_OP_RESTORE
                   || p->restoring != KST_PROG_FROM_NONE) {
            (void)say(p, KST_PROG_MSG_RESTORED, 0);
        } else {
            (void)say(p, KST_PROG_MSG_WRITTEN, p->writes);
        }
        break;
    case LINK_KST_OP_VERIFY:
        (void)say(p, KST_PROG_MSG_VERIFIED, 0);
        break;
    case LINK_KST_OP_POWER_CYCLED:
        session_gone(p);
        (void)say(p, KST_PROG_MSG_LEFT, 0);
        break;
    case LINK_KST_OP_REBOOT:
        session_gone(p);
        (void)say(p, KST_PROG_MSG_REBOOTED, 0);
        break;
    default:
        break;
    }
}

/* A write that ended without the servo verified. */
static void write_failed(kst_prog_t *p, const kst_link_outcome_t *o)
{
    bool nothing = false;   /* no register was written */

    switch (o->kind) {
    case KST_LINK_OUT_REFUSED:
    case KST_LINK_OUT_NACK:
    case KST_LINK_OUT_DROPPED:
        nothing = true;
        break;
    case KST_LINK_OUT_DONE:
        nothing = o->steps_done == 0u
               && (o->result == KST_SES_ERR_BUSY
                   || o->result == KST_SES_ERR_ARG
                   || o->result == KST_SES_ERR_NOT_IN_MODE
                   || o->result == KST_SES_ERR_NO_BACKUP
                   || o->result == KST_SES_ERR_FINGERPRINT
                   || o->result == KST_SES_ERR_LOCKED
                   || o->result == KST_SES_ERR_BAD_PLAN
                   || o->result == KST_SES_ERR_UNCONFIRMED
                   || o->result == KST_SES_ERR_CHANGED
                   || o->result == KST_SES_ERR_WRITE_FAILED);
        break;
    default:
        break;
    }
    if (nothing) {
        if (p->journal && same(&p->journal_img, &p->servo)) {
            journal_clear(p);
        } else {
            /* The journal of an earlier write stands as it was. */
            p->journal_open = false;
        }
        if (o->kind == KST_LINK_OUT_DONE
            && o->result == KST_SES_ERR_CHANGED) {
            p->need_read = true;
        }
    } else {
        p->need_read = true;
    }
}

static void outcome(kst_prog_t *p, const kst_link_outcome_t *o)
{
    const unsigned op = o->op;
    bool said = false;

    p->wait = W_NONE;
    p->cmd_ready = false;
    if (is_write(op)
        && !(o->kind == KST_LINK_OUT_DONE && o->result == KST_SES_OK)) {
        write_failed(p, o);
        if (p->journal && p->journal_open
            && (o->kind == KST_LINK_OUT_NO_ANSWER
                || o->kind == KST_LINK_OUT_RESTARTED)) {
            (void)say(p, KST_PROG_MSG_JOURNAL, p->journal_port + 1u);
            said = true;
        }
    }
    if (op != LINK_KST_OP_ENTER || o->kind != KST_LINK_OUT_DONE
        || o->result != KST_SES_OK) {
        p->chain_read = false;
    }
    if (o->kind == KST_LINK_OUT_NO_ANSWER
        && (op == LINK_KST_OP_READ_ALL || op == LINK_KST_OP_VERIFY)) {
        /* It may have run, with an image this side never takes. */
        p->need_read = true;
    }
    if (o->kind == KST_LINK_OUT_RESTARTED) {
        session_gone(p);
    }
    if (said) {
        return;
    }
    switch (o->kind) {
    case KST_LINK_OUT_DONE:
        if (o->result == KST_SES_OK) {
            done_ok(p, op);
        } else if (op == LINK_KST_OP_VERIFY
                   && o->result == KST_SES_ERR_VERIFY) {
            p->need_read = true;
            (void)say(p, KST_PROG_MSG_VERIFY_DIFF, ones(o->diff_regs));
        } else {
            if (op == LINK_KST_OP_READ_ALL) {
                p->need_read = p->have_servo;
            }
            (void)say_reg(p, KST_PROG_MSG_SES + (unsigned)o->result,
                          o->steps_done, o->fail_reg);
        }
        break;
    case KST_LINK_OUT_REFUSED:
        (void)say(p, KST_PROG_MSG_REF + (unsigned)o->refusal, 0);
        break;
    case KST_LINK_OUT_NACK:
        (void)say(p, KST_PROG_MSG_OUT + (unsigned)o->kind, o->nack);
        break;
    default:
        (void)say(p, KST_PROG_MSG_OUT + (unsigned)o->kind, 0);
        break;
    }
}

/* The command was not taken, or nothing answers: nothing was sent. */
static void not_started(kst_prog_t *p, unsigned id, unsigned arg)
{
    kst_link_outcome_t o;

    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DROPPED;
    o.op = (link_kst_op_t)p->op;
    if (is_write(p->op)) {
        write_failed(p, &o);
    }
    p->wait = W_NONE;
    p->cmd_ready = false;
    p->chain_read = false;
    (void)say(p, id, arg);
}

/* ------------------------------------------------------------------- public */

void kst_prog_init(kst_prog_t *p)
{
    memset(p, 0, sizeof(*p));
    p->msg.reg = 0xFFu;
    p->supply_mv = KST_PROG_SUPPLY_MV;
    p->supply_ma = KST_PROG_SUPPLY_MA;
}

void kst_prog_screen_entry(kst_prog_t *p)
{
    p->write_enabled = false;
}

void kst_prog_feed(kst_prog_t *p, const kst_prog_feed_t *f)
{
    const bool was_fed = p->fed;
    const bool was_stopped = p->fed && p->f.stopped;
    const bool was_up = p->fed && p->f.rd.phase != KST_LINK_DOWN
                     && p->f.rd.phase != KST_LINK_NO_PAGE;
    bool up;

    p->f = *f;
    p->fed = true;
    if (!was_fed) {
        p->outcomes_seen = f->outcomes;
    }
    up = f->rd.phase != KST_LINK_DOWN && f->rd.phase != KST_LINK_NO_PAGE;

    if (p->wait == W_ANSWER && f->answer_id == p->wait_id) {
        if (f->answer == KST_LINK_START_OK) {
            p->wait = W_OUTCOME;
            p->wait_ms = f->now_ms;
            /* The first write of the session is one that started: a write
             * the card, a stop or the link kept from starting is none. */
            p->written = p->written || is_write(p->op);
            /* And one that started with no card has no file. */
            p->no_file = p->no_file || (is_write(p->op) && p->held_no_file);
        } else {
            not_started(p, KST_PROG_MSG_START + (unsigned)f->answer, 0);
        }
    }
    if (f->outcomes != p->outcomes_seen) {
        p->outcomes_seen = f->outcomes;
        if (p->wait == W_OUTCOME || is_write(f->outcome.op)
            || f->outcome.kind == KST_LINK_OUT_RESTARTED) {
            outcome(p, &f->outcome);
        }
    }
    if (was_up && !up) {
        if (p->wait != W_NONE) {
            kst_link_outcome_t o;

            memset(&o, 0, sizeof(o));
            o.kind = KST_LINK_OUT_NO_ANSWER;
            o.op = (link_kst_op_t)p->op;
            if (p->wait == W_OUTCOME) {
                outcome(p, &o);
            } else {
                not_started(p, KST_PROG_MSG_OUT + KST_LINK_OUT_NO_ANSWER, 0);
            }
        }
        session_gone(p);
    }
    if (f->stopped && !was_stopped) {
        p->write_enabled = false;
        if (waits_unsent(p)) {
            not_started(p, KST_PROG_MSG_STOPPED, 0);
        }
    }

    if (p->wait == W_ANSWER
        && (uint32_t)(f->now_ms - p->wait_ms) >= KST_PROG_START_MS) {
        not_started(p, KST_PROG_MSG_TIMEOUT, 0);
    } else if (p->wait == W_FILE
               && (uint32_t)(f->now_ms - p->wait_ms) >= KST_PROG_STORE_MS) {
        not_started(p, KST_PROG_MSG_STORE_FAILED, 0);
    } else if (p->wait == W_OUTCOME
               && (uint32_t)(f->now_ms - p->wait_ms) >= KST_PROG_RUN_MS) {
        /* The operation may still end on the coprocessor, a read with an
         * image this side never takes: nothing is edited or written
         * before a read that ends. */
        p->need_read = true;
        p->wait = W_NONE;
        p->chain_read = false;
        p->cmd.kind = KST_PROG_CMD_ABORT;
        p->cmd.id = 0;
        p->cmd_ready = true;
        (void)say(p, KST_PROG_MSG_TIMEOUT, 0);
    }

    if (p->card_op != KST_PROG_CARD_IDLE
        && (uint32_t)(f->now_ms - p->card_ms) >= KST_PROG_CARD_MS) {
        kst_prog_card_done_t late;

        memset(&late, 0, sizeof(late));
        late.id = p->card_id;
        late.result = p->card_op == KST_PROG_CARD_SAVE
                          ? KST_PROG_CARD_WRITE_ERR : KST_PROG_CARD_READ_ERR;
        kst_prog_card_done(p, &late);
    }

    if (p->file_retry
        && (uint32_t)(f->now_ms - p->file_retry_ms)
               >= KST_PROG_FILE_RETRY_MS) {
        p->file_retry = false;
        file_want(p, FILE_KEEP);
    }

    if (up && f->rd.known && p->wait == W_NONE) {
        if (f->rd.port == LINK_KST_PWM) {
            if (p->have_servo || p->write_enabled || p->written
                || p->need_read) {
                session_gone(p);
            }
        } else {
            p->port = f->rd.port_no;
        }
    }
    /* A panel that started on a held port: the images the port holds are
     * fetched, the backup for the session's restore among them. */
    if (kst_prog_state(p) == KST_PROG_PROGRAMMING
        && f->rd.phase == KST_LINK_READY
        && (((f->rd.flags & LINK_KS_F_IMAGE) != 0u && !f->have_image)
            || ((f->rd.flags & LINK_KS_F_BACKUP) != 0u && !f->have_backup))
        && (!p->fetched
            || (uint32_t)(f->now_ms - p->fetch_ms) >= KST_PROG_FETCH_MS)) {
        p->fetched = true;
        p->fetch_ms = f->now_ms;
        p->cmd_fetch = true;
    }
}

bool kst_prog_take(kst_prog_t *p, kst_prog_cmd_t *out)
{
    if (!p->cmd_ready) {
        if (p->cmd_fetch) {
            p->cmd_fetch = false;
            memset(out, 0, sizeof(*out));
            out->kind = KST_PROG_CMD_FETCH;
            return true;
        }
        return false;
    }
    p->cmd_fetch = false;
    *out = p->cmd;
    p->cmd_ready = false;
    return true;
}

kst_prog_state_t kst_prog_state(const kst_prog_t *p)
{
    if (!p->fed || p->f.rd.phase == KST_LINK_DOWN) {
        return KST_PROG_DOWN;
    }
    if (p->f.rd.phase == KST_LINK_NO_PAGE) {
        return KST_PROG_NO_PAGE;
    }
    if (!p->f.rd.known || p->f.rd.phase == KST_LINK_SYNC) {
        return KST_PROG_SYNC;
    }
    if (p->wait != W_NONE || p->cmd_ready) {
        if (p->op == LINK_KST_OP_ENTER) {
            return KST_PROG_ENTERING;
        }
        return p->op == LINK_KST_OP_POWER_CYCLED
                    || p->op == LINK_KST_OP_REBOOT ? KST_PROG_LEAVING
                                                   : KST_PROG_WORKING;
    }
    switch (p->f.rd.port) {
    case LINK_KST_PWM:
        return KST_PROG_PWM;
    case LINK_KST_STOPPING:
    case LINK_KST_LOW:
        return KST_PROG_ENTERING;
    case LINK_KST_PROGRAMMING:
        return (p->f.rd.flags & LINK_KS_F_BUSY) != 0u ? KST_PROG_WORKING
                                                      : KST_PROG_PROGRAMMING;
    default:
        return KST_PROG_LEAVING;
    }
}

link_kst_op_t kst_prog_op(const kst_prog_t *p)
{
    return p->wait != W_NONE || p->cmd_ready ? (link_kst_op_t)p->op
                                             : LINK_KST_OP_NONE;
}

const kst_prog_msg_t *kst_prog_message(const kst_prog_t *p)
{
    return &p->msg;
}

bool kst_prog_have_servo(const kst_prog_t *p)
{
    return p->have_servo;
}

bool kst_prog_editable(const kst_prog_t *p)
{
    return p->have_servo && p->fp_ok && !kst_prog_in_doubt(p);
}

const kst_image_t *kst_prog_servo(const kst_prog_t *p)
{
    return &p->servo;
}

const kst_image_t *kst_prog_draft(const kst_prog_t *p)
{
    return &p->draft;
}

bool kst_prog_write_enabled(const kst_prog_t *p)
{
    return p->write_enabled;
}

uint8_t kst_prog_port(const kst_prog_t *p)
{
    return p->port;
}

bool kst_prog_journal(const kst_prog_t *p)
{
    return p->journal && p->journal_open;
}

bool kst_prog_journal_other(const kst_prog_t *p)
{
    return p->journal && p->journal_other;
}

uint8_t kst_prog_journal_port(const kst_prog_t *p)
{
    return p->journal_port;
}

bool kst_prog_in_doubt(const kst_prog_t *p)
{
    return kst_prog_journal(p) || p->need_read
        || flag(p, LINK_KS_F_MUST_READ) || flag(p, LINK_KS_F_LOCKED);
}

bool kst_prog_first_write(const kst_prog_t *p)
{
    return !p->written;
}

bool kst_prog_backed_up(const kst_prog_t *p)
{
    return p->backed_up;
}

const char *kst_prog_backup_name(const kst_prog_t *p)
{
    return p->backed_up ? p->backup : "";
}

bool kst_prog_no_file(const kst_prog_t *p)
{
    return p->no_file && !p->backed_up;
}

/* The image the file before a write takes.  After a write that went with
 * no card the servo no longer holds what the session found: the session's
 * backup does, while the coprocessor keeps it. */
static const kst_image_t *file_image(const kst_prog_t *p)
{
    return p->card_auto && p->no_file && p->f.have_backup ? &p->f.backup
                                                          : &p->servo;
}

bool kst_prog_save_first(const kst_prog_t *p)
{
    return p->have_servo && !p->backed_up && !kst_prog_in_doubt(p);
}

kst_field_set_t kst_prog_changed(const kst_prog_t *p)
{
    return p->have_servo ? differ(&p->servo, &p->draft) : 0u;
}

unsigned kst_prog_changes(const kst_prog_t *p)
{
    return ones(kst_prog_changed(p));
}

kst_rules_t kst_prog_rules(const kst_prog_t *p)
{
    return p->have_servo ? kst_limits_edit(&p->servo, &p->draft, 0, NULL)
                         : 0u;
}

/* ------------------------------------------------------------------ editing */

bool kst_prog_range(const kst_prog_t *p, kst_field_id_t id, uint16_t *lo,
                    uint16_t *hi)
{
    const uint16_t max = kst_field_raw_max(id);
    const uint16_t pitch = (uint16_t)(max / 256u + 1u);
    uint32_t good = kst_field_get(&p->draft, id);
    uint32_t a, b;

    if (!p->have_servo || !editable(id)) {
        return false;
    }
    if (!raw_taken(p, id, (uint16_t)good)) {
        for (good = 0; good <= max; good += pitch) {
            if (raw_taken(p, id, (uint16_t)good)) {
                break;
            }
        }
        if (good > max) {
            return false;
        }
    }
    /* The rules are thresholds, so the values taken are one run. */
    a = 0;
    b = good;
    while (a < b) {
        const uint32_t mid = (a + b) / 2u;

        if (raw_taken(p, id, (uint16_t)mid)) {
            b = mid;
        } else {
            a = mid + 1u;
        }
    }
    *lo = (uint16_t)a;
    a = good;
    b = max;
    while (a < b) {
        const uint32_t mid = (a + b + 1u) / 2u;

        if (raw_taken(p, id, (uint16_t)mid)) {
            a = mid;
        } else {
            b = mid - 1u;
        }
    }
    *hi = (uint16_t)a;
    return true;
}

kst_prog_edit_t kst_prog_edit(kst_prog_t *p, kst_field_id_t id, uint16_t raw)
{
    kst_image_t trial = p->draft;
    kst_rules_t hard, soft;

    p->soft = false;
    p->soft_load = false;
    if (!kst_prog_editable(p)) {
        (void)say(p, p->have_servo && !p->fp_ok ? KST_PROG_MSG_READ_ONLY
                                                 : KST_PROG_MSG_NEED_READ,
                  0);
        return KST_PROG_EDIT_REFUSED;
    }
    if (!editable(id)) {
        (void)say(p, KST_PROG_MSG_EDIT_LOCKED, 0);
        return KST_PROG_EDIT_REFUSED;
    }
    if (!kst_field_edit(&trial, id, raw)) {
        (void)say(p, KST_PROG_MSG_EDIT_HARD, KST_RULE_COUNT);
        return KST_PROG_EDIT_REFUSED;
    }
    hard = hard_new(p, &trial);
    if (hard != 0u) {
        (void)say(p, KST_PROG_MSG_EDIT_HARD, lowest(hard));
        return KST_PROG_EDIT_REFUSED;
    }
    soft = kst_limits_edit(&p->servo, &trial, 0, NULL) & ~KST_RULES_HARD
         & ~kst_limits_edit(&p->servo, &p->draft, 0, NULL);
    if (soft != 0u) {
        p->soft = true;
        p->soft_img = trial;
        p->soft_rules = soft;
        return KST_PROG_EDIT_SOFT;
    }
    p->draft = trial;
    return KST_PROG_EDIT_OK;
}

kst_prog_edit_t kst_prog_step(kst_prog_t *p, kst_field_id_t id, int dir)
{
    const uint16_t raw = kst_field_get(&p->draft, id);

    if ((dir > 0 && raw >= kst_field_raw_max(id)) || (dir <= 0 && raw == 0u)) {
        p->soft = false;
        (void)say(p, KST_PROG_MSG_EDIT_HARD, KST_RULE_COUNT);
        return KST_PROG_EDIT_REFUSED;
    }
    return kst_prog_edit(p, id, (uint16_t)(dir > 0 ? raw + 1u : raw - 1u));
}

kst_rules_t kst_prog_soft_rules(const kst_prog_t *p)
{
    return p->soft ? p->soft_rules : 0u;
}

const kst_image_t *kst_prog_soft_image(const kst_prog_t *p)
{
    return &p->soft_img;
}

static unsigned loaded_message(const kst_prog_t *p)
{
    switch (p->load_kind) {
    case KST_PROG_FILE_EDITED:
        return KST_PROG_MSG_LOADED_EDITED;
    case KST_PROG_FILE_UNTYPED:
        return KST_PROG_MSG_LOADED_UNTYPED;
    default:
        return KST_PROG_MSG_LOADED;
    }
}

/* The draft takes the file's target. */
static void load_take(kst_prog_t *p, const kst_image_t *target)
{
    const unsigned n = ones(differ(&p->servo, target));

    p->draft = *target;
    p->load_skipped = p->soft_skipped;
    (void)say(p, loaded_message(p), n | (ones(p->load_skipped) << 8));
}

void kst_prog_soft_confirm(kst_prog_t *p)
{
    if (p->soft) {
        p->soft = false;
        if (p->soft_load) {
            p->soft_load = false;
            load_take(p, &p->soft_img);
        } else {
            p->draft = p->soft_img;
        }
    }
}

void kst_prog_soft_cancel(kst_prog_t *p)
{
    p->soft = false;
    p->soft_load = false;
}

bool kst_prog_soft_is_load(const kst_prog_t *p)
{
    return p->soft && p->soft_load;
}

void kst_prog_discard(kst_prog_t *p)
{
    p->soft = false;
    p->soft_load = false;
    p->load_skipped = 0;
    if (p->have_servo) {
        p->draft = p->servo;
    }
}

/* --------------------------------------------------------------- operations */

/* What keeps the tester's port from an ENTER or a REBOOT. */
static kst_prog_msg_id_t from_pwm(const kst_prog_t *p, uint8_t port)
{
    if (p->f.stopped) {
        return KST_PROG_MSG_STOPPED;
    }
    return p->f.target == port + 1u ? KST_PROG_MSG_NONE
                                    : KST_PROG_MSG_NEED_TARGET;
}

kst_prog_msg_id_t kst_prog_connect(kst_prog_t *p, uint8_t port)
{
    kst_link_request_t r;
    kst_prog_msg_id_t m = idle(p);

    if (m == KST_PROG_MSG_NONE) {
        /* The port is in programming mode already. */
        m = (kst_prog_msg_id_t)(KST_PROG_MSG_START + KST_LINK_START_BUSY);
    } else if (m == KST_PROG_MSG_NEED_PORT) {
        m = from_pwm(p, port);
    }
    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, 0);
    }
    session_gone(p);
    p->port = port;
    p->chain_read = true;
    r = request(p, LINK_KST_OP_ENTER);
    send(p, &r);
    return KST_PROG_MSG_NONE;
}

kst_prog_msg_id_t kst_prog_reboot(kst_prog_t *p, uint8_t port)
{
    kst_link_request_t r;
    kst_prog_msg_id_t m = idle(p);

    if (m == KST_PROG_MSG_NEED_PORT) {
        m = from_pwm(p, port);
        if (m == KST_PROG_MSG_NONE) {
            p->port = port;
        }
    }
    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, 0);
    }
    r = request(p, LINK_KST_OP_REBOOT);
    send(p, &r);
    return KST_PROG_MSG_NONE;
}

void kst_prog_supply_set(kst_prog_t *p, uint16_t mv, uint16_t ma)
{
    p->supply_mv = mv;
    p->supply_ma = ma;
}

uint16_t kst_prog_supply_mv(const kst_prog_t *p)
{
    return p->supply_mv;
}

uint16_t kst_prog_supply_ma(const kst_prog_t *p)
{
    return p->supply_ma;
}

kst_prog_msg_id_t kst_prog_read(kst_prog_t *p)
{
    kst_link_request_t r;
    const kst_prog_msg_id_t m = idle_safe(p);

    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, 0);
    }
    r = request(p, LINK_KST_OP_READ_ALL);
    send(p, &r);
    return KST_PROG_MSG_NONE;
}

kst_prog_msg_id_t kst_prog_verify(kst_prog_t *p)
{
    kst_link_request_t r;
    kst_prog_msg_id_t m = idle_safe(p);

    if (m == KST_PROG_MSG_NONE
        && (!p->have_servo || kst_prog_in_doubt(p))) {
        m = KST_PROG_MSG_NEED_READ;
    }
    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, 0);
    }
    r = request(p, LINK_KST_OP_VERIFY);
    r.image = p->servo;
    send(p, &r);
    return KST_PROG_MSG_NONE;
}

kst_prog_msg_id_t kst_prog_enable(kst_prog_t *p, bool on)
{
    kst_prog_msg_id_t m;

    if (!on) {
        p->write_enabled = false;
        return KST_PROG_MSG_NONE;
    }
    m = idle_safe(p);
    if (m == KST_PROG_MSG_NONE) {
        if (flag(p, LINK_KS_F_LOCKED) && p->f.have_backup) {
            /* The session's restore is the one write a locked session
             * takes, whatever the servo holds now. */
        } else if (!p->have_servo) {
            m = KST_PROG_MSG_NEED_READ;
        } else if (!p->fp_ok) {
            m = KST_PROG_MSG_READ_ONLY;
        }
    }
    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, 0);
    }
    p->write_enabled = true;
    return KST_PROG_MSG_NONE;
}

/* What a write of the draft needs, the switch left out. */
static kst_prog_msg_id_t write_ready(const kst_prog_t *p, unsigned *arg)
{
    const kst_prog_msg_id_t m = idle_safe(p);

    *arg = 0;
    if (m != KST_PROG_MSG_NONE) {
        return m;
    }
    if (kst_prog_journal_other(p)) {
        /* A read changes nothing about it. */
        *arg = p->journal_port + 1u;
        return KST_PROG_MSG_JOURNAL_OTHER;
    }
    if (!p->have_servo || kst_prog_in_doubt(p)) {
        return KST_PROG_MSG_NEED_READ;
    }
    if (!p->fp_ok) {
        return KST_PROG_MSG_READ_ONLY;
    }
    return KST_PROG_MSG_NONE;
}

static kst_prog_msg_id_t plan_message(kst_plan_result_t r, kst_rules_t broken,
                                      unsigned *arg)
{
    switch (r) {
    case KST_PLAN_OK:
        return KST_PROG_MSG_NONE;
    case KST_PLAN_ERR_RULES:
        *arg = lowest(broken & KST_RULES_HARD);
        return KST_PROG_MSG_EDIT_HARD;
    case KST_PLAN_ERR_R00:
        return (kst_prog_msg_id_t)(KST_PROG_MSG_REF + LINK_KST_REF_PLAN_R00);
    case KST_PLAN_ERR_PAIRING:
        return (kst_prog_msg_id_t)(KST_PROG_MSG_REF
                                   + LINK_KST_REF_PLAN_PAIRING);
    default:
        return (kst_prog_msg_id_t)(KST_PROG_MSG_REF
                                   + LINK_KST_REF_PLAN_NO_PATH);
    }
}

static kst_prog_msg_id_t write_check(kst_prog_t *p, unsigned *arg)
{
    kst_rules_t broken = 0;
    kst_prog_msg_id_t m = write_ready(p, arg);

    if (m != KST_PROG_MSG_NONE) {
        return m;
    }
    if (same(&p->servo, &p->draft)) {
        return KST_PROG_MSG_NO_CHANGES;
    }
    if (!p->write_enabled) {
        return KST_PROG_MSG_NEED_ENABLE;
    }
    {
        const kst_plan_result_t r = kst_plan_edit(&p->servo, &p->draft, 0,
                                                  &p->plan, &broken);

        m = plan_message(r, broken, arg);
    }
    return m;
}

kst_prog_msg_id_t kst_prog_write_check(kst_prog_t *p)
{
    unsigned arg;

    return write_check(p, &arg);
}

static kst_prog_msg_id_t write_draft(kst_prog_t *p, kst_prog_from_t from)
{
    kst_link_request_t r;
    unsigned arg;
    const kst_prog_msg_id_t m = write_check(p, &arg);

    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, arg);
    }
    p->soft = false;
    p->soft_load = false;
    p->restoring = (uint8_t)from;
    p->writes = (uint8_t)kst_prog_changes(p);
    r = request(p, LINK_KST_OP_WRITE);
    r.image = p->draft;
    send_write(p, &r, &p->draft);
    return KST_PROG_MSG_NONE;
}

kst_prog_msg_id_t kst_prog_write(kst_prog_t *p)
{
    return write_draft(p, KST_PROG_FROM_NONE);
}

static kst_prog_msg_id_t release_check(kst_prog_t *p, unsigned *arg)
{
    kst_prog_msg_id_t m = write_ready(p, arg);

    if (m != KST_PROG_MSG_NONE) {
        return m;
    }
    if (!p->write_enabled) {
        return KST_PROG_MSG_NEED_ENABLE;
    }
    if (p->servo.r[KST_REG_NODE_ADDR] == 0u) {
        return KST_PROG_MSG_NO_CHANGES;
    }
    return plan_message(kst_plan_release_pairing(&p->servo, &p->plan), 0,
                        arg);
}

kst_prog_msg_id_t kst_prog_release_check(kst_prog_t *p)
{
    unsigned arg;

    return release_check(p, &arg);
}

kst_prog_msg_id_t kst_prog_release(kst_prog_t *p)
{
    kst_link_request_t r;
    unsigned arg;
    const kst_prog_msg_id_t m = release_check(p, &arg);

    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, arg);
    }
    p->soft = false;
    p->soft_load = false;
    p->restoring = KST_PROG_FROM_NONE;
    r = request(p, LINK_KST_OP_RELEASE);
    send_write(p, &r, &p->plan.target);
    return KST_PROG_MSG_NONE;
}

static const kst_image_t *source(const kst_prog_t *p, kst_prog_from_t from)
{
    switch (from) {
    case KST_PROG_FROM_SESSION:
        return p->fed && p->f.have_backup ? &p->f.backup : NULL;
    case KST_PROG_FROM_JOURNAL:
        return p->journal && !p->journal_other ? &p->journal_img : NULL;
    default:
        return NULL;
    }
}

bool kst_prog_restore_offered(const kst_prog_t *p, kst_prog_from_t from)
{
    const kst_image_t *src = source(p, from);

    /* The port takes a read and nothing else while it asks for one. */
    if (src == NULL || p->journal_other
        || kst_prog_state(p) != KST_PROG_PROGRAMMING
        || flag(p, LINK_KS_F_MUST_READ)) {
        return false;
    }
    if (from == KST_PROG_FROM_SESSION) {
        /* A locked session takes this restore and no read before it. */
        return flag(p, LINK_KS_F_LOCKED)
            || (p->have_servo && !same(src, &p->servo));
    }
    return p->have_servo && !same(src, &p->servo);
}

kst_prog_msg_id_t kst_prog_restore_plan(kst_prog_t *p, kst_prog_from_t from,
                                        kst_prog_restore_t *out)
{
    const kst_image_t *src = source(p, from);
    kst_image_t target;
    kst_rules_t hard;
    unsigned arg = 0;
    kst_prog_msg_id_t m = idle_safe(p);

    memset(out, 0, sizeof(*out));
    out->from = (uint8_t)from;
    if (m == KST_PROG_MSG_NONE && p->journal_other) {
        return say(p, KST_PROG_MSG_JOURNAL_OTHER, p->journal_port + 1u);
    }
    if (m == KST_PROG_MSG_NONE && src == NULL) {
        m = KST_PROG_MSG_NO_RESTORE;
    }
    if (m == KST_PROG_MSG_NONE && flag(p, LINK_KS_F_MUST_READ)) {
        m = KST_PROG_MSG_NEED_READ;
    }
    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, 0);
    }
    if (from == KST_PROG_FROM_SESSION) {
        if (!p->have_servo || kst_prog_in_doubt(p)) {
            /* What the servo holds is not known: the coprocessor plans,
             * and the operator confirms a result that is not checked. */
            out->writes = 0xFFu;
            out->unchecked = true;
            return KST_PROG_MSG_NONE;
        }
        m = plan_message(kst_plan_restore(&p->servo, src, &p->plan), 0,
                         &arg);
        if (m == KST_PROG_MSG_NONE && p->plan.n == 0u) {
            m = KST_PROG_MSG_NO_RESTORE;
        }
        if (m != KST_PROG_MSG_NONE) {
            return say(p, m, arg);
        }
        out->writes = p->plan.n;
        out->unchecked = p->plan.unchecked != 0u;
        out->soft = soft_new(p, src);
        return KST_PROG_MSG_NONE;
    }

    m = write_ready(p, &arg);
    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, arg);
    }
    target = p->servo;
    for (unsigned id = 0; id < KST_F_COUNT; ++id) {
        const kst_field_id_t fid = (kst_field_id_t)id;

        if (kst_field_get(src, fid) == kst_field_get(&p->servo, fid)
            || id == KST_F_DUTY_COPY) {
            continue;
        }
        if (editable(fid)) {
            (void)kst_field_edit(&target, fid, kst_field_get(src, fid));
            out->writes++;
        } else {
            out->stay++;
        }
    }
    if (out->writes == 0u) {
        return say(p, KST_PROG_MSG_NO_RESTORE, out->stay);
    }
    hard = hard_new(p, &target);
    if (hard != 0u) {
        return say(p, KST_PROG_MSG_RESTORE_REFUSED, lowest(hard));
    }
    out->soft = soft_new(p, &target);
    p->soft = false;
    p->soft_load = false;
    p->draft = target;
    return KST_PROG_MSG_NONE;
}

kst_prog_msg_id_t kst_prog_restore(kst_prog_t *p,
                                   const kst_prog_restore_t *plan)
{
    kst_link_request_t r;
    const kst_image_t *src = source(p, (kst_prog_from_t)plan->from);
    kst_prog_msg_id_t m;

    if (plan->from != KST_PROG_FROM_SESSION) {
        return write_draft(p, (kst_prog_from_t)plan->from);
    }
    m = idle_safe(p);
    if (m == KST_PROG_MSG_NONE && p->journal_other) {
        return say(p, KST_PROG_MSG_JOURNAL_OTHER, p->journal_port + 1u);
    }
    if (m == KST_PROG_MSG_NONE && src == NULL) {
        m = KST_PROG_MSG_NO_RESTORE;
    }
    if (m == KST_PROG_MSG_NONE && !p->write_enabled) {
        m = KST_PROG_MSG_NEED_ENABLE;
    }
    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, 0);
    }
    p->soft = false;
    p->soft_load = false;
    p->restoring = KST_PROG_FROM_SESSION;
    r = request(p, LINK_KST_OP_RESTORE);
    r.image = *src;
    r.confirm = plan->unchecked;
    send_write(p, &r, src);
    return KST_PROG_MSG_NONE;
}

void kst_prog_forget(kst_prog_t *p)
{
    if (p->journal && p->wait == W_NONE) {
        journal_clear(p);
        (void)say(p, KST_PROG_MSG_FORGOTTEN, 0);
    }
}

kst_prog_msg_id_t kst_prog_power_cycled(kst_prog_t *p)
{
    kst_link_request_t r;
    const kst_prog_msg_id_t m = idle(p);

    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, 0);
    }
    r = request(p, LINK_KST_OP_POWER_CYCLED);
    send(p, &r);
    return KST_PROG_MSG_NONE;
}

void kst_prog_abort(kst_prog_t *p)
{
    if (waits_unsent(p)) {
        not_started(p, KST_PROG_MSG_OUT + KST_LINK_OUT_DROPPED, 0);
    } else if (p->wait != W_NONE) {
        p->cmd.kind = KST_PROG_CMD_ABORT;
        p->cmd.id = 0;
        p->cmd_ready = true;
    }
}

/* -------------------------------------------------------------- servo files */

static kst_prog_msg_id_t card_free(const kst_prog_t *p)
{
    if (p->wait != W_NONE || p->cmd_ready || p->card_op != KST_PROG_CARD_IDLE) {
        return (kst_prog_msg_id_t)(KST_PROG_MSG_START + KST_LINK_START_BUSY);
    }
    if (!p->fed || !p->f.card) {
        return KST_PROG_MSG_CARD_NONE;
    }
    return KST_PROG_MSG_NONE;
}

/* What a load needs of the servo, when it is asked for and when the file
 * is there. */
static kst_prog_msg_id_t load_ready(const kst_prog_t *p, unsigned *arg)
{
    *arg = 0;
    if (kst_prog_state(p) != KST_PROG_PROGRAMMING) {
        return p->wait != W_NONE || p->cmd_ready
                   ? (kst_prog_msg_id_t)(KST_PROG_MSG_START
                                         + KST_LINK_START_BUSY)
                   : KST_PROG_MSG_NEED_PORT;
    }
    if (kst_prog_journal_other(p)) {
        *arg = p->journal_port + 1u;
        return KST_PROG_MSG_JOURNAL_OTHER;
    }
    if (!p->have_servo || kst_prog_in_doubt(p)) {
        return KST_PROG_MSG_NEED_READ;
    }
    if (!p->fp_ok) {
        return KST_PROG_MSG_READ_ONLY;
    }
    return KST_PROG_MSG_NONE;
}

kst_prog_msg_id_t kst_prog_save(kst_prog_t *p, const char *name)
{
    kst_prog_msg_id_t m = card_free(p);

    /* What the servo lacks is said before what the card lacks. */
    if ((m == KST_PROG_MSG_NONE || m == KST_PROG_MSG_CARD_NONE)
        && (!p->have_servo || kst_prog_in_doubt(p))) {
        m = KST_PROG_MSG_NEED_READ;
    }
    if (m == KST_PROG_MSG_NONE
        && (name == NULL || name[0] == '\0'
            || strlen(name) >= KST_PROG_NAME_MAX)) {
        m = KST_PROG_MSG_NAME_BAD;
    }
    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, 0);
    }
    p->card_auto = false;
    card_ask(p, KST_PROG_CARD_SAVE, name);
    return KST_PROG_MSG_NONE;
}

kst_prog_msg_id_t kst_prog_files(kst_prog_t *p, unsigned first)
{
    kst_prog_msg_id_t m = card_free(p);

    /* A servo in doubt takes no file and gives none: the read comes
     * first, and is said before what the card lacks. */
    if ((m == KST_PROG_MSG_NONE || m == KST_PROG_MSG_CARD_NONE)
        && kst_prog_in_doubt(p)) {
        m = KST_PROG_MSG_NEED_READ;
    }
    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, 0);
    }
    p->card_auto = false;
    p->card_first = (uint16_t)(first <= 0xFFFFu ? first : 0xFFFFu);
    card_ask(p, KST_PROG_CARD_LIST, NULL);
    return KST_PROG_MSG_NONE;
}

unsigned kst_prog_files_first(const kst_prog_t *p)
{
    return p->files_first;
}

kst_prog_msg_id_t kst_prog_load(kst_prog_t *p, const char *name)
{
    unsigned arg = 0;
    kst_prog_msg_id_t m = card_free(p);

    if (m == KST_PROG_MSG_NONE || m == KST_PROG_MSG_CARD_NONE) {
        /* What the servo lacks is said before what the card lacks. */
        const kst_prog_msg_id_t servo = load_ready(p, &arg);

        m = servo != KST_PROG_MSG_NONE ? servo : m;
    }
    if (m == KST_PROG_MSG_NONE
        && (name == NULL || name[0] == '\0'
            || strlen(name) >= KST_PROG_NAME_MAX)) {
        m = KST_PROG_MSG_CARD_READ;
    }
    if (m != KST_PROG_MSG_NONE) {
        return say(p, m, arg);
    }
    p->card_auto = false;
    p->soft = false;
    p->soft_load = false;
    card_ask(p, KST_PROG_CARD_LOAD, name);
    return KST_PROG_MSG_NONE;
}

bool kst_prog_card_busy(const kst_prog_t *p)
{
    return p->card_op != KST_PROG_CARD_IDLE;
}

uint32_t kst_prog_files_seq(const kst_prog_t *p)
{
    return p->files_seq;
}

const char *kst_prog_file_name(const kst_prog_t *p, unsigned i)
{
    return i < p->files_n ? p->files[i] : "";
}

unsigned kst_prog_file_count(const kst_prog_t *p, unsigned *total)
{
    if (total != NULL) {
        *total = p->files_total;
    }
    return p->files_n;
}

unsigned kst_prog_file_type(const kst_prog_t *p, unsigned i)
{
    return i < p->files_n ? p->file_types[i] : 0u;
}

kst_field_set_t kst_prog_load_skipped(const kst_prog_t *p)
{
    return p->load_skipped;
}

const char *kst_prog_preset_name(const kst_prog_t *p)
{
    return p->preset;
}

const char *kst_prog_card_name(const kst_prog_t *p)
{
    return p->card_name;
}

kst_prog_file_kind_t kst_prog_load_kind(const kst_prog_t *p)
{
    return (kst_prog_file_kind_t)p->load_kind;
}

bool kst_prog_card_take(kst_prog_t *p, kst_prog_card_req_t *out)
{
    if (!p->card_ready) {
        return false;
    }
    p->card_ready = false;
    memset(out, 0, sizeof(*out));
    out->op = p->card_op;
    out->id = p->card_id;
    out->preset = p->card_auto;
    out->first = p->card_op == KST_PROG_CARD_LIST ? p->card_first : 0u;
    memcpy(out->name, p->card_name, sizeof(out->name));
    out->image = *file_image(p);
    return true;
}

static void name_copy(char *to, const char *from)
{
    memcpy(to, from, KST_PROG_NAME_MAX);
    to[KST_PROG_NAME_MAX - 1u] = '\0';
}

static unsigned card_message(unsigned result, bool load)
{
    switch (result) {
    case KST_PROG_CARD_NO_CARD:
        return KST_PROG_MSG_CARD_NONE;
    case KST_PROG_CARD_FULL:
        return KST_PROG_MSG_CARD_FULL;
    case KST_PROG_CARD_WRITE_ERR:
        return KST_PROG_MSG_CARD_WRITE;
    case KST_PROG_CARD_NAME:
        return KST_PROG_MSG_NAME_BAD;
    case KST_PROG_CARD_EXISTS:
        return KST_PROG_MSG_NAME_EXISTS;
    case KST_PROG_CARD_BAD:
        return KST_PROG_MSG_LOAD_BAD;
    case KST_PROG_CARD_CHECKSUM:
        return KST_PROG_MSG_LOAD_CHECKSUM;
    case KST_PROG_CARD_NEWER:
        return KST_PROG_MSG_LOAD_NEWER;
    default:
        /* A result that is none of a save's or a list's. */
        return load ? KST_PROG_MSG_CARD_READ : KST_PROG_MSG_CARD_WRITE;
    }
}

static void save_done(kst_prog_t *p, const kst_prog_card_done_t *d)
{
    const bool waited = p->card_auto && p->wait == W_SAVE;
    const bool ok = d->result == KST_PROG_CARD_OK;

    if (ok) {
        name_copy(p->card_name, d->name);
    }
    /* A save by hand after a write that went with no card holds the
     * written servo, not the one the session found: it is no backup. */
    if (ok && (p->card_auto || !p->no_file)) {
        p->backed_up = p->have_servo;
        p->no_file = false;
        name_copy(p->backup, d->name);
    }
    if (waited) {
        if (ok) {
            write_go(p);
        } else {
            not_started(p, card_message(d->result, false), 0);
        }
    } else if (!p->card_auto) {
        (void)say(p, ok ? KST_PROG_MSG_SAVED : card_message(d->result, false),
                  0);
    }
    /* A save whose write was given up meanwhile says nothing more. */
    p->card_auto = false;
}

static void list_done(kst_prog_t *p, const kst_prog_card_done_t *d)
{
    if (d->result != KST_PROG_CARD_OK) {
        (void)say(p, card_message(d->result, true), 0);
        return;
    }
    p->files_n = d->count <= KST_PROG_FILES_MAX ? d->count
                                                : (uint8_t)KST_PROG_FILES_MAX;
    p->files_total = d->total;
    p->files_first = p->card_first;
    for (unsigned i = 0; i < p->files_n; ++i) {
        name_copy(p->files[i], d->names[i]);
        p->file_types[i] = d->types[i];
    }
    name_copy(p->preset, d->name);
    p->files_seq++;
    if (d->total == 0u) {
        (void)say(p, KST_PROG_MSG_NO_FILES, 0);
    }
}

static void load_done(kst_prog_t *p, const kst_prog_card_done_t *d)
{
    unsigned arg = 0;
    kst_rules_t hard, soft;
    kst_prog_msg_id_t m;

    if (d->result == KST_PROG_CARD_BAD) {
        (void)say(p, KST_PROG_MSG_LOAD_BAD, d->line);
        return;
    }
    if (d->result == KST_PROG_CARD_VALUE) {
        (void)say_reg(p, KST_PROG_MSG_LOAD_VALUE, d->line, d->field);
        return;
    }
    if (d->result == KST_PROG_CARD_TYPE) {
        (void)say(p, KST_PROG_MSG_LOAD_TYPE,
                  d->type | ((unsigned)d->servo_type << 8));
        return;
    }
    if (d->result != KST_PROG_CARD_OK) {
        (void)say(p, card_message(d->result, true), 0);
        return;
    }
    /* The servo the file was compared with may be gone meanwhile. */
    m = load_ready(p, &arg);
    if (m != KST_PROG_MSG_NONE) {
        (void)say(p, m, arg);
        return;
    }
    p->load_kind = d->kind < KST_PROG_FILE_KINDS ? d->kind
                                                 : KST_PROG_FILE_UNTYPED;
    p->soft_skipped = d->skipped;
    /* The card's side takes editable fields only: a target that differs
     * anywhere else is not a file's and is not taken. */
    for (unsigned id = 0; id < KST_F_COUNT; ++id) {
        const kst_field_id_t fid = (kst_field_id_t)id;

        if (id != KST_F_DUTY_COPY && !editable(fid)
            && kst_field_get(&d->image, fid)
                   != kst_field_get(&p->servo, fid)) {
            (void)say(p, KST_PROG_MSG_LOAD_BAD, 0);
            return;
        }
    }
    if (differ(&p->servo, &d->image) == 0u) {
        (void)say(p, KST_PROG_MSG_LOAD_SAME, ones(d->skipped));
        return;
    }
    hard = hard_new(p, &d->image);
    if (hard != 0u) {
        (void)say(p, KST_PROG_MSG_LOAD_REFUSED, lowest(hard));
        return;
    }
    soft = soft_new(p, &d->image);
    if (soft != 0u) {
        p->soft = true;
        p->soft_load = true;
        p->soft_img = d->image;
        p->soft_rules = soft;
        (void)say(p, KST_PROG_MSG_LOAD_SOFT, 0);
        return;
    }
    load_take(p, &d->image);
}

void kst_prog_card_done(kst_prog_t *p, const kst_prog_card_done_t *done)
{
    const unsigned op = p->card_op;

    if (op == KST_PROG_CARD_IDLE || done->id != p->card_id) {
        return;
    }
    p->card_op = KST_PROG_CARD_IDLE;
    p->card_ready = false;
    switch (op) {
    case KST_PROG_CARD_SAVE:
        save_done(p, done);
        break;
    case KST_PROG_CARD_LIST:
        list_done(p, done);
        break;
    default:
        load_done(p, done);
        break;
    }
}

/* ----------------------------------------------------- the journal's file */

static uint16_t crc16(const char *s, size_t n)
{
    uint16_t crc = 0xFFFFu;

    for (size_t i = 0; i < n; ++i) {
        crc = (uint16_t)(crc ^ ((unsigned)(uint8_t)s[i] << 8));
        for (unsigned b = 0; b < 8u; ++b) {
            const unsigned next = (unsigned)crc << 1;

            crc = (uint16_t)((crc & 0x8000u) != 0u ? next ^ 0x1021u : next);
        }
    }
    return crc;
}

static char *put(char *at, const char *s)
{
    while (*s != '\0') {
        *at++ = *s++;
    }
    return at;
}

static char *put_hex(char *at, unsigned v, unsigned digits)
{
    static const char k_hex[] = "0123456789ABCDEF";

    for (unsigned i = digits; i > 0u; --i) {
        *at++ = k_hex[(v >> (4u * (i - 1u))) & 0xFu];
    }
    return at;
}

static char *put_image(char *at, const char *key, const kst_image_t *img)
{
    at = put(at, key);
    for (unsigned i = 0; i < sizeof(img->r); ++i) {
        at = put_hex(at, img->r[i], 2);
    }
    *at++ = '\n';
    return at;
}

size_t kst_prog_file_text(const kst_prog_t *p, char *out, size_t n)
{
    char *at = out;

    if (n < KST_PROG_FILE_MAX) {
        return 0;
    }
    at = put(at, k_head);
    if (p->journal) {
        at = put_image(at, "journal=", &p->journal_img);
        at = put_image(at, "target=", &p->journal_target);
        at = put(at, "port=");
        at = put_hex(at, p->journal_port, 1);
        *at++ = '\n';
    }
    {
        const uint16_t crc = crc16(out, (size_t)(at - out));

        at = put(at, "check=");
        at = put_hex(at, crc, 4);
        *at++ = '\n';
    }
    *at = '\0';
    return (size_t)(at - out);
}

size_t kst_prog_file_take(kst_prog_t *p, char *out, size_t n)
{
    size_t len;

    if (!p->file_wanted || p->file_busy || !p->fed || !p->f.card) {
        return 0;
    }
    len = kst_prog_file_text(p, out, n);
    if (len != 0u) {
        p->file_wanted = false;
        p->file_busy = true;
        p->file_for = p->file_next;
        p->file_next = FILE_NONE;
    }
    return len;
}

void kst_prog_file_done(kst_prog_t *p, bool ok)
{
    const uint8_t what = p->file_for;

    if (!p->file_busy) {
        return;
    }
    p->file_busy = false;
    p->file_for = FILE_NONE;
    /* The card holds the text of before: a record cleared here would be
     * found again at the next start.  The text is written again. */
    p->file_retry = !ok;
    p->file_retry_ms = p->f.now_ms;
    if (p->wait == W_FILE && what == FILE_JOURNAL) {
        if (ok) {
            send(p, &p->held);
        } else {
            not_started(p, KST_PROG_MSG_STORE_FAILED, 0);
        }
    }
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* @p digits hexadecimal digits at @p s; -1 for anything else. */
static long take_hex(const char *s, unsigned digits)
{
    long v = 0;

    for (unsigned i = 0; i < digits; ++i) {
        const int d = hex(s[i]);

        if (d < 0) {
            return -1;
        }
        v = v * 16 + d;
    }
    return v;
}

/* "key" + 64 digits + newline at @p s, @p left bytes there. */
static bool take_image(const char *s, size_t left, const char *key,
                       kst_image_t *img)
{
    const size_t k = strlen(key);

    if (left < k + 65u || memcmp(s, key, k) != 0 || s[k + 64u] != '\n') {
        return false;
    }
    for (size_t i = 0; i < sizeof(img->r); ++i) {
        const long v = take_hex(s + k + 2u * i, 2);

        if (v < 0) {
            return false;
        }
        img->r[i] = (uint8_t)v;
    }
    return true;
}

typedef struct {
    bool journal;
    uint8_t journal_port;
    kst_image_t journal_img;
    kst_image_t journal_target;
} loaded_t;

static bool parse(loaded_t *into, const char *text, size_t len)
{
    const size_t head = sizeof(k_head) - 1u;
    size_t at = head;
    long v;

    if (len < head + 11u || len >= KST_PROG_FILE_MAX
        || memcmp(text, k_head, head) != 0) {
        return false;
    }
    /* The last line is the check of all before it. */
    if (memcmp(text + len - 11u, "check=", 6) != 0 || text[len - 1u] != '\n') {
        return false;
    }
    v = take_hex(text + len - 5u, 4);
    if (v < 0 || (uint16_t)v != crc16(text, len - 11u)) {
        return false;
    }
    len -= 11u;

    into->journal = false;
    if (at != len) {
        if (!take_image(text + at, len - at, "journal=", &into->journal_img)) {
            return false;
        }
        at += 8u + 65u;
        if (!take_image(text + at, len - at, "target=",
                        &into->journal_target)) {
            return false;
        }
        at += 7u + 65u;
        if (len - at != 7u || memcmp(text + at, "port=", 5) != 0
            || text[at + 6u] != '\n') {
            return false;
        }
        v = take_hex(text + at + 5u, 1);
        if (v < 0) {
            return false;
        }
        into->journal_port = (uint8_t)v;
        into->journal = true;
    }
    return true;
}

bool kst_prog_file_whole(const char *text, size_t len)
{
    loaded_t got;

    memset(&got, 0, sizeof(got));
    return parse(&got, text, len);
}

bool kst_prog_file_load(kst_prog_t *p, const char *text, size_t len)
{
    /* Parsed aside: a text that is not whole changes nothing. */
    loaded_t got;

    memset(&got, 0, sizeof(got));
    if (!parse(&got, text, len)) {
        (void)say(p, KST_PROG_MSG_FILE_BAD, 0);
        return false;
    }
    p->journal = got.journal;
    p->journal_open = got.journal;
    p->journal_port = got.journal_port;
    p->journal_img = got.journal_img;
    p->journal_target = got.journal_target;
    p->journal_other = false;
    if (p->journal) {
        (void)say(p, KST_PROG_MSG_JOURNAL, p->journal_port + 1u);
    }
    return true;
}
