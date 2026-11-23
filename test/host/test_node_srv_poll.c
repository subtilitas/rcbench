/**
 * @file
 * @brief The servo node's poll of the Core2350B
 *        (firmware/node/main/node_srv_poll.c).
 *
 * The failures this is about: a CONTROL write that slips behind the reads
 * past the IOmcu's 200ms silence, a row read at the wrong period, STATUS not
 * read every 50ms while a fault or an ARM waits, a stalled link that comes
 * back with a burst, and the periods breaking at the ms clock's wrap.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_can.h"
#include "link_uart.h"

#include "link_dev.h"
#include "link_pages.h"
#include "node_srv_poll.h"

/** @brief Take every due row at @p now_ms, in order; count them. */
static unsigned take_all(node_srv_poll_t *p, uint32_t now_ms, bool fast,
                         node_srv_row_t *first)
{
    unsigned n = 0u;
    *first = NODE_SRV_ROW_NONE;
    for (;;) {
        const node_srv_row_t r = node_srv_poll_next(p, now_ms, fast);
        if (r == NODE_SRV_ROW_NONE) {
            return n;
        }
        if (n == 0u) {
            *first = r;
        }
        node_srv_poll_taken(p, r, now_ms);
        ++n;
    }
}

TEST_CASE(the_table_reads_whole_pages_at_their_periods)
{
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_CONTROL].page, LINK_PAGE_CONTROL);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_CONTROL].count, 2u);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_STATUS].count, LINK_ST_COUNT);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_POWER].page, LINK_PAGE_POWER);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_POWER].count, LINK_PW_COUNT);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_SUPPLY].count, LINK_SP_COUNT);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_WIN_A].count, LINK_MAX_REGS);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_WIN_B].page, LINK_PAGE_SENSE_WIN_B);
    /* The write that keeps the IOmcu's link alive comes at least 4 times
     * inside its silence window. */
    CHECK(4u * node_srv_rows[NODE_SRV_ROW_CONTROL].period_ms
          <= LINK_DEV_SILENCE_MS);
}

TEST_CASE(every_row_is_due_at_the_start_control_first)
{
    node_srv_poll_t p;
    node_srv_poll_init(&p);
    node_srv_row_t first;
    CHECK_EQ(take_all(&p, 12345u, false, &first), (unsigned)NODE_SRV_ROWS);
    CHECK_EQ(first, NODE_SRV_ROW_CONTROL);
    CHECK_EQ(node_srv_poll_next(&p, 12345u, false), NODE_SRV_ROW_NONE);
}

/** @brief Each row due at its period after @p t0 and not a ms before. */
static void periods_from(uint32_t t0)
{
    node_srv_poll_t p;
    node_srv_poll_init(&p);
    node_srv_row_t first;
    (void)take_all(&p, t0, false, &first);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 49u, false), NODE_SRV_ROW_NONE);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 50u, false), NODE_SRV_ROW_CONTROL);
    node_srv_poll_taken(&p, NODE_SRV_ROW_CONTROL, t0 + 50u);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 50u, false), NODE_SRV_ROW_WIN_A);
    node_srv_poll_taken(&p, NODE_SRV_ROW_WIN_A, t0 + 50u);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 50u, false), NODE_SRV_ROW_WIN_B);
    node_srv_poll_taken(&p, NODE_SRV_ROW_WIN_B, t0 + 50u);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 99u, false), NODE_SRV_ROW_NONE);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 100u, false), NODE_SRV_ROW_CONTROL);
    node_srv_poll_taken(&p, NODE_SRV_ROW_CONTROL, t0 + 100u);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 100u, false), NODE_SRV_ROW_POWER);
    node_srv_poll_taken(&p, NODE_SRV_ROW_POWER, t0 + 100u);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 100u, false), NODE_SRV_ROW_SUPPLY);
    /* STATUS at 500ms and not at 499ms. */
    node_srv_poll_t q;
    node_srv_poll_init(&q);
    for (unsigned k = 0; k < (unsigned)NODE_SRV_ROWS; ++k) {
        node_srv_poll_taken(&q, (node_srv_row_t)k, t0 + 499u);
    }
    node_srv_poll_taken(&q, NODE_SRV_ROW_STATUS, t0);
    CHECK_EQ(node_srv_poll_next(&q, t0 + 499u, false), NODE_SRV_ROW_NONE);
    CHECK_EQ(node_srv_poll_next(&q, t0 + 500u, false), NODE_SRV_ROW_STATUS);
}

TEST_CASE(each_row_comes_at_its_period_and_not_a_ms_before)
{
    periods_from(1000u);
}

TEST_CASE(the_periods_hold_across_the_wrap)
{
    periods_from(0xFFFFFFFFu - 60u);
    periods_from(0xFFFFFFFFu - 499u);
    periods_from(0xFFFFFFFFu);
}

TEST_CASE(fast_reads_status_every_50ms_at_once)
{
    node_srv_poll_t p;
    node_srv_poll_init(&p);
    node_srv_row_t first;
    (void)take_all(&p, 0u, false, &first);
    (void)take_all(&p, 50u, false, &first);
    /* STATUS read at 0; fast from 60ms on. */
    CHECK_EQ(node_srv_poll_next(&p, 60u, true), NODE_SRV_ROW_STATUS);
    node_srv_poll_taken(&p, NODE_SRV_ROW_STATUS, 60u);
    CHECK_EQ(node_srv_poll_next(&p, 99u, true), NODE_SRV_ROW_NONE);
    CHECK_EQ(node_srv_poll_next(&p, 100u, true), NODE_SRV_ROW_CONTROL);
    node_srv_poll_taken(&p, NODE_SRV_ROW_CONTROL, 100u);
    CHECK_EQ(node_srv_poll_next(&p, 109u, true), NODE_SRV_ROW_POWER);
    node_srv_poll_taken(&p, NODE_SRV_ROW_POWER, 109u);
    node_srv_poll_taken(&p, NODE_SRV_ROW_SUPPLY, 109u);
    node_srv_poll_taken(&p, NODE_SRV_ROW_WIN_A, 109u);
    node_srv_poll_taken(&p, NODE_SRV_ROW_WIN_B, 109u);
    CHECK_EQ(node_srv_poll_next(&p, 109u, true), NODE_SRV_ROW_NONE);
    CHECK_EQ(node_srv_poll_next(&p, 110u, true), NODE_SRV_ROW_STATUS);
    CHECK_EQ(node_srv_poll_next(&p, 110u, false), NODE_SRV_ROW_NONE);
}

TEST_CASE(a_stalled_link_comes_back_with_one_exchange_a_row)
{
    node_srv_poll_t p;
    node_srv_poll_init(&p);
    node_srv_row_t first;
    (void)take_all(&p, 0u, false, &first);
    /* 1000ms without an exchange: each row once, CONTROL first. */
    CHECK_EQ(take_all(&p, 1000u, false, &first), (unsigned)NODE_SRV_ROWS);
    CHECK_EQ(first, NODE_SRV_ROW_CONTROL);
    CHECK_EQ(take_all(&p, 1049u, false, &first), 0u);
}

TEST_CASE(now_makes_a_row_due_at_once_and_ignores_none)
{
    node_srv_poll_t p;
    node_srv_poll_init(&p);
    node_srv_row_t first;
    (void)take_all(&p, 0u, false, &first);
    CHECK_EQ(node_srv_poll_next(&p, 10u, false), NODE_SRV_ROW_NONE);
    node_srv_poll_now(&p, NODE_SRV_ROW_CONTROL);
    node_srv_poll_now(&p, NODE_SRV_ROW_NONE);
    node_srv_poll_taken(&p, NODE_SRV_ROW_NONE, 10u);
    CHECK_EQ(node_srv_poll_next(&p, 10u, false), NODE_SRV_ROW_CONTROL);
}

/* ------------------------------------------------------- the receiver */

/**
 * @brief Feed @p n wire bytes; count the frames handed out.
 * @param rx      The decoder.
 * @param b       The bytes.
 * @param n       Their count.
 * @param refused The refused-frame counter.
 * @param last    Receives the last frame handed out.
 * @return The frames.
 */
static unsigned feed(link_uart_rx_t *rx, const uint8_t *b, size_t n,
                     uint32_t *refused, link_can_frame_t *last)
{
    unsigned frames = 0u;
    for (size_t i = 0; i < n; ++i) {
        if (link_uart_feed(rx, b[i], last, refused)) {
            ++frames;
        }
    }
    return frames;
}

/** @brief The first all-off write's ACK as the IOmcu sends it: one frame. */
static size_t first_ack(uint8_t *wire, size_t cap)
{
    link_msg_t ack;
    memset(&ack, 0, sizeof ack);
    ack.op = LINK_OP_ACK;
    ack.page = LINK_PAGE_CONTROL;
    ack.offset = 0u;
    ack.count = 2u;
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    CHECK_EQ(link_can_encode(&ack, f, LINK_CAN_MAX_FRAMES), 1u);
    return link_uart_encode(&f[0], wire, cap);
}

TEST_CASE(the_first_answer_after_the_start_is_taken)
{
    uint8_t wire[LINK_UART_WIRE_MAX];
    const size_t n = first_ack(wire, sizeof wire);
    CHECK(n > 0u);
    link_uart_rx_t rx;
    uint32_t refused = 0u;
    link_can_frame_t f;
    node_srv_rx_start(&rx);
    CHECK_EQ(feed(&rx, wire, n, &refused, &f), 1u);
    CHECK_EQ(refused, 0u);
    CHECK_EQ(rx.counts.frames, 1u);
    link_msg_t m;
    CHECK(link_can_decode(&f, &m));
    CHECK_EQ(m.op, LINK_OP_ACK);
    /* Without the start's 0x00 the same answer is lost: the failure this
     * guards. */
    link_uart_rx_t cold;
    link_uart_rx_init(&cold);
    CHECK_EQ(feed(&cold, wire, n, &refused, &f), 0u);
}

TEST_CASE(noise_before_the_first_answer_is_dropped_and_counted)
{
    uint8_t wire[64];
    static const uint8_t noise[] = { 0x55u, 0xAAu, 0x13u, 0x37u, 0x42u };
    memcpy(wire, noise, sizeof noise);
    const size_t n = first_ack(wire + sizeof noise, sizeof wire - sizeof noise);
    link_uart_rx_t rx;
    uint32_t refused = 0u;
    link_can_frame_t f;
    node_srv_rx_start(&rx);
    /* The noise runs into the answer's bytes: one refused frame at the
     * answer's 0x00, and the next answer is taken. */
    CHECK_EQ(feed(&rx, wire, sizeof noise + n, &refused, &f), 0u);
    CHECK_EQ(refused, 1u);
    CHECK_EQ(feed(&rx, wire + sizeof noise, n, &refused, &f), 1u);
    CHECK_EQ(refused, 1u);
    /* Noise ended by its own 0x00 is one refused frame and costs no answer. */
    node_srv_rx_start(&rx);
    refused = 0u;
    static const uint8_t ended[] = { 0x55u, 0xAAu, 0x00u };
    CHECK_EQ(feed(&rx, ended, sizeof ended, &refused, &f), 0u);
    CHECK_EQ(refused, 1u);
    CHECK_EQ(feed(&rx, wire + sizeof noise, n, &refused, &f), 1u);
}

int main(void)
{
    RUN(the_table_reads_whole_pages_at_their_periods);
    RUN(every_row_is_due_at_the_start_control_first);
    RUN(each_row_comes_at_its_period_and_not_a_ms_before);
    RUN(the_periods_hold_across_the_wrap);
    RUN(fast_reads_status_every_50ms_at_once);
    RUN(a_stalled_link_comes_back_with_one_exchange_a_row);
    RUN(now_makes_a_row_due_at_once_and_ignores_none);
    RUN(the_first_answer_after_the_start_is_taken);
    RUN(noise_before_the_first_answer_is_dropped_and_counted);
    return test_summary("node_srv_poll");
}
