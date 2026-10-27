/**
 * @file
 * @brief The files and backups of the programmer for servos by Hitec: the
 *        session's backup and its file, the servo files on the card (FILES,
 *        a save, a load), RESTORE of the session's backup and FACTORY
 *        DEFAULT.  See hitec_prog.h.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog.h"

#include <string.h>

#include "hitec_prog_int.h"

/** @brief The D series position register: it moves, a file does not. */
#define D_POSITION 0x0Cu

/**
 * @brief Say message @p id with its two arguments.
 *
 * @param p   The programmer.
 * @param id  hitec_prog_msg_id_t.
 * @param arg Its argument.
 * @param reg Its second argument.
 * @return @p id.
 */
static hitec_prog_msg_id_t say(hitec_prog_t *p, hitec_prog_msg_id_t id,
                               unsigned arg, unsigned reg)
{
    return hpg_say2(p, id, arg, reg);
}

/**
 * @brief Copy a base name, at most HITEC_CARD_NAME_MAX - 1 characters.
 *
 * @param to   Receives the name.
 * @param from The name.
 */
static void name_copy(char *to, const char *from)
{
    size_t n = strlen(from);

    n = n < HITEC_CARD_NAME_MAX ? n : HITEC_CARD_NAME_MAX - 1u;
    memcpy(to, from, n);
    to[n] = '\0';
}

/* ---------------------------------------------------- the session's backup */

void hpf_session(hitec_prog_t *p)
{
    /* A save answered later is of the session that asked for it. */
    p->bk_gen++;
    p->have_bk = false;
    p->bk_saved = false;
    p->bk_no_file = false;
    p->bk_name[0] = '\0';
    p->load_pend = false;
}

/**
 * @brief Whether two images of one series are of one servo: for the D
 *        series one model number; for a classic servo one name, date,
 *        serial and version.
 *
 * Two servos of one D series model are not told apart.
 *
 * @param a An image of a full read.
 * @param b Another, of the same series.
 * @return true when they are.
 */
static bool same_servo(const hitec_image_t *a, const hitec_image_t *b)
{
    static const hitec_range_t k_id[] = {
        { HITEC_C_NAME, HITEC_C_NAME_LEN },
        { HITEC_C_DATE, 4u },
        { HITEC_C_SERIAL, HITEC_C_SERIAL_LEN },
    };

    if (a->series == (uint8_t)HITEC_D) {
        return hitec_image_reg(a, HITEC_D_REG_MODEL)
               == hitec_image_reg(b, HITEC_D_REG_MODEL);
    }
    for (size_t r = 0; r < sizeof(k_id) / sizeof(k_id[0]); ++r) {
        for (unsigned c = 0; c < k_id[r].count; ++c) {
            const uint8_t at = (uint8_t)(k_id[r].first + c);

            if (hitec_image_cell(a, at) != hitec_image_cell(b, at)) {
                return false;
            }
        }
    }
    return a->has_version == b->has_version && a->version == b->version;
}

/**
 * @brief Whether two images are of one model: the D series model number,
 *        or the classic name cells.
 *
 * @param a An image.
 * @param b Another, of the same series.
 * @return true when they are.
 */
static bool same_model(const hitec_image_t *a, const hitec_image_t *b)
{
    if (a->series == (uint8_t)HITEC_D) {
        return hitec_image_reg(a, HITEC_D_REG_MODEL)
               == hitec_image_reg(b, HITEC_D_REG_MODEL);
    }
    for (unsigned c = 0; c < HITEC_C_NAME_LEN; ++c) {
        const uint8_t at = (uint8_t)(HITEC_C_NAME + c);

        if (hitec_image_cell(a, at) != hitec_image_cell(b, at)) {
            return false;
        }
    }
    return true;
}

void hpf_read_done(hitec_prog_t *p)
{
    if (p->have_bk && p->bk.series == p->servo.series
        && same_servo(&p->bk, &p->servo)) {
        return;
    }
    /* The first read of the session, or another servo on the port: its
     * own backup, saved before its first write. */
    p->have_bk = true;
    p->bk_gen++;
    p->bk = p->servo;
    p->bk_saved = false;
    p->bk_no_file = false;
    p->bk_name[0] = '\0';
}

/**
 * @brief Write into @p out every address of @p from that a plan writes.
 *
 * @param out  The image written into.
 * @param from The image read from.
 */
static void writable_from(hitec_image_t *out, const hitec_image_t *from)
{
    for (unsigned a = 0; a < HITEC_MEM_SIZE; ++a) {
        const uint8_t at = (uint8_t)a;

        if (!hitec_writable(out->series, at) || !hitec_image_known(from, at)) {
            continue;
        }
        if (out->series == (uint8_t)HITEC_D) {
            (void)hitec_image_put_reg(out, at, hitec_image_reg(from, at));
        } else {
            (void)hitec_image_put_cell(out, at, hitec_image_cell(from, at));
        }
    }
    if (out->series == (uint8_t)HITEC_CLASSIC) {
        (void)hitec_c_rebalance(out);
    }
}

bool hitec_prog_backup_image(const hitec_prog_t *p, hitec_image_t *out)
{
    if (!p->have_bk || !p->have_servo || p->bk.series != p->servo.series) {
        return false;
    }
    *out = p->servo;
    writable_from(out, &p->bk);
    return true;
}

bool hpf_writable_differs(const hitec_prog_t *p, const hitec_image_t *img)
{
    for (unsigned a = 0; a < HITEC_MEM_SIZE; ++a) {
        const uint8_t at = (uint8_t)a;

        if (hitec_writable(img->series, at)
            && (img->series == (uint8_t)HITEC_D
                    ? hitec_image_reg(img, at)
                          != hitec_image_reg(&p->servo, at)
                    : hitec_image_cell(img, at)
                          != hitec_image_cell(&p->servo, at))) {
            return true;
        }
    }
    return false;
}

bool hitec_prog_backup_differs(const hitec_prog_t *p)
{
    hitec_image_t img;

    return hitec_prog_backup_image(p, &img) && hpf_writable_differs(p, &img);
}

hitec_prog_msg_id_t hitec_prog_restore_backup(hitec_prog_t *p)
{
    hitec_image_t img;

    if (!hitec_prog_backup_image(p, &img)) {
        const hitec_prog_msg_id_t m =
            hitec_prog_write_gate(p, (uint8_t)HITEC_PROG_WR_BACKUP);

        return say(p, m != HITEC_PROG_MSG_NONE ? m : HITEC_PROG_MSG_NOT_READ,
                   0u, 0u);
    }
    return hpw_start(p, (uint8_t)HITEC_PROG_WR_BACKUP, &img);
}

bool hitec_prog_factory_image(const hitec_prog_t *p, hitec_image_t *out)
{
    if (!p->have_servo || p->servo.series != (uint8_t)HITEC_CLASSIC) {
        return false;
    }
    *out = p->servo;
    return hitec_c_factory(out);
}

hitec_prog_msg_id_t hitec_prog_factory(hitec_prog_t *p)
{
    hitec_image_t img;

    if (!hitec_prog_factory_image(p, &img)) {
        const hitec_prog_msg_id_t m =
            hitec_prog_write_gate(p, (uint8_t)HITEC_PROG_WR_FACTORY);

        return say(p, m != HITEC_PROG_MSG_NONE ? m : HITEC_PROG_MSG_NOT_READ,
                   0u, 0u);
    }
    return hpw_start(p, (uint8_t)HITEC_PROG_WR_FACTORY, &img);
}

const char *hitec_prog_backup_name(const hitec_prog_t *p)
{
    return p->have_bk && p->bk_saved ? p->bk_name : "";
}

bool hitec_prog_backup_ram_only(const hitec_prog_t *p)
{
    return p->have_bk && !p->bk_saved && p->bk_no_file;
}

bool hitec_prog_backup_due(const hitec_prog_t *p)
{
    return p->have_bk && !p->bk_saved && p->fed && p->f.card;
}

/* ------------------------------------------------------------- requests */

/**
 * @brief Put a request on its way to the card's side.
 *
 * @param p    The programmer.
 * @param op   hitec_card_op_t.
 * @param name The base name; NULL for none.
 * @param autosave true for the backup file's save.
 */
static void card_ask(hitec_prog_t *p, uint8_t op, const char *name,
                     bool autosave)
{
    if (++p->card_id == 0u) {
        p->card_id = 1u;
    }
    p->card_op = op;
    p->card_ready = true;
    p->card_auto = autosave;
    p->card_gen = p->bk_gen;
    p->card_ms = p->f.now_ms;
    name_copy(p->card_name, name != NULL ? name : "");
}

bool hpf_backup(hitec_prog_t *p)
{
    if (!p->have_bk || p->bk_saved) {
        return false;
    }
    if (!p->f.card) {
        /* No card: the write goes, the backup stays in RAM. */
        p->bk_no_file = true;
        return false;
    }
    card_ask(p, (uint8_t)HITEC_CARD_SAVE, NULL, true);
    p->st = ST_BACKUP;
    return true;
}

bool hitec_prog_card_busy(const hitec_prog_t *p)
{
    return p->card_op != (uint8_t)HITEC_CARD_IDLE;
}

hitec_prog_msg_id_t hitec_prog_files_gate(const hitec_prog_t *p)
{
    const hitec_prog_msg_id_t m = hpg_held_idle(p, true);

    if (m != HITEC_PROG_MSG_NONE) {
        return m;
    }
    if (!hitec_prog_in_mode(p)) {
        return HITEC_PROG_MSG_NOT_IN_MODE;
    }
    if (!p->have_servo) {
        return HITEC_PROG_MSG_NOT_READ;
    }
    if (p->must_read) {
        return HITEC_PROG_MSG_MUST_READ;
    }
    if (hitec_prog_card_busy(p)) {
        return HITEC_PROG_MSG_BUSY;
    }
    return p->f.card ? HITEC_PROG_MSG_NONE : HITEC_PROG_MSG_CARD_NONE;
}

/**
 * @brief Say why a file request is not taken.
 *
 * @param p The programmer.
 * @param m The reason.
 * @return @p m.
 */
static hitec_prog_msg_id_t refuse(hitec_prog_t *p, hitec_prog_msg_id_t m)
{
    return say(p, m, m == HITEC_PROG_MSG_CARD_NONE ? p->f.card_state : 0u,
               0u);
}

hitec_prog_msg_id_t hitec_prog_files(hitec_prog_t *p, unsigned first)
{
    const hitec_prog_msg_id_t m = hitec_prog_files_gate(p);

    if (m != HITEC_PROG_MSG_NONE) {
        return refuse(p, m);
    }
    p->card_first = (uint16_t)(first <= 0xFFFFu ? first : 0xFFFFu);
    card_ask(p, (uint8_t)HITEC_CARD_LIST, NULL, false);
    return HITEC_PROG_MSG_NONE;
}

hitec_prog_msg_id_t hitec_prog_save(hitec_prog_t *p, const char *name)
{
    hitec_prog_msg_id_t m = hitec_prog_files_gate(p);

    if (m == HITEC_PROG_MSG_NONE
        && (name == NULL || name[0] == '\0'
            || strlen(name) >= HITEC_CARD_NAME_MAX)) {
        m = HITEC_PROG_MSG_NAME_BAD;
    }
    if (m != HITEC_PROG_MSG_NONE) {
        return refuse(p, m);
    }
    card_ask(p, (uint8_t)HITEC_CARD_SAVE, name, false);
    return HITEC_PROG_MSG_NONE;
}

hitec_prog_msg_id_t hitec_prog_load(hitec_prog_t *p, const char *name)
{
    hitec_prog_msg_id_t m = hitec_prog_files_gate(p);

    if (m == HITEC_PROG_MSG_NONE
        && (name == NULL || name[0] == '\0'
            || strlen(name) >= HITEC_CARD_NAME_MAX)) {
        m = HITEC_PROG_MSG_CARD_READ;
    }
    if (m != HITEC_PROG_MSG_NONE) {
        return refuse(p, m);
    }
    p->load_pend = false;
    card_ask(p, (uint8_t)HITEC_CARD_LOAD, name, false);
    return HITEC_PROG_MSG_NONE;
}

bool hitec_prog_card_take(hitec_prog_t *p, hitec_card_req_t *out)
{
    if (!p->card_ready) {
        return false;
    }
    p->card_ready = false;
    memset(out, 0, sizeof(*out));
    out->op = p->card_op;
    out->id = p->card_id;
    out->preset = p->card_auto;
    out->first = p->card_op == (uint8_t)HITEC_CARD_LIST ? p->card_first : 0u;
    memcpy(out->name, p->card_name, sizeof(out->name));
    /* The backup file holds the session's backup; a save by hand the
     * servo as read. */
    out->image = p->card_auto ? p->bk : p->servo;
    return true;
}

void hpf_tick(hitec_prog_t *p)
{
    uint8_t op;

    if (!hitec_prog_card_busy(p)
        || p->f.now_ms - p->card_ms < HITEC_PROG_FILES_MS) {
        return;
    }
    /* Given up: an answer that comes later saves only a backup file. */
    op = p->card_op;
    p->card_op = (uint8_t)HITEC_CARD_IDLE;
    p->card_ready = false;
    if (p->st == ST_BACKUP) {
        p->st = ST_IDLE;
    }
    (void)say(p, op == (uint8_t)HITEC_CARD_SAVE ? HITEC_PROG_MSG_CARD_WRITE
                                                : HITEC_PROG_MSG_CARD_READ,
              0u, 0u);
}

bool hpf_abort(hitec_prog_t *p)
{
    if (p->st != ST_BACKUP) {
        return false;
    }
    p->st = ST_IDLE;
    (void)say(p, HITEC_PROG_MSG_ABORTED, 0u, 0u);
    return true;
}

/* -------------------------------------------------------------- answers */

/**
 * @brief The message of an answer that is not OK.
 *
 * @param p The programmer.
 * @param d The answer.
 */
static void card_say(hitec_prog_t *p, const hitec_card_done_t *d)
{
    switch (d->result) {
    case HITEC_CARD_DONE_NO_CARD:
        (void)say(p, HITEC_PROG_MSG_CARD_NONE, p->f.card_state, 0u);
        break;
    case HITEC_CARD_DONE_FULL:
        (void)say(p, HITEC_PROG_MSG_CARD_FULL, 0u, 0u);
        break;
    case HITEC_CARD_DONE_WRITE:
        (void)say(p, HITEC_PROG_MSG_CARD_WRITE, 0u, 0u);
        break;
    case HITEC_CARD_DONE_NAME:
        (void)say(p, HITEC_PROG_MSG_NAME_BAD, 0u, 0u);
        break;
    case HITEC_CARD_DONE_EXISTS:
        (void)say(p, HITEC_PROG_MSG_NAME_EXISTS, 0u, 0u);
        break;
    case HITEC_CARD_DONE_DENIED:
        (void)say(p, HITEC_PROG_MSG_CARD_DENIED, 0u, 0u);
        break;
    case HITEC_CARD_DONE_BAD:
        if (d->parse == (uint8_t)HITEC_CARD_ERR_FIELD) {
            (void)say(p, HITEC_PROG_MSG_LOAD_LINE, d->line, 0u);
        } else if (d->parse == (uint8_t)HITEC_CARD_ERR_TRUNCATED
                   || d->parse == (uint8_t)HITEC_CARD_ERR_CHECK) {
            (void)say(p, HITEC_PROG_MSG_LOAD_DAMAGED, d->parse, 0u);
        } else {
            (void)say(p, HITEC_PROG_MSG_LOAD_BAD, 0u, 0u);
        }
        break;
    default:
        (void)say(p, HITEC_PROG_MSG_CARD_READ, 0u, 0u);
        break;
    }
}

/**
 * @brief The answer to a save: a file by hand, or the backup file a write
 *        waits for.
 *
 * @param p       The programmer.
 * @param d       The answer.
 * @param waiting The request was not given up.
 */
static void save_done(hitec_prog_t *p, const hitec_card_done_t *d,
                      bool waiting)
{
    const bool ok = d->result == (uint8_t)HITEC_CARD_DONE_OK;
    /* The file holds the backup it was asked for, not one that replaced
     * it since: another session's or another servo's. */
    const bool same_bk = p->have_bk && p->card_gen == p->bk_gen;
    hitec_image_t now;

    if (ok && p->card_auto && same_bk) {
        /* A backup file made after the write gave up on it holds the
         * backup all the same. */
        p->bk_saved = true;
        name_copy(p->bk_name, d->name);
    }
    if (p->card_auto) {
        if (waiting && p->st == ST_BACKUP) {
            if (ok) {
                hpw_after_backup(p);
            } else {
                p->st = ST_IDLE;
                card_say(p, d);
            }
        }
        return;
    }
    if (!waiting) {
        return;
    }
    if (!ok) {
        card_say(p, d);
        return;
    }
    name_copy(p->card_name, d->name);
    /* A save by hand before the first write, of the servo as the session
     * found it, is the backup file. */
    if (same_bk && !p->bk_saved && !p->wrote && p->have_servo
        && hitec_prog_backup_image(p, &now)
        && memcmp(now.mem, p->servo.mem, sizeof(now.mem)) == 0) {
        p->bk_saved = true;
        name_copy(p->bk_name, d->name);
    }
    (void)say(p, HITEC_PROG_MSG_SAVED, 0u, 0u);
}

/**
 * @brief The answer to a list.
 *
 * @param p The programmer.
 * @param d The answer.
 */
static void list_done(hitec_prog_t *p, const hitec_card_done_t *d)
{
    if (d->result != (uint8_t)HITEC_CARD_DONE_OK) {
        card_say(p, d);
        return;
    }
    p->files_n = d->count <= HITEC_CARD_FILES ? d->count
                                              : (uint8_t)HITEC_CARD_FILES;
    p->files_total = d->total;
    p->files_first = p->card_first;
    for (unsigned i = 0; i < p->files_n; ++i) {
        name_copy(p->files[i], d->names[i]);
        memcpy(p->models[i], d->models[i], sizeof(p->models[i]));
        p->models[i][HITEC_CARD_MODEL_MAX] = '\0';
    }
    name_copy(p->preset, d->name);
    p->files_seq++;
    if (d->total == 0u) {
        (void)say(p, HITEC_PROG_MSG_NO_FILES, 0u, 0u);
    }
}

/**
 * @brief Copy the addresses of parameter @p info from @p from into @p to,
 *        and mark them in @p set.
 *
 * @param to   The image written into.
 * @param from The file's image.
 * @param info The parameter's row.
 * @param set  Bit a of byte a / 8: address a is copied.
 */
static void copy_param(hitec_image_t *to, const hitec_image_t *from,
                       const hitec_param_info_t *info,
                       uint8_t set[HITEC_MEM_SIZE / 8u])
{
    const bool d = to->series == (uint8_t)HITEC_D;
    uint8_t addrs[HITEC_C_NAME_LEN + 1u];
    unsigned n = 0;

    if (d) {
        addrs[n++] = info->addr;
    } else {
        for (unsigned i = 0; i < info->size && n < HITEC_C_NAME_LEN; ++i) {
            addrs[n++] = (uint8_t)(info->addr + i);
        }
    }
    if (info->addr2 != HITEC_ADDR_NONE) {
        addrs[n++] = info->addr2;
    }
    for (unsigned i = 0; i < n; ++i) {
        const uint8_t a = addrs[i];

        /* A file holds every address of an editable parameter; one it
         * does not know stays as the servo holds it. */
        if (hitec_image_known(from, a)) {
            if (d) {
                (void)hitec_image_put_reg(to, a, hitec_image_reg(from, a));
            } else {
                (void)hitec_image_put_cell(to, a,
                                           hitec_image_cell(from, a));
            }
            set[a / 8u] |= (uint8_t)(1u << (a % 8u));
        }
    }
}

/**
 * @brief The values of @p file the load does not write that differ from
 *        the servo read: addresses outside the editable parameters, the
 *        D series position and the classic balance not counted.
 *
 * @param p    The programmer.
 * @param file The file's image.
 * @param set  The addresses the load writes.
 * @return The count.
 */
static unsigned not_loaded(const hitec_prog_t *p, const hitec_image_t *file,
                           const uint8_t set[HITEC_MEM_SIZE / 8u])
{
    uint8_t addrs[HITEC_CARD_FILE_ADDRS];
    const unsigned n = hitec_card_file_addrs(file->series, addrs);
    const bool d = file->series == (uint8_t)HITEC_D;
    unsigned count = 0;

    for (unsigned i = 0; i < n; ++i) {
        const uint8_t a = addrs[i];

        if ((set[a / 8u] & (1u << (a % 8u))) != 0u
            || (d && a == D_POSITION) || (!d && a == HITEC_C_SUM_LAST)
            || !hitec_image_known(file, a)
            || !hitec_image_known(&p->servo, a)) {
            continue;
        }
        if (d ? hitec_image_reg(file, a) != hitec_image_reg(&p->servo, a)
              : hitec_image_cell(file, a)
                    != hitec_image_cell(&p->servo, a)) {
            ++count;
        }
    }
    return count;
}

/**
 * @brief The soft rules the editable values of @p img break against the
 *        servo read, where they differ from it.
 *
 * @param p      The programmer.
 * @param img    The draft the load leaves.
 * @param layout The layout.
 * @return The rule set.
 */
static uint16_t load_rules(const hitec_prog_t *p, const hitec_image_t *img,
                           uint8_t layout)
{
    uint16_t rules = 0;

    for (unsigned id = 0; id < (unsigned)HITEC_P_COUNT; ++id) {
        const hitec_param_info_t *info = hitec_param(layout, (uint8_t)id);
        hitec_value_t v;

        if (info != NULL && info->access == (uint8_t)HITEC_RW
            && hitec_prog_differs(p, img, (uint8_t)id)
            && hitec_param_get(img, layout, (uint8_t)id, &v)
                   == HITEC_GET_OK) {
            rules |= hitec_prog_rules(p, (uint8_t)id, v.value);
        }
    }
    return rules;
}

/**
 * @brief The load that waits, or one with nothing to confirm, into the
 *        draft: its rules confirmed for its values.
 *
 * @param p The programmer.
 */
static void load_take(hitec_prog_t *p)
{
    const uint8_t layout = hitec_prog_layout(p);

    p->load_pend = false;
    p->draft = p->load_img;
    hpw_unconfirm(p);
    for (unsigned id = 0; id < (unsigned)HITEC_P_COUNT; ++id) {
        hitec_value_t v;

        if (hitec_prog_differs(p, &p->draft, (uint8_t)id)
            && hitec_param_get(&p->draft, layout, (uint8_t)id, &v)
                   == HITEC_GET_OK
            && hitec_prog_rules(p, (uint8_t)id, v.value) != 0u) {
            p->soft_has |= (uint64_t)1u << id;
            p->soft_val[id] = v.value;
        }
    }
    (void)say(p, HITEC_PROG_MSG_LOADED, hpw_differing(p, &p->draft),
              p->load_skipped);
}

/**
 * @brief The answer to a load: refused, the same as the servo, waiting for
 *        CHECK THIS FILE, or the draft.
 *
 * @param p The programmer.
 * @param d The answer.
 */
static void load_done(hitec_prog_t *p, const hitec_card_done_t *d)
{
    uint8_t set[HITEC_MEM_SIZE / 8u];
    const uint8_t layout = hitec_prog_layout(p);
    hitec_prog_msg_id_t m;
    hitec_model_t file;
    unsigned skipped;

    if (d->result != (uint8_t)HITEC_CARD_DONE_OK) {
        card_say(p, d);
        return;
    }
    /* The servo the file is held against may have gone meanwhile. */
    m = hitec_prog_files_gate(p);
    if (m != HITEC_PROG_MSG_NONE && m != HITEC_PROG_MSG_CARD_NONE) {
        (void)refuse(p, m);
        return;
    }
    if (d->image.series != p->servo.series) {
        (void)say(p, HITEC_PROG_MSG_LOAD_SERIES, d->image.series, 0u);
        return;
    }
    /* A file that parses holds the cells or registers the model is read
     * from. */
    memset(&file, 0, sizeof(file));
    (void)hitec_identify(&d->image, &file);
    if (file.layout != layout) {
        (void)say(p, HITEC_PROG_MSG_LOAD_LAYOUT, file.layout, layout);
        return;
    }
    memset(set, 0, sizeof(set));
    p->load_img = p->servo;
    for (unsigned id = 0; id < (unsigned)HITEC_P_COUNT; ++id) {
        const hitec_param_info_t *info = hitec_param(layout, (uint8_t)id);

        if (info != NULL && info->access == (uint8_t)HITEC_RW) {
            copy_param(&p->load_img, &d->image, info, set);
        }
    }
    if (p->load_img.series == (uint8_t)HITEC_CLASSIC) {
        (void)hitec_c_rebalance(&p->load_img);
    }
    skipped = not_loaded(p, &d->image, set);
    p->load_skipped = (uint8_t)(skipped < 255u ? skipped : 255u);
    memcpy(p->load_model, d->model, sizeof(p->load_model));
    p->load_model[HITEC_CARD_MODEL_MAX] = '\0';
    if (hpw_differing(p, &p->load_img) == 0u) {
        /* The file holds the servo's values: the draft becomes them, and
         * an edit standing before the load goes with its confirmations. */
        p->draft = p->load_img;
        hpw_unconfirm(p);
        (void)say(p, HITEC_PROG_MSG_LOAD_SAME, p->load_skipped, 0u);
        return;
    }
    p->load_rules = load_rules(p, &p->load_img, layout);
    p->load_other = !same_model(&d->image, &p->servo);
    if (p->load_rules != 0u || p->load_other) {
        p->load_pend = true;
        (void)say(p, HITEC_PROG_MSG_LOAD_CHECK, 0u, 0u);
        return;
    }
    load_take(p);
}

void hitec_prog_card_done(hitec_prog_t *p, const hitec_card_done_t *d)
{
    const bool waiting = hitec_prog_card_busy(p);

    if (d == NULL || d->id != p->card_id) {
        return;
    }
    p->card_op = (uint8_t)HITEC_CARD_IDLE;
    p->card_ready = false;
    switch (d->op) {
    case HITEC_CARD_SAVE:
        save_done(p, d, waiting);
        break;
    case HITEC_CARD_LIST:
        if (waiting) {
            list_done(p, d);
        }
        break;
    case HITEC_CARD_LOAD:
        /* A file asked for by an earlier session, or against another
         * servo, is no load of this one. */
        if (waiting && p->card_gen == p->bk_gen) {
            load_done(p, d);
        }
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------- the loaded file */

uint16_t hitec_prog_load_pending(const hitec_prog_t *p, bool *other)
{
    if (other != NULL) {
        *other = p->load_pend && p->load_other;
    }
    return p->load_pend ? p->load_rules : 0u;
}

bool hitec_prog_load_waits(const hitec_prog_t *p)
{
    return p->load_pend;
}

const hitec_image_t *hitec_prog_load_image(const hitec_prog_t *p)
{
    return p->load_pend ? &p->load_img : NULL;
}

hitec_prog_msg_id_t hitec_prog_load_confirm(hitec_prog_t *p)
{
    hitec_prog_msg_id_t m;

    if (!p->load_pend) {
        return say(p, HITEC_PROG_MSG_BUSY, 0u, 0u);
    }
    m = hitec_prog_files_gate(p);
    if (m != HITEC_PROG_MSG_NONE && m != HITEC_PROG_MSG_CARD_NONE) {
        p->load_pend = false;
        return refuse(p, m);
    }
    load_take(p);
    return HITEC_PROG_MSG_NONE;
}

void hitec_prog_load_cancel(hitec_prog_t *p)
{
    p->load_pend = false;
}

/* ------------------------------------------------------------- queries */

uint32_t hitec_prog_files_seq(const hitec_prog_t *p)
{
    return p->files_seq;
}

unsigned hitec_prog_file_count(const hitec_prog_t *p, unsigned *total,
                               unsigned *first)
{
    if (total != NULL) {
        *total = p->files_total;
    }
    if (first != NULL) {
        *first = p->files_first;
    }
    return p->files_n;
}

const char *hitec_prog_file_name(const hitec_prog_t *p, unsigned i,
                                 const char **model)
{
    if (model != NULL) {
        *model = i < p->files_n ? p->models[i] : "";
    }
    return i < p->files_n ? p->files[i] : "";
}

const char *hitec_prog_preset_name(const hitec_prog_t *p)
{
    return p->preset;
}

const char *hitec_prog_card_name(const hitec_prog_t *p)
{
    return p->card_name;
}
