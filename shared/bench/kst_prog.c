/**
 * @file
 * @brief The state of the programmer for KST and Chaservo servos: see
 *        kst_prog.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_prog.h"

#include <string.h>

#include "kst_prog_int.h"

/* ----------------------------------------------------------------- helpers */

/**
 * @brief The GPIO programming port @p port drives as the feed says, or
 *        KST_PROG_NO_PIN.
 *
 * @param p    The programmer state.
 * @param port The programming port, from 0.
 * @return The GPIO, or KST_PROG_NO_PIN.
 */
static uint8_t pin_of(const kst_prog_t *p, uint8_t port)
{
    if (port >= LINK_PG_PORTS_MAX
        || (p->f.port_pin_known & (1u << port)) == 0u
        || p->f.port_pin[port] > KST_PROG_PIN_LAST) {
        return KST_PROG_NO_PIN;
    }
    return p->f.port_pin[port];
}

unsigned kst_prog_journal_arg(const kst_prog_t *p)
{
    unsigned arg = p->journal_port + 1u;

    if (p->journal_pin <= KST_PROG_PIN_LAST) {
        arg |= (p->journal_pin + 1u) << 8;
    }
    return arg;
}

bool kst_prog_same(const kst_image_t *a, const kst_image_t *b)
{
    return memcmp(a->r, b->r, sizeof(a->r)) == 0;
}

/**
 * @brief Whether an operation writes the servo.
 *
 * @param op The operation.
 * @return true for WRITE, RESTORE or RELEASE.
 */
static bool is_write(unsigned op)
{
    return op == LINK_KST_OP_WRITE || op == LINK_KST_OP_RESTORE
        || op == LINK_KST_OP_RELEASE;
}

unsigned kst_prog_lowest(uint64_t set)
{
    unsigned n = 0;

    while (n < 64u && ((set >> n) & 1u) == 0u) {
        n++;
    }
    return n;
}

unsigned kst_prog_ones(uint64_t set)
{
    unsigned n = 0;

    for (; set != 0u; set &= set - 1u) {
        n++;
    }
    return n;
}

/**
 * @brief The level of a message.
 *
 * @param id The message id.
 * @return The level, as kst_prog_level_t.
 */
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

kst_prog_msg_id_t kst_prog_say_reg(kst_prog_t *p, unsigned id, unsigned arg,
                                   unsigned reg)
{
    p->msg.id = (uint8_t)id;
    p->msg.level = (uint8_t)level_of(id);
    p->msg.arg = (uint16_t)arg;
    p->msg.reg = (uint8_t)reg;
    p->msg.seq++;
    return (kst_prog_msg_id_t)id;
}

kst_prog_msg_id_t kst_prog_say(kst_prog_t *p, unsigned id, unsigned arg)
{
    return kst_prog_say_reg(p, id, arg, 0xFFu);
}

/**
 * @brief The last message no longer describes what is shown: an edit was taken,
 *        the switch went on, the pending values went.
 *
 * A message of the danger level stays until the next one.
 *
 * @param p The programmer state.
 */
static void hush(kst_prog_t *p)
{
    if (p->msg.id != KST_PROG_MSG_NONE && p->msg.level != KST_PROG_DANGER) {
        p->msg.id = KST_PROG_MSG_NONE;
        p->msg.level = KST_PROG_INFO;
        p->msg.arg = 0;
        p->msg.reg = 0xFFu;
        p->msg.seq++;
    }
}

/**
 * @brief Asks for a file before the next write.
 *
 * @param p    The programmer state.
 * @param what The file step asked for.
 */
static void file_want(kst_prog_t *p, uint8_t what)
{
    p->file_wanted = true;
    if (what > p->file_next) {
        p->file_next = what;
    }
}

/**
 * @brief Clears the journal record and its pin.
 *
 * @param p The programmer state.
 */
static void journal_clear(kst_prog_t *p)
{
    if (p->journal) {
        p->journal = false;
        p->journal_open = false;
        p->journal_other = false;
        /* No record, no pin: the next record names the pin of its own
         * port at its own write. */
        p->journal_pin = KST_PROG_NO_PIN;
        file_want(p, FILE_KEEP);
    }
}

/**
 * @brief Whether a flag of the status read is set.
 *
 * @param p The programmer state.
 * @param f The flag bit.
 * @return true when the flag is set in a known read.
 */
static bool flag(const kst_prog_t *p, uint16_t f)
{
    return p->fed && p->f.rd.known && (p->f.rd.flags & f) != 0u;
}

/**
 * @brief The session no longer stands: the port is in PWM, the link went, or
 *        the IOmcu started again.
 *
 * @param p The programmer state.
 */
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
    p->wrote = 0;
    p->held_wrote = 0;
    p->wrote_before = 0;
    p->wrote_known = false;
    p->keep = false;
    p->conf_set = 0;
    p->plan_n = 0;
}

/* -------------------------------------------------------------- the fields */

bool kst_prog_field_editable(kst_field_id_t id)
{
    const kst_field_t *f = kst_field(id);

    return f != NULL && f->access == KST_ACCESS_EDITABLE
        && id != KST_F_DUTY_COPY;
}

kst_field_set_t kst_prog_differ(const kst_image_t *a, const kst_image_t *b)
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
kst_rules_t kst_prog_hard_new(const kst_prog_t *p, const kst_image_t *target)
{
    const kst_rules_t was = kst_limits_edit(&p->servo, &p->servo, 0, NULL);

    return kst_limits_edit(&p->servo, target, 0, NULL) & KST_RULES_HARD
         & ~was;
}

/**
 * @brief The soft rules @p target breaks as a step from the servo as last read:
 *        what a restore is checked with, which moves the servo back and is
 *        warned of by where it moves from.
 *
 * @param p      The programmer state.
 * @param target The image the step goes to.
 * @return The soft rules.
 */
static kst_rules_t soft_new(const kst_prog_t *p, const kst_image_t *target)
{
    return kst_limits_edit(&p->servo, target, 0, NULL) & ~KST_RULES_HARD;
}

/**
 * @brief The image the soft rules compare with: the servo as the session found
 *        it, so that the writes of a session do not add up unseen.
 *
 * The servo as last read where the session's backup is not held or is another
 * servo's.
 */
static const kst_image_t *soft_base(const kst_prog_t *p)
{
    return p->fed && p->f.have_backup && !kst_prog_backup_other(p)
         ? &p->f.backup : &p->servo;
}

/**
 * @brief The soft rules an image breaks against the soft base.
 *
 * @param p   The programmer state.
 * @param img The image to check.
 * @return The soft rules.
 */
static kst_rules_t soft_of(const kst_prog_t *p, const kst_image_t *img)
{
    return kst_limits_edit(soft_base(p), img, 0, NULL) & ~KST_RULES_HARD;
}

/**
 * @brief The soft rules of @p img that field @p id is at fault for: the ones
 *        that go when the field takes the base's value.
 *
 * @param p   The programmer state.
 * @param img The image to check.
 * @param id  The field.
 * @return The soft rules the field is at fault for.
 */
static kst_rules_t soft_by(const kst_prog_t *p, const kst_image_t *img,
                           kst_field_id_t id)
{
    const uint16_t was = kst_field_get(soft_base(p), id);
    kst_image_t t = *img;

    if (was == kst_field_get(img, id) || !kst_field_edit(&t, id, was)) {
        return 0;
    }
    return soft_of(p, img) & ~soft_of(p, &t);
}

/**
 * @brief Whether @p raw is further from the base's value @p base than @p conf,
 *        the value confirmed, or on its other side.
 *
 * @param raw  The raw value.
 * @param conf The confirmed raw value.
 * @param base The base raw value.
 * @return true when raw is further from base than conf, or on its other side.
 */
static bool further(uint16_t raw, uint16_t conf, uint16_t base)
{
    const unsigned a = raw > base ? (unsigned)(raw - base)
                                  : (unsigned)(base - raw);
    const unsigned b = conf > base ? (unsigned)(conf - base)
                                   : (unsigned)(base - conf);

    if (raw != base && conf != base && (raw > base) != (conf > base)) {
        return true;
    }
    return a > b;
}

/**
 * @brief The value of @p id the operator has confirmed a soft rule for; what
 *        the servo holds where none was.
 *
 * @param p  The programmer state.
 * @param id The field.
 * @return The confirmed raw value, or the servo's value where none was.
 */
static uint16_t confirmed(const kst_prog_t *p, kst_field_id_t id)
{
    return (p->conf_set & KST_FIELD_BIT(id)) != 0u ? p->conf_raw[id]
                                                  : kst_field_get(&p->servo,
                                                                  id);
}

/*
 * The soft rules the step from @p from to @p target asks a confirmation
 * for: the ones @p target breaks and @p from does not, and the ones a
 * field is at fault for that the step moves further from the base's value
 * than the value confirmed.
 */
kst_rules_t kst_prog_soft_asks(const kst_prog_t *p, const kst_image_t *from,
                               const kst_image_t *target)
{
    const kst_image_t *base = soft_base(p);
    kst_rules_t ask = soft_of(p, target) & ~soft_of(p, from);

    for (unsigned n = 0; n < KST_F_COUNT; ++n) {
        const kst_field_id_t id = (kst_field_id_t)n;
        const uint16_t raw = kst_field_get(target, id);

        if (kst_prog_field_editable(id) && raw != kst_field_get(from, id)
            && further(raw, confirmed(p, id), kst_field_get(base, id))) {
            ask |= soft_by(p, target, id);
        }
    }
    return ask;
}

/**
 * @brief The values of @p img that differ from @p from are confirmed.
 *
 * @param p    The programmer state.
 * @param from The image before the edit.
 * @param img  The image whose values are confirmed.
 */
static void confirm(kst_prog_t *p, const kst_image_t *from,
                    const kst_image_t *img)
{
    for (unsigned n = 0; n < KST_F_COUNT; ++n) {
        const kst_field_id_t id = (kst_field_id_t)n;

        if (kst_prog_field_editable(id)
            && kst_field_get(img, id) != kst_field_get(from, id)) {
            p->conf_raw[n] = kst_field_get(img, id);
            p->conf_set |= KST_FIELD_BIT(n);
        }
    }
}

const kst_image_t *kst_prog_soft_base(const kst_prog_t *p)
{
    return soft_base(p);
}

kst_field_set_t kst_prog_soft_fields(const kst_prog_t *p,
                                     const kst_image_t *img)
{
    kst_field_set_t set = 0;

    if (!p->have_servo) {
        return 0;
    }
    for (unsigned n = 0; n < KST_F_COUNT; ++n) {
        const kst_field_id_t id = (kst_field_id_t)n;

        if (kst_prog_field_editable(id)
            && kst_field_get(img, id) != kst_field_get(&p->servo, id)
            && soft_by(p, img, id) != 0u) {
            set |= KST_FIELD_BIT(n);
        }
    }
    return set;
}

/**
 * @brief Whether a raw value is taken for a field of the draft.
 *
 * @param p   The programmer state.
 * @param id  The field.
 * @param raw The raw value.
 * @return true when no hard rule is broken.
 */
static bool raw_taken(const kst_prog_t *p, kst_field_id_t id, uint16_t raw)
{
    kst_image_t trial = p->draft;

    return kst_field_edit(&trial, id, raw)
        && kst_prog_hard_new(p, &trial) == 0u;
}

/* ------------------------------------------------------------- the requests */

void kst_prog_send(kst_prog_t *p, const kst_link_request_t *req)
{
    if (++p->next_id == 0u) {
        p->next_id = 1u;
    }
    p->cmd.kind = KST_PROG_CMD_START;
    p->cmd.id = p->next_id;
    p->cmd.req = *req;
    p->cmd.req.id = p->next_id;
    p->cmd_ready = true;
    p->cmd_out = false;
    p->cmd_run = false;
    p->wait = W_ANSWER;
    p->wait_id = p->next_id;
    p->wait_ms = p->f.now_ms;
    p->op = (uint8_t)req->op;
}

/**
 * @brief Builds the link request for an operation.
 *
 * @param p  The programmer state.
 * @param op The operation.
 * @return The request.
 */
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
    if (op == LINK_KST_OP_LEAVE) {
        r.leave_ms = p->pwm_wait_ms;
    }
    r.write_enabled = p->write_enabled;
    return r;
}

/**
 * @brief Whether the programmer waits on a card or a command that is not sent.
 *
 * @param p The programmer state.
 * @return true when it waits.
 */
static bool waits_unsent(const kst_prog_t *p)
{
    return p->wait == W_SAVE || p->wait == W_FILE
        || (p->wait == W_ANSWER && !p->cmd_out);
}

void kst_prog_card_ask(kst_prog_t *p, kst_prog_card_op_t op, const char *name)
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
void kst_prog_write_go(kst_prog_t *p)
{
    const bool had_record = p->journal;

    if (p->have_servo) {
        /* The servo read is the journal's, or there is none: a write to
         * another servo than the journal's is not taken (journal_other). */
        p->held_over = p->journal;
        if (p->journal) {
            p->held_before = p->journal_target;
        } else {
            p->journal_img = p->servo;
        }
        p->journal = true;
        p->journal_open = true;
        p->journal_port = p->port;
        p->journal_pin = pin_of(p, p->port);
        p->journal_target = p->held_target;
        p->held_journal = true;
    }
    if (p->held.op == LINK_KST_OP_RESTORE && !had_record
        && (!p->have_servo || p->need_read)) {
        /* The session's restore to a servo whose image is not known: the
         * backup is what the record gives back, as it is what the write
         * aims at. */
        p->journal_img = p->held_target;
        p->journal_target = p->held_target;
        p->journal = true;
        p->journal_open = true;
        p->journal_port = p->port;
        p->journal_pin = pin_of(p, p->port);
        p->held_over = false;
        p->held_journal = true;
    }
    /* With no servo read the record of an earlier write is not this
     * write's: it stays as it is, in the model and on the card. */
    if (p->held_journal && p->f.card) {
        p->wait = W_FILE;
        p->wait_ms = p->f.now_ms;
        /* A text the card has not answered yet is of before this write. */
        p->file_stale = p->file_busy;
        file_want(p, FILE_JOURNAL);
    } else {
        kst_prog_send(p, &p->held);
    }
}

/**
 * @brief A write to the servo.
 *
 * The first of a session to a servo whose image is
 * known waits for that image in a file.  Without a card it goes with no
 * file, and the session's backup is the one copy of the servo as found.
 * A servo in doubt has no image worth a file: the session's restore, the
 * one write it takes, goes without.
 *
 * @param p      The programmer state.
 * @param req    The request.
 * @param target The image the write leaves.
 */
static void send_write(kst_prog_t *p, const kst_link_request_t *req,
                       const kst_image_t *target)
{
    const bool save = kst_prog_save_first(p) && !kst_prog_save_none(p);

    /* What the servo may hold other than its backup once the command is
     * taken: a write the card keeps from starting changes no field. */
    p->held_wrote = p->have_servo ? kst_prog_differ(&p->servo, target)
                                  : ~(kst_field_set_t)0;
    p->held = *req;
    p->held_target = *target;
    p->held_journal = false;
    p->held_no_file = kst_prog_save_none(p);
    p->op = (uint8_t)req->op;
    if (save) {
        p->wait = W_SAVE;
        p->wait_ms = p->f.now_ms;
        p->card_auto = true;
        kst_prog_card_ask(p, KST_PROG_CARD_SAVE, NULL);
    } else {
        kst_prog_write_go(p);
    }
}

/**
 * @brief Whether an operation may start now; the message when not.
 *
 * @param p The programmer state.
 * @return KST_PROG_MSG_NONE when it may, else the message why not.
 */
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

/**
 * @brief Whether the bench is armed.
 *
 * @param p The programmer state.
 * @return true when the programmer or the feed says so.
 */
static bool bench_armed(const kst_prog_t *p)
{
    return p->armed || (p->fed && p->f.armed);
}

/**
 * @brief The same for an operation that sends frames: not after STOP, and not
 *        on an armed bench.
 *
 * @param p The programmer state.
 * @return KST_PROG_MSG_NONE when it may, else the message why not.
 */
static kst_prog_msg_id_t idle_safe(const kst_prog_t *p)
{
    const kst_prog_msg_id_t m = idle(p);

    if (m == KST_PROG_MSG_NONE && p->f.stopped) {
        return KST_PROG_MSG_STOPPED;
    }
    if (m == KST_PROG_MSG_NONE && bench_armed(p)) {
        return KST_PROG_MSG_NEED_DISARM;
    }
    return m;
}

/* ---------------------------------------------------------------- outcomes */

/**
 * @brief What the servo holds is not known until a read ends: nothing is
 *        written to it, and the switch a write needs is off with what it was
 *        held for.
 *
 * @param p The programmer state.
 */
static void doubt(kst_prog_t *p)
{
    p->need_read = true;
    p->write_enabled = false;
}

/**
 * @brief Adopts the image the link read as the servo image.
 *
 * @param p The programmer state.
 */
static void adopt(kst_prog_t *p)
{
    const bool had = p->have_servo;
    const kst_image_t was = p->servo;
    bool other;

    p->servo = p->f.image;
    p->draft = p->servo;
    p->have_servo = true;
    p->need_read = false;
    p->soft = false;
    p->soft_load = false;
    p->load_skipped = 0;
    /* What the servo holds is what was confirmed. */
    p->conf_set = 0;
    p->fp_ok = kst_fingerprint(&p->servo, NULL)
            && (p->f.rd.flags & LINK_KS_F_FP_OK) != 0u;
    if ((had && !kst_prog_same(&was, &p->servo)) || !p->fp_ok) {
        /* Another servo, or one that is not written to. */
        p->write_enabled = false;
    }
    /* The file of the servo as found stays this servo's while the image
     * differs only where a write of the session may have changed it: a
     * write that ended in doubt, read afterwards, is no other servo. */
    other = had && (p->wrote_known
                        ? (kst_prog_differ(&was, &p->servo) & ~p->wrote) != 0u
                        : !kst_prog_same(&was, &p->servo));
    if (other) {
        p->backed_up = false;
        p->no_file = false;
    }
}

/**
 * @brief The registers a plan of the record's write may set to a value that is
 *        neither the record's nor the target's: the low register of every field
 *        in two registers that the write changes (kst_plan.h, the extra write).
 *
 * @param p The programmer state.
 * @return A bit mask of those registers.
 */
static uint32_t journal_detours(const kst_prog_t *p)
{
    uint32_t regs = 0;

    for (unsigned id = 0; id < KST_F_COUNT; ++id) {
        const kst_field_id_t fid = (kst_field_id_t)id;
        const kst_field_t *f = kst_field(fid);

        if (f->hi_bits != 0u
            && kst_field_get(&p->journal_img, fid)
                   != kst_field_get(&p->journal_target, fid)) {
            regs |= (uint32_t)1 << f->lo_reg;
        }
    }
    return regs;
}

/**
 * @brief Whether the servo read can be the one the journal records: on its
 *        port, and holding in every register the record's value or the
 *        target's.
 *
 * A register the write leaves alone holds the record's value then; a field the
 * write changes holds the one, the other, or a mix of their registers. The
 * registers of journal_detours() are exempt.  A servo of the same type with
 * other settings does not fit.
 *
 * @param p The programmer state.
 * @return true when it can.
 */
static bool journal_fits(const kst_prog_t *p)
{
    const uint32_t free_regs = journal_detours(p);

    if (p->port != p->journal_port) {
        return false;
    }
    for (unsigned reg = 0; reg < KST_REG_COUNT; ++reg) {
        const uint8_t now = p->servo.r[reg];

        if ((free_regs & ((uint32_t)1 << reg)) == 0u
            && now != p->journal_img.r[reg]
            && now != p->journal_target.r[reg]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief The record held and a servo read that holds the record's image or its
 *        target, register for register: a servo of the same type with the same
 *        settings holds them too, so the record stands until the operator says
 *        that this is the servo of the write.
 *
 * @param p The programmer state.
 * @return The message id, or KST_PROG_MSG_NONE.
 */
static kst_prog_msg_id_t journal_asks(const kst_prog_t *p)
{
    if (!p->journal || p->journal_other || !p->have_servo) {
        return KST_PROG_MSG_NONE;
    }
    if (kst_prog_same(&p->servo, &p->journal_img)) {
        return KST_PROG_MSG_JOURNAL_SAME;
    }
    if (kst_prog_same(&p->servo, &p->journal_target)) {
        return KST_PROG_MSG_JOURNAL_DONE;
    }
    return KST_PROG_MSG_NONE;
}

kst_prog_msg_id_t kst_prog_journal_asks(const kst_prog_t *p)
{
    return p->journal_open ? KST_PROG_MSG_NONE : journal_asks(p);
}

/*
 * The record held against the servo held: whether it is another servo's.
 * Another servo's record stands, and nothing goes to this servo until the
 * record is discarded.
 */
bool kst_prog_journal_classify(kst_prog_t *p)
{
    p->journal_other = p->journal && p->have_servo && !journal_fits(p);
    if (p->journal_other) {
        p->journal_open = true;
        p->write_enabled = false;
    }
    return p->journal_other;
}

/**
 * @brief Takes the result of a read of the servo.
 *
 * @param p The programmer state.
 */
static void read_done(kst_prog_t *p)
{
    if (!p->f.have_image) {
        (void)kst_prog_say(p, KST_PROG_MSG_SES + KST_SES_ERR_READ, 0);
        return;
    }
    adopt(p);
    if (kst_prog_journal_classify(p)) {
        (void)kst_prog_say(p, KST_PROG_MSG_JOURNAL_OTHER,
                           kst_prog_journal_arg(p));
    } else if (kst_prog_backup_other(p)) {
        /* Another servo than the session began with: its backup is that
         * servo's and is not restored to this one. */
        (void)kst_prog_say(p, KST_PROG_MSG_BACKUP_OTHER, 0);
    } else if (journal_asks(p) != KST_PROG_MSG_NONE) {
        /* Nothing is left of the write on this servo, and the record is
         * kept: the question stands until it is discarded. */
        p->journal_open = false;
        p->write_enabled = false;
        (void)kst_prog_say(p, journal_asks(p), kst_prog_journal_arg(p));
    } else if (p->journal && p->journal_open) {
        p->journal_open = false;
        (void)kst_prog_say(p, KST_PROG_MSG_JOURNAL_DIFF, 0);
    } else if ((p->f.rd.flags & LINK_KS_F_LOCKED) != 0u) {
        /* Read, and still the session's restore is all that is taken. */
        (void)kst_prog_say(p, KST_PROG_MSG_SES + KST_SES_ERR_LOCKED, 0);
    } else {
        (void)kst_prog_say(p, p->fp_ok ? KST_PROG_MSG_READ
                                       : KST_PROG_MSG_READ_ONLY, 0);
    }
}

/**
 * @brief Takes the end of an operation that succeeded.
 *
 * @param p  The programmer state.
 * @param op The operation.
 */
static void done_ok(kst_prog_t *p, unsigned op)
{
    switch (op) {
    case LINK_KST_OP_ENTER:
        /* A session that begins here: every write of it is seen. */
        p->wrote = 0;
        p->held_wrote = 0;
        p->wrote_before = 0;
        p->wrote_known = true;
        if (p->chain_read) {
            const kst_link_request_t r = request(p, LINK_KST_OP_READ_ALL);

            p->chain_read = false;
            kst_prog_send(p, &r);
        }
        break;
    case LINK_KST_OP_READ_ALL:
        read_done(p);
        break;
    case LINK_KST_OP_WRITE:
    case LINK_KST_OP_RESTORE:
    case LINK_KST_OP_RELEASE:
        if (p->held_journal) {
            journal_clear(p);
        }
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
            /* A record this write did not make, the restore of a session
             * with no servo read: it is another servo's and stands, or it
             * is this servo's and the image read says what is left of
             * it. */
            if (p->journal && !kst_prog_journal_classify(p)) {
                p->journal_open = false;
            }
        } else {
            doubt(p);
        }
        if (op == LINK_KST_OP_RELEASE) {
            (void)kst_prog_say(p, KST_PROG_MSG_RELEASED, 0);
        } else if (op == LINK_KST_OP_RESTORE
                   || p->restoring != KST_PROG_FROM_NONE) {
            (void)kst_prog_say(p, KST_PROG_MSG_RESTORED, 0);
        } else {
            (void)kst_prog_say(p, KST_PROG_MSG_WRITTEN, p->writes);
        }
        break;
    case LINK_KST_OP_VERIFY:
        (void)kst_prog_say(p, KST_PROG_MSG_VERIFIED, 0);
        break;
    case LINK_KST_OP_LEAVE:
        session_gone(p);
        (void)kst_prog_say(p, KST_PROG_MSG_LEFT, 0);
        break;
    case LINK_KST_OP_POWER_CYCLED:
        /* The exit where LEAVE is not taken: no wait was applied. */
        session_gone(p);
        (void)kst_prog_say(p, p->leave_doubt ? KST_PROG_MSG_LEFT_DOUBT
                                       : KST_PROG_MSG_LEFT_NO_WAIT, 0);
        break;
    default:
        break;
    }
}

kst_prog_res_t kst_prog_result_class(unsigned result)
{
    /* The planner ended without a plan: no frame went. */
    if (result == LINK_KST_RESULT_NO_PATH
        || result == LINK_KST_RESULT_TOO_COMPLEX) {
        return KST_PROG_RES_CLEAN;
    }
    if (result > (unsigned)KST_SES_ERR_NO_PLAN) {
        return KST_PROG_RES_DOUBT;
    }
    /* Every result of the session is named: one added to kst_ses_t does
     * not compile here until it has a class. */
    switch ((kst_ses_t)result) {
    case KST_SES_OK:
        return KST_PROG_RES_OK;
    case KST_SES_ERR_BUSY:
    case KST_SES_ERR_ARG:
    case KST_SES_ERR_NOT_IN_MODE:
    case KST_SES_ERR_NO_BACKUP:
    case KST_SES_ERR_FINGERPRINT:
    case KST_SES_ERR_LOCKED:
    case KST_SES_ERR_BAD_PLAN:
    case KST_SES_ERR_UNCONFIRMED:
    case KST_SES_ERR_CHANGED:
    case KST_SES_ERR_NO_PLAN:
    case KST_SES_ERR_WRITE_FAILED:
    case KST_SES_ERR_ROLLED_BACK:
        /* Refused before a frame; or, the last two, read back at the last
         * settled step: with no step counted done that is the image the
         * write started from. */
        return KST_PROG_RES_CLEAN;
    case KST_SES_BUSY:
    case KST_SES_ERR_NO_SERVO:
    case KST_SES_ERR_READ:
    case KST_SES_ERR_TORN:
    case KST_SES_ERR_UNINTENDED:
    case KST_SES_ERR_VERIFY:
    case KST_SES_ERR_DRIVER:
    case KST_SES_ERR_ABORTED:
        return KST_PROG_RES_DOUBT;
    }
    return KST_PROG_RES_DOUBT;
}

/**
 * @brief The pending values and the prompt as they are, for a write that ends
 *        with nothing written.
 *
 * @param p The programmer state.
 */
static void keep_now(kst_prog_t *p)
{
    p->keep = true;
    p->keep_draft = p->draft;
    p->keep_soft = p->soft;
    p->keep_soft_load = p->soft_load;
}

/**
 * @brief The pending values of before a write that wrote nothing.
 *
 * @param p The programmer state.
 */
static void keep_back(kst_prog_t *p)
{
    if (p->keep) {
        p->keep = false;
        p->draft = p->keep_draft;
        p->soft = p->keep_soft;
        p->soft_load = p->keep_soft_load;
    }
}

/**
 * @brief A write that ended without the servo verified.
 *
 * True when no register was written.
 *
 * @param p The programmer state.
 * @param o The outcome.
 * @return true when no register was written.
 */
static bool write_failed(kst_prog_t *p, const kst_link_outcome_t *o)
{
    bool nothing = false;   /* no register was written */

    switch (o->kind) {
    case KST_LINK_OUT_REFUSED:
    case KST_LINK_OUT_DROPPED:
        nothing = true;
        break;
    case KST_LINK_OUT_NACK:
        /* A frame refused after the command was acknowledged: the write
         * may have run, and no result of it was read. */
        nothing = !o->taken;
        break;
    case KST_LINK_OUT_DONE:
        nothing = o->steps_done == 0u
               && kst_prog_result_class((unsigned)o->result)
                      == KST_PROG_RES_CLEAN;
        break;
    default:
        break;
    }
    if (nothing) {
        if (!p->held_journal) {
            /* The write recorded nothing: a record held is as it was. */
        } else if (!p->held_over) {
            /* The record of this write alone, and it wrote nothing. */
            journal_clear(p);
        } else {
            /* The journal of an earlier write stands as it was, on the
             * card too. */
            p->journal_open = false;
            if (p->held_over
                && !kst_prog_same(&p->journal_target, &p->held_before)) {
                p->journal_target = p->held_before;
                file_want(p, FILE_KEEP);
            }
        }
        if (o->kind == KST_LINK_OUT_DONE
            && o->result == KST_SES_ERR_CHANGED) {
            doubt(p);
        }
    } else {
        doubt(p);
    }
    return nothing;
}

/**
 * @brief Takes the outcome of an operation.
 *
 * @param p The programmer state.
 * @param o The outcome.
 */
static void outcome(kst_prog_t *p, const kst_link_outcome_t *o)
{
    const unsigned op = o->op;
    bool said = false;

    p->wait = W_NONE;
    p->cmd_ready = false;
    if (is_write(op)
        && !(o->kind == KST_LINK_OUT_DONE && o->result == KST_SES_OK)) {
        if (write_failed(p, o)) {
            /* The IOmcu says no register was written: dropped
             * before the command went, refused, ended before its first
             * step, or rolled back to the image it started from and read
             * there.  The fields are the servo's as they were, and so are
             * the pending values a restore replaced. */
            p->wrote = p->wrote_before;
            keep_back(p);
        }
        if (p->journal && p->journal_open
            && (o->kind == KST_LINK_OUT_NO_ANSWER
                || o->kind == KST_LINK_OUT_RESTARTED)) {
            (void)kst_prog_say(p, KST_PROG_MSG_JOURNAL,
                               kst_prog_journal_arg(p));
            said = true;
        }
    }
    if (op != LINK_KST_OP_ENTER || o->kind != KST_LINK_OUT_DONE
        || o->result != KST_SES_OK) {
        p->chain_read = false;
    }
    p->keep = false;
    if (o->kind == KST_LINK_OUT_NO_ANSWER
        || (o->kind == KST_LINK_OUT_NACK && o->taken)) {
        /* The command was acknowledged, or whether it was is not known,
         * and the end of the operation was not read: it may have run, with
         * an image this side never takes. */
        if (op == LINK_KST_OP_READ_ALL || op == LINK_KST_OP_VERIFY
            || p->have_servo) {
            doubt(p);
        }
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
        } else if (op == LINK_KST_OP_LEAVE
                   && o->result == KST_SES_ERR_ABORTED) {
            /* The wait was cut.  The port says where it is: PROGRAMMING
             * with the session as it was after an abort, with a read owed
             * after a disarm, or on its way to PWM with the supply off. */
            (void)kst_prog_say(p, KST_PROG_MSG_LEAVE_CUT, 0);
        } else if (op == LINK_KST_OP_VERIFY
                   && o->result == KST_SES_ERR_VERIFY) {
            doubt(p);
            (void)kst_prog_say(p, KST_PROG_MSG_VERIFY_DIFF,
                               kst_prog_ones(o->diff_regs));
        } else {
            if (op == LINK_KST_OP_READ_ALL) {
                if (p->have_servo) {
                    doubt(p);
                }
            }
            if ((unsigned)o->result < KST_PROG_MSG_END - KST_PROG_MSG_SES) {
                (void)kst_prog_say_reg(p, KST_PROG_MSG_SES
                                          + (unsigned)o->result,
                                       o->steps_done, o->fail_reg);
            } else {
                (void)kst_prog_say(p, KST_PROG_MSG_UNKNOWN, o->result);
            }
        }
        break;
    case KST_LINK_OUT_REFUSED:
        if ((unsigned)o->refusal < KST_PROG_MSG_SES - KST_PROG_MSG_REF) {
            (void)kst_prog_say(p, KST_PROG_MSG_REF + (unsigned)o->refusal, 0);
        } else {
            (void)kst_prog_say(p, KST_PROG_MSG_UNKNOWN, (unsigned)o->refusal);
        }
        break;
    case KST_LINK_OUT_NACK:
        (void)kst_prog_say(p, KST_PROG_MSG_OUT + (unsigned)o->kind, o->nack);
        break;
    default:
        if ((unsigned)o->kind < KST_PROG_MSG_REF - KST_PROG_MSG_OUT) {
            (void)kst_prog_say(p, KST_PROG_MSG_OUT + (unsigned)o->kind, 0);
        } else {
            (void)kst_prog_say(p, KST_PROG_MSG_UNKNOWN, (unsigned)o->kind);
        }
        break;
    }
}

/**
 * @brief Whether an outcome is taken.
 *
 * The one of the operation awaited is, by its number.  An IOmcu that started
 * again ends whatever is awaited. The late end of a write given up is taken
 * while nothing else is on its way; with another operation asked for, it is
 * left out: the servo is in doubt already, and the operation asked for is not
 * that outcome's.
 *
 * @param p The programmer state.
 * @param o The outcome.
 * @return true when the outcome is taken.
 */
static bool outcome_due(const kst_prog_t *p, const kst_link_outcome_t *o)
{
    if (o->kind == KST_LINK_OUT_RESTARTED) {
        return true;
    }
    if (p->wait == W_OUTCOME) {
        return o->id == p->wait_id;
    }
    return p->wait == W_NONE && is_write(o->op)
        && !(p->cmd_ready && p->cmd.kind == KST_PROG_CMD_START);
}

/* The command was not taken, or nothing answers: nothing was sent. */
void kst_prog_not_started(kst_prog_t *p, unsigned id, unsigned arg)
{
    kst_link_outcome_t o;

    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DROPPED;
    o.op = (link_kst_op_t)p->op;
    if (is_write(p->op)) {
        (void)write_failed(p, &o);
    }
    p->held_wrote = 0;
    p->wait = W_NONE;
    p->cmd_ready = false;
    p->chain_read = false;
    keep_back(p);
    (void)kst_prog_say(p, id, arg);
}

/**
 * @brief The START awaited is taken back at the gate: the link's task takes it
 *        out of its queue and drops it, whenever that is.
 *
 * @param p The programmer state.
 * @return true when the START was taken back before it was carried out.
 */
static bool taken_back(kst_prog_t *p)
{
    if (p->gate == NULL || !kst_prog_gate_revoke(p->gate, p->wait_id)) {
        return false;
    }
    if (is_write(p->op)) {
        /* Before it was carried out: no field was written. */
        p->wrote = p->wrote_before;
    }
    return true;
}

/**
 * @brief A START nobody answers.
 *
 * One never handed out was not sent, and one taken
 * back at the gate never is.  One whose pass the link's task took is
 * carried out, and its answer is waited for once more.  With no answer
 * then, or with no gate, it may have run: the outcome is the one of an
 * operation that did not answer, and an abort follows it.
 *
 * @param p  The programmer state.
 * @param id The message id of the START.
 */
static void unanswered(kst_prog_t *p, unsigned id)
{
    kst_link_outcome_t o;

    if (!p->cmd_out || taken_back(p)) {
        kst_prog_not_started(p, id, 0);
        return;
    }
    if (p->gate != NULL && !p->cmd_run && id == KST_PROG_MSG_TIMEOUT) {
        p->cmd_run = true;
        p->wait_ms = p->f.now_ms;
        return;
    }
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_NO_ANSWER;
    o.op = (link_kst_op_t)p->op;
    p->written = p->written || is_write(p->op);
    p->no_file = p->no_file || (is_write(p->op) && p->held_no_file);
    outcome(p, &o);
    if (id == KST_PROG_MSG_TIMEOUT) {
        p->cmd.kind = KST_PROG_CMD_ABORT;
        p->cmd.id = 0;
        p->cmd_ready = true;
        if (p->msg.id != KST_PROG_MSG_JOURNAL) {
            (void)kst_prog_say(p, KST_PROG_MSG_TIMEOUT, 0);
        }
    }
}

/**
 * @brief An IOmcu below link 4.15 has no LEAVE.
 *
 * Its one command from PROGRAMMING to PWM is POWER_CYCLED, and with a PD mini
 * answering the port cycles the supply on it: it is sent only where no PD mini
 * answers, where it changes nothing but the port's state, and its end says that
 * no wait was applied.  With a PD mini the session stays and the message says
 * what ends it.
 *
 * @param p The programmer state.
 */
static void leave_old(kst_prog_t *p)
{
    if (!p->f.rd.known || (p->f.rd.flags & LINK_KS_F_RAIL) != 0u) {
        kst_prog_not_started(p, KST_PROG_MSG_LEAVE_SUPPLY, 0);
    } else {
        const kst_link_request_t r = request(p, LINK_KST_OP_POWER_CYCLED);

        p->leave_doubt = false;
        kst_prog_send(p, &r);
    }
}

/* ------------------------------------------------------------------- public */

void kst_prog_init(kst_prog_t *p)
{
    memset(p, 0, sizeof(*p));
    p->msg.reg = 0xFFu;
    p->supply_mv = KST_PROG_SUPPLY_MV;
    p->supply_ma = KST_PROG_SUPPLY_MA;
    p->pwm_wait_ms = KST_PROG_WAIT_MS;
    p->journal_pin = KST_PROG_NO_PIN;
}

void kst_prog_gate_set(kst_prog_t *p, kst_prog_gate_t *g)
{
    p->gate = g;
}

void kst_prog_gate_init(kst_prog_gate_t *g)
{
    atomic_store(&g->pass, 0u);
}

void kst_prog_gate_offer(kst_prog_gate_t *g, uint32_t id)
{
    atomic_store(&g->pass, id);
}

/**
 * @brief The pass of @p id, taken by whoever asks first.
 *
 * @param g  The gate.
 * @param id The START id.
 * @return true when the pass was taken.
 */
static bool gate_take(kst_prog_gate_t *g, uint32_t id)
{
    uint32_t want = id;

    return id != 0u
        && atomic_compare_exchange_strong(&g->pass, &want, 0u);
}

bool kst_prog_gate_claim(kst_prog_gate_t *g, uint32_t id)
{
    return gate_take(g, id);
}

bool kst_prog_gate_revoke(kst_prog_gate_t *g, uint32_t id)
{
    return gate_take(g, id);
}

void kst_prog_screen_entry(kst_prog_t *p)
{
    p->write_enabled = false;
}

void kst_prog_feed(kst_prog_t *p, const kst_prog_feed_t *f)
{
    const bool was_fed = p->fed;
    const bool was_card = p->fed && p->f.card;
    const bool was_stopped = p->fed && p->f.stopped;
    const bool was_up = p->fed && p->f.rd.phase != KST_LINK_DOWN
                     && p->f.rd.phase != KST_LINK_NO_PAGE;
    const uint32_t said = p->msg.seq;
    bool up;

    p->f = *f;
    p->fed = true;
    if (!was_fed) {
        p->outcomes_seen = f->outcomes;
    }
    up = f->rd.phase != KST_LINK_DOWN && f->rd.phase != KST_LINK_NO_PAGE;
    if (f->card && !was_card && p->journal) {
        /* A record made with no card in the slot: the card that comes
         * takes it. */
        file_want(p, FILE_KEEP);
    }

    if (p->wait == W_ANSWER && f->answer_id == p->wait_id) {
        if (f->answer == KST_LINK_START_OK) {
            p->wait = W_OUTCOME;
            p->wait_ms = f->now_ms;
            p->run_ms = f->now_ms;
            /* The first write of the session is one that started: a write
             * the card, a stop or the link kept from starting is none. */
            p->written = p->written || is_write(p->op);
            /* And one that started with no card has no file. */
            p->no_file = p->no_file || (is_write(p->op) && p->held_no_file);
        } else if (f->answer == KST_LINK_START_NO_OP
                   && p->op == LINK_KST_OP_LEAVE) {
            leave_old(p);
        } else {
            if (is_write(p->op)) {
                /* The IOmcu refused it: no field was written. */
                p->wrote = p->wrote_before;
            }
            kst_prog_not_started(p,
                                 KST_PROG_MSG_START + (unsigned)f->answer, 0);
        }
    }
    if (f->outcomes != p->outcomes_seen) {
        p->outcomes_seen = f->outcomes;
        if (outcome_due(p, &f->outcome)) {
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
            } else if (p->wait == W_ANSWER) {
                unanswered(p, KST_PROG_MSG_OUT + KST_LINK_OUT_NO_ANSWER);
            } else {
                kst_prog_not_started(p, KST_PROG_MSG_OUT
                                        + KST_LINK_OUT_NO_ANSWER, 0);
            }
        }
        session_gone(p);
    }
    if (f->stopped && !was_stopped) {
        p->write_enabled = false;
        if (waits_unsent(p)) {
            kst_prog_not_started(p, KST_PROG_MSG_STOPPED, 0);
        }
    }

    if (p->wait == W_OUTCOME && up && f->rd.known
        && (f->rd.flags & LINK_KS_F_BUSY) != 0u) {
        /* The port says the operation runs or plans: the wait for its
         * outcome counts from the last time it said so. */
        p->wait_ms = f->now_ms;
    }
    if (p->wait == W_ANSWER
        && (uint32_t)(f->now_ms - p->wait_ms) >= KST_PROG_START_MS) {
        unanswered(p, KST_PROG_MSG_TIMEOUT);
    } else if (p->wait == W_FILE
               && (uint32_t)(f->now_ms - p->wait_ms) >= KST_PROG_STORE_MS) {
        kst_prog_not_started(p, KST_PROG_MSG_STORE_FAILED, 0);
    } else if (p->wait == W_OUTCOME
               && ((uint32_t)(f->now_ms - p->wait_ms) >= KST_PROG_RUN_MS
                   || (uint32_t)(f->now_ms - p->run_ms)
                          >= KST_PROG_RUN_MAX_MS)) {
        /* The operation may still end on the IOmcu, a read with an
         * image this side never takes: nothing is edited or written
         * before a read that ends. */
        doubt(p);
        p->wait = W_NONE;
        p->chain_read = false;
        p->cmd.kind = KST_PROG_CMD_ABORT;
        p->cmd.id = 0;
        p->cmd_ready = true;
        (void)kst_prog_say(p, KST_PROG_MSG_TIMEOUT, 0);
    }

    if (p->card_op != KST_PROG_CARD_IDLE
        && (uint32_t)(f->now_ms - p->card_ms) >= KST_PROG_CARD_MS) {
        kst_prog_card_done_t late;

        memset(&late, 0, sizeof(late));
        late.id = p->card_id;
        late.result = p->card_op == KST_PROG_CARD_SAVE
                          ? KST_PROG_CARD_WRITE_ERR : KST_PROG_CARD_READ_ERR;
        /* The card may end the save yet: its file is taken then. */
        p->late_id = p->card_op == KST_PROG_CARD_SAVE && !p->card_ready
                         ? p->card_id : 0u;
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
                /* No command of this model put the port there. */
                session_gone(p);
                if (p->msg.seq == said) {
                    /* Unless this feed said how an operation ended. */
                    (void)kst_prog_say(p, KST_PROG_MSG_SESSION_ENDED, 0);
                }
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
    if (out->kind == KST_PROG_CMD_START) {
        /* From here the command may reach the servo, once its pass is
         * claimed. */
        p->cmd_out = true;
        if (p->gate != NULL) {
            kst_prog_gate_offer(p->gate, out->id);
        }
        if (is_write(out->req.op)) {
            p->wrote_before = p->wrote;
            p->wrote |= p->held_wrote;
            p->held_wrote = 0;
        }
    }
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
        return p->op == LINK_KST_OP_LEAVE
                   || p->op == LINK_KST_OP_POWER_CYCLED
                   ? KST_PROG_LEAVING : KST_PROG_WORKING;
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

bool kst_prog_writing(const kst_prog_t *p)
{
    return (p->wait != W_NONE || p->cmd_ready) && is_write(p->op);
}

unsigned kst_prog_write_steps(const kst_prog_t *p)
{
    return kst_prog_writing(p) ? p->plan_n : 0u;
}

bool kst_prog_planning(const kst_prog_t *p)
{
    return kst_prog_state(p) == KST_PROG_WORKING
        && p->f.rd.phase == KST_LINK_PLANNING;
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

bool kst_prog_journal_held(const kst_prog_t *p)
{
    return p->journal;
}

bool kst_prog_journal_other(const kst_prog_t *p)
{
    return p->journal && p->journal_other;
}

uint8_t kst_prog_journal_port(const kst_prog_t *p)
{
    return p->journal_port;
}

uint8_t kst_prog_journal_pin(const kst_prog_t *p)
{
    return p->journal_pin;
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

/**
 * @brief Whether the servo may hold other values than the session found: after
 *        a write that went with no card, and in a session whose writes this
 *        model did not all see, one the IOmcu kept over a link loss or a start
 *        of the panel.
 *
 * The session's backup holds the servo as found then.
 *
 * @param p The programmer state.
 * @return true when it may.
 */
static bool maybe_written(const kst_prog_t *p)
{
    return p->no_file || !p->wrote_known;
}

/* The image the file before a write takes: the servo as the session found
 * it. */
const kst_image_t *kst_prog_file_image(const kst_prog_t *p)
{
    return p->card_auto && maybe_written(p) && p->f.have_backup
         ? &p->f.backup : &p->servo;
}

/* The fields a limit run writes: the two angles, and PULSE LOWER and
 * PULSE UPPER where SERVO PULSES sets them. */
#define RUN_FIELDS (KST_FIELD_BIT(KST_F_LEFT_ANGLE) \
                    | KST_FIELD_BIT(KST_F_RIGHT_ANGLE) \
                    | KST_FIELD_BIT(KST_F_PULSE_LOWER) \
                    | KST_FIELD_BIT(KST_F_PULSE_UPPER))

bool kst_prog_run_matches(const kst_prog_t *p)
{
    return p->run_hold && p->have_servo
        && (kst_prog_differ(&p->run_img, &p->servo) & ~RUN_FIELDS) == 0u;
}

bool kst_prog_save_first(const kst_prog_t *p)
{
    /* A record held says the servo may be half written: its image is no
     * backup, and the record holds the servo as found.  A limit run's
     * later session has the file of its first write; a session after the
     * run saves its own. */
    return p->have_servo && !p->backed_up && !p->journal
        && !kst_prog_in_doubt(p)
        && !(p->run_own && p->run_written && kst_prog_run_matches(p));
}

bool kst_prog_save_none(const kst_prog_t *p)
{
    return kst_prog_save_first(p)
        && (!p->f.card || (maybe_written(p) && !p->f.have_backup));
}

kst_field_set_t kst_prog_changed(const kst_prog_t *p)
{
    return p->have_servo ? kst_prog_differ(&p->servo, &p->draft) : 0u;
}

unsigned kst_prog_changes(const kst_prog_t *p)
{
    return kst_prog_ones(kst_prog_changed(p));
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

    if (!p->have_servo || !kst_prog_field_editable(id)) {
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
        (void)kst_prog_say(p, p->have_servo && !p->fp_ok
                              ? KST_PROG_MSG_READ_ONLY : KST_PROG_MSG_NEED_READ,
                           0);
        return KST_PROG_EDIT_REFUSED;
    }
    if (!kst_prog_field_editable(id)) {
        (void)kst_prog_say(p, KST_PROG_MSG_EDIT_LOCKED, 0);
        return KST_PROG_EDIT_REFUSED;
    }
    if (!kst_field_edit(&trial, id, raw)) {
        (void)kst_prog_say(p, KST_PROG_MSG_EDIT_HARD, KST_RULE_COUNT);
        return KST_PROG_EDIT_REFUSED;
    }
    hard = kst_prog_hard_new(p, &trial);
    if (hard != 0u) {
        (void)kst_prog_say(p, KST_PROG_MSG_EDIT_HARD, kst_prog_lowest(hard));
        return KST_PROG_EDIT_REFUSED;
    }
    soft = kst_prog_soft_asks(p, &p->draft, &trial);
    if (soft != 0u) {
        p->soft = true;
        p->soft_img = trial;
        p->soft_rules = soft;
        return KST_PROG_EDIT_SOFT;
    }
    p->draft = trial;
    hush(p);
    return KST_PROG_EDIT_OK;
}

kst_prog_edit_t kst_prog_step(kst_prog_t *p, kst_field_id_t id, int dir)
{
    const uint16_t raw = kst_field_get(&p->draft, id);

    if ((dir > 0 && raw >= kst_field_raw_max(id)) || (dir <= 0 && raw == 0u)) {
        p->soft = false;
        (void)kst_prog_say(p, KST_PROG_MSG_EDIT_HARD, KST_RULE_COUNT);
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

/**
 * @brief The message for a loaded file, by its kind.
 *
 * @param p The programmer state.
 * @return The message id.
 */
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
void kst_prog_load_take(kst_prog_t *p, const kst_image_t *target)
{
    const unsigned n = kst_prog_ones(kst_prog_differ(&p->servo, target));

    p->draft = *target;
    p->load_skipped = p->soft_skipped;
    (void)kst_prog_say(p, loaded_message(p),
                       n | (kst_prog_ones(p->load_skipped) << 8));
}

void kst_prog_soft_confirm(kst_prog_t *p)
{
    if (p->soft) {
        p->soft = false;
        if (p->soft_load) {
            p->soft_load = false;
            confirm(p, &p->servo, &p->soft_img);
            kst_prog_load_take(p, &p->soft_img);
        } else {
            confirm(p, &p->draft, &p->soft_img);
            p->draft = p->soft_img;
            hush(p);
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
    p->conf_set = 0;
    if (p->have_servo) {
        p->draft = p->servo;
    }
    hush(p);
}

/* --------------------------------------------------------------- operations */

/**
 * @brief What keeps the tester's port from an ENTER.
 *
 * @param p    The programmer state.
 * @param port The port, from 0.
 * @return The message id, or KST_PROG_MSG_NONE.
 */
static kst_prog_msg_id_t from_pwm(const kst_prog_t *p, uint8_t port)
{
    if (p->f.stopped) {
        return KST_PROG_MSG_STOPPED;
    }
    if (bench_armed(p)) {
        return KST_PROG_MSG_NEED_DISARM;
    }
    if (p->f.target != port + 1u) {
        return KST_PROG_MSG_NEED_TARGET;
    }
    if ((p->caps_mv != 0u && p->supply_mv > p->caps_mv)
        || (p->caps_ma != 0u && p->supply_ma > p->caps_ma)) {
        return KST_PROG_MSG_SUPPLY_CAPS;
    }
    if (p->floor_mv != 0u && p->f.supply_read
        && p->f.set_mv < p->floor_mv) {
        /* An output that is on is never raised: the session would run
         * at what it is set to. */
        return (kst_prog_msg_id_t)(KST_PROG_MSG_REF
                                   + LINK_KST_REF_SUPPLY_LOW);
    }
    return KST_PROG_MSG_NONE;
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
        return kst_prog_say(p, m, 0);
    }
    session_gone(p);
    p->port = port;
    p->chain_read = true;
    r = request(p, LINK_KST_OP_ENTER);
    kst_prog_send(p, &r);
    return KST_PROG_MSG_NONE;
}

void kst_prog_bench_armed(kst_prog_t *p, bool armed)
{
    p->armed = armed;
}

bool kst_prog_port_taken(const kst_link_readout_t *rd)
{
    switch (rd->phase) {
    case KST_LINK_DOWN:
        return false;
    case KST_LINK_NO_PAGE:
        /* A page that answered on this link and refuses now: the port is
         * where its last status had it. */
        return rd->was_held;
    case KST_LINK_READY:
        return rd->known && rd->port != LINK_KST_PWM;
    default:
        return true;
    }
}

bool kst_prog_holds_port(const kst_prog_t *p)
{
    switch (kst_prog_state(p)) {
    case KST_PROG_ENTERING:
    case KST_PROG_PROGRAMMING:
    case KST_PROG_WORKING:
    case KST_PROG_LEAVING:
        return true;
    default:
        return p->fed && kst_prog_port_taken(&p->f.rd);
    }
}

void kst_prog_supply_set(kst_prog_t *p, uint16_t mv, uint16_t ma)
{
    p->supply_mv = mv;
    p->supply_ma = ma;
}

void kst_prog_supply_caps(kst_prog_t *p, uint16_t max_mv, uint16_t max_ma)
{
    p->caps_mv = max_mv;
    p->caps_ma = max_ma;
}

void kst_prog_supply_floor(kst_prog_t *p, uint16_t min_mv)
{
    p->floor_mv = min_mv;
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
        return kst_prog_say(p, m, 0);
    }
    r = request(p, LINK_KST_OP_READ_ALL);
    kst_prog_send(p, &r);
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
        return kst_prog_say(p, m, 0);
    }
    r = request(p, LINK_KST_OP_VERIFY);
    r.image = p->servo;
    kst_prog_send(p, &r);
    return KST_PROG_MSG_NONE;
}

bool kst_prog_restore_only(const kst_prog_t *p)
{
    return kst_prog_state(p) == KST_PROG_PROGRAMMING
        && flag(p, LINK_KS_F_LOCKED) && p->f.have_backup;
}

kst_prog_msg_id_t kst_prog_enable_check(const kst_prog_t *p)
{
    const kst_prog_msg_id_t m = idle_safe(p);

    if (m != KST_PROG_MSG_NONE) {
        return m;
    }
    if (kst_prog_restore_only(p)) {
        /* The session's restore is the one write a locked session takes,
         * whatever the servo holds now. */
        return KST_PROG_MSG_NONE;
    }
    if (!p->have_servo) {
        return KST_PROG_MSG_NEED_READ;
    }
    if (kst_prog_journal_asks(p) != KST_PROG_MSG_NONE) {
        /* Nothing is written while the record asks. */
        return kst_prog_journal_asks(p);
    }
    return p->fp_ok ? KST_PROG_MSG_NONE : KST_PROG_MSG_READ_ONLY;
}

kst_prog_msg_id_t kst_prog_enable(kst_prog_t *p, bool on)
{
    kst_prog_msg_id_t m;

    if (!on) {
        p->write_enabled = false;
        return KST_PROG_MSG_NONE;
    }
    m = kst_prog_enable_check(p);
    if (m != KST_PROG_MSG_NONE) {
        return kst_prog_say(p, m, m == kst_prog_journal_asks(p)
                                ? kst_prog_journal_arg(p) : 0u);
    }
    p->write_enabled = true;
    hush(p);
    return KST_PROG_MSG_NONE;
}

/**
 * @brief What a write of the draft needs, the switch left out.
 *
 * @param p   The programmer state.
 * @param arg Receives the argument of the message.
 * @return The message id; KST_PROG_MSG_NONE when it may go.
 */
static kst_prog_msg_id_t write_ready(const kst_prog_t *p, unsigned *arg)
{
    const kst_prog_msg_id_t m = idle_safe(p);

    *arg = 0;
    if (m != KST_PROG_MSG_NONE) {
        return m;
    }
    if (kst_prog_journal_other(p)) {
        /* A read changes nothing about it. */
        *arg = kst_prog_journal_arg(p);
        return KST_PROG_MSG_JOURNAL_OTHER;
    }
    if (!p->have_servo || kst_prog_in_doubt(p)) {
        return KST_PROG_MSG_NEED_READ;
    }
    if (kst_prog_journal_asks(p) != KST_PROG_MSG_NONE) {
        /* Nor about this. */
        *arg = kst_prog_journal_arg(p);
        return kst_prog_journal_asks(p);
    }
    if (!p->fp_ok) {
        return KST_PROG_MSG_READ_ONLY;
    }
    return KST_PROG_MSG_NONE;
}

/**
 * @brief The session's restore while the servo's image is in doubt: taken in a
 *        locked session, where it is the one write, and after a read otherwise.
 *
 * The record's question is answered first.
 *
 * @param p   The programmer state.
 * @param arg Receives the argument of the message.
 * @return The message id; KST_PROG_MSG_NONE when it may go.
 */
static kst_prog_msg_id_t session_restore_ready(const kst_prog_t *p,
                                               unsigned *arg)
{
    *arg = 0;
    if (flag(p, LINK_KS_F_MUST_READ)
        || (p->need_read && !flag(p, LINK_KS_F_LOCKED))) {
        return KST_PROG_MSG_NEED_READ;
    }
    if (kst_prog_journal_asks(p) != KST_PROG_MSG_NONE
        && !flag(p, LINK_KS_F_LOCKED)) {
        *arg = kst_prog_journal_arg(p);
        return kst_prog_journal_asks(p);
    }
    return KST_PROG_MSG_NONE;
}

/**
 * @brief The message for a plan result.
 *
 * @param r      The plan result.
 * @param broken The rules the plan breaks.
 * @param arg    Receives the argument of the message.
 * @return The message id.
 */
static kst_prog_msg_id_t plan_message(kst_plan_result_t r, kst_rules_t broken,
                                      unsigned *arg)
{
    switch (r) {
    case KST_PLAN_OK:
        return KST_PROG_MSG_NONE;
    case KST_PLAN_ERR_RULES:
        *arg = kst_prog_lowest(broken & KST_RULES_HARD);
        return KST_PROG_MSG_EDIT_HARD;
    case KST_PLAN_ERR_R00:
        return (kst_prog_msg_id_t)(KST_PROG_MSG_REF + LINK_KST_REF_PLAN_R00);
    case KST_PLAN_ERR_PAIRING:
        return (kst_prog_msg_id_t)(KST_PROG_MSG_REF
                                   + LINK_KST_REF_PLAN_PAIRING);
    case KST_PLAN_ERR_TOO_COMPLEX:
        return (kst_prog_msg_id_t)(KST_PROG_MSG_SES
                                   + LINK_KST_RESULT_TOO_COMPLEX);
    default:
        return (kst_prog_msg_id_t)(KST_PROG_MSG_REF
                                   + LINK_KST_REF_PLAN_NO_PATH);
    }
}

/**
 * @brief Checks whether a write may go.
 *
 * @param p   The programmer state.
 * @param arg Receives the argument of the message.
 * @return The message id; KST_PROG_MSG_NONE when it may.
 */
static kst_prog_msg_id_t write_check(kst_prog_t *p, unsigned *arg)
{
    kst_rules_t broken = 0;
    kst_prog_msg_id_t m = write_ready(p, arg);

    if (m != KST_PROG_MSG_NONE) {
        return m;
    }
    if (kst_prog_same(&p->servo, &p->draft)) {
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

/**
 * @brief Writes the draft to the servo.
 *
 * @param p    The programmer state.
 * @param from The source of the write.
 * @return The message id.
 */
static kst_prog_msg_id_t write_draft(kst_prog_t *p, kst_prog_from_t from)
{
    kst_link_request_t r;
    unsigned arg;
    const kst_prog_msg_id_t m = write_check(p, &arg);

    if (m != KST_PROG_MSG_NONE) {
        return kst_prog_say(p, m, arg);
    }
    keep_now(p);
    p->soft = false;
    p->soft_load = false;
    p->restoring = (uint8_t)from;
    p->writes = (uint8_t)kst_prog_changes(p);
    p->plan_n = p->plan.n;    /* write_check() planned it */
    r = request(p, LINK_KST_OP_WRITE);
    r.image = p->draft;
    send_write(p, &r, &p->draft);
    return KST_PROG_MSG_NONE;
}

kst_prog_msg_id_t kst_prog_write(kst_prog_t *p)
{
    return write_draft(p, KST_PROG_FROM_NONE);
}

/**
 * @brief Checks whether a release may go.
 *
 * @param p   The programmer state.
 * @param arg Receives the argument of the message.
 * @return The message id; KST_PROG_MSG_NONE when it may.
 */
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
        return kst_prog_say(p, m, arg);
    }
    keep_now(p);
    p->soft = false;
    p->soft_load = false;
    p->restoring = KST_PROG_FROM_NONE;
    p->plan_n = 0;
    r = request(p, LINK_KST_OP_RELEASE);
    send_write(p, &r, &p->plan.target);
    return KST_PROG_MSG_NONE;
}

bool kst_prog_backup_other(const kst_prog_t *p)
{
    return p->fed && p->f.have_backup && p->have_servo && p->wrote_known
        && (kst_prog_differ(&p->f.backup, &p->servo) & ~p->wrote) != 0u;
}

static const kst_image_t *source(const kst_prog_t *p, kst_prog_from_t from)
{
    switch (from) {
    case KST_PROG_FROM_SESSION:
        return p->fed && p->f.have_backup && !kst_prog_backup_other(p)
             ? &p->f.backup : NULL;
    case KST_PROG_FROM_JOURNAL:
        return p->journal && !p->journal_other ? &p->journal_img : NULL;
    case KST_PROG_FROM_RUN:
        return kst_prog_run_matches(p) ? &p->run_img : NULL;
    default:
        return NULL;
    }
}

/**
 * @brief Why source() gives none.
 *
 * @param p    The programmer state.
 * @param from The source of the image.
 * @return The message id.
 */
static kst_prog_msg_id_t from_none(const kst_prog_t *p, kst_prog_from_t from)
{
    return from == KST_PROG_FROM_SESSION && kst_prog_backup_other(p)
         ? KST_PROG_MSG_BACKUP_OTHER : KST_PROG_MSG_NO_RESTORE;
}

bool kst_prog_restore_offered(const kst_prog_t *p, kst_prog_from_t from)
{
    const kst_image_t *src = source(p, from);

    /* The port takes a read and nothing else while it asks for one. */
    if (src == NULL || p->journal_other
        || kst_prog_state(p) != KST_PROG_PROGRAMMING
        || flag(p, LINK_KS_F_MUST_READ)
        || kst_prog_journal_asks(p) != KST_PROG_MSG_NONE) {
        return false;
    }
    if (from == KST_PROG_FROM_SESSION) {
        if (p->need_read && !flag(p, LINK_KS_F_LOCKED)) {
            return false;
        }
        /* A locked session takes this restore and no read before it. */
        return flag(p, LINK_KS_F_LOCKED)
            || (p->have_servo && !kst_prog_same(src, &p->servo));
    }
    return p->have_servo && !kst_prog_same(src, &p->servo);
}

/**
 * @brief What a restore from a record or a file writes: the servo's image with
 *        the editable fields of @p src.
 *
 * Returns the fields that change and gives
 * the ones that differ and stay in @p stay.
 *
 * @param p      The programmer state.
 * @param src    The source image.
 * @param target Receives the target image.
 * @param stay   Receives the fields that stay.
 * @return The count of fields the target differs in.
 */
static unsigned editable_target(const kst_prog_t *p, const kst_image_t *src,
                                kst_image_t *target, unsigned *stay)
{
    unsigned writes = 0;

    *target = p->servo;
    *stay = 0;
    for (unsigned id = 0; id < KST_F_COUNT; ++id) {
        const kst_field_id_t fid = (kst_field_id_t)id;

        if (kst_field_get(src, fid) == kst_field_get(&p->servo, fid)
            || id == KST_F_DUTY_COPY) {
            continue;
        }
        if (kst_prog_field_editable(fid)) {
            (void)kst_field_edit(target, fid, kst_field_get(src, fid));
            writes++;
        } else {
            (*stay)++;
        }
    }
    return writes;
}

kst_prog_msg_id_t kst_prog_restore_plan(kst_prog_t *p, kst_prog_from_t from,
                                        kst_prog_restore_t *out)
{
    const kst_image_t *src = source(p, from);
    kst_image_t target;
    kst_rules_t hard;
    unsigned stay;
    unsigned arg = 0;
    kst_prog_msg_id_t m = idle_safe(p);

    memset(out, 0, sizeof(*out));
    out->from = (uint8_t)from;
    if (m == KST_PROG_MSG_NONE && p->journal_other) {
        return kst_prog_say(p, KST_PROG_MSG_JOURNAL_OTHER,
                            kst_prog_journal_arg(p));
    }
    if (m == KST_PROG_MSG_NONE && src == NULL) {
        m = from_none(p, from);
    }
    if (m == KST_PROG_MSG_NONE) {
        m = session_restore_ready(p, &arg);
        if (from != KST_PROG_FROM_SESSION && m != KST_PROG_MSG_NEED_READ) {
            m = KST_PROG_MSG_NONE;
            arg = 0;
        }
    }
    if (m != KST_PROG_MSG_NONE) {
        return kst_prog_say(p, m, arg);
    }
    if (from == KST_PROG_FROM_SESSION) {
        if (!p->have_servo || kst_prog_in_doubt(p)) {
            /* What the servo holds is not known: the IOmcu plans,
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
            return kst_prog_say(p, m, arg);
        }
        out->writes = p->plan.n;
        out->unchecked = p->plan.unchecked != 0u;
        out->soft = soft_new(p, src);
        return KST_PROG_MSG_NONE;
    }

    m = write_ready(p, &arg);
    if (m != KST_PROG_MSG_NONE) {
        return kst_prog_say(p, m, arg);
    }
    out->writes = (uint8_t)editable_target(p, src, &target, &stay);
    out->stay = (uint8_t)stay;
    if (out->writes == 0u) {
        return kst_prog_say(p, KST_PROG_MSG_NO_RESTORE, out->stay);
    }
    hard = kst_prog_hard_new(p, &target);
    if (hard != 0u) {
        return kst_prog_say(p, KST_PROG_MSG_RESTORE_REFUSED,
                            kst_prog_lowest(hard));
    }
    /* The pending values stay as they are until the restore is confirmed:
     * a page that is cancelled leaves nothing of the backup behind. */
    out->soft = soft_new(p, &target);
    return KST_PROG_MSG_NONE;
}

/**
 * @brief The confirmed restore from a record or a file: its values become the
 *        pending ones and go out as an ordinary write.
 *
 * A restore that does not start leaves the pending values as they were.
 *
 * @param p    The programmer state.
 * @param from The source of the image.
 * @param src  The source image.
 * @return The message id.
 */
static kst_prog_msg_id_t restore_editable(kst_prog_t *p, kst_prog_from_t from,
                                          const kst_image_t *src)
{
    const kst_image_t keep = p->draft;
    const bool soft = p->soft;
    const bool soft_load = p->soft_load;
    kst_image_t target;
    unsigned stay;
    unsigned arg = 0;
    kst_prog_msg_id_t m = idle_safe(p);

    if (m == KST_PROG_MSG_NONE && p->journal_other) {
        return kst_prog_say(p, KST_PROG_MSG_JOURNAL_OTHER,
                            kst_prog_journal_arg(p));
    }
    if (m == KST_PROG_MSG_NONE && src == NULL) {
        m = KST_PROG_MSG_NO_RESTORE;
    }
    if (m == KST_PROG_MSG_NONE) {
        m = write_ready(p, &arg);
    }
    if (m != KST_PROG_MSG_NONE) {
        return kst_prog_say(p, m, arg);
    }
    if (editable_target(p, src, &target, &stay) == 0u) {
        return kst_prog_say(p, KST_PROG_MSG_NO_RESTORE, stay);
    }
    if (kst_prog_hard_new(p, &target) != 0u) {
        return kst_prog_say(p, KST_PROG_MSG_RESTORE_REFUSED,
                            kst_prog_lowest(kst_prog_hard_new(p, &target)));
    }
    p->draft = target;
    p->soft = false;
    p->soft_load = false;
    m = write_draft(p, from);
    if (m != KST_PROG_MSG_NONE) {
        p->draft = keep;
        p->soft = soft;
        p->soft_load = soft_load;
    } else {
        /* Held until the write has an outcome: the file of the servo, the
         * journal's file and the start of the command can each still end
         * it with nothing sent. */
        p->keep = true;
        p->keep_draft = keep;
        p->keep_soft = soft;
        p->keep_soft_load = soft_load;
    }
    return m;
}

kst_prog_msg_id_t kst_prog_restore(kst_prog_t *p,
                                   const kst_prog_restore_t *plan)
{
    kst_link_request_t r;
    const kst_image_t *src = source(p, (kst_prog_from_t)plan->from);
    kst_prog_msg_id_t m;
    unsigned arg = 0;

    if (plan->from != KST_PROG_FROM_SESSION) {
        return restore_editable(p, (kst_prog_from_t)plan->from, src);
    }
    m = idle_safe(p);
    if (m == KST_PROG_MSG_NONE && p->journal_other) {
        return kst_prog_say(p, KST_PROG_MSG_JOURNAL_OTHER,
                            kst_prog_journal_arg(p));
    }
    if (m == KST_PROG_MSG_NONE && src == NULL) {
        m = from_none(p, KST_PROG_FROM_SESSION);
    }
    if (m == KST_PROG_MSG_NONE) {
        m = session_restore_ready(p, &arg);
    }
    if (m == KST_PROG_MSG_NONE && !p->write_enabled) {
        m = KST_PROG_MSG_NEED_ENABLE;
    }
    if (m != KST_PROG_MSG_NONE) {
        return kst_prog_say(p, m, arg);
    }
    keep_now(p);
    p->soft = false;
    p->soft_load = false;
    p->restoring = KST_PROG_FROM_SESSION;
    p->plan_n = plan->unchecked ? 0u : plan->writes;
    r = request(p, LINK_KST_OP_RESTORE);
    r.image = *src;
    r.confirm = plan->unchecked;
    send_write(p, &r, src);
    return KST_PROG_MSG_NONE;
}

bool kst_prog_run_hold(kst_prog_t *p)
{
    if (!p->have_servo) {
        return false;
    }
    p->run_hold = true;
    p->run_own = true;
    p->run_written = false;
    p->run_img = p->servo;
    p->run_backup[0] = '\0';
    return true;
}

void kst_prog_run_written(kst_prog_t *p)
{
    if (p->run_hold && !p->run_written) {
        p->run_written = true;
        if (p->backed_up) {
            memcpy(p->run_backup, p->backup, KST_PROG_NAME_MAX);
            p->run_backup[KST_PROG_NAME_MAX - 1u] = '\0';
        } else {
            p->run_backup[0] = '\0';
        }
    }
}

void kst_prog_run_close(kst_prog_t *p)
{
    p->run_own = false;
}

void kst_prog_run_drop(kst_prog_t *p)
{
    p->run_hold = false;
    p->run_own = false;
    p->run_written = false;
    p->run_backup[0] = '\0';
}

const kst_image_t *kst_prog_run_image(const kst_prog_t *p)
{
    return p->run_hold ? &p->run_img : NULL;
}

const char *kst_prog_run_backup(const kst_prog_t *p)
{
    return p->run_hold ? p->run_backup : "";
}

void kst_prog_forget(kst_prog_t *p)
{
    if (p->wait != W_NONE || p->cmd_ready) {
        /* The record of the write that runs is not one to discard. */
        (void)kst_prog_say(p, KST_PROG_MSG_START + KST_LINK_START_BUSY, 0);
    } else if (p->journal) {
        journal_clear(p);
        (void)kst_prog_say(p, KST_PROG_MSG_FORGOTTEN, 0);
    }
}

/*
 * The end of a session.  The servo is taken to need no power cycle: the
 * port goes back to PWM with the supply as it is.
 *
 * LEAVE (link 4.15) sends no frame and switches nothing: the port holds the
 * line low for pwm_wait_ms and then gives the pin back to the PWM generator.
 * This is the one place a session is ended from; leave_old() is what an
 * IOmcu without LEAVE gets.
 *
 * The port refuses a LEAVE while it asks for a read or holds a servo in
 * doubt.  With a PD mini answering the refusal is the message and STOP,
 * which switches the supply off, is the way out.  With none, STOP leaves the
 * port where it is, and a servo that no longer answers would hold the port
 * for good: there POWER_CYCLED is sent, which changes nothing but the
 * port's state, and its end says that no wait was applied.
 */
kst_prog_msg_id_t kst_prog_leave(kst_prog_t *p)
{
    kst_link_request_t r;
    const kst_prog_msg_id_t m = idle(p);
    const uint16_t fl = p->f.rd.flags;

    if (m != KST_PROG_MSG_NONE) {
        return kst_prog_say(p, m, 0);
    }
    p->leave_doubt = (fl & (LINK_KS_F_MUST_READ | LINK_KS_F_LOCKED)) != 0u
                  && (fl & LINK_KS_F_RAIL) == 0u;
    r = request(p, p->leave_doubt ? LINK_KST_OP_POWER_CYCLED
                                  : LINK_KST_OP_LEAVE);
    kst_prog_send(p, &r);
    return KST_PROG_MSG_NONE;
}

bool kst_prog_wait_set(kst_prog_t *p, uint32_t ms)
{
    if (ms > KST_PROG_WAIT_MAX_MS) {
        return false;
    }
    p->pwm_wait_ms = (uint16_t)ms;
    return true;
}

uint16_t kst_prog_wait_ms(const kst_prog_t *p)
{
    return p->pwm_wait_ms;
}

void kst_prog_abort(kst_prog_t *p)
{
    if (waits_unsent(p) || (p->wait == W_ANSWER && taken_back(p))) {
        kst_prog_not_started(p, KST_PROG_MSG_OUT + KST_LINK_OUT_DROPPED, 0);
    } else if (p->wait != W_NONE
               || (!p->cmd_ready && flag(p, LINK_KS_F_BUSY))) {
        /* Also for an operation this model awaits no longer, or never
         * started, that the port reports as running. */
        p->cmd.kind = KST_PROG_CMD_ABORT;
        p->cmd.id = 0;
        p->cmd_ready = true;
    }
}
