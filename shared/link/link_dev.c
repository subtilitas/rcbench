/**
 * @file
 * @brief The IOmcu end of the link: page dispatch, the arm latch and the
 *        failsafe tick.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_dev.h"

#include <string.h>

/**
 * @brief Wrap-safe: `(uint32_t)(now - then) >= timeout` holds across the 32-bit
 * millisecond wrap at 49.7 days; `now >= then + timeout` does not.
 *
 * @param now The current time, in ms.
 * @param then The time the wait started, in ms.
 * @param ms The timeout, in ms.
 * @return true when at least ms milliseconds have passed since then.
 */
static bool elapsed(uint32_t now, uint32_t then, uint32_t ms)
{
    return (uint32_t)(now - then) >= ms;
}

/**
 * @brief Find the page with a given number.
 *
 * @param d The link state holding the page table.
 * @param page The page number to find.
 * @return The table entry, or NULL when the table has no such page.
 */
static const link_page_t *find_page(const link_dev_t *d, uint8_t page)
{
    for (uint8_t i = 0; i < d->page_count; ++i) {
        if (d->pages[i].page == page) {
            return &d->pages[i];
        }
    }
    return NULL;
}

void link_dev_init(link_dev_t *d, const link_page_t *pages, uint8_t page_count,
                   void *ctx, uint32_t now_ms)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    d->pages           = pages;
    d->page_count      = page_count;
    d->ctx             = ctx;
    d->last_request_ms = now_ms;
    /* A start is not a silence, so `failsafe` stays clear; the latch is what
     * keeps an ARM written by a host that never saw the start from arming. */
    d->arm_latched     = true;
}

void link_dev_latch_arm(link_dev_t *d)
{
    if (d != NULL) {
        d->arm_latched = true;
    }
}

bool link_dev_arm_latched(const link_dev_t *d)
{
    return d == NULL || d->arm_latched;
}

void link_dev_clear_failsafe(link_dev_t *d, uint32_t now_ms)
{
    if (d == NULL) {
        return;
    }
    d->failsafe        = false;
    d->arm_latched     = false;
    d->silent          = false;
    d->last_request_ms = now_ms;
}

/**
 * @brief Build a NACK for a request, with its reason in regs[0].
 *
 * @param reply Receives the NACK; cleared first.
 * @param req The request being refused; its page and offset are echoed.
 * @param why The reason, sent as regs[0].
 */
static void refuse(link_msg_t *reply, const link_msg_t *req, link_nack_t why)
{
    memset(reply, 0, sizeof(*reply));
    reply->op      = LINK_OP_NACK;
    reply->page    = req->page;
    reply->offset  = req->offset;
    reply->count   = 1;
    reply->regs[0] = (uint16_t)why;
}

/**
 * @brief Answer a read of LINK_READ_REGS_MAX registers and keep the registers
 *        after them, read in the same page call.
 *
 * @param d The link state; `held` takes the rest.
 * @param req A READ of LINK_READ_REGS_MAX registers.
 * @param reply Receives the DATA.
 * @return false when the page is unknown, the window does not fit it or no
 *       register follows it: link_dev_apply() then answers as for any read.
 */
static bool read_and_keep(link_dev_t *d, const link_msg_t *req,
                          link_msg_t *reply)
{
    const link_page_t *p = find_page(d, req->page);
    const uint16_t end = (uint16_t)req->offset + LINK_READ_REGS_MAX;
    if (p == NULL || end >= p->count) {
        return false;
    }
    uint16_t n = (uint16_t)(p->count - req->offset);
    if (n > LINK_MAX_REGS) {
        n = LINK_MAX_REGS;
    }
    /* The whole window into the reply's registers, the rest moved out. */
    memset(reply, 0, sizeof(*reply));
    p->read(d->ctx, req->offset, (uint8_t)n, reply->regs);
    reply->op     = LINK_OP_DATA;
    reply->page   = req->page;
    reply->offset = req->offset;
    reply->count  = (uint8_t)LINK_READ_REGS_MAX;

    d->held_page   = req->page;
    d->held_offset = (uint8_t)end;
    d->held_count  = (uint8_t)(n - LINK_READ_REGS_MAX);
    memcpy(d->held, &reply->regs[LINK_READ_REGS_MAX],
           (size_t)d->held_count * sizeof(d->held[0]));
    memset(&reply->regs[LINK_READ_REGS_MAX], 0,
           (size_t)d->held_count * sizeof(d->held[0]));
    return true;
}

/**
 * @brief Answer a LINK_OP_READ_HELD from what read_and_keep() kept, once.
 *
 * @param d The link state; `held` is spent by an answer with DATA.
 * @param req The READ_HELD.
 * @param reply Receives the DATA, or a NACK of LINK_NACK_BUSY when nothing is
 *        kept for that page and offset, or of LINK_NACK_BAD_RANGE when the
 *        count is 0 or more than is kept.
 */
static void serve_held(link_dev_t *d, const link_msg_t *req,
                       link_msg_t *reply)
{
    if (d->held_count == 0u || req->page != d->held_page
        || req->offset != d->held_offset) {
        refuse(reply, req, LINK_NACK_BUSY);
        return;
    }
    if (req->count == 0u || req->count > d->held_count) {
        refuse(reply, req, LINK_NACK_BAD_RANGE);
        return;
    }
    memset(reply, 0, sizeof(*reply));
    reply->op     = LINK_OP_DATA;
    reply->page   = req->page;
    reply->offset = req->offset;
    reply->count  = req->count;
    memcpy(reply->regs, d->held, (size_t)req->count * sizeof(d->held[0]));
    d->held_count = 0u;
}

/**
 * @brief Whether two requests are the same message: op, window and, for a
 *        write, the values.
 *
 * @param a One request.
 * @param b The other.
 * @return true when a repeat of @p a would be @p b.
 */
static bool same_request(const link_msg_t *a, const link_msg_t *b)
{
    if (a->op != b->op || a->page != b->page || a->offset != b->offset
        || a->count != b->count || a->seq != b->seq) {
        return false;
    }
    if (a->op != LINK_OP_WRITE) {
        return true;   /* a read carries no values */
    }
    return memcmp(a->regs, b->regs, (size_t)a->count * sizeof(a->regs[0]))
           == 0;
}

/**
 * @brief The kept answer of a repeat, or NULL when @p req is not one.
 *
 * @param d The link state.
 * @param req A numbered request.
 * @param now_ms The current time, in ms.
 * @return The kept answer, or NULL when the request starts or continues an
 *         exchange.
 */
static const link_msg_t *repeat_of(link_dev_t *d, const link_msg_t *req,
                                   uint32_t now_ms)
{
    if (req->seq != d->seq || elapsed(now_ms, d->seq_ms, LINK_DEV_REPEAT_MS)) {
        /* A new exchange: what the last one kept is no answer to it. */
        d->seq        = req->seq;
        d->seq_ms     = now_ms;
        d->kept_count = 0u;
        return NULL;
    }
    for (uint8_t i = 0; i < d->kept_count; ++i) {
        if (same_request(&d->kept[i].req, req)) {
            return &d->kept[i].reply;
        }
    }
    return NULL;   /* another message of the same exchange */
}

/**
 * @brief Answer a request that is not a repeat: READ_HELD, a read that keeps
 *        the rest, or link_dev_apply().
 *
 * @param d The link state.
 * @param req The request.
 * @param reply Receives the answer.
 */
static void answer(link_dev_t *d, const link_msg_t *req, link_msg_t *reply)
{
    if (req->op == LINK_OP_READ_HELD) {
        serve_held(d, req, reply);
        return;
    }
    if (req->op == LINK_OP_READ && req->count == LINK_READ_REGS_MAX
        && read_and_keep(d, req, reply)) {
        return;
    }
    (void)link_dev_apply(d, req, reply);
}

bool link_dev_dispatch(link_dev_t *d, const link_msg_t *req,
                       link_msg_t *reply, uint32_t now_ms)
{
    if (d == NULL || req == NULL || reply == NULL) {
        return false;
    }

    /*
     * A request that arrives proves the host is alive, so it stops the
     * silence counter.  It does not lift the failsafe latch: leaving failsafe
     * takes an explicit clear, or a bench re-arms itself.  A repeat proves
     * the same.
     */
    d->last_request_ms = now_ms;
    d->silent          = false;
    d->heard           = true;
    ++d->requests;

    if (req->seq == 0u) {
        /* Unnumbered: a panel older than 4.37, or one that has not read
         * this end's minor yet.  Nothing it sends is a repeat. */
        d->seq        = 0u;
        d->kept_count = 0u;
    } else {
        /*
         * A repeat is the panel asking again for an answer it did not hear.
         * The page acted on the first one, so the answer is the one that
         * went out then: a write taken twice would arm twice, step a
         * programming session twice or append a block twice.
         */
        const link_msg_t *again = repeat_of(d, req, now_ms);
        if (again != NULL) {
            *reply = *again;
            ++d->repeats;
            return true;
        }
    }

    /*
     * One call of answer(), so the compiler takes it and the page calls into
     * this function: tools/stack_iomcu.py follows the page table from here.
     */
    answer(d, req, reply);
    if (req->seq == 0u) {
        return true;
    }
    reply->seq = req->seq;
    if (d->kept_count < LINK_DEV_REPEAT_KEEP) {
        d->kept[d->kept_count].req   = *req;
        d->kept[d->kept_count].reply = *reply;
        ++d->kept_count;
    }
    return true;
}

bool link_dev_apply(const link_dev_t *d, const link_msg_t *req,
                    link_msg_t *reply)
{
    if (d == NULL || req == NULL || reply == NULL) {
        return false;
    }

    const link_page_t *p = find_page(d, req->page);
    if (p == NULL) {
        refuse(reply, req, LINK_NACK_BAD_PAGE);
        return true;
    }
    /* The frame layer already refused anything wider than a page; this is the
     * narrower question of whether it fits *this* page. */
    if (req->count == 0
        || (uint16_t)req->offset + (uint16_t)req->count > p->count) {
        refuse(reply, req, LINK_NACK_BAD_RANGE);
        return true;
    }

    memset(reply, 0, sizeof(*reply));
    reply->page   = req->page;
    reply->offset = req->offset;
    reply->count  = req->count;

    if (req->op == LINK_OP_READ) {
        reply->op = LINK_OP_DATA;
        p->read(d->ctx, req->offset, req->count, reply->regs);
        return true;
    }

    if (req->op == LINK_OP_WRITE) {
        if (p->write == NULL) {
            refuse(reply, req, LINK_NACK_READ_ONLY);
            return true;
        }
        const uint8_t why = p->write(d->ctx, req->offset, req->count,
                                     req->regs);
        if (why != 0) {
            refuse(reply, req, (link_nack_t)why);
            return true;
        }
        reply->op    = LINK_OP_ACK;
        reply->count = req->count;
        /* An ACK echoes what was stored rather than what was sent.  A value
         * that was clamped on the way in is still accepted, and the host has
         * to be able to see that it was clamped. */
        p->read(d->ctx, req->offset, req->count, reply->regs);
        return true;
    }

    /* DATA, ACK (acknowledge) and NACK (negative acknowledge) are the
     * IOmcu's own vocabulary; one arriving here means another node is
     * transmitting on the IOmcu's behalf.  READ_HELD needs what
     * link_dev_dispatch() keeps, and nothing is kept for this path. */
    refuse(reply, req, LINK_NACK_BAD_VALUE);
    return true;
}


bool link_dev_tick(link_dev_t *d, uint32_t now_ms)
{
    if (d == NULL || d->silent) {
        return false;
    }
    /*
     * Nothing has ever been said, so nothing has gone quiet.  See `heard`:
     * this end boots first, and the wait for the panel's first request is
     * not a fault.
     */
    if (!d->heard) {
        return false;
    }
    if (!elapsed(now_ms, d->last_request_ms, LINK_DEV_SILENCE_MS)) {
        return false;
    }
    d->silent      = true;
    d->failsafe    = true;
    d->arm_latched = true;
    return true;   /* the edge, once, not every tick after it */
}
