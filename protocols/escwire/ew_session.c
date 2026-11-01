/**
 * @file ew_session.c
 * @brief The session; see ew_session.h.
 *
 * An operation is a list of actions.  An action that talks makes one
 * transaction and goes to the next when its answer fits.  Every frame passes
 * the guard before the driver sees it.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_session.h"

#include <string.h>

/** @brief The session's phases. */
enum {
    PH_IDLE = 0,  /**< no operation; the last result stands         */
    PH_HELD,      /**< the line high, waiting for ew_session_enter() */
    PH_HOLD,      /**< the hold before the first contact runs        */
    PH_XFER,      /**< a transaction runs                            */
    PH_LEAVE_IDLE, /**< the line idle after a leave frame            */
    PH_LOW,       /**< the line low after a leave                    */
};

/** @brief The operations. */
enum { OP_NONE = 0, OP_ENTER, OP_READ, OP_WRITE, OP_LEAVE };

/** @brief The actions.  Every list ends in one that ends the operation. */
enum {
    A_CONTACT = 0,
    A_SET_BASE,
    A_SET_NAME,
    A_SET_RECORD,
    A_READ_ID,
    A_READ_NAME,
    A_READ_RECORD,
    A_READ_PAGE,
    A_CHECK_BEFORE,
    A_ERASE,
    A_DATA,
    A_PROGRAM,
    A_CHECK_AFTER,
    A_LEAVE,
    A_FINISH_ID,
    A_FINISH_READ,
};

/** @brief The first contact. */
static const uint8_t k_contact_seq[] = { A_CONTACT };
/** @brief What identifies an EFM8. */
static const uint8_t k_id_efm8[] = { A_SET_BASE, A_READ_ID, A_FINISH_ID };
/** @brief What identifies an ARM part with bootloader protocol 2. */
static const uint8_t k_id_arm2[] = { A_SET_BASE, A_READ_ID, A_SET_NAME,
                                     A_READ_NAME, A_FINISH_ID };
/** @brief The same with protocol 3: its record too. */
static const uint8_t k_id_arm3[] = { A_SET_BASE, A_READ_ID, A_SET_NAME,
                                     A_READ_NAME, A_SET_RECORD, A_READ_RECORD,
                                     A_FINISH_ID };
/** @brief A read of the whole page. */
static const uint8_t k_read[] = { A_SET_BASE, A_READ_PAGE, A_FINISH_READ };
/** @brief Where the procedure itself starts in both write lists. */
#define WRITE_FROM 3u
/** @brief A write on an EFM8. */
static const uint8_t k_write_efm8[] = {
    A_SET_BASE, A_READ_PAGE, A_CHECK_BEFORE,
    A_SET_BASE, A_ERASE, A_SET_BASE, A_DATA, A_PROGRAM,
    A_SET_BASE, A_READ_PAGE, A_CHECK_AFTER,
};
/** @brief A write on AM32: the ESC erases by itself. */
static const uint8_t k_write_am[] = {
    A_SET_BASE, A_READ_PAGE, A_CHECK_BEFORE,
    A_SET_BASE, A_DATA, A_PROGRAM,
    A_SET_BASE, A_READ_PAGE, A_CHECK_AFTER,
};
/** @brief The read after a failed step of a write. */
static const uint8_t k_recover[] = { A_SET_BASE, A_READ_PAGE, A_CHECK_AFTER };
/** @brief A leave. */
static const uint8_t k_leave[] = { A_LEAVE };

static void run(ew_session_t *s);

ew_ses_t ew_session_init(ew_session_t *s, const ew_driver_t *d)
{
    if (s == NULL || d == NULL || d->start == NULL || d->poll == NULL
        || d->abort == NULL || d->line == NULL || d->now_us == NULL) {
        return EW_SES_ERR_ARG;
    }
    memset(s, 0, sizeof(*s));
    s->d = *d;
    ew_guard_init(&s->guard);
    return EW_SES_OK;
}

ew_ses_t ew_session_hold(ew_session_t *s)
{
    if (s == NULL) {
        return EW_SES_ERR_ARG;
    }
    if (s->op != OP_NONE) {
        return EW_SES_ERR_STATE;
    }
    const ew_driver_t d = s->d;

    memset(s, 0, sizeof(*s));
    s->d = d;
    ew_guard_init(&s->guard);
    s->phase = PH_HELD;
    s->d.line(s->d.ctx, EW_LINE_HIGH);
    return EW_SES_OK;
}

ew_ses_t ew_session_enter(ew_session_t *s, uint32_t hold_ms)
{
    if (s == NULL || hold_ms < EW_HOLD_MIN_MS || hold_ms > EW_HOLD_MAX_MS) {
        return EW_SES_ERR_ARG;
    }
    if (s->phase != PH_HELD) {
        return EW_SES_ERR_STATE;
    }
    s->op = OP_ENTER;
    s->seq = k_contact_seq;
    s->pc = 0u;
    s->tries = 0u;
    s->phase = PH_HOLD;
    s->t0 = s->d.now_us(s->d.ctx);
    s->wait_us = hold_ms * 1000u;
    s->result = EW_SES_BUSY;
    return EW_SES_BUSY;
}

/**
 * @brief An operation ends.  A stop that waited for it parks the line now.
 * @param s the session
 * @param r the result
 */
static void finish(ew_session_t *s, ew_ses_t r)
{
    s->op = OP_NONE;
    s->phase = PH_IDLE;
    s->writing = false;
    s->restoring = false;
    s->result = r;
    if (s->stop_wanted) {
        s->stop_wanted = false;
        s->stopped = true;
        s->in_mode = false;
        s->d.line(s->d.ctx, EW_LINE_PARK);
    }
}

/**
 * @brief The driver failed: nothing it does counts any more.
 * @param s the session
 */
static void driver_failed(ew_session_t *s)
{
    s->d.abort(s->d.ctx);
    s->in_mode = false;
    s->d.line(s->d.ctx, EW_LINE_PARK);
    s->stop_wanted = false;
    finish(s, EW_SES_ERR_DRIVER);
}

/**
 * @brief The guard refused a frame the session built: the session stops.
 * @param s the session
 * @param r the refusal
 */
static void refused(ew_session_t *s, ew_refuse_t r)
{
    s->refuse = (uint8_t)r;
    s->in_mode = false;
    s->d.line(s->d.ctx, EW_LINE_PARK);
    s->stop_wanted = false;
    finish(s, EW_SES_ERR_REFUSED);
}

/**
 * @brief Build and start a transaction from a frame in @c tx.
 * @param s        the session
 * @param n_tx     bytes in @c tx
 * @param n_head   bytes before the pause, 0 for none
 * @param max_rx   bytes to receive
 * @param first_us the wait for the first
 */
static void start_xfer(ew_session_t *s, uint16_t n_tx, uint16_t n_head,
                       uint16_t max_rx, uint32_t first_us)
{
    s->x = (ew_xfer_t){
        .rate = EW_RATE_A,
        .pause_bits = n_head != 0u ? EW_SES_PAUSE_BITS : 0u,
        .n_tx = n_tx,
        .n_head = n_head,
        .max_rx = max_rx,
        .first_us = max_rx != 0u ? first_us : 0u,
        .gap_us = max_rx != 0u ? EW_A_GAP_US : 0u,
        .tx = s->tx,
        .rx = max_rx != 0u ? s->rx : NULL,
    };
    if (!s->d.start(s->d.ctx, &s->x)) {
        driver_failed(s);
        return;
    }
    s->t0 = s->d.now_us(s->d.ctx);
    s->wait_us = ew_xfer_span_us(&s->x) + EW_SES_GRACE_US;
    s->phase = PH_XFER;
}

/**
 * @brief An address frame, guarded, then its transaction.
 * @param s    the session
 * @param addr the address
 */
static void act_address(ew_session_t *s, uint16_t addr)
{
    const size_t n = ew_a_address(s->tx, addr);
    const ew_refuse_t r = ew_guard_frame(&s->guard, s->tx, n);

    if (r != EW_REF_NONE) {
        refused(s, r);
        return;
    }
    start_xfer(s, (uint16_t)n, 0u, 1u, EW_A_ADDRESS_US);
}

/**
 * @brief A read frame, guarded, then its transaction.
 * @param s the session
 * @param n bytes asked for
 */
static void act_read(ew_session_t *s, uint16_t n)
{
    const size_t f = ew_a_read(s->tx, n);
    const ew_refuse_t r = ew_guard_frame(&s->guard, s->tx, f);

    if (r != EW_REF_NONE) {
        refused(s, r);
        return;
    }
    start_xfer(s, (uint16_t)f, 0u, (uint16_t)(n + 3u), EW_A_READ_US);
}

/**
 * @brief An erase or a program, guarded, then its transaction.  From here on
 *        the page may not be what was read.
 * @param s       the session
 * @param cmd     the command
 * @param wait_us the wait for its answer
 */
static void act_flash(ew_session_t *s, uint8_t cmd, uint32_t wait_us)
{
    const size_t n = ew_a_short(s->tx, cmd, 0x01u);
    const ew_refuse_t r = ew_guard_frame(&s->guard, s->tx, n);

    if (r != EW_REF_NONE) {
        refused(s, r);
        return;
    }
    s->dirty = true;
    s->reread = false;
    s->writing = true;
    start_xfer(s, (uint16_t)n, 0u, 1u, wait_us);
}

/**
 * @brief The announcement and the page in one transaction, a pause between.
 * @param s the session
 */
static void act_data(ew_session_t *s)
{
    const size_t head = ew_a_announce(s->tx, s->id.len);
    ew_refuse_t r = ew_guard_frame(&s->guard, s->tx, head);

    if (r == EW_REF_NONE) {
        const size_t body = ew_a_data(s->tx + head, s->target, s->id.len);

        r = ew_guard_frame(&s->guard, s->tx + head, body);
        if (r == EW_REF_NONE) {
            start_xfer(s, (uint16_t)(head + body), (uint16_t)head, 1u,
                       EW_A_BUFFER_US);
            return;
        }
    }
    refused(s, r);
}

/**
 * @brief Whether two pages agree: on AM32 the bootloader sets byte 2 itself.
 * @param s the session
 * @param a a page
 * @param b a page
 * @return true when they agree
 */
static bool same_page(const ew_session_t *s, const uint8_t *a, const uint8_t *b)
{
    for (size_t i = 0; i < s->id.len; ++i) {
        if (a[i] != b[i] && !(s->id.fw == (uint8_t)EW_FW_AM && i == 2u)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief The re-read before a write against the page read.
 * @param s the session
 * @return true when the write goes on
 */
static bool check_before(ew_session_t *s)
{
    if (!same_page(s, s->page, s->orig)) {
        ew_guard_done(&s->guard);
        finish(s, EW_SES_ERR_CHANGED);
        return false;
    }
    if (s->id.fw == (uint8_t)EW_FW_AM && !s->restoring) {
        /* The bootloader can rewrite byte 2 when it starts: the host leaves
         * it as read, and this is the latest read. */
        s->orig[2] = s->page[2];
        s->target[2] = s->page[2];
    }
    return true;
}

/**
 * @brief Enter the write procedure from its start.
 * @param s the session
 */
static void restart_write(ew_session_t *s)
{
    s->attempts++;
    s->seq = s->id.fw == (uint8_t)EW_FW_AM ? k_write_am : k_write_efm8;
    s->pc = WRITE_FROM;
    s->tries = 0u;
}

/**
 * @brief The read after a write: done, again, or the end of it.
 * @param s the session
 * @return true when the procedure runs again
 */
static bool check_after(ew_session_t *s)
{
    s->reread = s->dirty;
    if (same_page(s, s->page, s->target)) {
        memcpy(s->orig, s->page, s->id.len);
        memcpy(s->target, s->page, s->id.len);
        s->dirty = false;
        s->reread = false;
        s->have_read = true;
        if (s->restoring) {
            s->id.erased = false;
            s->id.layout = s->page[1];
            s->id.why = EW_WHY_NONE;
            s->id.writable = true;
        }
        ew_guard_done(&s->guard);
        finish(s, EW_SES_OK);
        return false;
    }
    if (s->attempts < EW_SES_ATTEMPTS && !s->stop_wanted) {
        restart_write(s);
        return true;
    }
    if (same_page(s, s->page, s->orig)) {
        s->dirty = false;
        s->reread = false;
        ew_guard_done(&s->guard);
        finish(s, EW_SES_ERR_WRITE_FAILED);
    } else {
        /* Neither page, whoever moved it: only these two go now. */
        s->dirty = true;
        s->reread = true;
        finish(s, EW_SES_ERR_TORN);
    }
    return false;
}

/**
 * @brief The identification, the reads done.
 * @param s the session
 */
static void finish_id(ew_session_t *s)
{
    if (s->contact.cpu == (uint8_t)EW_CPU_EFM8) {
        ew_id_efm8(&s->contact, s->orig, &s->id);
    } else {
        ew_id_arm(&s->contact, s->orig, s->name_region,
                  s->contact.protocol == 3u ? s->record : NULL, &s->id);
    }
    finish(s, s->id.fw == (uint8_t)EW_FW_NONE ? EW_SES_ERR_NOT_KNOWN
                                              : EW_SES_OK);
}

/**
 * @brief The actions up to the next one that talks, or the end of the list.
 * @param s the session
 */
static void run(ew_session_t *s)
{
    while (s->op != OP_NONE && s->phase != PH_XFER) {
        const uint8_t a = s->seq[s->pc];

        switch (a) {
        case A_CONTACT: {
            const size_t n = ew_a_contact(s->tx);
            const ew_refuse_t r = ew_guard_frame(&s->guard, s->tx, n);

            if (r != EW_REF_NONE) {
                refused(s, r);
            } else {
                start_xfer(s, (uint16_t)n, 0u, EW_A_ANSWER_LEN,
                           EW_A_CONTACT_US);
            }
            return;
        }
        case A_SET_BASE:
            act_address(s, s->contact.base);
            return;
        case A_SET_NAME:
            act_address(s, EW_AM_ADDR_NAME);
            return;
        case A_SET_RECORD:
            act_address(s, EW_AM_ADDR_RECORD);
            return;
        case A_READ_ID:
            act_read(s, s->contact.cpu == (uint8_t)EW_CPU_EFM8 ? EW_MAP_LEN_S
                                                               : EW_MAP_LEN_AM);
            return;
        case A_READ_NAME:
            act_read(s, EW_ID_NAME_REGION);
            return;
        case A_READ_RECORD:
            act_read(s, EW_ID_RECORD_LEN);
            return;
        case A_READ_PAGE:
            act_read(s, s->id.len);
            return;
        case A_ERASE:
            act_flash(s, EW_A_CMD_ERASE, EW_A_ERASE_US);
            return;
        case A_DATA:
            act_data(s);
            return;
        case A_PROGRAM:
            act_flash(s, EW_A_CMD_PROGRAM, EW_A_PROGRAM_US);
            return;
        case A_LEAVE: {
            const uint8_t arg = (uint8_t)(s->contact.cpu == (uint8_t)EW_CPU_ARM ? 0u : 1u);
            const size_t n = ew_a_short(s->tx, EW_A_CMD_LEAVE, arg);
            const ew_refuse_t r = ew_guard_frame(&s->guard, s->tx, n);

            if (r != EW_REF_NONE) {
                refused(s, r);
            } else {
                start_xfer(s, (uint16_t)n, 0u, 0u, 0u);
            }
            return;
        }
        case A_CHECK_BEFORE:
            if (!check_before(s)) {
                return;
            }
            s->pc++;
            s->attempts = 1u;
            break;
        case A_CHECK_AFTER:
            if (!check_after(s)) {
                return;
            }
            break;
        case A_FINISH_ID:
            finish_id(s);
            return;
        default: /* A_FINISH_READ */
            s->have_read = true;
            if (s->dirty) {
                s->reread = true;
            }
            finish(s, EW_SES_OK);
            return;
        }
    }
}

/**
 * @brief A step of a write failed: find out what the page holds.  Outside a
 *        write's procedure the operation ends.
 * @param s the session
 */
static void step_failed(ew_session_t *s)
{
    const bool in_proc = s->op == OP_WRITE && s->pc >= WRITE_FROM;

    if (s->op == OP_ENTER && s->seq == k_contact_seq) {
        finish(s, (ew_ans_t)s->last_ans == EW_ANS_SILENT ? EW_SES_ERR_SILENT
                                                         : EW_SES_ERR_CONTACT);
        return;
    }
    if (in_proc && s->seq != k_recover) {
        s->seq = k_recover;
        s->pc = 0u;
        s->tries = 0u;
        return;
    }
    if (s->op == OP_WRITE && s->dirty) {
        finish(s, EW_SES_ERR_LOST);
        return;
    }
    if (s->op == OP_WRITE) {
        ew_guard_done(&s->guard);
    }
    finish(s, EW_SES_ERR_READ);
}

/**
 * @brief Whether an action may be repeated when its answer does not fit.  An
 *        erase and a program are not: the read that follows says what they
 *        did.
 * @param a the action
 * @return true when it may
 */
static bool may_retry(uint8_t a)
{
    return a != A_ERASE && a != A_PROGRAM;
}

/**
 * @brief A try of the running action failed.
 * @param s the session
 * @param a the action
 */
static void try_failed(ew_session_t *s, uint8_t a)
{
    const bool is_read = a == A_READ_ID || a == A_READ_NAME
                         || a == A_READ_RECORD || a == A_READ_PAGE;

    s->tries++;
    if (may_retry(a) && s->tries < EW_SES_TRIES) {
        if (is_read) {
            /* An ESC can forget its address with any read: set it again. */
            s->pc--;
        }
        return;
    }
    step_failed(s);
}

/**
 * @brief A first contact was answered: choose the reads that identify the ESC.
 * @param s the session
 */
static void contact_done(ew_session_t *s)
{
    ew_id_contact(s->rx, &s->contact);
    ew_guard_target(&s->guard, &s->contact);
    s->in_mode = s->contact.cpu != (uint8_t)EW_CPU_NONE;
    s->tries = 0u;
    s->pc = 0u;
    if (s->contact.why != (uint8_t)EW_WHY_NONE) {
        s->id.fw = EW_FW_NONE;
        s->id.why = s->contact.why;
        finish(s, EW_SES_ERR_NOT_KNOWN);
    } else if (s->contact.cpu == (uint8_t)EW_CPU_EFM8) {
        s->seq = k_id_efm8;
    } else {
        s->seq = s->contact.protocol == 3u ? k_id_arm3 : k_id_arm2;
    }
}

/**
 * @brief A transaction ended: judge its answer and move on.
 * @param s the session
 * @param c its capture
 */
static void xfer_done(ew_session_t *s, const ew_capture_t *c)
{
    const uint8_t a = s->seq[s->pc];
    ew_ans_t ans = EW_ANS_OK;
    uint16_t n = 0u;
    uint8_t *to = NULL;

    s->phase = PH_IDLE;
    switch (a) {
    case A_CONTACT:
        ans = ew_a_contact_reply(c, s->rx);
        s->last_ans = (uint8_t)ans;
        if (ans == EW_ANS_OK) {
            contact_done(s);
        } else {
            s->tries++;
            if (s->tries >= EW_A_CONTACT_TRIES) {
                step_failed(s);
            }
        }
        return;
    case A_LEAVE:
        s->in_mode = false;
        s->t0 = s->d.now_us(s->d.ctx);
        s->wait_us = EW_SES_LEAVE_IDLE_US;
        s->phase = PH_LEAVE_IDLE;
        return;
    case A_READ_ID:
        n = s->contact.cpu == (uint8_t)EW_CPU_EFM8 ? EW_MAP_LEN_S
                                                   : EW_MAP_LEN_AM;
        to = s->orig;
        break;
    case A_READ_NAME:
        n = EW_ID_NAME_REGION;
        to = s->name_region;
        break;
    case A_READ_RECORD:
        n = EW_ID_RECORD_LEN;
        to = s->record;
        break;
    case A_READ_PAGE:
        n = s->id.len;
        to = s->page;
        break;
    default:
        break;
    }
    ans = to != NULL ? ew_a_read_reply(c, s->rx, n) : ew_a_one(c, s->rx);
    s->last_ans = (uint8_t)ans;
    if (ans != EW_ANS_OK) {
        try_failed(s, a);
        return;
    }
    if (to != NULL) {
        memcpy(to, s->rx, n);
        if (a == A_READ_PAGE && !s->dirty && s->seq == k_read) {
            memcpy(s->orig, s->rx, n);
        }
        s->tries = 0u;
    } else if (a != A_SET_BASE && a != A_SET_NAME && a != A_SET_RECORD) {
        s->tries = 0u;
    }
    s->pc++;
}

ew_ses_t ew_session_step(ew_session_t *s)
{
    if (s == NULL) {
        return EW_SES_ERR_ARG;
    }
    const uint32_t now = s->d.now_us(s->d.ctx);

    switch (s->phase) {
    case PH_HOLD:
        if (!ew_due(now, s->t0, s->wait_us)) {
            return EW_SES_BUSY;
        }
        s->phase = PH_IDLE;
        run(s);
        break;
    case PH_XFER: {
        ew_capture_t c;

        if (s->d.poll(s->d.ctx, &c)) {
            xfer_done(s, &c);
            if (s->phase == PH_IDLE) {
                run(s);
            }
        } else if (ew_due(now, s->t0, s->wait_us)) {
            driver_failed(s);
        }
        break;
    }
    case PH_LEAVE_IDLE:
        if (ew_due(now, s->t0, s->wait_us)) {
            s->d.line(s->d.ctx, EW_LINE_LOW);
            s->t0 = now;
            s->wait_us = EW_LEAVE_LOW_MS * 1000u;
            s->phase = PH_LOW;
        }
        break;
    case PH_LOW:
        if (!ew_due(now, s->t0, s->wait_us)) {
            return EW_SES_BUSY;
        }
        s->d.line(s->d.ctx, EW_LINE_PARK);
        finish(s, EW_SES_OK);
        break;
    default:
        break;
    }
    return s->op != OP_NONE ? EW_SES_BUSY : s->result;
}

/**
 * @brief Whether a new operation may start.
 * @param s the session
 * @return EW_SES_OK, or the refusal
 */
static ew_ses_t may_start(const ew_session_t *s)
{
    if (s->op != OP_NONE || !s->in_mode) {
        return EW_SES_ERR_STATE;
    }
    return EW_SES_OK;
}

/**
 * @brief Start an operation at the first action of a list.
 * @param s   the session
 * @param op  the operation
 * @param seq its actions
 * @return EW_SES_BUSY, or its result when it ended at once
 */
static ew_ses_t begin(ew_session_t *s, uint8_t op, const uint8_t *seq)
{
    s->op = op;
    s->seq = seq;
    s->pc = 0u;
    s->tries = 0u;
    s->phase = PH_IDLE;
    s->refuse = (uint8_t)EW_REF_NONE;
    s->result = EW_SES_BUSY;
    run(s);
    return s->op != OP_NONE ? EW_SES_BUSY : s->result;
}

ew_ses_t ew_session_read(ew_session_t *s)
{
    if (s == NULL) {
        return EW_SES_ERR_ARG;
    }
    const ew_ses_t st = may_start(s);

    if (st != EW_SES_OK) {
        return st;
    }
    if (s->id.len == 0u) {
        return EW_SES_ERR_STATE;
    }
    return begin(s, OP_READ, k_read);
}

/**
 * @brief Run the write procedure again on a page left half written, with no
 *        compare against the page read: it is no longer that page.
 * @param s the session
 * @return EW_SES_BUSY, or the result when it ended at once
 */
static ew_ses_t resume_write(ew_session_t *s)
{
    s->attempts = 0u;
    s->op = OP_WRITE;
    s->refuse = (uint8_t)EW_REF_NONE;
    s->result = EW_SES_BUSY;
    s->phase = PH_IDLE;
    restart_write(s);
    run(s);
    return s->op != OP_NONE ? EW_SES_BUSY : s->result;
}

ew_ses_t ew_session_write(ew_session_t *s, const uint8_t *draft, size_t n)
{
    if (s == NULL || draft == NULL) {
        return EW_SES_ERR_ARG;
    }
    const ew_ses_t st = may_start(s);

    if (st != EW_SES_OK) {
        return st;
    }
    if (s->id.erased) {
        return EW_SES_ERR_ERASED;
    }
    if (!s->id.writable) {
        return EW_SES_ERR_NOT_WRITABLE;
    }
    if (!s->have_read) {
        return EW_SES_ERR_NO_READ;
    }
    if (n != s->id.len) {
        return EW_SES_ERR_ARG;
    }
    if (s->dirty) {
        if (!s->reread) {
            return EW_SES_ERR_MUST_READ;
        }
        if (memcmp(draft, s->target, n) != 0
            && memcmp(draft, s->orig, n) != 0) {
            return EW_SES_ERR_DIRTY;
        }
    } else if (memcmp(draft, s->orig, n) == 0) {
        return EW_SES_OK;
    }
    memmove(s->target, draft, n);
    const ew_refuse_t r = ew_guard_approve(&s->guard, &s->id, s->orig,
                                           s->target, n, &s->bad_off);

    if (r != EW_REF_NONE) {
        s->refuse = (uint8_t)r;
        return EW_SES_ERR_REFUSED;
    }
    s->attempts = 1u;
    if (s->dirty) {
        return resume_write(s);
    }
    return begin(s, OP_WRITE,
                 s->id.fw == (uint8_t)EW_FW_AM ? k_write_am : k_write_efm8);
}

void ew_session_backup_id(const ew_session_t *s, ew_backup_id_t *out)
{
    memset(out, 0, sizeof(*out));
    out->pin_code = s->contact.pin_code;
    out->flash_code = s->contact.flash_code;
    out->protocol = s->contact.protocol;
    memcpy(out->name, s->id.name, sizeof(out->name));
}

ew_ses_t ew_session_restore(ew_session_t *s, const ew_backup_id_t *from,
                            const uint8_t *page, size_t n)
{
    if (s == NULL || from == NULL || page == NULL) {
        return EW_SES_ERR_ARG;
    }
    const ew_ses_t st = may_start(s);

    if (st != EW_SES_OK) {
        return st;
    }
    if (!s->id.erased) {
        return EW_SES_ERR_STATE;
    }
    ew_backup_id_t here;

    ew_session_backup_id(s, &here);
    if (here.pin_code != from->pin_code || here.flash_code != from->flash_code
        || here.protocol != from->protocol
        || strncmp(here.name, from->name, sizeof(here.name)) != 0) {
        return EW_SES_ERR_BACKUP;
    }
    if (n > EW_PAGE_MAX) {
        return EW_SES_ERR_ARG;
    }
    if (s->dirty) {
        /* A restore left half done: this backup again, and only this one. */
        if (!s->reread) {
            return EW_SES_ERR_MUST_READ;
        }
        if (n != s->id.len || memcmp(page, s->target, n) != 0) {
            return EW_SES_ERR_DIRTY;
        }
    }
    memmove(s->target, page, n);
    const ew_refuse_t r = ew_guard_approve_restore(&s->guard, &s->id, s->orig,
                                                   s->target, n);

    if (r != EW_REF_NONE) {
        s->refuse = (uint8_t)r;
        return EW_SES_ERR_REFUSED;
    }
    s->restoring = true;
    s->attempts = 1u;
    if (s->dirty) {
        return resume_write(s);
    }
    return begin(s, OP_WRITE, k_write_am);
}

ew_ses_t ew_session_leave(ew_session_t *s)
{
    if (s == NULL) {
        return EW_SES_ERR_ARG;
    }
    const ew_ses_t st = may_start(s);

    if (st != EW_SES_OK) {
        return st;
    }
    if (s->id.erased) {
        return EW_SES_ERR_ERASED;
    }
    if (s->dirty) {
        return EW_SES_ERR_DIRTY;
    }
    return begin(s, OP_LEAVE, k_leave);
}

void ew_session_stop(ew_session_t *s)
{
    if (s == NULL) {
        return;
    }
    if (s->writing) {
        s->stop_wanted = true;
        return;
    }
    const bool was_running = s->op != OP_NONE;
    const ew_ses_t kept = s->result;

    ew_session_abort(s);
    s->stopped = true;
    s->result = was_running ? EW_SES_ERR_STOPPED : kept;
}

void ew_session_abort(ew_session_t *s)
{
    if (s == NULL) {
        return;
    }
    const bool was_running = s->op != OP_NONE;

    if (s->phase == PH_XFER) {
        s->d.abort(s->d.ctx);
    }
    s->in_mode = false;
    s->d.line(s->d.ctx, EW_LINE_PARK);
    s->stop_wanted = false;
    const ew_ses_t kept = s->result;

    finish(s, was_running ? EW_SES_ERR_ABORTED : kept);
}

bool ew_session_writing(const ew_session_t *s)
{
    return s != NULL && s->writing;
}
