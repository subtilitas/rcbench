/*
 * The wire transaction and protocol A's frames (protocols/escwire/ew_wire.c,
 * ew_a.c).
 *
 * Under test: every CRC of the specification's vectors and the property that a
 * message with its own CRC sums to 0; exchanges 1 to 5 byte for byte; the
 * limits of a read and an announcement; each answer byte; every refusal of the
 * answer judges; the transaction's limits at both ends; its longest time; the
 * clock across its wrap.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ew_a.h"

typedef struct {
    const uint8_t *in;
    size_t n;
    uint16_t crc;
} crc_row_t;

#define ROW(crc, ...)                                                         \
    { (const uint8_t[]){ __VA_ARGS__ },                                       \
      sizeof((const uint8_t[]){ __VA_ARGS__ }), (crc) }

static const crc_row_t k_crc[] = {
    ROW(0x7DF4u, 0x42, 0x4C, 0x48, 0x65, 0x6C, 0x69),
    ROW(0x743Bu, 0xFF, 0x00, 0x1A, 0x00),
    ROW(0xCC31u, 0xFF, 0x00, 0x00, 0x20),
    ROW(0xD438u, 0xFF, 0x00, 0x1C, 0x00),
    ROW(0x1401u, 0x03, 0x70),
    ROW(0xA000u, 0x03, 0xC0),
    ROW(0xA0C0u, 0x02, 0x01),
    ROW(0x50C0u, 0x01, 0x01),
    ROW(0x2B30u, 0xFE, 0x00, 0x00, 0x04),
    ROW(0x0FA1u, 0x01, 0x02, 0x03, 0x04),
    ROW(0x0000u, 0x00, 0x00),
    ROW(0xC0C1u, 0x00, 0x01),
    ROW(0x7106u, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07),
};

static bool bytes_are(const uint8_t *p, size_t n, const uint8_t *want,
                      size_t want_n)
{
    return n == want_n && memcmp(p, want, n) == 0;
}

#define IS(p, n, ...)                                                         \
    CHECK(bytes_are((p), (n), (const uint8_t[]){ __VA_ARGS__ },               \
                    sizeof((const uint8_t[]){ __VA_ARGS__ })))

TEST_CASE(every_crc_of_the_vectors)
{
    for (size_t i = 0; i < sizeof(k_crc) / sizeof(k_crc[0]); ++i) {
        uint8_t m[16];

        CHECK_EQ(ew_crc16(k_crc[i].in, k_crc[i].n), k_crc[i].crc);
        memcpy(m, k_crc[i].in, k_crc[i].n);
        m[k_crc[i].n] = (uint8_t)(k_crc[i].crc & 0xFFu);
        m[k_crc[i].n + 1u] = (uint8_t)(k_crc[i].crc >> 8);
        CHECK_EQ(ew_crc16(m, k_crc[i].n + 2u), 0);
    }
    CHECK_EQ(ew_crc16(NULL, 0u), 0);
}

TEST_CASE(the_exchanges_byte_for_byte)
{
    uint8_t f[32];
    size_t n;

    /* 1: first contact. */
    n = ew_a_contact(f);
    IS(f, n, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x42, 0x4C,
       0x48, 0x65, 0x6C, 0x69, 0xF4, 0x7D);
    /* 2: read 112 bytes. */
    n = ew_a_address(f, 0x1A00u);
    IS(f, n, 0xFF, 0x00, 0x1A, 0x00, 0x3B, 0x74);
    n = ew_a_read(f, 0x70u);
    IS(f, n, 0x03, 0x70, 0x01, 0x14);
    /* 3: write four bytes. */
    n = ew_a_short(f, EW_A_CMD_ERASE, 1u);
    IS(f, n, 0x02, 0x01, 0xC0, 0xA0);
    n = ew_a_announce(f, 4u);
    IS(f, n, 0xFE, 0x00, 0x00, 0x04, 0x30, 0x2B);
    n = ew_a_data(f, (const uint8_t[]){ 1, 2, 3, 4 }, 4u);
    IS(f, n, 0x01, 0x02, 0x03, 0x04, 0xA1, 0x0F);
    n = ew_a_short(f, EW_A_CMD_PROGRAM, 1u);
    IS(f, n, 0x01, 0x01, 0xC0, 0x50);
    /* 4: AM32's read. */
    n = ew_a_address(f, 0x0020u);
    IS(f, n, 0xFF, 0x00, 0x00, 0x20, 0x31, 0xCC);
    n = ew_a_read(f, 192u);
    IS(f, n, 0x03, 0xC0, 0x00, 0xA0);
    /* 5: leave. */
    n = ew_a_short(f, EW_A_CMD_LEAVE, 1u);
    IS(f, n, 0x00, 0x01, 0xC1, 0xC0);
    n = ew_a_short(f, EW_A_CMD_LEAVE, 0u);
    IS(f, n, 0x00, 0x00, 0x00, 0x00);
}

TEST_CASE(the_limits_of_a_read_and_an_announcement)
{
    uint8_t f[8];

    CHECK_EQ(ew_a_read(f, 0u), 0u);
    CHECK_EQ(ew_a_read(f, 1u), 4u);
    CHECK_EQ(f[1], 1u);
    CHECK_EQ(ew_a_read(f, 255u), 4u);
    CHECK_EQ(f[1], 255u);
    CHECK_EQ(ew_a_read(f, 256u), 4u);
    CHECK_EQ(f[1], 0u);
    CHECK_EQ(ew_a_read(f, 257u), 0u);
    CHECK_EQ(ew_a_announce(f, 0u), 0u);
    CHECK_EQ(ew_a_announce(f, 1u), 6u);
    CHECK_EQ(ew_a_announce(f, 256u), 6u);
    CHECK_EQ(f[2], 1u);
    CHECK_EQ(f[3], 0u);
    CHECK_EQ(ew_a_announce(f, 257u), 0u);
}

TEST_CASE(a_one_byte_answer_for_each_byte)
{
    ew_capture_t c = { .n = 1u };
    uint8_t b;

    b = 0x30u; CHECK_EQ(ew_a_one(&c, &b), EW_ANS_OK);
    b = 0xC0u; CHECK_EQ(ew_a_one(&c, &b), EW_ANS_VERIFY);
    b = 0xC1u; CHECK_EQ(ew_a_one(&c, &b), EW_ANS_UNKNOWN);
    b = 0xC2u; CHECK_EQ(ew_a_one(&c, &b), EW_ANS_BAD_CRC);
    b = 0xC5u; CHECK_EQ(ew_a_one(&c, &b), EW_ANS_REFUSED);
    b = 0x31u; CHECK_EQ(ew_a_one(&c, &b), EW_ANS_OTHER);
    c.n = 2u;
    CHECK_EQ(ew_a_one(&c, &b), EW_ANS_LENGTH);
    c.n = 0u;
    CHECK_EQ(ew_a_one(&c, &b), EW_ANS_SILENT);
    c.n = 1u;
    c.bad_stop = 1u;
    CHECK_EQ(ew_a_one(&c, &b), EW_ANS_FRAMING);
    c.bad_stop = 0u;
    c.aborted = true;
    CHECK_EQ(ew_a_one(&c, &b), EW_ANS_ABORTED);
    c.aborted = false;
    c.overflow = true;
    CHECK_EQ(ew_a_one(&c, &b), EW_ANS_ABORTED);
}

TEST_CASE(a_read_answer_is_data_crc_and_the_ack)
{
    /* The specification's constructed read reply. */
    uint8_t r[11] = { 0, 1, 2, 3, 4, 5, 6, 7, 0x06, 0x71, 0x30 };
    ew_capture_t c = { .n = 11u };

    CHECK_EQ(ew_a_read_reply(&c, r, 8u), EW_ANS_OK);
    r[10] = 0x31u;
    CHECK_EQ(ew_a_read_reply(&c, r, 8u), EW_ANS_NO_ACK);
    r[10] = 0x30u;
    r[9] = 0x70u;
    CHECK_EQ(ew_a_read_reply(&c, r, 8u), EW_ANS_CRC);
    r[9] = 0x71u;
    c.n = 10u;
    CHECK_EQ(ew_a_read_reply(&c, r, 8u), EW_ANS_LENGTH);
    c.n = 12u;
    CHECK_EQ(ew_a_read_reply(&c, r, 8u), EW_ANS_LENGTH);
    c.n = 0u;
    CHECK_EQ(ew_a_read_reply(&c, r, 8u), EW_ANS_SILENT);
    /* One byte in place of the reply. */
    c.n = 1u;
    r[0] = 0xC1u;
    CHECK_EQ(ew_a_read_reply(&c, r, 8u), EW_ANS_UNKNOWN);
    r[0] = 0x30u;
    CHECK_EQ(ew_a_read_reply(&c, r, 8u), EW_ANS_LENGTH);
}

TEST_CASE(a_contact_answer_is_nine_bytes_with_471)
{
    uint8_t a[9] = { 0x34, 0x37, 0x31, 0x64, 0xE8, 0xB2, 0x06, 0x01, 0x30 };
    ew_capture_t c = { .n = 9u };

    CHECK_EQ(ew_a_contact_reply(&c, a), EW_ANS_OK);
    for (int i = 0; i < 3; ++i) {
        a[i] ^= 0x01u;
        CHECK_EQ(ew_a_contact_reply(&c, a), EW_ANS_OTHER);
        a[i] ^= 0x01u;
    }
    c.n = 8u;
    CHECK_EQ(ew_a_contact_reply(&c, a), EW_ANS_LENGTH);
    c.n = 0u;
    CHECK_EQ(ew_a_contact_reply(&c, a), EW_ANS_SILENT);
}

static ew_xfer_t good_xfer(const uint8_t *tx, uint8_t *rx)
{
    const ew_xfer_t x = { .rate = EW_RATE_A, .n_tx = 4u, .max_rx = 1u,
                          .first_us = 4000u, .gap_us = 2000u, .tx = tx,
                          .rx = rx };

    return x;
}

TEST_CASE(a_transaction_at_and_past_each_limit)
{
    const uint8_t tx[EW_TX_MAX] = { 0 };
    uint8_t rx[EW_RX_MAX];
    ew_xfer_t x = good_xfer(tx, rx);

    CHECK(ew_xfer_valid(&x));
    CHECK(!ew_xfer_valid(NULL));
    x.rate = EW_RATE_TEXT;
    CHECK(ew_xfer_valid(&x));
    x.rate = EW_RATES;
    CHECK(!ew_xfer_valid(&x));
    x = good_xfer(tx, rx);
    x.tx = NULL;
    CHECK(!ew_xfer_valid(&x));
    x = good_xfer(tx, rx);
    x.n_tx = 0u;
    CHECK(!ew_xfer_valid(&x));
    x.n_tx = EW_TX_MAX;
    CHECK(ew_xfer_valid(&x));
    x.n_tx = EW_TX_MAX + 1u;
    CHECK(!ew_xfer_valid(&x));
    x = good_xfer(tx, rx);
    x.n_head = 2u;
    x.pause_bits = EW_PAUSE_BITS_MAX;
    CHECK(ew_xfer_valid(&x));
    x.pause_bits = EW_PAUSE_BITS_MAX + 1u;
    CHECK(!ew_xfer_valid(&x));
    x.pause_bits = 1u;
    x.n_head = 0u;
    CHECK(!ew_xfer_valid(&x));
    x.pause_bits = 0u;
    x.n_head = 4u;
    CHECK(!ew_xfer_valid(&x));
    x = good_xfer(tx, rx);
    x.max_rx = EW_RX_MAX;
    CHECK(ew_xfer_valid(&x));
    x.max_rx = EW_RX_MAX + 1u;
    CHECK(!ew_xfer_valid(&x));
    x = good_xfer(tx, rx);
    x.rx = NULL;
    CHECK(!ew_xfer_valid(&x));
    x = good_xfer(tx, rx);
    x.first_us = 0u;
    CHECK(!ew_xfer_valid(&x));
    x.first_us = EW_FIRST_US_MAX;
    CHECK(ew_xfer_valid(&x));
    x.first_us = EW_FIRST_US_MAX + 1u;
    CHECK(!ew_xfer_valid(&x));
    x = good_xfer(tx, rx);
    x.gap_us = 0u;
    CHECK(!ew_xfer_valid(&x));
    x.gap_us = EW_GAP_US_MAX;
    CHECK(ew_xfer_valid(&x));
    x.gap_us = EW_GAP_US_MAX + 1u;
    CHECK(!ew_xfer_valid(&x));
    /* No receive: no buffer and no waits. */
    x = good_xfer(tx, NULL);
    x.max_rx = 0u;
    CHECK(!ew_xfer_valid(&x));
    x.first_us = 0u;
    CHECK(!ew_xfer_valid(&x));
    x.gap_us = 0u;
    CHECK(ew_xfer_valid(&x));
    x.rx = rx;
    CHECK(!ew_xfer_valid(&x));
    /* The largest pause reaches the 500us that serve every firmware. */
    CHECK(EW_PAUSE_BITS_MAX * EW_BIT_A_NS >= 500000u);
}

TEST_CASE(the_longest_time_of_a_transaction)
{
    const uint8_t tx[4] = { 0 };
    uint8_t rx[3];
    ew_xfer_t x = good_xfer(tx, rx);

    /* 4 bytes sent, 1 received: 52 bits at 52us, and the first wait. */
    CHECK_EQ(ew_xfer_span_us(&x), 52u * 52u + 4000u);
    x.max_rx = 3u;
    /* 76 bits, the first wait and 2 gaps. */
    CHECK_EQ(ew_xfer_span_us(&x), 76u * 52u + 4000u + 2u * 2000u);
    x.max_rx = 0u;
    x.pause_bits = 2u;
    CHECK_EQ(ew_xfer_span_us(&x), 42u * 52u);
    x.rate = EW_RATE_TEXT;
    x.pause_bits = 0u;
    /* 40 bits at 26.042us, rounded up. */
    CHECK_EQ(ew_xfer_span_us(&x), 1042u);
}

TEST_CASE(the_clock_across_its_wrap)
{
    CHECK_EQ(ew_elapsed_us(5u, 0xFFFFFFFBu), 10u);
    CHECK(!ew_due(5u, 0xFFFFFFFBu, 10u));
    CHECK(ew_due(6u, 0xFFFFFFFBu, 10u));
    CHECK(!ew_due(100u, 100u, 0u));
    CHECK(ew_due(101u, 100u, 0u));
}

int main(void)
{
    RUN(every_crc_of_the_vectors);
    RUN(the_exchanges_byte_for_byte);
    RUN(the_limits_of_a_read_and_an_announcement);
    RUN(a_one_byte_answer_for_each_byte);
    RUN(a_read_answer_is_data_crc_and_the_ack);
    RUN(a_contact_answer_is_nine_bytes_with_471);
    RUN(a_transaction_at_and_past_each_limit);
    RUN(the_longest_time_of_a_transaction);
    RUN(the_clock_across_its_wrap);
    return test_summary("ew_a");
}
