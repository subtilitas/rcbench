/*
 * The CAN transmit queue (can_txq.h) on a modelled MCP2515: three transmit
 * buffers with TXREQ and TXP, sent as the datasheet orders them -- the
 * requested buffer of the highest TXP first, and of equal TXP the highest
 * buffer number first -- one frame per wire step.  The model records every
 * call and every frame it puts on the wire.  Its READ STATUS also carries
 * what the part reports beside TXREQ: each buffer's TXnIF, latched from its
 * first send on (the queue never clears it), and RX0IF and RX1IF at
 * random.
 *
 * At the limits: a reply of 8 into 7 free places, 1, 4, 8 and 16 frames,
 * a controller that never sends, the stuck rule at 4999us and 5000us and
 * across the wrap of the 32-bit clock, a DLC of 9, the peak and the busy
 * flag of the last batch.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "can_txq.h"
#include "mcp2515.h"

/** The modelled controller. */
typedef struct {
    link_can_frame_t buf[3];
    uint8_t txp[3];
    bool    txreq[3];
    unsigned status_calls, load_calls, request_calls, abort_calls;
    unsigned load_on_busy;          /* loads into a buffer with TXREQ set */
    bool    txif[3];                /* latched by a send, never cleared   */
    uint32_t rng;                   /* RX0IF and RX1IF at random          */
    link_can_frame_t wire[64];
    unsigned on_wire;
} fake_t;

static fake_t F;

static uint8_t f_status(void *ctx)
{
    (void)ctx;
    ++F.status_calls;
    uint8_t st = 0;
    if (F.txreq[0]) { st |= MCP2515_STATUS_TX0REQ; }
    if (F.txreq[1]) { st |= MCP2515_STATUS_TX1REQ; }
    if (F.txreq[2]) { st |= MCP2515_STATUS_TX2REQ; }
    if (F.txif[0]) { st |= MCP2515_STATUS_TX0IF; }
    if (F.txif[1]) { st |= MCP2515_STATUS_TX1IF; }
    if (F.txif[2]) { st |= MCP2515_STATUS_TX2IF; }
    F.rng = F.rng * 1664525u + 1013904223u;
    st |= (uint8_t)((F.rng >> 24)
                    & (MCP2515_STATUS_RX0IF | MCP2515_STATUS_RX1IF));
    return st;
}

static void f_load(void *ctx, uint8_t b, uint8_t txp, const link_can_frame_t *f)
{
    (void)ctx;
    ++F.load_calls;
    if (b >= 3u) {
        return;
    }
    if (F.txreq[b]) {
        ++F.load_on_busy;
    }
    F.buf[b] = *f;
    F.txp[b] = txp;
}

static void f_request(void *ctx, uint8_t mask)
{
    (void)ctx;
    ++F.request_calls;
    for (unsigned b = 0; b < 3u; ++b) {
        if ((mask & (1u << b)) != 0u) {
            F.txreq[b] = true;
        }
    }
}

static void f_abort(void *ctx)
{
    (void)ctx;
    ++F.abort_calls;
    F.txreq[0] = F.txreq[1] = F.txreq[2] = false;
}

static const can_txq_ops_t OPS = { f_status, f_load, f_request, f_abort,
                                   NULL };

/** One frame onto the wire, as the MCP2515 picks it; false with none. */
static bool wire_step(void)
{
    int pick = -1;
    uint8_t best = 0u;
    for (int b = 0; b < 3; ++b) {
        if (!F.txreq[b]) {
            continue;
        }
        if (pick < 0 || F.txp[b] > best || (F.txp[b] == best && b > pick)) {
            pick = b;
            best = F.txp[b];
        }
    }
    if (pick < 0) {
        return false;
    }
    if (F.on_wire < 64u) {
        F.wire[F.on_wire++] = F.buf[pick];
    }
    F.txreq[pick] = false;
    F.txif[pick] = true;
    return true;
}

static void wire_drain(void)
{
    while (wire_step()) {
    }
}

static void fake_reset(void)
{
    memset(&F, 0, sizeof(F));
    F.rng = 12345u;
}

static link_can_frame_t frame(uint32_t n)
{
    link_can_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id  = 0x100000u + n;
    f.dlc = 8u;
    f.data[0] = (uint8_t)n;
    return f;
}

static void frames(link_can_frame_t *out, uint32_t first, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        out[i] = frame(first + (uint32_t)i);
    }
}

/* Pumps and drains the wire until the queue is idle; the pump count. */
static unsigned run_out(can_txq_t *q, uint32_t *now)
{
    unsigned pumps = 0;
    while (can_txq_busy(q) && pumps < 100u) {
        can_txq_pump(q, *now);
        wire_drain();
        *now += 400u;
        ++pumps;
    }
    return pumps;
}

TEST_CASE(a_reply_goes_whole_or_not_at_all)
{
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    link_can_frame_t f[16];
    frames(f, 0u, 9u);
    CHECK(can_txq_put(&q, f, 9u));          /* 7 places left */
    frames(f, 100u, 8u);
    CHECK(!can_txq_put(&q, f, 8u));
    CHECK_EQ(q.count, 9);
    CHECK_EQ(q.dropped, 8);
    /* 7 fit exactly. */
    CHECK(can_txq_put(&q, f, 7u));
    CHECK_EQ(q.count, 16);
    CHECK_EQ(q.dropped, 8);
    /* Nothing went near the controller. */
    CHECK_EQ(F.status_calls + F.load_calls + F.request_calls, 0);
    /* A NULL reply is refused and counted; an empty one is taken. */
    CHECK(!can_txq_put(&q, NULL, 2u));
    CHECK_EQ(q.dropped, 10);
    CHECK(can_txq_put(&q, f, 0u));
    CHECK_EQ(q.dropped, 10);
}

TEST_CASE(frames_leave_in_the_order_they_were_put)
{
    static const size_t sizes[] = { 1u, 4u, 8u, 16u };
    for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); ++k) {
        fake_reset();
        can_txq_t q;
        can_txq_init(&q, &OPS);
        link_can_frame_t f[16];
        frames(f, 0u, sizes[k]);
        CHECK(can_txq_put(&q, f, sizes[k]));
        uint32_t now = 0u;
        (void)run_out(&q, &now);
        CHECK_EQ(F.on_wire, sizes[k]);
        for (unsigned i = 0; i < F.on_wire; ++i) {
            if (F.wire[i].id != 0x100000u + i) {
                T_FAIL("%zu frames: frame %u on the wire is %lu", sizes[k], i,
                       (unsigned long)(F.wire[i].id - 0x100000u));
            }
        }
    }

    /* Puts between pumps, with a batch on the wire. */
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    link_can_frame_t f[8];
    frames(f, 0u, 4u);
    CHECK(can_txq_put(&q, f, 4u));
    can_txq_pump(&q, 0u);              /* 0, 1, 2 requested */
    (void)wire_step();                 /* 0 leaves */
    frames(f, 4u, 5u);
    CHECK(can_txq_put(&q, f, 5u));     /* 3 then 4..8 queued */
    can_txq_pump(&q, 100u);            /* 1 and 2 still requested */
    uint32_t now = 200u;
    (void)run_out(&q, &now);
    CHECK_EQ(F.on_wire, 9);
    for (unsigned i = 0; i < F.on_wire; ++i) {
        CHECK_EQ(F.wire[i].id, 0x100000u + i);
    }
}

TEST_CASE(equal_txp_would_reverse_the_batch)
{
    /* The model, not the queue: at equal TXP the highest buffer number
     * goes first, so equal priorities send TXB2, TXB1, TXB0. */
    fake_reset();
    for (uint8_t b = 0; b < 3u; ++b) {
        f_load(NULL, b, 2u, &(link_can_frame_t){ .id = b, .dlc = 0u });
    }
    f_request(NULL, 0x07u);
    wire_drain();
    CHECK_EQ(F.on_wire, 3);
    CHECK_EQ(F.wire[0].id, 2);
    CHECK_EQ(F.wire[1].id, 1);
    CHECK_EQ(F.wire[2].id, 0);
    /* And the higher TXP goes first whatever the buffer number. */
    fake_reset();
    f_load(NULL, 0u, 1u, &(link_can_frame_t){ .id = 10u, .dlc = 0u });
    f_load(NULL, 2u, 0u, &(link_can_frame_t){ .id = 12u, .dlc = 0u });
    f_load(NULL, 1u, 3u, &(link_can_frame_t){ .id = 11u, .dlc = 0u });
    f_request(NULL, 0x07u);
    wire_drain();
    CHECK_EQ(F.wire[0].id, 11);
    CHECK_EQ(F.wire[1].id, 10);
    CHECK_EQ(F.wire[2].id, 12);
}

TEST_CASE(nothing_is_loaded_while_a_buffer_is_requested)
{
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    link_can_frame_t f[16];
    frames(f, 0u, 16u);
    CHECK(can_txq_put(&q, f, 16u));
    uint32_t now = 0u;
    /* One frame leaves per step, and the queue is pumped between every
     * step: TXB0 and TXB1 empty with TXB2 still requested loads nothing. */
    for (unsigned i = 0; i < 200u && can_txq_busy(&q); ++i) {
        can_txq_pump(&q, now);
        (void)wire_step();
        now += 150u;
    }
    CHECK(!can_txq_busy(&q));
    CHECK_EQ(F.on_wire, 16);
    CHECK_EQ(F.load_on_busy, 0);
    CHECK_EQ(F.load_calls, 16);
    CHECK_EQ(F.request_calls, 6);
}

TEST_CASE(one_pump_is_one_status_at_most_three_loads_one_request)
{
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    /* Idle: not one call. */
    can_txq_pump(&q, 0u);
    CHECK_EQ(F.status_calls, 0);

    link_can_frame_t f[8];
    frames(f, 0u, 8u);
    CHECK(can_txq_put(&q, f, 8u));
    can_txq_pump(&q, 0u);
    CHECK_EQ(F.status_calls, 1);
    CHECK_EQ(F.load_calls, 3);
    CHECK_EQ(F.request_calls, 1);
    CHECK_EQ(q.count, 5);
    wire_drain();
    can_txq_pump(&q, 500u);
    CHECK_EQ(F.status_calls, 2);
    CHECK_EQ(F.load_calls, 6);
    CHECK_EQ(F.request_calls, 2);
    /* Two frames left: two loads, one request for TXB0 and TXB1. */
    wire_drain();
    can_txq_pump(&q, 1000u);
    CHECK_EQ(F.status_calls, 3);
    CHECK_EQ(F.load_calls, 8);
    CHECK_EQ(F.request_calls, 3);
    CHECK(F.txreq[0] && F.txreq[1] && !F.txreq[2]);
}

TEST_CASE(the_pump_never_waits)
{
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    link_can_frame_t f[4];
    frames(f, 0u, 4u);
    CHECK(can_txq_put(&q, f, 4u));
    can_txq_pump(&q, 0u);              /* the batch is requested */
    /* The controller sends nothing: each pump is one status read. */
    for (uint32_t t = 1u; t < 10u; ++t) {
        const unsigned before = F.status_calls;
        can_txq_pump(&q, t * 100u);
        CHECK_EQ(F.status_calls - before, 1);
    }
    CHECK_EQ(F.load_calls, 3);
    CHECK_EQ(F.request_calls, 1);
}

TEST_CASE(a_stuck_batch_is_aborted_at_5000us_not_4999us)
{
    CHECK_EQ(CAN_TXQ_STUCK_US, 5000);
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    link_can_frame_t f[8];
    frames(f, 0u, 8u);
    CHECK(can_txq_put(&q, f, 8u));
    can_txq_pump(&q, 1000u);           /* RTS at 1000us */
    can_txq_pump(&q, 1000u + 4999u);
    CHECK_EQ(F.abort_calls, 0);
    CHECK_EQ(q.count, 5);
    CHECK_EQ(q.dropped, 0);
    can_txq_pump(&q, 1000u + 5000u);
    CHECK_EQ(F.abort_calls, 1);
    CHECK_EQ(q.count, 0);
    CHECK_EQ(q.dropped, 8);            /* 3 aborted, 5 queued */
    CHECK(!can_txq_busy(&q));
    /* The bus recovers: the next reply goes out whole. */
    frames(f, 20u, 2u);
    CHECK(can_txq_put(&q, f, 2u));
    can_txq_pump(&q, 7000u);
    wire_drain();
    CHECK_EQ(F.on_wire, 2);
    CHECK_EQ(F.wire[0].id, 0x100000u + 20u);
}

TEST_CASE(the_stuck_clock_survives_the_wrap)
{
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    link_can_frame_t f[3];
    frames(f, 0u, 3u);
    CHECK(can_txq_put(&q, f, 3u));
    const uint32_t rts = 0xFFFFFFFFu - 99u;   /* 100us before 2^32 */
    can_txq_pump(&q, rts);
    can_txq_pump(&q, rts + 50u);              /* before the wrap */
    CHECK_EQ(F.abort_calls, 0);
    can_txq_pump(&q, rts + 100u);             /* 0, past the wrap */
    can_txq_pump(&q, rts + 4999u);
    CHECK_EQ(F.abort_calls, 0);
    can_txq_pump(&q, rts + 5000u);
    CHECK_EQ(F.abort_calls, 1);
    CHECK_EQ(q.dropped, 3);
}

TEST_CASE(a_frame_above_8_bytes_is_refused)
{
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    link_can_frame_t f[3];
    frames(f, 0u, 3u);
    f[1].dlc = 9u;
    CHECK(!can_txq_put(&q, f, 3u));
    CHECK_EQ(q.count, 0);
    CHECK_EQ(q.dropped, 3);
    f[1].dlc = 8u;
    f[2].id = 0x20000000u;                    /* 30 bits */
    CHECK(!can_txq_put(&q, f, 3u));
    CHECK_EQ(q.count, 0);
    CHECK_EQ(q.dropped, 6);
    f[2].id = 0x1FFFFFFFu;
    CHECK(can_txq_put(&q, f, 3u));
    CHECK_EQ(q.count, 3);
}

TEST_CASE(the_peak_is_the_deepest_queue_and_resets)
{
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    CHECK_EQ(can_txq_take_peak(&q), 0);
    link_can_frame_t f[8];
    frames(f, 0u, 8u);
    CHECK(can_txq_put(&q, f, 4u));
    CHECK(can_txq_put(&q, f, 8u));
    can_txq_pump(&q, 0u);
    wire_drain();
    CHECK(can_txq_put(&q, f, 1u));             /* 9 + 1 = 10 < 12 */
    CHECK_EQ(can_txq_take_peak(&q), 12);
    /* The next reading starts from what is queued now: 10. */
    CHECK_EQ(can_txq_take_peak(&q), 10);
    uint32_t now = 0u;
    (void)run_out(&q, &now);
    CHECK_EQ(can_txq_take_peak(&q), 10);
    CHECK_EQ(can_txq_take_peak(&q), 0);
    /* A refused reply is no depth. */
    frames(f, 0u, 8u);
    CHECK(can_txq_put(&q, f, 8u));
    CHECK(can_txq_put(&q, f, 8u));
    CHECK(!can_txq_put(&q, f, 1u));
    CHECK_EQ(can_txq_take_peak(&q), 16);
}

TEST_CASE(busy_holds_until_the_last_frame_left)
{
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    CHECK(!can_txq_busy(&q));
    link_can_frame_t f[2];
    frames(f, 0u, 2u);
    CHECK(can_txq_put(&q, f, 2u));
    CHECK(can_txq_busy(&q));
    can_txq_pump(&q, 0u);
    CHECK_EQ(q.count, 0);
    CHECK(can_txq_busy(&q));                   /* the batch is on the wire */
    (void)wire_step();
    can_txq_pump(&q, 100u);
    CHECK(can_txq_busy(&q));                   /* one frame still requested */
    (void)wire_step();
    CHECK(can_txq_busy(&q));                   /* not seen yet */
    can_txq_pump(&q, 200u);
    CHECK(!can_txq_busy(&q));
}

TEST_CASE(a_load_writes_ctrl_id_dlc_and_the_payload_padded)
{
    link_can_frame_t f;
    memset(&f, 0xEE, sizeof(f));
    f.id  = 0x1ABCDEF0u;
    f.dlc = 3u;
    f.data[0] = 1u; f.data[1] = 2u; f.data[2] = 3u;
    uint8_t out[MCP2515_TXB_LOAD_BYTES];
    memset(out, 0xAA, sizeof(out));
    CHECK(mcp2515_tx_load(&f, 3u, out));
    uint8_t id[4];
    CHECK(mcp2515_pack_id(f.id, id));
    CHECK_EQ(out[0], 3);                       /* TXP 3, TXREQ clear */
    CHECK(memcmp(out + 1, id, 4) == 0);
    CHECK_EQ(out[5], 3);
    CHECK_EQ(out[6], 1);
    CHECK_EQ(out[7], 2);
    CHECK_EQ(out[8], 3);
    for (unsigned i = 9; i < MCP2515_TXB_LOAD_BYTES; ++i) {
        CHECK_EQ(out[i], 0);
    }
    /* TXP is masked to its 2 bits; TXREQ is never set by a load. */
    CHECK(mcp2515_tx_load(&f, 0xFFu, out));
    CHECK_EQ(out[0], MCP2515_TXB_TXP_MASK);
    CHECK_EQ(out[0] & MCP2515_TXB_TXREQ, 0);
    /* Refused, and nothing written. */
    memset(out, 0xAA, sizeof(out));
    f.dlc = 9u;
    CHECK(!mcp2515_tx_load(&f, 1u, out));
    f.dlc = 8u;
    f.id = 0x20000000u;
    CHECK(!mcp2515_tx_load(&f, 1u, out));
    CHECK(!mcp2515_tx_load(NULL, 1u, out));
    CHECK(!mcp2515_tx_load(&f, 1u, NULL));
    CHECK_EQ(out[0], 0xAA);
}

TEST_CASE(an_abort_counts_the_frames_still_requested_not_the_sent_ones)
{
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    link_can_frame_t f[5];
    frames(f, 0u, 5u);
    CHECK(can_txq_put(&q, f, 5u));
    can_txq_pump(&q, 0u);              /* 0, 1, 2 requested; 3, 4 queued */
    (void)wire_step();                 /* TXB0 leaves */
    (void)wire_step();                 /* TXB1 leaves; TXB2 stays */
    can_txq_pump(&q, 5000u);
    CHECK_EQ(F.abort_calls, 1);
    CHECK_EQ(F.on_wire, 2);
    CHECK_EQ(q.dropped, 3);            /* TXB2's frame and the 2 queued */
    CHECK(!can_txq_busy(&q));
}

TEST_CASE(a_request_the_queue_did_not_make_is_timed_from_its_first_sight)
{
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    F.txreq[1] = true;                 /* requested by nobody here */
    link_can_frame_t f[2];
    frames(f, 0u, 2u);
    CHECK(can_txq_put(&q, f, 2u));
    can_txq_pump(&q, 100000u);         /* first sight: the clock starts */
    CHECK_EQ(F.abort_calls, 0);
    CHECK_EQ(F.load_calls, 0);
    can_txq_pump(&q, 100000u + 4999u);
    CHECK_EQ(F.abort_calls, 0);
    can_txq_pump(&q, 100000u + 5000u);
    CHECK_EQ(F.abort_calls, 1);
    CHECK_EQ(q.dropped, 2);            /* the queued two; that frame not */
    CHECK_EQ(F.load_calls, 0);
}

TEST_CASE(a_latched_sent_flag_or_a_received_frame_holds_nothing_back)
{
    /* Every frame leaves although TXnIF stays set after the first batch
     * and RX0IF and RX1IF come and go: only TXREQ holds a load back. */
    fake_reset();
    can_txq_t q;
    can_txq_init(&q, &OPS);
    link_can_frame_t f[16];
    frames(f, 0u, 16u);
    CHECK(can_txq_put(&q, f, 16u));
    uint32_t now = 0u;
    (void)run_out(&q, &now);
    CHECK_EQ(F.on_wire, 16);
    CHECK_EQ(F.request_calls, 6);
    CHECK_EQ(F.abort_calls, 0);
    CHECK_EQ(q.dropped, 0);
    CHECK(F.txif[0] && F.txif[1] && F.txif[2]);
}

int main(void)
{
    RUN(a_reply_goes_whole_or_not_at_all);
    RUN(frames_leave_in_the_order_they_were_put);
    RUN(equal_txp_would_reverse_the_batch);
    RUN(nothing_is_loaded_while_a_buffer_is_requested);
    RUN(one_pump_is_one_status_at_most_three_loads_one_request);
    RUN(the_pump_never_waits);
    RUN(a_stuck_batch_is_aborted_at_5000us_not_4999us);
    RUN(the_stuck_clock_survives_the_wrap);
    RUN(a_frame_above_8_bytes_is_refused);
    RUN(the_peak_is_the_deepest_queue_and_resets);
    RUN(busy_holds_until_the_last_frame_left);
    RUN(a_load_writes_ctrl_id_dlc_and_the_payload_padded);
    RUN(an_abort_counts_the_frames_still_requested_not_the_sent_ones);
    RUN(a_request_the_queue_did_not_make_is_timed_from_its_first_sight);
    RUN(a_latched_sent_flag_or_a_received_frame_holds_nothing_back);
    return test_summary("can_txq");
}
