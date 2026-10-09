/*
 * SPDX-License-Identifier: MIT
 */

#include "link_port.h"

#include <string.h>

#include "link_can.h"

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
