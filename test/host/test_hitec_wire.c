/*
 * The Hitec protocol core without a wire (protocols/hitec/hitec_wire.c):
 * the frames of both families, the transactions, the reply decoders and
 * the clock helpers.
 *
 * The frames and reply times in the tables are read off bench captures of
 * an HPP-21 Plus and an HFP-30 with an HS-5645MG, an HS-7955TG and a
 * D485HW: the command, the reply and the reply's first start bit from the
 * start of the command's last byte (classic), the answer and its start from
 * the end of the request (D series).
 *
 * Under test:
 *  - the sums: every published example, the bench frames, the classic sum
 *    over every pair of parameters, a frame with one byte changed;
 *  - the transactions the builders make and every limit of a valid one;
 *  - each decoder's every refusal, the accept windows and the byte spacing
 *    at both edges, the trailing byte and the break of a D series answer;
 *  - the elapsed time and the due test across the wrap of the clock.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "hitec_wire.h"

/* ------------------------------------------------------------- helpers */

static const uint32_t C_BIT = 52083u;   /* ns, rounded */
static const uint32_t C_ARM = 468750u;  /* 9 bit times */
static const uint32_t D_BIT = 8681u;

static hitec_capture_t cap;

static void cap_clear(uint8_t family)
{
    memset(&cap, 0, sizeof(cap));
    cap.family = family;
}

static void cap_byte(uint32_t at_ns, uint8_t value)
{
    cap.byte[cap.n].at_ns = at_ns;
    cap.byte[cap.n].value = value;
    cap.byte[cap.n].framing = 0;
    cap.n++;
}

/* A classic reply: its first start bit @p delay_ns after the start of the
 * master's last byte, the second @p spacing_ns after the first. */
static void classic_reply(uint32_t delay_ns, uint32_t spacing_ns, uint8_t a,
                          uint8_t b)
{
    cap_clear(HITEC_CLASSIC);
    cap_byte(delay_ns - C_ARM, a);
    cap_byte(delay_ns - C_ARM + spacing_ns, b);
}

/* A D series answer @p delay_ns after the end of the request, the bytes
 * back to back, and the byte 0x00 after them. */
static void d_answer(uint32_t delay_ns, const uint8_t *a, bool trailing)
{
    cap_clear(HITEC_D);
    for (unsigned i = 0; i < HITEC_D_ANSWER_LEN; ++i) {
        cap_byte(delay_ns - D_BIT + i * 86806u, a[i]);
    }
    if (trailing) {
        cap_byte(delay_ns - D_BIT + 7u * 86806u, 0x00);
    }
}

/* ---------------------------------------------------------------- cases */

TEST_CASE(the_classic_sum_makes_every_frame_sum_to_zero)
{
    static const uint8_t bench[][4] = {
        { 0x67, 0x00, 0x00, 0x99 }, { 0x67, 0x89, 0x74, 0x9C },
        { 0x61, 0x00, 0x00, 0x9F }, { 0x61, 0x23, 0x00, 0x7C },
        { 0x62, 0x02, 0x01, 0x9B }, { 0x62, 0x1C, 0x8D, 0xF5 },
        { 0x64, 0x18, 0x01, 0x83 }, { 0x64, 0x96, 0xFF, 0x07 },
        { 0x65, 0x00, 0x00, 0x9B }, { 0x66, 0x17, 0x94, 0xEF },
        { 0x66, 0x5E, 0x50, 0xEC },
    };
    uint8_t f[HITEC_C_FRAME_LEN];

    for (unsigned i = 0; i < sizeof(bench) / sizeof(bench[0]); ++i) {
        CHECK_EQ(hitec_c_sum(bench[i][0], bench[i][1], bench[i][2]),
                 bench[i][3]);
        hitec_c_frame(bench[i][0], bench[i][1], bench[i][2], f);
        CHECK(memcmp(f, bench[i], 4) == 0);
        CHECK(hitec_c_frame_ok(bench[i]));
    }
    for (unsigned p1 = 0; p1 < 256u; ++p1) {
        for (unsigned p2 = 0; p2 < 256u; p2 += 7u) {
            hitec_c_frame(HITEC_C_WRITE_EEPROM, (uint8_t)p1, (uint8_t)p2, f);
            CHECK(hitec_c_frame_ok(f));
            CHECK_EQ((uint8_t)(f[0] + f[1] + f[2] + f[3]), 0u);
            f[2] ^= 0x01u;
            CHECK(!hitec_c_frame_ok(f));
        }
    }
}

TEST_CASE(the_d_sum_is_the_low_byte_of_every_byte_after_the_header)
{
    static const uint8_t reads[][5] = {
        { 0x96, 0x00, 0x0C, 0x00, 0x0C },
    };
    static const struct {
        uint8_t addr;
        uint16_t value;
        uint8_t frame[7];
    } writes[] = {
        { 0x1E, 3000u, { 0x96, 0x00, 0x1E, 0x02, 0xB8, 0x0B, 0xE3 } },
        { 0x32, 3u, { 0x96, 0x00, 0x32, 0x02, 0x03, 0x00, 0x37 } },
        { 0x6E, 0x0F0Fu, { 0x96, 0x00, 0x6E, 0x02, 0x0F, 0x0F, 0x8E } },
        { 0x70, 0xFFFFu, { 0x96, 0x00, 0x70, 0x02, 0xFF, 0xFF, 0x70 } },
        { 0x46, 1u, { 0x96, 0x00, 0x46, 0x02, 0x01, 0x00, 0x49 } },
        { 0x1E, 5262u, { 0x96, 0x00, 0x1E, 0x02, 0x8E, 0x14, 0xC2 } },
    };
    static const uint8_t answers[][7] = {
        { 0x69, 0x00, 0x0C, 0x02, 0x00, 0x20, 0x2E },
        { 0x69, 0x00, 0x0C, 0x02, 0xC3, 0x3B, 0x0C },
        { 0x69, 0x00, 0x0C, 0x02, 0xC2, 0x3B, 0x0B },
    };
    uint8_t f[HITEC_D_WRITE_LEN];

    hitec_d_read_frame(0x00, 0x0C, f);
    CHECK(memcmp(f, reads[0], 5) == 0);
    for (unsigned i = 0; i < sizeof(writes) / sizeof(writes[0]); ++i) {
        hitec_d_write_frame(0x00, writes[i].addr, writes[i].value, f);
        CHECK(memcmp(f, writes[i].frame, 7) == 0);
    }
    for (unsigned i = 0; i < sizeof(answers) / sizeof(answers[0]); ++i) {
        CHECK_EQ(hitec_d_sum(&answers[i][1], 5), answers[i][6]);
    }
    CHECK_EQ(hitec_d_sum(f, 0), 0u);
    /* The sum wraps: 5 bytes of 0xFF. */
    memset(f, 0xFF, sizeof(f));
    CHECK_EQ(hitec_d_sum(f, 5), 0xFBu);
}

TEST_CASE(the_bit_times_of_both_families)
{
    CHECK_EQ(hitec_bit_rate(HITEC_CLASSIC), 19200u);
    CHECK_EQ(hitec_bit_rate(HITEC_D), 115200u);
    CHECK_EQ(hitec_bit_rate(2), 0u);
    CHECK_EQ(hitec_bits_ns(HITEC_CLASSIC, 1), C_BIT);
    CHECK_EQ(hitec_bits_ns(HITEC_CLASSIC, HITEC_C_ARM_BITS), C_ARM);
    CHECK_EQ(hitec_bits_ns(HITEC_CLASSIC, 11), 572917u);
    CHECK_EQ(hitec_bits_ns(HITEC_D, 1), D_BIT);
    CHECK_EQ(hitec_bits_ns(HITEC_D, 50), 434028u);
    CHECK_EQ(hitec_bits_ns(7, 1), 0u);
    /* The accept limit is the middle of the master's stop bit. */
    CHECK_EQ((hitec_bits_ns(HITEC_CLASSIC, 19) + 1u) / 2u, HITEC_C_REPLY_MIN_NS);
}

TEST_CASE(the_builders_make_valid_transactions)
{
    hitec_xfer_t x;

    hitec_c_xfer(HITEC_C_READ_EEPROM, 0x23, 0x00, &x);
    CHECK(hitec_xfer_valid(&x));
    CHECK_EQ(x.kind, HITEC_XFER_SEND);
    CHECK_EQ(x.family, HITEC_CLASSIC);
    CHECK_EQ(x.n_tx, 4);
    CHECK_EQ(x.tx[3], 0x7C);
    CHECK_EQ(x.gap_bits, 1);
    CHECK_EQ(x.lead_us, 0u);
    CHECK_EQ(x.window_us, 1500u);
    CHECK_EQ(x.max_rx, 2);

    hitec_d_read_xfer(0x00, 0x0C, &x);
    CHECK(hitec_xfer_valid(&x));
    CHECK_EQ(x.family, HITEC_D);
    CHECK_EQ(x.n_tx, 5);
    CHECK_EQ(x.gap_bits, 0);
    CHECK_EQ(x.lead_us, 88u);
    CHECK_EQ(x.window_us, 25000u);
    CHECK_EQ(x.max_rx, 8);

    hitec_d_write_xfer(0x00, 0x70, 0xFFFF, &x);
    CHECK(hitec_xfer_valid(&x));
    CHECK_EQ(x.n_tx, 7);
    CHECK_EQ(x.tx[6], 0x70);
    CHECK_EQ(x.window_us, 0u);
    CHECK_EQ(x.max_rx, 0);

    CHECK(hitec_listen_xfer(HITEC_CLASSIC, 1000000u, 2, true, HITEC_C_ACK,
                            &x));
    CHECK_EQ(x.kind, HITEC_XFER_LISTEN);
    CHECK_EQ(x.answer, 1);
    CHECK_EQ(x.answer_byte, HITEC_C_ACK);
    CHECK(hitec_level_xfer(HITEC_CLASSIC, 0, 6700u, &x));
    CHECK_EQ(x.kind, HITEC_XFER_LEVEL);
    CHECK_EQ(x.level_us, 6700u);

    /* A refused build leaves the transaction as it was. */
    memset(&x, 0x5A, sizeof(x));
    CHECK(!hitec_listen_xfer(HITEC_CLASSIC, 0u, 1, false, 0, &x));
    CHECK(!hitec_listen_xfer(HITEC_CLASSIC, 1000u, 0, false, 0, &x));
    CHECK(!hitec_listen_xfer(HITEC_CLASSIC, 1000u, HITEC_RX_MAX + 1u, false,
                             0, &x));
    CHECK(!hitec_listen_xfer(HITEC_CLASSIC, HITEC_TIME_MAX_US + 1u, 1, false,
                             0, &x));
    CHECK(!hitec_listen_xfer(5, 1000u, 1, false, 0, &x));
    CHECK(!hitec_listen_xfer(HITEC_D, 1000u, 1, false, 0, NULL));
    CHECK(!hitec_level_xfer(HITEC_D, 2, 1000u, &x));
    CHECK(!hitec_level_xfer(HITEC_D, 0, 0u, &x));
    CHECK(!hitec_level_xfer(HITEC_D, 0, HITEC_TIME_MAX_US + 1u, &x));
    CHECK(!hitec_level_xfer(HITEC_D, 0, 1000u, NULL));
    CHECK_EQ(x.kind, 0x5A);
    CHECK(hitec_listen_xfer(HITEC_D, HITEC_TIME_MAX_US, HITEC_RX_MAX, false,
                            0, &x));
    CHECK(hitec_level_xfer(HITEC_D, 1, HITEC_TIME_MAX_US, &x));
    CHECK(hitec_level_xfer(HITEC_D, 0, 1u, &x));
}

TEST_CASE(a_transaction_outside_its_limits_is_not_valid)
{
    hitec_xfer_t x;
    hitec_xfer_t base;

    hitec_c_xfer(HITEC_C_MOVE, 0x17, 0x70, &base);
    CHECK(!hitec_xfer_valid(NULL));
    x = base; x.n_tx = 0;                          CHECK(!hitec_xfer_valid(&x));
    x = base; x.n_tx = HITEC_TX_MAX;               CHECK(hitec_xfer_valid(&x));
    x = base; x.n_tx = HITEC_TX_MAX + 1u;          CHECK(!hitec_xfer_valid(&x));
    x = base; x.gap_bits = HITEC_GAP_MAX;          CHECK(hitec_xfer_valid(&x));
    x = base; x.gap_bits = HITEC_GAP_MAX + 1u;     CHECK(!hitec_xfer_valid(&x));
    x = base; x.lead_us = HITEC_TIME_MAX_US;       CHECK(hitec_xfer_valid(&x));
    x = base; x.lead_us = HITEC_TIME_MAX_US + 1u;  CHECK(!hitec_xfer_valid(&x));
    x = base; x.window_us = HITEC_TIME_MAX_US;     CHECK(hitec_xfer_valid(&x));
    x = base; x.window_us = HITEC_TIME_MAX_US + 1u;
    CHECK(!hitec_xfer_valid(&x));
    x = base; x.max_rx = HITEC_RX_MAX;             CHECK(hitec_xfer_valid(&x));
    x = base; x.max_rx = HITEC_RX_MAX + 1u;        CHECK(!hitec_xfer_valid(&x));
    /* A receive has both a count and a window, or neither. */
    x = base; x.max_rx = 0;                        CHECK(!hitec_xfer_valid(&x));
    x = base; x.window_us = 0;                     CHECK(!hitec_xfer_valid(&x));
    x = base; x.window_us = 0; x.max_rx = 0;       CHECK(hitec_xfer_valid(&x));
    x = base; x.answer = 1;                        CHECK(!hitec_xfer_valid(&x));
    x = base; x.family = 2;                        CHECK(!hitec_xfer_valid(&x));
    x = base; x.kind = 3;                          CHECK(!hitec_xfer_valid(&x));
    CHECK(hitec_listen_xfer(HITEC_D, 1000u, 1, true, 0, &base));
    x = base; x.answer = 2;                        CHECK(!hitec_xfer_valid(&x));
    CHECK(hitec_level_xfer(HITEC_D, 0, 100u, &base));
    x = base; x.window_us = 10; x.max_rx = 1;      CHECK(!hitec_xfer_valid(&x));
    x = base; x.answer = 1;                        CHECK(!hitec_xfer_valid(&x));
}

TEST_CASE(classic_replies_from_the_bench_decode)
{
    static const struct {
        uint8_t cmd;
        uint8_t r0, r1;
        uint32_t delay_ns;
        uint16_t value;
    } rows[] = {
        { 0x61, 0x50, 0x2B, 608000u, 0x50 },   /* first read of a session */
        { 0x61, 0x01, 0x2B, 513000u, 0x01 },
        { 0x62, 0x2B, 0x2B, 514000u, 0 },
        { 0x62, 0x2B, 0x2B, 614000u, 0 },      /* the latest seen */
        { 0x64, 0x2B, 0x2B, 510000u, 0 },
        { 0x65, 0x01, 0xF3, 512000u, 499u },   /* position 0x01F3 */
        { 0x66, 0x2B, 0x2B, 511000u, 0 },
        { 0x67, 0x16, 0x2B, 510000u, 0x16 },   /* HS-5645MG */
        { 0x67, 0x26, 0x2B, 510000u, 0x26 },   /* HS-7955TG */
        { 0x61, 0x2D, 0x2B, 508000u, 0x2D },   /* a value of 0x2D is no NAK */
    };

    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        hitec_c_expect_t e;
        hitec_reply_t r;

        CHECK(hitec_c_expect_of(rows[i].cmd, &e));
        classic_reply(rows[i].delay_ns, 520000u, rows[i].r0, rows[i].r1);
        CHECK_EQ(hitec_c_decode(&cap, e, &r), HITEC_RX_OK);
        CHECK_EQ(r.value, rows[i].value);
        CHECK_EQ(r.delay_ns, rows[i].delay_ns);
        CHECK_EQ(r.n, 2);
    }
    CHECK(hitec_c_expect_of(HITEC_C_READ_RAM, NULL));
    CHECK(!hitec_c_expect_of(0x68, NULL));
    CHECK(!hitec_c_expect_of(0x00, NULL));
}

TEST_CASE(a_classic_reply_is_refused_with_one_code_per_check)
{
    hitec_reply_t r;

    classic_reply(513000u, 520000u, 0x01, 0x2B);
    CHECK_EQ(hitec_c_decode(NULL, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_ARG);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, NULL),
             HITEC_RX_ERR_ARG);
    CHECK_EQ(hitec_c_decode(&cap, (hitec_c_expect_t)3, &r), HITEC_RX_ERR_ARG);
    cap.family = HITEC_D;
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_ARG);
    cap.family = HITEC_CLASSIC;
    cap.aborted = 1;
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_ABORTED);
    cap.aborted = 0;
    cap.overflow = 1;
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_OVERFLOW);
    cap.overflow = 0;
    cap.n = HITEC_RX_MAX + 1u;
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_OVERFLOW);
    cap_clear(HITEC_CLASSIC);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_SILENT);
    CHECK_EQ(r.n, 0);

    /* The accept window at both edges. */
    classic_reply(HITEC_C_REPLY_MIN_NS, 520000u, 0x01, 0x2B);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r), HITEC_RX_OK);
    classic_reply(HITEC_C_REPLY_MIN_NS - 1u, 520000u, 0x01, 0x2B);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_EARLY);
    CHECK_EQ(r.delay_ns, HITEC_C_REPLY_MIN_NS - 1u);
    classic_reply(HITEC_C_REPLY_MAX_NS, 520000u, 0x01, 0x2B);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r), HITEC_RX_OK);
    classic_reply(HITEC_C_REPLY_MAX_NS + 1u, 520000u, 0x01, 0x2B);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_LATE);
    /* A first byte at the receiver's start: early. */
    cap_clear(HITEC_CLASSIC);
    cap_byte(0, 0x01);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_EARLY);
    CHECK_EQ(r.delay_ns, C_ARM);

    /* `-` alone; with a bad stop bit it is a short reply. */
    cap_clear(HITEC_CLASSIC);
    cap_byte(513000u - C_ARM, HITEC_C_NAK);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_ACK, &r), HITEC_RX_ERR_NAK);
    cap.byte[0].framing = 1;
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_ACK, &r),
             HITEC_RX_ERR_LENGTH);
    /* One byte, three bytes. */
    cap_clear(HITEC_CLASSIC);
    cap_byte(513000u - C_ARM, 0x01);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_LENGTH);
    CHECK_EQ(r.n, 1);
    classic_reply(513000u, 520000u, 0x01, 0x2B);
    cap_byte(cap.byte[1].at_ns + 520000u, 0x2B);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_LENGTH);
    /* A bad stop bit in either byte. */
    classic_reply(513000u, 520000u, 0x01, 0x2B);
    cap.byte[0].framing = 1;
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_FRAMING);
    classic_reply(513000u, 520000u, 0x01, 0x2B);
    cap.byte[1].framing = 1;
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_FRAMING);
    /* The second byte: 9.5 to 15 bit times after the first. */
    classic_reply(513000u, 494792u, 0x01, 0x2B);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r), HITEC_RX_OK);
    classic_reply(513000u, 494791u, 0x01, 0x2B);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_GAP);
    classic_reply(513000u, 781250u, 0x01, 0x2B);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r), HITEC_RX_OK);
    classic_reply(513000u, 781251u, 0x01, 0x2B);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_GAP);
    classic_reply(513000u, 520000u, 0x01, 0x2B);
    cap.byte[1].at_ns = cap.byte[0].at_ns - 1u;
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_GAP);
    /* The `+` bytes. */
    classic_reply(513000u, 520000u, 0x01, 0x2A);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r),
             HITEC_RX_ERR_ACK);
    classic_reply(513000u, 520000u, 0x2A, 0x2B);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_ACK, &r), HITEC_RX_ERR_ACK);
    classic_reply(513000u, 520000u, 0x2B, 0x2A);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_ACK, &r), HITEC_RX_ERR_ACK);
    /* A word has no `+`. */
    classic_reply(513000u, 520000u, 0x2B, 0x2A);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_WORD, &r), HITEC_RX_OK);
    CHECK_EQ(r.value, 0x2B2Au);
}

TEST_CASE(d_answers_from_the_bench_decode)
{
    static const struct {
        uint8_t addr;
        uint8_t a[7];
        uint32_t delay_ns;
        uint16_t value;
    } rows[] = {
        { 0x0C, { 0x69, 0x00, 0x0C, 0x02, 0xC3, 0x3B, 0x0C }, 6461000u,
          15299u },
        { 0x0C, { 0x69, 0x00, 0x0C, 0x02, 0xC2, 0x3B, 0x0B }, 6281000u,
          15298u },
        { 0x0C, { 0x69, 0x00, 0x0C, 0x02, 0x00, 0x20, 0x2E }, 5900000u,
          8192u },
        { 0x4E, { 0x69, 0x00, 0x4E, 0x02, 0x01, 0x00, 0x51 }, 6500000u, 1u },
        { 0x54, { 0x69, 0x00, 0x54, 0x02, 0xFF, 0x0F, 0x64 }, 6000000u,
          4095u },
        /* 15.2 ms, as reported with another programmer. */
        { 0xC2, { 0x69, 0x00, 0xC2, 0x02, 0x00, 0x20, 0xE4 }, 15200000u,
          8192u },
    };

    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        hitec_reply_t r;

        d_answer(rows[i].delay_ns, rows[i].a, true);
        CHECK_EQ(hitec_d_decode(&cap, rows[i].addr, &r), HITEC_RX_OK);
        CHECK_EQ(r.value, rows[i].value);
        CHECK_EQ(r.delay_ns, rows[i].delay_ns);
        CHECK_EQ(r.n, 8);
        CHECK_EQ(r.id, 0);
        /* Without the byte after it. */
        d_answer(rows[i].delay_ns, rows[i].a, false);
        CHECK_EQ(hitec_d_decode(&cap, rows[i].addr, &r), HITEC_RX_OK);
    }
}

TEST_CASE(a_d_answer_is_refused_with_one_code_per_check)
{
    static const uint8_t a[] = { 0x69, 0x00, 0x0C, 0x02, 0x00, 0x20, 0x2E };
    uint8_t b[7];
    hitec_reply_t r;

    d_answer(6000000u, a, true);
    CHECK_EQ(hitec_d_decode(NULL, 0x0C, &r), HITEC_RX_ERR_ARG);
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, NULL), HITEC_RX_ERR_ARG);
    cap.family = HITEC_CLASSIC;
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_ARG);
    cap.family = HITEC_D;
    cap.aborted = 1;
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_ABORTED);
    cap.aborted = 0;
    cap.overflow = 1;
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_OVERFLOW);
    cap_clear(HITEC_D);
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_SILENT);

    /* The accept window at both edges. */
    d_answer(HITEC_D_ANSWER_MIN_NS, a, true);
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_OK);
    d_answer(HITEC_D_ANSWER_MIN_NS - 1u, a, true);
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_EARLY);
    d_answer(HITEC_D_ANSWER_MAX_NS, a, true);
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_OK);
    d_answer(HITEC_D_ANSWER_MAX_NS + 1u, a, true);
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_LATE);
    CHECK_EQ(r.delay_ns, HITEC_D_ANSWER_MAX_NS + 1u);

    /* Six bytes. */
    d_answer(6000000u, a, false);
    cap.n = 6;
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_LENGTH);
    /* A bad stop bit in the answer; in the byte after it, none. */
    d_answer(6000000u, a, true);
    cap.byte[6].framing = 1;
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_FRAMING);
    d_answer(6000000u, a, true);
    cap.byte[7].framing = 1;
    cap.byte[7].value = 0x00;
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_OK);
    /* Spacing: 9.5 and 15 bit times are in, past them out. */
    d_answer(6000000u, a, false);
    cap.byte[3].at_ns = cap.byte[2].at_ns + 82465u;
    cap.byte[4].at_ns = cap.byte[3].at_ns + 130208u;
    cap.byte[5].at_ns = cap.byte[4].at_ns + 86806u;
    cap.byte[6].at_ns = cap.byte[5].at_ns + 86806u;
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_OK);
    cap.byte[3].at_ns -= 1u;
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_GAP);
    cap.byte[3].at_ns += 1u;
    cap.byte[4].at_ns += 1u;
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_GAP);
    /* Header, length byte, address, sum.  The ID is not checked. */
    memcpy(b, a, sizeof(b));
    b[0] = 0x96;
    d_answer(6000000u, b, true);
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_HEADER);
    memcpy(b, a, sizeof(b));
    b[3] = 0x00;
    b[6] = hitec_d_sum(&b[1], 5);
    d_answer(6000000u, b, true);
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_HEADER);
    d_answer(6000000u, a, true);
    CHECK_EQ(hitec_d_decode(&cap, 0x0E, &r), HITEC_RX_ERR_ADDRESS);
    memcpy(b, a, sizeof(b));
    b[6] ^= 0x80u;
    d_answer(6000000u, b, true);
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_ERR_SUM);
    memcpy(b, a, sizeof(b));
    b[1] = 0xFE;
    b[6] = hitec_d_sum(&b[1], 5);
    d_answer(6000000u, b, true);
    CHECK_EQ(hitec_d_decode(&cap, 0x0C, &r), HITEC_RX_OK);
    CHECK_EQ(r.id, 0xFE);
}

TEST_CASE(a_listen_is_held_to_its_one_byte)
{
    hitec_reply_t r;

    cap_clear(HITEC_CLASSIC);
    cap_byte(230000u, HITEC_C_ACK);
    CHECK_EQ(hitec_listen_decode(&cap, HITEC_C_ACK, &r), HITEC_RX_OK);
    CHECK_EQ(r.delay_ns, 230000u);
    CHECK_EQ(r.value, HITEC_C_ACK);
    CHECK_EQ(hitec_listen_decode(&cap, HITEC_C_NAK, &r), HITEC_RX_ERR_ACK);
    cap.byte[0].framing = 1;
    CHECK_EQ(hitec_listen_decode(&cap, HITEC_C_ACK, &r),
             HITEC_RX_ERR_FRAMING);
    CHECK_EQ(hitec_listen_decode(NULL, HITEC_C_ACK, &r), HITEC_RX_ERR_ARG);
    CHECK_EQ(hitec_listen_decode(&cap, HITEC_C_ACK, NULL), HITEC_RX_ERR_ARG);
    cap.family = 9;
    CHECK_EQ(hitec_listen_decode(&cap, HITEC_C_ACK, &r), HITEC_RX_ERR_ARG);
    cap_clear(HITEC_D);
    CHECK_EQ(hitec_listen_decode(&cap, 0x00, &r), HITEC_RX_ERR_SILENT);
    cap.aborted = 1;
    CHECK_EQ(hitec_listen_decode(&cap, 0x00, &r), HITEC_RX_ERR_ABORTED);
    cap.aborted = 0;
    cap.overflow = 1;
    CHECK_EQ(hitec_listen_decode(&cap, 0x00, &r), HITEC_RX_ERR_OVERFLOW);
}

TEST_CASE(the_elapsed_time_and_the_due_test_hold_across_the_wrap)
{
    CHECK_EQ(hitec_elapsed_us(1000u, 400u), 600u);
    CHECK_EQ(hitec_elapsed_us(0x10u, 0xFFFFFF00u), 0x110u);
    CHECK_EQ(hitec_elapsed_us(0u, 0xFFFFFFFFu), 1u);
    CHECK_EQ(hitec_elapsed_us(5u, 5u), 0u);
    /* At the span and one short of it, on both sides of the wrap. */
    CHECK(hitec_due(0xFFFFFF00u + 1000u, 0xFFFFFF00u, 1000u));
    CHECK(!hitec_due(0xFFFFFF00u + 999u, 0xFFFFFF00u, 1000u));
    CHECK(hitec_due(2000u, 1000u, 1000u));
    CHECK(!hitec_due(1999u, 1000u, 1000u));
    CHECK(hitec_due(7u, 7u, 0u));
    /* The longest span. */
    CHECK(hitec_due(0x7FFFFFFFu + 10u, 10u, 0x7FFFFFFFu));
    CHECK(!hitec_due(0x7FFFFFFFu + 9u, 10u, 0x7FFFFFFFu));
}

int main(void)
{
    RUN(the_classic_sum_makes_every_frame_sum_to_zero);
    RUN(the_d_sum_is_the_low_byte_of_every_byte_after_the_header);
    RUN(the_bit_times_of_both_families);
    RUN(the_builders_make_valid_transactions);
    RUN(a_transaction_outside_its_limits_is_not_valid);
    RUN(classic_replies_from_the_bench_decode);
    RUN(a_classic_reply_is_refused_with_one_code_per_check);
    RUN(d_answers_from_the_bench_decode);
    RUN(a_d_answer_is_refused_with_one_code_per_check);
    RUN(a_listen_is_held_to_its_one_byte);
    RUN(the_elapsed_time_and_the_due_test_hold_across_the_wrap);
    return test_summary("hitec_wire");
}
