/**
 * @file
 * @brief The panel's half of the ESC_WIRE and ESC_DATA pages.  See esc_link.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_link.h"

#include <stddef.h>
#include <string.h>

/* The exchange in flight. */
enum {
    X_NONE = 0,
    X_STATUS,  /* read ESC_WIRE's registers */
    X_FRAME,   /* a frame ahead of the command */
    X_CMD,     /* the command */
    X_URGENT,  /* STOP or ABORT */
    X_BLOCK,   /* the block a fetch reads next */
    X_DATA,    /* its registers */
};

/** @brief Registers of a write frame: one CAN frame. */
#define FRAME_REGS 4u
/** @brief Frames of a block: the choice of the block, and its registers. */
#define BLOCK_FRAMES (1u + LINK_ED_COUNT / FRAME_REGS)
/** @brief Frames of the staged registers: 4, 4 and 2. */
#define STAGE_FRAMES ((LINK_EW_W_STAGED + FRAME_REGS - 1u) / FRAME_REGS)
/** @brief A staged register of @p l by its write offset @p reg. */
#define SG(l, reg) ((l)->staged[(unsigned)(reg) - (unsigned)LINK_EW_W_TARGET])
/** @brief The blocks a fetch brings beyond the page: name, info, music. */
#define FETCH_MORE (1u + 2u + 4u)

_Static_assert(LINK_ED_COUNT % FRAME_REGS == 0u, "a block is whole frames");
_Static_assert(LINK_ED_NAME + 1u == LINK_ED_INFO
               && LINK_ED_INFO + 2u == LINK_ED_MUSIC
               && LINK_ED_MUSIC + 4u == LINK_ED_BACKUP, "the blocks of a fetch");

/* --- the status ---------------------------------------------------------- */

/**
 * @brief Whether the page runs something: an outcome waits for its end.
 * @param state a link_esc_state_t
 * @return true for the states that move by themselves
 */
static bool state_runs(uint16_t state)
{
    return state == (uint16_t)LINK_ESC_STOPPING
           || state == (uint16_t)LINK_ESC_POWER_OFF
           || state == (uint16_t)LINK_ESC_LEAD
           || state == (uint16_t)LINK_ESC_POWER_ON
           || state == (uint16_t)LINK_ESC_RUN;
}

/**
 * @brief Whether an operation has a draft ahead of its command.
 * @param op a link_esc_op_t
 * @return true for WRITE and RESTORE
 */
static bool op_keyed(uint8_t op)
{
    return op == (uint8_t)LINK_ESC_OP_WRITE
           || op == (uint8_t)LINK_ESC_OP_RESTORE;
}

/**
 * @brief Whether an operation has staged registers ahead of its command.
 * @param op a link_esc_op_t
 * @return true for OPEN, ENTER, WRITE and RESTORE
 */
static bool op_staged(uint8_t op)
{
    return op == (uint8_t)LINK_ESC_OP_OPEN || op == (uint8_t)LINK_ESC_OP_ENTER
           || op_keyed(op);
}

/**
 * @brief The request ends: its outcome, unless it is the CLOSE after a restart.
 * @param l    the module
 * @param kind how it went
 * @param nack a NACK's reason
 */
static void finish(esc_link_t *l, esc_link_outcome_kind_t kind, uint8_t nack)
{
    if (!l->internal) {
        esc_link_outcome_t *o = &l->outcome;
        const uint16_t *s = l->status;

        memset(o, 0, sizeof(*o));
        o->kind = kind;
        o->op = l->op;
        o->id = l->id;
        o->nack = nack;
        o->taken = l->taken != 0u;
        o->state = (uint8_t)s[LINK_EW_STATE];
        o->ses = (uint8_t)(s[LINK_EW_SES] & 0xFFu);
        o->guard = (uint8_t)(s[LINK_EW_SES] >> 8);
        o->result = (uint8_t)(s[LINK_EW_RESULT] & 0xFFu);
        o->refusal = (uint8_t)(s[LINK_EW_RESULT] >> 8);
        l->outcome_new = 1u;
    }
    if (l->internal) {
        l->close_timed = 1u;
        l->close_at = l->now_ms + ESC_LINK_CLOSE_RETRY_MS;
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
 * @brief What a read of the page says: errors counted, and the fetch owed.
 * @param l    the module
 * @param regs the registers
 */
static void absorb(esc_link_t *l, const uint16_t *regs)
{
    const bool first = !l->known;
    const uint16_t was_state = first ? (uint16_t)LINK_ESC_FREE
                                     : l->status[LINK_EW_STATE];
    const uint16_t was_flags = l->status[LINK_EW_FLAGS];
    const uint16_t state = regs[LINK_EW_STATE];
    const uint16_t flags = regs[LINK_EW_FLAGS];

    if (!first && (flags & LINK_EW_F_FAULT) != 0u
        && (was_flags & LINK_EW_F_FAULT) == 0u) {
        ++l->faults;
    }
    if (state == (uint16_t)LINK_ESC_NOT_BACK
        && was_state != (uint16_t)LINK_ESC_NOT_BACK) {
        ++l->faults;
        l->not_back = 1u;
        l->not_back_target = regs[LINK_EW_TARGET];
    }
    memcpy(l->status, regs, sizeof(l->status));
    l->known = 1u;
    if (state == (uint16_t)LINK_ESC_FREE) {
        l->ours = 0u;
        l->close_owed = 0u;
    }
    if ((flags & LINK_EW_F_HAVE_READ) == 0u) {
        l->fetching = 0u;
        l->fetch_given_up = 0u;
        return;
    }
    if (state == (uint16_t)LINK_ESC_READY && !l->fetching
        && (l->image.gen == 0u || l->image.reads != regs[LINK_EW_READS]
            || l->image.read_crc != regs[LINK_EW_READ_CRC])
        && (!l->fetch_given_up || l->fetch_reads != regs[LINK_EW_READS]
            || l->fetch_crc != regs[LINK_EW_READ_CRC])) {
        l->fetching = 1u;
        l->fetch_at = 0u;
        l->block_set = 0u;
        l->fetch_tries = 0u;
        l->fetch_given_up = 0u;
        l->fetch_reads = regs[LINK_EW_READS];
        l->fetch_crc = regs[LINK_EW_READ_CRC];
    }
}

/**
 * @brief What a status says of the request.
 * @param l the module
 */
static void judge(esc_link_t *l)
{
    const uint16_t *s = l->status;
    const uint16_t refusal = (uint16_t)(s[LINK_EW_RESULT] >> 8);

    if (l->verdict || l->doubt) {
        const bool took = s[LINK_EW_SEQ] == l->last_seq;

        if (!took) {
            /* Unanswered, and no sign of it: nothing ran.  Acknowledged, and
             * the page has another SEQ: it started again, or another writer. */
            finish(l, l->doubt ? ESC_LINK_OUT_NO_ANSWER
                               : ESC_LINK_OUT_RESTARTED, 0u);
            return;
        }
        l->verdict = 0u;
        l->doubt = 0u;
        l->taken = 1u;
        l->taken_ms = l->now_ms;
        if (refusal != (uint16_t)LINK_ESC_REF_NONE) {
            finish(l, ESC_LINK_OUT_REFUSED, 0u);
            return;
        }
        if (l->op == (uint8_t)LINK_ESC_OP_OPEN) {
            l->ours = 1u;
        }
    } else if (s[LINK_EW_SEQ] != l->last_seq) {
        finish(l, ESC_LINK_OUT_RESTARTED, 0u);
        return;
    }
    if (!state_runs(s[LINK_EW_STATE])) {
        finish(l, ESC_LINK_OUT_DONE, 0u);
    } else if (!l->limit_cut
               && (uint32_t)(l->now_ms - l->taken_ms) >= ESC_LINK_RUN_MAX_MS) {
        l->limit_cut = 1u;
        esc_link_abort(l);
    }
}

/* --- requests ------------------------------------------------------------ */

/**
 * @brief The phase as esc_link_phase() gives it.
 * @param l the module
 * @return the phase
 */
static esc_link_phase_t phase_of(const esc_link_t *l)
{
    if (!l->up) {
        return ESC_LINK_DOWN;
    }
    if (!l->has_page) {
        return ESC_LINK_NO_PAGE;
    }
    if (!l->known || l->close_owed) {
        return ESC_LINK_SYNC;
    }
    if (l->active) {
        return l->taken || l->verdict ? ESC_LINK_RUNNING : ESC_LINK_SENDING;
    }
    return l->fetching ? ESC_LINK_FETCHING : ESC_LINK_READY;
}

/**
 * @brief A request taken: everything ahead of its command is set up by the
 *        caller afterwards.
 * @param l  the module
 * @param op the operation
 * @param id the caller's number
 * @return ESC_LINK_START_OK, or why not
 */
static esc_link_start_t begin(esc_link_t *l, link_esc_op_t op, uint32_t id)
{
    if (l == NULL) {
        return ESC_LINK_START_ARG;
    }
    const esc_link_phase_t ph = phase_of(l);

    if (ph == ESC_LINK_DOWN) {
        return ESC_LINK_START_DOWN;
    }
    if (ph == ESC_LINK_NO_PAGE) {
        return ESC_LINK_START_NO_PAGE;
    }
    if (ph != ESC_LINK_READY || l->pending != X_NONE || l->urgent != 0u) {
        return ESC_LINK_START_BUSY;
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
    return ESC_LINK_START_OK;
}

esc_link_start_t esc_link_open(esc_link_t *l, uint16_t target, uint32_t id)
{
    const esc_link_start_t r = begin(l, LINK_ESC_OP_OPEN, id);

    if (r == ESC_LINK_START_OK) {
        memset(l->staged, 0, sizeof(l->staged));
        SG(l, LINK_EW_W_TARGET) = target;
    }
    return r;
}

esc_link_start_t esc_link_enter(esc_link_t *l, const esc_link_power_t *p,
                                uint32_t id)
{
    if (p == NULL) {
        return ESC_LINK_START_ARG;
    }
    const esc_link_start_t r = begin(l, LINK_ESC_OP_ENTER, id);

    if (r == ESC_LINK_START_OK) {
        memset(l->staged, 0, sizeof(l->staged));
        SG(l, LINK_EW_W_TARGET) = l->status[LINK_EW_TARGET];
        SG(l, LINK_EW_W_POWER) = p->power;
        SG(l, LINK_EW_W_MV) = p->mv;
        SG(l, LINK_EW_W_MA) = p->ma;
        SG(l, LINK_EW_W_HOLD_LO) = (uint16_t)(p->hold_ms & 0xFFFFu);
        SG(l, LINK_EW_W_HOLD_HI) = (uint16_t)(p->hold_ms >> 16);
        SG(l, LINK_EW_W_OFF_MS) = p->off_ms;
    }
    return r;
}

esc_link_start_t esc_link_powered(esc_link_t *l, uint32_t id)
{
    return begin(l, LINK_ESC_OP_DONE, id);
}

esc_link_start_t esc_link_read(esc_link_t *l, uint32_t id)
{
    return begin(l, LINK_ESC_OP_READ, id);
}

/**
 * @brief What WRITE and RESTORE share: the draft and the staged registers.
 * @param l        the module
 * @param op       the operation
 * @param draft    the draft
 * @param len      its bytes
 * @param reads    LINK_EW_READS
 * @param read_crc LINK_EW_READ_CRC
 * @param id       the caller's number
 * @return ESC_LINK_START_OK, or why not
 */
static esc_link_start_t keyed(esc_link_t *l, link_esc_op_t op,
                              const uint8_t *draft, uint16_t len, uint16_t reads,
                              uint16_t read_crc, uint32_t id)
{
    if (draft == NULL || len == 0u || len > ESC_LINK_PAGE_MAX) {
        return ESC_LINK_START_ARG;
    }
    const esc_link_start_t r = begin(l, op, id);

    if (r == ESC_LINK_START_OK) {
        memset(l->draft, 0, sizeof(l->draft));
        memcpy(l->draft, draft, len);
        l->draft_len = len;
        memset(l->staged, 0, sizeof(l->staged));
        SG(l, LINK_EW_W_READS) = reads;
        SG(l, LINK_EW_W_READ_CRC) = read_crc;
        SG(l, LINK_EW_W_DRAFT_CRC) = link_crc(LINK_CRC_INIT, draft, len);
    }
    return r;
}

esc_link_start_t esc_link_write(esc_link_t *l, const uint8_t *draft,
                                uint16_t len, uint16_t reads,
                                uint16_t read_crc, uint32_t id)
{
    return keyed(l, LINK_ESC_OP_WRITE, draft, len, reads, read_crc, id);
}

esc_link_start_t esc_link_restore(esc_link_t *l, const uint8_t *draft,
                                  uint16_t len, const uint8_t *identity,
                                  uint16_t reads, uint16_t read_crc,
                                  uint32_t id)
{
    if (identity == NULL) {
        return ESC_LINK_START_ARG;
    }
    const esc_link_start_t r =
        keyed(l, LINK_ESC_OP_RESTORE, draft, len, reads, read_crc, id);

    if (r == ESC_LINK_START_OK) {
        memcpy(l->backup, identity, sizeof(l->backup));
    }
    return r;
}

esc_link_start_t esc_link_leave(esc_link_t *l, uint32_t id)
{
    return begin(l, LINK_ESC_OP_LEAVE, id);
}

esc_link_start_t esc_link_clear(esc_link_t *l, uint32_t id)
{
    return begin(l, LINK_ESC_OP_CLEAR, id);
}

esc_link_start_t esc_link_close(esc_link_t *l, uint32_t id)
{
    return begin(l, LINK_ESC_OP_CLOSE, id);
}

/**
 * @brief STOP or ABORT owed: a request not gone yet is dropped.
 * @param l  the module
 * @param op the operation
 */
static void ask_urgent(esc_link_t *l, link_esc_op_t op)
{
    if (l == NULL || !l->up || !l->has_page) {
        return;
    }
    if (l->active && !l->cmd_gone && !l->taken) {
        finish(l, ESC_LINK_OUT_DROPPED, 0u);
    }
    /* ABORT is the stronger one: a STOP owed does not hold it back. */
    if (l->urgent != (uint8_t)LINK_ESC_OP_ABORT) {
        l->urgent = (uint8_t)op;
    }
    l->urgent_tries = 0u;
}

void esc_link_stop(esc_link_t *l)
{
    ask_urgent(l, LINK_ESC_OP_STOP);
}

void esc_link_abort(esc_link_t *l)
{
    ask_urgent(l, LINK_ESC_OP_ABORT);
}

/* --- the exchanges ------------------------------------------------------- */

/**
 * @brief A command frame with the next SEQ.
 * @param l  the module
 * @param op the operation
 * @param x  takes the frame
 */
static void command_frame(esc_link_t *l, uint8_t op, esc_link_xfer_t *x)
{
    x->write = true;
    x->page = LINK_PAGE_ESC_WIRE;
    x->off = 0u;
    x->n = (uint8_t)LINK_EW_W_CMD_FRAME;
    x->regs[LINK_EW_W_CMD] = op;
    x->regs[LINK_EW_W_SEQ] = (uint16_t)(l->status[LINK_EW_SEQ] + 1u);
    x->regs[LINK_EW_W_KEY] = op_keyed(op) ? LINK_ESC_WRITE_KEY : 0u;
    x->regs[LINK_EW_W_CRC] = op_staged(op)
                                 ? link_kst_crc(l->staged, LINK_EW_W_STAGED) : 0u;
    l->last_seq = x->regs[LINK_EW_W_SEQ];
}

/**
 * @brief A frame of a block.
 * @param x     takes the frame
 * @param block the block
 * @param bytes its bytes
 * @param at    the frame of the block: 0 chooses it
 */
static void block_frame(esc_link_xfer_t *x, uint16_t block, const uint8_t *bytes,
                        unsigned at)
{
    x->write = true;
    if (at == 0u) {
        x->page = LINK_PAGE_ESC_WIRE;
        x->off = (uint8_t)LINK_EW_W_BLOCK;
        x->n = 1u;
        x->regs[0] = block;
        return;
    }
    const unsigned first = (at - 1u) * FRAME_REGS;

    x->page = LINK_PAGE_ESC_DATA;
    x->off = (uint8_t)first;
    x->n = (uint8_t)FRAME_REGS;
    for (unsigned i = 0u; i < FRAME_REGS; ++i) {
        const unsigned b = (first + i) * 2u;

        x->regs[i] = (uint16_t)(bytes[b] | ((uint16_t)bytes[b + 1u] << 8));
    }
}

/**
 * @brief The frame @c send_at of the request ahead of its command.
 * @param l the module
 * @param x takes it
 * @return false when none is left: the command is next
 */
static bool frame_at(const esc_link_t *l, esc_link_xfer_t *x)
{
    unsigned at = l->send_at;

    if (l->op == (uint8_t)LINK_ESC_OP_RESTORE) {
        if (at < BLOCK_FRAMES) {
            block_frame(x, LINK_ED_BACKUP, l->backup, at);
            return true;
        }
        at -= BLOCK_FRAMES;
    }
    if (op_keyed(l->op)) {
        const unsigned blocks = ((unsigned)l->draft_len + LINK_ED_BLOCK_BYTES - 1u)
                                / LINK_ED_BLOCK_BYTES;

        if (at < blocks * BLOCK_FRAMES) {
            const unsigned b = at / BLOCK_FRAMES;

            block_frame(x, (uint16_t)(LINK_ED_DRAFT + b),
                        &l->draft[(size_t)b * LINK_ED_BLOCK_BYTES], at % BLOCK_FRAMES);
            return true;
        }
        at -= blocks * BLOCK_FRAMES;
    }
    if (op_staged(l->op) && at < STAGE_FRAMES) {
        const unsigned first = at * FRAME_REGS;
        const unsigned n = LINK_EW_W_STAGED - first < FRAME_REGS
                               ? LINK_EW_W_STAGED - first : FRAME_REGS;

        x->write = true;
        x->page = LINK_PAGE_ESC_WIRE;
        x->off = (uint8_t)(LINK_EW_W_TARGET + first);
        x->n = (uint8_t)n;
        memcpy(x->regs, &l->staged[first], n * sizeof(x->regs[0]));
        return true;
    }
    return false;
}

/**
 * @brief The blocks of the page, as LINK_EW_LEN says, 4 at most.
 * @param l the module
 * @return the blocks
 */
static unsigned page_blocks(const esc_link_t *l)
{
    const unsigned n = ((unsigned)l->status[LINK_EW_LEN]
                        + LINK_ED_BLOCK_BYTES - 1u) / LINK_ED_BLOCK_BYTES;

    return n < 4u ? n : 4u;
}

/**
 * @brief The block a fetch reads at @p at.
 * @param l  the module
 * @param at the blocks done
 * @return the block
 */
static uint16_t fetch_block(const esc_link_t *l, unsigned at)
{
    const unsigned page = page_blocks(l);

    return at < page ? (uint16_t)(LINK_ED_READ + at)
                     : (uint16_t)(LINK_ED_NAME + (at - page));
}

/**
 * @brief The next exchange of a fetch.
 * @param l the module
 * @param x takes it
 */
static void fetch_next(esc_link_t *l, esc_link_xfer_t *x)
{
    if (!l->block_set) {
        block_frame(x, fetch_block(l, l->fetch_at), NULL, 0u);
        l->pending = X_BLOCK;
    } else {
        x->write = false;
        x->page = LINK_PAGE_ESC_DATA;
        x->off = 0u;
        x->n = (uint8_t)LINK_ED_COUNT;
        l->pending = X_DATA;
    }
}

/**
 * @brief A block read: into the copy, and at the last one the check.
 * @param l    the module
 * @param regs its registers
 */
static void fetch_took(esc_link_t *l, const uint16_t *regs)
{
    esc_link_image_t *im = &l->image;
    const uint16_t block = fetch_block(l, l->fetch_at);
    uint8_t *to = NULL;

    if (block < (uint16_t)LINK_ED_DRAFT) {
        to = &im->page[(size_t)block * LINK_ED_BLOCK_BYTES];
    } else if (block == (uint16_t)LINK_ED_NAME) {
        to = (uint8_t *)im->name;
    } else if (block < (uint16_t)LINK_ED_MUSIC) {
        to = &im->info[(size_t)(block - LINK_ED_INFO) * LINK_ED_BLOCK_BYTES];
    } else {
        to = (uint8_t *)&im->music[(size_t)(block - LINK_ED_MUSIC)
                                   * LINK_ED_BLOCK_BYTES];
    }
    for (unsigned i = 0u; i < LINK_ED_COUNT; ++i) {
        to[(size_t)i * 2u] = (uint8_t)(regs[i] & 0xFFu);
        to[(size_t)i * 2u + 1u] = (uint8_t)(regs[i] >> 8);
    }
    ++l->fetch_at;
    l->block_set = 0u;
    if (l->fetch_at < page_blocks(l) + FETCH_MORE) {
        return;
    }
    /* The whole copy, as the page says it is. */
    const uint16_t len = l->status[LINK_EW_LEN] <= ESC_LINK_PAGE_MAX
                             ? l->status[LINK_EW_LEN] : 0u;

    im->name[sizeof(im->name) - 1u] = '\0';
    im->music[sizeof(im->music) - 1u] = '\0';
    l->fetching = 0u;
    if (link_crc(LINK_CRC_INIT, im->page, len) == l->fetch_crc) {
        im->reads = l->fetch_reads;
        im->read_crc = l->fetch_crc;
        im->len = len;
        ++im->gen;
        if (im->gen == 0u) {
            im->gen = 1u;
        }
    } else if (++l->fetch_tries < ESC_LINK_FETCH_TRIES) {
        l->fetching = 1u;
        l->fetch_at = 0u;
    } else {
        l->fetch_given_up = 1u;
    }
}

/**
 * @brief A read of ESC_WIRE's registers.
 * @param l the module
 * @param x takes it
 * @return true
 */
static bool status_read(esc_link_t *l, esc_link_xfer_t *x)
{
    x->write = false;
    x->page = LINK_PAGE_ESC_WIRE;
    x->off = 0u;
    x->n = (uint8_t)LINK_EW_COUNT;
    l->pending = X_STATUS;
    l->polled = 1u;
    l->status_ms = l->now_ms;
    l->poll_now = 0u;
    return true;
}

bool esc_link_next(esc_link_t *l, uint32_t now_ms, esc_link_xfer_t *x)
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
        if (l->status[LINK_EW_STATE] == (uint16_t)LINK_ESC_FREE) {
            l->urgent = 0u;
        } else {
            command_frame(l, l->urgent, x);
            l->pending = X_URGENT;
            return true;
        }
    }
    /* The pin nobody here knows of, after a panel restart. */
    if (l->close_owed && !l->active
        && (!l->close_timed || (int32_t)(now_ms - l->close_at) >= 0)) {
        l->active = 1u;
        l->internal = 1u;
        l->op = (uint8_t)LINK_ESC_OP_CLOSE;
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
        /* Sent again it is the same frame, as no read comes between: the page
         * takes it once. */
        command_frame(l, l->op, x);
        l->cmd_gone = 1u;
        l->pending = X_CMD;
        return true;
    }
    if (l->fetching && !l->active) {
        fetch_next(l, x);
        return true;
    }
    const uint32_t every = l->active ? ESC_LINK_POLL_MS
                           : l->status[LINK_EW_STATE] != (uint16_t)LINK_ESC_FREE
                               ? ESC_LINK_HELD_MS : ESC_LINK_IDLE_MS;

    if (l->polled && (uint32_t)(now_ms - l->status_ms) < every) {
        return false;
    }
    return status_read(l, x);
}

void esc_link_done(esc_link_t *l, int result, const uint16_t *regs)
{
    if (l == NULL) {
        return;
    }
    const uint8_t was = l->pending;

    l->pending = X_NONE;
    switch (was) {
    case X_STATUS:
        if (result == ESC_LINK_ACK && regs != NULL) {
            absorb(l, regs);
            if (l->active && (l->verdict || l->doubt || l->taken)) {
                judge(l);
            }
        }
        break;
    case X_FRAME:
        if (result == ESC_LINK_ACK) {
            ++l->send_at;
            l->tries = 0u;
        } else if (result != ESC_LINK_NO_ANSWER) {
            finish(l, ESC_LINK_OUT_NACK, (uint8_t)result);
        } else if (++l->tries >= ESC_LINK_RETRIES) {
            finish(l, ESC_LINK_OUT_NO_ANSWER, 0u);
        }
        break;
    case X_CMD:
        if (result == ESC_LINK_ACK) {
            l->verdict = 1u;
            l->tries = 0u;
            l->poll_now = 1u;
        } else if (result != ESC_LINK_NO_ANSWER) {
            finish(l, ESC_LINK_OUT_NACK, (uint8_t)result);
            l->poll_now = 1u;
        } else if (++l->tries >= ESC_LINK_RETRIES) {
            l->doubt = 1u;
            l->poll_now = 1u;
        }
        break;
    case X_URGENT:
        if (result == ESC_LINK_ACK) {
            l->urgent = 0u;
            l->poll_now = 1u;
            /* The page's SEQ is this one's now: a request running goes on
             * under it. */
            if (l->active) {
                l->verdict = 0u;
                l->doubt = 0u;
                l->taken = 1u;
            }
        } else if (result != ESC_LINK_NO_ANSWER) {
            /* Another writer moved the SEQ: read it, and again. */
            l->poll_now = 1u;
            if (++l->urgent_tries >= ESC_LINK_URGENT_TRIES) {
                l->urgent = 0u;
            }
        }
        break;
    case X_BLOCK:
        if (result == ESC_LINK_ACK) {
            l->block_set = 1u;
            l->tries = 0u;
        } else if (result != ESC_LINK_NO_ANSWER
                   || ++l->tries >= ESC_LINK_RETRIES) {
            l->fetching = 0u;
            l->tries = 0u;
        }
        break;
    case X_DATA:
        if (result == ESC_LINK_ACK && regs != NULL) {
            l->tries = 0u;
            fetch_took(l, regs);
        } else if (result != ESC_LINK_NO_ANSWER
                   || ++l->tries >= ESC_LINK_RETRIES) {
            l->fetching = 0u;
            l->tries = 0u;
        }
        break;
    default:
        break;
    }
}

unsigned esc_link_service(esc_link_t *l, uint32_t now_ms, esc_link_xchg_fn xchg,
                          void *ctx)
{
    static uint16_t s_regs[LINK_MAX_REGS];
    unsigned made = 0u;
    esc_link_xfer_t x;

    if (xchg == NULL) {
        return 0u;
    }
    while (made < ESC_LINK_PASS_MAX && esc_link_next(l, now_ms, &x)) {
        const int r = xchg(ctx, &x, x.write ? NULL : s_regs);

        esc_link_done(l, r, (!x.write && r == ESC_LINK_ACK) ? s_regs : NULL);
        ++made;
        if (r == ESC_LINK_NO_ANSWER) {
            break;
        }
    }
    return made;
}

/* --- set-up and copies --------------------------------------------------- */

void esc_link_init(esc_link_t *l)
{
    if (l != NULL) {
        memset(l, 0, sizeof(*l));
        l->close_owed = 1u;
    }
}

void esc_link_lost(esc_link_t *l)
{
    if (l == NULL) {
        return;
    }
    if (l->active) {
        finish(l, ESC_LINK_OUT_NO_ANSWER, 0u);
    }
    l->up = 0u;
    l->has_page = 0u;
    l->minor = 0u;
    l->known = 0u;
    l->polled = 0u;
    l->poll_now = 0u;
    l->pending = X_NONE;
    l->ours = 0u;
    l->urgent = 0u;
    l->fetching = 0u;
    l->fetch_given_up = 0u;
    l->close_timed = 0u;
    /* A session held over the gap is another's as far as this end knows. */
    l->image.gen = 0u;
}

void esc_link_came_up(esc_link_t *l, uint16_t minor)
{
    if (l == NULL) {
        return;
    }
    l->up = 1u;
    l->minor = minor;
    l->has_page = minor >= ESC_LINK_MINOR ? 1u : 0u;
    l->known = 0u;
    l->polled = 0u;
    l->poll_now = 0u;
    l->pending = X_NONE;
}

bool esc_link_has_page(const esc_link_t *l)
{
    return l != NULL && l->up && l->has_page;
}

esc_link_phase_t esc_link_phase(const esc_link_t *l)
{
    return l != NULL ? phase_of(l) : ESC_LINK_DOWN;
}

bool esc_link_outcome(esc_link_t *l, esc_link_outcome_t *out)
{
    if (l == NULL || out == NULL || !l->outcome_new) {
        return false;
    }
    *out = l->outcome;
    l->outcome_new = 0u;
    return true;
}

void esc_link_readout(const esc_link_t *l, esc_link_readout_t *r)
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
    if (!r->known) {
        return;
    }
    r->state = (uint8_t)s[LINK_EW_STATE];
    r->flags = s[LINK_EW_FLAGS];
    r->held = s[LINK_EW_STATE] != (uint16_t)LINK_ESC_FREE;
    r->ours = l->ours != 0u;
    r->target = s[LINK_EW_TARGET];
    r->line = (uint8_t)(s[LINK_EW_LINE] & 0xFFu);
    r->gpio = (uint8_t)(s[LINK_EW_LINE] >> 8);
    r->op = (uint8_t)s[LINK_EW_OP];
    r->result = (uint8_t)(s[LINK_EW_RESULT] & 0xFFu);
    r->refusal = (uint8_t)(s[LINK_EW_RESULT] >> 8);
    r->ses = (uint8_t)(s[LINK_EW_SES] & 0xFFu);
    r->guard = (uint8_t)(s[LINK_EW_SES] >> 8);
    r->fw = (uint8_t)(s[LINK_EW_ID] & 0xFFu);
    r->layout = (uint8_t)(s[LINK_EW_ID] >> 8);
    r->why = (uint8_t)(s[LINK_EW_WHY] & 0xFFu);
    r->cpu = (uint8_t)(s[LINK_EW_WHY] >> 8);
    r->signature = s[LINK_EW_SIGNATURE];
    r->len = s[LINK_EW_LEN];
    r->bad_off = s[LINK_EW_BAD_OFF];
    r->clipped = s[LINK_EW_CLIPPED];
    r->tries = s[LINK_EW_TRIES];
    r->reads = s[LINK_EW_READS];
    r->read_crc = s[LINK_EW_READ_CRC];
    r->t_ms = s[LINK_EW_T_MS];
    r->pin_code = (uint8_t)(s[LINK_EW_CONTACT] & 0xFFu);
    r->flash_code = (uint8_t)(s[LINK_EW_CONTACT] >> 8);
    r->protocol = (uint8_t)s[LINK_EW_PROTOCOL];
}

const esc_link_image_t *esc_link_image(const esc_link_t *l)
{
    return l != NULL ? &l->image : NULL;
}

uint32_t esc_link_faults(const esc_link_t *l)
{
    return l != NULL ? l->faults : 0u;
}

bool esc_link_not_back(esc_link_t *l, uint16_t *target)
{
    if (l == NULL || target == NULL || !l->not_back) {
        return false;
    }
    *target = l->not_back_target;
    l->not_back = 0u;
    return true;
}
