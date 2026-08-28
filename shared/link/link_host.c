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

static bool start(link_host_t *h, uint8_t op, uint8_t page, uint8_t offset,
                  uint8_t count, const uint16_t *regs, uint32_t now_ms,
                  link_msg_t *out)
{
    if (h == NULL || out == NULL || h->pending) {
        return false;
    }
    if (count == 0 || count > LINK_MAX_REGS
        || (uint16_t)offset + (uint16_t)count > LINK_MAX_REGS) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->op     = op;
    out->page   = page;
    out->offset = offset;
    out->count  = count;
    if (regs != NULL) {
        memcpy(out->regs, regs, (size_t)count * sizeof(regs[0]));
    }

    h->pending  = true;
    h->op       = op;
    h->page     = page;
    h->offset   = offset;
    h->count    = count;
    h->acc_seen = 0;
    memset(h->acc, 0, sizeof(h->acc));
    h->sent_ms  = now_ms;   /* this request's own timeout runs from here */
    ++h->polls;
    return true;
}

bool link_host_read(link_host_t *h, uint8_t page, uint8_t offset,
                    uint8_t count, uint32_t now_ms, link_msg_t *out)
{
    return start(h, LINK_OP_READ, page, offset, count, NULL, now_ms, out);
}

bool link_host_write(link_host_t *h, uint8_t page, uint8_t offset,
                     uint8_t count, const uint16_t *regs, uint32_t now_ms,
                     link_msg_t *out)
{
    if (regs == NULL) {
        return false;
    }
    return start(h, LINK_OP_WRITE, page, offset, count, regs, now_ms, out);
}

/* Everything in the window has arrived. */
static bool window_complete(const link_host_t *h)
{
    const uint32_t want = (h->count >= 32u) ? 0xFFFFFFFFu
                                            : (((uint32_t)1u << h->count) - 1u);
    return (h->acc_seen & want) == want;
}

static void answered(link_host_t *h, uint32_t now_ms, bool refused)
{
    h->pending       = false;
    h->last_reply_ms = now_ms;
    h->escalated     = false;
    ++h->replies;
    if (refused) {
        ++h->nacks;
    }
}

bool link_host_accept(link_host_t *h, const link_msg_t *part, uint32_t now_ms,
                      link_msg_t *whole)
{
    if (h == NULL || part == NULL || whole == NULL || !h->pending) {
        if (h != NULL) {
            ++h->mismatches;
        }
        return false;
    }

    /* A NACK answers the request it refuses and carries its own offset back;
     * DATA and ACK have to fall inside the window that was asked for. */
    const bool answers_read  = (h->op == LINK_OP_READ
                                && part->op == LINK_OP_DATA);
    const bool answers_write = (h->op == LINK_OP_WRITE
                                && part->op == LINK_OP_ACK);
    const bool refuses       = (part->op == LINK_OP_NACK);

    if ((!answers_read && !answers_write && !refuses)
        || part->page != h->page) {
        ++h->mismatches;
        return false;
    }

    if (refuses) {
        if (part->offset != h->offset) {
            ++h->mismatches;
            return false;
        }
        *whole = *part;
        answered(h, now_ms, true);
        return true;
    }

    if (answers_write) {
        if (part->offset != h->offset || part->count != h->count) {
            ++h->mismatches;
            return false;
        }
        *whole = *part;
        answered(h, now_ms, false);
        return true;
    }

    /*
     * A read's answer may be in pieces.  Each says where it belongs, so the
     * only questions are whether it belongs to this window at all and whether
     * the window is now full.
     */
    if (part->count == 0 || part->offset < h->offset
        || (uint16_t)part->offset + (uint16_t)part->count
               > (uint16_t)h->offset + (uint16_t)h->count) {
        ++h->mismatches;
        return false;
    }

    const uint8_t at = (uint8_t)(part->offset - h->offset);
    for (uint8_t i = 0; i < part->count; ++i) {
        h->acc[at + i] = part->regs[i];
        h->acc_seen |= (uint32_t)1u << (at + i);
    }
    if (!window_complete(h)) {
        return false;   /* not a fault: more of it is still coming */
    }

    memset(whole, 0, sizeof(*whole));
    whole->op     = LINK_OP_DATA;
    whole->page   = h->page;
    whole->offset = h->offset;
    whole->count  = h->count;
    memcpy(whole->regs, h->acc, (size_t)h->count * sizeof(h->acc[0]));
    answered(h, now_ms, false);
    return true;
}

bool link_host_tick(link_host_t *h, uint32_t now_ms)
{
    if (h == NULL) {
        return false;
    }

    bool acted = false;

    /*
     * Abandon a request that has been outstanding too long -- every time, not
     * only the first.  This used to be gated on `escalated`, which is set once
     * and never cleared without a reply, so the second unanswered request
     * stayed pending for ever: the poller could never ask again, and the
     * panel's poll loop -- whose only exit is this function returning true --
     * spun with no render, no touch and no heartbeat.
     */
    if (h->pending && elapsed(now_ms, h->sent_ms, LINK_HOST_TIMEOUT_MS)) {
        h->pending = false;
        ++h->timeouts;
        acted = true;
    }

    /*
     * And say the link is down, once.  That is a different fact on a different
     * clock: a request times out from when it was sent, the link is reported
     * down when nothing has been answered for a second.
     */
    if (!h->escalated
        && elapsed(now_ms, h->last_reply_ms, LINK_HOST_TIMEOUT_MS)) {
        h->escalated = true;
        acted = true;
    }
    return acted;
}

void link_host_abandon(link_host_t *h)
{
    if (h == NULL || !h->pending) {
        return;
    }
    h->pending = false;
    ++h->timeouts;
}
