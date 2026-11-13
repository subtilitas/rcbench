/*
 * The panel takes a reply's frames in any order (link_host_accept()): a
 * 32-register DATA reply of 8 frames and the 8 ACK frames of a 32-register
 * write, encoded with link_can_encode() and decoded as received, in 1000
 * seeded random orders, in reverse, and with duplicates (the answer to the
 * 50ms repeat) mixed in.  The answer completes on the last new piece and
 * not before it.
 *
 * The IOmcu's transmit queue (can_txq.h) sends a reply in batches of three
 * frames, and the order inside a batch rests on the controller's priority
 * logic; this suite pins that the panel does not depend on it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_can.h"
#include "link_host.h"
#include "link_pages.h"

#define PIECES LINK_CAN_MAX_FRAMES

/* A small fixed generator, so a failure names a seed that repeats. */
static uint32_t rng;
static uint32_t next_rand(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng >> 8;
}

static void shuffle(unsigned *order, unsigned n)
{
    for (unsigned i = n - 1u; i > 0u; --i) {
        const unsigned j = next_rand() % (i + 1u);
        const unsigned t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
}

/* The 8 frames of a DATA reply to a 32-register read of exchange seq. */
static void data_frames(uint8_t seq, link_can_frame_t f[PIECES])
{
    link_msg_t m;
    memset(&m, 0, sizeof(m));
    m.op = LINK_OP_DATA;
    m.page = LINK_PAGE_OUTPUTS;
    m.offset = 0u;
    m.count = 32u;
    m.seq = seq;
    for (unsigned i = 0; i < 32u; ++i) {
        m.regs[i] = (uint16_t)(0x1000u + i);
    }
    CHECK_EQ(link_can_encode(&m, f, PIECES), PIECES);
}

/* The 8 ACK frames the IOmcu answers a 32-register write's fragments
 * with: one each, four registers at offsets 0, 4 ... 28. */
static void ack_frames(uint8_t seq, link_can_frame_t f[PIECES])
{
    for (unsigned p = 0; p < PIECES; ++p) {
        link_msg_t m;
        memset(&m, 0, sizeof(m));
        m.op = LINK_OP_ACK;
        m.page = LINK_PAGE_OUTPUTS;
        m.offset = (uint8_t)(p * 4u);
        m.count = 4u;
        m.seq = seq;
        for (unsigned i = 0; i < 4u; ++i) {
            m.regs[i] = (uint16_t)(0x2000u + p * 4u + i);
        }
        CHECK_EQ(link_can_encode(&m, &f[p], 1u), 1);
    }
}

/*
 * Feeds the pieces in @p order (indices into @p f, repeats allowed) to a
 * host that asked for the window; returns the step the answer completed on,
 * or -1.  Fails the case on a completion before every piece was seen.
 */
static int feed(link_host_t *h, const link_can_frame_t *f,
                const unsigned *order, unsigned n, uint16_t base,
                uint8_t want_op)
{
    bool seen[PIECES] = { false };
    unsigned distinct = 0;
    for (unsigned s = 0; s < n; ++s) {
        link_msg_t part, whole;
        if (!link_can_decode(&f[order[s]], &part)) {
            T_FAIL("piece %u does not decode", order[s]);
            return -1;
        }
        if (!seen[order[s]]) {
            seen[order[s]] = true;
            ++distinct;
        }
        if (link_host_accept(h, &part, 10u, &whole)) {
            if (distinct != PIECES) {
                T_FAIL("complete after %u of %u pieces", distinct,
                       (unsigned)PIECES);
            }
            CHECK_EQ(whole.op, want_op);
            CHECK_EQ(whole.count, 32);
            for (unsigned i = 0; i < 32u; ++i) {
                if (whole.regs[i] != (uint16_t)(base + i)) {
                    T_FAIL("register %u reads %u", i, whole.regs[i]);
                    break;
                }
            }
            return (int)s;
        }
    }
    return -1;
}

static void ask_read(link_host_t *h, bool numbered)
{
    link_host_init(h, 0u);
    h->numbered = numbered;
    link_msg_t req;
    CHECK(link_host_read(h, LINK_PAGE_OUTPUTS, 0u, 32u, 0u, &req));
}

static void ask_write(link_host_t *h, bool numbered)
{
    link_host_init(h, 0u);
    h->numbered = numbered;
    uint16_t regs[32];
    for (unsigned i = 0; i < 32u; ++i) {
        regs[i] = (uint16_t)(0x2000u + i);
    }
    link_msg_t req;
    CHECK(link_host_write(h, LINK_PAGE_OUTPUTS, 0u, 32u, regs, 0u, &req));
}

TEST_CASE(a_read_completes_on_its_last_piece_in_any_order)
{
    rng = 1u;
    for (unsigned round = 0; round < 1000u; ++round) {
        const bool numbered = (round & 1u) != 0u;
        link_host_t h;
        ask_read(&h, numbered);
        link_can_frame_t f[PIECES];
        data_frames(h.seq, f);
        unsigned order[PIECES];
        for (unsigned i = 0; i < PIECES; ++i) {
            order[i] = i;
        }
        shuffle(order, PIECES);
        const int done = feed(&h, f, order, PIECES, 0x1000u, LINK_OP_DATA);
        if (done != (int)PIECES - 1) {
            T_FAIL("round %u: completed on step %d", round, done);
            return;
        }
    }
}

TEST_CASE(a_write_completes_on_its_last_ack_in_any_order)
{
    rng = 2u;
    for (unsigned round = 0; round < 1000u; ++round) {
        const bool numbered = (round & 1u) != 0u;
        link_host_t h;
        ask_write(&h, numbered);
        link_can_frame_t f[PIECES];
        ack_frames(h.seq, f);
        unsigned order[PIECES];
        for (unsigned i = 0; i < PIECES; ++i) {
            order[i] = i;
        }
        shuffle(order, PIECES);
        const int done = feed(&h, f, order, PIECES, 0x2000u, LINK_OP_ACK);
        if (done != (int)PIECES - 1) {
            T_FAIL("round %u: completed on step %d", round, done);
            return;
        }
    }
}

TEST_CASE(reverse_order_completes_on_the_first_frame_sent)
{
    unsigned order[PIECES];
    for (unsigned i = 0; i < PIECES; ++i) {
        order[i] = PIECES - 1u - i;
    }
    link_host_t h;
    link_can_frame_t f[PIECES];
    ask_read(&h, true);
    data_frames(h.seq, f);
    CHECK_EQ(feed(&h, f, order, PIECES, 0x1000u, LINK_OP_DATA), PIECES - 1);
    ask_write(&h, true);
    ack_frames(h.seq, f);
    CHECK_EQ(feed(&h, f, order, PIECES, 0x2000u, LINK_OP_ACK), PIECES - 1);
}

TEST_CASE(duplicates_of_the_repeat_neither_complete_nor_break_it)
{
    rng = 3u;
    for (unsigned round = 0; round < 1000u; ++round) {
        const bool write = (round & 1u) != 0u;
        link_host_t h;
        link_can_frame_t f[PIECES];
        if (write) {
            ask_write(&h, true);
            ack_frames(h.seq, f);
        } else {
            ask_read(&h, true);
            data_frames(h.seq, f);
        }
        /* Each piece once, then up to 8 repeats of pieces already sent,
         * placed anywhere before the last new piece. */
        unsigned base[PIECES];
        for (unsigned i = 0; i < PIECES; ++i) {
            base[i] = i;
        }
        shuffle(base, PIECES);
        unsigned order[2u * PIECES];
        unsigned n = 0;
        for (unsigned i = 0; i < PIECES; ++i) {
            if (i > 0u && i < PIECES - 1u) {
                const unsigned dups = next_rand() % 2u;
                for (unsigned d = 0; d < dups; ++d) {
                    order[n++] = base[next_rand() % i];
                }
            }
            order[n++] = base[i];
        }
        const int done = feed(&h, f, order, n, write ? 0x2000u : 0x1000u,
                              write ? LINK_OP_ACK : LINK_OP_DATA);
        if (done != (int)n - 1) {
            T_FAIL("round %u: completed on step %d of %u", round, done, n);
            return;
        }
    }
}

int main(void)
{
    RUN(a_read_completes_on_its_last_piece_in_any_order);
    RUN(a_write_completes_on_its_last_ack_in_any_order);
    RUN(reverse_order_completes_on_the_first_frame_sent);
    RUN(duplicates_of_the_repeat_neither_complete_nor_break_it);
    return test_summary("link_reply_order");
}
