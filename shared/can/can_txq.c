/**
 * @file
 * @brief A transmit queue for an MCP2515-type CAN controller; see can_txq.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "can_txq.h"

#include <string.h>

#include "mcp2515.h"

_Static_assert(CAN_TXQ_DEPTH <= 255u, "the ring's indices are bytes");
_Static_assert(CAN_TXQ_BUFFERS == MCP2515_TX_BUFFERS,
               "a batch fills the controller's transmit buffers");

void can_txq_init(can_txq_t *q, const can_txq_ops_t *ops)
{
    memset(q, 0, sizeof(*q));
    q->ops = ops;
}

/**
 * @brief Whether a frame can go on the wire as it is.
 *
 * @param f  The frame.
 * @return true for a DLC of 8 or less and an identifier of 29 bits.
 */
static bool frame_ok(const link_can_frame_t *f)
{
    return f->dlc <= 8u && f->id <= 0x1FFFFFFFu;
}

bool can_txq_put(can_txq_t *q, const link_can_frame_t *frames, size_t n)
{
    if (n == 0u) {
        return true;
    }
    bool ok = frames != NULL && n <= (size_t)(CAN_TXQ_DEPTH - q->count);
    for (size_t i = 0; ok && i < n; ++i) {
        ok = frame_ok(&frames[i]);
    }
    if (!ok) {
        q->dropped += (uint32_t)n;
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const unsigned at = ((unsigned)q->head + q->count) % CAN_TXQ_DEPTH;
        q->frames[at] = frames[i];
        ++q->count;
    }
    if (q->count > q->peak) {
        q->peak = q->count;
    }
    return true;
}

void can_txq_pump(can_txq_t *q, uint32_t now_us)
{
    if (q->count == 0u && q->on_wire == 0u) {
        return;
    }
    const uint8_t st = q->ops->status(q->ops->ctx);
    if ((st & MCP2515_STATUS_TXREQ) != 0u) {
        if (!q->timed) {
            /* A request this queue did not make: timed from now. */
            q->timed    = true;
            q->since_us = now_us;
            return;
        }
        if ((uint32_t)(now_us - q->since_us) < CAN_TXQ_STUCK_US) {
            return;
        }
        q->ops->abort(q->ops->ctx);
        /* The frames of the batch whose buffer was still requested at the
         * status read; the others have left.  None for a request this
         * queue did not make. */
        const unsigned held =
            ((st & MCP2515_STATUS_TX0REQ) != 0u ? 1u : 0u)
            + ((st & MCP2515_STATUS_TX1REQ) != 0u ? 1u : 0u)
            + ((st & MCP2515_STATUS_TX2REQ) != 0u ? 1u : 0u);
        q->dropped += (uint32_t)(held < q->on_wire ? held : q->on_wire)
                      + q->count;
        q->on_wire  = 0u;
        q->count    = 0u;
        q->head     = 0u;
        q->since_us = now_us;
        return;
    }
    q->on_wire = 0u;
    q->timed   = false;
    if (q->count == 0u) {
        return;
    }
    const uint8_t k = q->count < CAN_TXQ_BUFFERS ? q->count
                                                 : (uint8_t)CAN_TXQ_BUFFERS;
    uint8_t mask = 0u;
    for (uint8_t b = 0; b < k; ++b) {
        /* The oldest frame takes the highest priority, so the controller
         * sends the batch in queue order. */
        q->ops->load(q->ops->ctx, b, (uint8_t)(CAN_TXQ_BUFFERS - b),
                     &q->frames[q->head]);
        q->head = (uint8_t)((q->head + 1u) % CAN_TXQ_DEPTH);
        mask |= (uint8_t)(1u << b);
    }
    q->count = (uint8_t)(q->count - k);
    q->ops->request(q->ops->ctx, mask);
    q->on_wire  = k;
    q->timed    = true;
    q->since_us = now_us;
}

bool can_txq_busy(const can_txq_t *q)
{
    return q->count != 0u || q->on_wire != 0u;
}

uint8_t can_txq_take_peak(can_txq_t *q)
{
    const uint8_t p = q->peak;
    q->peak = q->count;
    return p;
}
