/*
 * The INA3221's CH1 windows from the part to the panel: the modelled part,
 * the coprocessor's schedule and pages, and the panel's sense_link, run by
 * sense_chain.h on one clock.
 *
 * Under test: every window number handed over once and in order with polls
 * 50, 53, 55, 100, 150, 199 and 200 ms apart, none lost; with polls 201 and
 * 250 ms apart the count of lost windows equal to the numbers missing; each
 * window under its own number; a reply lost on the link losing nothing; the
 * window number across 65535 to 0 and every timer across the 2^32 ms tick
 * wrap; a ring that starts again -- a set-up written, a coprocessor
 * restart seen and one not seen -- handing over nothing twice and nothing
 * invented; numbers the coprocessor skipped neither handed over nor
 * counted; a link lost and back; windows nobody takes kept to the ring's
 * depth and counted beyond; a part that resets itself; a clipped window
 * and a negative current as plain readings; a coprocessor of protocol 4.10
 * sent nothing on SERVO_WIN, and one that names 4.11 and refuses the page
 * asked once; CH2 and CH3 on the row of their window; and the CAN frames
 * of a poll.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "bench_state.h"
#include "sense_chain.h"

/* What was handed over since tally_clear(). */
typedef struct {
    unsigned n;
    uint16_t first, last;
    unsigned back;       /* a number not ahead of the one before          */
    unsigned missing;    /* numbers stepped over                          */
    unsigned wrong;      /* a mean that is another window's               */
    unsigned empty;      /* windows without current samples               */
    unsigned most;       /* the most windows one poll handed over         */
    bool     wrapped;    /* a number below the one before it by the wrap  */
} tally_t;

static tally_t tl;

static void tally_clear(void)
{
    memset(&tl, 0, sizeof(tl));
}

static void tally(const sense_link_win_t *w)
{
    if (tl.n == 0u) {
        tl.first = w->number;
    } else {
        const uint16_t step = (uint16_t)(w->number - tl.last);
        if (step == 0u || step >= 0x8000u) {
            ++tl.back;
        } else {
            tl.missing += step - 1u;
        }
        if (w->number < tl.last && step < 0x8000u) {
            tl.wrapped = true;
        }
    }
    if (!w->current) {
        ++tl.empty;
    } else if (w->mean_ma != chain_mean_ma(w->number)) {
        ++tl.wrong;
    }
    tl.last = w->number;
    ++tl.n;
}

/* Every window waiting, as the control task takes them after a poll. */
static unsigned take_all(void)
{
    sense_link_win_t w;
    unsigned n = 0u;
    while (sense_link_take_win(&ch.sl, &w)) {
        tally(&w);
        ++n;
    }
    if (n > tl.most) {
        tl.most = n;
    }
    return n;
}

static void cycles(unsigned n, unsigned period_ms)
{
    for (unsigned i = 0u; i < n; ++i) {
        chain_cycle(period_ms);
        (void)take_all();
    }
}

/* A 4.11 bench linked and handing windows over, polled every
 * @p period_ms, with nothing counted yet. */
static void running(unsigned period_ms, uint32_t tick0)
{
    chain_start(LINK_PROTOCOL_MINOR, tick0, 0x01u);
    chain_far(400u);
    chain_link_up();
    tally_clear();
    cycles(8u, period_ms);
    tally_clear();
}

/* ------------------------------------------------------ the poll period */

/* Polls up to the ring's 200 ms apart: every number once, in order, each
 * with its own mean, and nothing counted lost. */
TEST_CASE(no_window_is_lost_with_polls_up_to_200_ms_apart)
{
    static const unsigned k_period[] = { 50u, 53u, 55u, 100u, 150u, 199u,
                                         200u };
    for (size_t i = 0u; i < sizeof(k_period) / sizeof(k_period[0]); ++i) {
        const unsigned p = k_period[i];
        running(p, 1000u);
        const unsigned head0 = ch.win_head;
        const unsigned all0  = ch.win_all;
        const uint64_t began = ch.us;
        cycles(400u, p);
        CHECK_EQ(tl.back, 0u);
        CHECK_EQ(tl.missing, 0u);
        CHECK_EQ(tl.wrong, 0u);
        CHECK_EQ(tl.empty, 0u);
        CHECK_EQ(sense_link_win_lost(&ch.sl), 0u);
        /* As many as closed in that time, to the one at either end. */
        const unsigned closed = (unsigned)((ch.us - began) / 50000u);
        CHECK(tl.n + 1u >= closed && tl.n <= closed + 1u);
        CHECK_EQ((uint16_t)(tl.last - tl.first) + 1u, tl.n);
        if (p <= 100u) {
            /* Two windows at most a poll: registers 0 to 15 carry them. */
            CHECK_EQ(ch.win_all, all0);
            CHECK_EQ(ch.win_head - head0, 400u);
            CHECK(tl.most <= 2u);
        } else {
            /* A third can have closed: the whole page, in one read. */
            CHECK_EQ(ch.win_head, head0);
            CHECK_EQ(ch.win_all - all0, 400u);
        }
        if (p == 53u || p == 55u || p == 100u) {
            CHECK_EQ(tl.most, 2u);     /* the poll that catches one up */
        }
        if (p == 200u) {
            CHECK_EQ(tl.most, 4u);     /* the ring's depth, every poll */
        }
    }
}

/* Past the ring's depth the windows that left it are counted, and the
 * count is the numbers missing from what was handed over. */
TEST_CASE(windows_beyond_the_ring_are_counted_exactly)
{
    running(201u, 1000u);
    cycles(400u, 201u);
    CHECK_EQ(tl.back, 0u);
    CHECK_EQ(tl.wrong, 0u);
    CHECK(sense_link_win_lost(&ch.sl) > 0u);
    CHECK_EQ(sense_link_win_lost(&ch.sl), tl.missing);
    /* 1 ms a poll over the depth: one window in 50 polls. */
    CHECK_EQ(tl.missing, 8u);

    running(250u, 1000u);
    const uint32_t lost0 = sense_link_win_lost(&ch.sl);
    cycles(100u, 250u);
    CHECK_EQ(tl.back, 0u);
    CHECK_EQ(tl.wrong, 0u);
    /* Five windows close in 250 ms and four are in the ring. */
    CHECK_EQ(tl.n, 400u);
    CHECK_EQ(sense_link_win_lost(&ch.sl) - lost0, 100u);
    CHECK_EQ(tl.missing, 99u);         /* between the 100 polls' windows */
    CHECK_EQ(sense_link_win_lost(NULL), 0u);
}

/* The log's own call at a 53 ms poll: a window number in every row it
 * takes, none stepped over.  SERVO_SENSE alone shows the last window, and
 * loses one in 17 at this period. */
TEST_CASE(every_window_reaches_the_log_at_a_53_ms_poll)
{
    chain_start(LINK_PROTOCOL_MINOR, 1000u, 0x01u);
    chain_far(400u);
    chain_link_up();
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    unsigned rows = 0u;
    unsigned stepped = 0u;
    uint16_t last = 0u;
    for (unsigned i = 0u; i < 400u; ++i) {
        chain_cycle(53u);
        while (sense_link_take_window(&ch.sl, &b)) {
            CHECK(b.servo_new);
            CHECK_EQ(b.servo_ok, 0x01u);
            CHECK_EQ(b.servo_mean_ma[0], chain_mean_ma(b.servo_window));
            CHECK_EQ(b.servo_min_mv[0], 6000u);
            if (rows > 0u && (uint16_t)(b.servo_window - last) != 1u) {
                ++stepped;
            }
            last = b.servo_window;
            ++rows;
            b.servo_new = false;
        }
    }
    CHECK_EQ(stepped, 0u);
    CHECK(rows >= 400u);               /* 53 ms polls, 50 ms windows */
}

/* ---------------------------------------------------- a reply that is lost */

TEST_CASE(a_reply_lost_on_the_link_loses_no_window)
{
    running(53u, 1000u);
    const unsigned all0 = ch.win_all;
    for (unsigned i = 0u; i < 400u; ++i) {
        if (i % 30u == 5u) {
            ch.lose_win = 1u;          /* one reply                 */
        }
        if (i % 30u == 15u) {
            ch.lose_win = 2u;          /* two polls in a row, 159 ms */
        }
        cycles(1u, 53u);
    }
    CHECK_EQ(tl.back, 0u);
    CHECK_EQ(tl.missing, 0u);
    CHECK_EQ(tl.wrong, 0u);
    CHECK_EQ(sense_link_win_lost(&ch.sl), 0u);
    /* The poll after a lost reply reads the whole page at once. */
    CHECK(ch.win_all > all0);
    CHECK(tl.most >= 3u);

    /* Three in a row is 212 ms without a read: past the ring, and what is
     * counted is what is missing. */
    running(53u, 1000u);
    for (unsigned i = 0u; i < 400u; ++i) {
        if (i % 20u == 7u) {
            ch.lose_win = 3u;
        }
        cycles(1u, 53u);
    }
    CHECK_EQ(tl.back, 0u);
    CHECK_EQ(tl.wrong, 0u);
    CHECK(tl.missing > 0u);
    CHECK_EQ(sense_link_win_lost(&ch.sl), tl.missing);
}

/* A read of registers 0 to 15 that owes more than its two windows -- the
 * ring started again -- hands nothing over and asks for the whole page.
 * With a window boundary between the two reads the windows still come
 * from the second reply alone, each under its own number. */
TEST_CASE(a_window_that_closes_between_two_reads_keeps_its_number)
{
    unsigned between = 0u;
    for (unsigned wait = 51u; wait <= 82u; ++wait) {
        running(53u, 1000u);
        (void)chain_poll();
        (void)take_all();
        chain_far_boot();                      /* its numbers start at 0 */
        chain_far(wait);
        tally_clear();
        const unsigned head0 = ch.win_head;
        const unsigned all0  = ch.win_all;
        ch.exch_ms = 7u;
        (void)chain_poll();
        ch.exch_ms = 1u;
        CHECK_EQ(ch.win_head - head0, 1u);
        CHECK_EQ(ch.win_all - all0, 1u);       /* in the same poll */
        const unsigned n = take_all();
        CHECK(n == 1u || n == 2u);
        if (n == 2u) {
            ++between;                         /* number 1 closed meanwhile */
        }
        cycles(20u, 53u);
        CHECK_EQ(tl.first, 0u);
        CHECK_EQ(tl.back, 0u);
        CHECK_EQ(tl.missing, 0u);
        CHECK_EQ(tl.wrong, 0u);
    }
    CHECK(between >= 5u);
}

/* ------------------------------------------------------------ the wraps */

TEST_CASE(the_window_number_runs_on_across_65535)
{
    static const unsigned k_period[] = { 53u, 200u, 250u };
    for (size_t i = 0u; i < sizeof(k_period) / sizeof(k_period[0]); ++i) {
        const unsigned p = k_period[i];
        chain_start(LINK_PROTOCOL_MINOR, 1000u, 0x01u);
        chain_far(120u);
        /* A tick late by 65500 windows less the warm-up's: the numbering
         * goes on from there, some 20 windows before the wrap. */
        chain_far_late((65500u - (8u * p) / 50u) * 50u);
        chain_far(400u);
        chain_link_up();
        tally_clear();
        cycles(8u, p);
        tally_clear();
        const uint32_t lost0 = sense_link_win_lost(&ch.sl);
        cycles(60u, p);
        CHECK(tl.wrapped);
        CHECK(tl.first > 65000u);
        CHECK(tl.last < 1000u);
        CHECK_EQ(tl.back, 0u);
        CHECK_EQ(tl.wrong, 0u);
        CHECK_EQ(sense_link_win_lost(&ch.sl) - lost0, tl.missing
                 + ((p == 250u) ? 1u : 0u));
        if (p <= 200u) {
            CHECK_EQ(tl.missing, 0u);
        }
    }
}

/* The read timers and the whole-page timer across the 2^32 ms tick wrap,
 * with the wrap at every phase of a lost reply. */
TEST_CASE(the_reads_run_on_across_the_tick_wrap)
{
    for (uint32_t before = 20u; before <= 700u; before += 17u) {
        running(53u, (uint32_t)(0u - before - 824u));
        /* running() took 824 ms: the wrap lies `before` ms ahead. */
        CHECK(chain_now() > 0xFFFFF000u);
        const unsigned reads0 = ch.to_win;
        for (unsigned i = 0u; i < 30u; ++i) {
            if (i == 3u) {
                ch.lose_win = 2u;
            }
            cycles(1u, 53u);
        }
        CHECK(chain_now() < 0x00001000u);
        CHECK_EQ(ch.to_win - reads0, 30u);         /* one read a poll */
        CHECK_EQ(tl.back, 0u);
        CHECK_EQ(tl.missing, 0u);
        CHECK_EQ(tl.wrong, 0u);
        CHECK_EQ(sense_link_win_lost(&ch.sl), 0u);
    }
}

/* ------------------------------------------------- a ring that starts again */

/* A set-up the panel writes empties the ring and starts the numbers at 0:
 * every window after the write is handed over, from number 0, and none of
 * the set-up before it. */
TEST_CASE(a_setup_written_starts_the_windows_at_number_0)
{
    running(53u, 1000u);
    cycles(40u, 53u);
    CHECK(tl.last > 40u);
    sense_setup_t w = chain_setup(0x03u);          /* CH2 as well */
    sense_link_want(&ch.sl, &w, chain_now());
    const unsigned writes0 = ch.writes;
    unsigned waited = 0u;
    while (ch.writes == writes0 && waited < 40u) {
        cycles(1u, 53u);
        ++waited;
    }
    CHECK(ch.writes > writes0);                    /* after the 500 ms rest */
    const uint32_t lost0 = sense_link_win_lost(&ch.sl);
    tally_clear();
    cycles(60u, 53u);
    CHECK_EQ(tl.first, 0u);
    CHECK_EQ(tl.back, 0u);
    CHECK_EQ(tl.missing, 0u);
    CHECK_EQ(tl.wrong, 0u);
    CHECK_EQ(sense_link_win_lost(&ch.sl), lost0);
    CHECK(tl.n >= 60u);
}

/* The coprocessor restarts and the link does not notice.  A read before
 * its first window closes shows the ring empty; a read after shows a
 * newest number behind the cursor.  Either way the new windows are handed
 * over from number 0, once. */
TEST_CASE(a_coprocessor_restart_starts_the_windows_again)
{
    static const unsigned k_after[] = { 10u, 130u, 190u };
    for (size_t i = 0u; i < sizeof(k_after) / sizeof(k_after[0]); ++i) {
        running(53u, 1000u);
        cycles(40u, 53u);
        const uint32_t lost0 = sense_link_win_lost(&ch.sl);
        chain_far_boot();
        chain_far(k_after[i]);
        tally_clear();
        cycles(60u, 53u);
        CHECK_EQ(tl.first, 0u);
        CHECK_EQ(tl.back, 0u);
        CHECK_EQ(tl.missing, 0u);
        CHECK_EQ(tl.wrong, 0u);
        CHECK_EQ(sense_link_win_lost(&ch.sl), lost0);
    }
}

/* A coprocessor tick late by more than a window: the numbers it skipped
 * never closed.  They are not handed over, and while they are in the ring
 * at the next read they are not counted lost.  A number that left the
 * ring before a read is counted, closed or not: the page no longer says
 * which it was. */
TEST_CASE(a_number_the_coprocessor_skipped_is_no_window)
{
    static const unsigned k_late[] = { 49u, 50u, 51u, 120u, 170u, 260u };
    for (size_t i = 0u; i < sizeof(k_late) / sizeof(k_late[0]); ++i) {
        running(53u, 1000u);
        cycles(20u, 53u);
        chain_far_late(k_late[i]);
        cycles(40u, 53u);
        CHECK_EQ(tl.back, 0u);
        CHECK_EQ(tl.wrong, 0u);
        /* One number skipped for each whole window the tick was late. */
        CHECK(tl.missing + 1u >= k_late[i] / 50u);
        CHECK(tl.missing <= k_late[i] / 50u);
        if (k_late[i] <= 170u) {
            CHECK_EQ(sense_link_win_lost(&ch.sl), 0u);
        } else {
            /* Four skipped, two of them out of the ring by the read. */
            CHECK_EQ(tl.missing, 4u);
            CHECK_EQ(sense_link_win_lost(&ch.sl), 2u);
        }
    }
}

/* ------------------------------------------------------------- the link */

/* The link lost and back, the coprocessor running on: what the ring holds
 * at the first read may have been handed over before, so the hand-over
 * starts after it.  No number comes twice and none is counted lost. */
TEST_CASE(a_link_lost_and_back_hands_no_window_over_twice)
{
    static const unsigned k_down[] = { 0u, 30u, 120u, 1500u };
    for (size_t i = 0u; i < sizeof(k_down) / sizeof(k_down[0]); ++i) {
        running(53u, 1000u);
        cycles(20u, 53u);
        chain_poll();                              /* read, not yet taken */
        sense_link_lost(&ch.sl);
        CHECK_EQ(take_all(), 0u);                  /* gone with the link  */
        CHECK_EQ(sense_link_windows(&ch.sl), 0u);
        chain_far(k_down[i]);
        const uint16_t before = tl.last;
        chain_link_up();
        const uint16_t at_up = ch.pg.win[LINK_SW_WINDOW];
        cycles(1u, 53u);
        CHECK_EQ(tl.last, before);                 /* the first read: none */
        tally_clear();
        cycles(40u, 53u);
        /* The first one closed after the link-up. */
        CHECK((uint16_t)(tl.first - at_up) >= 1u);
        CHECK((uint16_t)(tl.first - at_up) <= 2u);
        CHECK((uint16_t)(tl.first - before) >= 1u);
        CHECK((uint16_t)(tl.first - before) < 0x8000u);
        CHECK_EQ(tl.back, 0u);
        CHECK_EQ(tl.missing, 0u);
        CHECK_EQ(tl.wrong, 0u);
        CHECK_EQ(sense_link_win_lost(&ch.sl), 0u);
    }
}

/* The coprocessor restarted while the link was down: its numbers start
 * again, and the panel hands over its windows and none of its own making. */
TEST_CASE(a_link_back_to_a_restarted_coprocessor_invents_nothing)
{
    running(53u, 1000u);
    cycles(20u, 53u);
    sense_link_lost(&ch.sl);
    chain_far_boot();
    chain_far(1200u);
    chain_link_up();
    const uint16_t at_up = ch.pg.win[LINK_SW_WINDOW];
    tally_clear();
    cycles(40u, 53u);
    CHECK((uint16_t)(tl.first - at_up) >= 1u);
    CHECK((uint16_t)(tl.first - at_up) <= 2u);
    CHECK_EQ(tl.back, 0u);
    CHECK_EQ(tl.missing, 0u);
    CHECK_EQ(tl.wrong, 0u);
}

/* --------------------------------------------------- nobody takes them */

/* Windows not taken wait, the ring's depth of them.  The rest stay owed on
 * the page: taken late they are still handed over in order, and those the
 * ring let go meanwhile are counted. */
TEST_CASE(windows_not_taken_wait_and_those_past_the_ring_are_counted)
{
    running(50u, 1000u);
    for (unsigned i = 0u; i < 6u; ++i) {
        chain_cycle(50u);
    }
    CHECK_EQ(sense_link_windows(&ch.sl), LINK_SW_RING);
    CHECK_EQ(take_all(), LINK_SW_RING);
    cycles(20u, 50u);
    CHECK_EQ(tl.back, 0u);
    CHECK_EQ(tl.missing, 0u);
    CHECK_EQ(tl.wrong, 0u);
    CHECK_EQ(sense_link_win_lost(&ch.sl), 0u);

    running(50u, 1000u);
    for (unsigned i = 0u; i < 14u; ++i) {
        chain_cycle(50u);
    }
    CHECK_EQ(take_all(), LINK_SW_RING);
    cycles(20u, 50u);
    CHECK_EQ(tl.back, 0u);
    CHECK_EQ(tl.wrong, 0u);
    /* 14 closed and a 15th by the next read; 4 waited, 4 are in the ring. */
    CHECK_EQ(tl.missing, 7u);
    CHECK_EQ(sense_link_win_lost(&ch.sl), 7u);

    sense_link_win_t w;
    CHECK(!sense_link_take_win(&ch.sl, NULL));
    CHECK(!sense_link_take_win(NULL, &w));
    CHECK_EQ(sense_link_windows(NULL), 0u);
}

/* -------------------------------------------------------- the readings */

/* A part that resets itself is found by the coprocessor, which counts it
 * in RESETS and sets the part up again 1000 ms later.  The windows of that
 * time close without samples: each is handed over under its number, empty,
 * and the count shows in the meter's facts. */
TEST_CASE(a_part_that_resets_itself_hands_over_empty_windows)
{
    running(53u, 1000u);
    sense_link_meter_t m;
    sense_link_meter(&ch.sl, &m);
    CHECK(m.resets_read);
    CHECK_EQ(m.resets, 0u);
    cycles(10u, 53u);
    CHECK_EQ(tl.empty, 0u);
    fake_reset3221(ch.i3221);
    cycles(40u, 53u);
    sense_link_meter(&ch.sl, &m);
    CHECK_EQ(m.resets, 1u);
    CHECK((m.flags & LINK_SN_I3221_ONLINE) != 0u);     /* set up again */
    CHECK(tl.empty >= 19u && tl.empty <= 21u);         /* 1000 ms      */
    CHECK_EQ(tl.back, 0u);
    CHECK_EQ(tl.missing, 0u);
    CHECK_EQ(tl.wrong, 0u);
    CHECK_EQ(sense_link_win_lost(&ch.sl), 0u);
}

/* A clipped window and a negative current are readings: handed over as
 * the page carries them, signed, with the clipped samples counted. */
TEST_CASE(a_clipped_window_and_a_negative_current_are_plain_readings)
{
    running(50u, 1000u);
    ch.flat = true;
    sense_link_win_t w;
    memset(&w, 0, sizeof(w));

    ch.i3221->amps[0] = 2.0;                   /* past the 1.638 A range */
    chain_far(120u);
    cycles(2u, 50u);
    chain_cycle(50u);
    CHECK(sense_link_take_win(&ch.sl, &w));
    CHECK(w.current && w.voltage);
    CHECK(w.clip_hi);
    CHECK(!w.clip_lo);
    CHECK_EQ(w.clipped, 50u);
    CHECK_EQ(w.mean_ma, 1638);
    CHECK_EQ(w.max_ma, 1638);
    CHECK_EQ(w.min_ma, 1638);
    CHECK_EQ(w.mean_mv, 6000u);
    CHECK_EQ(w.min_mv, 6000u);
    /* Stamped with the panel's tick at its read, the poll's third. */
    CHECK_EQ(w.taken_ms, chain_now() - 50u + 3u);

    ch.i3221->amps[0] = -0.05;
    chain_far(120u);
    cycles(2u, 50u);
    chain_cycle(50u);
    CHECK(sense_link_take_win(&ch.sl, &w));
    CHECK(!w.clip_hi && !w.clip_lo);
    CHECK_EQ(w.clipped, 0u);
    CHECK_EQ(w.mean_ma, -50);
    CHECK_EQ(w.max_ma, -50);
    CHECK_EQ(w.min_ma, -50);

    ch.i3221->amps[0] = -2.0;
    chain_far(120u);
    cycles(2u, 50u);
    chain_cycle(50u);
    CHECK(sense_link_take_win(&ch.sl, &w));
    CHECK(w.clip_lo);
    CHECK(!w.clip_hi);
    CHECK_EQ(w.clipped, 50u);
    CHECK_EQ(w.min_ma, -1638);

    /* And into the log's fields signed. */
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    sense_link_win_bench(&ch.sl, &w, &b);
    CHECK(b.servo_new);
    CHECK_EQ(b.servo_window, w.number);
    CHECK_EQ(b.servo_ok, 0x01u);
    CHECK_EQ(b.servo_mean_ma[0], w.mean_ma);
    CHECK_EQ(b.servo_max_ma[0], w.max_ma);
    CHECK_EQ(b.servo_min_mv[0], 6000u);
    sense_link_win_bench(NULL, &w, &b);        /* nothing, no crash */
    sense_link_win_bench(&ch.sl, NULL, &b);
    sense_link_win_bench(&ch.sl, &w, NULL);
}

/* ------------------------------------------------- the older coprocessor */

/* A 4.10 coprocessor has no SERVO_WIN: nothing is sent there, the status
 * read ends at register 25, and the log has SERVO_SENSE's last window. */
TEST_CASE(nothing_is_sent_to_servo_win_on_a_4_10_coprocessor)
{
    chain_start(10u, 1000u, 0x01u);
    chain_far(400u);
    chain_link_up();
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    unsigned rows = 0u;
    for (unsigned i = 0u; i < 200u; ++i) {
        chain_cycle(50u);
        CHECK(!sense_link_win_on(&ch.sl));
        CHECK_EQ(take_all(), 0u);
        CHECK(sense_link_windows(&ch.sl) <= 1u);
        if (sense_link_take_window(&ch.sl, &b)) {
            CHECK_EQ(b.servo_mean_ma[0], chain_mean_ma(b.servo_window));
            ++rows;
        }
        CHECK(!sense_link_take_window(&ch.sl, &b));
    }
    CHECK_EQ(ch.to_win, 0u);
    CHECK_EQ(ch.status_n, SENSE_LINK_STATUS_COUNT_V48);
    CHECK(rows >= 198u);
    sense_link_meter_t m;
    sense_link_meter(&ch.sl, &m);
    CHECK(m.page);
    CHECK(m.wanted);
    CHECK(m.held);
    CHECK(m.status);
    CHECK(!m.resets_read);
    CHECK(!m.win);
    CHECK_EQ(sense_link_win_lost(&ch.sl), 0u);
}

/* One that names 4.11 and refuses the page is asked once per link, and the
 * rest of SENSE goes on being read. */
TEST_CASE(a_coprocessor_that_refuses_servo_win_is_asked_once)
{
    chain_start(LINK_PROTOCOL_MINOR, 1000u, 0x01u);
    ch.no_win_page = true;
    chain_far(400u);
    chain_link_up();
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    unsigned rows = 0u;
    for (unsigned i = 0u; i < 100u; ++i) {
        chain_cycle(50u);
        if (sense_link_take_window(&ch.sl, &b)) {
            ++rows;
        }
    }
    CHECK_EQ(ch.to_win, 1u);
    CHECK(rows >= 98u);
    CHECK(ch.sl.page);
    CHECK_EQ(sense_link_events(&ch.sl), 0u);
    /* The next link-up asks again. */
    sense_link_lost(&ch.sl);
    chain_link_up();
    cycles(10u, 50u);
    CHECK_EQ(ch.to_win, 2u);
}

/* ------------------------------------------------------- CH2 and CH3 */

/* The ring is CH1's.  CH2 and CH3 come from SERVO_SENSE's one window, on
 * the row whose number that read showed. */
TEST_CASE(ch2_and_ch3_ride_on_the_row_of_their_window)
{
    chain_start(LINK_PROTOCOL_MINOR, 1000u, 0x07u);
    chain_far(400u);
    chain_link_up();
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    unsigned rows = 0u;
    unsigned with = 0u;
    for (unsigned i = 0u; i < 200u; ++i) {
        chain_cycle(50u);
        while (sense_link_take_window(&ch.sl, &b)) {
            ++rows;
            CHECK((b.servo_ok & 0x01u) != 0u);
            CHECK_EQ(b.servo_mean_ma[0], chain_mean_ma(b.servo_window));
            if ((b.servo_ok & 0x06u) == 0x06u) {
                ++with;
                CHECK_EQ(b.servo_mean_ma[1], 200);
                CHECK_EQ(b.servo_max_ma[1], 200);
                CHECK_EQ(b.servo_min_mv[1], 6000u);
                CHECK_EQ(b.servo_mean_ma[2], 300);
                CHECK_EQ(b.servo_min_mv[2], 6000u);
            } else {
                CHECK_EQ(b.servo_ok, 0x01u);
                CHECK_EQ(b.servo_mean_ma[1], 0);
                CHECK_EQ(b.servo_max_ma[2], 0);
                CHECK_EQ(b.servo_min_mv[2], 0u);
            }
        }
    }
    CHECK(rows >= 198u);
    CHECK_EQ(with, rows);              /* every poll reads both pages */

    /* Without CH1 there is no ring to read: SERVO_SENSE alone. */
    chain_start(LINK_PROTOCOL_MINOR, 1000u, 0x06u);
    chain_far(400u);
    chain_link_up();
    rows = 0u;
    for (unsigned i = 0u; i < 50u; ++i) {
        chain_cycle(50u);
        CHECK(!sense_link_win_on(&ch.sl));
        if (sense_link_take_window(&ch.sl, &b)) {
            CHECK_EQ(b.servo_ok, 0x06u);
            CHECK_EQ(b.servo_mean_ma[1], 200);
            ++rows;
        }
    }
    CHECK_EQ(ch.to_win, 0u);
    CHECK(rows >= 48u);
    sense_link_meter_t m;
    sense_link_meter(&ch.sl, &m);
    CHECK(!m.wanted);
    CHECK(!m.held);
    CHECK(!m.win);
}

/* --------------------------------------------------------- the budget */

/* The CAN frames of a poll's SENSE, SERVO_SENSE and SERVO_WIN reads, a
 * request and one data frame for every four registers: docs/Link.md's
 * budget. */
TEST_CASE(a_poll_reads_16_frames_and_19_with_the_whole_page)
{
    CHECK_EQ(SENSE_LINK_WIN_HEAD, 16u);
    CHECK_EQ(SENSE_LINK_WIN_HEAD_WINDOWS, 2u);
    CHECK_EQ(SENSE_LINK_WIN_MS, 40u);
    CHECK_EQ(SENSE_LINK_WIN_ALL_MS, 100u);
    CHECK_EQ(SENSE_LINK_WIN_MINOR, 11u);
    running(53u, 1000u);
    unsigned most = 0u;
    for (unsigned i = 0u; i < 200u; ++i) {
        cycles(1u, 53u);
        /* SENSE 12 to 31, 6 frames; SERVO_SENSE 0 to 13, 5; SERVO_WIN 0
         * to 15, 5. */
        CHECK_EQ(ch.poll_frames, 16u);
        CHECK_EQ(ch.status_n, SENSE_LINK_STATUS_COUNT);
        if (ch.poll_frames > most) {
            most = ch.poll_frames;
        }
    }
    CHECK_EQ(most, 16u);
    /* After a lost reply SERVO_WIN 0 to 27 is 8 frames. */
    ch.lose_win = 1u;
    cycles(1u, 53u);
    cycles(1u, 53u);
    CHECK_EQ(ch.poll_frames, 19u);
    cycles(1u, 53u);
    CHECK_EQ(ch.poll_frames, 16u);
}

int main(void)
{
    RUN(no_window_is_lost_with_polls_up_to_200_ms_apart);
    RUN(windows_beyond_the_ring_are_counted_exactly);
    RUN(every_window_reaches_the_log_at_a_53_ms_poll);
    RUN(a_reply_lost_on_the_link_loses_no_window);
    RUN(a_window_that_closes_between_two_reads_keeps_its_number);
    RUN(the_window_number_runs_on_across_65535);
    RUN(the_reads_run_on_across_the_tick_wrap);
    RUN(a_setup_written_starts_the_windows_at_number_0);
    RUN(a_coprocessor_restart_starts_the_windows_again);
    RUN(a_number_the_coprocessor_skipped_is_no_window);
    RUN(a_link_lost_and_back_hands_no_window_over_twice);
    RUN(a_link_back_to_a_restarted_coprocessor_invents_nothing);
    RUN(windows_not_taken_wait_and_those_past_the_ring_are_counted);
    RUN(a_part_that_resets_itself_hands_over_empty_windows);
    RUN(a_clipped_window_and_a_negative_current_are_plain_readings);
    RUN(nothing_is_sent_to_servo_win_on_a_4_10_coprocessor);
    RUN(a_coprocessor_that_refuses_servo_win_is_asked_once);
    RUN(ch2_and_ch3_ride_on_the_row_of_their_window);
    RUN(a_poll_reads_16_frames_and_19_with_the_whole_page);
    return test_summary("sense_windows");
}
