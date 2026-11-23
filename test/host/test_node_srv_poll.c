/**
 * @file
 * @brief The servo node's poll of the Core2350B
 *        (firmware/node/main/node_srv_poll.c).
 *
 * The failures this is about: a CONTROL write that slips behind the reads
 * past the IOmcu's 200ms silence, a row read at the wrong period, STATUS not
 * read every 50ms while a fault or an ARM waits, a stalled link that comes
 * back with a burst, and the periods breaking at the ms clock's wrap.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "link_dev.h"
#include "link_pages.h"
#include "node_srv_poll.h"

/** @brief Take every due row at @p now_ms, in order; count them. */
static unsigned take_all(node_srv_poll_t *p, uint32_t now_ms, bool fast,
                         node_srv_row_t *first)
{
    unsigned n = 0u;
    *first = NODE_SRV_ROW_NONE;
    for (;;) {
        const node_srv_row_t r = node_srv_poll_next(p, now_ms, fast);
        if (r == NODE_SRV_ROW_NONE) {
            return n;
        }
        if (n == 0u) {
            *first = r;
        }
        node_srv_poll_taken(p, r, now_ms);
        ++n;
    }
}

TEST_CASE(the_table_reads_whole_pages_at_their_periods)
{
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_CONTROL].page, LINK_PAGE_CONTROL);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_CONTROL].count, 2u);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_STATUS].count, LINK_ST_COUNT);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_POWER].page, LINK_PAGE_POWER);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_POWER].count, LINK_PW_COUNT);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_SUPPLY].count, LINK_SP_COUNT);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_WIN_A].count, LINK_MAX_REGS);
    CHECK_EQ(node_srv_rows[NODE_SRV_ROW_WIN_B].page, LINK_PAGE_SENSE_WIN_B);
    /* The write that keeps the IOmcu's link alive comes at least 4 times
     * inside its silence window. */
    CHECK(4u * node_srv_rows[NODE_SRV_ROW_CONTROL].period_ms
          <= LINK_DEV_SILENCE_MS);
}

TEST_CASE(every_row_is_due_at_the_start_control_first)
{
    node_srv_poll_t p;
    node_srv_poll_init(&p);
    node_srv_row_t first;
    CHECK_EQ(take_all(&p, 12345u, false, &first), (unsigned)NODE_SRV_ROWS);
    CHECK_EQ(first, NODE_SRV_ROW_CONTROL);
    CHECK_EQ(node_srv_poll_next(&p, 12345u, false), NODE_SRV_ROW_NONE);
}

/** @brief Each row due at its period after @p t0 and not a ms before. */
static void periods_from(uint32_t t0)
{
    node_srv_poll_t p;
    node_srv_poll_init(&p);
    node_srv_row_t first;
    (void)take_all(&p, t0, false, &first);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 49u, false), NODE_SRV_ROW_NONE);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 50u, false), NODE_SRV_ROW_CONTROL);
    node_srv_poll_taken(&p, NODE_SRV_ROW_CONTROL, t0 + 50u);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 50u, false), NODE_SRV_ROW_WIN_A);
    node_srv_poll_taken(&p, NODE_SRV_ROW_WIN_A, t0 + 50u);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 50u, false), NODE_SRV_ROW_WIN_B);
    node_srv_poll_taken(&p, NODE_SRV_ROW_WIN_B, t0 + 50u);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 99u, false), NODE_SRV_ROW_NONE);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 100u, false), NODE_SRV_ROW_CONTROL);
    node_srv_poll_taken(&p, NODE_SRV_ROW_CONTROL, t0 + 100u);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 100u, false), NODE_SRV_ROW_POWER);
    node_srv_poll_taken(&p, NODE_SRV_ROW_POWER, t0 + 100u);
    CHECK_EQ(node_srv_poll_next(&p, t0 + 100u, false), NODE_SRV_ROW_SUPPLY);
    /* STATUS at 500ms and not at 499ms. */
    node_srv_poll_t q;
    node_srv_poll_init(&q);
    for (unsigned k = 0; k < (unsigned)NODE_SRV_ROWS; ++k) {
        node_srv_poll_taken(&q, (node_srv_row_t)k, t0 + 499u);
    }
    node_srv_poll_taken(&q, NODE_SRV_ROW_STATUS, t0);
    CHECK_EQ(node_srv_poll_next(&q, t0 + 499u, false), NODE_SRV_ROW_NONE);
    CHECK_EQ(node_srv_poll_next(&q, t0 + 500u, false), NODE_SRV_ROW_STATUS);
}

TEST_CASE(each_row_comes_at_its_period_and_not_a_ms_before)
{
    periods_from(1000u);
}

TEST_CASE(the_periods_hold_across_the_wrap)
{
    periods_from(0xFFFFFFFFu - 60u);
    periods_from(0xFFFFFFFFu - 499u);
    periods_from(0xFFFFFFFFu);
}

TEST_CASE(fast_reads_status_every_50ms_at_once)
{
    node_srv_poll_t p;
    node_srv_poll_init(&p);
    node_srv_row_t first;
    (void)take_all(&p, 0u, false, &first);
    (void)take_all(&p, 50u, false, &first);
    /* STATUS read at 0; fast from 60ms on. */
    CHECK_EQ(node_srv_poll_next(&p, 60u, true), NODE_SRV_ROW_STATUS);
    node_srv_poll_taken(&p, NODE_SRV_ROW_STATUS, 60u);
    CHECK_EQ(node_srv_poll_next(&p, 99u, true), NODE_SRV_ROW_NONE);
    CHECK_EQ(node_srv_poll_next(&p, 100u, true), NODE_SRV_ROW_CONTROL);
    node_srv_poll_taken(&p, NODE_SRV_ROW_CONTROL, 100u);
    CHECK_EQ(node_srv_poll_next(&p, 109u, true), NODE_SRV_ROW_POWER);
    node_srv_poll_taken(&p, NODE_SRV_ROW_POWER, 109u);
    node_srv_poll_taken(&p, NODE_SRV_ROW_SUPPLY, 109u);
    node_srv_poll_taken(&p, NODE_SRV_ROW_WIN_A, 109u);
    node_srv_poll_taken(&p, NODE_SRV_ROW_WIN_B, 109u);
    CHECK_EQ(node_srv_poll_next(&p, 109u, true), NODE_SRV_ROW_NONE);
    CHECK_EQ(node_srv_poll_next(&p, 110u, true), NODE_SRV_ROW_STATUS);
    CHECK_EQ(node_srv_poll_next(&p, 110u, false), NODE_SRV_ROW_NONE);
}

TEST_CASE(a_stalled_link_comes_back_with_one_exchange_a_row)
{
    node_srv_poll_t p;
    node_srv_poll_init(&p);
    node_srv_row_t first;
    (void)take_all(&p, 0u, false, &first);
    /* 1000ms without an exchange: each row once, CONTROL first. */
    CHECK_EQ(take_all(&p, 1000u, false, &first), (unsigned)NODE_SRV_ROWS);
    CHECK_EQ(first, NODE_SRV_ROW_CONTROL);
    CHECK_EQ(take_all(&p, 1049u, false, &first), 0u);
}

TEST_CASE(now_makes_a_row_due_at_once_and_ignores_none)
{
    node_srv_poll_t p;
    node_srv_poll_init(&p);
    node_srv_row_t first;
    (void)take_all(&p, 0u, false, &first);
    CHECK_EQ(node_srv_poll_next(&p, 10u, false), NODE_SRV_ROW_NONE);
    node_srv_poll_now(&p, NODE_SRV_ROW_CONTROL);
    node_srv_poll_now(&p, NODE_SRV_ROW_NONE);
    node_srv_poll_taken(&p, NODE_SRV_ROW_NONE, 10u);
    CHECK_EQ(node_srv_poll_next(&p, 10u, false), NODE_SRV_ROW_CONTROL);
}

int main(void)
{
    RUN(the_table_reads_whole_pages_at_their_periods);
    RUN(every_row_is_due_at_the_start_control_first);
    RUN(each_row_comes_at_its_period_and_not_a_ms_before);
    RUN(the_periods_hold_across_the_wrap);
    RUN(fast_reads_status_every_50ms_at_once);
    RUN(a_stalled_link_comes_back_with_one_exchange_a_row);
    RUN(now_makes_a_row_due_at_once_and_ignores_none);
    return test_summary("node_srv_poll");
}
