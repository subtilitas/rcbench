/**
 * @file test_scan_port.c
 * @brief shared/scanner's port: lines to the queue, frames to the
 *        sequencer, the rate the UART is asked to open at, the queue of 4
 *        dropping its oldest, lost input (a flush or a UART error)
 *        dropping the open line and every byte up to 100ms of quiet, a line
 *        past 64 bytes, a stopped port, the console's injected line, the
 *        gap that drops a partial line, and NULL.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "scan_port.h"

/** @brief The port, off the stack. */
static scan_port_t s_p;

/**
 * @brief Feed a string's bytes.
 * @param s   The text.
 * @param now The clock.
 */
static void feed_str(const char *s, uint32_t now)
{
    scan_port_rx(&s_p, (const uint8_t *)s, strlen(s), now);
}

/**
 * @brief Build an answer frame 02 00 00 len data CRC.
 * @param out  Receives the frame.
 * @param data The data.
 * @param n    Its length.
 * @return The frame's length.
 */
static size_t answer(uint8_t *out, const uint8_t *data, uint8_t n)
{
    out[0] = 0x02u;
    out[1] = 0x00u;
    out[2] = 0x00u;
    out[3] = n;
    memcpy(out + 4, data, n);
    const uint16_t crc = gm861_crc(out + 2, 2u + n);
    out[4 + n] = (uint8_t)(crc >> 8);
    out[5 + n] = (uint8_t)crc;
    return 6u + n;
}

/**
 * @brief Step the port from @p from to @p to with RX high, keeping the
 *        last request sent.
 * @param from The first ms.
 * @param to   The ms after the last.
 * @param tx   Receives the last request; GM861_REQ_LEN bytes.
 * @param reopen Receives the last rate asked, 0 none.
 * @return Requests sent.
 */
static unsigned run(uint32_t from, uint32_t to, uint8_t *tx, uint32_t *reopen)
{
    unsigned sent = 0u;
    uint8_t buf[GM861_REQ_LEN];
    for (uint32_t t = from; t != to; ++t) {
        uint32_t r = 0u;
        const size_t n = scan_port_step(&s_p, t, true, buf, sizeof buf, &r);
        if (r != 0u && reopen != NULL) {
            *reopen = r;
        }
        if (n != 0u) {
            ++sent;
            if (tx != NULL) {
                memcpy(tx, buf, n);
            }
        }
    }
    return sent;
}

TEST_CASE(a_started_port_asks_for_9600_once_and_probes)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    uint32_t r = 0u;
    uint8_t buf[GM861_REQ_LEN];
    CHECK_EQ(scan_port_step(&s_p, 0u, true, buf, sizeof buf, &r), 0u);
    CHECK_EQ(r, 9600u);
    CHECK_EQ(scan_port_step(&s_p, 1u, true, buf, sizeof buf, &r), 0u);
    CHECK_EQ(r, 0u);
    uint8_t tx[GM861_REQ_LEN] = { 0 };
    CHECK_EQ(run(2u, 320u, tx, NULL), 1u);
    /* the probe: read zone 0x0000, 1 byte */
    static const uint8_t probe[GM861_REQ_LEN] = { 0x7E, 0x00, 0x07, 0x01,
                                                  0x00, 0x00, 0x01, 0x01,
                                                  0x41 };
    CHECK(memcmp(tx, probe, sizeof probe) == 0);
}

TEST_CASE(an_unanswered_probe_asks_for_the_next_rate)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    uint32_t r = 0u;
    (void)run(0u, 320u, NULL, &r);
    CHECK_EQ(r, 9600u);
    r = 0u;
    (void)run(320u, 700u, NULL, &r);
    CHECK_EQ(r, gm861_rates()[1]);
    CHECK_EQ(s_p.baud, gm861_rates()[1]);
}

TEST_CASE(an_answer_frame_reaches_the_sequencer_and_not_the_queue)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    (void)run(0u, 320u, NULL, NULL);
    const uint8_t zone0 = 0x83u;
    uint8_t f[16];
    const size_t n = answer(f, &zone0, 1u);
    scan_port_rx(&s_p, f, n, 330u);
    CHECK_EQ(s_p.q_n, 0u);
    /* the next request reads the 97-byte block */
    uint8_t tx[GM861_REQ_LEN] = { 0 };
    CHECK(run(331u, 400u, tx, NULL) >= 1u);
    CHECK_EQ(tx[2], GM861_T_READ);
    CHECK_EQ(tx[6], 0x61u);
    CHECK(gm861_state(&s_p.g) != GM861_NO_ANSWER);
}

TEST_CASE(lines_go_to_the_queue_in_order)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    feed_str("KST_X10-0000A\r\n", 10u);
    feed_str("SERVO-ZZZZZ\n", 20u);
    scan_event_t e;
    CHECK(scan_port_take(&s_p, &e));
    CHECK_EQ(e.kind, SCAN_EV_LINE);
    CHECK_STR_EQ(e.text, "KST_X10-0000A");
    CHECK_EQ(e.at_ms, 10u);
    CHECK(scan_port_take(&s_p, &e));
    CHECK_STR_EQ(e.text, "SERVO-ZZZZZ");
    CHECK(!scan_port_take(&s_p, &e));
}

TEST_CASE(a_full_queue_drops_its_oldest_and_counts_it)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    const char *lines[6] = { "A-00001\r", "A-00002\r", "A-00003\r",
                             "A-00004\r", "A-00005\r", "A-00006\r" };
    for (unsigned i = 0; i < 6u; ++i) {
        feed_str(lines[i], 10u * i);
    }
    CHECK_EQ(s_p.q_n, SCAN_PORT_QUEUE);
    CHECK_EQ(s_p.drops, 2u);
    scan_event_t e;
    CHECK(scan_port_take(&s_p, &e));
    CHECK_STR_EQ(e.text, "A-00003");
    CHECK(scan_port_take(&s_p, &e));
    CHECK(scan_port_take(&s_p, &e));
    CHECK(scan_port_take(&s_p, &e));
    CHECK_STR_EQ(e.text, "A-00006");
    CHECK(!scan_port_take(&s_p, &e));
}

/* Half a line, the input flushed, then the other half of another id: no
 * line comes out of the two halves, nor of the second half alone; the
 * next line after 100ms of quiet comes out whole. */
TEST_CASE(a_flush_mid_line_joins_nothing_and_drops_the_suffix)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    feed_str("KST-", 10u);
    scan_port_flush(&s_p, 11u);
    feed_str("00002\r", 20u);
    feed_str("KST-00003\r", 30u);
    scan_event_t e;
    CHECK(!scan_port_take(&s_p, &e));
    CHECK_EQ(s_p.flushes, 1u);
    CHECK_EQ(s_p.discarded, 16u);
    /* 100ms without a byte ends the discard; the step sees it too */
    (void)run(31u, 131u, NULL, NULL);
    CHECK(!s_p.discard);
    feed_str("KST-00004\r", 140u);
    CHECK(scan_port_take(&s_p, &e));
    CHECK_STR_EQ(e.text, "KST-00004");
    CHECK(!scan_port_take(&s_p, &e));
}

/* A flush between two whole lines: the first stays queued, the second,
 * after 100ms of quiet, comes out whole.  A byte at 99ms still counts as
 * the burst that lost bytes. */
TEST_CASE(a_flush_between_whole_lines_leaves_both_whole)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    feed_str("KST-00001\r", 10u);
    scan_port_flush(&s_p, 11u);
    feed_str("K", 110u);
    CHECK_EQ(s_p.discarded, 1u);
    feed_str("KST-00002\r", 210u);
    scan_event_t e;
    CHECK(scan_port_take(&s_p, &e));
    CHECK_STR_EQ(e.text, "KST-00001");
    CHECK(scan_port_take(&s_p, &e));
    CHECK_STR_EQ(e.text, "KST-00002");
    CHECK(!scan_port_take(&s_p, &e));
}

/* A flush inside an answer frame drops the frame: the sequencer never sees
 * its head joined to later bytes. */
TEST_CASE(a_flush_mid_frame_drops_the_frame)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    (void)run(0u, 320u, NULL, NULL);
    const uint8_t zone0 = 0x83u;
    uint8_t f[16];
    const size_t n = answer(f, &zone0, 1u);
    scan_port_rx(&s_p, f, 3u, 321u);
    scan_port_flush(&s_p, 322u);
    scan_port_rx(&s_p, f + 3, n - 3u, 323u);
    CHECK_EQ(scan_line_counts(&s_p.line).frames, 0u);
    CHECK_EQ(s_p.discarded, n - 3u);
}

/* A change of rate drops the open line: bytes read at the old rate never
 * join bytes at the new one. */
TEST_CASE(a_new_rate_drops_the_open_line)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    (void)run(0u, 320u, NULL, NULL);
    feed_str("KST-", 550u);
    uint32_t r = 0u;
    uint32_t t = 551u;
    uint8_t buf[GM861_REQ_LEN];
    while (r == 0u && t < 1000u) {
        (void)scan_port_step(&s_p, t++, true, buf, sizeof buf, &r);
    }
    CHECK_EQ(r, gm861_rates()[1]);
    CHECK(t < 650u);
    feed_str("00002\r", t);
    scan_event_t e;
    CHECK(scan_port_take(&s_p, &e));
    CHECK_STR_EQ(e.text, "00002");
    CHECK(!s_p.discard);
}

/* A UART error is lost input: the line the bad byte falls in, open or
 * still to come in the same burst, is dropped, and counted. */
TEST_CASE(a_uart_error_drops_the_burst_it_falls_in)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    /* the bad byte read already, its line open */
    feed_str("KST-0", 10u);
    scan_port_error(&s_p, 11u);
    feed_str("0001\r", 12u);
    /* the bad byte still to come, after a line end in the same burst:
     * the X of the second line stands for a printable bad byte */
    feed_str("KST-00005\r", 300u);
    scan_port_error(&s_p, 301u);
    feed_str("KST-00007\rKST-0X008\r", 302u);
    CHECK_EQ(s_p.errors, 2u);
    CHECK_EQ(s_p.flushes, 2u);
    scan_event_t e;
    CHECK(scan_port_take(&s_p, &e));
    CHECK_STR_EQ(e.text, "KST-00005");
    CHECK(!scan_port_take(&s_p, &e));
}

TEST_CASE(a_line_past_64_bytes_is_handed_out_as_long)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    char line[80];
    memset(line, 'X', 70);
    line[70] = '\r';
    line[71] = '\0';
    feed_str(line, 10u);
    scan_event_t e;
    CHECK(scan_port_take(&s_p, &e));
    CHECK_EQ(e.kind, SCAN_EV_LONG);
    CHECK_EQ(strlen(e.text), SCAN_LINE_MAX);
}

TEST_CASE(a_partial_line_is_dropped_after_100ms_by_the_step)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    (void)run(0u, 1u, NULL, NULL);   /* the UART opened at 9600 */
    feed_str("KST-000", 10u);
    (void)run(11u, 111u, NULL, NULL);
    CHECK_EQ(scan_line_counts(&s_p.line).partial, 1u);
    feed_str("01\r", 111u);
    scan_event_t e;
    CHECK(scan_port_take(&s_p, &e));
    CHECK_STR_EQ(e.text, "01");
}

TEST_CASE(a_stopped_port_takes_and_sends_nothing_and_keeps_its_queue)
{
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_CONTINUOUS);
    feed_str("A-00001\r", 1u);
    scan_port_stop(&s_p);
    CHECK_EQ(gm861_state(&s_p.g), GM861_OFF);
    feed_str("A-00002\r", 2u);
    scan_port_error(&s_p, 3u);
    scan_port_flush(&s_p, 3u);
    CHECK_EQ(s_p.errors, 0u);
    CHECK_EQ(s_p.flushes, 0u);
    uint32_t r = 7u;
    CHECK_EQ(run(0u, 1000u, NULL, &r), 0u);
    CHECK_EQ(r, 7u);
    scan_port_set_mode(&s_p, GM861_MODE_SENSING);
    CHECK_EQ(gm861_state(&s_p.g), GM861_OFF);
    scan_event_t e;
    CHECK(scan_port_take(&s_p, &e));
    CHECK_STR_EQ(e.text, "A-00001");
    CHECK(!scan_port_take(&s_p, &e));
    /* started again: 9600 asked again */
    scan_port_start(&s_p, GM861_MODE_SENSING);
    uint8_t buf[GM861_REQ_LEN];
    (void)scan_port_step(&s_p, 2000u, true, buf, sizeof buf, &r);
    CHECK_EQ(r, 9600u);
}

TEST_CASE(an_injected_line_is_queued_as_a_scan)
{
    scan_port_init(&s_p);
    CHECK(scan_port_inject(&s_p, "KST X10-0000A", 5u));
    char long_text[SCAN_LINE_MAX + 2u];
    memset(long_text, 'A', SCAN_LINE_MAX + 1u);
    long_text[SCAN_LINE_MAX + 1u] = '\0';
    CHECK(!scan_port_inject(&s_p, long_text, 6u));
    long_text[SCAN_LINE_MAX] = '\0';
    CHECK(scan_port_inject(&s_p, long_text, 7u));
    CHECK(!scan_port_inject(&s_p, "", 8u));
    CHECK(!scan_port_inject(&s_p, "A\tB", 8u));
    CHECK(!scan_port_inject(&s_p, NULL, 8u));
    CHECK(!scan_port_inject(NULL, "A", 8u));
    scan_event_t e;
    CHECK(scan_port_take(&s_p, &e));
    CHECK_EQ(e.kind, SCAN_EV_LINE);
    CHECK_STR_EQ(e.text, "KST X10-0000A");
    CHECK_EQ(e.at_ms, 5u);
    CHECK(scan_port_take(&s_p, &e));
    CHECK_EQ(strlen(e.text), SCAN_LINE_MAX);
}

TEST_CASE(null_is_taken_everywhere)
{
    scan_port_init(NULL);
    scan_port_start(NULL, GM861_MODE_SENSING);
    scan_port_stop(NULL);
    scan_port_set_mode(NULL, GM861_MODE_SENSING);
    scan_port_rx(NULL, (const uint8_t *)"A", 1u, 0u);
    scan_port_error(NULL, 0u);
    scan_port_flush(NULL, 0u);
    uint32_t r = 5u;
    uint8_t buf[GM861_REQ_LEN];
    CHECK_EQ(scan_port_step(NULL, 0u, true, buf, sizeof buf, &r), 0u);
    CHECK_EQ(r, 0u);
    scan_event_t e;
    CHECK(!scan_port_take(NULL, &e));
    scan_port_init(&s_p);
    scan_port_start(&s_p, GM861_MODE_SENSING);
    scan_port_rx(&s_p, NULL, 4u, 0u);
    CHECK(!scan_port_take(&s_p, NULL));
    CHECK_EQ(scan_port_step(&s_p, 0u, true, buf, sizeof buf, NULL), 0u);
}

int main(void)
{
    RUN(a_started_port_asks_for_9600_once_and_probes);
    RUN(an_unanswered_probe_asks_for_the_next_rate);
    RUN(an_answer_frame_reaches_the_sequencer_and_not_the_queue);
    RUN(lines_go_to_the_queue_in_order);
    RUN(a_full_queue_drops_its_oldest_and_counts_it);
    RUN(a_flush_mid_line_joins_nothing_and_drops_the_suffix);
    RUN(a_flush_between_whole_lines_leaves_both_whole);
    RUN(a_flush_mid_frame_drops_the_frame);
    RUN(a_new_rate_drops_the_open_line);
    RUN(a_uart_error_drops_the_burst_it_falls_in);
    RUN(a_line_past_64_bytes_is_handed_out_as_long);
    RUN(a_partial_line_is_dropped_after_100ms_by_the_step);
    RUN(a_stopped_port_takes_and_sends_nothing_and_keeps_its_queue);
    RUN(an_injected_line_is_queued_as_a_scan);
    RUN(null_is_taken_everywhere);
    return test_summary("scan_port");
}
