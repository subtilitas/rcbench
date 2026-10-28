/*
 * The I2C transaction queue's calls into and out of its done callback
 * (protocols/i2c_txq), over the modelled bus of i2c_txq_model.c.
 *
 * Under test:
 *  - a callback that submits again, and one that submits and polls, 1000
 *    times in a row, on the poll and the interrupt path, at a depth of 1;
 *  - a callback poll inside a bus clear and after an abort;
 *  - the shut: submits from its own callbacks refused, a new init;
 *  - the hold that comes while a picked transaction settles;
 *  - each of the 13 points that run a callback, with a callback that shuts
 *    the queue (no driver call after it, the queue idle and shut) and with
 *    one that submits and polls (the transaction runs);
 *  - a clear started by the callback of a clear's cut.
 *
 * SPDX-License-Identifier: MIT
 */
#include "i2c_txq_rig.h"

/* --------------------------------------------------------------- cases */

TEST_CASE(a_done_callback_submits_again_without_a_loop)
{
    i2c_txq_xfer_t x;

    setup(1000);
    /* Dropped, submitted again by the callback with the same deadline,
     * dropped again at the next poll and not in the same one. */
    rd2(&x, 0x40, I2C_TXQ_SOFT, 0, 900);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    resubmit = true;
    i2c_txq_poll(&q, 1000);
    CHECK_EQ(done_n, 1u);
    CHECK_EQ(x.state, I2C_TXQ_ST_QUEUED);
    i2c_txq_poll(&q, 1001);
    CHECK_EQ(done_n, 2u);
    resubmit = false;
    i2c_txq_poll(&q, 1002);
    CHECK_EQ(done_n, 3u);
    CHECK_EQ(x.state, I2C_TXQ_ST_DONE);
    CHECK_EQ(q.st.dropped, 3u);
    CHECK_EQ(q.st.submitted, 3u);
    /* A periodic read: each end submits the next. */
    setup(1000);
    x.deadline_us = 100000;
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    resubmit = true;
    model_run_to(&m, &q, 1001u + 10u * 120u);
    resubmit = false;
    CHECK_EQ(q.st.ok, 10u);
}

TEST_CASE(a_callback_that_submits_and_polls_1000_times_never_nests)
{
    i2c_txq_xfer_t x;
    const unsigned per_call = 1u + I2C_TXQ_REPOLLS;

    /* The poll path: dropped, submitted again with the deadline passed and
     * polled from the callback, 1000 times.  An outer poll ends at most
     * 1 + I2C_TXQ_REPOLLS of them, never nested. */
    setup(5000);
    rd2(&x, 0x40, I2C_TXQ_SOFT, 5000, 4999);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    repoll_left = 1000;
    repoll_deadline = 4999;
    i2c_txq_poll(&q, 5000);
    CHECK_EQ(done_n, per_call);
    CHECK_EQ(x.state, I2C_TXQ_ST_QUEUED);
    unsigned polls = 1;
    while (x.state != I2C_TXQ_ST_DONE && polls < 2000u) {
        i2c_txq_poll(&q, 5000);
        polls++;
    }
    CHECK_EQ(cb_max, 1u);
    CHECK_EQ(done_n, 1001u);
    CHECK_EQ(q.st.dropped, 1001u);
    CHECK_EQ(polls, (1001u + per_call - 1u) / per_call);
    CHECK_EQ(repoll_left, 0u);
    CHECK_EQ(q.depth, 0u);
    CHECK(!q.repoll);

    /* The interrupt path: the end of a transaction on the wire. */
    setup(5000);
    rd2(&x, 0x40, I2C_TXQ_SOFT, 5000, 9000);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, 5000);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    repoll_left = 1000;
    repoll_deadline = 4999;
    m.now = 5120;
    model_report(&m, &q);
    /* The end, then I2C_TXQ_REPOLLS drops; the last callback queued it. */
    CHECK_EQ(x.state, I2C_TXQ_ST_QUEUED);
    CHECK_EQ(q.st.ok, 1u);
    CHECK_EQ(cb_max, 1u);
    CHECK_EQ(done_n, per_call);
    while (x.state != I2C_TXQ_ST_DONE && done_n < 2000u) {
        i2c_txq_poll(&q, 5120);
    }
    CHECK_EQ(cb_max, 1u);
    CHECK_EQ(done_n, 1001u);
    CHECK_EQ(q.st.dropped, 1000u);
    CHECK_EQ(m.starts, 1u);

    /* A fresh one submitted from the callback of an end starts in that
     * same call, after the callback has returned. */
    setup(5000);
    rd2(&x, 0x40, I2C_TXQ_SOFT, 5000, 9000);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, 5000);
    repoll_left = 1;
    repoll_deadline = 9000;
    m.now = 5120;
    model_report(&m, &q);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    CHECK(x.start_us == 5120u);
    CHECK_EQ(m.starts, 2u);
    CHECK_EQ(cb_max, 1u);
}

TEST_CASE(a_callback_poll_starts_nothing_in_a_clear_and_once_after_an_abort)
{
    i2c_txq_xfer_t x;

    /* The clear cuts the one on the wire; its callback submits again and
     * polls: nothing starts until the clear is over. */
    setup(5000);
    rd2(&x, 0x40, I2C_TXQ_SOFT, 5000, 90000);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, 5000);
    repoll_left = 1;
    repoll_deadline = 90000;
    CHECK(i2c_txq_recover(&q, 5000));
    CHECK(i2c_txq_clearing(&q));
    CHECK_EQ(x.state, I2C_TXQ_ST_QUEUED);
    CHECK_EQ(m.starts, 1u);
    model_run_to(&m, &q, 5115);
    CHECK(!i2c_txq_clearing(&q));
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    CHECK(x.flags & I2C_TXQ_F_TRIAL);
    CHECK_EQ(m.starts, 2u);

    /* An abort of a queued one: the callback's poll runs once the abort is
     * done, not inside it. */
    setup(5000);
    rd2(&x, 0x40, I2C_TXQ_SOFT, 5000, 90000);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    repoll_left = 1;
    repoll_deadline = 90000;
    CHECK(i2c_txq_abort(&q, &x, 5000));
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    CHECK_EQ(m.starts, 1u);
    CHECK_EQ(done_n, 1u);
    CHECK_EQ(cb_max, 1u);
}

TEST_CASE(a_shut_queue_refuses_a_submit_from_its_own_callbacks)
{
    i2c_txq_xfer_t x[3];

    setup(5000);
    for (unsigned i = 0; i < 3u; ++i) {
        rd2(&x[i], 0x40, I2C_TXQ_SOFT, 5000, 90000);
        CHECK_EQ(i2c_txq_submit(&q, &x[i]), I2C_TXQ_E_OK);
    }
    i2c_txq_poll(&q, 5000);
    CHECK_EQ(x[0].state, I2C_TXQ_ST_RUNNING);
    resubmit = true;            /* each callback submits again */
    repoll_left = 3;            /* and polls */
    repoll_deadline = 90000;
    CHECK(!i2c_txq_closed(&q));
    i2c_txq_shut(&q, 5010);
    CHECK(i2c_txq_closed(&q));
    CHECK_EQ(done_n, 3u);
    for (unsigned i = 0; i < 3u; ++i) {
        CHECK_EQ(x[i].state, I2C_TXQ_ST_DONE);
        CHECK_EQ(x[i].result, I2C_TXQ_ABORTED);
    }
    CHECK_EQ(i2c_txq_queued(&q), 0u);
    CHECK(i2c_txq_idle(&q));
    CHECK_EQ(m.resets, 1u);
    CHECK_EQ(m.starts, 1u);
    resubmit = false;
    repoll_left = 0;
    /* Shut: no submit, no poll, no clear. */
    CHECK_EQ(i2c_txq_submit(&q, &x[0]), I2C_TXQ_E_CLOSED);
    CHECK_EQ(x[0].state, I2C_TXQ_ST_DONE);
    i2c_txq_poll(&q, 5020);
    CHECK(!i2c_txq_recover(&q, 5020));
    CHECK_EQ(m.gpio_ons, 0u);
    /* An argument error is still told first. */
    x[1].addr = 0x00;
    CHECK_EQ(i2c_txq_submit(&q, &x[1]), I2C_TXQ_E_ARG);
    x[1].addr = 0x40;
    /* Set up again: every descriptor takes a submit. */
    CHECK(i2c_txq_init(&q, &model_ops, &m, NULL));
    CHECK(!i2c_txq_closed(&q));
    for (unsigned i = 0; i < 3u; ++i) {
        CHECK_EQ(i2c_txq_submit(&q, &x[i]), I2C_TXQ_E_OK);
    }
    CHECK(i2c_txq_closed(NULL));
    i2c_txq_shut(NULL, 0);
}

TEST_CASE(a_hold_puts_a_settled_transaction_back_in_its_place)
{
    i2c_txq_xfer_t x, hard, soft;

    /* Held and let go before the line rises: it starts when it rises. */
    setup(1000);
    m.sda_hold_us = 1005;
    rd2(&x, 0x40, I2C_TXQ_SOFT, 1000, 9000);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, 1000);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    model_run_to(&m, &q, 1002);
    i2c_txq_hold(&q, true);
    model_run_to(&m, &q, 1003);
    i2c_txq_hold(&q, false);
    model_run_to(&m, &q, 1005);
    CHECK_EQ(m.starts, 1u);
    CHECK(x.start_us == 1005u);

    /* Held when the line rises: back to the queue, nothing on the wire,
     * started at the first poll after the release. */
    setup(1000);
    m.sda_hold_us = 1005;
    rd2(&x, 0x40, I2C_TXQ_SOFT, 1000, 9000);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, 1000);
    model_run_to(&m, &q, 1002);
    i2c_txq_hold(&q, true);
    model_run_to(&m, &q, 1005);
    CHECK_EQ(m.starts, 0u);
    CHECK_EQ(x.state, I2C_TXQ_ST_QUEUED);
    CHECK_EQ(i2c_txq_queued(&q), 1u);
    CHECK(i2c_txq_idle(&q));
    model_run_to(&m, &q, 1050);
    CHECK_EQ(m.starts, 0u);
    /* A hard one submitted while held goes first after the release; the
     * soft one keeps its place before a later soft one of its deadline. */
    rd2(&hard, 0x41, I2C_TXQ_HARD, 1050, 9500);
    rd2(&soft, 0x42, I2C_TXQ_SOFT, 1050, 9000);
    CHECK_EQ(i2c_txq_submit(&q, &hard), I2C_TXQ_E_OK);
    CHECK_EQ(i2c_txq_submit(&q, &soft), I2C_TXQ_E_OK);
    i2c_txq_hold(&q, false);
    model_run_to(&m, &q, 1500);
    CHECK_EQ(done_n, 3u);
    CHECK(done_log[0] == &hard);
    CHECK(done_log[1] == &x);
    CHECK(done_log[2] == &soft);
    CHECK_EQ(x.result, I2C_TXQ_OK);

    /* A soft one whose deadline passes while held is dropped, held. */
    setup(1000);
    m.sda_hold_us = 1005;
    rd2(&x, 0x40, I2C_TXQ_SOFT, 1000, 1010);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, 1000);
    i2c_txq_hold(&q, true);
    model_run_to(&m, &q, 1010);
    CHECK_EQ(x.state, I2C_TXQ_ST_QUEUED);
    model_tick(&m, &q);
    CHECK_EQ(x.result, I2C_TXQ_DROPPED);
    CHECK(x.done_us == 1011u);
    CHECK_EQ(m.starts, 0u);

    /* A trial put back keeps the trial for the next pick, the flag goes. */
    setup(1000);
    CHECK(i2c_txq_recover(&q, 1000));
    model_run_to(&m, &q, 1115);
    m.sda_hold_us = 1120;
    rd2(&x, 0x40, I2C_TXQ_SOFT, 1115, 9000);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, 1115);
    CHECK(x.flags & I2C_TXQ_F_TRIAL);
    i2c_txq_hold(&q, true);
    model_run_to(&m, &q, 1120);
    CHECK_EQ(x.state, I2C_TXQ_ST_QUEUED);
    CHECK_EQ(x.flags & I2C_TXQ_F_TRIAL, 0u);
    i2c_txq_hold(&q, false);
    model_tick(&m, &q);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    CHECK(x.flags & I2C_TXQ_F_TRIAL);

    /* 16 queued and one picked: the picked one still fits back. */
    i2c_txq_xfer_t many[I2C_TXQ_DEPTH];
    setup(1000);
    m.sda_hold_us = 1005;
    rd2(&x, 0x40, I2C_TXQ_SOFT, 1000, 9000);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, 1000);
    for (unsigned i = 0; i < I2C_TXQ_DEPTH; ++i) {
        rd2(&many[i], 0x41, I2C_TXQ_SOFT, 1000, 9000);
        CHECK_EQ(i2c_txq_submit(&q, &many[i]), I2C_TXQ_E_OK);
    }
    i2c_txq_hold(&q, true);
    model_run_to(&m, &q, 1005);
    CHECK_EQ(i2c_txq_queued(&q), I2C_TXQ_DEPTH + 1u);
    CHECK_EQ(i2c_txq_submit(&q, &soft), I2C_TXQ_E_FULL);
    i2c_txq_hold(&q, false);
    model_run_to(&m, &q, 1000u + 18u * 130u);
    CHECK_EQ(q.st.ok, 17u);
    CHECK(done_log[0] == &x);
}

/* The places a done callback runs, each driven to its first callback. */
enum {
    AT_ABORT_QUEUED = 0, /* i2c_txq_abort() of a queued one               */
    AT_ABORT_SETTLE,     /* i2c_txq_abort() in the line check             */
    AT_COMPLETE,         /* i2c_txq_complete()                            */
    AT_TIMEOUT,          /* the poll's timeout                            */
    AT_NO_PULLUP,        /* the poll's line check: the probe rose         */
    AT_BUS_LOW,          /* the poll's line check: the probe ran out      */
    AT_SETTLE_DROP,      /* the poll's line check: deadline in the settle */
    AT_DROP_LATE,        /* the poll's drop of a queued one               */
    AT_DROP_IN_CLEAR,    /* the same while a bus clear runs               */
    AT_FLUSH,            /* i2c_txq_flush(): the cut and the queued ones  */
    AT_SHUT,             /* i2c_txq_shut()                                */
    AT_RECOVER_WIRE,     /* i2c_txq_recover() cutting one on the wire     */
    AT_RECOVER_SETTLE,   /* i2c_txq_recover() cutting one in the settle   */
    AT_SITES
};

/* Set up site @p at, arm the callback with @p mode, and make the call that
 * runs the first callback. */
static void drive_site(int at, int mode)
{
    static i2c_txq_xfer_t x, y;

    setup(1000);
    rd2(&x, 0x40, I2C_TXQ_SOFT, 1000, 90000);
    rd2(&y, 0x41, I2C_TXQ_SOFT, 1000, 90000);
    switch (at) {
    case AT_ABORT_QUEUED:
        i2c_txq_hold(&q, true);
        (void)i2c_txq_submit(&q, &x);
        i2c_txq_hold(&q, false);
        site_mode = mode;
        (void)i2c_txq_abort(&q, &x, 1000);
        break;
    case AT_ABORT_SETTLE:
        m.sda_hold_us = 1005;
        (void)i2c_txq_submit(&q, &x);
        i2c_txq_poll(&q, 1000);
        m.now = 1006;
        site_mode = mode;
        (void)i2c_txq_abort(&q, &x, 1006);
        break;
    case AT_COMPLETE:
        (void)i2c_txq_submit(&q, &x);
        i2c_txq_poll(&q, 1000);
        m.now = 1120;
        site_mode = mode;
        model_report(&m, &q);
        break;
    case AT_TIMEOUT:
        m.stall = true;
        (void)i2c_txq_submit(&q, &x);
        i2c_txq_poll(&q, 1000);
        m.stall = false;
        m.pending = false;
        m.now = 2000;
        site_mode = mode;
        i2c_txq_poll(&q, 2000);
        break;
    case AT_NO_PULLUP:
        m.pullups = false;
        (void)i2c_txq_submit(&q, &x);
        i2c_txq_poll(&q, 1000);
        i2c_txq_poll(&q, 1010);
        m.pullups = true;
        m.now = 1011;
        site_mode = mode;
        i2c_txq_poll(&q, 1011);
        break;
    case AT_BUS_LOW:
        m.sda_hold = -1;
        (void)i2c_txq_submit(&q, &x);
        i2c_txq_poll(&q, 1000);
        i2c_txq_poll(&q, 1010);
        m.now = 1060;
        site_mode = mode;
        i2c_txq_poll(&q, 1060);
        m.sda_hold = 0;
        break;
    case AT_SETTLE_DROP:
        m.sda_hold_us = 1005;
        x.deadline_us = 1002;
        (void)i2c_txq_submit(&q, &x);
        i2c_txq_poll(&q, 1000);
        m.now = 1005;
        site_mode = mode;
        i2c_txq_poll(&q, 1005);
        break;
    case AT_DROP_LATE:
        x.deadline_us = 999;
        (void)i2c_txq_submit(&q, &x);
        site_mode = mode;
        i2c_txq_poll(&q, 1000);
        break;
    case AT_DROP_IN_CLEAR:
        (void)i2c_txq_recover(&q, 1000);
        x.deadline_us = 1003;
        (void)i2c_txq_submit(&q, &x);
        m.now = 1004;
        site_mode = mode;
        i2c_txq_poll(&q, 1004);
        break;
    case AT_FLUSH:
    case AT_SHUT:
        (void)i2c_txq_submit(&q, &x);
        (void)i2c_txq_submit(&q, &y);
        i2c_txq_poll(&q, 1000);
        m.now = 1005;
        site_mode = mode;
        if (at == AT_FLUSH) {
            i2c_txq_flush(&q, 1005);
        } else {
            i2c_txq_shut(&q, 1005);
        }
        break;
    case AT_RECOVER_WIRE:
        (void)i2c_txq_submit(&q, &x);
        i2c_txq_poll(&q, 1000);
        m.now = 1005;
        site_mode = mode;
        (void)i2c_txq_recover(&q, 1005);
        break;
    default: /* AT_RECOVER_SETTLE */
        m.sda_hold_us = 1008;
        (void)i2c_txq_submit(&q, &x);
        i2c_txq_poll(&q, 1000);
        m.now = 1005;
        site_mode = mode;
        (void)i2c_txq_recover(&q, 1005);
        break;
    }
    if (site_mode != SITE_OFF) {
        T_FAIL("site %d ran no callback", at);
    }
}

TEST_CASE(a_callback_that_shuts_the_queue_leaves_the_driver_alone_at_every_site)
{
    for (int at = 0; at < AT_SITES; ++at) {
        drive_site(at, SITE_SHUT);
        /* No driver call after the shut, the queue idle and shut. */
        if (m.ops != ops_at_shut) {
            T_FAIL("site %d: %u driver calls after the shut", at,
                   m.ops - ops_at_shut);
        }
        if (!i2c_txq_closed(&q) || !i2c_txq_idle(&q) || i2c_txq_clearing(&q)
            || i2c_txq_queued(&q) != 0u || q.run != NULL || q.depth != 0u) {
            T_FAIL("site %d: closed %d idle %d clearing %d queued %u", at,
                   i2c_txq_closed(&q), i2c_txq_idle(&q), i2c_txq_clearing(&q),
                   (unsigned)i2c_txq_queued(&q));
        }
        /* And none later. */
        model_run_to(&m, &q, m.now + 2000u);
        CHECK(!i2c_txq_recover(&q, m.now));
        if (m.ops != ops_at_shut) {
            T_FAIL("site %d: driver calls after the shut, later", at);
        }
        /* The shut ends the other queued one of a flush or a shut inside
         * the first callback: its callback runs one deeper, nowhere else. */
        const unsigned want = (at == AT_FLUSH || at == AT_SHUT) ? 2u : 1u;
        if (cb_max != want) {
            T_FAIL("site %d: callback depth %u", at, cb_max);
        }
    }
}

TEST_CASE(a_callback_that_submits_and_polls_works_at_every_site)
{
    for (int at = 0; at < AT_SITES; ++at) {
        drive_site(at, SITE_SUBMIT);
        if (at == AT_SHUT) {
            /* A shut queue takes nothing, by design. */
            CHECK_EQ(again_err, I2C_TXQ_E_CLOSED);
            CHECK_EQ(again.state, I2C_TXQ_ST_IDLE);
            continue;
        }
        if (again_err != I2C_TXQ_E_OK) {
            T_FAIL("site %d: submit %d", at, again_err);
        }
        model_run_to(&m, &q, m.now + 3000u);
        /* Picked and ended on the bus as the bus is; never left queued. */
        if (again.state != I2C_TXQ_ST_DONE || again.result == I2C_TXQ_DROPPED
            || again.result == I2C_TXQ_ABORTED) {
            T_FAIL("site %d: state %d result %d", at, again.state,
                   again.result);
        }
        if (at != AT_BUS_LOW && again.result != I2C_TXQ_OK) {
            T_FAIL("site %d: result %d", at, again.result);
        }
        CHECK_EQ(cb_max, 1u);
        CHECK(!i2c_txq_closed(&q));
    }
}

TEST_CASE(a_clear_started_by_the_callback_of_a_clears_cut_runs_once)
{
    drive_site(AT_RECOVER_WIRE, SITE_RECOVER);
    CHECK(i2c_txq_clearing(&q));
    CHECK_EQ(m.gpio_ons, 1u);
    model_run_to(&m, &q, m.now + 200u);
    CHECK(!i2c_txq_clearing(&q));
    CHECK_EQ(m.gpio_ons, 1u);
    CHECK_EQ(m.gpio_offs, 1u);
    CHECK_EQ(m.scl_rises, 10u);
    CHECK_EQ(q.st.clears, 1u);
}

int main(void)
{
    RUN(a_done_callback_submits_again_without_a_loop);
    RUN(a_callback_that_submits_and_polls_1000_times_never_nests);
    RUN(a_callback_poll_starts_nothing_in_a_clear_and_once_after_an_abort);
    RUN(a_shut_queue_refuses_a_submit_from_its_own_callbacks);
    RUN(a_hold_puts_a_settled_transaction_back_in_its_place);
    RUN(a_callback_that_shuts_the_queue_leaves_the_driver_alone_at_every_site);
    RUN(a_callback_that_submits_and_polls_works_at_every_site);
    RUN(a_clear_started_by_the_callback_of_a_clears_cut_runs_once);
    return test_summary("i2c_txq_calls");
}
