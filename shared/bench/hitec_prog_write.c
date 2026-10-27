/**
 * @file
 * @brief The write side of the programmer for servos by Hitec: the draft and
 *        its edits, the soft rules and their confirmations, the write
 *        switch, the writes, and the record of a write.  See hitec_prog.h.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog.h"

#include <string.h>

#include "hitec_prog_int.h"

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
    return hpg_say2(p, id, arg, 0u);
}

/* --------------------------------------------------------------- queries */

const hitec_image_t *hitec_prog_draft(const hitec_prog_t *p)
{
    return p->have_servo ? &p->draft : NULL;
}

bool hitec_prog_can_edit(const hitec_prog_t *p)
{
    return hitec_prog_state(p) == HITEC_PROG_HELD && hitec_prog_in_mode(p)
           && p->have_servo && !hitec_prog_stopped(p);
}

bool hitec_prog_editable(const hitec_prog_t *p, uint8_t id)
{
    const hitec_param_info_t *info = hitec_param(hitec_prog_layout(p), id);

    return info != NULL && info->access == (uint8_t)HITEC_RW;
}

bool hitec_prog_differs(const hitec_prog_t *p, const hitec_image_t *img,
                        uint8_t id)
{
    const uint8_t layout = hitec_prog_layout(p);
    hitec_value_t a;
    hitec_value_t b;
    hitec_get_t ga;
    hitec_get_t gb;

    if (!p->have_servo) {
        return false;
    }
    ga = hitec_param_get(&p->servo, layout, id, &a);
    gb = hitec_param_get(img != NULL ? img : &p->draft, layout, id, &b);
    if (ga == HITEC_GET_ERR_ARG || gb == HITEC_GET_ERR_ARG) {
        return false;
    }
    /* A decoded value by its value: a cell shared with another parameter
     * changes its stored value, not its meaning. */
    return ga != gb || (ga == HITEC_GET_OK ? a.value != b.value
                                           : a.raw != b.raw);
}

unsigned hpw_differing(const hitec_prog_t *p, const hitec_image_t *img)
{
    unsigned n = 0;

    for (unsigned id = 0; id < (unsigned)HITEC_P_COUNT; ++id) {
        if (hitec_prog_differs(p, img, (uint8_t)id)) {
            ++n;
        }
    }
    return n;
}

unsigned hitec_prog_changes(const hitec_prog_t *p)
{
    return p->have_servo ? hpw_differing(p, &p->draft) : 0u;
}

bool hitec_prog_sum_bad(const hitec_prog_t *p)
{
    hitec_value_t v;

    return p->have_servo && p->series == (uint8_t)HITEC_CLASSIC
           && hitec_param_get(&p->servo, hitec_prog_layout(p),
                              HITEC_P_C_CHECKSUM, &v) == HITEC_GET_OK
           && v.value == HITEC_SUM_BAD;
}

bool hitec_prog_first_done(const hitec_prog_t *p)
{
    return p->wrote;
}

bool hitec_prog_write_progress(const hitec_prog_t *p, unsigned *done,
                               unsigned *total, uint8_t *kind)
{
    const bool on = p->st == ST_SES && p->op == OP_WRITE;
    const unsigned at = p->ses.step_index;

    *done = on ? at : 0u;
    *total = on ? p->ses.plan.n : 0u;
    *kind = on && at < p->ses.plan.n ? p->ses.plan.step[at].kind : 0xFFu;
    return on;
}

/* ------------------------------------------------------------ soft rules */

/**
 * @brief The absolute value of @p v.
 *
 * @param v The value.
 * @return |v|.
 */
static int32_t mag(int32_t v)
{
    return v < 0 ? -v : v;
}

uint16_t hitec_prog_rules(const hitec_prog_t *p, uint8_t id, int32_t value)
{
    hitec_value_t s;
    bool known;
    uint16_t r = 0;

    if (!p->have_servo || hitec_param(hitec_prog_layout(p), id) == NULL) {
        return 0;
    }
    known = hitec_param_get(&p->servo, hitec_prog_layout(p), id, &s)
            == HITEC_GET_OK;
    /* A stored value that fits no encoding is no value to compare with:
     * every change asks. */
    const bool moved = !known || value != s.value;

    switch (id) {
    case HITEC_P_D_SPEED:
    case HITEC_P_C_SPEED:
        if (value < HITEC_PROG_SLOW_PCT) {
            r |= HITEC_RULE_BIT(HITEC_RULE_SPEED);
        }
        break;
    case HITEC_P_D_DEAD_BAND:
    case HITEC_P_C_DEAD_BAND:
        if (!known || value < s.value) {
            r |= HITEC_RULE_BIT(HITEC_RULE_DEAD_BAND);
        }
        break;
    case HITEC_P_D_OVERLOAD:
    case HITEC_P_C_OVERLOAD:
        if (value == HITEC_OVERLOAD_OFF
            && (!known || s.value != HITEC_OVERLOAD_OFF)) {
            r |= HITEC_RULE_BIT(HITEC_RULE_OVERLOAD);
        }
        break;
    case HITEC_P_D_LEFT_END:
    case HITEC_P_D_RIGHT_END:
    case HITEC_P_C_LEFT_END:
    case HITEC_P_C_RIGHT_END:
        if (moved) {
            r |= HITEC_RULE_BIT(HITEC_RULE_END);
        }
        break;
    case HITEC_P_D_NEUTRAL:
    case HITEC_P_C_CENTRE:
        if (moved) {
            r |= HITEC_RULE_BIT(HITEC_RULE_CENTRE);
        }
        break;
    case HITEC_P_D_DIRECTION:
    case HITEC_P_C_DIRECTION:
        if (moved) {
            r |= HITEC_RULE_BIT(HITEC_RULE_DIRECTION);
        }
        break;
    case HITEC_P_D_FAILSAFE:
        if (value != HITEC_FS_LIMP && value != HITEC_FS_OFF
            && mag(value - 1500) > HITEC_PROG_FS_FAR_US) {
            r |= HITEC_RULE_BIT(HITEC_RULE_FS_FAR);
        }
        break;
    case HITEC_P_C_FS_POSITION:
        /* In 0.25us: 6000 is 1500us. */
        if (mag(value - 6000) > 4 * HITEC_PROG_FS_FAR_US) {
            r |= HITEC_RULE_BIT(HITEC_RULE_FS_FAR);
        }
        break;
    default:
        break;
    }
    return r;
}

/**
 * @brief Whether a confirmation covers @p value of @p id: the value
 *        confirmed, or one on its side of the servo's value and no further
 *        from it.
 *
 * @param p     The programmer.
 * @param id    hitec_param_t.
 * @param value The value.
 * @return true when no new confirmation is needed.
 */
static bool covered(const hitec_prog_t *p, uint8_t id, int32_t value)
{
    hitec_value_t s;
    int32_t dv;
    int32_t dc;

    if ((p->soft_has & ((uint64_t)1u << id)) == 0u) {
        return false;
    }
    if (p->soft_val[id] == value) {
        return true;
    }
    if (hitec_param_get(&p->servo, hitec_prog_layout(p), id, &s)
        != HITEC_GET_OK) {
        return false;
    }
    dv = value - s.value;
    dc = p->soft_val[id] - s.value;
    return dv != 0 && (dv > 0) == (dc > 0) && mag(dv) <= mag(dc);
}

/* ------------------------------------------------------------------ edits */

/**
 * @brief Why an edit is not taken now.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it is.
 */
static hitec_prog_msg_id_t edit_gate(const hitec_prog_t *p)
{
    const hitec_prog_msg_id_t m = hpg_held_idle(p, false);

    if (m != HITEC_PROG_MSG_NONE) {
        return m;
    }
    if (!hitec_prog_in_mode(p)) {
        return HITEC_PROG_MSG_NOT_IN_MODE;
    }
    if (!p->have_servo) {
        return HITEC_PROG_MSG_NOT_READ;
    }
    return hitec_prog_stopped(p) ? HITEC_PROG_MSG_STOPPED
                                 : HITEC_PROG_MSG_NONE;
}

/**
 * @brief Take @p value of @p id into a copy of the draft.
 *
 * @param p     The programmer.
 * @param id    hitec_param_t.
 * @param value The value.
 * @param out   Receives the draft with the edit.
 * @return HITEC_PROG_EDIT_OK, or HITEC_PROG_EDIT_REFUSED with its message.
 */
static hitec_prog_edit_t trial(hitec_prog_t *p, uint8_t id, int32_t value,
                               hitec_image_t *out)
{
    const hitec_prog_msg_id_t m = edit_gate(p);
    hitec_set_t r;

    if (m != HITEC_PROG_MSG_NONE) {
        (void)say(p, m, 0u);
        return HITEC_PROG_EDIT_REFUSED;
    }
    if (!hitec_prog_editable(p, id)) {
        (void)say(p, HITEC_PROG_MSG_EDIT_REFUSED, (unsigned)HITEC_SET_ERR_RO);
        return HITEC_PROG_EDIT_REFUSED;
    }
    *out = p->draft;
    r = hitec_param_set(out, hitec_prog_layout(p), id, value);
    if (r != HITEC_SET_OK) {
        (void)say(p, HITEC_PROG_MSG_EDIT_REFUSED, (unsigned)r);
        return HITEC_PROG_EDIT_REFUSED;
    }
    return HITEC_PROG_EDIT_OK;
}

hitec_prog_edit_t hitec_prog_edit(hitec_prog_t *p, uint8_t id, int32_t value)
{
    hitec_image_t next;
    uint16_t rules;

    p->pend = false;
    if (trial(p, id, value, &next) != HITEC_PROG_EDIT_OK) {
        return HITEC_PROG_EDIT_REFUSED;
    }
    if (memcmp(next.mem, p->draft.mem, sizeof(next.mem)) == 0) {
        return HITEC_PROG_EDIT_OK;
    }
    rules = hitec_prog_rules(p, id, value);
    if (rules != 0u && !covered(p, id, value)) {
        p->pend = true;
        p->pend_id = id;
        p->pend_value = value;
        p->pend_rules = rules;
        return HITEC_PROG_EDIT_CHECK;
    }
    p->draft = next;
    return HITEC_PROG_EDIT_OK;
}

uint16_t hitec_prog_pending(const hitec_prog_t *p, uint8_t *id,
                            int32_t *value)
{
    if (!p->pend) {
        return 0;
    }
    if (id != NULL) {
        *id = p->pend_id;
    }
    if (value != NULL) {
        *value = p->pend_value;
    }
    return p->pend_rules;
}

hitec_prog_edit_t hitec_prog_confirm(hitec_prog_t *p)
{
    hitec_image_t next;

    if (!p->pend) {
        return HITEC_PROG_EDIT_REFUSED;
    }
    p->pend = false;
    if (trial(p, p->pend_id, p->pend_value, &next) != HITEC_PROG_EDIT_OK) {
        return HITEC_PROG_EDIT_REFUSED;
    }
    p->draft = next;
    p->soft_has |= (uint64_t)1u << p->pend_id;
    p->soft_val[p->pend_id] = p->pend_value;
    return HITEC_PROG_EDIT_OK;
}

void hitec_prog_cancel(hitec_prog_t *p)
{
    p->pend = false;
}

void hpw_unconfirm(hitec_prog_t *p)
{
    p->soft_has = 0u;
    p->pend = false;
}

void hitec_prog_discard(hitec_prog_t *p)
{
    if (p->have_servo) {
        p->draft = p->servo;
    }
    hpw_unconfirm(p);
}

/* ---------------------------------------------------------------- switch */

hitec_prog_msg_id_t hitec_prog_write_enable(hitec_prog_t *p, bool on)
{
    hitec_prog_msg_id_t m;

    if (!on) {
        p->write_on = false;
        return HITEC_PROG_MSG_NONE;
    }
    m = hpg_held_idle(p, true);
    if (m == HITEC_PROG_MSG_NONE && !hitec_prog_in_mode(p)) {
        m = HITEC_PROG_MSG_NOT_IN_MODE;
    }
    if (m == HITEC_PROG_MSG_NONE && !p->have_servo) {
        m = HITEC_PROG_MSG_NOT_READ;
    }
    if (m != HITEC_PROG_MSG_NONE) {
        return say(p, m, 0u);
    }
    p->write_on = true;
    return HITEC_PROG_MSG_NONE;
}

bool hitec_prog_write_on(const hitec_prog_t *p)
{
    return p->write_on;
}

/* ---------------------------------------------------------------- writes */

/**
 * @brief Whether FACTORY DEFAULT changes the servo: a classic servo whose
 *        factory image differs from it in a cell a write sends.
 *
 * @param p The programmer.
 * @return true when it does.
 */
static bool factory_differs(const hitec_prog_t *p)
{
    hitec_image_t img;

    /* Every cell the reset copies counts, a cell no parameter names
     * included. */
    return hitec_prog_factory_image(p, &img) && hpf_writable_differs(p, &img);
}

hitec_prog_msg_id_t hitec_prog_write_gate(const hitec_prog_t *p,
                                          uint8_t kind)
{
    hitec_prog_msg_id_t m = hpg_held_idle(p, true);

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
        /* A file of the card is on its way: the backup file waits for
         * the card as well. */
        return HITEC_PROG_MSG_BUSY;
    }
    /* RESTORE of the record keeps it; so does RESTORE of the session's
     * backup where one stands.  Every other write needs none. */
    if (kind == (uint8_t)HITEC_PROG_WR_RESTORE
        || (kind == (uint8_t)HITEC_PROG_WR_BACKUP && p->rec)) {
        if (!p->rec || p->rec_open
            || p->rec_verdict == (uint8_t)HITEC_PROG_REC_NONE) {
            return HITEC_PROG_MSG_MUST_READ;
        }
        if (p->rec_verdict == (uint8_t)HITEC_PROG_REC_OTHER) {
            return HITEC_PROG_MSG_REC_OTHER;
        }
    } else if (p->rec) {
        return HITEC_PROG_MSG_RECORD_HELD;
    }
    if (kind == (uint8_t)HITEC_PROG_WR_EDITS) {
        if (hitec_prog_changes(p) == 0u) {
            return HITEC_PROG_MSG_NO_CHANGES;
        }
    } else if (hitec_prog_changes(p) != 0u) {
        return HITEC_PROG_MSG_EDITS_PENDING;
    }
    if (kind == (uint8_t)HITEC_PROG_WR_REPAIR && !hitec_prog_sum_bad(p)) {
        return HITEC_PROG_MSG_SUM_OK;
    }
    if ((kind == (uint8_t)HITEC_PROG_WR_BACKUP && !hitec_prog_backup_differs(p))
        || (kind == (uint8_t)HITEC_PROG_WR_FACTORY && !factory_differs(p))) {
        return HITEC_PROG_MSG_NO_CHANGES;
    }
    return p->write_on ? HITEC_PROG_MSG_NONE : HITEC_PROG_MSG_WRITE_OFF;
}

/**
 * @brief The record's text is due on the card.
 *
 * @param p The programmer.
 */
static void file_due(hitec_prog_t *p)
{
    p->file_wanted = true;
}

/**
 * @brief The record ends: the write it stood for is verified, or nothing
 *        of it was sent.
 *
 * @param p The programmer.
 */
static void rec_clear(hitec_prog_t *p)
{
    if (p->rec) {
        p->rec = false;
        p->rec_open = false;
        p->rec_verdict = (uint8_t)HITEC_PROG_REC_NONE;
        file_due(p);
    }
}

/**
 * @brief A write that ended before a step reached the servo: the record
 *        goes with it, a RESTORE's excepted, and the switch goes off.
 *
 * @param p The programmer.
 * @param r The session's result.
 */
static void not_sent(hitec_prog_t *p, hitec_ses_t r)
{
    p->st = ST_IDLE;
    p->op = OP_NONE;
    p->write_on = false;
    if (p->wr_own_rec) {
        rec_clear(p);
    }
    if (r == HITEC_SES_ERR_PLAN) {
        (void)say(p, HITEC_PROG_MSG_MUST_READ, 0u);
    } else {
        hpg_ses_say(p, r);
    }
}

/**
 * @brief The write goes out: its plan, step by step.
 *
 * @param p The programmer.
 */
static void go(hitec_prog_t *p)
{
    hitec_prog_msg_id_t m;
    hitec_ses_t r;

    p->st = ST_IDLE;
    /* The gate again, as it is now: the card took its time, and the port,
     * the bench, the stop, the entry or the switch may have changed. */
    m = hpg_held_idle(p, true);
    if (m == HITEC_PROG_MSG_NONE && !hitec_prog_in_mode(p)) {
        m = HITEC_PROG_MSG_NOT_IN_MODE;
    }
    if (m == HITEC_PROG_MSG_NONE && !p->write_on) {
        m = HITEC_PROG_MSG_WRITE_OFF;
    }
    if (m != HITEC_PROG_MSG_NONE) {
        /* Nothing went to the servo: the record of this write goes, as
         * KST's journal of a write that did not start; a RESTORE keeps the
         * record it restores from. */
        if (p->wr_own_rec) {
            rec_clear(p);
        }
        (void)say(p, m, 0u);
        return;
    }
    r = hitec_session_write(&p->ses, &p->wr_target);
    if (r == HITEC_SES_BUSY) {
        p->st = ST_SES;
        p->op = OP_WRITE;
        p->wrote = true;
        hpg_pump(p);
        return;
    }
    if (r == HITEC_SES_OK) {
        /* An empty plan: the servo holds the target already. */
        hpw_write_done(p, r);
        return;
    }
    /* Refused before a step: the session's count of steps sent is still
     * the last write's. */
    not_sent(p, r);
}

hitec_prog_msg_id_t hpw_start(hitec_prog_t *p, uint8_t kind,
                              const hitec_image_t *target)
{
    const hitec_prog_msg_id_t m = hitec_prog_write_gate(p, kind);

    if (m != HITEC_PROG_MSG_NONE) {
        return say(p, m, 0u);
    }
    /* The session plans it again; a plan refused here sends nothing and
     * records nothing. */
    if (hitec_plan(&p->ses.image, target, &p->ses.plan) != HITEC_PLAN_OK) {
        return say(p, HITEC_PROG_MSG_MUST_READ, 0u);
    }
    p->wr_kind = kind;
    p->wr_target = *target;
    p->wr_values = (uint8_t)hpw_differing(p, target);
    p->wr_own_rec = false;
    hpw_unconfirm(p);
    if (p->ses.plan.n != 0u && hpf_backup(p)) {
        /* The session's backup goes to its file first. */
        return HITEC_PROG_MSG_NONE;
    }
    hpw_after_backup(p);
    return HITEC_PROG_MSG_NONE;
}

void hpw_after_backup(hitec_prog_t *p)
{
    const uint8_t kind = p->wr_kind;

    p->st = ST_IDLE;
    if (kind != (uint8_t)HITEC_PROG_WR_RESTORE
        && !(kind == (uint8_t)HITEC_PROG_WR_BACKUP && p->rec)
        && p->ses.plan.n != 0u) {
        /* A restore keeps the record it restores from. */
        p->wr_own_rec = true;
        p->rec = true;
        p->rec_open = false;
        p->rec_verdict = (uint8_t)HITEC_PROG_REC_NONE;
        p->rec_r.series = p->ses.series;
        p->rec_r.port = p->port;
        p->rec_r.pin = hitec_prog_pin(p);
        p->rec_r.before = p->ses.image;
        p->rec_r.target = p->wr_target;
        file_due(p);
        if (p->f.card) {
            p->st = ST_RECORD;
            p->file_ms = p->f.now_ms;
            /* A text the card has not answered yet is of before this
             * write: its answer releases nothing. */
            p->file_rec = false;
            return;
        }
    }
    go(p);
}

hitec_prog_msg_id_t hitec_prog_write(hitec_prog_t *p)
{
    return hpw_start(p, (uint8_t)HITEC_PROG_WR_EDITS, &p->draft);
}

hitec_prog_msg_id_t hitec_prog_repair(hitec_prog_t *p)
{
    hitec_image_t img = p->servo;

    (void)hitec_c_rebalance(&img);
    return hpw_start(p, (uint8_t)HITEC_PROG_WR_REPAIR, &img);
}

/**
 * @brief The addresses a write and the record compare: what a plan writes,
 *        without the classic balance.
 *
 * @param series hitec_family_t.
 * @param addr   The address.
 * @return true when compared.
 */
static bool compared(uint8_t series, uint8_t addr)
{
    return hitec_writable(series, addr);
}

/**
 * @brief The value at @p addr of @p img in its series' width.
 *
 * @param img  The image.
 * @param addr The address.
 * @return The cell or the register.
 */
static unsigned at(const hitec_image_t *img, uint8_t addr)
{
    return img->series == (uint8_t)HITEC_D ? hitec_image_reg(img, addr)
                                           : hitec_image_cell(img, addr);
}

bool hitec_prog_restore_image(const hitec_prog_t *p, hitec_image_t *out)
{
    const hitec_card_record_t *r = &p->rec_r;
    uint8_t addrs[HITEC_C_BLOCK_LEN];
    const unsigned n = hitec_card_addrs(r->series, addrs);

    if (!p->rec || !p->have_servo || r->series != p->servo.series) {
        return false;
    }
    *out = p->servo;
    for (unsigned i = 0; i < n; ++i) {
        if (!compared(r->series, addrs[i])) {
            continue;
        }
        if (r->series == (uint8_t)HITEC_D) {
            (void)hitec_image_put_reg(out, addrs[i],
                                      hitec_image_reg(&r->before, addrs[i]));
        } else {
            (void)hitec_image_put_cell(out, addrs[i],
                                       hitec_image_cell(&r->before,
                                                        addrs[i]));
        }
    }
    if (r->series == (uint8_t)HITEC_CLASSIC) {
        (void)hitec_c_rebalance(out);
    }
    return true;
}

hitec_prog_msg_id_t hitec_prog_restore(hitec_prog_t *p)
{
    hitec_image_t img;

    if (!hitec_prog_restore_image(p, &img)) {
        const hitec_prog_msg_id_t m =
            hitec_prog_write_gate(p, (uint8_t)HITEC_PROG_WR_RESTORE);

        return say(p, m != HITEC_PROG_MSG_NONE ? m
                                               : HITEC_PROG_MSG_REC_OTHER,
                   0u);
    }
    return hpw_start(p, (uint8_t)HITEC_PROG_WR_RESTORE, &img);
}

/**
 * @brief How many addresses the read-back found different.
 *
 * @param p The programmer.
 * @return The count.
 */
static unsigned diff_count(const hitec_prog_t *p)
{
    unsigned n = 0;

    for (unsigned a = 0; a < HITEC_MEM_SIZE; ++a) {
        if (hitec_ses_in_set(p->ses.diff, (uint8_t)a)) {
            ++n;
        }
    }
    return n;
}

void hpw_write_done(hitec_prog_t *p, hitec_ses_t r)
{
    const uint8_t kind = p->wr_kind;
    const hitec_plan_t *plan = &p->ses.plan;

    p->st = ST_IDLE;
    p->op = OP_NONE;
    if (r == HITEC_SES_OK) {
        p->servo = p->ses.image;
        p->draft = p->servo;
        p->must_read = false;
        rec_clear(p);
        (void)say(p, kind == (uint8_t)HITEC_PROG_WR_REPAIR
                         ? HITEC_PROG_MSG_REPAIRED
                     : kind == (uint8_t)HITEC_PROG_WR_RESTORE
                         ? HITEC_PROG_MSG_RESTORED
                     : kind == (uint8_t)HITEC_PROG_WR_BACKUP
                         ? HITEC_PROG_MSG_BACKUP_RESTORED
                     : kind == (uint8_t)HITEC_PROG_WR_FACTORY
                         ? HITEC_PROG_MSG_FACTORY_DONE
                         : HITEC_PROG_MSG_WRITTEN,
                  p->wr_values);
        return;
    }
    p->write_on = false;
    if (r == HITEC_SES_ERR_VERIFY) {
        /* The read-back is what the servo holds now; READ ALL compares it
         * with the record. */
        p->servo = p->ses.image;
        p->must_read = true;
        (void)say(p, HITEC_PROG_MSG_VERIFY_FAIL, diff_count(p));
        return;
    }
    if (p->ses.sent == 0u) {
        /* Nothing reached the servo. */
        not_sent(p, r);
        return;
    }
    p->must_read = true;
    if (p->ses.step_index >= plan->n) {
        /* Every write went out; the read-back did not end. */
        (void)hpg_say2(p, HITEC_PROG_MSG_WRITE_CUT, 0u, HITEC_PROG_CUT_CHECK);
    } else {
        const hitec_step_t *st = &plan->step[p->ses.step_index];
        /* A balance names the cell it balances. */
        const unsigned addr = st->kind == (uint8_t)HITEC_STEP_C_SUM
                                      && p->ses.step_index > 0u
                                  ? plan->step[p->ses.step_index - 1u].addr
                                  : st->addr;

        (void)hpg_say2(p, HITEC_PROG_MSG_WRITE_CUT, addr, st->kind);
    }
}

/* ---------------------------------------------------------------- record */

bool hitec_prog_record(const hitec_prog_t *p)
{
    return p->rec;
}

hitec_prog_rec_t hitec_prog_record_verdict(const hitec_prog_t *p)
{
    return p->rec && !p->rec_open ? (hitec_prog_rec_t)p->rec_verdict
                                  : HITEC_PROG_REC_NONE;
}

/**
 * @brief What the servo read holds of the record.
 *
 * Another series, another port or another D series model is another
 * servo, and so is an address that holds neither the value before the
 * write nor the value the write was to leave.
 *
 * @param p The programmer.
 * @return hitec_prog_rec_t.
 */
static hitec_prog_rec_t verdict(const hitec_prog_t *p)
{
    const hitec_card_record_t *r = &p->rec_r;
    const hitec_image_t *s = &p->servo;
    uint8_t addrs[HITEC_C_BLOCK_LEN];
    const unsigned n = hitec_card_addrs(r->series, addrs);
    unsigned old = 0;
    unsigned now = 0;

    if (r->series != s->series || r->port != p->port
        || (r->series == (uint8_t)HITEC_D
            && at(s, HITEC_D_REG_MODEL) != at(&r->before, HITEC_D_REG_MODEL))) {
        return HITEC_PROG_REC_OTHER;
    }
    for (unsigned i = 0; i < n; ++i) {
        const uint8_t a = addrs[i];
        const unsigned b = at(&r->before, a);
        const unsigned t = at(&r->target, a);
        const unsigned v = at(s, a);

        if (!compared(r->series, a)) {
            continue;
        }
        if (v == b && b != t) {
            ++old;
        } else if (v == t && b != t) {
            ++now;
        } else if (v != b) {
            return HITEC_PROG_REC_OTHER;
        }
    }
    return now == 0u  ? HITEC_PROG_REC_SAME
         : old == 0u  ? HITEC_PROG_REC_DONE
                      : HITEC_PROG_REC_MIX;
}

/**
 * @brief Compare the servo read with the record and say what it holds.
 *
 * @param p The programmer.
 */
static void rec_compare(hitec_prog_t *p)
{
    static const uint8_t k_msg[] = {
        [HITEC_PROG_REC_NONE]  = HITEC_PROG_MSG_RECORD,
        [HITEC_PROG_REC_SAME]  = HITEC_PROG_MSG_REC_SAME,
        [HITEC_PROG_REC_DONE]  = HITEC_PROG_MSG_REC_DONE,
        [HITEC_PROG_REC_MIX]   = HITEC_PROG_MSG_REC_MIX,
        [HITEC_PROG_REC_OTHER] = HITEC_PROG_MSG_REC_OTHER,
    };
    const hitec_prog_rec_t v = verdict(p);

    p->rec_open = false;
    p->rec_verdict = (uint8_t)v;
    (void)hpg_say2(p, (hitec_prog_msg_id_t)k_msg[v], p->rec_r.pin,
                   p->rec_r.port);
}

hitec_prog_msg_id_t hitec_prog_forget(hitec_prog_t *p)
{
    if (p->st == ST_RECORD || p->st == ST_BACKUP
        || (p->st == ST_SES && p->op == OP_WRITE)) {
        return say(p, HITEC_PROG_MSG_BUSY, 0u);
    }
    if (!p->rec) {
        return HITEC_PROG_MSG_NONE;
    }
    rec_clear(p);
    return say(p, HITEC_PROG_MSG_FORGOTTEN, 0u);
}

bool hitec_prog_file_due(const hitec_prog_t *p)
{
    return p->file_wanted && !p->file_busy;
}

size_t hitec_prog_file_take(hitec_prog_t *p, char *out, size_t n)
{
    size_t len;

    if (!hitec_prog_file_due(p) || !p->fed || !p->f.card) {
        return 0;
    }
    len = hitec_card_text(p->rec ? &p->rec_r : NULL, out, n);
    if (len != 0u) {
        p->file_wanted = false;
        p->file_busy = true;
        p->file_rec = p->st == ST_RECORD;
    }
    return len;
}

void hitec_prog_file_done(hitec_prog_t *p, bool ok)
{
    const bool mine = p->file_rec;

    if (!p->file_busy) {
        return;
    }
    p->file_busy = false;
    p->file_rec = false;
    if (p->st != ST_RECORD || !mine) {
        /* No write waits, or the answer is to a text of before the write
         * that waits: that write's text is due still and goes next. */
        return;
    }
    if (ok) {
        go(p);
        return;
    }
    p->st = ST_IDLE;
    rec_clear(p);
    (void)say(p, HITEC_PROG_MSG_RECORD_FAILED, 0u);
}

bool hitec_prog_file_load(hitec_prog_t *p, const char *text, size_t len)
{
    hitec_card_record_t r;
    const hitec_card_result_t got = hitec_card_parse(text, len, &r);

    if (got == HITEC_CARD_NONE) {
        return true;
    }
    if (got != HITEC_CARD_OK) {
        (void)say(p, HITEC_PROG_MSG_RECORD_BAD, (unsigned)got);
        return false;
    }
    p->rec = true;
    p->rec_open = true;
    p->rec_verdict = (uint8_t)HITEC_PROG_REC_NONE;
    p->rec_r = r;
    if (p->have_servo && hitec_prog_in_mode(p)) {
        rec_compare(p);
    } else {
        (void)hpg_say2(p, HITEC_PROG_MSG_RECORD, r.pin, r.port);
    }
    return true;
}

/* -------------------------------------------------------- the seam's half */

void hpw_connect(hitec_prog_t *p)
{
    p->write_on = false;
    p->must_read = false;
    p->wrote = false;
    hpw_unconfirm(p);
}

void hpw_off(hitec_prog_t *p)
{
    p->write_on = false;
    p->pend = false;
}

void hpw_read_done(hitec_prog_t *p, bool had, const hitec_image_t *was)
{
    bool changed = !had;

    for (unsigned a = 0; had && a < HITEC_MEM_SIZE; ++a) {
        if (compared(p->servo.series, (uint8_t)a)
            && at(&p->servo, (uint8_t)a) != at(was, (uint8_t)a)) {
            changed = true;
        }
    }
    if (changed) {
        p->write_on = false;
    }
    p->draft = p->servo;
    p->must_read = false;
    hpw_unconfirm(p);
    if (p->rec) {
        rec_compare(p);
    }
}

void hpw_tick(hitec_prog_t *p)
{
    if (p->f.stopped || hitec_prog_stopped(p)) {
        p->write_on = false;
    }
    if (p->st == ST_RECORD
        && p->f.now_ms - p->file_ms >= HITEC_PROG_CARD_MS) {
        p->st = ST_IDLE;
        rec_clear(p);
        (void)say(p, HITEC_PROG_MSG_RECORD_FAILED, 0u);
    }
}

bool hpw_abort(hitec_prog_t *p)
{
    if (p->st != ST_RECORD) {
        return false;
    }
    p->st = ST_IDLE;
    rec_clear(p);
    (void)say(p, HITEC_PROG_MSG_ABORTED, 0u);
    return true;
}
