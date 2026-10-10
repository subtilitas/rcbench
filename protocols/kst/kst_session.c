/*
 * SPDX-License-Identifier: MIT
 */

#include "kst_session.h"

#include <string.h>

enum { OP_NONE = 0, OP_ENTER, OP_READ_ALL, OP_VERIFY, OP_WRITE };
enum { T_IDLE = 0, T_GAP, T_BUSY };

enum {
    /* enter */
    E_WAITED = 0,     /* the line was low for KST_ENTRY_LOW_US */
    E_FIRST_READ,
    E_SYNC,
    E_LAST_READ,
    /* write */
    W_PRE = 0,        /* read all before the first write */
    W_WROTE,
    W_READ1,
    W_READ2,
    W_CHECK,          /* read all after a read-back that did not match */
    W_UNDO_WROTE,
    W_UNDO_READ1,
    W_UNDO_READ2,
    W_UNDO_FINAL,     /* read all after a roll-back */
    W_FINAL,          /* read all after the last write */
};

#define ALL_REGS 0xFFFFFFFFu

static uint32_t now(const kst_session_t *s)
{
    return s->drv.now_us(s->drv.ctx);
}

static uint32_t diff_mask(const kst_image_t *a, const kst_image_t *b)
{
    uint32_t mask = 0;

    for (unsigned r = 0; r < KST_REG_COUNT; ++r) {
        if (a->r[r] != b->r[r]) {
            mask |= (uint32_t)1 << r;
        }
    }
    return mask;
}

/* --- one transaction -------------------------------------------------------- */

static void txn_begin(kst_session_t *s, kst_expect_t expect, uint8_t reg,
                      uint8_t value)
{
    /* Every register here is one a frame can address: a read counts up to
     * KST_REG_MAX, and a plan that passed plan_is_sound() holds the planner's
     * writes, registers 0x01 to KST_REG_MAX. */
    if (expect == KST_EXPECT_READ) {
        (void)kst_frame_read(reg, &s->frame);
    } else if (expect == KST_EXPECT_WRITE) {
        (void)kst_frame_write(reg, value, &s->frame);
    } else {
        kst_frame_sync(&s->frame);
    }
    s->t_expect = (uint8_t)expect;
    s->t_frame = 1;
    s->t_state = T_GAP;
}

static void wait_begin(kst_session_t *s, uint32_t us)
{
    s->t_mark = now(s);
    s->t_gap = us;
    s->t_frame = 0;
    s->t_state = T_GAP;
}

static void note_reply(kst_session_t *s)
{
    kst_stats_t *st = &s->stats;
    const kst_reply_t *r = &s->last_reply;

    if (r->result != KST_RX_OK) {
        st->faults++;
        return;
    }
    st->replies++;
    if (s->t_expect != KST_EXPECT_READ) {
        return;
    }
    if (st->delay_max_ns == 0u || r->delay_ns < st->delay_min_ns) {
        st->delay_min_ns = r->delay_ns;
    }
    if (r->delay_ns > st->delay_max_ns) {
        st->delay_max_ns = r->delay_ns;
    }
    if (st->half_max_ns == 0u || r->half_cell_ns < st->half_min_ns) {
        st->half_min_ns = r->half_cell_ns;
    }
    if (r->half_cell_ns > st->half_max_ns) {
        st->half_max_ns = r->half_cell_ns;
    }
}

/* True when the transaction is over and its reply is in last_reply. */
static bool txn_step(kst_session_t *s)
{
    const uint32_t t = now(s);

    if (s->t_state == T_GAP) {
        const kst_expect_t expect = (kst_expect_t)s->t_expect;
        const uint32_t window = kst_reply_window_ns(expect);

        /* Nothing is on the wire yet: an abort takes effect here. */
        if (s->abort != 0u) {
            s->t_state = T_IDLE;
            return true;
        }
        if (t - s->t_mark < s->t_gap) {
            return false;
        }
        if (s->t_frame == 0u) {
            s->t_state = T_IDLE;
            return true;
        }
        if (!s->drv.start(s->drv.ctx, &s->frame, window)) {
            s->fault = 1;
            s->t_state = T_IDLE;
            return true;
        }
        if (expect == KST_EXPECT_WRITE) {
            /* From here the servo may hold part of a plan. */
            s->dirty = 1;
        }
        s->t_start = t;
        s->t_limit = (kst_frame_duration_ns(&s->frame) + window) / 1000u
                     + KST_DRIVER_SLACK_US;
        s->t_state = T_BUSY;
        return false;
    }
    {
        kst_capture_t cap;

        if (!s->drv.poll(s->drv.ctx, &cap)) {
            if (t - s->t_start > s->t_limit) {
                s->fault = 1;
                s->t_state = T_IDLE;
                return true;
            }
            return false;
        }
        (void)kst_reply_decode(&cap, (kst_expect_t)s->t_expect,
                               &s->last_reply);
    }
    note_reply(s);
    s->t_mark = t;
    s->t_gap = s->last_reply.result == KST_RX_OK ? KST_GAP_US
                                                 : KST_GAP_AFTER_FAULT_US;
    s->t_state = T_IDLE;
    return true;
}

static bool replied(const kst_session_t *s)
{
    return s->last_reply.result == KST_RX_OK;
}

/* --- read all ---------------------------------------------------------------- */

static void read_begin(kst_session_t *s)
{
    s->reading = 1;
    s->pass = 0;
    s->passes = KST_READ_PASSES;
    s->reg = 0;
    s->bad_regs = 0;
    memset(s->vals_ok, 0, sizeof(s->vals_ok));
    txn_begin(s, KST_EXPECT_READ, 0, 0);
}

/* The value of a register that KST_READ_EQUAL passes agree on. */
static bool agreed(const kst_session_t *s, unsigned reg, uint8_t *value)
{
    const uint32_t bit = (uint32_t)1 << reg;

    for (unsigned i = 0; i < s->passes; ++i) {
        unsigned equal = 0;

        if ((s->vals_ok[i] & bit) == 0u) {
            continue;
        }
        for (unsigned j = 0; j < s->passes; ++j) {
            if ((s->vals_ok[j] & bit) != 0u
                && s->vals[j][reg] == s->vals[i][reg]) {
                equal++;
            }
        }
        if (equal >= KST_READ_EQUAL) {
            *value = s->vals[i][reg];
            return true;
        }
    }
    return false;
}

static uint32_t tally(kst_session_t *s)
{
    uint32_t bad = 0;

    for (unsigned r = 0; r < KST_REG_COUNT; ++r) {
        if (!agreed(s, r, &s->read.r[r])) {
            bad |= (uint32_t)1 << r;
        }
    }
    return bad;
}

/* Takes the read that ended and starts the next.  True when the passes are
 * over; read_result and read hold the outcome. */
static bool read_advance(kst_session_t *s)
{
    if (replied(s)) {
        s->vals[s->pass][s->reg] = s->last_reply.value;
        s->vals_ok[s->pass] |= (uint32_t)1 << s->reg;
    }
    s->reg++;
    if (s->reg == KST_REG_COUNT) {
        s->reg = 0;
        s->pass++;
        if (s->vals_ok[s->pass - 1u] == 0u) {
            s->bad_regs = ALL_REGS;
            s->read_result = KST_SES_ERR_NO_SERVO;
            s->reading = 0;
            return true;
        }
        if (s->pass == s->passes) {
            s->bad_regs = tally(s);
            if (s->bad_regs == 0u || s->passes == KST_READ_PASSES_MAX) {
                s->read_result = s->bad_regs == 0u ? KST_SES_OK
                                                   : KST_SES_ERR_READ;
                s->reading = 0;
                return true;
            }
            s->passes = KST_READ_PASSES_MAX;
        }
    }
    txn_begin(s, KST_EXPECT_READ, s->reg, 0);
    return false;
}

/* --- operations -------------------------------------------------------------- */

static void finish(kst_session_t *s, kst_ses_t result)
{
    s->op = OP_NONE;
    s->reading = 0;
    s->result = (uint8_t)result;
}

/* The servo may hold part of a plan. */
static void finish_in_doubt(kst_session_t *s, kst_ses_t result)
{
    if (s->dirty != 0u) {
        s->locked = 1;
    }
    finish(s, result);
}

static void took_image(kst_session_t *s)
{
    s->image = s->read;
    s->has_image = 1;
    s->fingerprint_ok = kst_fingerprint(&s->image, &s->fingerprint) ? 1u : 0u;
}

static void enter_advance(kst_session_t *s)
{
    if (s->phase != E_WAITED && replied(s)) {
        s->in_mode = 1;
        finish(s, KST_SES_OK);
        return;
    }
    /* No reply is the normal course here, not a fault of the line. */
    s->t_gap = KST_GAP_US;
    switch (s->phase) {
    case E_WAITED:
        txn_begin(s, KST_EXPECT_READ, KST_REG_DUTY_COPY, 0);
        s->phase = E_FIRST_READ;
        break;
    case E_FIRST_READ:
        txn_begin(s, KST_EXPECT_SYNC, 0, 0);
        s->phase = E_SYNC;
        break;
    case E_SYNC:
        txn_begin(s, KST_EXPECT_READ, KST_REG_DUTY_COPY, 0);
        s->phase = E_LAST_READ;
        break;
    default:
        finish(s, KST_SES_ERR_NO_SERVO);
        break;
    }
}

static void read_all_advance(kst_session_t *s)
{
    if (s->read_result != KST_SES_OK) {
        finish(s, (kst_ses_t)s->read_result);
        return;
    }
    took_image(s);
    if (s->has_backup == 0u) {
        s->backup = s->image;
        s->has_backup = 1;
    }
    finish(s, KST_SES_OK);
}

static void verify_advance(kst_session_t *s)
{
    if (s->read_result != KST_SES_OK) {
        finish(s, (kst_ses_t)s->read_result);
        return;
    }
    took_image(s);
    s->diff_regs = diff_mask(&s->image, &s->expect);
    if (s->diff_regs != 0u) {
        finish(s, KST_SES_ERR_VERIFY);
        return;
    }
    if (s->has_backup != 0u && diff_mask(&s->image, &s->backup) == 0u) {
        s->locked = 0;
    }
    finish(s, KST_SES_OK);
}

static void write_start(kst_session_t *s)
{
    const kst_write_t *w = &s->plan.step[s->step_index];

    txn_begin(s, KST_EXPECT_WRITE, w->reg, w->value);
    s->phase = W_WROTE;
}

static void write_done(kst_session_t *s)
{
    const kst_write_t *w = &s->plan.step[s->step_index];

    s->expect.r[w->reg] = w->value;
    s->step_index++;
    s->attempt = 0;
    if (s->step_index < s->plan.n) {
        write_start(s);
    } else {
        read_begin(s);
        s->phase = W_FINAL;
    }
}

static void undo_next(kst_session_t *s)
{
    if (s->undo < s->undo_stop) {
        read_begin(s);
        s->phase = W_UNDO_FINAL;
        return;
    }
    s->undone = 1;
    /* undo is 0 .. plan.n - 1 here; the modulo states the bound where the
     * index is used. */
    const kst_write_t *w =
        &s->plan.step[(unsigned)s->undo % KST_PLAN_MAX_STEPS];
    txn_begin(s, KST_EXPECT_WRITE, w->reg, w->prev);
    s->phase = W_UNDO_WROTE;
}

/* The write of step_index failed for good; the register holds @p held. */
static void undo_begin(kst_session_t *s, uint8_t held)
{
    const kst_write_t *w = &s->plan.step[s->step_index];
    int settled = (int)s->step_index - 1;

    while (settled >= 0 && s->plan.step[settled].settled == 0u) {
        settled--;
    }
    s->expect.r[w->reg] = held;
    s->undo = (int16_t)(held != w->prev ? s->step_index : s->step_index - 1);
    s->undo_stop = (int16_t)(settled + 1);
    s->undone = 0;
    s->attempt = 0;
    undo_next(s);
}

static bool read_back_matches(const kst_session_t *s, uint8_t value)
{
    return replied(s) && s->last_reply.value == value;
}

static void write_advance(kst_session_t *s)
{
    const kst_plan_t *plan = &s->plan;
    /* step_index equals n after the last write; the pointer is used in the
     * phases before that only.  The same holds for the step being undone. */
    const kst_write_t *w = &plan->step[s->step_index % KST_PLAN_MAX_STEPS];
    const kst_write_t *u = &plan->step[s->undo < 0 ? 0 : s->undo];

    switch (s->phase) {
    case W_PRE:
        if (s->read_result != KST_SES_OK) {
            finish(s, (kst_ses_t)s->read_result);
            break;
        }
        took_image(s);
        s->diff_regs = diff_mask(&s->image, &plan->start);
        if (s->diff_regs != 0u) {
            finish(s, KST_SES_ERR_CHANGED);
            break;
        }
        s->expect = plan->start;
        if (plan->n == 0u) {
            /* The servo holds the backup: a restore with nothing to write
             * has succeeded as one with writes does. */
            if (plan->kind == KST_PLAN_RESTORE) {
                s->locked = 0;
            }
            finish(s, KST_SES_OK);
            break;
        }
        write_start(s);
        break;
    case W_WROTE:
        txn_begin(s, KST_EXPECT_READ, w->reg, 0);
        s->phase = W_READ1;
        break;
    case W_READ1:
        s->first_ok = read_back_matches(s, w->value) ? 1u : 0u;
        txn_begin(s, KST_EXPECT_READ, w->reg, 0);
        s->phase = W_READ2;
        break;
    case W_READ2:
        if (s->first_ok != 0u && read_back_matches(s, w->value)) {
            write_done(s);
        } else {
            read_begin(s);
            s->phase = W_CHECK;
        }
        break;
    case W_CHECK:
        if (s->read_result != KST_SES_OK) {
            finish_in_doubt(s, (kst_ses_t)s->read_result);
            break;
        }
        took_image(s);
        s->diff_regs = diff_mask(&s->image, &s->expect)
                       & ~((uint32_t)1 << w->reg);
        if (s->diff_regs != 0u) {
            finish_in_doubt(s, KST_SES_ERR_UNINTENDED);
        } else if (s->image.r[w->reg] == w->value) {
            write_done(s);
        } else if (++s->attempt < KST_WRITE_ATTEMPTS) {
            write_start(s);
        } else {
            undo_begin(s, s->image.r[w->reg]);
        }
        break;
    case W_UNDO_WROTE:
        txn_begin(s, KST_EXPECT_READ, u->reg, 0);
        s->phase = W_UNDO_READ1;
        break;
    case W_UNDO_READ1:
        s->first_ok = read_back_matches(s, u->prev) ? 1u : 0u;
        txn_begin(s, KST_EXPECT_READ, u->reg, 0);
        s->phase = W_UNDO_READ2;
        break;
    case W_UNDO_READ2:
        if (s->first_ok != 0u && read_back_matches(s, u->prev)) {
            s->expect.r[u->reg] = u->prev;
            s->undo--;
            s->attempt = 0;
            undo_next(s);
        } else if (++s->attempt < KST_WRITE_ATTEMPTS) {
            undo_next(s);
        } else {
            finish_in_doubt(s, KST_SES_ERR_TORN);
        }
        break;
    case W_UNDO_FINAL:
        if (s->read_result != KST_SES_OK) {
            finish_in_doubt(s, (kst_ses_t)s->read_result);
            break;
        }
        took_image(s);
        s->diff_regs = diff_mask(&s->image, &s->expect);
        if (s->diff_regs != 0u) {
            finish_in_doubt(s, KST_SES_ERR_VERIFY);
        } else {
            s->step_index = (uint8_t)s->undo_stop;
            finish(s, s->undone != 0u ? KST_SES_ERR_ROLLED_BACK
                                      : KST_SES_ERR_WRITE_FAILED);
        }
        break;
    default: /* W_FINAL */
        if (s->read_result != KST_SES_OK) {
            finish_in_doubt(s, (kst_ses_t)s->read_result);
            break;
        }
        took_image(s);
        s->diff_regs = diff_mask(&s->image, &plan->target);
        if (s->diff_regs != 0u) {
            finish_in_doubt(s, KST_SES_ERR_VERIFY);
            break;
        }
        if (plan->kind == KST_PLAN_RESTORE) {
            s->locked = 0;
        }
        finish(s, KST_SES_OK);
        break;
    }
}

static void advance(kst_session_t *s)
{
    if (s->fault != 0u) {
        finish_in_doubt(s, KST_SES_ERR_DRIVER);
        return;
    }
    if (s->abort != 0u) {
        finish_in_doubt(s, KST_SES_ERR_ABORTED);
        return;
    }
    if (s->reading != 0u && !read_advance(s)) {
        return;
    }
    switch (s->op) {
    case OP_ENTER:
        enter_advance(s);
        break;
    case OP_READ_ALL:
        read_all_advance(s);
        break;
    case OP_VERIFY:
        verify_advance(s);
        break;
    default:
        write_advance(s);
        break;
    }
}

kst_ses_t kst_session_step(kst_session_t *s)
{
    if (s == NULL) {
        return KST_SES_ERR_ARG;
    }
    while (s->op != OP_NONE) {
        if (s->t_state != T_IDLE && !txn_step(s)) {
            return KST_SES_BUSY;
        }
        advance(s);
    }
    return (kst_ses_t)s->result;
}

/* --- starting an operation --------------------------------------------------- */

bool kst_session_init(kst_session_t *s, const kst_driver_t *driver)
{
    if (s == NULL) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    if (driver == NULL || driver->start == NULL || driver->poll == NULL
        || driver->now_us == NULL) {
        return false;
    }
    s->drv = *driver;
    s->t_mark = now(s);
    s->t_gap = KST_GAP_US;
    return true;
}

static kst_ses_t refuse(kst_session_t *s, kst_ses_t result)
{
    s->result = (uint8_t)result;
    return result;
}

/* The checks every operation starts with; KST_SES_OK lets it start. */
static kst_ses_t may_start(const kst_session_t *s, bool needs_mode)
{
    if (s == NULL || s->drv.now_us == NULL) {
        return KST_SES_ERR_ARG;
    }
    if (s->op != OP_NONE) {
        return KST_SES_ERR_BUSY;
    }
    if (needs_mode && s->in_mode == 0u) {
        return KST_SES_ERR_NOT_IN_MODE;
    }
    return KST_SES_OK;
}

static kst_ses_t started(kst_session_t *s, uint8_t op)
{
    s->op = op;
    s->phase = 0;
    s->fault = 0;
    s->abort = 0;
    s->dirty = 0;
    s->attempt = 0;
    s->step_index = 0;
    s->diff_regs = 0;
    s->result = (uint8_t)KST_SES_BUSY;
    return KST_SES_BUSY;
}

kst_ses_t kst_session_enter(kst_session_t *s)
{
    const kst_ses_t gate = may_start(s, false);

    if (gate == KST_SES_ERR_ARG || gate == KST_SES_ERR_BUSY) {
        return gate;
    }
    (void)started(s, OP_ENTER);
    wait_begin(s, KST_ENTRY_LOW_US);
    return KST_SES_BUSY;
}

kst_ses_t kst_session_read_all(kst_session_t *s)
{
    const kst_ses_t gate = may_start(s, true);

    if (gate == KST_SES_ERR_ARG || gate == KST_SES_ERR_BUSY) {
        return gate;
    }
    if (gate != KST_SES_OK) {
        return refuse(s, gate);
    }
    (void)started(s, OP_READ_ALL);
    read_begin(s);
    return KST_SES_BUSY;
}

kst_ses_t kst_session_verify(kst_session_t *s, const kst_image_t *expected)
{
    const kst_ses_t gate = may_start(s, true);

    if (gate == KST_SES_ERR_ARG || gate == KST_SES_ERR_BUSY) {
        return gate;
    }
    if (expected == NULL) {
        return refuse(s, KST_SES_ERR_ARG);
    }
    if (gate != KST_SES_OK) {
        return refuse(s, gate);
    }
    (void)started(s, OP_VERIFY);
    s->expect = *expected;
    read_begin(s);
    return KST_SES_BUSY;
}

/* A plan as kst_plan.h builds one.  The planner is run again on the plan's
 * start, target and unlock set, and the plan has to be the one it gives:
 * the same writes in the same order, the same settled marks and the same
 * unchecked mark.  A plan changed after it was built, in its order or in
 * the mark that asks for a confirmation, is not that plan. */
static bool plan_is_sound(const kst_plan_t *plan)
{
    kst_plan_t built;
    kst_plan_result_t r;

    switch (plan->kind) {
    case KST_PLAN_EDIT:
        r = kst_plan_edit(&plan->start, &plan->target, plan->unlocked,
                          &built, NULL);
        break;
    case KST_PLAN_RESTORE:
        r = kst_plan_restore(&plan->start, &plan->target, &built);
        break;
    case KST_PLAN_RELEASE_PAIRING:
        r = kst_plan_release_pairing(&plan->start, &built);
        break;
    default:
        return false;
    }
    if (r != KST_PLAN_OK || built.n != plan->n
        || built.unchecked != plan->unchecked
        || built.unlocked != plan->unlocked
        || diff_mask(&built.target, &plan->target) != 0u) {
        return false;
    }
    for (unsigned i = 0; i < built.n; ++i) {
        const kst_write_t *a = &built.step[i];
        const kst_write_t *b = &plan->step[i];

        if (a->reg != b->reg || a->value != b->value || a->prev != b->prev
            || a->settled != b->settled) {
            return false;
        }
    }
    return true;
}

static kst_ses_t write_gate(const kst_session_t *s, const kst_plan_t *plan,
                            bool confirm_unchecked)
{
    if (s->has_backup == 0u) {
        return KST_SES_ERR_NO_BACKUP;
    }
    if (!plan_is_sound(plan)) {
        return KST_SES_ERR_BAD_PLAN;
    }
    if (plan->kind == KST_PLAN_RESTORE) {
        if (diff_mask(&plan->target, &s->backup) != 0u) {
            return KST_SES_ERR_BAD_PLAN;
        }
        return plan->unchecked != 0u && !confirm_unchecked
                   ? KST_SES_ERR_UNCONFIRMED : KST_SES_OK;
    }
    if (s->locked != 0u) {
        return KST_SES_ERR_LOCKED;
    }
    if (!kst_fingerprint(&plan->start, NULL)) {
        return KST_SES_ERR_FINGERPRINT;
    }
    return KST_SES_OK;
}

kst_ses_t kst_session_write(kst_session_t *s, const kst_plan_t *plan,
                            bool confirm_unchecked)
{
    kst_ses_t gate = may_start(s, true);

    if (gate == KST_SES_ERR_ARG || gate == KST_SES_ERR_BUSY) {
        return gate;
    }
    if (plan == NULL) {
        return refuse(s, KST_SES_ERR_ARG);
    }
    if (gate == KST_SES_OK) {
        gate = write_gate(s, plan, confirm_unchecked);
    }
    if (gate != KST_SES_OK) {
        return refuse(s, gate);
    }
    (void)started(s, OP_WRITE);
    s->plan = *plan;
    s->undo = 0;
    read_begin(s);
    return KST_SES_BUSY;
}

void kst_session_abort(kst_session_t *s)
{
    if (s != NULL && s->op != OP_NONE) {
        s->abort = 1;
    }
}

void kst_session_power_cycled(kst_session_t *s)
{
    if (s != NULL) {
        s->in_mode = 0;
        kst_session_abort(s);
    }
}
