/**
 * @file
 * @brief The panel's half of the YGE_PROG and YGE_DATA pages.  See yge_link.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "yge_link.h"

#include <stddef.h>
#include <string.h>

#include "openyge_guard.h"

/* The exchange in flight. */
enum {
    X_NONE = 0,
    X_STATUS,  /* read YGE_PROG's registers */
    X_FRAME,   /* a frame ahead of the command */
    X_CMD,     /* the command */
    X_URGENT,  /* STOP or ABORT */
    X_BLOCK,   /* the block a fetch reads next */
    X_DATA,    /* its registers */
};

/** @brief Registers of a write frame: one CAN frame. */
#define FRAME_REGS 4u
/** @brief Frames of a block: the choice of the block, and its registers. */
#define BLOCK_FRAMES (1u + LINK_YD_COUNT / FRAME_REGS)
/** @brief Frames of the staged registers: 4 and 4. */
#define STAGE_FRAMES ((LINK_YP_W_STAGED + FRAME_REGS - 1u) / FRAME_REGS)
/** @brief A staged register of @p l by its write offset @p reg. */
#define SG(l, reg) ((l)->staged[(unsigned)(reg) - (unsigned)LINK_YP_W_PORT])

_Static_assert(LINK_YD_COUNT % FRAME_REGS == 0u, "a block is whole frames");
_Static_assert(LINK_YP_W_PORT + LINK_YP_W_STAGED == LINK_YP_W_BLOCK,
               "the staged registers end where BLOCK starts");

/**
 * @brief The blocks @p n values take: 1 or 2.
 * @param n the values, 1 to YGE_LINK_VALUES_MAX
 * @return the blocks
 */
static unsigned blocks_of(uint16_t n)
{
    return n > LINK_YD_COUNT ? 2u : 1u;
}

/* --- the status ---------------------------------------------------------- */

/**
 * @brief A fetch of the read copy starts: its READS, CRC and ENTRIES are taken
 *        from @p regs once, and every step of it uses them.
 * @param l    the module
 * @param regs the registers of the read that asks for it
 */
static void fetch_start(yge_link_t *l, const uint16_t *regs)
{
    l->fetching = 1u;
    l->fetch_at = 0u;
    l->block_set = 0u;
    l->fetch_tries = 0u;
    l->fetch_given_up = 0u;
    l->fetch_reads = regs[LINK_YP_READS];
    l->fetch_crc = regs[LINK_YP_READ_CRC];
    l->fetch_n = regs[LINK_YP_ENTRIES];
}

/**
 * @brief Whether the page runs something: an outcome waits for its end.
 * @param state a link_yge_state_t
 * @return true for the states that move by themselves
 */
static bool state_runs(uint16_t state)
{
    return state == (uint16_t)LINK_YGE_STOPPING
           || state == (uint16_t)LINK_YGE_POWER_OFF
           || state == (uint16_t)LINK_YGE_POWER_ON
           || state == (uint16_t)LINK_YGE_RUN;
}

/**
 * @brief Whether an operation carries the key: ENTER and WRITE.
 * @param op a link_yge_op_t
 * @return true then
 */
static bool op_keyed(uint8_t op)
{
    return op == (uint8_t)LINK_YGE_OP_ENTER || op == (uint8_t)LINK_YGE_OP_WRITE;
}

/**
 * @brief Whether an operation has staged registers ahead of its command.
 * @param op a link_yge_op_t
 * @return true for OPEN, ENTER and WRITE
 */
static bool op_staged(uint8_t op)
{
    return op == (uint8_t)LINK_YGE_OP_OPEN || op_keyed(op);
}

/**
 * @brief The request ends: its outcome, unless it is the CLOSE after a
 *        restart.
 * @param l    the module
 * @param kind how it went
 * @param nack a NACK's reason
 */
static void finish(yge_link_t *l, yge_link_outcome_kind_t kind, uint8_t nack)
{
    if (!l->internal) {
        yge_link_outcome_t *o = &l->outcome;
        const uint16_t *s = l->status;

        memset(o, 0, sizeof(*o));
        o->kind = kind;
        o->op = l->op;
        o->id = l->id;
        o->nack = nack;
        o->taken = l->taken != 0u;
        o->state = (uint8_t)s[LINK_YP_STATE];
        o->ses = (uint8_t)(s[LINK_YP_SES] & 0xFFu);
        o->guard = (uint8_t)(s[LINK_YP_SES] >> 8);
        o->result = (uint8_t)(s[LINK_YP_RESULT] & 0xFFu);
        o->refusal = (uint8_t)(s[LINK_YP_RESULT] >> 8);
        l->outcome_new = 1u;
    } else {
        l->close_timed = 1u;
        l->close_at = l->now_ms + YGE_LINK_CLOSE_RETRY_MS;
    }
    l->active = 0u;
    l->internal = 0u;
    l->cmd_gone = 0u;
    l->verdict = 0u;
    l->doubt = 0u;
    l->limit_cut = 0u;
    l->tries = 0u;
    l->send_at = 0u;
}

/**
 * @brief Whether a read names a copy a fetch can bring: a whole table read,
 *        of 1 to YGE_LINK_VALUES_MAX entries.
 * @param regs the registers
 * @return true then
 */
static bool copy_named(const uint16_t *regs)
{
    return (regs[LINK_YP_FLAGS] & LINK_YP_F_HAVE_READ) != 0u
           && regs[LINK_YP_ENTRIES] != 0u
           && regs[LINK_YP_ENTRIES] <= YGE_LINK_VALUES_MAX;
}

/**
 * @brief Whether a fetch under way is of another copy than @p regs name.
 * @param l    the module
 * @param regs the registers
 * @return true then
 */
static bool fetch_other(const yge_link_t *l, const uint16_t *regs)
{
    return l->fetch_reads != regs[LINK_YP_READS]
           || l->fetch_crc != regs[LINK_YP_READ_CRC]
           || l->fetch_n != regs[LINK_YP_ENTRIES];
}

/**
 * @brief The read copy a read names: fetched, kept, or none.
 * @param l    the module
 * @param regs the registers
 */
static void follow_copy(yge_link_t *l, const uint16_t *regs)
{
    const uint16_t state = regs[LINK_YP_STATE];
    const yge_link_image_t *im = &l->image;

    if (state == (uint16_t)LINK_YGE_FREE || !copy_named(regs)) {
        /* The next session's copy is its own, also with equal values. */
        l->image.gen = 0u;
        l->fetching = 0u;
        l->fetch_given_up = 0u;
        return;
    }
    if (state == (uint16_t)LINK_YGE_READY && !l->fetching
        && (im->gen == 0u || im->reads != regs[LINK_YP_READS]
            || im->read_crc != regs[LINK_YP_READ_CRC]
            || im->entries != regs[LINK_YP_ENTRIES])
        && (!l->fetch_given_up || fetch_other(l, regs))) {
        fetch_start(l, regs);
    } else if (l->fetching && fetch_other(l, regs)) {
        /* Another read copy under the fetch: the blocks taken so far belong
         * to no table.  From the first block again, or not at all. */
        if (state == (uint16_t)LINK_YGE_READY) {
            fetch_start(l, regs);
        } else {
            l->fetching = 0u;
        }
    }
}

/**
 * @brief What a read of the page says: errors counted, and the fetch owed.
 * @param l    the module
 * @param regs the registers
 */
static void absorb(yge_link_t *l, const uint16_t *regs)
{
    const bool first = !l->known;
    const uint16_t was_state = first ? (uint16_t)LINK_YGE_FREE
                                     : l->status[LINK_YP_STATE];
    const uint16_t was_flags = l->status[LINK_YP_FLAGS];
    const uint16_t state = regs[LINK_YP_STATE];
    const uint16_t flags = regs[LINK_YP_FLAGS];

    if (!first && (flags & LINK_YP_F_FAULT) != 0u
        && (was_flags & LINK_YP_F_FAULT) == 0u) {
        ++l->faults;
    }
    if (state == (uint16_t)LINK_YGE_NOT_BACK
        && was_state != (uint16_t)LINK_YGE_NOT_BACK) {
        ++l->faults;
        l->not_back = 1u;
        l->not_back_port = (uint8_t)(regs[LINK_YP_PORT] & 0xFFu);
        /* CLOSE tries again, as after a restart. */
        l->close_owed = 1u;
        l->close_timed = 1u;
        l->close_at = l->now_ms + YGE_LINK_CLOSE_RETRY_MS;
    }
    memcpy(l->status, regs, sizeof(l->status));
    l->known = 1u;
    if (state == (uint16_t)LINK_YGE_FREE) {
        l->ours = 0u;
        l->close_owed = 0u;
    }
    follow_copy(l, regs);
}

/**
 * @brief What a status says of the request.
 * @param l the module
 */
static void judge(yge_link_t *l)
{
    const uint16_t *s = l->status;
    const uint16_t refusal = (uint16_t)(s[LINK_YP_RESULT] >> 8);

    if (l->verdict || l->doubt) {
        const bool took = s[LINK_YP_SEQ] == l->last_seq;

        if (!took) {
            /* Unanswered, and no sign of it: nothing ran.  Acknowledged, and
             * the page has another SEQ: it started again, or another writer. */
            finish(l, !l->doubt ? YGE_LINK_OUT_RESTARTED
                      : l->urgent != 0u ? YGE_LINK_OUT_DROPPED
                                        : YGE_LINK_OUT_NO_ANSWER, 0u);
            return;
        }
        l->verdict = 0u;
        l->doubt = 0u;
        l->taken = 1u;
        l->taken_ms = l->now_ms;
        if (refusal != (uint16_t)LINK_YGE_REF_NONE) {
            finish(l, YGE_LINK_OUT_REFUSED, 0u);
            return;
        }
        if (l->op == (uint8_t)LINK_YGE_OP_OPEN) {
            l->ours = 1u;
        }
    } else if (s[LINK_YP_SEQ] != l->last_seq) {
        finish(l, YGE_LINK_OUT_RESTARTED, 0u);
        return;
    }
    if (!state_runs(s[LINK_YP_STATE])) {
        finish(l, YGE_LINK_OUT_DONE, 0u);
    } else if (!l->limit_cut
               && (uint32_t)(l->now_ms - l->taken_ms) >= YGE_LINK_RUN_MAX_MS) {
        l->limit_cut = 1u;
        yge_link_abort(l);
    }
}

/* --- requests ------------------------------------------------------------ */

/**
 * @brief The phase as yge_link_phase() gives it.
 * @param l the module
 * @return the phase
 */
static yge_link_phase_t phase_of(const yge_link_t *l)
{
    if (!l->up) {
        return YGE_LINK_DOWN;
    }
    if (!l->has_page) {
        return YGE_LINK_NO_PAGE;
    }
    if (!l->known || l->close_owed) {
        return YGE_LINK_SYNC;
    }
    if (l->active) {
        return l->taken || l->verdict ? YGE_LINK_RUNNING : YGE_LINK_SENDING;
    }
    return l->fetching ? YGE_LINK_FETCHING : YGE_LINK_READY;
}

/**
 * @brief A request taken: everything ahead of its command is set up by the
 *        caller afterwards.
 * @param l  the module
 * @param op the operation
 * @param id the caller's number
 * @return YGE_LINK_START_OK, or why not
 */
static yge_link_start_t begin(yge_link_t *l, link_yge_op_t op, uint32_t id)
{
    if (l == NULL) {
        return YGE_LINK_START_ARG;
    }
    const yge_link_phase_t ph = phase_of(l);

    if (ph == YGE_LINK_DOWN) {
        return YGE_LINK_START_DOWN;
    }
    if (ph == YGE_LINK_NO_PAGE) {
        return YGE_LINK_START_NO_PAGE;
    }
    if (ph != YGE_LINK_READY || l->pending != X_NONE || l->urgent != 0u) {
        return YGE_LINK_START_BUSY;
    }
    l->active = 1u;
    l->op = (uint8_t)op;
    l->id = id;
    l->send_at = 0u;
    l->tries = 0u;
    l->cmd_gone = 0u;
    l->verdict = 0u;
    l->doubt = 0u;
    l->taken = 0u;
    l->limit_cut = 0u;
    memset(l->staged, 0, sizeof(l->staged));
    return YGE_LINK_START_OK;
}

yge_link_start_t yge_link_open(yge_link_t *l, uint8_t port, uint32_t id)
{
    const yge_link_start_t r = begin(l, LINK_YGE_OP_OPEN, id);

    if (r == YGE_LINK_START_OK) {
        SG(l, LINK_YP_W_PORT) = port;
    }
    return r;
}

yge_link_start_t yge_link_enter(yge_link_t *l, const yge_link_power_t *p,
                                uint32_t id)
{
    if (p == NULL) {
        return YGE_LINK_START_ARG;
    }
    const yge_link_start_t r = begin(l, LINK_YGE_OP_ENTER, id);

    if (r == YGE_LINK_START_OK) {
        SG(l, LINK_YP_W_PORT) = (uint16_t)(l->status[LINK_YP_PORT] & 0xFFu);
        SG(l, LINK_YP_W_POWER) = p->power;
        SG(l, LINK_YP_W_MV) = p->mv;
        SG(l, LINK_YP_W_MA) = p->ma;
        SG(l, LINK_YP_W_BOOT_MS) = p->boot_ms;
    }
    return r;
}

yge_link_start_t yge_link_hand_done(yge_link_t *l, uint32_t id)
{
    return begin(l, LINK_YGE_OP_HAND_DONE, id);
}

yge_link_start_t yge_link_read(yge_link_t *l, uint32_t id)
{
    return begin(l, LINK_YGE_OP_READ, id);
}

yge_link_start_t yge_link_write(yge_link_t *l, const uint16_t *draft,
                                uint16_t n, uint16_t reads, uint16_t read_crc,
                                uint32_t id)
{
    if (draft == NULL || n == 0u || n > YGE_LINK_VALUES_MAX) {
        return YGE_LINK_START_ARG;
    }
    const yge_link_start_t r = begin(l, LINK_YGE_OP_WRITE, id);

    if (r == YGE_LINK_START_OK) {
        memset(l->draft, 0, sizeof(l->draft));
        memcpy(l->draft, draft, (size_t)n * sizeof(draft[0]));
        l->draft_n = n;
        SG(l, LINK_YP_W_READS) = reads;
        SG(l, LINK_YP_W_READ_CRC) = read_crc;
        SG(l, LINK_YP_W_DRAFT_CRC) = oy_guard_table_crc(draft, n);
    }
    return r;
}

yge_link_start_t yge_link_leave(yge_link_t *l, uint32_t id)
{
    return begin(l, LINK_YGE_OP_LEAVE, id);
}

yge_link_start_t yge_link_clear(yge_link_t *l, uint32_t id)
{
    return begin(l, LINK_YGE_OP_CLEAR, id);
}

yge_link_start_t yge_link_close(yge_link_t *l, uint32_t id)
{
    return begin(l, LINK_YGE_OP_CLOSE, id);
}

/**
 * @brief STOP or ABORT owed: a request not gone yet is dropped.
 * @param l  the module
 * @param op the operation
 */
static void ask_urgent(yge_link_t *l, link_yge_op_t op)
{
    if (l == NULL || !l->up || !l->has_page) {
        return;
    }
    if (l->active && !l->cmd_gone && !l->taken) {
        finish(l, YGE_LINK_OUT_DROPPED, 0u);
    } else if (l->active && !l->taken && !l->verdict) {
        /* Gone with no answer yet: not sent again.  A read says whether the
         * page took it, and the STOP or ABORT goes to what that shows. */
        l->doubt = 1u;
    }
    /* ABORT is the stronger one: a STOP owed does not hold it back. */
    if (l->urgent != (uint8_t)LINK_YGE_OP_ABORT) {
        l->urgent = (uint8_t)op;
    }
    l->urgent_tries = 0u;
    l->urgent_read = 0u;
    l->poll_now = 1u;
}

void yge_link_stop_follow(yge_link_t *l, uint32_t stops)
{
    if (l != NULL && l->up && l->has_page && stops != l->stops_seen) {
        l->stops_seen = stops;
        yge_link_stop(l);
    }
}

void yge_link_stop(yge_link_t *l)
{
    ask_urgent(l, LINK_YGE_OP_STOP);
}

void yge_link_abort(yge_link_t *l)
{
    ask_urgent(l, LINK_YGE_OP_ABORT);
}

/* --- the exchanges ------------------------------------------------------- */

/**
 * @brief A command frame with the next SEQ.  The key goes with ENTER and WRITE
 *        only.
 * @param l  the module
 * @param op the operation
 * @param x  takes the frame
 */
static void command_frame(yge_link_t *l, uint8_t op, yge_link_xfer_t *x)
{
    x->write = true;
    x->page = LINK_PAGE_YGE_PROG;
    x->off = (uint8_t)LINK_YP_W_CMD;
    x->n = (uint8_t)LINK_YP_W_CMD_FRAME;
    x->regs[LINK_YP_W_CMD] = op;
    x->regs[LINK_YP_W_SEQ] = (uint16_t)(l->status[LINK_YP_SEQ] + 1u);
    x->regs[LINK_YP_W_KEY] = op_keyed(op) ? LINK_YGE_KEY : 0u;
    x->regs[LINK_YP_W_CRC] = op_staged(op)
                                 ? link_kst_crc(l->staged, LINK_YP_W_STAGED) : 0u;
    l->last_seq = x->regs[LINK_YP_W_SEQ];
}

/**
 * @brief The frame that chooses a block of YGE_DATA.
 * @param x     takes the frame
 * @param block the block
 */
static void block_choice(yge_link_xfer_t *x, uint16_t block)
{
    x->write = true;
    x->page = LINK_PAGE_YGE_PROG;
    x->off = (uint8_t)LINK_YP_W_BLOCK;
    x->n = 1u;
    x->regs[0] = block;
}

/**
 * @brief The frame @c send_at of the request ahead of its command.
 * @param l the module
 * @param x takes it
 * @return false when none is left: the command is next
 */
static bool frame_at(const yge_link_t *l, yge_link_xfer_t *x)
{
    unsigned at = l->send_at;

    if (l->op == (uint8_t)LINK_YGE_OP_WRITE) {
        const unsigned blocks = blocks_of(l->draft_n);

        if (at < blocks * BLOCK_FRAMES) {
            const unsigned b = at / BLOCK_FRAMES;
            const unsigned f = at % BLOCK_FRAMES;

            if (f == 0u) {
                block_choice(x, (uint16_t)(LINK_YD_DRAFT + b));
            } else {
                const unsigned first = (f - 1u) * FRAME_REGS;

                x->write = true;
                x->page = LINK_PAGE_YGE_DATA;
                x->off = (uint8_t)first;
                x->n = (uint8_t)FRAME_REGS;
                memcpy(x->regs, &l->draft[(size_t)b * LINK_YD_COUNT + first],
                       FRAME_REGS * sizeof(x->regs[0]));
            }
            return true;
        }
        at -= blocks * BLOCK_FRAMES;
    }
    if (op_staged(l->op) && at < STAGE_FRAMES) {
        const unsigned first = at * FRAME_REGS;

        x->write = true;
        x->page = LINK_PAGE_YGE_PROG;
        x->off = (uint8_t)(LINK_YP_W_PORT + first);
        x->n = (uint8_t)FRAME_REGS;
        memcpy(x->regs, &l->staged[first], FRAME_REGS * sizeof(x->regs[0]));
        return true;
    }
    return false;
}

/**
 * @brief The next exchange of a fetch.
 * @param l the module
 * @param x takes it
 */
static void fetch_next(yge_link_t *l, yge_link_xfer_t *x)
{
    if (!l->block_set) {
        block_choice(x, (uint16_t)(LINK_YD_READ + l->fetch_at));
        l->pending = X_BLOCK;
    } else {
        x->write = false;
        x->page = LINK_PAGE_YGE_DATA;
        x->off = 0u;
        x->n = (uint8_t)LINK_YD_COUNT;
        l->pending = X_DATA;
    }
}

/**
 * @brief A block read: into the buffer, and at the last one the check against
 *        the CRC the page named.
 * @param l    the module
 * @param regs its registers
 */
static void fetch_took(yge_link_t *l, const uint16_t *regs)
{
    yge_link_image_t *im = &l->image;

    memcpy(&l->fetch_buf[(size_t)l->fetch_at * LINK_YD_COUNT], regs,
           LINK_YD_COUNT * sizeof(regs[0]));
    ++l->fetch_at;
    l->block_set = 0u;
    if (l->fetch_at < blocks_of(l->fetch_n)) {
        return;
    }
    l->fetching = 0u;
    if (oy_guard_table_crc(l->fetch_buf, l->fetch_n) == l->fetch_crc) {
        memset(im->value, 0, sizeof(im->value));
        memcpy(im->value, l->fetch_buf, (size_t)l->fetch_n * sizeof(im->value[0]));
        im->reads = l->fetch_reads;
        im->read_crc = l->fetch_crc;
        im->entries = l->fetch_n;
        ++im->gen;
        if (im->gen == 0u) {
            im->gen = 1u;
        }
    } else if (++l->fetch_tries < YGE_LINK_FETCH_TRIES) {
        l->fetching = 1u;
        l->fetch_at = 0u;
    } else {
        l->fetch_given_up = 1u;
    }
}

/**
 * @brief A read of YGE_PROG's registers.
 * @param l the module
 * @param x takes it
 * @return true
 */
static bool status_read(yge_link_t *l, yge_link_xfer_t *x)
{
    x->write = false;
    x->page = LINK_PAGE_YGE_PROG;
    x->off = 0u;
    x->n = (uint8_t)LINK_YP_COUNT;
    l->pending = X_STATUS;
    l->polled = 1u;
    l->status_ms = l->now_ms;
    l->poll_now = 0u;
    return true;
}

bool yge_link_next(yge_link_t *l, uint32_t now_ms, yge_link_xfer_t *x)
{
    if (l == NULL || x == NULL || !l->up || !l->has_page
        || l->pending != X_NONE) {
        return false;
    }
    l->now_ms = now_ms;
    memset(x, 0, sizeof(*x));

    if (l->poll_now || !l->known) {
        return status_read(l, x);
    }
    /* What a stop asks first. */
    if (l->urgent != 0u) {
        /* A request under way may have taken a pin the last read did not
         * show; and a free page counts only from a read answered since the
         * ask. */
        if (l->status[LINK_YP_STATE] != (uint16_t)LINK_YGE_FREE || l->active) {
            command_frame(l, l->urgent, x);
            l->pending = X_URGENT;
            return true;
        }
        if (!l->urgent_read) {
            return status_read(l, x);
        }
        l->urgent = 0u;
    }
    /* The pin nobody here knows of, after a panel restart. */
    if (l->close_owed && !l->active
        && (!l->close_timed || (int32_t)(now_ms - l->close_at) >= 0)) {
        l->active = 1u;
        l->internal = 1u;
        l->op = (uint8_t)LINK_YGE_OP_CLOSE;
        l->send_at = 0u;
        l->tries = 0u;
        l->cmd_gone = 0u;
        l->taken = 0u;
    }
    if (l->active && !l->verdict && !l->taken && !l->doubt) {
        if (frame_at(l, x)) {
            l->pending = X_FRAME;
            return true;
        }
        /* Sent again it is the same frame, as no read comes between: the
         * page takes it once. */
        command_frame(l, l->op, x);
        l->cmd_gone = 1u;
        l->pending = X_CMD;
        return true;
    }
    if (l->fetching && !l->active) {
        fetch_next(l, x);
        return true;
    }
    const uint32_t every = l->active ? YGE_LINK_POLL_MS
                           : l->status[LINK_YP_STATE] != (uint16_t)LINK_YGE_FREE
                               ? YGE_LINK_HELD_MS : YGE_LINK_IDLE_MS;

    if (l->polled && (uint32_t)(now_ms - l->status_ms) < every) {
        return false;
    }
    return status_read(l, x);
}

/**
 * @brief A read of the page that failed: YGE_LINK_STATUS_FAILS in a row end a
 *        request, and over a hold that is an error and the STOP goes.
 * @param l      the module
 * @param result how it failed
 */
static void status_failed(yge_link_t *l, int result)
{
    if (!l->active || ++l->status_fails < YGE_LINK_STATUS_FAILS) {
        return;
    }
    /* Other pages may still answer, so no link loss ends this.  Over a hold
     * it is an error: no session runs on unwatched. */
    const bool held = l->status[LINK_YP_STATE] != (uint16_t)LINK_YGE_FREE;
    const bool silent = held || result == YGE_LINK_NO_ANSWER;

    l->status_fails = 0u;
    finish(l, silent ? YGE_LINK_OUT_NO_ANSWER : YGE_LINK_OUT_NACK,
           silent ? 0u : (uint8_t)result);
    if (held) {
        ++l->faults;
        yge_link_stop(l);
    }
}

/**
 * @brief How a frame ahead of the command or the command went.
 * @param l      the module
 * @param was    X_FRAME or X_CMD
 * @param result how it went
 */
static void request_done(yge_link_t *l, uint8_t was, int result)
{
    if (result == YGE_LINK_ACK) {
        l->tries = 0u;
        if (was == X_FRAME) {
            ++l->send_at;
        } else {
            l->verdict = 1u;
            l->poll_now = 1u;
        }
    } else if (result != YGE_LINK_NO_ANSWER) {
        finish(l, YGE_LINK_OUT_NACK, (uint8_t)result);
        if (was == X_CMD) {
            l->poll_now = 1u;
        }
    } else if (++l->tries >= YGE_LINK_RETRIES) {
        if (was == X_FRAME) {
            finish(l, YGE_LINK_OUT_NO_ANSWER, 0u);
        } else {
            l->doubt = 1u;
            l->poll_now = 1u;
        }
    }
}

/**
 * @brief How a STOP or ABORT went.
 * @param l      the module
 * @param result how it went
 */
static void urgent_done(yge_link_t *l, int result)
{
    if (result == YGE_LINK_ACK) {
        l->urgent = 0u;
        l->poll_now = 1u;
        if (l->active && l->doubt) {
            /* No read was answered since the command went, so this frame
             * carried the command's SEQ, and the page takes a SEQ once: it
             * never saw the command. */
            finish(l, YGE_LINK_OUT_DROPPED, 0u);
        } else if (l->active) {
            /* The page's SEQ is this one's now: a request running goes on
             * under it. */
            l->verdict = 0u;
            if (!l->taken) {
                l->taken = 1u;
                l->taken_ms = l->now_ms;
            }
        }
    } else if (result != YGE_LINK_NO_ANSWER) {
        /* Another writer moved the SEQ: read it, and again. */
        l->poll_now = 1u;
        if (++l->urgent_tries >= YGE_LINK_URGENT_TRIES) {
            l->urgent = 0u;
        }
    }
}

void yge_link_done(yge_link_t *l, int result, const uint16_t *regs)
{
    if (l == NULL) {
        return;
    }
    const uint8_t was = l->pending;

    l->pending = X_NONE;
    switch (was) {
    case X_STATUS:
        if (result == YGE_LINK_ACK && regs != NULL) {
            l->status_fails = 0u;
            l->urgent_read = 1u;
            absorb(l, regs);
            if (l->active && (l->verdict || l->doubt || l->taken)) {
                judge(l);
            }
        } else {
            status_failed(l, result);
        }
        break;
    case X_FRAME:
    case X_CMD:
        request_done(l, was, result);
        break;
    case X_URGENT:
        urgent_done(l, result);
        break;
    case X_BLOCK:
        if (result == YGE_LINK_ACK) {
            l->block_set = 1u;
            l->tries = 0u;
        } else if (result != YGE_LINK_NO_ANSWER
                   || ++l->tries >= YGE_LINK_RETRIES) {
            l->fetching = 0u;
            l->tries = 0u;
        }
        break;
    case X_DATA:
        if (result == YGE_LINK_ACK && regs != NULL) {
            l->tries = 0u;
            fetch_took(l, regs);
        } else if (result != YGE_LINK_NO_ANSWER
                   || ++l->tries >= YGE_LINK_RETRIES) {
            l->fetching = 0u;
            l->tries = 0u;
        }
        break;
    default:
        break;
    }
}

unsigned yge_link_service(yge_link_t *l, uint32_t now_ms, yge_link_xchg_fn xchg,
                          void *ctx)
{
    static uint16_t s_regs[LINK_MAX_REGS];
    unsigned made = 0u;
    yge_link_xfer_t x;

    if (xchg == NULL) {
        return 0u;
    }
    while (made < YGE_LINK_PASS_MAX && yge_link_next(l, now_ms, &x)) {
        const int r = xchg(ctx, &x, x.write ? NULL : s_regs);

        yge_link_done(l, r, (!x.write && r == YGE_LINK_ACK) ? s_regs : NULL);
        ++made;
        if (r == YGE_LINK_NO_ANSWER) {
            break;
        }
    }
    return made;
}

/* --- set-up and copies --------------------------------------------------- */

void yge_link_init(yge_link_t *l)
{
    if (l != NULL) {
        memset(l, 0, sizeof(*l));
        l->close_owed = 1u;
    }
}

void yge_link_lost(yge_link_t *l)
{
    if (l == NULL) {
        return;
    }
    if (l->active) {
        finish(l, YGE_LINK_OUT_NO_ANSWER, 0u);
    }
    l->up = 0u;
    l->has_page = 0u;
    l->minor = 0u;
    l->known = 0u;
    l->polled = 0u;
    l->poll_now = 0u;
    l->pending = X_NONE;
    l->ours = 0u;
    /* A stop owed stays: the first read after the link came up decides it. */
    l->urgent_read = 0u;
    l->fetching = 0u;
    l->fetch_given_up = 0u;
    l->close_timed = 0u;
    /* A session held over the gap is another's as far as this end knows. */
    l->image.gen = 0u;
}

void yge_link_came_up(yge_link_t *l, uint16_t minor)
{
    if (l == NULL) {
        return;
    }
    l->up = 1u;
    l->minor = minor;
    l->has_page = minor >= YGE_LINK_MINOR ? 1u : 0u;
    if (!l->has_page) {
        l->urgent = 0u;
    }
    l->known = 0u;
    l->polled = 0u;
    l->poll_now = 0u;
    l->pending = X_NONE;
}

bool yge_link_has_page(const yge_link_t *l)
{
    return l != NULL && l->up && l->has_page;
}

yge_link_phase_t yge_link_phase(const yge_link_t *l)
{
    return l != NULL ? phase_of(l) : YGE_LINK_DOWN;
}

bool yge_link_outcome(yge_link_t *l, yge_link_outcome_t *out)
{
    if (l == NULL || out == NULL || !l->outcome_new) {
        return false;
    }
    *out = l->outcome;
    l->outcome_new = 0u;
    return true;
}

void yge_link_readout(const yge_link_t *l, yge_link_readout_t *r)
{
    if (l == NULL || r == NULL) {
        return;
    }
    const uint16_t *s = l->status;

    memset(r, 0, sizeof(*r));
    r->phase = phase_of(l);
    r->minor = l->minor;
    r->known = l->known != 0u;
    r->gpio = 0xFFu;
    r->port = 0xFFu;
    r->writing = 0xFFu;
    if (!r->known) {
        return;
    }
    r->state = (uint8_t)s[LINK_YP_STATE];
    r->flags = s[LINK_YP_FLAGS];
    r->held = s[LINK_YP_STATE] != (uint16_t)LINK_YGE_FREE;
    r->ours = l->ours != 0u;
    r->port = (uint8_t)(s[LINK_YP_PORT] & 0xFFu);
    r->line = (uint8_t)(s[LINK_YP_LINE] & 0xFFu);
    r->gpio = (uint8_t)(s[LINK_YP_LINE] >> 8);
    r->op = (uint8_t)s[LINK_YP_OP];
    r->result = (uint8_t)(s[LINK_YP_RESULT] & 0xFFu);
    r->refusal = (uint8_t)(s[LINK_YP_RESULT] >> 8);
    r->ses = (uint8_t)(s[LINK_YP_SES] & 0xFFu);
    r->guard = (uint8_t)(s[LINK_YP_SES] >> 8);
    r->bad_index = s[LINK_YP_BAD_INDEX];
    r->type = s[LINK_YP_TYPE];
    r->firmware = (uint32_t)s[LINK_YP_FW_LO] | ((uint32_t)s[LINK_YP_FW_HI] << 16);
    r->serial = (uint32_t)s[LINK_YP_SERIAL_LO]
                | ((uint32_t)s[LINK_YP_SERIAL_HI] << 16);
    r->entries = s[LINK_YP_ENTRIES];
    r->reads = s[LINK_YP_READS];
    r->read_crc = s[LINK_YP_READ_CRC];
    r->seen = (uint8_t)(s[LINK_YP_PROGRESS] & 0xFFu);
    r->writing = (uint8_t)(s[LINK_YP_PROGRESS] >> 8);
    r->t_ms = s[LINK_YP_T_MS];
    r->ses_state = (uint8_t)(s[LINK_YP_SES_STATE] & 0xFFu);
    r->ro_why = (uint8_t)(s[LINK_YP_SES_STATE] >> 8);
    r->misses = s[LINK_YP_MISSES];
    r->dropped = s[LINK_YP_DROPPED];
}

const yge_link_image_t *yge_link_image(const yge_link_t *l)
{
    return l != NULL ? &l->image : NULL;
}

uint32_t yge_link_faults(const yge_link_t *l)
{
    return l != NULL ? l->faults : 0u;
}

bool yge_link_not_back(yge_link_t *l, uint8_t *port)
{
    if (l == NULL || port == NULL || !l->not_back) {
        return false;
    }
    *port = l->not_back_port;
    l->not_back = 0u;
    return true;
}
