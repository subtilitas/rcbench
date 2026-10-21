/**
 * @file
 * @brief The programmer's files on the card: the save, the list and the load of
 *        kst_prog.h as the state sends them and takes their answers back.
 *
 * See kst_prog.h and kst_card.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "kst_prog.h"
#include "kst_prog_int.h"

/* -------------------------------------------------------------- servo files */

/**
 * @brief Whether the card can take a request now.
 *
 * @param p The programmer state.
 * @return KST_PROG_MSG_NONE when it can, else the reason it cannot.
 */
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

/**
 * @brief What a load needs of the servo, when it is asked for and when the file
 *        is there.
 *
 * @param p   The programmer state.
 * @param arg Receives the argument of that message.
 * @return The message for what is missing, or KST_PROG_MSG_NONE.
 */
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
        *arg = kst_prog_journal_arg(p);
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
        return kst_prog_say(p, m, 0);
    }
    p->card_auto = false;
    kst_prog_card_ask(p, KST_PROG_CARD_SAVE, name);
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
        return kst_prog_say(p, m, 0);
    }
    p->card_auto = false;
    p->card_first = (uint16_t)(first <= 0xFFFFu ? first : 0xFFFFu);
    kst_prog_card_ask(p, KST_PROG_CARD_LIST, NULL);
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
        return kst_prog_say(p, m, arg);
    }
    p->card_auto = false;
    p->soft = false;
    p->soft_load = false;
    kst_prog_card_ask(p, KST_PROG_CARD_LOAD, name);
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

unsigned kst_prog_load_named(const kst_prog_t *p, unsigned *fields)
{
    if (fields != NULL) {
        *fields = p->load_fields;
    }
    return p->load_named;
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
    out->image = *kst_prog_file_image(p);
    p->late_img = out->image;
    return true;
}

/**
 * @brief Copies a file name, at most KST_PROG_NAME_MAX - 1 characters.
 *
 * @param to   Receives the name.
 * @param from The name to copy.
 */
static void name_copy(char *to, const char *from)
{
    memcpy(to, from, KST_PROG_NAME_MAX);
    to[KST_PROG_NAME_MAX - 1u] = '\0';
}

/**
 * @brief The message for a card result.
 *
 * @param result The card result.
 * @param load   true for a load, false for a save or a list.
 * @return The message id.
 */
static unsigned card_message(unsigned result, bool load)
{
    switch (result) {
    case KST_PROG_CARD_NO_CARD:
        return KST_PROG_MSG_CARD_NONE;
    case KST_PROG_CARD_FULL:
        return KST_PROG_MSG_CARD_FULL;
    case KST_PROG_CARD_WRITE_ERR:
        return KST_PROG_MSG_CARD_WRITE;
    case KST_PROG_CARD_DENIED:
        return KST_PROG_MSG_CARD_DENIED;
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

/**
 * @brief Takes the answer of a save.
 *
 * @param p The programmer state.
 * @param d The answer of the card.
 */
static void save_done(kst_prog_t *p, const kst_prog_card_done_t *d)
{
    const bool waited = p->card_auto && p->wait == W_SAVE;
    const bool ok = d->result == KST_PROG_CARD_OK;

    if (ok) {
        name_copy(p->card_name, d->name);
    }
    /* A save by hand is the session's backup while it holds the servo as
     * the session found it: before the first write, and with no backup
     * named yet.  A later one holds the written servo and is no backup.
     * In a session whose writes are not all known the servo is as found
     * only while it equals the session's backup. */
    if (ok && (p->card_auto
               || (!p->written && !p->backed_up
                   && (p->wrote_known
                       || (p->f.have_backup
                           && kst_prog_same(&p->f.backup, &p->servo)))))) {
        p->backed_up = p->have_servo;
        p->no_file = false;
        name_copy(p->backup, d->name);
    }
    if (waited) {
        if (ok) {
            kst_prog_write_go(p);
        } else {
            kst_prog_not_started(p, card_message(d->result, false), 0);
        }
    } else if (!p->card_auto) {
        (void)kst_prog_say(p, ok ? KST_PROG_MSG_SAVED
                                 : card_message(d->result, false), 0);
    }
    /* A save whose write was given up meanwhile says nothing more. */
    p->card_auto = false;
}

/**
 * @brief Takes the answer of a list.
 *
 * @param p The programmer state.
 * @param d The answer of the card.
 */
static void list_done(kst_prog_t *p, const kst_prog_card_done_t *d)
{
    if (d->result != KST_PROG_CARD_OK) {
        (void)kst_prog_say(p, card_message(d->result, true), 0);
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
        (void)kst_prog_say(p, KST_PROG_MSG_NO_FILES, 0);
    }
}

/**
 * @brief Takes the answer of a load.
 *
 * @param p The programmer state.
 * @param d The answer of the card.
 */
static void load_done(kst_prog_t *p, const kst_prog_card_done_t *d)
{
    unsigned arg = 0;
    kst_rules_t hard, soft;
    kst_prog_msg_id_t m;

    if (d->result == KST_PROG_CARD_BAD) {
        (void)kst_prog_say(p, KST_PROG_MSG_LOAD_BAD, d->line);
        return;
    }
    if (d->result == KST_PROG_CARD_VALUE) {
        (void)kst_prog_say_reg(p, KST_PROG_MSG_LOAD_VALUE, d->line, d->field);
        return;
    }
    if (d->result == KST_PROG_CARD_LIMIT) {
        (void)kst_prog_say(p, KST_PROG_MSG_LOAD_REFUSED, d->rule);
        return;
    }
    if (d->result == KST_PROG_CARD_TYPE) {
        (void)kst_prog_say(p, KST_PROG_MSG_LOAD_TYPE,
                           d->type | ((unsigned)d->servo_type << 8));
        return;
    }
    if (d->result != KST_PROG_CARD_OK) {
        (void)kst_prog_say(p, card_message(d->result, true), 0);
        return;
    }
    /* The servo the file was compared with may be gone meanwhile. */
    m = load_ready(p, &arg);
    if (m != KST_PROG_MSG_NONE) {
        (void)kst_prog_say(p, m, arg);
        return;
    }
    p->load_kind = d->kind < KST_PROG_FILE_KINDS ? d->kind
                                                 : KST_PROG_FILE_UNTYPED;
    /* A count that does not hold is not shown. */
    p->load_fields = d->named <= d->fields ? d->fields : 0u;
    p->load_named = p->load_fields != 0u ? d->named : 0u;
    p->soft_skipped = d->skipped;
    /* The card's side takes editable fields only: a target that differs
     * anywhere else is not a file's and is not taken. */
    for (unsigned id = 0; id < KST_F_COUNT; ++id) {
        const kst_field_id_t fid = (kst_field_id_t)id;

        if (id != KST_F_DUTY_COPY && !kst_prog_field_editable(fid)
            && kst_field_get(&d->image, fid)
                   != kst_field_get(&p->servo, fid)) {
            (void)kst_prog_say(p, KST_PROG_MSG_LOAD_BAD, 0);
            return;
        }
    }
    if (kst_prog_differ(&p->servo, &d->image) == 0u) {
        (void)kst_prog_say(p, KST_PROG_MSG_LOAD_SAME,
                           kst_prog_ones(d->skipped));
        return;
    }
    hard = kst_prog_hard_new(p, &d->image);
    if (hard != 0u) {
        (void)kst_prog_say(p, KST_PROG_MSG_LOAD_REFUSED, kst_prog_lowest(hard));
        return;
    }
    soft = kst_prog_soft_asks(p, &p->servo, &d->image);
    if (soft != 0u) {
        p->soft = true;
        p->soft_load = true;
        p->soft_img = d->image;
        p->soft_rules = soft;
        (void)kst_prog_say(p, KST_PROG_MSG_LOAD_SOFT, 0);
        return;
    }
    kst_prog_load_take(p, &d->image);
}

/**
 * @brief The card ends a save this model gave up at KST_PROG_CARD_MS.
 *
 * The file is on the card: it is named, and it is the session's backup where a
 * save by hand would be, so that the write that follows makes no second one.
 *
 * @param p The programmer state.
 * @param d The answer of the card.
 */
static void save_late(kst_prog_t *p, const kst_prog_card_done_t *d)
{
    p->late_id = 0;
    if (d->result != KST_PROG_CARD_OK) {
        return;
    }
    if (p->have_servo && kst_prog_same(&p->late_img, &p->servo) && !p->written
        && !p->backed_up && !p->journal && !kst_prog_in_doubt(p)
        && (p->wrote_known
            || (p->f.have_backup && kst_prog_same(&p->f.backup, &p->servo)))) {
        p->backed_up = true;
        p->no_file = false;
        name_copy(p->backup, d->name);
    }
    if (p->card_op == KST_PROG_CARD_IDLE) {
        /* No other file is asked for: the name is this file's. */
        name_copy(p->card_name, d->name);
        (void)kst_prog_say(p, KST_PROG_MSG_SAVED, 0);
    }
}

void kst_prog_card_done(kst_prog_t *p, const kst_prog_card_done_t *done)
{
    const unsigned op = p->card_op;

    if (p->late_id != 0u && done->id == p->late_id
        && (op == KST_PROG_CARD_IDLE || done->id != p->card_id)) {
        save_late(p, done);
        return;
    }
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
