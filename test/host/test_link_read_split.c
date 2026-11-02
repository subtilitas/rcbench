/*
 * A read over the link in parts the panel's receive FIFO holds
 * (link_read_split(), shared/link/link_port.c, and the device end in
 * shared/link/link_dev.c): no request asks for more than LINK_READ_REGS_MAX
 * registers, so no reply is more than 4 frames, and a wide read is one state
 * of the page however often the page changes.
 *
 * The far end is a link_dev with one 32-register page whose contents a case
 * can change after every exchange; requests and replies go over the CAN
 * mapping frame by frame.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_can.h"
#include "link_dev.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_pages_console.h"
#include "link_port.h"

#define PAGE 0x33u

static link_dev_t  g_dev;
static link_host_t g_host;
static uint32_t    g_now;
static uint16_t    g_page[LINK_MAX_REGS];
static int         g_exchanges;
static int         g_widest;      /* registers the widest request asked  */
static int         g_most_frames; /* frames of the longest reply         */
static int         g_refuse_at;   /* the exchange refused, -1 none        */
static int         g_silent_at;   /* the exchange unanswered, -1 none     */
static uint8_t     g_ops[8];      /* the op of each request               */
/** @brief Called after each exchange: the far end's page moves. */
static void (*g_after)(int exchange);
/** @brief Called between the exchanges of a split read. */
static void (*g_between)(void);

static void page_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    memcpy(out, &g_page[off], (size_t)n * sizeof(out[0]));
}

static const link_page_t k_pages[] = {
    { PAGE, LINK_MAX_REGS, page_read, NULL },
};

static bool port_exchange(void *ctx, link_host_t *h, const link_msg_t *req,
                          link_msg_t *reply)
{
    static link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    link_can_frame_t q[LINK_CAN_MAX_FRAMES];
    link_msg_t got, ans;
    const int at = g_exchanges++;

    (void)ctx;
    if (at < (int)sizeof(g_ops)) {
        g_ops[at] = req->op;
    }
    if (req->count > g_widest) {
        g_widest = req->count;
    }
    if (at == g_silent_at) {
        link_host_abandon(h);
        return false;
    }
    /* The request crosses as frames too: a read of any kind is one frame. */
    if (link_can_encode(req, q, LINK_CAN_MAX_FRAMES) != 1u
        || !link_can_decode(&q[0], &got)
        || !link_dev_dispatch(&g_dev, &got, &ans, g_now)) {
        link_host_abandon(h);
        return false;
    }
    if (at == g_refuse_at) {
        ans.op = LINK_OP_NACK;
        ans.count = 1u;
        ans.regs[0] = (uint16_t)LINK_NACK_BAD_RANGE;
    }
    const size_t m = link_can_encode(&ans, f, LINK_CAN_MAX_FRAMES);

    if ((int)m > g_most_frames) {
        g_most_frames = (int)m;
    }
    bool whole = false;

    for (size_t i = 0u; i < m && !whole; ++i) {
        link_msg_t part;

        whole = link_can_decode(&f[i], &part) && link_host_accept(h, &part, g_now, reply);
    }
    if (g_after != NULL) {
        g_after(at);
    }
    return whole;
}

static uint32_t port_now(void *ctx)
{
    (void)ctx;
    return g_now;
}

static int g_betweens;

static void port_between(void *ctx)
{
    (void)ctx;
    ++g_betweens;
    if (g_between != NULL) {
        g_between();
    }
}

static const link_port_t k_port = { port_exchange, port_now, NULL, port_between };

/** @brief The page as state @p s: every register says which state it is of. */
static void state(uint16_t s)
{
    for (unsigned i = 0u; i < LINK_MAX_REGS; ++i) {
        g_page[i] = (uint16_t)(s * 100u + i);
    }
}

static void fresh(void)
{
    g_now = 1000u;
    link_dev_init(&g_dev, k_pages, 1u, NULL, g_now);
    link_host_init(&g_host, g_now);
    g_host.held_reads = true;
    state(1u);
    g_exchanges = 0;
    g_widest = 0;
    g_most_frames = 0;
    g_refuse_at = -1;
    g_silent_at = -1;
    memset(g_ops, 0, sizeof(g_ops));
    g_after = NULL;
    g_between = NULL;
    g_betweens = 0;
}

/** @brief Whether @p r holds @p n registers from @p off, all of state @p s. */
static bool all_of(const link_msg_t *r, uint8_t off, uint8_t n, uint16_t s)
{
    if (r->op != LINK_OP_DATA || r->count != n || r->offset != off) {
        return false;
    }
    for (unsigned i = 0u; i < n; ++i) {
        if (r->regs[i] != (uint16_t)(s * 100u + off + i)) {
            return false;
        }
    }
    return true;
}

TEST_CASE(a_read_of_16_is_one_exchange_of_4_frames)
{
    link_msg_t r;

    fresh();
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, 16u, &r));
    CHECK(all_of(&r, 0u, 16u, 1u));
    CHECK_EQ(g_exchanges, 1);
    CHECK_EQ(g_most_frames, 4);
    CHECK_EQ(g_betweens, 0);
}

/* The PROG page, 28 registers: 16, then the 12 the far end kept. */
TEST_CASE(the_prog_page_is_a_read_of_16_and_a_held_read_of_12)
{
    link_msg_t r;

    fresh();
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, (uint8_t)LINK_PG_REGS, &r));
    CHECK(all_of(&r, 0u, (uint8_t)LINK_PG_REGS, 1u));
    CHECK_EQ(g_exchanges, 2);
    CHECK_EQ(g_ops[0], (uint8_t)LINK_OP_READ);
    CHECK_EQ(g_ops[1], (uint8_t)LINK_OP_READ_HELD);
    CHECK_EQ(g_widest, (int)LINK_READ_REGS_MAX);
    CHECK_EQ(g_most_frames, 4);
    CHECK_EQ(g_betweens, 1);
}

/* At the boundary: 17 is 16 and 1; 32 from 0 is 16 and 16; 20 from 4 is 16
 * and 4; 33 is refused before anything is sent. */
TEST_CASE(a_read_one_past_the_limit_splits_and_the_widest_is_two_halves)
{
    link_msg_t r;

    fresh();
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, 17u, &r));
    CHECK(all_of(&r, 0u, 17u, 1u));
    CHECK_EQ(g_exchanges, 2);
    fresh();
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, LINK_MAX_REGS, &r));
    CHECK(all_of(&r, 0u, LINK_MAX_REGS, 1u));
    CHECK_EQ(g_widest, 16);
    CHECK_EQ(g_most_frames, 4);
    fresh();
    CHECK(link_read_split(&g_host, &k_port, PAGE, 4u, 20u, &r));
    CHECK(all_of(&r, 4u, 20u, 1u));
    fresh();
    CHECK(!link_read_split(&g_host, &k_port, PAGE, 0u, LINK_MAX_REGS + 1u, &r));
    CHECK_EQ(g_exchanges, 0);
}

static void move_always(int at)
{
    state((uint16_t)(10 + at));
}

/* The page changes after every exchange: the two parts are the state the first
 * exchange read, never a mix. */
TEST_CASE(a_page_that_changes_at_every_exchange_reads_as_one_state)
{
    link_msg_t r;

    fresh();
    g_after = move_always;
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, 28u, &r));
    CHECK(all_of(&r, 0u, 28u, 1u));
    CHECK_EQ(g_exchanges, 2);
    /* And again: the second read is of the state the page holds then. */
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, LINK_MAX_REGS, &r));
    CHECK(all_of(&r, 0u, LINK_MAX_REGS, 11u));
}

/* An IOmcu older than 4.35 keeps nothing: one wide read of 7 frames. */
TEST_CASE(an_older_far_end_is_read_in_one_exchange)
{
    link_msg_t r;

    fresh();
    g_host.held_reads = false;
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, 28u, &r));
    CHECK(all_of(&r, 0u, 28u, 1u));
    CHECK_EQ(g_exchanges, 1);
    CHECK_EQ(g_widest, 28);
    CHECK_EQ(g_most_frames, 7);
}

/* Between the parts another read of 16 takes the far end's copy. */
static void read_16_elsewhere(void)
{
    link_msg_t q, a;

    memset(&q, 0, sizeof(q));
    q.op = LINK_OP_READ;
    q.page = PAGE;
    q.offset = 4u;
    q.count = 16u;
    (void)link_dev_dispatch(&g_dev, &q, &a, g_now);
    state(2u);
}

/* The held read is refused, and one wide read gives the page as it is then. */
TEST_CASE(a_copy_taken_between_the_parts_falls_back_to_one_wide_read)
{
    link_msg_t r;

    fresh();
    g_between = read_16_elsewhere;
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, 28u, &r));
    CHECK(all_of(&r, 0u, 28u, 2u));
    CHECK_EQ(g_exchanges, 3);
    CHECK_EQ(g_ops[2], (uint8_t)LINK_OP_READ);
    CHECK_EQ(g_widest, 28);
}

/* A refusal of the first part is the answer; a refusal of the second falls
 * back to the wide read; an unanswered part ends the read. */
TEST_CASE(a_refused_or_unanswered_part)
{
    link_msg_t r;

    fresh();
    g_refuse_at = 0;
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, 28u, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], (uint16_t)LINK_NACK_BAD_RANGE);
    CHECK_EQ(g_exchanges, 1);

    fresh();
    g_refuse_at = 1;
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, 28u, &r));
    CHECK(all_of(&r, 0u, 28u, 1u));
    CHECK_EQ(g_exchanges, 3);

    for (int at = 0; at < 2; ++at) {
        fresh();
        g_silent_at = at;
        CHECK(!link_read_split(&g_host, &k_port, PAGE, 0u, 28u, &r));
        CHECK_EQ(g_exchanges, at + 1);
    }
}

/** @brief One request straight to the device end. */
static link_msg_t ask(uint8_t op, uint8_t off, uint8_t n)
{
    link_msg_t q, a;

    memset(&q, 0, sizeof(q));
    q.op = op;
    q.page = PAGE;
    q.offset = off;
    q.count = n;
    CHECK(link_dev_dispatch(&g_dev, &q, &a, g_now));
    return a;
}

/* The device end keeps the rest of a read of exactly 16 registers, up to 16
 * more, once; anything else is refused. */
TEST_CASE(the_device_end_keeps_a_rest_once)
{
    link_msg_t a;

    fresh();
    a = ask(LINK_OP_READ_HELD, 16u, 4u);
    CHECK_EQ(a.op, LINK_OP_NACK);
    CHECK_EQ(a.regs[0], (uint16_t)LINK_NACK_BUSY);

    (void)ask(LINK_OP_READ, 8u, 16u);           /* keeps 24..31: 8 registers */
    a = ask(LINK_OP_READ_HELD, 16u, 8u);        /* not that offset */
    CHECK_EQ(a.regs[0], (uint16_t)LINK_NACK_BUSY);
    a = ask(LINK_OP_READ_HELD, 24u, 9u);        /* more than is kept */
    CHECK_EQ(a.op, LINK_OP_NACK);
    CHECK_EQ(a.regs[0], (uint16_t)LINK_NACK_BAD_RANGE);
    state(5u);
    a = ask(LINK_OP_READ_HELD, 24u, 8u);
    CHECK(all_of(&a, 24u, 8u, 1u));
    a = ask(LINK_OP_READ_HELD, 24u, 8u);        /* spent */
    CHECK_EQ(a.regs[0], (uint16_t)LINK_NACK_BUSY);

    (void)ask(LINK_OP_READ, 16u, 16u);          /* nothing after it */
    a = ask(LINK_OP_READ_HELD, 0u, 1u);
    CHECK_EQ(a.regs[0], (uint16_t)LINK_NACK_BUSY);
    (void)ask(LINK_OP_READ, 0u, 15u);           /* not 16: keeps nothing */
    a = ask(LINK_OP_READ_HELD, 15u, 1u);
    CHECK_EQ(a.regs[0], (uint16_t)LINK_NACK_BUSY);

    /* The console's path keeps nothing and refuses the op. */
    link_msg_t q, b;
    memset(&q, 0, sizeof(q));
    q.op = LINK_OP_READ_HELD;
    q.page = PAGE;
    q.offset = 16u;
    q.count = 1u;
    CHECK(link_dev_apply(&g_dev, &q, &b));
    CHECK_EQ(b.op, LINK_OP_NACK);
    CHECK_EQ(b.regs[0], (uint16_t)LINK_NACK_BAD_VALUE);
}

/* A held read is one frame with no payload, and its op survives the frame. */
TEST_CASE(a_held_read_is_one_empty_frame)
{
    link_msg_t q, back;
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];

    memset(&q, 0, sizeof(q));
    q.op = LINK_OP_READ_HELD;
    q.page = PAGE;
    q.offset = 16u;
    q.count = 12u;
    CHECK_EQ(link_can_encode(&q, f, LINK_CAN_MAX_FRAMES), 1u);
    CHECK_EQ(f[0].dlc, 0u);
    CHECK(link_can_decode(&f[0], &back));
    CHECK_EQ(back.op, LINK_OP_READ_HELD);
    CHECK_EQ(back.offset, 16u);
    CHECK_EQ(back.count, 12u);
    CHECK(LINK_PROTOCOL_MINOR >= LINK_MINOR_READ_HELD);
}

/* The console's window read fits one read of 4 frames. */
TEST_CASE(the_console_window_is_one_read_of_4_frames)
{
    link_msg_t r;

    CHECK((unsigned)LINK_CN_COUNT <= LINK_READ_REGS_MAX);
    CHECK_EQ((unsigned)LINK_CN_DATA + LINK_CN_DATA_BYTES / 2u, (unsigned)LINK_CN_COUNT);
    fresh();
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, (uint8_t)LINK_CN_COUNT, &r));
    CHECK_EQ(g_exchanges, 1);
    CHECK_EQ(g_most_frames, 4);
}

int main(void)
{
    RUN(a_read_of_16_is_one_exchange_of_4_frames);
    RUN(the_prog_page_is_a_read_of_16_and_a_held_read_of_12);
    RUN(a_read_one_past_the_limit_splits_and_the_widest_is_two_halves);
    RUN(a_page_that_changes_at_every_exchange_reads_as_one_state);
    RUN(an_older_far_end_is_read_in_one_exchange);
    RUN(a_copy_taken_between_the_parts_falls_back_to_one_wide_read);
    RUN(a_refused_or_unanswered_part);
    RUN(the_device_end_keeps_a_rest_once);
    RUN(a_held_read_is_one_empty_frame);
    RUN(the_console_window_is_one_read_of_4_frames);
    return test_summary("link_read_split");
}
