/**
 * @file
 * @brief Acknowledged write and read sequences over a link port; see
 *        link_port.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_port.h"

#include <string.h>

#include "link_can.h"

/**
 * @brief Check that a link exchange has everything it needs.
 *
 * @param h The host state.
 * @param port The transport and clock.
 * @param reply Where the answer goes.
 * @return true when h, port, port->exchange, port->now_ms and reply are all
 *       non-NULL.
 */
static bool usable(const link_host_t *h, const link_port_t *port,
                   const link_msg_t *reply)
{
    return h != NULL && port != NULL && port->exchange != NULL
           && port->now_ms != NULL && reply != NULL;
}

bool link_write_acked(link_host_t *h, const link_port_t *port, uint8_t page,
                      uint8_t offset, uint8_t count, const uint16_t *regs,
                      link_msg_t *reply)
{
    if (!usable(h, port, reply) || regs == NULL) {
        return false;
    }
    if (count == 0u || count > LINK_MAX_REGS
        || (uint16_t)offset + (uint16_t)count > LINK_MAX_REGS) {
        return false;
    }

    link_msg_t whole;
    memset(&whole, 0, sizeof(whole));
    whole.op     = LINK_OP_ACK;
    whole.page   = page;
    whole.offset = offset;
    whole.count  = count;

    for (uint8_t at = 0; at < count; ) {
        uint8_t n = (uint8_t)(count - at);
        if (n > LINK_CAN_REGS_PER_FRAME) {
            n = LINK_CAN_REGS_PER_FRAME;
        }
        link_msg_t req, part;
        if (!link_host_write(h, page, (uint8_t)(offset + at), n, &regs[at],
                             port->now_ms(port->ctx), &req)) {
            return false;
        }
        if (!port->exchange(port->ctx, h, &req, &part)) {
            return false;
        }
        if (part.op != LINK_OP_ACK) {
            /* A refusal ends the write where it was refused; the frames
             * after it are not sent. */
            *reply = part;
            return true;
        }
        /* An acknowledgement carries what the far end stored. */
        memcpy(&whole.regs[at], part.regs, (size_t)n * sizeof(part.regs[0]));
        at = (uint8_t)(at + n);
        if (at < count && port->between != NULL) {
            port->between(port->ctx);
        }
    }
    *reply = whole;
    return true;
}

bool link_read_window(link_host_t *h, const link_port_t *port, uint8_t page,
                      uint8_t offset, uint8_t count, link_msg_t *reply)
{
    if (!usable(h, port, reply)) {
        return false;
    }
    link_msg_t req;
    if (!link_host_read(h, page, offset, count, port->now_ms(port->ctx),
                        &req)) {
        return false;
    }
    return port->exchange(port->ctx, h, &req, reply);
}

/**
 * @brief One part of a split read.
 *
 * @param h The host state.
 * @param port The transport and clock.
 * @param page The page.
 * @param offset Its first register.
 * @param count Its registers.
 * @param part Receives the answer.
 * @return 1 for data of @p count registers, 0 for any other answer, -1 for
 *         none.
 */
static int read_part(link_host_t *h, const link_port_t *port, uint8_t page,
                     uint8_t offset, uint8_t count, link_msg_t *part)
{
    if (!link_read_window(h, port, page, offset, count, part)) {
        return -1;
    }
    return (part->op == LINK_OP_DATA && part->count >= count) ? 1 : 0;
}

bool link_read_split(link_host_t *h, const link_port_t *port, uint8_t page,
                     uint8_t offset, uint8_t count, link_msg_t *reply)
{
    if (count <= LINK_READ_REGS_MAX) {
        return link_read_window(h, port, page, offset, count, reply);
    }
    if (!usable(h, port, reply) || count > LINK_MAX_REGS) {
        return false;
    }
    const uint8_t rest = (uint8_t)(count - LINK_READ_REGS_MAX);
    link_msg_t first, second, again;

    for (unsigned t = 0u; t < LINK_READ_TRIES; ++t) {
        const link_msg_t *const parts[3] = { &first, &second, &again };
        const uint8_t offs[3] = { offset, (uint8_t)(offset + LINK_READ_REGS_MAX),
                                  offset };
        const uint8_t ns[3] = { (uint8_t)LINK_READ_REGS_MAX, rest,
                                (uint8_t)LINK_READ_REGS_MAX };

        for (unsigned k = 0u; k < 3u; ++k) {
            if (k > 0u && port->between != NULL) {
                port->between(port->ctx);
            }
            const int got = read_part(h, port, page, offs[k], ns[k],
                                      (link_msg_t *)parts[k]);
            if (got < 0) {
                return false;
            }
            if (got == 0) {
                /* A refusal, or an answer short of the part: as it came. */
                *reply = *parts[k];
                return true;
            }
        }
        if (memcmp(first.regs, again.regs,
                   LINK_READ_REGS_MAX * sizeof(first.regs[0])) == 0) {
            *reply = first;
            reply->offset = offset;
            reply->count = count;
            memcpy(&reply->regs[LINK_READ_REGS_MAX], second.regs,
                   (size_t)rest * sizeof(second.regs[0]));
            return true;
        }
        if (port->between != NULL) {
            port->between(port->ctx);
        }
    }
    memset(reply, 0, sizeof(*reply));
    reply->op = LINK_OP_NACK;
    reply->page = page;
    reply->offset = offset;
    reply->count = 1u;
    reply->regs[0] = (uint16_t)LINK_NACK_BUSY;
    return true;
}
