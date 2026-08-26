#include "link_dev.h"

#include <string.h>

/*
 * Wrap-safe throughout: `(uint32_t)(now - then) >= timeout` is correct across
 * the 32-bit millisecond wrap, and `now >= then + timeout` is not.  A bench
 * that fails to fail safe after 49 days of uptime would be a memorable bug.
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

static size_t refuse(uint8_t *out, size_t cap, const link_msg_t *req,
                     link_nack_t why)
{
    link_msg_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.op      = LINK_OP_NACK;
    reply.page    = req->page;
    reply.offset  = req->offset;
    reply.count   = 1;
    reply.regs[0] = (uint16_t)why;
    return link_encode(out, cap, &reply);
}

size_t link_dev_handle(link_dev_t *d, const link_msg_t *req,
                       uint8_t *out, size_t cap, uint32_t now_ms)
{
    if (d == NULL || req == NULL || out == NULL) {
        return 0;
    }

    /*
     * A request that arrives is proof the host is alive, so it stops the
     * silence counter -- but it does not lift the latch.  Those are different
     * facts and conflating them is how a bench re-arms itself.
     */
    d->last_request_ms = now_ms;
    d->silent          = false;
    ++d->requests;

    const link_page_t *p = find_page(d, req->page);
    if (p == NULL) {
        return refuse(out, cap, req, LINK_NACK_BAD_PAGE);
    }
    /* The frame layer already refused anything wider than a page; this is the
     * narrower question of whether it fits *this* page. */
    if (req->count == 0
        || (uint16_t)req->offset + (uint16_t)req->count > p->count) {
        return refuse(out, cap, req, LINK_NACK_BAD_RANGE);
    }

    link_msg_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.page   = req->page;
    reply.offset = req->offset;
    reply.count  = req->count;

    if (req->op == LINK_OP_READ) {
        reply.op = LINK_OP_DATA;
        p->read(d->ctx, req->offset, req->count, reply.regs);
        return link_encode(out, cap, &reply);
    }

    if (req->op == LINK_OP_WRITE) {
        if (p->write == NULL) {
            return refuse(out, cap, req, LINK_NACK_READ_ONLY);
        }
        const uint8_t why = p->write(d->ctx, req->offset, req->count,
                                     req->regs);
        if (why != 0) {
            return refuse(out, cap, req, (link_nack_t)why);
        }
        reply.op    = LINK_OP_ACK;
        reply.count = req->count;
        /* An ACK echoes what was stored rather than what was sent.  A value
         * that was clamped on the way in is still accepted, and the host has
         * to be able to see that it was clamped. */
        p->read(d->ctx, req->offset, req->count, reply.regs);
        return link_encode(out, cap, &reply);
    }

    /* DATA, ACK and NACK are the coprocessor's own vocabulary.  Receiving one
     * means something is talking that should be listening. */
    return refuse(out, cap, req, LINK_NACK_BAD_VALUE);
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
    return true;   /* the edge, once -- not every tick after it */
}
