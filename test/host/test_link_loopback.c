/*
 * Both ends of the link, talking to each other over a simulated CAN
 * (Controller Area Network) bus.
 *
 * The other link suites test one half each: the dispatcher's judgement, the
 * poller's matching, the identifier layout.  This one runs real requests
 * through real answers, where the failures live in the seam: a reply that
 * assembles into the wrong window, a fragment attributed to an abandoned
 * request, a refusal that leaves the poller waiting.
 *
 * The bus can drop, delay and reorder frames.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_can.h"
#include "link_dev.h"
#include "link_host.h"
#include "link_pages.h"

/* ------------------------------------------------------------ the device */

/* A page as wide as the protocol allows, writable, so a request that has to
 * be split can be answered the way the far end answers it. Both output pages
 * are this wide. */
#define WIDE_COUNT LINK_MAX_REGS

typedef struct {
    uint16_t identity[LINK_ID_COUNT];
    uint16_t control[LINK_CT_COUNT];
    uint16_t bench[LINK_BN_COUNT];
    uint16_t wide[WIDE_COUNT];
    /* The first register of the wide page the far end will not take, so a
     * fragment of a wide write can be refused at a chosen offset.  WIDE_COUNT
     * refuses nothing. */
    uint8_t  refuse_from;
} state_t;

static state_t   g;
static link_dev_t dev;
static link_host_t host;

static void id_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    memcpy(out, ((state_t *)ctx)->identity + off, (size_t)n * 2u);
}
static void ct_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    memcpy(out, ((state_t *)ctx)->control + off, (size_t)n * 2u);
}
static uint8_t ct_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    if (off == LINK_CT_THROTTLE && in[0] > LINK_THROTTLE_MAX) {
        return LINK_NACK_BAD_VALUE;
    }
    memcpy(((state_t *)ctx)->control + off, in, (size_t)n * 2u);
    return 0;
}
static void bn_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    memcpy(out, ((state_t *)ctx)->bench + off, (size_t)n * 2u);
}

static void wide_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    memcpy(out, ((state_t *)ctx)->wide + off, (size_t)n * 2u);
}
static uint8_t wide_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    state_t *st = (state_t *)ctx;
    if (off >= st->refuse_from) {
        return LINK_NACK_BAD_RANGE;
    }
    memcpy(st->wide + off, in, (size_t)n * 2u);
    return 0;
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_IDENTITY, LINK_ID_COUNT, id_read, NULL },
    { LINK_PAGE_CONTROL,  LINK_CT_COUNT, ct_read, ct_write },
    { LINK_PAGE_BENCH,    LINK_BN_COUNT, bn_read, NULL },
    { LINK_PAGE_OUTPUTS,  WIDE_COUNT,    wide_read, wide_write },
};

/* --------------------------------------------------------------- the bus */

typedef struct {
    int      drop_every;   /* 0 = never */
    bool     reverse;      /* deliver a reply's frames back to front */
    uint32_t carried;
    uint32_t dropped;
} bus_t;

static bus_t bus;

/*
 * Carry one request to the device and its answer back, feeding every frame of
 * the reply to the host.  Returns true if the request was answered.
 */
static bool exchange(const link_msg_t *req, uint32_t now_ms, link_msg_t *got)
{
    link_can_frame_t out[LINK_CAN_MAX_FRAMES];
    const size_t n = link_can_encode(req, out, LINK_CAN_MAX_FRAMES);
    if (n == 0) {
        return false;
    }

    /* Outbound: the device sees each frame as a message of its own.  A
     * request never exceeds one frame in this protocol, but the loop does not
     * assume it. */
    link_msg_t reply;
    bool have_reply = false;
    for (size_t i = 0; i < n; ++i) {
        link_msg_t part;
        ++bus.carried;
        if (!link_can_decode(&out[i], &part)) {
            continue;
        }
        have_reply = link_dev_dispatch(&dev, &part, &reply, now_ms);
    }
    if (!have_reply) {
        return false;
    }

    link_can_frame_t back[LINK_CAN_MAX_FRAMES];
    const size_t m = link_can_encode(&reply, back, LINK_CAN_MAX_FRAMES);
    bool answered = false;
    for (size_t k = 0; k < m; ++k) {
        const size_t i = bus.reverse ? (m - 1u - k) : k;
        ++bus.carried;
        if (bus.drop_every != 0 && (bus.carried % (uint32_t)bus.drop_every) == 0u) {
            ++bus.dropped;
            continue;
        }
        link_msg_t part;
        if (!link_can_decode(&back[i], &part)) {
            continue;
        }
        if (link_host_accept(&host, &part, now_ms, got)) {
            answered = true;
        }
    }
    return answered;
}

static void fresh(void)
{
    memset(&g, 0, sizeof(g));
    memset(&bus, 0, sizeof(bus));
    for (uint8_t i = 0; i < LINK_BN_COUNT; ++i) {
        g.bench[i] = (uint16_t)(0x2000 + i);
    }
    g.identity[LINK_ID_PROTOCOL_MAJOR] = LINK_PROTOCOL_MAJOR;
    g.identity[LINK_ID_PROTOCOL_MINOR] = LINK_PROTOCOL_MINOR;
    g.refuse_from = (uint8_t)WIDE_COUNT;   /* takes the whole page */
    link_dev_init(&dev, k_pages, 4, &g, 0);
    link_host_init(&host, 0);
}

/* ----------------------------------------------------------------- cases */

/*
 * The whole bench page in one request: 13 registers come back as four frames
 * and are assembled into the window that was asked for.
 */
TEST_CASE(a_page_wider_than_a_frame_comes_back_whole)
{
    fresh();
    link_msg_t req, got;
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0, LINK_BN_COUNT, 0, &req));
    CHECK(exchange(&req, 100, &got));

    CHECK_EQ(got.op, LINK_OP_DATA);
    CHECK_EQ(got.page, LINK_PAGE_BENCH);
    CHECK_EQ(got.offset, 0);
    CHECK_EQ(got.count, LINK_BN_COUNT);
    for (uint8_t i = 0; i < LINK_BN_COUNT; ++i) {
        CHECK_EQ(got.regs[i], 0x2000 + i);
    }
    CHECK_EQ(host.replies, 1u);
    CHECK_EQ(host.mismatches, 0u);
    CHECK(!host.pending);
}

/*
 * Every frame says where it belongs, so the order they arrive in is not
 * information.  A transport that quietly relied on order would pass a
 * forward-only test and fail on a bus that retried one frame of four.
 */
TEST_CASE(the_pieces_may_arrive_in_any_order)
{
    fresh();
    bus.reverse = true;
    link_msg_t req, got;
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0, LINK_BN_COUNT, 0, &req));
    CHECK(exchange(&req, 100, &got));
    for (uint8_t i = 0; i < LINK_BN_COUNT; ++i) {
        CHECK_EQ(got.regs[i], 0x2000 + i);
    }
}

/* A window that starts inside a page keeps its offset all the way back. */
TEST_CASE(a_window_inside_a_page_answers_with_that_window)
{
    fresh();
    link_msg_t req, got;
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 8, 5, 0, &req));
    CHECK(exchange(&req, 100, &got));
    CHECK_EQ(got.offset, 8);
    CHECK_EQ(got.count, 5);
    for (uint8_t i = 0; i < 5; ++i) {
        CHECK_EQ(got.regs[i], 0x2000 + 8 + i);
    }
}

TEST_CASE(a_write_is_acknowledged_with_what_was_stored)
{
    fresh();
    link_msg_t req, got;
    const uint16_t v[2] = { 1, 4321 };
    CHECK(link_host_write(&host, LINK_PAGE_CONTROL, 0, 2, v, 0, &req));
    CHECK(exchange(&req, 100, &got));
    CHECK_EQ(got.op, LINK_OP_ACK);
    CHECK_EQ(g.control[LINK_CT_ARM], 1);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 4321);
    /* The acknowledgement carries what the device kept, not what was sent. */
    CHECK_EQ(got.regs[1], 4321);
}

/*
 * A refusal is an answer.  A host that kept waiting on a NACK (negative
 * acknowledge) could not tell a page it may not write from a link that has
 * died.
 */
/*
 * The transport the coprocessor actually is.
 *
 * exchange() above keeps one reply, which is what a request of a single
 * frame produces. firmware/iomcu/src/main.c decodes every frame it receives
 * and answers each one (its receive loop dispatches and transmits inside the
 * same pass), so a request that took eight frames draws eight answers. A
 * write of a whole page is exactly that, and neither output page fits in one
 * frame: LINK_CC_COUNT and LINK_OS_COUNT are both 32 registers against
 * LINK_CAN_REGS_PER_FRAME of 4.
 */
static bool exchange_answering_every_frame(const link_msg_t *req,
                                           uint32_t now_ms, link_msg_t *got)
{
    link_can_frame_t out[LINK_CAN_MAX_FRAMES];
    const size_t n = link_can_encode(req, out, LINK_CAN_MAX_FRAMES);
    if (n == 0) {
        return false;
    }
    bool answered = false;
    for (size_t i = 0; i < n; ++i) {
        link_msg_t part, reply;
        ++bus.carried;
        if (!link_can_decode(&out[i], &part)) {
            continue;
        }
        if (!link_dev_dispatch(&dev, &part, &reply, now_ms)) {
            continue;
        }
        link_can_frame_t back[LINK_CAN_MAX_FRAMES];
        const size_t m = link_can_encode(&reply, back, LINK_CAN_MAX_FRAMES);
        for (size_t k = 0; k < m; ++k) {
            link_msg_t ans;
            ++bus.carried;
            if (!link_can_decode(&back[k], &ans)) {
                continue;
            }
            if (link_host_accept(&host, &ans, now_ms, got)) {
                answered = true;
            }
        }
    }
    return answered;
}

/*
 * A page-wide write, answered the way the far end answers it.
 *
 * The acknowledgement comes back in as many pieces as the request went out
 * in, each naming its own four registers. A host that waited for one
 * acknowledgement of the whole window matched none of them, counted all
 * eight as mismatches and waited out LINK_HOST_TIMEOUT_MS -- during which it
 * transmitted nothing, so the far end's own 200 ms silence watchdog latched.
 * Applying an output binding did that every time.
 */
TEST_CASE(a_write_wider_than_a_frame_is_answered_by_its_pieces)
{
    fresh();
    link_msg_t req, got;
    uint16_t v[WIDE_COUNT];
    const uint8_t wide = (uint8_t)WIDE_COUNT;
    CHECK(wide > LINK_CAN_REGS_PER_FRAME);
    for (uint8_t i = 0; i < wide; ++i) {
        v[i] = (uint16_t)(0x100 + i);
    }

    CHECK(link_host_write(&host, LINK_PAGE_OUTPUTS, 0, wide, v, 0, &req));
    CHECK(exchange_answering_every_frame(&req, 100, &got));
    CHECK_EQ(got.op, LINK_OP_ACK);

    /* The window it answers is the window that was asked for, not the last
     * frame's quarter of it. */
    CHECK_EQ(got.offset, 0);
    CHECK_EQ(got.count, wide);

    /* Every register reached the far end. */
    for (uint8_t i = 0; i < wide; ++i) {
        CHECK_EQ(g.wide[i], (uint16_t)(0x100 + i));
    }

    /* And the answer carries the whole window that was stored, not the
     * four registers of whichever piece arrived last. */
    for (uint8_t i = 0; i < wide; ++i) {
        CHECK_EQ(got.regs[i], (uint16_t)(0x100 + i));
    }

    /* Nothing counted as a stray: every piece belonged to this request. */
    CHECK_EQ(host.mismatches, 0u);
    CHECK(!host.pending);
    CHECK_EQ(host.replies, 1u);
}

/*
 * A wide write whose later fragment is refused.
 *
 * Pins that the refusal answers the transaction and carries its reason back.
 * The request goes out as eight frames at offsets 0, 4 ... 28; this far end
 * takes the first four and answers BAD_RANGE at offsets 16, 20, 24 and 28,
 * which is what a coprocessor whose page is 16 registers wide does to a
 * panel that thinks it is 32.  Matching a refusal only against the
 * transaction's own offset drops it, and the transaction then runs to
 * LINK_HOST_TIMEOUT_MS (1000 ms) with the reason lost:
 * firmware/panel/main/main.c maps a refusal to OUTPUTS_REFUSED and a timeout
 * to OUTPUTS_NO_LINK, so the reason decides whether the operator is sent to
 * the pins chosen or to the cable.
 */
TEST_CASE(a_refusal_of_a_later_fragment_answers_the_write)
{
    fresh();
    g.refuse_from = 16;          /* the far end's page ends here */

    link_msg_t req, got;
    uint16_t v[WIDE_COUNT];
    const uint8_t wide = (uint8_t)WIDE_COUNT;
    for (uint8_t i = 0; i < wide; ++i) {
        v[i] = (uint16_t)(0x200 + i);
    }

    CHECK(link_host_write(&host, LINK_PAGE_OUTPUTS, 0, wide, v, 0, &req));
    CHECK(exchange_answering_every_frame(&req, 100, &got));

    CHECK_EQ(got.op, LINK_OP_NACK);
    CHECK_EQ(got.regs[0], LINK_NACK_BAD_RANGE);
    CHECK_EQ(got.offset, 16);    /* and says which fragment was refused */
    CHECK_EQ(host.nacks, 1u);
    CHECK_EQ(host.replies, 1u);
    CHECK(!host.pending);

    /* The refusal is the answer, so no clock is still running against this
     * transaction: at 1099 ms the host has neither abandoned a request nor
     * reported the link down.  A transaction left unanswered reaches
     * LINK_HOST_TIMEOUT_MS from the request at 0 ms and fires both. */
    CHECK(!link_host_tick(&host, 100 + LINK_HOST_TIMEOUT_MS - 1u));
    CHECK_EQ(host.timeouts, 0u);

    /* The three frames still behind it answer a transaction that is over:
     * refusals at offsets 20, 24 and 28, arriving with nothing outstanding,
     * are counted as strays. */
    CHECK_EQ(host.mismatches, 3u);

    /* The fragments the far end took stay applied.  The transport has no
     * rollback, and the refusal does not undo them. */
    for (uint8_t i = 0; i < 16; ++i) {
        CHECK_EQ(g.wide[i], (uint16_t)(0x200 + i));
    }
    for (uint8_t i = 16; i < wide; ++i) {
        CHECK_EQ(g.wide[i], 0);
    }
}

/*
 * The edges of that matching, which a write's refusal must not widen past
 * and a read's must not widen at all.
 *
 * Pins three strays: a refusal naming a register outside the window, a
 * refusal of another page, and a refusal naming a register inside a read's
 * window but not the offset the read asked at.  A read goes out as one
 * frame with one offset, so nothing about it is fragmented.  This case is
 * the guard on the fix rather than the proof of it: it holds with the
 * transaction's own offset demanded and with the window matched, and fails
 * on a fix that widens the read path too.
 */
TEST_CASE(a_refusal_at_an_unrelated_offset_is_a_stray)
{
    fresh();
    link_msg_t req, got;
    const uint16_t v[2] = { 0, 1234 };

    /* A write of two registers at offset 0.  Offset 8 is outside it. */
    CHECK(link_host_write(&host, LINK_PAGE_CONTROL, 0, 2, v, 0, &req));
    link_msg_t stray = { 0 };
    stray.op      = LINK_OP_NACK;
    stray.page    = LINK_PAGE_CONTROL;
    stray.offset  = 8;
    stray.count   = 1;
    stray.regs[0] = LINK_NACK_BAD_VALUE;
    CHECK(!link_host_accept(&host, &stray, 100, &got));
    CHECK_EQ(host.mismatches, 1u);
    CHECK_EQ(host.nacks, 0u);
    CHECK(host.pending);         /* still waiting for its own answer */

    /* Another page's refusal is a stray wherever it points. */
    stray.page   = LINK_PAGE_BENCH;
    stray.offset = 0;
    CHECK(!link_host_accept(&host, &stray, 100, &got));
    CHECK_EQ(host.mismatches, 2u);
    CHECK(host.pending);
    link_host_abandon(&host);

    /* A 13-register read is one request at one offset.  A refusal naming
     * offset 4 answers nothing this host asked. */
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0, LINK_BN_COUNT, 0, &req));
    stray.page   = LINK_PAGE_BENCH;
    stray.offset = 4;
    CHECK(!link_host_accept(&host, &stray, 200, &got));
    CHECK_EQ(host.mismatches, 3u);
    CHECK(host.pending);

    /* The offset it was asked at answers it. */
    stray.offset = 0;
    CHECK(link_host_accept(&host, &stray, 200, &got));
    CHECK_EQ(got.op, LINK_OP_NACK);
    CHECK_EQ(got.regs[0], LINK_NACK_BAD_VALUE);
    CHECK_EQ(host.nacks, 1u);
    CHECK(!host.pending);
}

/* And a piece that belongs to no window is still a stray. */
TEST_CASE(an_acknowledgement_outside_the_window_is_refused)
{
    fresh();
    link_msg_t req, got;
    const uint16_t v[2] = { 0, 4321 };
    CHECK(link_host_write(&host, LINK_PAGE_CONTROL, 0, 2, v, 0, &req));

    link_msg_t stray = { 0 };
    stray.op     = LINK_OP_ACK;
    stray.page   = LINK_PAGE_CONTROL;
    stray.offset = 8;               /* past the two registers asked for */
    stray.count  = 2;
    CHECK(!link_host_accept(&host, &stray, 100, &got));
    CHECK_EQ(host.mismatches, 1u);
    CHECK(host.pending);            /* still waiting for its own answer */
}

TEST_CASE(a_refusal_answers_and_says_why)
{
    fresh();
    link_msg_t req, got;
    const uint16_t bad = LINK_THROTTLE_MAX + 1u;
    CHECK(link_host_write(&host, LINK_PAGE_CONTROL, LINK_CT_THROTTLE, 1, &bad, 0, &req));
    CHECK(exchange(&req, 100, &got));
    CHECK_EQ(got.op, LINK_OP_NACK);
    CHECK_EQ(got.regs[0], LINK_NACK_BAD_VALUE);
    CHECK_EQ(host.nacks, 1u);
    CHECK(!host.pending);
    CHECK_EQ(g.control[LINK_CT_THROTTLE], 0);   /* and nothing was stored */

    /* A read-only page refuses the same way. */
    const uint16_t one = 1;
    CHECK(link_host_write(&host, LINK_PAGE_IDENTITY, 0, 1, &one, 0, &req));
    CHECK(exchange(&req, 200, &got));
    CHECK_EQ(got.op, LINK_OP_NACK);
    CHECK_EQ(got.regs[0], LINK_NACK_READ_ONLY);
}

/*
 * One frame of four lost.  The request must stay outstanding rather than be
 * answered by a partial window -- three quarters of a page reported as a whole
 * one is worse than no answer, because it looks like an answer.
 */
TEST_CASE(a_lost_piece_leaves_the_request_unanswered)
{
    fresh();
    bus.drop_every = 3;      /* every third frame carried disappears */
    link_msg_t req, got;
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0, LINK_BN_COUNT, 0, &req));
    CHECK_EQ(exchange(&req, 100, &got), false);
    CHECK(bus.dropped > 0);
    CHECK(host.pending);
    CHECK_EQ(host.replies, 0u);

    /* And the host gives up on it in its own time rather than for ever. */
    CHECK(link_host_tick(&host, 100 + LINK_HOST_TIMEOUT_MS));
    CHECK(!host.pending);
    CHECK_EQ(host.timeouts, 1u);
}

/*
 * The bad case the poller exists for: a reply to a question already abandoned,
 * arriving while a different one is outstanding.  Accepting it would attach
 * one page's registers to another page's request.
 */
TEST_CASE(a_reply_to_an_abandoned_question_is_refused)
{
    fresh();
    link_msg_t req, got;

    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0, 4, 0, &req));
    link_can_frame_t stale[LINK_CAN_MAX_FRAMES];
    link_msg_t reply;
    CHECK(link_dev_dispatch(&dev, &req, &reply, 100));
    const size_t n = link_can_encode(&reply, stale, LINK_CAN_MAX_FRAMES);
    CHECK(n > 0);

    /* It times out, and a different question goes out. */
    CHECK(link_host_tick(&host, LINK_HOST_TIMEOUT_MS));
    CHECK(link_host_read(&host, LINK_PAGE_IDENTITY, 0, 2, 0, &req));

    /* The old answer turns up. */
    for (size_t i = 0; i < n; ++i) {
        link_msg_t part;
        CHECK(link_can_decode(&stale[i], &part));
        CHECK_EQ(link_host_accept(&host, &part, LINK_HOST_TIMEOUT_MS + 10,
                                  &got), false);
    }
    CHECK(host.mismatches >= 1u);
    CHECK(host.pending);      /* still waiting for the identity it asked for */
}

/* The device's own watchdog runs off requests arriving, whatever carries
 * them, and a link that goes quiet must fail safe on the far end too. */
TEST_CASE(the_devices_watchdog_still_runs_on_can)
{
    fresh();
    link_msg_t req, got;
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0, 4, 0, &req));
    CHECK(exchange(&req, 1000, &got));
    CHECK(!dev.failsafe);

    CHECK(link_dev_tick(&dev, 1000 + LINK_DEV_SILENCE_MS));
    CHECK(dev.failsafe);

    /* And a request arriving again stops the silence without lifting the
     * latch: those are different facts. */
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0, 4, 0, &req));
    CHECK(exchange(&req, 2000, &got));
    CHECK(dev.failsafe);
}

/* Nothing the host asks for can be answered by a page it did not ask about,
 * however well-formed the frame is. */
TEST_CASE(another_pages_answer_is_not_this_pages_answer)
{
    fresh();
    link_msg_t req, got;
    CHECK(link_host_read(&host, LINK_PAGE_BENCH, 0, 4, 0, &req));

    link_msg_t other = { 0 };
    other.op = LINK_OP_DATA; other.page = LINK_PAGE_CONTROL;
    other.offset = 0; other.count = 4;
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    CHECK(link_can_encode(&other, f, LINK_CAN_MAX_FRAMES) > 0);

    link_msg_t part;
    CHECK(link_can_decode(&f[0], &part));
    CHECK_EQ(link_host_accept(&host, &part, 100, &got), false);
    CHECK_EQ(host.mismatches, 1u);
    CHECK(host.pending);
}

/*
 * A refusal naming a register between two fragment starts belongs to some
 * other request.  A 32-register write goes out at offsets 0, 4 ... 28, so a
 * NACK at offset 2 was never any fragment of it: accepting one would end the
 * transaction and hand the caller a reason that came from somewhere else.
 */
TEST_CASE(a_refusal_off_a_fragment_start_is_not_this_write)
{
    fresh();
    const uint8_t wide = (uint8_t)WIDE_COUNT;
    uint16_t v[LINK_MAX_REGS];
    link_msg_t req;
    for (uint8_t i = 0; i < wide; ++i) {
        v[i] = (uint16_t)(0x100 + i);
    }
    CHECK(link_host_write(&host, LINK_PAGE_OUTPUTS, 0, wide, v, 0, &req));

    link_msg_t nack = { .op = LINK_OP_NACK, .page = LINK_PAGE_OUTPUTS,
                        .offset = 2, .count = 1 };
    nack.regs[0] = LINK_NACK_BAD_VALUE;
    link_msg_t got;
    CHECK(!link_host_accept(&host, &nack, 10u, &got));
    CHECK_EQ(host.mismatches, 1u);
    CHECK(host.pending);

    /* The fragment start beside it is one this write sent, and answers it. */
    nack.offset = LINK_CAN_REGS_PER_FRAME;
    CHECK(link_host_accept(&host, &nack, 11u, &got));
    CHECK_EQ(got.op, LINK_OP_NACK);
    CHECK(!host.pending);
}

int main(void)
{
    RUN(a_page_wider_than_a_frame_comes_back_whole);
    RUN(the_pieces_may_arrive_in_any_order);
    RUN(a_window_inside_a_page_answers_with_that_window);
    RUN(a_write_is_acknowledged_with_what_was_stored);
    RUN(a_write_wider_than_a_frame_is_answered_by_its_pieces);
    RUN(a_refusal_of_a_later_fragment_answers_the_write);
    RUN(a_refusal_at_an_unrelated_offset_is_a_stray);
    RUN(an_acknowledgement_outside_the_window_is_refused);
    RUN(a_refusal_answers_and_says_why);
    RUN(a_lost_piece_leaves_the_request_unanswered);
    RUN(a_reply_to_an_abandoned_question_is_refused);
    RUN(the_devices_watchdog_still_runs_on_can);
    RUN(another_pages_answer_is_not_this_pages_answer);
    RUN(a_refusal_off_a_fragment_start_is_not_this_write);
    return test_summary("link_loopback");
}
