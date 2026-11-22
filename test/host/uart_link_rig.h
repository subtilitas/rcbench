/**
 * @file
 * @brief Both ends of the link over a modelled UART (universal asynchronous
 *        receiver-transmitter) at 1Mbaud, in steps of one byte time (10us).
 *
 * The node's end is link_host.c with link_uart.c's framing: it sends a
 * request's frames back to back, decodes the bytes that come back and runs
 * the repeat and the timeout on a millisecond clock, as test_link_retry's
 * panel does.  The IOmcu's end is link_uart_dev.c as firmware/iomcu runs
 * it: a DMA (direct memory access) model writes every byte that arrives
 * into the 1KiB ring whatever the reader does; a pass every RIG_PASS_US
 * reads the DMA's count, takes the frames, answers them through
 * link_dev_dispatch() and moves the transmit queue into a 32-byte FIFO
 * (first in, first out buffer) that shifts out one byte every 10us.
 *
 * The line can delay each direction, flip a byte or lose a run of bytes by
 * their number, and carry noise.  The reader can stall for a window: the
 * DMA writes on, the FIFO empties, nothing is read or queued.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "greatest.h"

#include "link_can.h"
#include "link_dev.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_port.h"
#include "link_uart.h"
#include "link_uart_dev.h"

/** @brief The IOmcu's pass period, us. */
#define RIG_PASS_US 250u

/** @brief Bytes a direction of the line holds in flight. */
#define RIG_WIRE_CAP 16384u

/** @brief No byte number: nothing flipped or lost. */
#define RIG_NONE 0xFFFFFFFFu

/* ------------------------------------------------------------ the device */

/** @brief The IOmcu's pages and what was done to them. */
typedef struct {
    uint16_t control[LINK_CT_COUNT]; /**< CONTROL: a write is an action  */
    uint32_t control_writes;         /**< CONTROL write calls            */
    uint16_t wide[LINK_MAX_REGS];    /**< OUTPUTS, 32 registers          */
    uint32_t wide_writes;            /**< OUTPUTS write calls            */
} rig_far_t;

static rig_far_t g_far;

/**
 * @brief CONTROL read.
 * @param ctx unused
 * @param off first register
 * @param n registers
 * @param out receives them
 */
static void rig_ct_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    memcpy(out, g_far.control + off, (size_t)n * 2u);
}

/**
 * @brief CONTROL write, counted.
 * @param ctx unused
 * @param off first register
 * @param n registers
 * @param in the values
 * @return 0, taken
 */
static uint8_t rig_ct_write(void *ctx, uint8_t off, uint8_t n,
                            const uint16_t *in)
{
    (void)ctx;
    memcpy(g_far.control + off, in, (size_t)n * 2u);
    ++g_far.control_writes;
    return 0;
}

/**
 * @brief BENCH read: 0x2000 plus the register number.
 * @param ctx unused
 * @param off first register
 * @param n registers
 * @param out receives them
 */
static void rig_bn_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = (uint16_t)(0x2000u + off + i);
    }
}

/**
 * @brief OUTPUTS read.
 * @param ctx unused
 * @param off first register
 * @param n registers
 * @param out receives them
 */
static void rig_wide_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    memcpy(out, g_far.wide + off, (size_t)n * 2u);
}

/**
 * @brief OUTPUTS write, counted.
 * @param ctx unused
 * @param off first register
 * @param n registers
 * @param in the values
 * @return 0, taken
 */
static uint8_t rig_wide_write(void *ctx, uint8_t off, uint8_t n,
                              const uint16_t *in)
{
    (void)ctx;
    memcpy(g_far.wide + off, in, (size_t)n * 2u);
    ++g_far.wide_writes;
    return 0;
}

/** @brief The IOmcu's page table. */
static const link_page_t k_rig_pages[] = {
    { LINK_PAGE_CONTROL, LINK_CT_COUNT, rig_ct_read,   rig_ct_write },
    { LINK_PAGE_BENCH,   LINK_BN_COUNT, rig_bn_read,   NULL },
    { LINK_PAGE_OUTPUTS, LINK_MAX_REGS, rig_wide_read, rig_wide_write },
};

/* -------------------------------------------------------------- the line */

/** @brief One byte on its way and when it arrives. */
typedef struct {
    uint8_t  b;  /**< the byte        */
    uint32_t at; /**< arrival, rig us */
} rig_byte_t;

/** @brief One direction of the line, in arrival order. */
typedef struct {
    rig_byte_t q[RIG_WIRE_CAP]; /**< the bytes          */
    uint32_t   head;            /**< bytes put          */
    uint32_t   tail;            /**< bytes delivered    */
    uint32_t   last;            /**< the last arrival   */
    uint32_t   n;               /**< bytes ever sent    */
    uint32_t   delay_us;        /**< added to each byte */
    uint32_t   flip;            /**< this byte number arrives XOR 0x10 */
    uint32_t   lose_from;       /**< bytes from this number ...        */
    uint32_t   lose_to;         /**< ... to before this one are lost   */
} rig_wire_t;

/** @brief The whole rig: clock, line, both ends. */
typedef struct {
    uint32_t t;         /**< the rig's clock, us from the start          */
    uint32_t ms0;       /**< both ends' ms clock at t = 0                */
    rig_wire_t up;      /**< node to IOmcu                               */
    rig_wire_t down;    /**< IOmcu to node                               */
    uint32_t node_free; /**< the node's line is free from this time      */
    uint32_t dev_free;  /**< the IOmcu's FIFO is empty from this time    */

    /* The IOmcu. */
    uint8_t  ring[LINK_UART_RING_BYTES]; /**< written by the DMA model   */
    uint32_t written;   /**< bytes the DMA wrote                         */
    uint32_t reload;    /**< its transfers per trigger                   */
    link_uart_ring_t rd;   /**< the reader                               */
    link_uart_txq_t  txq;  /**< the transmit queue                       */
    link_dev_t       dev;  /**< the dispatcher                           */
    uint32_t refused;   /**< STATUS registers 8 and 9                    */
    uint32_t served;    /**< requests taken                              */
    uint32_t stall_from; /**< the reader runs no pass from this time ... */
    uint32_t stall_to;   /**< ... to before this one                     */
    uint32_t noise_at;   /**< noise starts on the node's line at this time */
    uint32_t noise_n;    /**< its bytes                                   */
    uint8_t  noise_b;    /**< their value                                 */
    bool     noise_delim; /**< a 0x00 after them                          */

    /* The node. */
    link_host_t     host;  /**< the link host                            */
    link_uart_rx_t  nrx;   /**< its decoder                              */
    uint32_t nrefused;  /**< frames its decoder refused                  */
    bool     answered;  /**< the host took a whole answer                */
    link_msg_t whole;   /**< that answer                                 */
} rig_t;

static rig_t rig;

/**
 * @brief Both ends' millisecond clock now.
 * @return ms
 */
static uint32_t rig_ms(void)
{
    return rig.ms0 + rig.t / 1000u;
}

/**
 * @brief Put one byte on a direction of the line, after everything on it,
 *        unless its number is lost.
 * @param w the direction
 * @param b the byte
 * @param at when it arrives, before the line's ordering
 */
static void rig_wire_put(rig_wire_t *w, uint8_t b, uint32_t at)
{
    const uint32_t k = w->n++;
    if (k >= w->lose_from && k < w->lose_to) {
        return;
    }
    if (k == w->flip) {
        b ^= 0x10u;
    }
    if ((int32_t)(at - w->last) <= 0 && w->head != w->tail) {
        at = w->last + 1u;
    }
    CHECK(w->head - w->tail < RIG_WIRE_CAP);
    w->q[w->head % RIG_WIRE_CAP].b  = b;
    w->q[w->head % RIG_WIRE_CAP].at = at;
    ++w->head;
    w->last = at;
}

/**
 * @brief The node sends one message: its frames back to back on its line.
 * @param m the message
 */
static void rig_node_send(const link_msg_t *m)
{
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    const size_t n = link_can_encode(m, f, LINK_CAN_MAX_FRAMES);
    CHECK(n > 0u);
    for (size_t i = 0; i < n; ++i) {
        uint8_t wire[LINK_UART_WIRE_MAX];
        const size_t k = link_uart_encode(&f[i], wire, sizeof(wire));
        CHECK(k >= LINK_UART_WIRE_MIN);
        for (size_t j = 0; j < k; ++j) {
            if ((int32_t)(rig.node_free - rig.t) < 0) {
                rig.node_free = rig.t;
            }
            rig.node_free += LINK_UART_BYTE_US;
            rig_wire_put(&rig.up, wire[j], rig.node_free + rig.up.delay_us);
        }
    }
}

/**
 * @brief Noise on the node's line from now: @p n bytes of @p b at the line
 *        rate, then one 0x00 when @p delim.
 * @param n bytes
 * @param b their value
 * @param delim a 0x00 after them
 */
static void rig_noise(uint32_t n, uint8_t b, bool delim)
{
    for (uint32_t i = 0; i <= n; ++i) {
        if (i == n && !delim) {
            break;
        }
        if ((int32_t)(rig.node_free - rig.t) < 0) {
            rig.node_free = rig.t;
        }
        rig.node_free += LINK_UART_BYTE_US;
        rig_wire_put(&rig.up, i == n ? 0x00u : b,
                     rig.node_free + rig.up.delay_us);
    }
}

/**
 * @brief The DMA's transfer count as the channel shows it now.
 * @return remaining transfers of the current trigger
 */
static uint32_t rig_dma_remaining(void)
{
    return rig.reload - (rig.written % rig.reload);
}

/** @brief One pass of the IOmcu's main loop: receive, answer, transmit. */
static void rig_dev_pass(void)
{
    const uint32_t now = rig_ms();
    link_uart_ring_dma(&rig.rd, rig_dma_remaining());
    link_can_frame_t in;
    while (link_uart_ring_next(&rig.rd, &in, &rig.refused)) {
        link_can_frame_t out[LINK_CAN_MAX_FRAMES];
        const size_t n = link_uart_dev_answer(&rig.dev, &in, now,
                                              &rig.refused, &rig.served, out);
        if (n > 0u) {
            (void)link_uart_txq_put(&rig.txq, out, n);
        }
    }
    /* The FIFO takes bytes while it holds fewer than 32. */
    for (;;) {
        if ((int32_t)(rig.dev_free - rig.t) < 0) {
            rig.dev_free = rig.t;
        }
        if (rig.dev_free - rig.t >= LINK_UART_FIFO_BYTES * LINK_UART_BYTE_US) {
            break;
        }
        uint8_t b;
        if (!link_uart_txq_get(&rig.txq, &b)) {
            break;
        }
        rig.dev_free += LINK_UART_BYTE_US;
        rig_wire_put(&rig.down, b, rig.dev_free + rig.down.delay_us);
    }
    (void)link_dev_tick(&rig.dev, now);
}

/** @brief Deliver what is due on both directions of the line. */
static void rig_deliver(void)
{
    while (rig.up.tail != rig.up.head
           && (int32_t)(rig.t - rig.up.q[rig.up.tail % RIG_WIRE_CAP].at) >= 0) {
        rig.ring[rig.written % LINK_UART_RING_BYTES] =
            rig.up.q[rig.up.tail % RIG_WIRE_CAP].b;
        ++rig.written;
        ++rig.up.tail;
    }
    while (rig.down.tail != rig.down.head
           && (int32_t)(rig.t - rig.down.q[rig.down.tail % RIG_WIRE_CAP].at)
                  >= 0) {
        const uint8_t b = rig.down.q[rig.down.tail % RIG_WIRE_CAP].b;
        ++rig.down.tail;
        link_can_frame_t f;
        link_msg_t m;
        if (link_uart_feed(&rig.nrx, b, &f, &rig.nrefused)
            && link_can_decode(&f, &m)
            && link_host_accept(&rig.host, &m, rig_ms(), &rig.whole)) {
            rig.answered = true;
        }
    }
}

/**
 * @brief Run one byte time: the line, then the IOmcu's pass when due and
 *        not stalled.
 */
static void rig_step(void)
{
    rig.t += LINK_UART_BYTE_US;
    if (rig.t == rig.noise_at) {
        rig_noise(rig.noise_n, rig.noise_b, rig.noise_delim);
    }
    rig_deliver();
    const bool stalled = (int32_t)(rig.t - rig.stall_from) >= 0
                         && (int32_t)(rig.t - rig.stall_to) < 0;
    if (rig.t % RIG_PASS_US == 0u && !stalled) {
        rig_dev_pass();
    }
}

/**
 * @brief Run the rig for @p us microseconds with no request of the node's.
 * @param us the time
 */
static void rig_run(uint32_t us)
{
    for (uint32_t k = 0; k < us / LINK_UART_BYTE_US; ++k) {
        rig_step();
    }
}

/**
 * @brief The node's wait for one request, as the panel's exchange(): send,
 *        then a millisecond a step: receive, repeat when due, tick.
 * @param req the request, built by link_host_read() or link_host_write()
 * @param whole receives the answer
 * @param ended receives the ms clock when the wait ended
 * @return true when answered, false when abandoned
 */
static bool rig_wait(const link_msg_t *req, link_msg_t *whole,
                     uint32_t *ended)
{
    rig.answered = false;
    rig_node_send(req);
    for (unsigned ms = 0; ms < 3000u; ++ms) {
        for (unsigned k = 0; k < 1000u / LINK_UART_BYTE_US; ++k) {
            rig_step();
            if (rig.answered) {
                *whole = rig.whole;
                *ended = rig_ms();
                return true;
            }
        }
        link_msg_t again;
        if (link_host_retry(&rig.host, rig_ms(), &again)) {
            rig_node_send(&again);
        }
        (void)link_host_tick(&rig.host, rig_ms());
        if (!link_host_pending(&rig.host)) {
            *ended = rig_ms();
            return false;
        }
    }
    CHECK(false);
    return false;
}

/**
 * @brief Fresh ends at ms clock @p ms0, the node numbering its requests,
 *        the DMA re-triggering every @p reload bytes.
 * @param ms0 both ends' ms clock at the start
 * @param reload transfers per trigger, a multiple of the ring
 */
static void rig_fresh(uint32_t ms0, uint32_t reload)
{
    memset(&rig, 0, sizeof(rig));
    memset(&g_far, 0, sizeof(g_far));
    rig.ms0 = ms0;
    rig.reload = reload;
    rig.up.flip = RIG_NONE;
    rig.down.flip = RIG_NONE;
    rig.up.lose_from = RIG_NONE;
    rig.up.lose_to = RIG_NONE;
    rig.down.lose_from = RIG_NONE;
    rig.down.lose_to = RIG_NONE;
    rig.stall_from = RIG_NONE;
    rig.stall_to = RIG_NONE;
    rig.noise_at = RIG_NONE;
    CHECK(link_uart_ring_init(&rig.rd, rig.ring, reload));
    link_uart_txq_init(&rig.txq);
    link_dev_init(&rig.dev, k_rig_pages,
                  (uint8_t)(sizeof(k_rig_pages) / sizeof(k_rig_pages[0])),
                  NULL, ms0);
    link_host_init(&rig.host, ms0);
    rig.host.numbered = true;
    rig.host.held_reads = true;
    link_uart_rx_init(&rig.nrx);
    /* The IOmcu's reader starts synced; the node's decoder syncs on the
     * 0x00 the IOmcu sends at its start. */
    rig_wire_put(&rig.down, LINK_UART_DELIM, LINK_UART_BYTE_US);
    rig.down.n = 0u;
    rig_run(1000u);
}

/**
 * @brief Write @p v to CONTROL register 1 through the rig.
 * @param v the value
 * @param whole receives the answer
 * @param ended receives the ms clock when the wait ended
 * @return true when answered
 */
static bool rig_write_control(uint16_t v, link_msg_t *whole, uint32_t *ended)
{
    link_msg_t req;
    CHECK(link_host_write(&rig.host, LINK_PAGE_CONTROL, 1u, 1u, &v, rig_ms(),
                          &req));
    return rig_wait(&req, whole, ended);
}

/**
 * @brief link_port_t's exchange over the rig.
 * @param ctx unused
 * @param h the host, the rig's
 * @param req the request
 * @param reply receives the answer
 * @return true when answered
 */
/* The member of link_port_t takes a non-const host; this one only compares
 * it, and a const parameter would need a cast at the table entry. */
// cppcheck-suppress constParameterCallback
static bool rig_port_exchange(void *ctx, link_host_t *h, const link_msg_t *req,
                              link_msg_t *reply)
{
    (void)ctx;
    CHECK(h == &rig.host);
    uint32_t ended;
    return rig_wait(req, reply, &ended);
}

/**
 * @brief link_port_t's clock.
 * @param ctx unused
 * @return the rig's ms clock
 */
static uint32_t rig_port_now(void *ctx)
{
    (void)ctx;
    return rig_ms();
}

/** @brief The rig as a link_port_t. */
static const link_port_t k_rig_port = {
    rig_port_exchange, rig_port_now, NULL, NULL,
};
