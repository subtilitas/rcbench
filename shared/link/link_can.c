/*
 * SPDX-License-Identifier: MIT
 */

#include "link_can.h"

#include <string.h>

#include "link_pages.h"

static bool op_is_known(uint8_t op)
{
    switch (op) {
    case LINK_OP_READ:
    case LINK_OP_WRITE:
    case LINK_OP_DATA:
    case LINK_OP_ACK:
    case LINK_OP_NACK:
        return true;
    default:
        return false;
    }
}

/* A READ says how many registers it wants and carries none of them; every
 * other op carries the registers it is about.  A property of the protocol,
 * not of the transport. */
static bool op_carries_payload(uint8_t op)
{
    return op != (uint8_t)LINK_OP_READ;
}

link_can_prio_t link_can_priority(uint8_t page, uint8_t op)
{
    (void)op;
    /*
     * By page, not by op.  A NACK (negative acknowledge) of a control write
     * is as urgent as the write: it is the news that the bench did not do
     * what it was told, and must not queue behind telemetry.
     */
    if (page == LINK_PAGE_CONTROL || page == LINK_PAGE_FAILSAFE
        || page == LINK_PAGE_LIMITS) {
        return LINK_CAN_PRIO_CONTROL;
    }
    return LINK_CAN_PRIO_NORMAL;
}

uint32_t link_can_id(link_can_prio_t prio, uint8_t op, uint8_t page,
                     uint8_t offset, uint8_t count)
{
    return (((uint32_t)prio & LINK_CAN_PRIO_MASK) << LINK_CAN_PRIO_SHIFT)
           | (((uint32_t)op & LINK_CAN_OP_MASK) << LINK_CAN_OP_SHIFT)
           | (((uint32_t)page & LINK_CAN_PAGE_MASK) << LINK_CAN_PAGE_SHIFT)
           | (((uint32_t)offset & LINK_CAN_OFFSET_MASK)
              << LINK_CAN_OFFSET_SHIFT)
           | (((uint32_t)count & LINK_CAN_COUNT_MASK) << LINK_CAN_COUNT_SHIFT);
}

void link_can_id_split(uint32_t id, link_can_prio_t *prio, uint8_t *op,
                       uint8_t *page, uint8_t *offset, uint8_t *count)
{
    if (prio != NULL) {
        *prio = (link_can_prio_t)((id >> LINK_CAN_PRIO_SHIFT)
                                  & LINK_CAN_PRIO_MASK);
    }
    if (op != NULL) {
        *op = (uint8_t)((id >> LINK_CAN_OP_SHIFT) & LINK_CAN_OP_MASK);
    }
    if (page != NULL) {
        *page = (uint8_t)((id >> LINK_CAN_PAGE_SHIFT) & LINK_CAN_PAGE_MASK);
    }
    if (offset != NULL) {
        *offset = (uint8_t)((id >> LINK_CAN_OFFSET_SHIFT)
                            & LINK_CAN_OFFSET_MASK);
    }
    if (count != NULL) {
        *count = (uint8_t)((id >> LINK_CAN_COUNT_SHIFT) & LINK_CAN_COUNT_MASK);
    }
}

size_t link_can_encode(const link_msg_t *msg, link_can_frame_t *out, size_t cap)
{
    if (msg == NULL || out == NULL) {
        return 0;
    }
    if (!op_is_known(msg->op) || msg->count > LINK_MAX_REGS) {
        return 0;
    }
    /* A page is LINK_MAX_REGS wide, so a transfer starting inside one and
     * running past its end is a caller bug rather than a wire condition. */
    if ((size_t)msg->offset + (size_t)msg->count > LINK_MAX_REGS) {
        return 0;
    }

    const link_can_prio_t prio = link_can_priority(msg->page, msg->op);

    if (!op_carries_payload(msg->op)) {
        /* The whole question fits in the identifier. */
        if (cap < 1) {
            return 0;
        }
        memset(&out[0], 0, sizeof(out[0]));
        out[0].id = link_can_id(prio, msg->op, msg->page, msg->offset,
                                msg->count);
        out[0].dlc = 0;
        return 1;
    }

    /* An ACK (acknowledge) carries nothing; a NACK carries its reason in
     * regs[0].  Both are one frame, and count says so. */
    const size_t frames = (msg->count == 0)
                              ? 1u
                              : (((size_t)msg->count
                                  + LINK_CAN_REGS_PER_FRAME - 1u)
                                 / LINK_CAN_REGS_PER_FRAME);
    if (frames > cap) {
        return 0;
    }

    for (size_t i = 0; i < frames; ++i) {
        const size_t first = i * LINK_CAN_REGS_PER_FRAME;
        size_t here = (size_t)msg->count - first;
        if (here > LINK_CAN_REGS_PER_FRAME) {
            here = LINK_CAN_REGS_PER_FRAME;
        }
        memset(&out[i], 0, sizeof(out[i]));
        out[i].id = link_can_id(prio, msg->op, msg->page,
                                (uint8_t)(msg->offset + first),
                                (uint8_t)here);
        out[i].dlc = (uint8_t)(here * 2u);
        for (size_t r = 0; r < here; ++r) {
            const uint16_t v = msg->regs[first + r];
            out[i].data[2 * r]      = (uint8_t)(v & 0xFFu);
            out[i].data[2 * r + 1u] = (uint8_t)(v >> 8);
        }
    }
    return frames;
}

bool link_can_decode(const link_can_frame_t *f, link_msg_t *out)
{
    if (f == NULL || out == NULL || f->dlc > 8) {
        return false;
    }

    uint8_t op = 0, page = 0, offset = 0, count = 0;
    link_can_id_split(f->id, NULL, &op, &page, &offset, &count);

    if (!op_is_known(op)) {
        return false;
    }
    if (count > LINK_MAX_REGS
        || (size_t)offset + (size_t)count > LINK_MAX_REGS) {
        return false;
    }

    if (op_carries_payload(op)) {
        /* One frame carries at most four registers, and the payload has to
         * be exactly the length the identifier claims.  A frame whose DLC
         * (data length code) and count disagree is a version mismatch or a
         * bug at the far end, and is not acted on. */
        if (count > LINK_CAN_REGS_PER_FRAME
            || f->dlc != (uint8_t)(count * 2u)) {
            return false;
        }
    } else if (f->dlc != 0) {
        return false;   /* a question with an answer attached */
    }

    memset(out, 0, sizeof(*out));
    out->op     = op;
    out->page   = page;
    out->offset = offset;
    out->count  = count;
    for (size_t r = 0; r < count && op_carries_payload(op); ++r) {
        out->regs[r] = (uint16_t)((uint16_t)f->data[2 * r]
                                  | ((uint16_t)f->data[2 * r + 1u] << 8));
    }
    return true;
}
