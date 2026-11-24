/**
 * @file
 * @brief The IOmcu end of the link on a UART: receive ring, transmit queue
 * and the answer to a request frame; see link_uart_dev.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_uart_dev.h"

#include <string.h>

_Static_assert((LINK_UART_RING_BYTES & (LINK_UART_RING_BYTES - 1u)) == 0u,
               "the ring is a power of two");
_Static_assert((LINK_UART_TXQ_BYTES & (LINK_UART_TXQ_BYTES - 1u)) == 0u,
               "the queue is a power of two");
_Static_assert(LINK_UART_DMA_RELOAD % LINK_UART_RING_BYTES == 0u,
               "a trigger ends where the ring starts");
_Static_assert(LINK_UART_DMA_RELOAD <= 0x0FFFFFFFu,
               "the RP2350's transfer count has 28 bits");
_Static_assert(LINK_UART_TXQ_BYTES >= 3u * LINK_CAN_MAX_FRAMES
                                          * LINK_UART_WIRE_MAX,
               "the queue holds three replies of 8 frames");

bool link_uart_ring_init(link_uart_ring_t *r, const volatile uint8_t *ring,
                         uint32_t reload)
{
    if (r == NULL || ring == NULL || reload == 0u
        || reload % LINK_UART_RING_BYTES != 0u) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    r->ring = ring;
    r->reload = reload;
    link_uart_rx_init(&r->rx);
    /* As after a 0x00: a line that is idle when the DMA starts needs none,
     * so the node's first request is taken.  A start in the middle of a
     * frame is one refused frame at its 0x00. */
    link_can_frame_t none;
    (void)link_uart_feed(&r->rx, LINK_UART_DELIM, &none, NULL);
    return true;
}

void link_uart_ring_dma(link_uart_ring_t *r, uint32_t remaining)
{
    if (r == NULL) {
        return;
    }
    if (remaining > r->reload) {
        remaining = r->reload;
    }
    const uint32_t done = r->reload - remaining;
    /* Fewer done than at the last look: the channel ran to its end and
     * started again, so the rest of the old trigger counts as well. */
    const uint32_t step = (done >= r->done) ? done - r->done
                                            : (r->reload - r->done) + done;
    r->head += step;
    r->done = done;
}

bool link_uart_ring_next(link_uart_ring_t *r, link_can_frame_t *out,
                         uint32_t *refused)
{
    if (r == NULL || out == NULL) {
        return false;
    }
    const uint32_t keep = LINK_UART_RING_BYTES - LINK_UART_RING_SLACK;
    if (r->head - r->tail > keep) {
        /* Bytes before head - keep may be overwritten while they are read:
         * the ones behind them are lost already.  The frame under way is
         * dropped up to the next 0x00; frames after it in what is kept
         * are taken. */
        ++r->laps;
        r->tail = r->head - keep;
        link_uart_rx_drop(&r->rx);
    }
    while (r->tail != r->head) {
        const uint8_t b = r->ring[r->tail & (LINK_UART_RING_BYTES - 1u)];
        ++r->tail;
        if (link_uart_feed(&r->rx, b, out, refused)) {
            return true;
        }
    }
    return false;
}

void link_uart_txq_init(link_uart_txq_t *q)
{
    if (q != NULL) {
        memset(q, 0, sizeof(*q));
    }
}

bool link_uart_txq_put(link_uart_txq_t *q, const link_can_frame_t *frames,
                       size_t n)
{
    if (q == NULL || frames == NULL || n == 0u || n > LINK_CAN_MAX_FRAMES) {
        return false;
    }
    uint8_t wire[LINK_CAN_MAX_FRAMES * LINK_UART_WIRE_MAX];
    size_t len = 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t k = link_uart_encode(&frames[i], &wire[len],
                                          sizeof(wire) - len);
        if (k == 0u) {
            ++q->dropped;
            return false;
        }
        len += k;
    }
    const size_t held = (size_t)(q->head - q->tail);
    if (len > LINK_UART_TXQ_BYTES - held) {
        ++q->dropped;
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        q->buf[(q->head + i) & (LINK_UART_TXQ_BYTES - 1u)] = wire[i];
    }
    q->head += (uint32_t)len;
    if (held + len > q->peak) {
        q->peak = (uint16_t)(held + len);
    }
    return true;
}

bool link_uart_txq_get(link_uart_txq_t *q, uint8_t *byte)
{
    if (q == NULL || byte == NULL || q->tail == q->head) {
        return false;
    }
    *byte = q->buf[q->tail & (LINK_UART_TXQ_BYTES - 1u)];
    ++q->tail;
    return true;
}

size_t link_uart_txq_held(const link_uart_txq_t *q)
{
    return (q == NULL) ? 0u : (size_t)(q->head - q->tail);
}

uint16_t link_uart_txq_take_peak(link_uart_txq_t *q)
{
    if (q == NULL) {
        return 0u;
    }
    const uint16_t p = q->peak;
    q->peak = (uint16_t)(q->head - q->tail);
    return p;
}

size_t link_uart_dev_answer(link_dev_t *dev, const link_can_frame_t *in,
                            uint32_t now_ms, uint32_t *refused,
                            uint32_t *served, link_can_frame_t *out)
{
    if (dev == NULL || in == NULL || out == NULL) {
        return 0u;
    }
    link_msg_t req;
    link_msg_t reply;
    if (!link_can_decode_counted(in, &req, refused)) {
        return 0u;
    }
    if (served != NULL) {
        ++*served;
    }
    if (!link_dev_dispatch(dev, &req, &reply, now_ms)) {
        return 0u;
    }
    return link_can_encode(&reply, out, LINK_CAN_MAX_FRAMES);
}
