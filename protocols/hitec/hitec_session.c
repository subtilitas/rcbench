/**
 * @file
 * @brief Hitec session: entry, reads, write plans and leave, handed out as
 *        steps.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hitec_session.h"

#include <string.h>

/** @brief Operation the session runs. */
enum { OP_NONE = 0, OP_ENTER, OP_READ, OP_WRITE, OP_LEAVE };

/** @brief The step an operation is at. */
enum {
    PH_C_POWER = 0,  /* classic entry: POWER, the servo's first `+` */
    PH_C_NEXT,       /* classic entry: the servo's next `+` */
    PH_C_FIRST,      /* classic entry: the first command */
    PH_D_POWER,      /* D series entry: POWER */
    PH_READ,         /* one address of todo */
    PH_WRITE,        /* plan.step[step_index] */
    PH_LEAVE,        /* classic POWER, D series reboot */
};

/** @brief The first command's first byte, as the HPP-21 Plus sends it. */
#define C_FIRST_P1 0x00u
/** @brief The first command's second byte, as the HPP-21 Plus sends it. */
#define C_FIRST_P2 0x00u

/** @brief Bits of an address set. */
#define SET_BITS (HITEC_SES_SET_BYTES * 8u)

/* --- address sets --------------------------------------------------------- */

bool hitec_ses_in_set(const uint8_t *set, uint8_t addr)
{
    return set != NULL && (set[addr / 8u] & (1u << (addr % 8u))) != 0u;
}

/**
 * @brief Put @p addr into @p set.
 *
 * @param set   Address set.
 * @param addr  Address.
 */
static void set_add(uint8_t *set, uint8_t addr)
{
    set[addr / 8u] = (uint8_t)(set[addr / 8u] | (1u << (addr % 8u)));
}

/**
 * @brief Take @p addr out of @p set.
 *
 * @param set   Address set.
 * @param addr  Address.
 */
static void set_drop(uint8_t *set, uint8_t addr)
{
    set[addr / 8u] = (uint8_t)(set[addr / 8u] & ~(1u << (addr % 8u)));
}

/**
 * @brief The lowest address in @p set.
 *
 * @param set  Address set.
 * @param out  Receives the address.
 * @return false for an empty set.
 */
static bool set_first(const uint8_t *set, uint8_t *out)
{
    for (unsigned a = 0; a < SET_BITS; ++a) {
        if (hitec_ses_in_set(set, (uint8_t)a)) {
            *out = (uint8_t)a;
            return true;
        }
    }
    return false;
}

/**
 * @brief Addresses in @p set.
 *
 * @param set  Address set.
 * @return The count, 0 to 256.
 */
static uint16_t set_count(const uint8_t *set)
{
    uint16_t n = 0;

    for (unsigned a = 0; a < SET_BITS; ++a) {
        if (hitec_ses_in_set(set, (uint8_t)a)) {
            ++n;
        }
    }
    return n;
}

/**
 * @brief Put the series' full read into @p set.
 *
 * @param series  hitec_family_t.
 * @param set     Address set, emptied first.
 */
static void set_full_read(uint8_t series, uint8_t *set)
{
    size_t n = 0;

    memset(set, 0, HITEC_SES_SET_BYTES);
    if (series == HITEC_CLASSIC) {
        const hitec_range_t *r = hitec_c_read_ranges(&n);

        for (size_t i = 0; i < n; ++i) {
            for (unsigned c = 0; c < r[i].count; ++c) {
                set_add(set, (uint8_t)(r[i].first + c));
            }
        }
    } else {
        const uint8_t *d = hitec_d_read_list(&n);

        for (size_t i = 0; i < n; ++i) {
            set_add(set, d[i]);
        }
    }
}

/* --- time ----------------------------------------------------------------- */

/**
 * @brief @p ms in ticks of the session's clock.
 *
 * @param s   Session.
 * @param ms  Time, ms.
 * @return Ticks.
 */
static uint32_t ticks(const hitec_session_t *s, uint32_t ms)
{
    return s->clock == HITEC_SES_CLOCK_US ? ms * 1000u : ms;
}

/**
 * @brief Start the gap of @p ms before the next step, from @p now.
 *
 * @param s    Session.
 * @param now  The clock.
 * @param ms   Gap, ms; 0 for none.
 */
static void gap_from(hitec_session_t *s, uint32_t now, uint32_t ms)
{
    s->t_mark = now;
    s->gap = ticks(s, ms);
    s->gap_on = ms != 0u ? 1u : 0u;
}

/**
 * @brief The gap after a transaction of the session's series.
 *
 * @param s  Session.
 * @return HITEC_SES_C_GAP_MS or HITEC_SES_D_GAP_MS.
 */
static uint32_t series_gap(const hitec_session_t *s)
{
    return s->series == HITEC_CLASSIC ? HITEC_SES_C_GAP_MS
                                      : HITEC_SES_D_GAP_MS;
}

/* --- operations ----------------------------------------------------------- */

/**
 * @brief True when the read-back of the running write has read @p addr.
 *
 * @param s     Session.
 * @param addr  Address.
 * @return true for an address of the read-back's set it has read.
 */
static bool read_back(const hitec_session_t *s, uint8_t addr)
{
    bool in_set = false;

    if (s->phase != PH_READ) {
        return false;
    }
    if (s->series == HITEC_CLASSIC) {
        in_set = addr < HITEC_C_BLOCK_LEN;
    } else {
        size_t n = 0;
        const uint8_t *d = hitec_d_read_list(&n);

        for (size_t i = 0; i < n; ++i) {
            in_set = in_set || d[i] == addr;
        }
    }
    return in_set && !hitec_ses_in_set(s->todo, addr);
}

/**
 * @brief After a write that failed: mark every address the servo may have
 *        changed and the read-back has not read as not known.
 *
 * D series: every register of the full read and every register the plan
 * names; a save, a reboot or a factory reset may change any of them.
 * Classic: the cell of the step that ran with no `+ +`, and the balance.
 *
 * @param s  Session.
 */
static void forget_unsure(hitec_session_t *s)
{
    size_t n = 0;
    const uint8_t *d = hitec_d_read_list(&n);

    for (unsigned a = 0; a < SET_BITS; ++a) {
        const uint8_t addr = (uint8_t)a;
        bool may = false;

        if (s->series == HITEC_CLASSIC) {
            may = addr == HITEC_C_SUM_LAST
                  || (s->unsure != 0u && s->phase == PH_WRITE
                      && addr == s->plan.step[s->step_index].addr);
        } else if ((addr & 1u) == 0u) {
            for (size_t i = 0; i < n; ++i) {
                may = may || d[i] == addr;
            }
            for (unsigned i = 0; i < s->plan.n; ++i) {
                may = may || s->plan.step[i].addr == addr;
            }
        }
        if (may && !read_back(s, addr)) {
            (void)hitec_image_forget(&s->image, addr);
        }
    }
}

/**
 * @brief End the operation with @p res; an abort asked for ends it with
 *        HITEC_SES_ERR_ABORTED.  A write that ends with an error after a
 *        step was handed out and its result fed forgets what the servo
 *        may have changed.
 *
 * @param s    Session.
 * @param res  Result.
 * @return The result the operation ended with.
 */
static hitec_ses_t finish(hitec_session_t *s, hitec_ses_t res)
{
    if (s->abort != 0u) {
        res = HITEC_SES_ERR_ABORTED;
    }
    if (s->op == OP_WRITE && res != HITEC_SES_OK && s->sent != 0u) {
        forget_unsure(s);
    }
    s->op = OP_NONE;
    s->out = 0;
    s->abort = 0;
    s->result = (uint8_t)res;
    return res;
}

/**
 * @brief Begin operation @p op at @p phase.
 *
 * @param s      Session.
 * @param op     Operation.
 * @param phase  Its first step.
 */
static void begin(hitec_session_t *s, uint8_t op, uint8_t phase)
{
    s->op = op;
    s->phase = phase;
    s->out = 0;
    s->abort = 0;
    s->tries = 0;
    s->result = HITEC_SES_BUSY;
}

/**
 * @brief Go to a read of the addresses in todo.
 *
 * @param s  Session.
 */
static void begin_read(hitec_session_t *s)
{
    s->phase = PH_READ;
    s->tries = 0;
    s->done = 0;
    s->total = set_count(s->todo);
    (void)set_first(s->todo, &s->addr);
}

/**
 * @brief The common refusals of an operation.
 *
 * @param s        Session.
 * @param in_mode  true when the operation needs a successful entry.
 * @return HITEC_SES_OK when it may start.
 */
static hitec_ses_t may_start(const hitec_session_t *s, bool in_mode)
{
    if (s == NULL) {
        return HITEC_SES_ERR_ARG;
    }
    if (s->op != OP_NONE) {
        return HITEC_SES_ERR_BUSY;
    }
    if (in_mode && s->in_mode == 0u) {
        return HITEC_SES_ERR_NOT_IN_MODE;
    }
    return HITEC_SES_OK;
}

bool hitec_session_init(hitec_session_t *s, uint8_t series, uint8_t clock)
{
    if (s == NULL || hitec_bit_rate(series) == 0u
        || clock > HITEC_SES_CLOCK_MS) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->series = series;
    s->clock = clock;
    s->off_ms = HITEC_SES_OFF_MS;
    s->result = HITEC_SES_OK;
    hitec_image_init(&s->image, series);
    hitec_image_init(&s->target, series);
    return true;
}

hitec_ses_t hitec_session_enter(hitec_session_t *s, bool power,
                                uint16_t off_ms)
{
    const hitec_ses_t r = may_start(s, false);

    if (r != HITEC_SES_OK) {
        return r;
    }
    if (off_ms > HITEC_SES_OFF_MS_MAX) {
        return HITEC_SES_ERR_ARG;
    }
    s->off_ms = off_ms;
    s->in_mode = 0;
    s->power = power ? 1u : 0u;
    hitec_image_init(&s->image, s->series);
    if (s->series == HITEC_CLASSIC) {
        begin(s, OP_ENTER, PH_C_POWER);
    } else {
        begin(s, OP_ENTER, PH_D_POWER);
        if (!power) {
            memset(s->todo, 0, sizeof(s->todo));
            set_add(s->todo, HITEC_D_REG_MODEL);
            begin_read(s);
        }
    }
    return HITEC_SES_BUSY;
}

hitec_ses_t hitec_session_read_all(hitec_session_t *s)
{
    const hitec_ses_t r = may_start(s, true);
    uint8_t version;
    uint8_t has_version;

    if (r != HITEC_SES_OK) {
        return r;
    }
    version = s->image.version;
    has_version = s->image.has_version;
    hitec_image_init(&s->image, s->series);
    s->image.version = version;
    s->image.has_version = has_version;
    set_full_read(s->series, s->todo);
    begin(s, OP_READ, PH_READ);
    begin_read(s);
    return HITEC_SES_BUSY;
}

hitec_ses_t hitec_session_read_set(hitec_session_t *s, const uint8_t *addrs,
                                   uint16_t n)
{
    const hitec_ses_t r = may_start(s, true);
    uint8_t set[HITEC_SES_SET_BYTES] = { 0 };

    if (r != HITEC_SES_OK) {
        return r;
    }
    if (addrs == NULL || n == 0u || n > SET_BITS) {
        return HITEC_SES_ERR_ARG;
    }
    for (uint16_t i = 0; i < n; ++i) {
        if (s->series == HITEC_D && (addrs[i] & 1u) != 0u) {
            return HITEC_SES_ERR_ARG;
        }
        set_add(set, addrs[i]);
    }
    memcpy(s->todo, set, sizeof(set));
    begin(s, OP_READ, PH_READ);
    begin_read(s);
    return HITEC_SES_BUSY;
}

/**
 * @brief True when every step of @p plan is one the session runs on its
 *        series.
 *
 * @param s     Session.
 * @param plan  Plan.
 * @return true for a plan of the session's series.
 */
static bool plan_fits(const hitec_session_t *s, const hitec_plan_t *plan)
{
    if (plan->n > HITEC_PLAN_MAX) {
        return false;
    }
    for (unsigned i = 0; i < plan->n; ++i) {
        const hitec_step_t *st = &plan->step[i];
        bool ok;

        if (s->series == HITEC_CLASSIC) {
            ok = st->value <= 0xFFu
                 && (st->kind == HITEC_STEP_C_CELL
                     || (st->kind == HITEC_STEP_C_SUM
                         && st->addr == HITEC_C_SUM_LAST));
        } else {
            ok = (st->addr & 1u) == 0u
                 && (st->kind == HITEC_STEP_D_WRITE
                     || st->kind == HITEC_STEP_D_SAVE
                     || st->kind == HITEC_STEP_D_REBOOT);
        }
        if (!ok) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Start the plan in s->plan; the read-back compares with target
 *        when @p compare.
 *
 * @param s        Session.
 * @param compare  true to compare the read-back with target.
 * @return HITEC_SES_BUSY, or HITEC_SES_OK for an empty plan.
 */
static hitec_ses_t start_plan(hitec_session_t *s, bool compare)
{
    memset(s->diff, 0, sizeof(s->diff));
    s->step_index = 0;
    s->sent = 0;
    s->unsure = 0;
    s->compare = compare ? 1u : 0u;
    if (s->plan.n == 0u) {
        s->result = HITEC_SES_OK;
        return HITEC_SES_OK;
    }
    begin(s, OP_WRITE, PH_WRITE);
    return HITEC_SES_BUSY;
}

hitec_ses_t hitec_session_write(hitec_session_t *s,
                                const hitec_image_t *target)
{
    hitec_ses_t r = may_start(s, false);

    if (r != HITEC_SES_OK) {
        return r;
    }
    if (target == NULL || target->series != s->series) {
        return HITEC_SES_ERR_ARG;
    }
    if (s->in_mode == 0u) {
        return HITEC_SES_ERR_NOT_IN_MODE;
    }
    s->plan_result = (uint8_t)hitec_plan(&s->image, target, &s->plan);
    if (s->plan_result != HITEC_PLAN_OK) {
        return HITEC_SES_ERR_PLAN;
    }
    s->target = *target;
    return start_plan(s, true);
}

hitec_ses_t hitec_session_run_plan(hitec_session_t *s,
                                   const hitec_plan_t *plan,
                                   const hitec_image_t *expect)
{
    hitec_ses_t r = may_start(s, false);

    if (r != HITEC_SES_OK) {
        return r;
    }
    if (plan == NULL || (expect != NULL && expect->series != s->series)) {
        return HITEC_SES_ERR_ARG;
    }
    if (s->in_mode == 0u) {
        return HITEC_SES_ERR_NOT_IN_MODE;
    }
    if (!plan_fits(s, plan)) {
        return HITEC_SES_ERR_PLAN;
    }
    s->plan = *plan;
    if (expect != NULL) {
        s->target = *expect;
    }
    return start_plan(s, expect != NULL);
}

hitec_ses_t hitec_session_leave(hitec_session_t *s)
{
    const hitec_ses_t r = may_start(s, false);

    if (r != HITEC_SES_OK) {
        return r;
    }
    s->in_mode = 0;
    begin(s, OP_LEAVE, PH_LEAVE);
    return HITEC_SES_BUSY;
}

bool hitec_session_abort(hitec_session_t *s)
{
    if (s == NULL || s->op == OP_NONE) {
        return false;
    }
    if (s->out != 0u) {
        s->abort = 1;
        return true;
    }
    s->abort = 1;
    (void)finish(s, HITEC_SES_ERR_ABORTED);
    return false;
}

hitec_ses_t hitec_session_result(const hitec_session_t *s)
{
    if (s == NULL) {
        return HITEC_SES_ERR_ARG;
    }
    return s->op != OP_NONE ? HITEC_SES_BUSY : (hitec_ses_t)s->result;
}

/* --- steps ---------------------------------------------------------------- */

/**
 * @brief A POWER step whose LISTEN waits @p window_us.
 *
 * @param s          Session.
 * @param st         Receives the step.
 * @param window_us  The LISTEN's window, us.
 * @param answer     true to answer the first byte with `+`.
 */
static void power_step(const hitec_session_t *s, hitec_ses_step_t *st,
                       uint32_t window_us, bool answer)
{
    st->kind = HITEC_SES_STEP_POWER;
    st->off_ms = s->off_ms;
    (void)hitec_listen_xfer(s->series, window_us, 1u, answer, HITEC_C_ACK,
                            &st->xfer);
}

/**
 * @brief Build the step of the operation's phase into s->step.
 *
 * @param s  Session.
 */
static void build(hitec_session_t *s)
{
    hitec_ses_step_t *st = &s->step;
    const bool classic = s->series == HITEC_CLASSIC;

    memset(st, 0, sizeof(*st));
    st->kind = HITEC_SES_STEP_XFER;
    switch (s->phase) {
    case PH_C_POWER:
        power_step(s, st, HITEC_SES_C_ENTRY_WINDOW_US, true);
        st->decoder = HITEC_DEC_LISTEN;
        st->dec_arg = HITEC_C_ACK;
        break;
    case PH_C_NEXT:
        (void)hitec_listen_xfer(HITEC_CLASSIC, HITEC_SES_C_NEXT_WINDOW_US, 1u,
                                false, 0u, &st->xfer);
        st->decoder = HITEC_DEC_LISTEN;
        st->dec_arg = HITEC_C_ACK;
        break;
    case PH_C_FIRST:
        hitec_c_xfer(HITEC_C_FIRST, C_FIRST_P1, C_FIRST_P2, &st->xfer);
        st->decoder = HITEC_DEC_C;
        st->dec_arg = HITEC_C_FIRST;
        break;
    case PH_D_POWER:
        power_step(s, st, HITEC_SES_POWER_WINDOW_US, false);
        break;
    case PH_READ:
        if (classic) {
            hitec_c_xfer(HITEC_C_READ_EEPROM, s->addr, 0u, &st->xfer);
            st->decoder = HITEC_DEC_C;
            st->dec_arg = HITEC_C_READ_EEPROM;
        } else {
            hitec_d_read_xfer(0u, s->addr, &st->xfer);
            st->decoder = HITEC_DEC_D;
            st->dec_arg = s->addr;
        }
        break;
    case PH_WRITE: {
        const hitec_step_t *w = &s->plan.step[s->step_index];

        s->addr = w->addr;
        if (classic) {
            hitec_c_xfer(HITEC_C_WRITE_EEPROM, w->addr, (uint8_t)w->value,
                         &st->xfer);
            st->decoder = HITEC_DEC_C;
            st->dec_arg = HITEC_C_WRITE_EEPROM;
        } else {
            hitec_d_write_xfer(0u, w->addr, w->value, &st->xfer);
        }
        break;
    }
    default: /* PH_LEAVE */
        if (classic) {
            power_step(s, st, HITEC_SES_POWER_WINDOW_US, false);
        } else {
            hitec_d_write_xfer(0u, HITEC_D_REG_REBOOT, 1u, &st->xfer);
        }
        break;
    }
}

hitec_ses_kind_t hitec_session_next(hitec_session_t *s, uint32_t now,
                                    hitec_ses_step_t *out)
{
    hitec_ses_step_t wait;

    if (s == NULL || s->op == OP_NONE) {
        if (out != NULL) {
            memset(out, 0, sizeof(*out));
            out->kind = HITEC_SES_STEP_DONE;
        }
        return HITEC_SES_STEP_DONE;
    }
    if (s->out == 0u && s->gap_on != 0u) {
        const uint32_t elapsed = now - s->t_mark;

        if (elapsed <= s->gap) {
            const uint32_t left = s->gap - elapsed + 1u;
            uint32_t ms = s->clock == HITEC_SES_CLOCK_US
                              ? (left + 999u) / 1000u
                              : left;

            if (ms > UINT16_MAX) {
                ms = UINT16_MAX;  /* a plan wait of 65535ms, one tick on */
            }
            memset(&wait, 0, sizeof(wait));
            wait.kind = HITEC_SES_STEP_WAIT;
            wait.wait_ms = (uint16_t)ms;
            if (out != NULL) {
                *out = wait;
            }
            return HITEC_SES_STEP_WAIT;
        }
        s->gap_on = 0;
    }
    if (s->out == 0u) {
        build(s);
        s->out = 1;
    }
    if (out != NULL) {
        *out = s->step;
    }
    return (hitec_ses_kind_t)s->step.kind;
}

/* --- results -------------------------------------------------------------- */

/**
 * @brief True when the image holds what target holds at every address
 *        the read-back checks; diff names the others.
 *
 * @param s  Session.
 * @return true when nothing differs.
 */
static bool compare(hitec_session_t *s)
{
    bool same = true;

    for (unsigned a = 0; a < SET_BITS; ++a) {
        const uint8_t addr = (uint8_t)a;
        bool checked;
        bool differs;

        if (s->series == HITEC_CLASSIC) {
            checked = hitec_writable(HITEC_CLASSIC, addr)
                      || addr == HITEC_C_SUM_LAST;
            differs = hitec_image_cell(&s->image, addr)
                      != hitec_image_cell(&s->target, addr);
        } else {
            checked = (addr & 1u) == 0u && hitec_writable(HITEC_D, addr);
            differs = hitec_image_reg(&s->image, addr)
                      != hitec_image_reg(&s->target, addr);
        }
        if (!checked || !hitec_image_known(&s->target, addr)) {
            continue;
        }
        if (differs || !hitec_image_known(&s->image, addr)) {
            set_add(s->diff, addr);
            same = false;
        }
    }
    return same;
}

/**
 * @brief The end of a failed transaction with no tries left, or with
 *        none at all.
 *
 * @param s   Session.
 * @param rx  The last try's hitec_rx_t.
 * @return The result the operation ends with.
 */
static hitec_ses_t fail(hitec_session_t *s, uint8_t rx)
{
    hitec_ses_t err;

    s->fail_addr = s->addr;
    if (rx == HITEC_RX_ERR_SILENT) {
        err = HITEC_SES_ERR_NO_SERVO;
    } else if (s->op == OP_ENTER) {
        err = HITEC_SES_ERR_ENTRY;
    } else if (s->phase == PH_WRITE) {
        err = HITEC_SES_ERR_WRITE;
    } else {
        err = HITEC_SES_ERR_READ;
    }
    return finish(s, err);
}

/**
 * @brief The read of s->addr took @p value: store it, then the next
 *        address or the operation's end.  The gap is the one feed started.
 *
 * @param s      Session.
 * @param value  The decoded value.
 * @return HITEC_SES_BUSY, or the result the operation ends with.
 */
static hitec_ses_t read_took(hitec_session_t *s, uint16_t value)
{
    if (s->series == HITEC_CLASSIC) {
        (void)hitec_image_put_cell(&s->image, s->addr, (uint8_t)value);
    } else {
        (void)hitec_image_put_reg(&s->image, s->addr, value);
    }
    set_drop(s->todo, s->addr);
    ++s->done;
    if (s->abort != 0u) {
        return finish(s, HITEC_SES_ERR_ABORTED);
    }
    s->tries = 0;
    if (set_first(s->todo, &s->addr)) {
        return HITEC_SES_BUSY;
    }
    if (s->op == OP_ENTER) {
        s->in_mode = 1;
        return finish(s, HITEC_SES_OK);
    }
    if (s->op == OP_WRITE && s->compare != 0u && !compare(s)) {
        return finish(s, HITEC_SES_ERR_VERIFY);
    }
    return finish(s, HITEC_SES_OK);
}

/**
 * @brief The write of plan.step[step_index] took: count it, start its
 *        wait, then the next write or the read-back.
 *
 * A classic write answered `+ +` goes into the image.  A D series write
 * has no answer and leaves the image alone: the read-back puts the value
 * the servo holds there.
 *
 * @param s    Session.
 * @param now  The clock.
 * @return HITEC_SES_BUSY, or the result the operation ends with.
 */
static hitec_ses_t write_took(hitec_session_t *s, uint32_t now)
{
    const hitec_step_t *w = &s->plan.step[s->step_index];
    uint32_t gap = series_gap(s);

    if (s->series == HITEC_CLASSIC) {
        (void)hitec_image_put_cell(&s->image, w->addr, (uint8_t)w->value);
    }
    s->unsure = 0;
    ++s->step_index;
    if (w->wait_ms > gap) {
        gap = w->wait_ms;
    }
    gap_from(s, now, gap);
    if (s->abort != 0u) {
        return finish(s, HITEC_SES_ERR_ABORTED);
    }
    s->tries = 0;
    if (s->step_index < s->plan.n) {
        return HITEC_SES_BUSY;
    }
    if (s->series == HITEC_CLASSIC) {
        memset(s->todo, 0, sizeof(s->todo));
        for (uint8_t c = 0; c < HITEC_C_BLOCK_LEN; ++c) {
            set_add(s->todo, c);
        }
    } else {
        set_full_read(HITEC_D, s->todo);
    }
    begin_read(s);
    return HITEC_SES_BUSY;
}

/**
 * @brief A decoded transaction ended with @p rx.
 *
 * @param s      Session.
 * @param now    The clock.
 * @param rx     hitec_rx_t.
 * @param value  The decoded value.
 * @return HITEC_SES_BUSY, or the result the operation ends with.
 */
static hitec_ses_t decoded(hitec_session_t *s, uint32_t now, uint8_t rx,
                           uint16_t value)
{
    s->last_rx = rx;
    ++s->tries;
    if (rx == HITEC_RX_ERR_ARG) {
        return finish(s, HITEC_SES_ERR_STEP);
    }
    if (rx == HITEC_RX_ERR_ABORTED) {
        return finish(s, HITEC_SES_ERR_CUT);
    }
    if (rx != HITEC_RX_OK) {
        if (s->phase == PH_C_POWER || s->phase == PH_C_NEXT) {
            return fail(s, rx);
        }
        if (s->abort != 0u) {
            return finish(s, HITEC_SES_ERR_ABORTED);
        }
        if (s->tries >= HITEC_SES_TRIES) {
            return fail(s, rx);
        }
        return HITEC_SES_BUSY;
    }
    switch (s->phase) {
    case PH_C_POWER:
    case PH_C_NEXT:
        if (s->abort != 0u) {
            return finish(s, HITEC_SES_ERR_ABORTED);
        }
        s->tries = 0;
        if (s->phase == PH_C_POWER) {
            /* The servo's next `+` comes 250ms on: listen at once. */
            s->phase = PH_C_NEXT;
            gap_from(s, now, 0u);
        } else {
            s->phase = PH_C_FIRST;
        }
        return HITEC_SES_BUSY;
    case PH_C_FIRST:
        (void)hitec_image_put_version(&s->image, (uint8_t)value);
        if (s->abort != 0u) {
            return finish(s, HITEC_SES_ERR_ABORTED);
        }
        s->in_mode = 1;
        return finish(s, HITEC_SES_OK);
    case PH_READ:
        return read_took(s, value);
    default: /* PH_WRITE, classic */
        return write_took(s, now);
    }
}

hitec_ses_t hitec_session_feed(hitec_session_t *s, uint32_t now,
                               const hitec_ses_result_t *r)
{
    if (s == NULL || r == NULL || s->op == OP_NONE || s->out == 0u) {
        return HITEC_SES_ERR_ARG;
    }
    s->out = 0;
    /* Every result starts the series gap first, an abort's included; a
     * step with a longer wait sets that below. */
    gap_from(s, now, series_gap(s));
    if (s->phase == PH_WRITE) {
        /* Ran, cut or lost: the bytes may have reached the servo. */
        s->sent = 1;
        s->unsure = 1;  /* until write_took() has its `+ +` */
    }
    if (r->run == HITEC_SES_CUT) {
        return finish(s, HITEC_SES_ERR_CUT);
    }
    if (r->run != HITEC_SES_RAN) {
        return finish(s, HITEC_SES_ERR_LINK);
    }
    if (s->step.decoder != HITEC_DEC_NONE) {
        return decoded(s, now, r->rx, r->value);
    }
    /* No decoder: the D series POWER, a D series write, the leave. */
    switch (s->phase) {
    case PH_D_POWER:
        gap_from(s, now, HITEC_SES_D_ENTRY_WAIT_MS);
        if (s->abort != 0u) {
            return finish(s, HITEC_SES_ERR_ABORTED);
        }
        memset(s->todo, 0, sizeof(s->todo));
        set_add(s->todo, HITEC_D_REG_MODEL);
        begin_read(s);
        return HITEC_SES_BUSY;
    case PH_WRITE:
        return write_took(s, now);
    default: /* PH_LEAVE */
        if (s->series == HITEC_D) {
            gap_from(s, now, HITEC_D_REBOOT_WAIT_MS);  /* it restarts */
        }
        return finish(s, HITEC_SES_OK);
    }
}
