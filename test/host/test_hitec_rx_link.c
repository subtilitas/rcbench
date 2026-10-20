/*
 * The panel's half of the Hitec receive mode (shared/bench/hitec_rx_link.c):
 * when SETUP's Hitec D receive is written to the HITEC page's register 23,
 * what each answer leaves, and what the row says, against an IOmcu of
 * 4.24 and of 4.23.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "hitec_rx_link.h"
#include "link_pages.h"

static hitec_rx_link_t L;

TEST_CASE(nothing_is_written_before_a_link_and_pull_up_is_the_start)
{
    uint16_t v = 0xBEEFu;

    hitec_rx_link_init(&L);
    CHECK_EQ(L.want, LINK_HITEC_RX_PULL_UP);
    CHECK(!hitec_rx_link_next(&L, 0u, &v));
    CHECK_EQ(v, 0xBEEFu);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_HELD);
    /* An edit with no link: owed, nothing said. */
    hitec_rx_link_want(&L, LINK_HITEC_RX_PRECHARGE);
    CHECK(!hitec_rx_link_next(&L, 0u, &v));
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_HELD);
    /* NULLs. */
    hitec_rx_link_init(NULL);
    hitec_rx_link_want(NULL, 1u);
    hitec_rx_link_came_up(NULL, 24u);
    hitec_rx_link_lost(NULL);
    hitec_rx_link_done(NULL, 0, 0u);
    CHECK(!hitec_rx_link_next(NULL, 0u, &v));
    CHECK(!hitec_rx_link_next(&L, 0u, NULL));
    CHECK_EQ(hitec_rx_link_mark(NULL), HITEC_RX_LINK_HELD);
}

TEST_CASE(a_link_up_writes_the_mode_and_an_ack_holds_it)
{
    uint16_t v = 0u;

    hitec_rx_link_init(&L);
    hitec_rx_link_came_up(&L, LINK_MINOR_HITEC_RX);
    /* PULL-UP is written too: the IOmcu may hold what a console wrote. */
    CHECK(hitec_rx_link_next(&L, 10u, &v));
    CHECK_EQ(v, LINK_HITEC_RX_PULL_UP);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_PENDING);
    hitec_rx_link_done(&L, HITEC_RX_LINK_ACK, 10u);
    CHECK(!hitec_rx_link_next(&L, 11u, &v));
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_HELD);
    /* An edit: written at the next poll. */
    hitec_rx_link_want(&L, LINK_HITEC_RX_PRECHARGE);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_PENDING);
    CHECK(hitec_rx_link_next(&L, 12u, &v));
    CHECK_EQ(v, LINK_HITEC_RX_PRECHARGE);
    hitec_rx_link_done(&L, HITEC_RX_LINK_ACK, 12u);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_HELD);
    /* The same value again owes nothing. */
    hitec_rx_link_want(&L, LINK_HITEC_RX_PRECHARGE);
    CHECK(!hitec_rx_link_next(&L, 13u, &v));
    /* A value out of range is PULL-UP. */
    hitec_rx_link_want(&L, 7u);
    CHECK(hitec_rx_link_next(&L, 14u, &v));
    CHECK_EQ(v, LINK_HITEC_RX_PULL_UP);
}

TEST_CASE(an_unanswered_write_goes_again_after_500_ms)
{
    uint16_t v = 0u;

    hitec_rx_link_init(&L);
    hitec_rx_link_want(&L, LINK_HITEC_RX_PRECHARGE);
    hitec_rx_link_came_up(&L, LINK_PROTOCOL_MINOR);
    CHECK(hitec_rx_link_next(&L, 0xFFFFFF00u, &v));
    hitec_rx_link_done(&L, HITEC_RX_LINK_NO_ANSWER, 0xFFFFFF00u);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_PENDING);
    /* Across the wrap of the clock: 499 ms waits, 500 ms writes. */
    CHECK(!hitec_rx_link_next(&L, 0xFFFFFF00u + 499u, &v));
    CHECK(hitec_rx_link_next(&L, 0xFFFFFF00u + 500u, &v));
    CHECK_EQ(v, LINK_HITEC_RX_PRECHARGE);
    /* An edit in between is written at once. */
    hitec_rx_link_done(&L, HITEC_RX_LINK_NO_ANSWER, 1000u);
    hitec_rx_link_want(&L, LINK_HITEC_RX_PULL_UP);
    CHECK(hitec_rx_link_next(&L, 1001u, &v));
    CHECK_EQ(v, LINK_HITEC_RX_PULL_UP);
    /* The link lost and back: owed again, at once. */
    hitec_rx_link_done(&L, HITEC_RX_LINK_NO_ANSWER, 2000u);
    hitec_rx_link_lost(&L);
    CHECK(!hitec_rx_link_next(&L, 2001u, &v));
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_HELD);
    hitec_rx_link_came_up(&L, LINK_PROTOCOL_MINOR);
    CHECK(hitec_rx_link_next(&L, 2002u, &v));
}

TEST_CASE(a_refusal_ends_the_debt_until_an_edit_or_a_link_up)
{
    uint16_t v = 0u;

    hitec_rx_link_init(&L);
    hitec_rx_link_want(&L, LINK_HITEC_RX_PRECHARGE);
    hitec_rx_link_came_up(&L, LINK_MINOR_HITEC_RX);
    CHECK(hitec_rx_link_next(&L, 0u, &v));
    hitec_rx_link_done(&L, LINK_NACK_BAD_VALUE, 0u);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_REFUSED);
    CHECK(!hitec_rx_link_next(&L, 10000u, &v));
    /* An edit: written again. */
    hitec_rx_link_want(&L, LINK_HITEC_RX_PULL_UP);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_PENDING);
    CHECK(hitec_rx_link_next(&L, 10001u, &v));
    hitec_rx_link_done(&L, LINK_NACK_READ_ONLY, 10001u);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_REFUSED);
    /* A link-up: written again. */
    hitec_rx_link_came_up(&L, LINK_MINOR_HITEC_RX);
    CHECK(hitec_rx_link_next(&L, 10002u, &v));
    hitec_rx_link_done(&L, HITEC_RX_LINK_ACK, 10002u);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_HELD);
}

TEST_CASE(an_iomcu_before_4_24_is_sent_nothing_and_said_to_use_the_pull_up)
{
    uint16_t v = 0u;

    hitec_rx_link_init(&L);
    hitec_rx_link_came_up(&L, LINK_MINOR_HITEC_RX - 1u);
    CHECK(!hitec_rx_link_next(&L, 0u, &v));
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_HELD);
    hitec_rx_link_want(&L, LINK_HITEC_RX_PRECHARGE);
    CHECK(!hitec_rx_link_next(&L, 1u, &v));
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_OLD);
    hitec_rx_link_want(&L, LINK_HITEC_RX_PULL_UP);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_HELD);
    /* Before the HITEC page: no Hitec line at all, at its last minor and
     * at 0; with PULL-UP asked nothing to say. */
    hitec_rx_link_want(&L, LINK_HITEC_RX_PRECHARGE);
    hitec_rx_link_came_up(&L, LINK_MINOR_HITEC - 1u);
    CHECK(!hitec_rx_link_next(&L, 1u, &v));
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_NO_LINE);
    hitec_rx_link_came_up(&L, 0u);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_NO_LINE);
    hitec_rx_link_want(&L, LINK_HITEC_RX_PULL_UP);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_HELD);
    /* 4.23 itself has the line and receives with the pull-up. */
    hitec_rx_link_want(&L, LINK_HITEC_RX_PRECHARGE);
    hitec_rx_link_came_up(&L, LINK_MINOR_HITEC);
    CHECK_EQ(hitec_rx_link_mark(&L), HITEC_RX_LINK_OLD);
    /* The IOmcu updated to 4.24: the mode goes out. */
    hitec_rx_link_came_up(&L, LINK_MINOR_HITEC_RX);
    CHECK(hitec_rx_link_next(&L, 2u, &v));
    CHECK_EQ(v, LINK_HITEC_RX_PRECHARGE);
    /* A minor past a byte is held at 255. */
    hitec_rx_link_came_up(&L, 0x1234u);
    CHECK_EQ(L.minor, 0xFFu);
}

int main(void)
{
    RUN(nothing_is_written_before_a_link_and_pull_up_is_the_start);
    RUN(a_link_up_writes_the_mode_and_an_ack_holds_it);
    RUN(an_unanswered_write_goes_again_after_500_ms);
    RUN(a_refusal_ends_the_debt_until_an_edit_or_a_link_up);
    RUN(an_iomcu_before_4_24_is_sent_nothing_and_said_to_use_the_pull_up);
    return test_summary("hitec_rx_link");
}
