/**
 * @file test_bleota.c
 * @brief protocols/bleota's codec at its limits: every control write and
 *        notification round-trips, every wrong length and unknown kind is
 *        refused, a CREDIT whose limit lies before its next offset and a
 *        FAIL code past the last are refused, and a chunk's length and the
 *        MTU's chunk size hold at both ends; random writes decode only
 *        to their own bytes, and the device side takes them without a read
 *        or write outside its buffers.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "bleota.h"

TEST_CASE(the_uuids_share_the_base_and_differ_in_their_first_byte)
{
    CHECK_EQ(bleota_uuid_svc[0], 0x00);
    CHECK_EQ(bleota_uuid_ctrl[0], 0x01);
    CHECK_EQ(bleota_uuid_data[0], 0x02);
    CHECK_EQ(bleota_uuid_svc[15], 0x70);
    CHECK(memcmp(bleota_uuid_svc + 1, bleota_uuid_data + 1, 15) == 0);
}

TEST_CASE(begin_round_trips_at_the_largest_values)
{
    uint8_t b[BLEOTA_LEN_BEGIN];
    const uint16_t ver[3] = { 65535u, 0u, 65535u };
    CHECK_EQ(bleota_begin_encode(BLEOTA_KIND_NODE_BAL, 0xFFFFFFFFu, ver,
                                 BLEOTA_F_REINSTALL, b), BLEOTA_LEN_BEGIN);
    bleota_ctrl_t c;
    CHECK(bleota_ctrl_decode(b, sizeof b, &c));
    CHECK_EQ(c.op, BLEOTA_OP_BEGIN);
    CHECK_EQ(c.proto, BLEOTA_PROTO);
    CHECK_EQ(c.kind, BLEOTA_KIND_NODE_BAL);
    CHECK_EQ(c.flags, BLEOTA_F_REINSTALL);
    CHECK_EQ(c.size, 0xFFFFFFFFu);
    CHECK_EQ(c.ver[0], 65535);
    CHECK_EQ(c.ver[1], 0);
    CHECK_EQ(c.ver[2], 65535);
    CHECK(!bleota_ctrl_decode(b, sizeof b - 1u, &c));
    uint8_t longer[BLEOTA_LEN_BEGIN + 1u];
    memcpy(longer, b, sizeof b);
    longer[BLEOTA_LEN_BEGIN] = 0u;
    CHECK(!bleota_ctrl_decode(longer, sizeof longer, &c));
    CHECK_EQ(bleota_begin_encode(1u, 1u, NULL, 0u, b), 0u);
    CHECK_EQ(bleota_begin_encode(1u, 1u, ver, 0u, NULL), 0u);
}

TEST_CASE(hash_carries_each_half_and_refuses_a_third_part)
{
    uint8_t sha[32];
    for (unsigned i = 0; i < 32u; ++i) {
        sha[i] = (uint8_t)(0xA0u + i);
    }
    uint8_t b[BLEOTA_LEN_HASH];
    bleota_ctrl_t c;
    for (uint8_t part = 0; part < 2u; ++part) {
        CHECK_EQ(bleota_hash_encode(sha, part, b), BLEOTA_LEN_HASH);
        CHECK(bleota_ctrl_decode(b, sizeof b, &c));
        CHECK_EQ(c.op, BLEOTA_OP_HASH);
        CHECK_EQ(c.part, part);
        CHECK(memcmp(c.half, sha + 16u * part, 16u) == 0);
    }
    CHECK_EQ(bleota_hash_encode(sha, 2u, b), 0u);
    b[1] = 2u;
    CHECK(!bleota_ctrl_decode(b, sizeof b, &c));
    b[1] = 1u;
    CHECK(!bleota_ctrl_decode(b, sizeof b - 1u, &c));
}

TEST_CASE(the_one_byte_ops_take_one_byte_and_nothing_else)
{
    uint8_t b[2] = { 0, 0 };
    bleota_ctrl_t c;
    const uint8_t ops[3] = { BLEOTA_OP_END, BLEOTA_OP_ABORT, BLEOTA_OP_POLL };
    for (unsigned i = 0; i < 3u; ++i) {
        CHECK_EQ(bleota_op_encode(ops[i], b), 1u);
        CHECK(bleota_ctrl_decode(b, 1u, &c));
        CHECK_EQ(c.op, ops[i]);
        CHECK(!bleota_ctrl_decode(b, 2u, &c));
    }
    CHECK_EQ(bleota_op_encode(BLEOTA_OP_BEGIN, b), 0u);
    CHECK_EQ(bleota_op_encode(0x06u, b), 0u);
    b[0] = 0x00u;
    CHECK(!bleota_ctrl_decode(b, 1u, &c));
    b[0] = 0x06u;
    CHECK(!bleota_ctrl_decode(b, 1u, &c));
    CHECK(!bleota_ctrl_decode(b, 0u, &c));
    CHECK(!bleota_ctrl_decode(NULL, 1u, &c));
}

TEST_CASE(a_notification_round_trips_and_its_limits_hold)
{
    bleota_status_t s = { .ev = BLEOTA_EV_CREDIT, .code = 0u,
                          .next = 0xFFFFFFF0u, .limit = 0xFFFFFFFFu,
                          .secs = 65535u };
    uint8_t b[BLEOTA_LEN_STATUS];
    CHECK_EQ(bleota_status_encode(&s, b), BLEOTA_LEN_STATUS);
    bleota_status_t r;
    CHECK(bleota_status_decode(b, sizeof b, &r));
    CHECK_EQ(r.ev, BLEOTA_EV_CREDIT);
    CHECK_EQ(r.next, 0xFFFFFFF0u);
    CHECK_EQ(r.limit, 0xFFFFFFFFu);
    CHECK_EQ(r.secs, 65535);
    CHECK(!bleota_status_decode(b, sizeof b - 1u, &r));
    /* A limit before the next offset is no credit; equal is. */
    s.limit = s.next - 1u;
    (void)bleota_status_encode(&s, b);
    CHECK(!bleota_status_decode(b, sizeof b, &r));
    s.limit = s.next;
    (void)bleota_status_encode(&s, b);
    CHECK(bleota_status_decode(b, sizeof b, &r));
    /* FAIL's code up to the last defined one. */
    s.ev = BLEOTA_EV_FAIL;
    s.code = BLEOTA_E_MAX;
    (void)bleota_status_encode(&s, b);
    CHECK(bleota_status_decode(b, sizeof b, &r));
    CHECK_EQ(r.code, BLEOTA_E_MAX);
    s.code = BLEOTA_E_MAX + 1u;
    (void)bleota_status_encode(&s, b);
    CHECK(!bleota_status_decode(b, sizeof b, &r));
    /* Events outside WAIT to FAIL. */
    s.ev = BLEOTA_EV_WAIT - 1u;
    CHECK_EQ(bleota_status_encode(&s, b), 0u);
    b[0] = BLEOTA_EV_FAIL + 1u;
    CHECK(!bleota_status_decode(b, sizeof b, &r));
    b[0] = BLEOTA_EV_WAIT - 1u;
    CHECK(!bleota_status_decode(b, sizeof b, &r));
    CHECK(!bleota_status_decode(NULL, sizeof b, &r));
}

TEST_CASE(a_chunk_holds_one_to_240_image_bytes)
{
    uint8_t b[BLEOTA_CHUNK_HDR + BLEOTA_CHUNK_MAX + 1u];
    memset(b, 0x5A, sizeof b);
    bleota_chunk_header(0x01020304u, b);
    uint32_t off = 0;
    const uint8_t *p = NULL;
    uint16_t len = 0;
    CHECK(!bleota_chunk_decode(b, BLEOTA_CHUNK_HDR, &off, &p, &len));
    CHECK(bleota_chunk_decode(b, BLEOTA_CHUNK_HDR + 1u, &off, &p, &len));
    CHECK_EQ(off, 0x01020304u);
    CHECK_EQ(len, 1);
    CHECK(p == b + BLEOTA_CHUNK_HDR);
    CHECK(bleota_chunk_decode(b, BLEOTA_CHUNK_HDR + BLEOTA_CHUNK_MAX, &off,
                              &p, &len));
    CHECK_EQ(len, BLEOTA_CHUNK_MAX);
    CHECK(!bleota_chunk_decode(b, sizeof b, &off, &p, &len));
    CHECK(!bleota_chunk_decode(NULL, 8u, &off, &p, &len));
}

TEST_CASE(the_chunk_fills_the_mtu_up_to_240_bytes)
{
    CHECK_EQ(bleota_chunk_bytes(0u), 16);
    CHECK_EQ(bleota_chunk_bytes(22u), 16);
    CHECK_EQ(bleota_chunk_bytes(BLEOTA_MTU_MIN), 16);
    CHECK_EQ(bleota_chunk_bytes(100u), 93);
    CHECK_EQ(bleota_chunk_bytes(BLEOTA_MTU - 1u), BLEOTA_CHUNK_MAX - 1u);
    CHECK_EQ(bleota_chunk_bytes(BLEOTA_MTU), BLEOTA_CHUNK_MAX);
    CHECK_EQ(bleota_chunk_bytes(517u), BLEOTA_CHUNK_MAX);
    CHECK_EQ(BLEOTA_CHUNK_HDR + BLEOTA_CHUNK_MAX + 3u, BLEOTA_MTU);
}

/**
 * @brief xorshift32: the fuzz's numbers, the same on every run.
 * @param s the state
 * @return the next number
 */
static uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/**
 * @brief Re-encode a decoded control write.
 * @param c   the write
 * @param out BLEOTA_CTRL_MAX bytes
 * @return its length, 0 when the encoders do not give this form
 */
static size_t ctrl_encode(const bleota_ctrl_t *c, uint8_t *out)
{
    if (c->op == BLEOTA_OP_BEGIN) {
        return c->proto == BLEOTA_PROTO
            ? bleota_begin_encode(c->kind, c->size, c->ver, c->flags, out)
            : 0u;
    }
    if (c->op == BLEOTA_OP_HASH) {
        uint8_t sha[32];
        memset(sha, 0, sizeof sha);
        memcpy(sha + (size_t)16u * c->part, c->half, 16u);
        const size_t n = bleota_hash_encode(sha, c->part, out);
        return n;
    }
    return bleota_op_encode(c->op, out);
}

TEST_CASE(random_writes_decode_only_to_their_own_bytes)
{
    /* Each write is a heap block of exactly its length, so a read past it
     * is a fault under AddressSanitizer.  The same bytes go to a device as
     * CTRL and as DATA, with its key toggled at random. */
    static uint8_t ring[BLEOTA_WINDOW];
    const bleota_dev_cfg_t cfg = { BLEOTA_KIND_DIAL, BLEOTA_CONFIRM_HOLD,
                                   4096u, { 0u, 23u, 12u } };
    bleota_dev_t d;
    CHECK(bleota_dev_init(&d, &cfg, ring, sizeof ring));
    uint32_t seed = 0x6D2B79F5u;
    uint32_t now = 0u;
    unsigned bad = 0;
    unsigned ctrl = 0;
    for (unsigned i = 0; i < 200000u; ++i) {
        /* A quarter of the writes get a control write's length. */
        static const size_t lens[3] = { BLEOTA_LEN_OP, BLEOTA_LEN_BEGIN,
                                        BLEOTA_LEN_HASH };
        const size_t len = (rnd(&seed) & 3u) == 0u
            ? lens[rnd(&seed) % 3u]
            : rnd(&seed) % (BLEOTA_CHUNK_HDR + BLEOTA_CHUNK_MAX + 3u);
        uint8_t *b = malloc(len == 0u ? 1u : len);
        REQUIRE_ALLOC(b);
        for (size_t k = 0; k < len; ++k) {
            b[k] = (uint8_t)rnd(&seed);
        }
        if (len > 0u && (rnd(&seed) & 3u) != 0u) {
            b[0] = (uint8_t)(1u + rnd(&seed) % 5u);
        }
        if (len == BLEOTA_LEN_BEGIN && (rnd(&seed) & 1u) != 0u) {
            b[1] = BLEOTA_PROTO;
            b[2] = (uint8_t)(rnd(&seed) % 4u);
            b[3] &= 1u;
            b[6] = b[7] = 0u;
        }
        bleota_ctrl_t c;
        uint8_t again[BLEOTA_LEN_STATUS + BLEOTA_CTRL_MAX];
        if (bleota_ctrl_decode(b, len, &c)) {
            ++ctrl;
            const size_t n = ctrl_encode(&c, again);
            if ((n != 0u || c.op != BLEOTA_OP_BEGIN)
                && (n != len || memcmp(again, b, len) != 0)) {
                ++bad;
            }
        }
        bleota_status_t s;
        if (bleota_status_decode(b, len, &s)
            && (bleota_status_encode(&s, again) != len
                || memcmp(again, b, len) != 0)) {
            ++bad;
        }
        uint32_t off = 0;
        const uint8_t *p = NULL;
        uint16_t got = 0;
        if (bleota_chunk_decode(b, len, &off, &p, &got)
            && (p != b + BLEOTA_CHUNK_HDR
                || got != len - BLEOTA_CHUNK_HDR)) {
            ++bad;
        }
        now += rnd(&seed) % 700u;
        bleota_dev_key(&d, (rnd(&seed) & 7u) == 0u, now);
        if ((rnd(&seed) & 1u) != 0u) {
            bleota_dev_ctrl(&d, b, len, now);
        } else {
            bleota_dev_data(&d, b, len, now);
        }
        free(b);
        uint8_t out[BLEOTA_LEN_STATUS];
        while (bleota_dev_status(&d, out)) {
            bleota_status_t st;
            if (!bleota_status_decode(out, sizeof out, &st)) {
                ++bad;
            }
        }
        uint8_t w[BLEOTA_WINDOW];
        uint8_t digest[32];
        switch (bleota_dev_poll(&d, now)) {
        case BLEOTA_DO_OPEN:
            bleota_dev_opened(&d, (rnd(&seed) & 7u) != 0u, now);
            break;
        case BLEOTA_DO_WRITE:
            (void)bleota_dev_take(&d, w, sizeof w);
            bleota_dev_written(&d, true, now);
            break;
        case BLEOTA_DO_VERIFY:
            memset(digest, 0, sizeof digest);
            (void)bleota_dev_verify(&d, digest, now);
            break;
        case BLEOTA_DO_INSTALL:
            bleota_dev_installed(&d, false, BLEOTA_E_IMAGE, now);
            break;
        case BLEOTA_DO_CLOSE:
            bleota_dev_closed(&d);
            break;
        default:
            break;
        }
        if (d.wr > d.rx || d.rx > d.size || d.rx - d.wr > sizeof ring) {
            ++bad;
        }
    }
    CHECK_EQ(bad, 0u);
    CHECK(ctrl > 10000u);
}

int main(void)
{
    RUN(the_uuids_share_the_base_and_differ_in_their_first_byte);
    RUN(begin_round_trips_at_the_largest_values);
    RUN(hash_carries_each_half_and_refuses_a_third_part);
    RUN(the_one_byte_ops_take_one_byte_and_nothing_else);
    RUN(a_notification_round_trips_and_its_limits_hold);
    RUN(a_chunk_holds_one_to_240_image_bytes);
    RUN(the_chunk_fills_the_mtu_up_to_240_bytes);
    RUN(random_writes_decode_only_to_their_own_bytes);
    return test_summary("bleota");
}
