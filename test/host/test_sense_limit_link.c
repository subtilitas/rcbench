/*
 * The panel's half of SENSE_LIMIT (sense_limit_link.h) against the
 * IOmcu's page code (sense_limit.c): read whole at link-up, the limits
 * written after their settle in any bank state and only where they differ,
 * OVER read while a limit is held and each bit told once, at most one
 * alert every 5 s and a channel once every 30 s, a lost reply's bits said
 * as lost, a refusal said once, and nothing sent to an IOmcu older than
 * 4.27.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "sense_limit.h"
#include "sense_limit_link.h"
#include "sense_page.h"

static sense_page_t       pg;     /* the IOmcu's end */
static sense_limit_link_t ll;     /* the panel's end */
static uint32_t now;
static uint16_t minor;
static unsigned writes, reads;
static bool     refuse;           /* the page refuses every write */
static bool     drop;             /* the next reply is lost */

static int far_exchange(const sense_limit_link_op_t *op, uint16_t *regs)
{
    if (minor < LINK_MINOR_SENSE_LIMIT) {
        return LINK_NACK_BAD_PAGE;
    }
    if (op->write) {
        ++writes;
        if (refuse) {
            return LINK_NACK_BAD_VALUE;
        }
        const uint8_t nack = sense_limit_write(&pg, op->off, op->n, op->regs);
        return (nack == 0u) ? SENSE_LIMIT_LINK_ACK : (int)nack;
    }
    ++reads;
    memset(regs, 0, LINK_MAX_REGS * sizeof(uint16_t));
    sense_limit_read(&pg, op->off, op->n, regs);    /* clears OVER */
    if (drop) {
        drop = false;
        return SENSE_LIMIT_LINK_NO_ANSWER;
    }
    return SENSE_LIMIT_LINK_ACK;
}

/* One poll: every exchange owed, then 50 ms on. */
static void poll_once(void)
{
    for (int k = 0; k < 4; ++k) {
        sense_limit_link_op_t op;
        if (!sense_limit_link_next(&ll, now, &op)) {
            break;
        }
        uint16_t regs[LINK_MAX_REGS];
        const int r = far_exchange(&op, regs);
        sense_limit_link_done(&ll, r,
                              (op.write || r != SENSE_LIMIT_LINK_ACK) ? NULL
                                                                      : regs,
                              now);
    }
    now += 50u;
}

static void polls(int n)
{
    for (int i = 0; i < n; ++i) {
        poll_once();
    }
}

static void fresh(uint16_t m, uint32_t t0)
{
    sense_page_init(&pg);
    sense_limit_link_init(&ll);
    now = t0;
    minor = m;
    writes = reads = 0u;
    refuse = drop = false;
    sense_limit_link_came_up(&ll, minor, now);
}

static void want_one(unsigned c, uint16_t ma)
{
    uint16_t w[LINK_SL_KEPT] = { 0 };
    w[c] = ma;
    sense_limit_link_want(&ll, w, now);
}

/* A window of channel @p c over its limit, as core 0 publishes it. */
static void over(unsigned c)
{
    pg.limit[LINK_SL_OVER] = (uint16_t)(pg.limit[LINK_SL_OVER] | (1u << c));
    ++pg.limit[LINK_SL_OVER_COUNT];
}

TEST_CASE(nothing_goes_to_an_iomcu_older_than_4_27)
{
    fresh(26u, 1000u);
    want_one(0, 500u);
    polls(40);
    CHECK_EQ(reads + writes, 0u);
    CHECK_EQ(sense_limit_link_status(&ll), SENSE_LIMIT_ST_OLD);
    /* SETUP marks the limits asked: none is judged. */
    CHECK_EQ(sense_limit_link_unheld(&ll), 0x0001u);
    /* One that names 4.27 and refuses the page is left alone. */
    fresh(27u, 1000u);
    minor = 26u;                        /* its pages are 4.26's */
    polls(40);
    CHECK_EQ(reads, 0u);
    CHECK_EQ(sense_limit_link_status(&ll), SENSE_LIMIT_ST_OLD);
    /* No link: nothing, and nothing known. */
    sense_limit_link_lost(&ll);
    CHECK_EQ(sense_limit_link_status(&ll), SENSE_LIMIT_ST_UNKNOWN);
    CHECK_EQ(sense_limit_link_status(NULL), SENSE_LIMIT_ST_UNKNOWN);
    sense_limit_link_op_t op;
    CHECK(!sense_limit_link_next(&ll, now, &op));
    CHECK(!sense_limit_link_next(NULL, now, &op));
    CHECK(!sense_limit_link_next(&ll, now, NULL));
}

/* Read whole first; the limits go out SETTLE_MS after the last edit, in
 * one write of 12 registers, and only where the page differs; with no
 * limit held OVER is not read. */
TEST_CASE(the_page_is_read_then_the_limits_written_after_their_settle)
{
    fresh(27u, 1000u);
    CHECK_EQ(sense_limit_link_status(&ll), SENSE_LIMIT_ST_UNKNOWN);
    want_one(4, 0u);
    CHECK_EQ(sense_limit_link_unheld(&ll), 0u);   /* not read yet */
    polls(1);
    CHECK_EQ(reads, 1u);
    CHECK_EQ(sense_limit_link_status(&ll), SENSE_LIMIT_ST_HELD);
    /* Nothing held and nothing asked: no OVER read. */
    polls(20);
    CHECK_EQ(reads, 1u);
    CHECK_EQ(writes, 0u);
    want_one(4, 2000u);
    CHECK_EQ(sense_limit_link_status(&ll), SENSE_LIMIT_ST_PENDING);
    CHECK_EQ(sense_limit_link_unheld(&ll), 0x0010u);
    polls(9);                           /* 450 ms */
    CHECK_EQ(writes, 0u);
    polls(2);
    CHECK_EQ(writes, 1u);
    CHECK_EQ(pg.limit[4], 2000u);
    CHECK_EQ(sense_limit_link_held_ma(&ll, 4u), 2000u);
    CHECK_EQ(sense_limit_link_held_ma(&ll, 12u), 0u);
    CHECK_EQ(sense_limit_link_held_ma(NULL, 4u), 0u);
    CHECK_EQ(sense_limit_link_status(&ll), SENSE_LIMIT_ST_HELD);
    CHECK_EQ(sense_limit_link_unheld(&ll), 0u);
    CHECK_EQ(sense_limit_link_unheld(NULL), 0u);
    /* The same again is no write; OVER is read every 200 ms now. */
    const unsigned r0 = reads;
    want_one(4, 2000u);
    polls(40);                          /* 2 s */
    CHECK_EQ(writes, 1u);
    CHECK(reads - r0 >= 9u && reads - r0 <= 11u);
    /* A limit above the page's top goes out as the top. */
    want_one(4, 40000u);
    polls(12);
    CHECK_EQ(pg.limit[4], (uint16_t)LINK_SL_MA_MAX);
    sense_limit_link_want(&ll, NULL, now);
    sense_limit_link_want(NULL, NULL, now);
}

/* A page that already holds the limits asked, at link-up: no write. */
TEST_CASE(limits_the_page_keeps_are_not_written_again)
{
    fresh(27u, 1000u);
    pg.limit[2] = 750u;
    want_one(2, 750u);
    polls(20);
    CHECK_EQ(writes, 0u);
    CHECK_EQ(sense_limit_link_status(&ll), SENSE_LIMIT_ST_HELD);
    /* The link goes and comes back: read whole again, the asked kept. */
    sense_limit_link_lost(&ll);
    CHECK_EQ(sense_limit_link_held_ma(&ll, 2u), 0u);
    const unsigned r0 = reads;
    sense_limit_link_came_up(&ll, 27u, now);
    polls(1);
    CHECK_EQ(reads, r0 + 1u);                     /* whole, OVER in it */
    CHECK_EQ(sense_limit_link_held_ma(&ll, 2u), 750u);
    CHECK_EQ(writes, 0u);
}

/* A refused write is said once and not tried again until an edit. */
TEST_CASE(a_refused_write_is_said_once_until_the_next_edit)
{
    fresh(27u, 1000u);
    refuse = true;
    want_one(0, 300u);
    polls(20);
    CHECK_EQ(writes, 1u);
    CHECK_EQ(sense_limit_link_status(&ll), SENSE_LIMIT_ST_REFUSED);
    CHECK_EQ(sense_limit_link_unheld(&ll), 0x0001u);
    unsigned ch = 99u;
    CHECK_EQ(sense_limit_link_event(&ll, now, &ch),
             SENSE_LIMIT_LINK_EV_REFUSED);
    CHECK_EQ(sense_limit_link_event(&ll, now + 10000u, &ch),
             SENSE_LIMIT_LINK_EV_NONE);
    polls(40);
    CHECK_EQ(writes, 1u);
    refuse = false;
    want_one(0, 400u);
    polls(20);
    CHECK_EQ(writes, 2u);
    CHECK_EQ(pg.limit[0], 400u);
}

/* Each OVER bit is told once, the lowest channel first; at most one alert
 * every GAP_MS; a channel told is not told again for REPEAT_MS while it
 * keeps passing, and is after. */
TEST_CASE(each_channel_over_is_told_at_its_pace)
{
    fresh(27u, 1000u);
    uint16_t w[LINK_SL_KEPT] = { 0 };
    w[1] = 500u;
    w[7] = 500u;
    sense_limit_link_want(&ll, w, now);
    polls(20);
    unsigned ch = 99u;
    CHECK_EQ(sense_limit_link_event(&ll, now, &ch), SENSE_LIMIT_LINK_EV_NONE);
    over(7);
    over(1);
    polls(5);
    CHECK_EQ(pg.limit[LINK_SL_OVER], 0u);         /* the read cleared it */
    CHECK_EQ(sense_limit_link_event(&ll, now, &ch), SENSE_LIMIT_LINK_EV_OVER);
    CHECK_EQ(ch, 1u);
    CHECK_EQ(sense_limit_link_event(&ll, now, &ch), SENSE_LIMIT_LINK_EV_NONE);
    const uint32_t t1 = now;
    CHECK_EQ(sense_limit_link_event(&ll, t1 + SENSE_LIMIT_LINK_GAP_MS - 1u,
                                    &ch), SENSE_LIMIT_LINK_EV_NONE);
    CHECK_EQ(sense_limit_link_event(&ll, t1 + SENSE_LIMIT_LINK_GAP_MS, &ch),
             SENSE_LIMIT_LINK_EV_OVER);
    CHECK_EQ(ch, 7u);
    /* Channel 1 passing on: not told again within REPEAT_MS. */
    for (int i = 0; i < 100; ++i) {               /* 5 s */
        over(1);
        polls(1);
    }
    CHECK_EQ(sense_limit_link_event(&ll, now, &ch), SENSE_LIMIT_LINK_EV_NONE);
    now = t1 + SENSE_LIMIT_LINK_REPEAT_MS;
    over(1);
    polls(5);
    CHECK_EQ(sense_limit_link_event(&ll, now, &ch), SENSE_LIMIT_LINK_EV_OVER);
    CHECK_EQ(ch, 1u);
    CHECK_EQ(sense_limit_link_event(NULL, now, &ch), SENSE_LIMIT_LINK_EV_NONE);
}

/* A reply lost with its bits: the next read finds OVER_COUNT moved and
 * no bit, and says a limit was passed without saying where. */
TEST_CASE(bits_lost_on_the_link_are_said_as_lost)
{
    fresh(27u, 1000u);
    want_one(3, 100u);
    polls(20);
    over(3);
    drop = true;
    polls(10);                                    /* two reads */
    CHECK(!drop);
    unsigned ch = 99u;
    CHECK_EQ(sense_limit_link_event(&ll, now, &ch), SENSE_LIMIT_LINK_EV_LOST);
    CHECK_EQ(ch, 99u);
    CHECK_EQ(sense_limit_link_event(&ll, now + SENSE_LIMIT_LINK_GAP_MS, &ch),
             SENSE_LIMIT_LINK_EV_NONE);
    /* An answered read that shows its bit is no loss. */
    over(3);
    polls(5);
    CHECK_EQ(sense_limit_link_event(&ll, now + SENSE_LIMIT_LINK_GAP_MS, &ch),
             SENSE_LIMIT_LINK_EV_OVER);
    CHECK_EQ(ch, 3u);
}

/* Bits the IOmcu kept from before the link came up are told; across the
 * 32-bit millisecond wrap every clock keeps its pace. */
TEST_CASE(the_clocks_hold_across_the_wrap)
{
    fresh(27u, UINT32_MAX - 300u);
    pg.limit[0] = 250u;
    over(0);
    want_one(0, 250u);
    polls(1);
    unsigned ch = 99u;
    CHECK_EQ(sense_limit_link_event(&ll, now, &ch), SENSE_LIMIT_LINK_EV_OVER);
    CHECK_EQ(ch, 0u);
    const unsigned r0 = reads;
    polls(20);                                    /* 1 s, over the wrap */
    CHECK(reads - r0 >= 4u && reads - r0 <= 6u);
    over(0);
    polls(5);
    CHECK_EQ(sense_limit_link_event(&ll, now, &ch), SENSE_LIMIT_LINK_EV_NONE);
    want_one(0, 260u);
    const unsigned w0 = writes;
    polls(9);
    CHECK_EQ(writes, w0);
    polls(2);
    CHECK_EQ(writes, w0 + 1u);
    /* A read refused otherwise than BAD_PAGE is asked again on its clock. */
    sense_limit_link_op_t op;
    now += SENSE_LIMIT_LINK_READ_MS;
    CHECK(sense_limit_link_next(&ll, now, &op));
    CHECK_EQ(op.kind, SENSE_LIMIT_LINK_OP_OVER);
    sense_limit_link_done(&ll, (int)LINK_NACK_BAD_RANGE, NULL, now);
    CHECK(!sense_limit_link_next(&ll, now, &op));
    CHECK(sense_limit_link_next(&ll, now + SENSE_LIMIT_LINK_READ_MS, &op));
    CHECK_EQ(sense_limit_link_status(&ll), SENSE_LIMIT_ST_HELD);
    sense_limit_link_init(NULL);
    sense_limit_link_lost(NULL);
    sense_limit_link_came_up(NULL, 27u, now);
    sense_limit_link_done(NULL, 0, NULL, now);
}

int main(void)
{
    RUN(nothing_goes_to_an_iomcu_older_than_4_27);
    RUN(the_page_is_read_then_the_limits_written_after_their_settle);
    RUN(limits_the_page_keeps_are_not_written_again);
    RUN(a_refused_write_is_said_once_until_the_next_edit);
    RUN(each_channel_over_is_told_at_its_pace);
    RUN(bits_lost_on_the_link_are_said_as_lost);
    RUN(the_clocks_hold_across_the_wrap);
    return test_summary("sense_limit_link");
}
