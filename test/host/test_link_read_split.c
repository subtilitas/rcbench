/*
 * A read over the link in parts the panel's receive FIFO holds
 * (link_read_split(), shared/link/link_port.c): no request asks for more than
 * LINK_READ_REGS_MAX registers, so no reply is more than 4 frames, and a wide
 * read is one state of the page or a NACK of LINK_NACK_BUSY.
 *
 * The far end is a link_dev with one 32-register page whose read a case can
 * change between exchanges; requests and replies go over the CAN mapping frame
 * by frame.
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
static int         g_widest;      /* registers the widest request asked */
static int         g_most_frames; /* frames of the longest reply        */
static int         g_refuse_at;   /* the exchange refused, -1 none       */
static int         g_silent_at;   /* the exchange unanswered, -1 none    */
/** @brief Called after each exchange: the far end's page moves. */
static void (*g_after)(int exchange);

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
    link_msg_t ans;
    const int at = g_exchanges++;

    (void)ctx;
    if (req->count > g_widest) {
        g_widest = req->count;
    }
    if (at == g_silent_at) {
        link_host_abandon(h);
        return false;
    }
    if (!link_dev_dispatch(&g_dev, req, &ans, g_now)) {
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
    state(1u);
    g_exchanges = 0;
    g_widest = 0;
    g_most_frames = 0;
    g_refuse_at = -1;
    g_silent_at = -1;
    g_after = NULL;
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

/* The PROG page, 28 registers: 16, 12, and the first 16 again. */
TEST_CASE(the_prog_page_is_read_in_parts_of_at_most_4_frames)
{
    link_msg_t r;

    fresh();
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, (uint8_t)LINK_PG_REGS, &r));
    CHECK(all_of(&r, 0u, (uint8_t)LINK_PG_REGS, 1u));
    CHECK_EQ(g_exchanges, 3);
    CHECK_EQ(g_widest, (int)LINK_READ_REGS_MAX);
    CHECK_EQ(g_most_frames, 4);
    CHECK_EQ(g_betweens, 2);
}

/* At the boundary: 17 is 16 and 1; 32 from 0 is 16 and 16. */
TEST_CASE(a_read_one_past_the_limit_splits_and_the_widest_is_two_halves)
{
    link_msg_t r;

    fresh();
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, 17u, &r));
    CHECK(all_of(&r, 0u, 17u, 1u));
    CHECK_EQ(g_exchanges, 3);
    fresh();
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, LINK_MAX_REGS, &r));
    CHECK(all_of(&r, 0u, LINK_MAX_REGS, 1u));
    CHECK_EQ(g_widest, 16);
    CHECK_EQ(g_most_frames, 4);
    fresh();
    CHECK(link_read_split(&g_host, &k_port, PAGE, 4u, 20u, &r));
    CHECK(all_of(&r, 4u, 20u, 1u));
    /* Past the page's end: refused, as a single read would be. */
    fresh();
    CHECK(!link_read_split(&g_host, &k_port, PAGE, 0u, LINK_MAX_REGS + 1u, &r));
}

static void move_after_first(int at)
{
    if (at == 0) {
        state(2u);
    }
}

/* The page moves after the first part: the second read of the first part
 * differs, and the parts are read again, all of the new state. */
TEST_CASE(a_torn_read_is_seen_and_read_again)
{
    link_msg_t r;

    fresh();
    g_after = move_after_first;
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, 28u, &r));
    CHECK(all_of(&r, 0u, 28u, 2u));
    CHECK_EQ(g_exchanges, 6);
}

static void move_second_part_only(int at)
{
    if (at == 0) {
        for (unsigned i = 16u; i < LINK_MAX_REGS; ++i) {
            g_page[i] = (uint16_t)(200u + i);
        }
    }
}

/* Only the second part moves, after the first is read: the copy is the page as
 * it stood at the read of the second, which is one state. */
TEST_CASE(a_change_in_the_second_part_alone_is_one_state)
{
    link_msg_t r;

    fresh();
    g_after = move_second_part_only;
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, 28u, &r));
    CHECK_EQ(g_exchanges, 3);
    CHECK_EQ(r.regs[0], 100u);
    CHECK_EQ(r.regs[16], 216u);
}

static void move_always(int at)
{
    state((uint16_t)(10 + at));
}

/* A first part that moves at every exchange: no copy, a NACK of BUSY after
 * LINK_READ_TRIES tries. */
TEST_CASE(a_page_that_never_holds_still_is_busy_and_never_torn)
{
    link_msg_t r;

    fresh();
    g_after = move_always;
    CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, 28u, &r));
    CHECK_EQ(r.op, LINK_OP_NACK);
    CHECK_EQ(r.regs[0], (uint16_t)LINK_NACK_BUSY);
    CHECK_EQ(g_exchanges, (int)(3u * LINK_READ_TRIES));
}

/* A refusal of any part is the answer; an unanswered part ends the read. */
TEST_CASE(a_refused_or_unanswered_part_ends_the_read)
{
    link_msg_t r;

    for (int at = 0; at < 3; ++at) {
        fresh();
        g_refuse_at = at;
        CHECK(link_read_split(&g_host, &k_port, PAGE, 0u, 28u, &r));
        CHECK_EQ(r.op, LINK_OP_NACK);
        CHECK_EQ(r.regs[0], (uint16_t)LINK_NACK_BAD_RANGE);
        CHECK_EQ(g_exchanges, at + 1);
        fresh();
        g_silent_at = at;
        CHECK(!link_read_split(&g_host, &k_port, PAGE, 0u, 28u, &r));
        CHECK_EQ(g_exchanges, at + 1);
    }
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
    RUN(the_prog_page_is_read_in_parts_of_at_most_4_frames);
    RUN(a_read_one_past_the_limit_splits_and_the_widest_is_two_halves);
    RUN(a_torn_read_is_seen_and_read_again);
    RUN(a_change_in_the_second_part_alone_is_one_state);
    RUN(a_page_that_never_holds_still_is_busy_and_never_torn);
    RUN(a_refused_or_unanswered_part_ends_the_read);
    RUN(the_console_window_is_one_read_of_4_frames);
    return test_summary("link_read_split");
}
