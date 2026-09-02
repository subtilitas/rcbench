/*
 * SPDX-License-Identifier: MIT
 */

#include "link_dev.h"

#include <string.h>

/*
 * Wrap-safe: `(uint32_t)(now - then) >= timeout` holds across the 32-bit
 * millisecond wrap at 49.7 days; `now >= then + timeout` does not.
 */
static bool elapsed(uint32_t now, uint32_t then, uint32_t ms)
{
    return (uint32_t)(now - then) >= ms;
}

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
}

void link_dev_clear_failsafe(link_dev_t *d, uint32_t now_ms)
{
    if (d == NULL) {
        return;
    }
    d->failsafe        = false;
    d->silent          = false;
    d->last_request_ms = now_ms;
}

static void refuse(link_msg_t *reply, const link_msg_t *req, link_nack_t why)
{
    memset(reply, 0, sizeof(*reply));
    reply->op      = LINK_OP_NACK;
    reply->page    = req->page;
    reply->offset  = req->offset;
    reply->count   = 1;
    reply->regs[0] = (uint16_t)why;
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
     * takes an explicit clear, or a bench re-arms itself.
     */
    d->last_request_ms = now_ms;
    d->silent          = false;
    ++d->requests;

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
     * coprocessor's own vocabulary; one arriving here means another node is
     * transmitting on the coprocessor's behalf. */
    refuse(reply, req, LINK_NACK_BAD_VALUE);
    return true;
}


bool link_dev_tick(link_dev_t *d, uint32_t now_ms)
{
    if (d == NULL || d->silent) {
        return false;
    }
    if (!elapsed(now_ms, d->last_request_ms, LINK_DEV_SILENCE_MS)) {
        return false;
    }
    d->silent   = true;
    d->failsafe = true;
    return true;   /* the edge, once, not every tick after it */
}
