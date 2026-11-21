/**
 * @file
 * @brief Link frames on a UART: COBS (consistent overhead byte stuffing)
 * framing with a CRC-16 (cyclic redundancy check), shared/link/link_uart.c.
 *
 * Every DLC (data length code) 0 to 8 with every byte value; the 9 and
 * 17-byte limits; a bad CRC, a DLC over 8, a length against its DLC, an
 * identifier over 29 bits, a COBS error, an overlong and a truncated frame,
 * each dropped and counted once; garbage before the first 0x00; an ESP32-S3
 * ROM boot message at 115,200 baud read by a 1Mbaud receiver in front of the
 * first frames; frames split at every byte and back to back; the refused
 * count's wrap at 2^32.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_can.h"
#include "link_crc.h"
#include "link_pages.h"
#include "link_uart.h"

/** @brief What a byte stream gave: the frames handed out and the counter. */
typedef struct {
    link_can_frame_t f[160]; /**< frames in arrival order       */
    size_t n;                /**< frames held                   */
    size_t lost;             /**< frames past the end of f      */
    uint32_t refused;        /**< the STATUS refused counter    */
} sink_t;

/**
 * @brief Feed @p len bytes one at a time and collect what comes out.
 *
 * @param rx The decoder.
 * @param p The bytes.
 * @param len Bytes in p.
 * @param s Collects the frames and the refused count.
 */
static void feed(link_uart_rx_t *rx, const uint8_t *p, size_t len, sink_t *s)
{
    for (size_t i = 0; i < len; ++i) {
        link_can_frame_t f;
        if (link_uart_feed(rx, p[i], &f, &s->refused)) {
            if (s->n < sizeof(s->f) / sizeof(s->f[0])) {
                s->f[s->n++] = f;
            } else {
                ++s->lost;
            }
        }
    }
}

/**
 * @brief Every refusal count of a decoder, summed.
 *
 * @param c The counts.
 * @return garbage + overlong + bad_cobs + bad_len + bad_crc + bad_dlc +
 *         bad_id.
 */
static uint32_t refusals(const link_uart_counts_t *c)
{
    return c->garbage + c->overlong + c->bad_cobs + c->bad_len + c->bad_crc
           + c->bad_dlc + c->bad_id;
}

/**
 * @brief A reference COBS encoder, written apart from the module's.
 *
 * @param in The bytes.
 * @param len Bytes in in, under 254.
 * @param out Receives len + 1 bytes and the 0x00 after them.
 * @return The bytes written, delimiter included.
 */
static size_t ref_wire(const uint8_t *in, size_t len, uint8_t *out)
{
    size_t w = 0;
    size_t start = 0;
    for (size_t i = 0; i <= len; ++i) {
        if (i == len || in[i] == 0u) {
            out[w++] = (uint8_t)(i - start + 1u);
            memcpy(&out[w], &in[start], i - start);
            w += i - start;
            start = i + 1u;
        }
    }
    out[w++] = 0u;
    return w;
}

/**
 * @brief The bytes before COBS: identifier, a DLC field, data and a CRC.
 *
 * The DLC field and the data are separate so a frame can lie about its
 * length and still carry a good CRC.
 *
 * @param id The 32-bit identifier field.
 * @param dlc The DLC byte as sent.
 * @param data The data bytes.
 * @param ndata Bytes of data.
 * @param raw Receives 7 + ndata bytes.
 * @return 7 + ndata.
 */
static size_t raw_of(uint32_t id, uint8_t dlc, const uint8_t *data,
                     size_t ndata, uint8_t *raw)
{
    raw[0] = (uint8_t)id;
    raw[1] = (uint8_t)(id >> 8);
    raw[2] = (uint8_t)(id >> 16);
    raw[3] = (uint8_t)(id >> 24);
    raw[4] = dlc;
    if (ndata > 0u) {
        memcpy(&raw[5], data, ndata);
    }
    const uint16_t crc = link_crc(LINK_CRC_INIT, raw, 5u + ndata);
    raw[5u + ndata] = (uint8_t)crc;
    raw[6u + ndata] = (uint8_t)(crc >> 8);
    return 7u + ndata;
}

/**
 * @brief A frame with a given identifier, DLC and data pattern.
 *
 * @param id The identifier.
 * @param dlc 0 to 8.
 * @param seed data[i] = seed + 37 * i.
 * @return The frame.
 */
static link_can_frame_t frame(uint32_t id, uint8_t dlc, uint8_t seed)
{
    link_can_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id = id;
    f.dlc = dlc;
    for (uint8_t i = 0; i < dlc; ++i) {
        f.data[i] = (uint8_t)(seed + 37u * i);
    }
    return f;
}

/**
 * @brief Whether two frames carry the same identifier, DLC and data.
 *
 * @param a One frame.
 * @param b The other.
 * @return true when they agree.
 */
static bool same(const link_can_frame_t *a, const link_can_frame_t *b)
{
    return a->id == b->id && a->dlc == b->dlc
           && memcmp(a->data, b->data, a->dlc) == 0;
}

/** @brief A decoder that has seen its first 0x00 and counts from 0. */
static link_uart_rx_t synced_rx(void)
{
    link_uart_rx_t rx;
    link_uart_rx_init(&rx);
    link_can_frame_t f;
    (void)link_uart_feed(&rx, 0u, &f, NULL);
    return rx;
}

TEST_CASE(the_wire_bytes_match_a_reference_computed_apart)
{
    /* Computed with a separate CRC-16/CCITT-FALSE and COBS: identifier
     * little-endian, DLC, data, CRC little-endian, COBS, 0x00. */
    static const uint8_t want4[] = {0x06, 0x84, 0x03, 0x4A, 0x0C, 0x04, 0x02,
                                    0x11, 0x04, 0xFF, 0xBE, 0x35, 0x00};
    static const uint8_t want0[] = {0x01, 0x01, 0x01, 0x01, 0x01,
                                    0x03, 0x0C, 0x11, 0x00};
    static const uint8_t want8[] = {0x10, 0xFF, 0xFF, 0xFF, 0x1F, 0x08,
                                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                    0x07, 0x08, 0x7F, 0x24, 0x00};
    uint8_t w[LINK_UART_WIRE_MAX];

    link_can_frame_t f = frame(0x0C4A0384u, 4, 0);
    f.data[0] = 0x00; f.data[1] = 0x11; f.data[2] = 0x00; f.data[3] = 0xFF;
    CHECK_EQ(link_uart_encode(&f, w, sizeof(w)), sizeof(want4));
    CHECK(memcmp(w, want4, sizeof(want4)) == 0);

    f = frame(0, 0, 0);
    CHECK_EQ(link_uart_encode(&f, w, sizeof(w)), sizeof(want0));
    CHECK(memcmp(w, want0, sizeof(want0)) == 0);

    f = frame(0x1FFFFFFFu, 8, 0);
    for (uint8_t i = 0; i < 8; ++i) {
        f.data[i] = (uint8_t)(i + 1u);
    }
    CHECK_EQ(link_uart_encode(&f, w, sizeof(w)), sizeof(want8));
    CHECK(memcmp(w, want8, sizeof(want8)) == 0);
    CHECK_EQ(sizeof(want8), LINK_UART_WIRE_MAX);
    CHECK_EQ(sizeof(want0), LINK_UART_WIRE_MIN);
}

TEST_CASE(every_dlc_and_every_byte_value_survives_the_round_trip)
{
    for (uint8_t dlc = 0; dlc <= 8u; ++dlc) {
        for (unsigned v = 0; v < 256u; ++v) {
            /* Every byte of the frame is v once: identifier, data and, over
             * the 256 values, CRC bytes of both kinds, 0x00 among them. */
            const uint32_t id = ((uint32_t)v | ((uint32_t)v << 8)
                                 | ((uint32_t)v << 16) | ((uint32_t)v << 24))
                                & LINK_UART_ID_MAX;
            link_can_frame_t f = frame(id, dlc, 0);
            memset(f.data, (int)v, dlc);
            link_can_frame_t g = frame(id ^ 0x15555555u, dlc, (uint8_t)v);

            const link_can_frame_t *each[2] = {&f, &g};
            for (size_t k = 0; k < 2u; ++k) {
                uint8_t w[LINK_UART_WIRE_MAX + 4];
                const size_t n = link_uart_encode(each[k], w, sizeof(w));
                CHECK_EQ(n, LINK_UART_WIRE_MIN + dlc);
                CHECK_EQ(w[n - 1u], 0);
                CHECK(memchr(w, 0, n - 1u) == NULL);

                link_uart_rx_t rx = synced_rx();
                sink_t s;
                memset(&s, 0, sizeof(s));
                feed(&rx, w, n, &s);
                CHECK_EQ(s.n, 1);
                CHECK(same(&s.f[0], each[k]));
                CHECK_EQ(s.refused, 0);
                CHECK_EQ(rx.counts.frames, 1);
                CHECK_EQ(refusals(&rx.counts), 0);
            }
        }
    }
}

TEST_CASE(a_frame_runs_unchanged_through_the_can_mapping)
{
    link_msg_t m;
    memset(&m, 0, sizeof(m));
    m.op = LINK_OP_DATA;
    m.page = LINK_PAGE_STATUS;
    m.offset = 0;
    m.count = LINK_MAX_REGS;
    m.seq = LINK_SEQ_MAX;
    for (uint8_t i = 0; i < LINK_MAX_REGS; ++i) {
        m.regs[i] = (uint16_t)(0x0100u * i + 0x00FFu - i);
    }
    link_can_frame_t cf[LINK_CAN_MAX_FRAMES];
    const size_t frames = link_can_encode(&m, cf, LINK_CAN_MAX_FRAMES);
    CHECK_EQ(frames, LINK_CAN_MAX_FRAMES);

    uint8_t wire[LINK_CAN_MAX_FRAMES * LINK_UART_WIRE_MAX];
    size_t n = 0;
    for (size_t i = 0; i < frames; ++i) {
        n += link_uart_encode(&cf[i], &wire[n], sizeof(wire) - n);
    }
    CHECK_EQ(n, frames * LINK_UART_WIRE_MAX);

    link_uart_rx_t rx = synced_rx();
    sink_t s;
    memset(&s, 0, sizeof(s));
    feed(&rx, wire, n, &s);
    CHECK_EQ(s.n, frames);
    uint16_t got[LINK_MAX_REGS] = {0};
    for (size_t i = 0; i < s.n; ++i) {
        link_msg_t part;
        CHECK(link_can_decode_counted(&s.f[i], &part, &s.refused));
        CHECK_EQ(part.seq, LINK_SEQ_MAX);
        CHECK_EQ(part.page, LINK_PAGE_STATUS);
        for (uint8_t r = 0; r < part.count; ++r) {
            got[part.offset + r] = part.regs[r];
        }
    }
    CHECK(memcmp(got, m.regs, sizeof(got)) == 0);
    CHECK_EQ(s.refused, 0);
}

TEST_CASE(the_17_byte_maximum_is_taken_and_one_byte_more_is_overlong)
{
    link_uart_rx_t rx = synced_rx();
    sink_t s;
    memset(&s, 0, sizeof(s));

    /* DLC 8 and no 0x00 anywhere: 16 COBS bytes and the delimiter. */
    const link_can_frame_t big = frame(0x1FFFFFFFu, 8, 1);
    uint8_t w[LINK_UART_WIRE_MAX];
    CHECK_EQ(link_uart_encode(&big, w, sizeof(w)), LINK_UART_WIRE_MAX);
    feed(&rx, w, sizeof(w), &s);
    CHECK_EQ(s.n, 1);

    /* 17 bytes before the 0x00: refused at the 17th, once. */
    uint8_t over[LINK_UART_COBS_MAX + 2];
    memset(over, 0x11, sizeof(over));
    over[sizeof(over) - 1u] = 0u;
    feed(&rx, over, sizeof(over), &s);
    CHECK_EQ(s.n, 1);
    CHECK_EQ(rx.counts.overlong, 1);
    CHECK_EQ(s.refused, 1);

    /* 1,000 bytes before the 0x00 count once too. */
    uint8_t flood[1001];
    memset(flood, 0xA5, sizeof(flood));
    flood[1000] = 0u;
    feed(&rx, flood, sizeof(flood), &s);
    CHECK_EQ(rx.counts.overlong, 2);
    CHECK_EQ(s.refused, 2);

    /* The next frame after the 0x00 is taken. */
    feed(&rx, w, sizeof(w), &s);
    CHECK_EQ(s.n, 2);
    CHECK(same(&s.f[1], &big));
    CHECK_EQ(refusals(&rx.counts), 2);
}

TEST_CASE(every_single_bit_error_fails_the_crc_and_is_counted)
{
    uint8_t data[8];
    for (uint8_t i = 0; i < 8; ++i) {
        data[i] = (uint8_t)(0x5Au ^ (i * 29u));
    }
    for (uint8_t dlc = 0; dlc <= 8u; ++dlc) {
        uint8_t raw[LINK_UART_RAW_MAX];
        const size_t len = raw_of(0x0ABCDEF1u, dlc, data, dlc, raw);
        link_uart_rx_t rx = synced_rx();
        sink_t s;
        memset(&s, 0, sizeof(s));
        uint32_t flips = 0;
        for (size_t b = 0; b < len * 8u; ++b) {
            uint8_t bad[LINK_UART_RAW_MAX];
            memcpy(bad, raw, len);
            bad[b / 8u] ^= (uint8_t)(1u << (b % 8u));
            uint8_t w[LINK_UART_WIRE_MAX];
            feed(&rx, w, ref_wire(bad, len, w), &s);
            ++flips;
        }
        CHECK_EQ(s.n, 0);
        CHECK_EQ(rx.counts.bad_crc, flips);
        CHECK_EQ(s.refused, flips);
        CHECK_EQ(refusals(&rx.counts), flips);

        /* And the unchanged frame right after is taken. */
        uint8_t w[LINK_UART_WIRE_MAX];
        feed(&rx, w, ref_wire(raw, len, w), &s);
        CHECK_EQ(s.n, 1);
        CHECK_EQ(s.f[0].dlc, dlc);
    }
}

TEST_CASE(any_wire_byte_changed_drops_the_frame_and_counts_it)
{
    const link_can_frame_t f = frame(0x00400C84u, 8, 0x80);
    uint8_t good[LINK_UART_WIRE_MAX];
    const size_t n = link_uart_encode(&f, good, sizeof(good));
    CHECK(n > 0u);
    link_uart_rx_t rx = synced_rx();
    sink_t s;
    memset(&s, 0, sizeof(s));
    uint32_t tries = 0;

    for (size_t at = 0; at + 1u < n; ++at) {
        for (unsigned v = 1; v < 256u; ++v) {
            if (v == good[at]) {
                continue;
            }
            uint8_t w[LINK_UART_WIRE_MAX];
            memcpy(w, good, n);
            w[at] = (uint8_t)v;
            feed(&rx, w, n, &s);
            ++tries;
        }
    }
    CHECK_EQ(s.n, 0);
    CHECK_EQ(s.refused, tries);
    CHECK_EQ(refusals(&rx.counts), tries);
    CHECK(rx.counts.bad_cobs > 0u);
    CHECK(rx.counts.bad_crc > 0u);
}

TEST_CASE(a_dlc_over_8_is_refused_even_with_a_good_crc)
{
    link_uart_rx_t rx = synced_rx();
    sink_t s;
    memset(&s, 0, sizeof(s));
    const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint32_t sent = 0;

    for (unsigned dlc = 9; dlc < 256u; ++dlc) {
        for (size_t nd = 0; nd <= 8u; ++nd) {
            uint8_t raw[LINK_UART_RAW_MAX];
            uint8_t w[LINK_UART_WIRE_MAX];
            const size_t len = raw_of(0x123u, (uint8_t)dlc, data, nd, raw);
            feed(&rx, w, ref_wire(raw, len, w), &s);
            ++sent;
        }
    }
    CHECK_EQ(s.n, 0);
    CHECK_EQ(rx.counts.bad_dlc, sent);
    CHECK_EQ(s.refused, sent);
    CHECK_EQ(refusals(&rx.counts), sent);
}

TEST_CASE(a_length_that_disagrees_with_the_dlc_is_refused)
{
    link_uart_rx_t rx = synced_rx();
    sink_t s;
    memset(&s, 0, sizeof(s));
    const uint8_t data[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    uint32_t sent = 0;

    /* Every DLC 0 to 8 with every other count of data bytes. */
    for (uint8_t dlc = 0; dlc <= 8u; ++dlc) {
        for (size_t nd = 0; nd <= 8u; ++nd) {
            if (nd == dlc) {
                continue;
            }
            uint8_t raw[LINK_UART_RAW_MAX];
            uint8_t w[LINK_UART_WIRE_MAX];
            const size_t len = raw_of(0x7u, dlc, data, nd, raw);
            feed(&rx, w, ref_wire(raw, len, w), &s);
            ++sent;
        }
    }
    /* Under 7 bytes after COBS: 1 to 6 bytes. */
    for (size_t len = 1; len < LINK_UART_RAW_MIN; ++len) {
        uint8_t raw[LINK_UART_RAW_MIN] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36};
        uint8_t w[LINK_UART_WIRE_MAX];
        feed(&rx, w, ref_wire(raw, len, w), &s);
        ++sent;
    }
    CHECK_EQ(s.n, 0);
    CHECK_EQ(rx.counts.bad_len, sent);
    CHECK_EQ(s.refused, sent);
}

TEST_CASE(an_identifier_above_29_bits_is_refused_both_ways)
{
    uint8_t w[LINK_UART_WIRE_MAX];
    link_can_frame_t f = frame(LINK_UART_ID_MAX + 1u, 2, 3);
    CHECK_EQ(link_uart_encode(&f, w, sizeof(w)), 0);
    f.id = 0xFFFFFFFFu;
    CHECK_EQ(link_uart_encode(&f, w, sizeof(w)), 0);

    link_uart_rx_t rx = synced_rx();
    sink_t s;
    memset(&s, 0, sizeof(s));
    const uint32_t ids[3] = {0x20000000u, 0x80000000u, 0xFFFFFFFFu};
    for (size_t i = 0; i < 3u; ++i) {
        uint8_t raw[LINK_UART_RAW_MAX];
        const size_t len = raw_of(ids[i], 0, NULL, 0, raw);
        feed(&rx, w, ref_wire(raw, len, w), &s);
    }
    CHECK_EQ(s.n, 0);
    CHECK_EQ(rx.counts.bad_id, 3);
    CHECK_EQ(s.refused, 3);

    /* 0x1FFFFFFF itself is an identifier. */
    uint8_t raw[LINK_UART_RAW_MAX];
    const size_t len = raw_of(LINK_UART_ID_MAX, 0, NULL, 0, raw);
    feed(&rx, w, ref_wire(raw, len, w), &s);
    CHECK_EQ(s.n, 1);
    CHECK_EQ(s.f[0].id, LINK_UART_ID_MAX);
}

TEST_CASE(a_cobs_code_past_the_end_is_a_cobs_error)
{
    link_uart_rx_t rx = synced_rx();
    sink_t s;
    memset(&s, 0, sizeof(s));
    static const uint8_t past[] = {0x05, 0x01, 0x02, 0x00};
    static const uint8_t ff[] = {0xFF, 0x01, 0x02, 0x03, 0x00};
    static const uint8_t mid[] = {0x02, 0x01, 0x09, 0x01, 0x00};
    /* One byte past the end, after the first code and after a later one. */
    static const uint8_t one_past[] = {0x04, 0x01, 0x02, 0x00};
    static const uint8_t late[] = {0x02, 0x01, 0x03, 0x01, 0x00};
    feed(&rx, past, sizeof(past), &s);
    feed(&rx, ff, sizeof(ff), &s);
    feed(&rx, mid, sizeof(mid), &s);
    feed(&rx, one_past, sizeof(one_past), &s);
    feed(&rx, late, sizeof(late), &s);
    /* All 16 bytes held, the code asking for 16 after it. */
    uint8_t full[LINK_UART_COBS_MAX + 1];
    memset(full, 0x7E, sizeof(full));
    full[0] = LINK_UART_COBS_MAX + 1u;
    full[LINK_UART_COBS_MAX] = 0u;
    feed(&rx, full, sizeof(full), &s);
    CHECK_EQ(s.n, 0);
    CHECK_EQ(rx.counts.bad_cobs, 6);
    CHECK_EQ(s.refused, 6);
    CHECK_EQ(refusals(&rx.counts), 6);
}

TEST_CASE(a_truncated_frame_is_dropped_and_the_next_one_is_taken)
{
    const link_can_frame_t f = frame(0x0A5A5A5Au, 8, 0x40);
    uint8_t good[LINK_UART_WIRE_MAX];
    const size_t n = link_uart_encode(&f, good, sizeof(good));
    link_uart_rx_t rx = synced_rx();
    sink_t s;
    memset(&s, 0, sizeof(s));

    /* Lose 1 to 15 of the 16 COBS bytes at the end, at the start and in
     * the middle; the 0x00 arrives. */
    uint32_t cut = 0;
    for (size_t lose = 1; lose < n - 1u; ++lose) {
        uint8_t w[LINK_UART_WIRE_MAX];
        const size_t keep = n - 1u - lose;
        memcpy(w, good, keep);
        w[keep] = 0u;
        feed(&rx, w, keep + 1u, &s);
        memcpy(w, &good[lose], keep + 1u);
        feed(&rx, w, keep + 1u, &s);
        const size_t half = keep / 2u;
        memcpy(w, good, half);
        memcpy(&w[half], &good[half + lose], n - half - lose);
        feed(&rx, w, n - lose, &s);
        cut += 3u;
    }
    CHECK_EQ(s.n, 0);
    CHECK_EQ(s.refused, cut);
    CHECK_EQ(refusals(&rx.counts), cut);

    /* A frame whose 0x00 is lost merges with the next; the one after that
     * is taken. */
    const link_can_frame_t z = frame(0x5u, 0, 0);
    uint8_t a[LINK_UART_WIRE_MAX];
    const size_t na = link_uart_encode(&z, a, sizeof(a));
    feed(&rx, a, na - 1u, &s);
    feed(&rx, a, na, &s);
    CHECK_EQ(s.n, 0);
    CHECK_EQ(s.refused, cut + 1u);
    feed(&rx, good, n - 1u, &s);
    feed(&rx, good, n, &s);
    CHECK_EQ(s.n, 0);
    CHECK_EQ(rx.counts.overlong, 1);
    feed(&rx, good, n, &s);
    CHECK_EQ(s.n, 1);
    CHECK(same(&s.f[0], &f));
    CHECK_EQ(s.refused, cut + 2u);
}

TEST_CASE(garbage_before_the_first_zero_counts_once)
{
    const link_can_frame_t f1 = frame(0x11u, 3, 7);
    const link_can_frame_t f2 = frame(0x22u, 5, 9);
    uint8_t w1[LINK_UART_WIRE_MAX], w2[LINK_UART_WIRE_MAX];
    const size_t n1 = link_uart_encode(&f1, w1, sizeof(w1));
    const size_t n2 = link_uart_encode(&f2, w2, sizeof(w2));

    /* A fresh decoder joining in the middle: what comes before its first
     * 0x00 is dropped, here 40 bytes of noise and all of f1. */
    link_uart_rx_t rx;
    link_uart_rx_init(&rx);
    sink_t s;
    memset(&s, 0, sizeof(s));
    uint8_t noise[40];
    for (size_t i = 0; i < sizeof(noise); ++i) {
        noise[i] = (uint8_t)(0x80u | i);
    }
    feed(&rx, noise, sizeof(noise), &s);
    CHECK_EQ(s.refused, 0);              /* nothing ended yet */
    feed(&rx, w1, n1, &s);
    feed(&rx, w2, n2, &s);
    CHECK_EQ(s.n, 1);
    CHECK(same(&s.f[0], &f2));
    CHECK_EQ(rx.counts.garbage, 1);
    CHECK_EQ(rx.counts.overlong, 0);
    CHECK_EQ(s.refused, 1);

    /* A 0x00 in front of the first frame: taken, nothing counted, and idle
     * 0x00 between frames count nothing either. */
    link_uart_rx_init(&rx);
    memset(&s, 0, sizeof(s));
    static const uint8_t idle[3] = {0, 0, 0};
    feed(&rx, idle, 1, &s);
    feed(&rx, w1, n1, &s);
    feed(&rx, idle, sizeof(idle), &s);
    feed(&rx, w2, n2, &s);
    CHECK_EQ(s.n, 2);
    CHECK_EQ(s.refused, 0);
    CHECK_EQ(refusals(&rx.counts), 0);
}

/* What an ESP32-S3 prints from its ROM at each reset, on TXD0 at 115,200
 * baud, 8N1; the addresses and lengths vary with the build. */
static const char ROM_BOOT[] =
    "ESP-ROM:esp32s3-20210327\r\n"
    "Build:Mar 27 2021\r\n"
    "rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
    "SPIWP:0xee\r\n"
    "mode:DIO, clock div:1\r\n"
    "load:0x3fce3818,len:0x16f8\r\n"
    "load:0x403c9700,len:0x4\r\n"
    "load:0x403c9704,len:0xc00\r\n"
    "load:0x403cc700,len:0x2eb0\r\n"
    "entry 0x403c9908\r\n";

/**
 * @brief The line level at @p t_ns of @p text sent at 115,200 baud, 8N1,
 * 10 idle bits before the text and 20 after each line end.
 *
 * @param text The text.
 * @param len Its bytes.
 * @param t_ns The time since the line started idle, in ns.
 * @return 1 for idle (high), 0 for low.
 */
static int level_115200(const char *text, size_t len, uint64_t t_ns)
{
    /* One bit is 8,680.6ns: count in units of 1/115,200 of a second. */
    uint64_t bit = (t_ns * 115200u) / 1000000000u;
    if (bit < 10u) {
        return 1;
    }
    bit -= 10u;
    for (size_t i = 0; i < len; ++i) {
        if (bit < 10u) {
            if (bit == 0u) {
                return 0;
            }
            if (bit == 9u) {
                return 1;
            }
            return ((uint8_t)text[i] >> (bit - 1u)) & 1;
        }
        bit -= 10u;
        if (text[i] == '\n') {
            if (bit < 20u) {
                return 1;
            }
            bit -= 20u;
        }
    }
    return 1;
}

/**
 * @brief The bytes a 1Mbaud receiver with 16 samples a bit reads from a
 * line carrying @p text at 115,200 baud.
 *
 * A low sample starts a byte; the start bit is checked at its middle, the 8
 * data bits and the stop bit are sampled at their middles.  A byte with a
 * low stop bit (framing error) is kept, as a DMA (direct memory access)
 * channel reading the data register keeps it.  What the receiver does after
 * that byte is the parameter: wait for the line to go high, or start the
 * next byte at the next low sample.  Which of the two the RP2350's UART
 * does is not known, so the suite holds the decoder to both.
 *
 * @param text The text on the line.
 * @param restart_low Start the next byte at a low sample right after a
 *        framing error, instead of waiting for the line to go high.
 * @param out Receives the bytes.
 * @param cap Bytes out can hold.
 * @return The bytes read.
 */
static size_t read_at_1mbaud(const char *text, bool restart_low, uint8_t *out,
                             size_t cap)
{
    const size_t len = strlen(text);
    /* Long enough for every bit of the text, its line gaps and some idle. */
    const uint64_t end_ns = (uint64_t)(len * 10u + 20u * 12u + 40u)
                            * 1000000000u / 115200u;
    size_t n = 0;
    bool wait_high = false;
    uint64_t t = 0;
    while (t < end_ns && n < cap) {
        const int lv = level_115200(text, len, t);
        if (wait_high) {
            wait_high = lv == 0;
            t += 62u;              /* 1/16 of 1µs, rounded down */
            continue;
        }
        if (lv != 0) {
            t += 62u;
            continue;
        }
        if (level_115200(text, len, t + 500u) != 0) {
            t += 62u;              /* a glitch, not a start bit */
            continue;
        }
        uint8_t byte = 0;
        for (unsigned b = 0; b < 8u; ++b) {
            byte |= (uint8_t)(level_115200(text, len, t + 1500u + 1000u * b)
                              << b);
        }
        out[n++] = byte;
        wait_high = !restart_low && level_115200(text, len, t + 9500u) == 0;
        t += 10000u;
    }
    return n;
}

/**
 * @brief Count the chunks of a stream that end at a 0x00 and are not empty,
 * and how many of them are exactly one of the given wire frames.
 *
 * @param p The stream.
 * @param len Its bytes.
 * @param frames Wire frames, each ending in its 0x00.
 * @param lens Their lengths.
 * @param nf How many.
 * @param matched Receives the chunks equal to a frame.
 * @return The non-empty chunks that end at a 0x00.
 */
static uint32_t chunks(const uint8_t *p, size_t len,
                       const uint8_t *const *frames, const size_t *lens,
                       size_t nf, uint32_t *matched)
{
    uint32_t count = 0;
    size_t start = 0;
    *matched = 0;
    for (size_t i = 0; i < len; ++i) {
        if (p[i] != 0u) {
            continue;
        }
        const size_t clen = i - start + 1u;
        if (clen > 1u) {
            ++count;
            for (size_t k = 0; k < nf; ++k) {
                if (lens[k] == clen
                    && memcmp(frames[k], &p[start], clen) == 0) {
                    ++*matched;
                }
            }
        }
        start = i + 1u;
    }
    return count;
}

/**
 * @brief Run @p noise, then two frames, through a decoder and hold the
 * outcome to the chunk count.
 *
 * @param noise The bytes in front.
 * @param nn Their count.
 * @param synced Start the decoder synced, as a running link sees its far
 *        end reset.
 * @return The frames refused.
 */
static uint32_t noise_then_frames(const uint8_t *noise, size_t nn,
                                  bool synced)
{
    const link_can_frame_t fa = frame(0x0444C000u, 0, 0);
    const link_can_frame_t fb = frame(0x08404182u, 4, 0x33);
    uint8_t wa[LINK_UART_WIRE_MAX], wb[LINK_UART_WIRE_MAX];
    const size_t na = link_uart_encode(&fa, wa, sizeof(wa));
    const size_t nb = link_uart_encode(&fb, wb, sizeof(wb));

    static uint8_t stream[4096];
    CHECK(nn + na + nb <= sizeof(stream));
    if (nn + na + nb > sizeof(stream)) {
        return 0;
    }
    memcpy(stream, noise, nn);
    memcpy(&stream[nn], wa, na);
    memcpy(&stream[nn + na], wb, nb);
    const size_t total = nn + na + nb;

    link_uart_rx_t rx;
    if (synced) {
        rx = synced_rx();
    } else {
        link_uart_rx_init(&rx);
    }
    sink_t s;
    memset(&s, 0, sizeof(s));
    feed(&rx, stream, total, &s);

    const uint8_t *const frames[2] = {wa, wb};
    const size_t lens[2] = {na, nb};
    uint32_t matched = 0;
    const uint32_t ended = chunks(stream, total, frames, lens, 2, &matched);

    /* fb is always taken, last; fa only when the noise ends in 0x00. */
    CHECK(s.n >= 1u);
    CHECK(same(&s.f[s.n - 1u], &fb));
    const bool fa_clean = nn == 0u || noise[nn - 1u] == 0u;
    CHECK_EQ(s.n, fa_clean ? 2u : 1u);
    if (fa_clean) {
        CHECK(same(&s.f[0], &fa));
    }
    /* Not one noise chunk passes as a frame: every ended chunk that is
     * not one of the two frames is refused, and counted once. */
    CHECK_EQ(s.n, matched);
    CHECK_EQ(s.refused, ended - matched);
    CHECK_EQ(refusals(&rx.counts), s.refused);
    return s.refused;
}

TEST_CASE(a_rom_boot_message_in_front_is_dropped_and_counted)
{
    static uint8_t seen[2048];

    /* A 115,200-baud bit lasts 8.68µs and a 1Mbaud byte samples its data
     * from 1.5 to 8.5µs after the edge that starts it.  A receiver that
     * starts each byte on an edge therefore reads every character as 0x00:
     * idle line to the decoder, nothing taken and nothing counted. */
    size_t n = read_at_1mbaud(ROM_BOOT, false, seen, sizeof(seen));
    CHECK(n >= strlen(ROM_BOOT));
    bool all_zero = true;
    for (size_t i = 0; i < n; ++i) {
        all_zero = all_zero && seen[i] == 0u;
    }
    CHECK(all_zero);
    noise_then_frames(seen, n, false);
    noise_then_frames(seen, n, true);

    /* A receiver that starts the next byte inside a long low reads other
     * values between the 0x00: each run between two 0x00 is refused and
     * counted, and the frames after the message are taken. */
    n = read_at_1mbaud(ROM_BOOT, true, seen, sizeof(seen));
    size_t nonzero = 0;
    for (size_t i = 0; i < n; ++i) {
        nonzero += seen[i] != 0u;
    }
    CHECK(nonzero > 10u);
    CHECK(memchr(seen, 0, n) != NULL);
    CHECK(noise_then_frames(seen, n, false) > 10u);
    CHECK(noise_then_frames(seen, n, true) > 10u);
    for (size_t cut = 1; cut <= n; ++cut) {
        (void)noise_then_frames(seen, cut, true);
        (void)noise_then_frames(seen, cut, false);
    }

    /* The text itself, as a receiver at 115,200 baud reads it: no 0x00 at
     * all, so it and the first frame are one run, refused once. */
    CHECK_EQ(noise_then_frames((const uint8_t *)ROM_BOOT, strlen(ROM_BOOT),
                               false), 1);
    CHECK_EQ(noise_then_frames((const uint8_t *)ROM_BOOT, strlen(ROM_BOOT),
                               true), 1);
}

TEST_CASE(frames_split_at_every_byte_boundary_decode_the_same)
{
    link_can_frame_t f[3];
    f[0] = frame(0x00000000u, 0, 0);
    f[1] = frame(0x1FFFFFFFu, 8, 0x00);
    f[2] = frame(0x0C000000u, 3, 0xFF);
    uint8_t wire[3 * LINK_UART_WIRE_MAX];
    size_t n = 0;
    for (size_t i = 0; i < 3u; ++i) {
        n += link_uart_encode(&f[i], &wire[n], sizeof(wire) - n);
    }

    /* Two reads, split at each byte. */
    for (size_t cut = 0; cut <= n; ++cut) {
        link_uart_rx_t rx = synced_rx();
        sink_t s;
        memset(&s, 0, sizeof(s));
        feed(&rx, wire, cut, &s);
        feed(&rx, &wire[cut], n - cut, &s);
        CHECK_EQ(s.n, 3);
        for (size_t i = 0; i < s.n && i < 3u; ++i) {
            CHECK(same(&s.f[i], &f[i]));
        }
        CHECK_EQ(s.refused, 0);
    }

    /* Reads of 1 to 7 bytes, the sizes from a fixed pseudo-random run. */
    uint32_t lcg = 12345u;
    for (int round = 0; round < 200; ++round) {
        link_uart_rx_t rx = synced_rx();
        sink_t s;
        memset(&s, 0, sizeof(s));
        size_t at = 0;
        while (at < n) {
            lcg = lcg * 1103515245u + 12345u;
            size_t take = 1u + (lcg >> 16) % 7u;
            if (take > n - at) {
                take = n - at;
            }
            feed(&rx, &wire[at], take, &s);
            at += take;
        }
        CHECK_EQ(s.n, 3);
        CHECK_EQ(s.refused, 0);
    }
}

TEST_CASE(frames_back_to_back_are_all_taken)
{
    /* Every pair of DLCs, each frame's 0x00 the only gap. */
    for (uint8_t a = 0; a <= 8u; ++a) {
        for (uint8_t b = 0; b <= 8u; ++b) {
            const link_can_frame_t fa = frame(0x100u + a, a, 0);
            const link_can_frame_t fb = frame(0x200u + b, b, 0x80);
            uint8_t w[2 * LINK_UART_WIRE_MAX];
            size_t n = link_uart_encode(&fa, w, sizeof(w));
            n += link_uart_encode(&fb, &w[n], sizeof(w) - n);
            link_uart_rx_t rx = synced_rx();
            sink_t s;
            memset(&s, 0, sizeof(s));
            feed(&rx, w, n, &s);
            CHECK_EQ(s.n, 2);
            CHECK(same(&s.f[0], &fa));
            CHECK(same(&s.f[1], &fb));
        }
    }

    /* 150 frames in one stream. */
    static uint8_t w[150 * LINK_UART_WIRE_MAX];
    size_t n = 0;
    for (unsigned i = 0; i < 150u; ++i) {
        const link_can_frame_t f = frame(i * 0x31337u & LINK_UART_ID_MAX,
                                         (uint8_t)(i % 9u), (uint8_t)i);
        n += link_uart_encode(&f, &w[n], sizeof(w) - n);
    }
    link_uart_rx_t rx = synced_rx();
    sink_t s;
    memset(&s, 0, sizeof(s));
    feed(&rx, w, n, &s);
    CHECK_EQ(s.n, 150);
    CHECK_EQ(s.lost, 0);
    CHECK_EQ(rx.counts.frames, 150);
    CHECK_EQ(s.f[149].dlc, 149 % 9);
}

TEST_CASE(the_refused_count_wraps_at_two_to_the_32)
{
    link_uart_rx_t rx = synced_rx();
    rx.counts.bad_cobs = 0xFFFFFFFFu;
    uint32_t refused = 0xFFFFFFFFu;
    static const uint8_t bad[] = {0x05, 0x01, 0x00};
    link_can_frame_t f;
    for (size_t i = 0; i < sizeof(bad); ++i) {
        CHECK(!link_uart_feed(&rx, bad[i], &f, &refused));
    }
    CHECK_EQ(refused, 0);
    CHECK_EQ(rx.counts.bad_cobs, 0);

    /* NULL counts nothing outside the decoder, and the decoder still does. */
    for (size_t i = 0; i < sizeof(bad); ++i) {
        CHECK(!link_uart_feed(&rx, bad[i], &f, NULL));
    }
    CHECK_EQ(rx.counts.bad_cobs, 1);
}

TEST_CASE(bad_arguments_are_refused_rather_than_dereferenced)
{
    uint8_t w[LINK_UART_WIRE_MAX];
    link_can_frame_t f = frame(0x42u, 8, 1);
    CHECK_EQ(link_uart_encode(NULL, w, sizeof(w)), 0);
    CHECK_EQ(link_uart_encode(&f, NULL, sizeof(w)), 0);
    /* The cap: one byte short refused, exact taken. */
    CHECK_EQ(link_uart_encode(&f, w, LINK_UART_WIRE_MAX - 1u), 0);
    CHECK_EQ(link_uart_encode(&f, w, LINK_UART_WIRE_MAX), LINK_UART_WIRE_MAX);
    f.dlc = 9;
    CHECK_EQ(link_uart_encode(&f, w, sizeof(w)), 0);
    f.dlc = 0;
    CHECK_EQ(link_uart_encode(&f, w, LINK_UART_WIRE_MIN - 1u), 0);
    CHECK_EQ(link_uart_encode(&f, w, LINK_UART_WIRE_MIN), LINK_UART_WIRE_MIN);

    link_uart_rx_init(NULL);
    uint32_t refused = 0;
    link_uart_rx_t rx = synced_rx();
    CHECK(!link_uart_feed(NULL, 0x01, &f, &refused));
    /* A NULL frame takes no byte: the decoder is as it was. */
    const link_uart_rx_t before = rx;
    CHECK(!link_uart_feed(&rx, 0x01, NULL, &refused));
    CHECK(!link_uart_feed(&rx, 0x00, NULL, &refused));
    CHECK(memcmp(&before, &rx, sizeof(rx)) == 0);
    CHECK_EQ(refused, 0);
}

int main(void)
{
    RUN(the_wire_bytes_match_a_reference_computed_apart);
    RUN(every_dlc_and_every_byte_value_survives_the_round_trip);
    RUN(a_frame_runs_unchanged_through_the_can_mapping);
    RUN(the_17_byte_maximum_is_taken_and_one_byte_more_is_overlong);
    RUN(every_single_bit_error_fails_the_crc_and_is_counted);
    RUN(any_wire_byte_changed_drops_the_frame_and_counts_it);
    RUN(a_dlc_over_8_is_refused_even_with_a_good_crc);
    RUN(a_length_that_disagrees_with_the_dlc_is_refused);
    RUN(an_identifier_above_29_bits_is_refused_both_ways);
    RUN(a_cobs_code_past_the_end_is_a_cobs_error);
    RUN(a_truncated_frame_is_dropped_and_the_next_one_is_taken);
    RUN(garbage_before_the_first_zero_counts_once);
    RUN(a_rom_boot_message_in_front_is_dropped_and_counted);
    RUN(frames_split_at_every_byte_boundary_decode_the_same);
    RUN(frames_back_to_back_are_all_taken);
    RUN(the_refused_count_wraps_at_two_to_the_32);
    RUN(bad_arguments_are_refused_rather_than_dereferenced);
    return test_summary("link_uart");
}
