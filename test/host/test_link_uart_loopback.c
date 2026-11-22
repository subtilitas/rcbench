/**
 * @file
 * @brief Both ends of the link over a modelled UART (universal asynchronous
 *        receiver-transmitter): link_host.c at the node, link_uart_dev.c
 *        and link_dev.c at the IOmcu, link_uart.c's framing between them
 *        (uart_link_rig.h).
 *
 * The parts first: the DMA (direct memory access) count turned into bytes
 * across its reload and across 2^32, the lap at the ring's slack, the
 * transmit queue whole or not at all, and the answer to one frame.  Then
 * whole exchanges: a page wider than a frame, a write acknowledged frame by
 * frame, the widest reply through the 32-byte FIFO (first in, first out
 * buffer), a flipped byte each way, and a boot message in front of the
 * first request.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "uart_link_rig.h"

/* ------------------------------------------------------- the ring reader */

/** @brief A ring the cases write by hand. */
static uint8_t s_ring[LINK_UART_RING_BYTES];

/**
 * @brief Write one frame's wire bytes into s_ring from byte number @p at.
 * @param f the frame
 * @param at the byte number of its first byte
 * @return its wire bytes
 */
static uint32_t ring_put_frame(const link_can_frame_t *f, uint32_t at)
{
    uint8_t wire[LINK_UART_WIRE_MAX];
    const size_t n = link_uart_encode(f, wire, sizeof(wire));
    CHECK(n > 0u);
    for (size_t i = 0; i < n; ++i) {
        s_ring[(at + i) & (LINK_UART_RING_BYTES - 1u)] = wire[i];
    }
    return (uint32_t)n;
}

/**
 * @brief A frame with @p dlc bytes of @p v and identifier @p id.
 * @param id the identifier
 * @param dlc the data length
 * @param v the data bytes
 * @return the frame
 */
static link_can_frame_t frame_of(uint32_t id, uint8_t dlc, uint8_t v)
{
    link_can_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id = id;
    f.dlc = dlc;
    memset(f.data, v, dlc);
    return f;
}

/* A reload that is 0 or no multiple of the ring is refused; the RP2350's
 * figure is taken. */
TEST_CASE(a_reload_that_is_no_multiple_of_the_ring_is_refused)
{
    link_uart_ring_t r;
    CHECK(!link_uart_ring_init(&r, s_ring, 0u));
    CHECK(!link_uart_ring_init(&r, s_ring, LINK_UART_RING_BYTES + 1u));
    CHECK(!link_uart_ring_init(&r, s_ring, LINK_UART_RING_BYTES / 2u));
    CHECK(!link_uart_ring_init(&r, NULL, LINK_UART_DMA_RELOAD));
    CHECK(!link_uart_ring_init(NULL, s_ring, LINK_UART_DMA_RELOAD));
    CHECK(link_uart_ring_init(&r, s_ring, LINK_UART_DMA_RELOAD));
    CHECK(link_uart_ring_init(&r, s_ring, LINK_UART_RING_BYTES));
    CHECK_EQ(r.head, 0u);
    CHECK_EQ(r.tail, 0u);
}

/* The count runs down from the reload; at 0 or back at the reload the
 * channel has started again, and the bytes of both triggers count. */
TEST_CASE(the_dma_count_turns_into_bytes_across_its_reload)
{
    link_uart_ring_t r;
    CHECK(link_uart_ring_init(&r, s_ring, 4096u));
    link_uart_ring_dma(&r, 4096u);
    CHECK_EQ(r.head, 0u);
    link_uart_ring_dma(&r, 4000u);
    CHECK_EQ(r.head, 96u);
    link_uart_ring_dma(&r, 1u);
    CHECK_EQ(r.head, 4095u);
    /* Read as 0 at the end of the trigger ... */
    link_uart_ring_dma(&r, 0u);
    CHECK_EQ(r.head, 4096u);
    /* ... and then 6 into the next. */
    link_uart_ring_dma(&r, 4090u);
    CHECK_EQ(r.head, 4102u);
    /* Read as the reload at the end of the next one. */
    link_uart_ring_dma(&r, 4096u);
    CHECK_EQ(r.head, 8192u);
    /* Over the reload is read as the reload: nothing more. */
    link_uart_ring_dma(&r, 5000u);
    CHECK_EQ(r.head, 8192u);
    /* A reload passed between two looks: 4000 left, then 4090 left. */
    link_uart_ring_dma(&r, 4000u);
    link_uart_ring_dma(&r, 4090u);
    CHECK_EQ(r.head, 8192u + 96u + 4000u + 6u);
    link_uart_ring_dma(NULL, 1u);
}

/* The bytes written since the start wrap at 2^32, a multiple of the ring,
 * so a frame across the wrap decodes from the right bytes. */
TEST_CASE(a_frame_across_the_2_32_wrap_of_the_byte_count_decodes)
{
    link_uart_ring_t r;
    const uint32_t R = LINK_UART_DMA_RELOAD;
    CHECK(link_uart_ring_init(&r, s_ring, R));
    /* 16 triggers: 2^32 - 16384 bytes. */
    for (unsigned k = 0; k < 16u; ++k) {
        link_uart_ring_dma(&r, R / 2u);
        link_uart_ring_dma(&r, R);
    }
    CHECK_EQ(r.head, 0xFFFFC000u);
    link_uart_ring_dma(&r, R - (16384u - 5u));
    CHECK_EQ(r.head, 0xFFFFFFFBu);
    link_can_frame_t got;
    uint32_t refused = 0u;
    /* What the reader skipped is a lap, and the ring holds nothing. */
    CHECK(!link_uart_ring_next(&r, &got, &refused));
    CHECK_EQ(r.laps, 1u);
    /* A 0x00 so the frame after it is whole, then the frame over the wrap. */
    memset(s_ring, 0x00, sizeof(s_ring));
    const link_can_frame_t f = frame_of(0x1ABCDEF0u, 8u, 0xA5u);
    const uint32_t n = ring_put_frame(&f, r.head + 1u);
    link_uart_ring_dma(&r, R - (16384u - 5u) - 1u - n);
    CHECK(r.head < 0x10u);
    CHECK(link_uart_ring_next(&r, &got, &refused));
    CHECK_EQ(got.id, f.id);
    CHECK_EQ(got.dlc, 8u);
    CHECK(memcmp(got.data, f.data, 8u) == 0);
    CHECK_EQ(refused, 0u);
    CHECK(!link_uart_ring_next(&r, &got, &refused));
}

/* 960 bytes behind is no lap; 961 is.  After the lap the frame under way
 * is dropped and the frames after the next 0x00 are taken. */
TEST_CASE(a_reader_one_byte_past_the_slack_counts_one_lap)
{
    const uint32_t keep = LINK_UART_RING_BYTES - LINK_UART_RING_SLACK;
    link_uart_ring_t r;
    CHECK(link_uart_ring_init(&r, s_ring, 4096u));
    memset(s_ring, 0x00, sizeof(s_ring));
    link_uart_ring_dma(&r, 4096u - keep);
    link_can_frame_t got;
    uint32_t refused = 0u;
    CHECK(!link_uart_ring_next(&r, &got, &refused));
    CHECK_EQ(r.laps, 0u);
    CHECK_EQ(r.tail, keep);

    /* The ring as bytes 960 .. 1921: 0x55 up to a 0x00, then a frame. */
    memset(s_ring, 0x55, sizeof(s_ring));
    const uint32_t start = r.tail + 1u;  /* one byte past the slack */
    s_ring[(start + 100u) & (LINK_UART_RING_BYTES - 1u)] = 0x00u;
    const link_can_frame_t f = frame_of(0x00012345u, 3u, 0x00u);
    const uint32_t n = ring_put_frame(&f, start + 101u);
    const uint32_t head = start + keep;
    for (uint32_t i = start + 101u + n; i != head; ++i) {
        s_ring[i & (LINK_UART_RING_BYTES - 1u)] = 0x00u;
    }
    link_uart_ring_dma(&r, 4096u - head);
    CHECK_EQ(r.head - r.tail, keep + 1u);
    CHECK(link_uart_ring_next(&r, &got, &refused));
    CHECK_EQ(r.laps, 1u);
    CHECK_EQ(got.id, f.id);
    CHECK_EQ(got.dlc, 3u);
    /* The 100 bytes of 0x55 count nothing: the lap is their count. */
    CHECK_EQ(refused, 0u);
    CHECK(!link_uart_ring_next(&r, &got, &refused));
    CHECK_EQ(r.tail, r.head);
    CHECK(!link_uart_ring_next(NULL, &got, &refused));
    CHECK(!link_uart_ring_next(&r, NULL, &refused));

    /* A decoder not yet synced stays so: the bytes before its first 0x00
     * still count once, as garbage. */
    link_uart_rx_t rx;
    link_uart_rx_init(&rx);
    (void)link_uart_feed(&rx, 0x41u, &got, &refused);
    link_uart_rx_drop(&rx);
    link_uart_rx_drop(NULL);
    CHECK(!rx.synced);
    (void)link_uart_feed(&rx, 0x00u, &got, &refused);
    CHECK_EQ(rx.counts.garbage, 1u);
    CHECK_EQ(refused, 1u);
}

/* ------------------------------------------------------- the transmit queue */

/* Three replies of 8 frames fit; a fourth is refused whole and counted, and
 * what is queued leaves in order. */
TEST_CASE(the_queue_takes_a_reply_whole_or_not_at_all)
{
    link_uart_txq_t q;
    link_uart_txq_init(&q);
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    for (unsigned i = 0; i < LINK_CAN_MAX_FRAMES; ++i) {
        f[i] = frame_of(0x100u + i, 8u, (uint8_t)(0x11u * (i + 1u)));
    }
    const size_t wide = LINK_CAN_MAX_FRAMES * LINK_UART_WIRE_MAX;
    CHECK_EQ(wide, 136u);
    for (unsigned k = 0; k < 3u; ++k) {
        CHECK(link_uart_txq_put(&q, f, LINK_CAN_MAX_FRAMES));
    }
    CHECK_EQ(link_uart_txq_held(&q), 3u * wide);
    CHECK(!link_uart_txq_put(&q, f, LINK_CAN_MAX_FRAMES));
    CHECK_EQ(q.dropped, 1u);
    CHECK_EQ(link_uart_txq_held(&q), 3u * wide);
    /* 104 bytes free: a frame of 17 fits, and a reply of 7 does not. */
    CHECK(!link_uart_txq_put(&q, f, 7u));
    CHECK_EQ(q.dropped, 2u);
    CHECK(link_uart_txq_put(&q, f, 6u));
    CHECK_EQ(link_uart_txq_held(&q), 3u * wide + 6u * 17u);
    CHECK_EQ(link_uart_txq_take_peak(&q), 3u * wide + 6u * 17u);

    /* The bytes leave as the encoder wrote them, across the wrap. */
    link_uart_rx_t rx;
    link_uart_rx_init(&rx);
    link_can_frame_t none;
    (void)link_uart_feed(&rx, 0u, &none, NULL);
    unsigned frames = 0;
    uint8_t b;
    while (link_uart_txq_get(&q, &b)) {
        link_can_frame_t got;
        if (link_uart_feed(&rx, b, &got, NULL)) {
            CHECK_EQ(got.id, f[frames % LINK_CAN_MAX_FRAMES].id);
            ++frames;
        }
    }
    CHECK_EQ(frames, 30u);
    CHECK_EQ(rx.counts.frames, 30u);
    CHECK_EQ(link_uart_txq_held(&q), 0u);
    /* The peak starts again from what is held. */
    CHECK_EQ(link_uart_txq_take_peak(&q), 3u * wide + 6u * 17u);
    CHECK_EQ(link_uart_txq_take_peak(&q), 0u);
    CHECK(link_uart_txq_put(&q, f, LINK_CAN_MAX_FRAMES));
    CHECK(link_uart_txq_put(&q, f, LINK_CAN_MAX_FRAMES));
    CHECK(link_uart_txq_put(&q, f, LINK_CAN_MAX_FRAMES));
    CHECK_EQ(link_uart_txq_held(&q), 3u * wide);

    /* Refused without a count: no frames, too many, NULL. */
    CHECK(!link_uart_txq_put(&q, f, 0u));
    CHECK(!link_uart_txq_put(&q, f, LINK_CAN_MAX_FRAMES + 1u));
    CHECK(!link_uart_txq_put(&q, NULL, 1u));
    CHECK(!link_uart_txq_put(NULL, f, 1u));
    CHECK_EQ(q.dropped, 2u);
    /* A frame that does not encode refuses the reply and counts. */
    link_uart_txq_init(&q);
    f[1].dlc = 9u;
    CHECK(!link_uart_txq_put(&q, f, 2u));
    CHECK_EQ(q.dropped, 1u);
    CHECK_EQ(link_uart_txq_held(&q), 0u);
    CHECK(!link_uart_txq_get(&q, &b));
    CHECK(!link_uart_txq_get(NULL, &b));
    CHECK_EQ(link_uart_txq_held(NULL), 0u);
    CHECK_EQ(link_uart_txq_take_peak(NULL), 0u);
}

/* -------------------------------------------------------------- the answer */

/* A request is counted and answered; a frame the link decode refuses counts
 * in refused and gets nothing. */
TEST_CASE(a_frame_the_link_refuses_counts_and_gets_no_answer)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    link_msg_t req;
    CHECK(link_host_read(&rig.host, LINK_PAGE_BENCH, 0u, LINK_BN_COUNT, 1000u,
                         &req));
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    CHECK_EQ(link_can_encode(&req, f, LINK_CAN_MAX_FRAMES), 1u);
    link_can_frame_t out[LINK_CAN_MAX_FRAMES];
    uint32_t refused = 0u;
    uint32_t served = 0u;
    CHECK_EQ(link_uart_dev_answer(&rig.dev, &f[0], 1000u, &refused, &served,
                                  out), 4u);
    CHECK_EQ(served, 1u);
    CHECK_EQ(refused, 0u);
    /* A READ with a payload: refused at the link decode. */
    f[0].dlc = 8u;
    CHECK_EQ(link_uart_dev_answer(&rig.dev, &f[0], 1000u, &refused, &served,
                                  out), 0u);
    CHECK_EQ(served, 1u);
    CHECK_EQ(refused, 1u);
    CHECK_EQ(link_uart_dev_answer(NULL, &f[0], 1000u, &refused, &served, out),
             0u);
    CHECK_EQ(link_uart_dev_answer(&rig.dev, NULL, 1000u, &refused, &served,
                                  out), 0u);
    CHECK_EQ(link_uart_dev_answer(&rig.dev, &f[0], 1000u, &refused, &served,
                                  NULL), 0u);
}

/* ---------------------------------------------------------- whole exchanges */

/* BENCH's 13 registers: 9 bytes up, 4 frames back (62 bytes), assembled
 * into the window asked for within the millisecond. */
TEST_CASE(a_page_wider_than_a_frame_comes_back_whole)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    const uint32_t start = rig_ms();
    link_msg_t req;
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(link_host_read(&rig.host, LINK_PAGE_BENCH, 0u, LINK_BN_COUNT,
                         start, &req));
    CHECK(rig_wait(&req, &got, &ended));
    CHECK_EQ(got.op, LINK_OP_DATA);
    CHECK_EQ(got.count, LINK_BN_COUNT);
    for (unsigned i = 0; i < LINK_BN_COUNT; ++i) {
        CHECK_EQ(got.regs[i], 0x2000u + i);
    }
    CHECK(ended - start <= 1u);
    CHECK_EQ(rig.up.n, 9u);
    CHECK_EQ(rig.down.n, 3u * 17u + 11u);
    CHECK_EQ(rig.served, 1u);
    CHECK_EQ(rig.refused, 0u);
    CHECK_EQ(rig.nrefused, 0u);
    CHECK_EQ(rig.rd.laps, 0u);
    CHECK_EQ(rig.host.retried, 0u);
}

/* A 32-register write as link_write_acked() sends it: 8 exchanges, each
 * acknowledged before the next. */
TEST_CASE(a_32_register_write_is_acknowledged_frame_by_frame)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    uint16_t v[LINK_MAX_REGS];
    for (unsigned i = 0; i < LINK_MAX_REGS; ++i) {
        v[i] = (uint16_t)(0x7000u + i);
    }
    link_msg_t got;
    CHECK(link_write_acked(&rig.host, &k_rig_port, LINK_PAGE_OUTPUTS, 0u,
                           LINK_MAX_REGS, v, &got));
    CHECK_EQ(got.op, LINK_OP_ACK);
    CHECK(memcmp(g_far.wide, v, sizeof(v)) == 0);
    CHECK_EQ(g_far.wide_writes, 8u);
    CHECK_EQ(rig.served, 8u);
    CHECK_EQ(rig.host.replies, 8u);
    CHECK_EQ(rig.refused, 0u);
}

/* A read of 32 registers in one request is the widest reply: 8 frames, 136
 * bytes, queued whole and moved into the 32-byte FIFO over the passes. */
TEST_CASE(the_widest_reply_leaves_through_the_32_byte_fifo)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    for (unsigned i = 0; i < LINK_MAX_REGS; ++i) {
        g_far.wide[i] = (uint16_t)(0x0100u * i + 0x00FFu);
    }
    const uint32_t start = rig_ms();
    link_msg_t req;
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(link_host_read(&rig.host, LINK_PAGE_OUTPUTS, 0u, LINK_MAX_REGS,
                         start, &req));
    CHECK(rig_wait(&req, &got, &ended));
    CHECK_EQ(got.count, LINK_MAX_REGS);
    CHECK(memcmp(got.regs, g_far.wide, sizeof(g_far.wide)) == 0);
    CHECK_EQ(rig.down.n, 136u);
    CHECK_EQ(link_uart_txq_take_peak(&rig.txq), 136u);
    CHECK_EQ(rig.txq.dropped, 0u);
    /* 9 bytes up, a pass within 250us, 136 bytes at 10us: under 2ms. */
    CHECK(ended - start <= 2u);
}

/* A byte of the request changed on the line: the IOmcu refuses the frame
 * and counts it in STATUS registers 8 and 9; the node's repeat at 50ms is
 * the first request it acts on. */
TEST_CASE(a_flipped_request_byte_is_refused_counted_and_repeated)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    rig.up.flip = rig.up.n + 4u;
    const uint32_t start = rig_ms();
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(rig_write_control(77u, &got, &ended));
    CHECK_EQ(got.op, LINK_OP_ACK);
    CHECK_EQ(rig.refused, 1u);
    CHECK_EQ(rig.rd.rx.counts.bad_crc + rig.rd.rx.counts.bad_cobs
                 + rig.rd.rx.counts.bad_len, 1u);
    CHECK_EQ(rig.served, 1u);
    CHECK_EQ(g_far.control_writes, 1u);
    CHECK_EQ(rig.host.retried, 1u);
    CHECK_EQ(ended - start, LINK_HOST_RETRY_MS);
    CHECK(!rig.dev.failsafe);
}

/* A byte of the answer changed: the node's decoder drops it, and the
 * repeat is answered from the IOmcu's copy: the write acts once. */
TEST_CASE(a_flipped_answer_byte_is_dropped_and_the_copy_answers)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    rig.down.flip = rig.down.n + 2u;
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(rig_write_control(5u, &got, &ended));
    CHECK_EQ(got.op, LINK_OP_ACK);
    CHECK_EQ(got.regs[0], 5u);
    CHECK_EQ(rig.nrefused, 1u);
    CHECK_EQ(rig.refused, 0u);
    CHECK_EQ(rig.served, 2u);
    CHECK_EQ(rig.dev.repeats, 1u);
    CHECK_EQ(g_far.control_writes, 1u);
    CHECK_EQ(rig.host.retried, 1u);
}

/* The ES3C28P's ROM prints at 115,200 baud on the same line as it boots.
 * Ending in a 0x00 it is one refused frame and the request after it is
 * taken; without one the request is part of it, refused with it, and the
 * repeat at 50ms is taken. */
TEST_CASE(a_boot_message_in_front_costs_at_most_the_first_request)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    rig_noise(300u, 0x7Eu, true);
    rig_run(4000u);
    CHECK_EQ(rig.refused, 1u);
    CHECK_EQ(rig.rd.rx.counts.overlong, 1u);
    uint32_t start = rig_ms();
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(rig_write_control(1u, &got, &ended));
    CHECK(ended - start <= 1u);
    CHECK_EQ(rig.host.retried, 0u);

    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    rig_noise(300u, 0x7Eu, false);
    start = rig_ms();
    CHECK(rig_write_control(2u, &got, &ended));
    CHECK_EQ(rig.refused, 1u);
    CHECK_EQ(rig.rd.rx.counts.overlong, 1u);
    CHECK_EQ(rig.host.retried, 1u);
    CHECK_EQ(g_far.control_writes, 1u);
    CHECK_EQ(g_far.control[1], 2u);
    CHECK(ended - start <= LINK_HOST_RETRY_MS + 4u);
}

int main(void)
{
    RUN(a_reload_that_is_no_multiple_of_the_ring_is_refused);
    RUN(the_dma_count_turns_into_bytes_across_its_reload);
    RUN(a_frame_across_the_2_32_wrap_of_the_byte_count_decodes);
    RUN(a_reader_one_byte_past_the_slack_counts_one_lap);
    RUN(the_queue_takes_a_reply_whole_or_not_at_all);
    RUN(a_frame_the_link_refuses_counts_and_gets_no_answer);
    RUN(a_page_wider_than_a_frame_comes_back_whole);
    RUN(a_32_register_write_is_acknowledged_frame_by_frame);
    RUN(the_widest_reply_leaves_through_the_32_byte_fifo);
    RUN(a_flipped_request_byte_is_refused_counted_and_repeated);
    RUN(a_flipped_answer_byte_is_dropped_and_the_copy_answers);
    RUN(a_boot_message_in_front_costs_at_most_the_first_request);
    return test_summary("link_uart_loopback");
}
