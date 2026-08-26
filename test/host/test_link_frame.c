/*
 * The link codec, fed everything an RS485 pair beside 300 A of switching
 * current can do to it.
 *
 * The two properties worth testing here are not "does it round-trip" -- that
 * is table stakes -- but the two the wire actually depends on: a corrupt
 * frame is never reported, and a good frame that begins inside a burst of
 * noise is still found rather than lost with the noise.
 */
#include <string.h>

#include "greatest.h"

#include "link_crc.h"
#include "link_frame.h"

static link_msg_t a_write(void)
{
    link_msg_t m = { 0 };
    m.op     = LINK_OP_WRITE;
    m.page   = 0x12;
    m.offset = 2;
    m.count  = 4;
    for (int i = 0; i < 4; ++i) {
        m.regs[i] = (uint16_t)(0x1000 + i * 0x0111);
    }
    return m;
}

/* Feed a whole buffer through the decoder and report how many frames came
 * out, leaving the last one in *out. */
static int feed(link_decoder_t *d, const uint8_t *bytes, size_t n, link_msg_t *out)
{
    int frames = 0;
    for (size_t i = 0; i < n; ++i) {
        if (link_decode_byte(d, bytes[i], out)) {
            ++frames;
        }
    }
    return frames;
}

TEST_CASE(a_frame_round_trips)
{
    const link_msg_t sent = a_write();
    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_encode(wire, sizeof(wire), &sent);
    CHECK(n > 0);

    link_decoder_t d;
    link_decoder_reset(&d);
    link_msg_t got = { 0 };
    CHECK_EQ(feed(&d, wire, n, &got), 1);

    CHECK_EQ(got.op, sent.op);
    CHECK_EQ(got.page, sent.page);
    CHECK_EQ(got.offset, sent.offset);
    CHECK_EQ(got.count, sent.count);
    for (int i = 0; i < sent.count; ++i) {
        CHECK_EQ(got.regs[i], sent.regs[i]);
    }
}

/* A READ is the question, so it carries no registers however many it asks
 * for -- and asking for a whole page must still fit the wire. */
TEST_CASE(a_read_carries_no_payload)
{
    link_msg_t m = { 0 };
    m.op = LINK_OP_READ; m.page = 3; m.offset = 0; m.count = LINK_MAX_REGS;

    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_encode(wire, sizeof(wire), &m);
    CHECK_EQ(n, (size_t)(LINK_HEADER_BYTES + 2));

    link_decoder_t d;
    link_decoder_reset(&d);
    link_msg_t got = { 0 };
    CHECK_EQ(feed(&d, wire, n, &got), 1);
    CHECK_EQ(got.count, LINK_MAX_REGS);
}

/* A whole-page transfer is the largest thing the wire carries, and the
 * buffer is sized from it rather than from a round number. */
TEST_CASE(a_whole_page_is_the_largest_frame)
{
    link_msg_t m = { 0 };
    m.op = LINK_OP_DATA; m.page = 1; m.offset = 0; m.count = LINK_MAX_REGS;
    for (int i = 0; i < LINK_MAX_REGS; ++i) {
        m.regs[i] = (uint16_t)(i * 2027u);
    }

    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_encode(wire, sizeof(wire), &m);
    CHECK_EQ(n, (size_t)LINK_MAX_FRAME);

    link_decoder_t d;
    link_decoder_reset(&d);
    link_msg_t got = { 0 };
    CHECK_EQ(feed(&d, wire, n, &got), 1);
    for (int i = 0; i < LINK_MAX_REGS; ++i) {
        CHECK_EQ(got.regs[i], m.regs[i]);
    }
}

TEST_CASE(the_encoder_refuses_what_it_cannot_send)
{
    uint8_t wire[LINK_MAX_FRAME];
    link_msg_t m = a_write();

    CHECK_EQ(link_encode(NULL, sizeof(wire), &m), 0u);
    CHECK_EQ(link_encode(wire, sizeof(wire), NULL), 0u);
    CHECK_EQ(link_encode(wire, 4, &m), 0u);          /* no room */

    m = a_write(); m.op = 0x7F;                       /* not an op */
    CHECK_EQ(link_encode(wire, sizeof(wire), &m), 0u);

    m = a_write(); m.count = LINK_MAX_REGS + 1;       /* wider than a page */
    CHECK_EQ(link_encode(wire, sizeof(wire), &m), 0u);

    m = a_write(); m.offset = 30; m.count = 4;        /* runs off the end */
    CHECK_EQ(link_encode(wire, sizeof(wire), &m), 0u);
}

/* The property the wire depends on.  Every single-bit flip anywhere in the
 * frame -- header, payload or CRC -- must fail to decode. */
TEST_CASE(no_single_bit_flip_is_ever_accepted)
{
    const link_msg_t sent = a_write();
    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_encode(wire, sizeof(wire), &sent);

    for (size_t byte = 0; byte < n; ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            uint8_t corrupt[LINK_MAX_FRAME];
            memcpy(corrupt, wire, n);
            corrupt[byte] ^= (uint8_t)(1u << bit);

            link_decoder_t d;
            link_decoder_reset(&d);
            link_msg_t got = { 0 };
            if (feed(&d, corrupt, n, &got) != 0) {
                T_FAIL("byte %u bit %d decoded as a frame", (unsigned)byte, bit);
            }
        }
    }
}

/* A mid-frame turnaround on an auto-direction transceiver looks exactly like
 * this, so it is not a hypothetical. */
TEST_CASE(no_truncation_is_ever_accepted)
{
    const link_msg_t sent = a_write();
    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_encode(wire, sizeof(wire), &sent);

    for (size_t len = 0; len < n; ++len) {
        link_decoder_t d;
        link_decoder_reset(&d);
        link_msg_t got = { 0 };
        CHECK_EQ(feed(&d, wire, len, &got), 0);
    }
}

/* The other half of the contract: noise in front of a frame must cost the
 * noise, not the frame. */
TEST_CASE(a_frame_behind_noise_is_still_found)
{
    const link_msg_t sent = a_write();
    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_encode(wire, sizeof(wire), &sent);

    /* Including bytes that look like a sync, which is the case that breaks a
     * decoder that trusts the first 0xA5 it sees and skips a whole frame's
     * worth of bytes after it. */
    static const uint8_t noise[] = {
        0x00, 0xFF, LINK_SYNC, 0x40, 0x13, LINK_SYNC, LINK_SYNC, 0x02, 0xC3
    };

    uint8_t stream[sizeof(noise) + LINK_MAX_FRAME];
    memcpy(stream, noise, sizeof(noise));
    memcpy(stream + sizeof(noise), wire, n);

    link_decoder_t d;
    link_decoder_reset(&d);
    link_msg_t got = { 0 };
    CHECK_EQ(feed(&d, stream, sizeof(noise) + n, &got), 1);
    CHECK_EQ(got.page, sent.page);
    CHECK(d.resyncs > 0);
}

/* A corrupt frame followed by a good one: on a polled link the good one is
 * the reply, and a decoder that waits for an idle gap loses it. */
TEST_CASE(a_good_frame_after_a_corrupt_one_still_arrives)
{
    const link_msg_t sent = a_write();
    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_encode(wire, sizeof(wire), &sent);

    uint8_t stream[2 * LINK_MAX_FRAME];
    memcpy(stream, wire, n);
    stream[3] ^= 0x20u;                    /* break the first one */
    memcpy(stream + n, wire, n);           /* and send it again, intact */

    link_decoder_t d;
    link_decoder_reset(&d);
    link_msg_t got = { 0 };
    CHECK_EQ(feed(&d, stream, 2 * n, &got), 1);
    CHECK_EQ(got.page, sent.page);
    CHECK_EQ(got.regs[0], sent.regs[0]);
    CHECK(d.crc_errors > 0);
}

/* Back-to-back frames with no gap: the decoder must keep whatever arrived
 * behind the frame it just consumed. */
TEST_CASE(frames_arriving_back_to_back_are_all_reported)
{
    uint8_t stream[4 * LINK_MAX_FRAME];
    size_t n = 0;
    for (int i = 0; i < 4; ++i) {
        link_msg_t m = a_write();
        m.page = (uint8_t)i;
        n += link_encode(stream + n, sizeof(stream) - n, &m);
    }

    link_decoder_t d;
    link_decoder_reset(&d);
    link_msg_t got = { 0 };
    CHECK_EQ(feed(&d, stream, n, &got), 4);
    CHECK_EQ(got.page, 3);
    CHECK_EQ(d.frames, 4u);
}

/* A stream of pure noise must never manufacture a frame, and must never run
 * the buffer off its end.  Deterministic so a failure is reproducible. */
TEST_CASE(pure_noise_never_manufactures_a_frame)
{
    link_decoder_t d;
    link_decoder_reset(&d);
    link_msg_t got = { 0 };

    uint32_t x = 0x13579BDFu;
    for (int i = 0; i < 200000; ++i) {
        x = x * 1664525u + 1013904223u;      /* Numerical Recipes LCG */
        const uint8_t byte = (uint8_t)(x >> 24);
        if (link_decode_byte(&d, byte, &got)) {
            T_FAIL("noise decoded as a frame at byte %d", i);
        }
        CHECK(d.len <= LINK_MAX_FRAME);
    }
}

/* A frame that verifies but claims an impossible shape is a version
 * mismatch or a bug at the far end.  It is still not something to act on. */
TEST_CASE(a_verified_frame_with_an_impossible_shape_is_refused)
{
    uint8_t wire[LINK_MAX_FRAME];
    link_msg_t m = a_write();
    size_t n = link_encode(wire, sizeof(wire), &m);

    /* Rewrite the count so the length no longer agrees with it, then repair
     * the CRC -- exactly what a well-formed sender with a different idea of
     * the protocol would put on the wire. */
    wire[5] = LINK_MAX_REGS;
    const uint16_t crc = link_crc(LINK_CRC_INIT, wire, n - 2);
    wire[n - 2] = (uint8_t)(crc & 0xFFu);
    wire[n - 1] = (uint8_t)(crc >> 8);

    link_decoder_t d;
    link_decoder_reset(&d);
    link_msg_t got = { 0 };
    CHECK_EQ(feed(&d, wire, n, &got), 0);
    CHECK_EQ(d.crc_errors, 0u);   /* the CRC was fine; the shape was not */
}

TEST_CASE(a_reset_decoder_holds_nothing)
{
    link_decoder_t d;
    link_decoder_reset(&d);
    link_msg_t got = { 0 };

    const link_msg_t sent = a_write();
    uint8_t wire[LINK_MAX_FRAME];
    const size_t n = link_encode(wire, sizeof(wire), &sent);

    feed(&d, wire, n - 1, &got);     /* half a frame in the buffer */
    CHECK(d.len > 0);
    link_decoder_reset(&d);
    CHECK_EQ(d.len, 0);
    CHECK_EQ(d.frames, 0u);

    /* The tail of the abandoned frame must not complete anything. */
    CHECK_EQ(feed(&d, wire + n - 1, 1, &got), 0);
}

int main(void)
{
    RUN(a_frame_round_trips);
    RUN(a_read_carries_no_payload);
    RUN(a_whole_page_is_the_largest_frame);
    RUN(the_encoder_refuses_what_it_cannot_send);
    RUN(no_single_bit_flip_is_ever_accepted);
    RUN(no_truncation_is_ever_accepted);
    RUN(a_frame_behind_noise_is_still_found);
    RUN(a_good_frame_after_a_corrupt_one_still_arrives);
    RUN(frames_arriving_back_to_back_are_all_reported);
    RUN(pure_noise_never_manufactures_a_frame);
    RUN(a_verified_frame_with_an_impossible_shape_is_refused);
    RUN(a_reset_decoder_holds_nothing);
    return test_summary("link_frame");
}
