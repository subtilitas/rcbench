#include "link_host.h"

#include <string.h>

static bool elapsed(uint32_t now, uint32_t then, uint32_t ms)
{
    return (uint32_t)(now - then) >= ms;
}

void link_host_init(link_host_t *h, uint32_t now_ms)
{
    if (h == NULL) {
        return;
    }
    memset(h, 0, sizeof(*h));
    h->last_reply_ms = now_ms;
}

static size_t start(link_host_t *h, uint8_t op, uint8_t page, uint8_t offset,
                    uint8_t count, const uint16_t *regs,
                    uint8_t *out, size_t cap)
{
    if (h == NULL || out == NULL || h->pending) {
        return 0;
    }

    link_msg_t req;
    memset(&req, 0, sizeof(req));
    req.op     = op;
    req.page   = page;
    req.offset = offset;
    req.count  = count;
    if (regs != NULL && count <= LINK_MAX_REGS) {
        memcpy(req.regs, regs, (size_t)count * sizeof(uint16_t));
    }

    const size_t n = link_encode(out, cap, &req);
    if (n == 0) {
        return 0;   /* malformed or too big: no request was started */
    }

    h->pending = true;
    h->op      = op;
    h->page    = page;
    h->offset  = offset;
    h->count   = count;
    ++h->polls;
    return n;
}

size_t link_host_read(link_host_t *h, uint8_t page, uint8_t offset,
                      uint8_t count, uint8_t *out, size_t cap)
{
    return start(h, LINK_OP_READ, page, offset, count, NULL, out, cap);
}

size_t link_host_write(link_host_t *h, uint8_t page, uint8_t offset,
                       uint8_t count, const uint16_t *regs,
                       uint8_t *out, size_t cap)
{
    if (regs == NULL) {
        return 0;
    }
    return start(h, LINK_OP_WRITE, page, offset, count, regs, out, cap);
}

bool link_host_reply(link_host_t *h, const link_msg_t *reply, uint32_t now_ms)
{
    if (h == NULL || reply == NULL || !h->pending) {
        if (h != NULL) {
            ++h->mismatches;
        }
        return false;
    }

    /* A NACK answers the request it refuses, and carries its own offset back;
     * DATA and ACK have to match the window that was asked for. */
    const bool answers_read  = (h->op == LINK_OP_READ
                                && reply->op == LINK_OP_DATA);
    const bool answers_write = (h->op == LINK_OP_WRITE
                                && reply->op == LINK_OP_ACK);
    const bool refuses       = (reply->op == LINK_OP_NACK);

    if (!answers_read && !answers_write && !refuses) {
        ++h->mismatches;
        return false;
    }
    if (reply->page != h->page || reply->offset != h->offset) {
        ++h->mismatches;
        return false;
    }
    if (!refuses && reply->count != h->count) {
        ++h->mismatches;
        return false;
    }

    h->pending       = false;
    h->last_reply_ms = now_ms;
    h->escalated     = false;
    ++h->replies;
    if (refuses) {
        ++h->nacks;
    }
    return true;
}

bool link_host_tick(link_host_t *h, uint32_t now_ms)
{
    if (h == NULL || h->escalated) {
        return false;
    }
    if (!elapsed(now_ms, h->last_reply_ms, LINK_HOST_TIMEOUT_MS)) {
        return false;
    }
    h->escalated = true;
    /* The request in flight is abandoned here rather than left outstanding
     * forever: without this the host can never poll again, and a link that
     * recovers would find a panel that had stopped asking. */
    if (h->pending) {
        h->pending = false;
        ++h->timeouts;
    }
    return true;
}
