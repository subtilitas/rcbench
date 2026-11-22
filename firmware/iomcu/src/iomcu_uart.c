/**
 * @file
 * @brief The link on UART0 (universal asynchronous receiver-transmitter):
 * the transport of board 3, the remote servo node's Core2350B, beside the
 * CAN (Controller Area Network) transport of boards 1 and 2 (iomcu_link.h).
 *
 * UART0 runs at LINK_UART_BAUD (1,000,000 baud), 8N1, TX on GPIO 0 and RX
 * on GPIO 1, no flow control.  At clk_peri 150MHz the divisor is
 * 150MHz / 16 / 1Mbaud = 9.375 = 9 + 24/64, exact.  Frames are
 * shared/link/link_uart.h's: COBS (consistent overhead byte stuffing) with a
 * CRC-16 (cyclic redundancy check), 9 to 17 bytes, one link_can_frame_t
 * each.
 *
 * Receive: one DMA (direct memory access) channel copies every byte from
 * the UART's data register into a 1KiB ring in SRAM, paced by the UART's
 * receive request, and re-triggers itself every LINK_UART_DMA_RELOAD bytes.
 * It runs through a flash window, which stops this core for up to 42ms
 * with interrupts off; the UART's 32-byte FIFO (first in, first out
 * buffer) alone would hold 320us.  Each service reads the channel's count,
 * takes the frames up to it (link_uart_ring_next()) and answers them as the
 * CAN loop does: the self-test echo and status first, then
 * link_uart_dev_answer() through s_dev.  A refused frame counts in
 * s_refused, STATUS registers 8 and 9.
 *
 * Transmit: a reply is queued whole (link_uart_txq_put()) and moved into
 * the UART's 32-byte FIFO while it has room, twice a pass; no call waits
 * for the line.  The widest reply, 8 frames of 17 bytes, is 1,360us on the
 * line.
 *
 * At start the UART sends one 0x00, so the node's decoder starts at a frame
 * boundary.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

#include "iomcu.h"

#include "can_selftest.h"
#include "iomcu_link.h"
#include "link_uart_dev.h"

/** @brief The UART block the link runs on. */
#define IOMCU_UART uart0

_Static_assert(LINK_UART_TXQ_BYTES * LINK_UART_BYTE_US <= LOOP_WATCH_DRAIN_US,
               "uart_link_drain() empties a full queue within loop_watch.h's "
               "drain bound");

/**
 * @brief The receive ring the DMA channel writes; aligned to its size, as
 * the channel's address wrap needs.
 */
static uint8_t s_uart_ring[LINK_UART_RING_BYTES]
    __attribute__((aligned(LINK_UART_RING_BYTES)));

/** @brief The ring's reader and frame decoder. */
static link_uart_ring_t s_uart_rd;
/** @brief The replies on their way out. */
static link_uart_txq_t s_uart_txq;
/** @brief The receive DMA channel; -1 before uart_link_start() claimed one. */
static int s_uart_dma = -1;
/** @brief The UART and its channel are up. */
static bool s_uart_up;
/** @brief The baud rate the divisor gives. */
static uint32_t s_uart_baud;
/** @brief Self-test echoes answered since power-up. */
static uint32_t s_uart_echoes;
/** @brief Services that found the receive FIFO's overrun flag set. */
static uint32_t s_uart_overruns;

/* Documented in iomcu_link.h. */
void uart_link_start(void)
{
    link_uart_txq_init(&s_uart_txq);
    s_uart_baud = uart_init(IOMCU_UART, LINK_UART_BAUD);
    uart_set_format(IOMCU_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(IOMCU_UART, false, false);
    uart_set_fifo_enabled(IOMCU_UART, true);
    gpio_set_function(IOMCU_UART_PIN_TX, GPIO_FUNC_UART);
    gpio_set_function(IOMCU_UART_PIN_RX, GPIO_FUNC_UART);
    /* An open or unpowered line reads idle, not as a stream of 0x00. */
    gpio_pull_up(IOMCU_UART_PIN_RX);

    s_uart_dma = dma_claim_unused_channel(false);
    if (s_uart_dma < 0) {
        printf("rcbench-iomcu: UART DID NOT START (no free DMA channel)\n");
        return;
    }
    const uint ch = (uint)s_uart_dma;
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    /* The write address wraps at 2^10 bytes: the ring. */
    channel_config_set_ring(&c, true, 10);
    channel_config_set_dreq(&c, uart_get_dreq_num(IOMCU_UART, false));
    (void)link_uart_ring_init(&s_uart_rd, s_uart_ring, LINK_UART_DMA_RELOAD);
    dma_channel_configure(
        ch, &c, s_uart_ring, &uart_get_hw(IOMCU_UART)->dr,
        dma_encode_transfer_count_with_self_trigger(LINK_UART_DMA_RELOAD),
        true);
    s_uart_up = true;
    /* The node's decoder starts at the next 0x00: this one. */
    uart_get_hw(IOMCU_UART)->dr = LINK_UART_DELIM;
    printf("rcbench-iomcu: UART up at %lu baud, TX GPIO %d RX GPIO %d, "
           "receive DMA channel %u\n",
           (unsigned long)s_uart_baud, IOMCU_UART_PIN_TX, IOMCU_UART_PIN_RX,
           ch);
}

/**
 * @brief Move queued bytes into the UART's transmit FIFO while it has room.
 */
static void uart_pump(void)
{
    uint8_t b;
    while (uart_is_writable(IOMCU_UART) && link_uart_txq_get(&s_uart_txq, &b)) {
        uart_get_hw(IOMCU_UART)->dr = b;
    }
}

/**
 * @brief One frame received: the self-test's echo and status, else the
 * link's request, answered into the transmit queue.
 *
 * @param in The frame.
 * @param now The pass's clock in ms.
 */
static void uart_answer(const link_can_frame_t *in, uint32_t now)
{
    link_can_frame_t out[LINK_CAN_MAX_FRAMES];
    if (can_selftest_echo(in, &out[0])) {
        if (link_uart_txq_put(&s_uart_txq, out, 1u)) {
            ++s_uart_echoes;
        }
        return;
    }
    can_remote_status_t st;
    memset(&st, 0, sizeof(st));
    st.up = s_uart_up;
    st.echoes = (uint16_t)s_uart_echoes;
    st.overflows = (uint16_t)s_uart_rd.laps;
    if (can_selftest_status_reply(in, &st, &out[0])) {
        (void)link_uart_txq_put(&s_uart_txq, out, 1u);
        return;
    }
    const size_t n = link_uart_dev_answer(&s_dev, in, now, &s_refused,
                                          &s_frames, out);
    if (n > 0u) {
        /* Whole or not at all; one refused counts in the queue's dropped,
         * and the node repeats the request after LINK_HOST_RETRY_MS. */
        (void)link_uart_txq_put(&s_uart_txq, out, n);
    }
}

/* Documented in iomcu_link.h. */
void uart_link_service(uint32_t now)
{
    if (!s_uart_up) {
        return;
    }
    const uint32_t t0 = time_us_32();
    uart_hw_t *hw = uart_get_hw(IOMCU_UART);
    if ((hw->rsr & UART_UARTRSR_OE_BITS) != 0u) {
        /* The FIFO was full as a byte arrived: the channel fell behind. */
        ++s_uart_overruns;
        hw->rsr = UART_UARTRSR_OE_BITS;
    }
    const uint32_t remaining = dma_channel_hw_addr((uint)s_uart_dma)
                                   ->transfer_count
                               & ~DMA_CH0_TRANS_COUNT_MODE_BITS;
    link_uart_ring_dma(&s_uart_rd, remaining);
    link_can_frame_t in;
    while (link_uart_ring_next(&s_uart_rd, &in, &s_refused)) {
        s_last_rx_ms = now;
        uart_answer(&in, now);
    }
    uart_pump();
    pass_stats_section(&s_pass, PASS_SEC_CAN, t0, time_us_32());
}

/* Documented in iomcu_link.h. */
void uart_link_drain(void)
{
    if (!s_uart_up) {
        return;
    }
    const uint32_t t0 = time_us_32();
    while ((link_uart_txq_held(&s_uart_txq) > 0u
            || (uart_get_hw(IOMCU_UART)->fr & UART_UARTFR_BUSY_BITS) != 0u)
           && time_us_32() - t0 < LOOP_WATCH_DRAIN_US) {
        uart_pump();
    }
}

/* Documented in iomcu_link.h. */
void uart_link_wire_errors(uint16_t *crc_errors, uint16_t *resyncs)
{
    const link_uart_counts_t *c = &s_uart_rd.rx.counts;
    *crc_errors = (uint16_t)(c->bad_crc & 0xFFFFu);
    *resyncs = (uint16_t)((c->garbage + c->overlong + c->bad_cobs
                           + s_uart_rd.laps) & 0xFFFFu);
}

/* Documented in iomcu_link.h. */
void uart_link_snap(report_snap_t *s)
{
    const link_uart_counts_t *c = &s_uart_rd.rx.counts;
    s->uart         = true;
    s->can_up       = s_uart_up;
    s->bitrate      = s_uart_baud;
    s->requests     = s_frames;
    s->echoes       = s_uart_echoes;
    s->uart_crc     = c->bad_crc;
    s->uart_resync  = c->garbage + c->overlong + c->bad_cobs
                      + s_uart_rd.laps;
    s->uart_laps    = s_uart_rd.laps;
    s->overruns     = s_uart_overruns;
    s->tx_drop      = s_uart_txq.dropped;
    s->uart_txq_max = link_uart_txq_take_peak(&s_uart_txq);
}
