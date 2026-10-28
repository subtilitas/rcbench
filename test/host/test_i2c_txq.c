/*
 * The I2C transaction queue's order and limits (protocols/i2c_txq), over
 * the modelled bus of i2c_txq_model.c.
 *
 * Under test:
 *  - the set-up and the refusals of a submit at every limit: address,
 *    lengths, class, a descriptor already queued, a full queue;
 *  - the deadline of a soft transaction at now - 1, now and now + 1, and
 *    the late start of a hard one at the same three;
 *  - the order: hard before soft, earliest deadline, submit order, the
 *    not-before time at now and now + 1;
 *  - the 64-bit clock and the 32-bit submit count across their wrap;
 *  - the abort of a queued transaction, the flush, the hold, the done
 *    callback that submits again;
 *  - the bus time arithmetic and the abort-cause table.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "i2c_txq.h"
#include "i2c_txq_model.h"

/* ------------------------------------------------------------- helpers */

static model_t m;
static i2c_txq_t q;
static i2c_txq_xfer_t *done_log[64];
static unsigned done_n;
static bool resubmit;

static void on_done(void *user, i2c_txq_xfer_t *x)
{
    (void)user;
    if (done_n < 64u) {
        done_log[done_n] = x;
    }
    done_n++;
    if (resubmit) {
        (void)i2c_txq_submit(&q, x);
    }
}

/* An empty queue over a bus with parts at 0x40 to 0x4F, at time @p now. */
static void setup(uint64_t now)
{
    i2c_txq_cfg_t c;

    model_init(&m, now);
    for (unsigned a = 0x40u; a <= 0x4Fu; ++a) {
        m.present[a] = true;
    }
    i2c_txq_cfg_default(&c);
    c.done = on_done;
    if (!i2c_txq_init(&q, &model_ops, &m, &c)) {
        T_FAIL("init");
    }
    done_n = 0;
    resubmit = false;
}

/* A register read of 2 bytes. */
static void rd2(i2c_txq_xfer_t *x, uint8_t addr, uint8_t cls,
                uint64_t not_before, uint64_t deadline)
{
    memset(x, 0, sizeof(*x));
    x->addr = addr;
    x->wr_len = 1;
    x->wr[0] = 0x01;
    x->rd_len = 2;
    x->cls = cls;
    x->not_before_us = not_before;
    x->deadline_us = deadline;
}

/* --------------------------------------------------------------- cases */

TEST_CASE(the_set_up_refuses_a_missing_part)
{
    i2c_txq_t z;
    i2c_txq_ops_t ops;
    i2c_txq_cfg_t c;

    model_init(&m, 0);
    CHECK(!i2c_txq_init(NULL, &model_ops, &m, NULL));
    CHECK(!i2c_txq_init(&z, NULL, &m, NULL));
#define WITHOUT(field)                                                        \
    do {                                                                      \
        ops = model_ops;                                                      \
        ops.field = NULL;                                                     \
        CHECK(!i2c_txq_init(&z, &ops, &m, NULL));                             \
    } while (0)
    WITHOUT(get);
    WITHOUT(start);
    WITHOUT(abort);
    WITHOUT(reset);
    WITHOUT(pullups);
    WITHOUT(gpio);
    WITHOUT(line);
#undef WITHOUT
    i2c_txq_cfg_default(&c);
    c.hz = 0;
    CHECK(!i2c_txq_init(&z, &model_ops, &m, &c));
    i2c_txq_cfg_default(NULL);

    CHECK(i2c_txq_init(&z, &model_ops, &m, NULL));
    CHECK_EQ(z.cfg.hz, 400000u);
    CHECK_EQ(z.cfg.timeout_us, 1000u);
    CHECK_EQ(z.cfg.settle_us, 10u);
    CHECK_EQ(z.cfg.probe_us, 50u);
    CHECK_EQ(z.cfg.clear_half_us, 5u);
    CHECK_EQ(z.cfg.clear_stretch_us, 100u);
    CHECK_EQ(z.cfg.late_us, 100u);
    CHECK(z.cfg.done == NULL);
    CHECK(i2c_txq_idle(&z));
    CHECK_EQ(i2c_txq_queued(&z), 0u);
    CHECK_EQ(i2c_txq_clear_result(&z), I2C_TXQ_OK);
    CHECK(!i2c_txq_clearing(&z));
}

TEST_CASE(a_submit_is_refused_at_every_limit)
{
    i2c_txq_xfer_t x;

    setup(1000);
    i2c_txq_hold(&q, true);
    rd2(&x, 0x40, I2C_TXQ_SOFT, 0, 2000);
    CHECK_EQ(i2c_txq_submit(NULL, &x), I2C_TXQ_E_ARG);
    CHECK_EQ(i2c_txq_submit(&q, NULL), I2C_TXQ_E_ARG);
    /* Addresses: 0x07 and 0x78 are reserved, 0x08 and 0x77 are not. */
    static const uint8_t bad[] = {0x00, 0x07, 0x78, 0x7F, 0x80, 0xFF};
    for (size_t i = 0; i < sizeof(bad); ++i) {
        x.addr = bad[i];
        CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_ARG);
        CHECK_EQ(x.state, I2C_TXQ_ST_IDLE);
    }
    x.addr = 0x08;
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    i2c_txq_xfer_t y;
    rd2(&y, 0x77, I2C_TXQ_SOFT, 0, 2000);
    CHECK_EQ(i2c_txq_submit(&q, &y), I2C_TXQ_E_OK);
    /* Lengths: 5 written and 6 read are the most, nothing at all is not
     * a transaction. */
    i2c_txq_xfer_t z;
    rd2(&z, 0x40, I2C_TXQ_SOFT, 0, 2000);
    z.wr_len = 6;
    CHECK_EQ(i2c_txq_submit(&q, &z), I2C_TXQ_E_ARG);
    z.wr_len = 5;
    z.rd_len = 7;
    CHECK_EQ(i2c_txq_submit(&q, &z), I2C_TXQ_E_ARG);
    z.wr_len = 0;
    z.rd_len = 0;
    CHECK_EQ(i2c_txq_submit(&q, &z), I2C_TXQ_E_ARG);
    z.wr_len = 5;
    z.rd_len = 6;
    z.cls = 2;
    CHECK_EQ(i2c_txq_submit(&q, &z), I2C_TXQ_E_ARG);
    z.cls = I2C_TXQ_HARD;
    CHECK_EQ(i2c_txq_submit(&q, &z), I2C_TXQ_E_OK);
    /* Queued once is enough. */
    CHECK_EQ(i2c_txq_submit(&q, &z), I2C_TXQ_E_BUSY);
    CHECK_EQ(i2c_txq_queued(&q), 3u);
    CHECK_EQ(q.st.submitted, 3u);
    /* Running too. */
    i2c_txq_hold(&q, false);
    i2c_txq_poll(&q, 1000);
    CHECK_EQ(z.state, I2C_TXQ_ST_RUNNING);
    CHECK_EQ(i2c_txq_submit(&q, &z), I2C_TXQ_E_BUSY);
    CHECK_EQ(q.st.submitted, 3u);
}

TEST_CASE(a_submit_again_clears_what_the_last_end_left)
{
    i2c_txq_xfer_t x;

    setup(1000);
    m.extra_us = 200;           /* reported late: a stamp flag */
    rd2(&x, 0x41, I2C_TXQ_HARD, 1000, 900);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    model_run_to(&m, &q, 1400);
    CHECK_EQ(x.result, I2C_TXQ_OK);
    CHECK(x.flags & I2C_TXQ_F_LATE_START);
    CHECK(x.flags & I2C_TXQ_F_LATE_STAMP);
    CHECK(x.start_us != 0u);
    CHECK(x.done_us != 0u);
    /* Ended with a NACK, then queued again: nothing of the end stays. */
    m.present[0x41] = false;
    m.extra_us = 0;
    x.not_before_us = m.now;
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    model_run_to(&m, &q, m.now + 100u);
    CHECK_EQ(x.result, I2C_TXQ_NACK_ADDR);
    i2c_txq_hold(&q, true);
    CHECK_EQ(i2c_txq_submit(&q, &x), I2C_TXQ_E_OK);
    CHECK_EQ(x.state, I2C_TXQ_ST_QUEUED);
    CHECK_EQ(x.result, I2C_TXQ_OK);
    CHECK_EQ(x.flags, 0u);
    CHECK(x.start_us == 0u);
    CHECK(x.done_us == 0u);
}

TEST_CASE(the_queue_holds_16_and_refuses_the_17th)
{
    i2c_txq_xfer_t x[I2C_TXQ_DEPTH + 2u];

    setup(1000);
    i2c_txq_hold(&q, true);
    for (unsigned i = 0; i < I2C_TXQ_DEPTH; ++i) {
        rd2(&x[i], 0x40, I2C_TXQ_SOFT, 0, 100000);
        CHECK_EQ(i2c_txq_submit(&q, &x[i]), I2C_TXQ_E_OK);
    }
    rd2(&x[16], 0x40, I2C_TXQ_SOFT, 0, 100000);
    CHECK_EQ(i2c_txq_submit(&q, &x[16]), I2C_TXQ_E_FULL);
    CHECK_EQ(x[16].state, I2C_TXQ_ST_IDLE);
    CHECK_EQ(q.st.max_queued, 16u);
    CHECK_EQ(q.st.submitted, 16u);
    /* One on the wire is not counted: the 17th goes in. */
    i2c_txq_hold(&q, false);
    i2c_txq_poll(&q, m.now);
    CHECK_EQ(m.starts, 1u);
    CHECK_EQ(i2c_txq_queued(&q), 15u);
    CHECK_EQ(i2c_txq_submit(&q, &x[16]), I2C_TXQ_E_OK);
    rd2(&x[17], 0x40, I2C_TXQ_SOFT, 0, 100000);
    CHECK_EQ(i2c_txq_submit(&q, &x[17]), I2C_TXQ_E_FULL);
    /* Every one runs, in turn. */
    model_run_to(&m, &q, m.now + 17u * 130u);
    CHECK_EQ(q.st.ok, 17u);
    CHECK_EQ(done_n, 17u);
    CHECK(i2c_txq_idle(&q));
    CHECK_EQ(i2c_txq_submit(&q, &x[17]), I2C_TXQ_E_OK);
}

TEST_CASE(a_soft_deadline_at_now_minus_1_drops_and_at_now_runs)
{
    i2c_txq_xfer_t a, b, c;
    const uint64_t now = 5000;

    setup(now);
    rd2(&a, 0x40, I2C_TXQ_SOFT, 0, now - 1u);
    CHECK_EQ(i2c_txq_submit(&q, &a), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, now);
    CHECK_EQ(a.state, I2C_TXQ_ST_DONE);
    CHECK_EQ(a.result, I2C_TXQ_DROPPED);
    CHECK_EQ(a.flags, 0u);
    CHECK(a.done_us == now);
    CHECK_EQ(q.st.dropped, 1u);
    CHECK_EQ(m.starts, 0u);

    rd2(&b, 0x40, I2C_TXQ_SOFT, 0, now);
    CHECK_EQ(i2c_txq_submit(&q, &b), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, now);
    CHECK_EQ(b.state, I2C_TXQ_ST_RUNNING);
    CHECK(b.start_us == now);
    CHECK_EQ(m.starts, 1u);
    model_run_to(&m, &q, now + 200u);
    CHECK_EQ(b.result, I2C_TXQ_OK);

    rd2(&c, 0x40, I2C_TXQ_SOFT, 0, m.now + 1u);
    CHECK_EQ(i2c_txq_submit(&q, &c), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, m.now);
    CHECK_EQ(c.state, I2C_TXQ_ST_RUNNING);
    CHECK_EQ(q.st.dropped, 1u);
}

TEST_CASE(a_queued_soft_one_drops_one_microsecond_after_its_deadline)
{
    i2c_txq_xfer_t busy, w;

    setup(1000);
    m.stall = true;                  /* the first one never ends */
    rd2(&busy, 0x40, I2C_TXQ_HARD, 0, 0);
    CHECK_EQ(i2c_txq_submit(&q, &busy), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, 1000);
    rd2(&w, 0x41, I2C_TXQ_SOFT, 0, 1300);
    CHECK_EQ(i2c_txq_submit(&q, &w), I2C_TXQ_E_OK);
    model_run_to(&m, &q, 1300);
    CHECK_EQ(w.state, I2C_TXQ_ST_QUEUED);
    model_tick(&m, &q);
    CHECK_EQ(w.result, I2C_TXQ_DROPPED);
    CHECK(w.done_us == 1301u);
}

TEST_CASE(a_hard_one_runs_late_and_says_so)
{
    i2c_txq_xfer_t a, b, c;
    const uint64_t now = 5000;

    setup(now);
    rd2(&a, 0x40, I2C_TXQ_HARD, 0, now - 1u);
    rd2(&b, 0x40, I2C_TXQ_HARD, 0, now);
    rd2(&c, 0x40, I2C_TXQ_HARD, 0, now + 1u);
    CHECK_EQ(i2c_txq_submit(&q, &c), I2C_TXQ_E_OK);
    CHECK_EQ(i2c_txq_submit(&q, &b), I2C_TXQ_E_OK);
    CHECK_EQ(i2c_txq_submit(&q, &a), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, now);
    CHECK_EQ(a.state, I2C_TXQ_ST_RUNNING);
    CHECK(a.flags & I2C_TXQ_F_LATE_START);
    CHECK_EQ(q.st.late_starts, 1u);
    model_run_to(&m, &q, now + 130u);
    CHECK_EQ(a.result, I2C_TXQ_OK);
    CHECK_EQ(b.state, I2C_TXQ_ST_RUNNING);
    model_run_to(&m, &q, now + 500u);
    CHECK_EQ(b.result, I2C_TXQ_OK);
    CHECK_EQ(c.result, I2C_TXQ_OK);
    /* b and c started after their deadline too: the bus was busy. */
    CHECK(b.flags & I2C_TXQ_F_LATE_START);
    CHECK(c.flags & I2C_TXQ_F_LATE_START);
    CHECK_EQ(q.st.late_starts, 3u);
    CHECK_EQ(q.st.dropped, 0u);

    /* At its deadline exactly a hard one is on time. */
    i2c_txq_xfer_t d;
    rd2(&d, 0x40, I2C_TXQ_HARD, 0, m.now);
    CHECK_EQ(i2c_txq_submit(&q, &d), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, m.now);
    CHECK_EQ(d.state, I2C_TXQ_ST_RUNNING);
    CHECK_EQ(d.flags & I2C_TXQ_F_LATE_START, 0u);
}

TEST_CASE(hard_goes_first_then_the_earliest_deadline_then_submit_order)
{
    i2c_txq_xfer_t x[6];

    setup(1000);
    i2c_txq_hold(&q, true);
    rd2(&x[0], 0x40, I2C_TXQ_SOFT, 0, 3000);
    rd2(&x[1], 0x41, I2C_TXQ_SOFT, 0, 2000);
    rd2(&x[2], 0x42, I2C_TXQ_HARD, 0, 9000);
    rd2(&x[3], 0x43, I2C_TXQ_SOFT, 0, 2000);
    rd2(&x[4], 0x44, I2C_TXQ_HARD, 0, 8000);
    rd2(&x[5], 0x45, I2C_TXQ_SOFT, 0, 2500);
    for (unsigned i = 0; i < 6u; ++i) {
        CHECK_EQ(i2c_txq_submit(&q, &x[i]), I2C_TXQ_E_OK);
    }
    i2c_txq_hold(&q, false);
    model_run_to(&m, &q, 1000u + 6u * 130u);
    static const uint8_t want[] = {4, 2, 1, 3, 5, 0};
    CHECK_EQ(done_n, 6u);
    for (unsigned i = 0; i < 6u; ++i) {
        CHECK(done_log[i] == &x[want[i]]);
    }
}

TEST_CASE(the_not_before_time_holds_back_until_it_comes)
{
    i2c_txq_xfer_t early, later;

    setup(1000);
    /* The earlier deadline is not ready yet: the other one goes. */
    rd2(&early, 0x40, I2C_TXQ_HARD, 1001, 1100);
    rd2(&later, 0x41, I2C_TXQ_SOFT, 0, 5000);
    CHECK_EQ(i2c_txq_submit(&q, &early), I2C_TXQ_E_OK);
    CHECK_EQ(i2c_txq_submit(&q, &later), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, 1000);
    CHECK_EQ(later.state, I2C_TXQ_ST_RUNNING);
    CHECK_EQ(early.state, I2C_TXQ_ST_QUEUED);
    model_run_to(&m, &q, 1300);
    CHECK_EQ(early.result, I2C_TXQ_OK);
    CHECK(early.start_us == later.done_us);

    /* Not before 2000: queued at 1999, started at 2000. */
    setup(1999);
    rd2(&early, 0x40, I2C_TXQ_SOFT, 2000, 2000);
    CHECK_EQ(i2c_txq_submit(&q, &early), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, 1999);
    CHECK_EQ(early.state, I2C_TXQ_ST_QUEUED);
    model_tick(&m, &q);
    CHECK_EQ(early.state, I2C_TXQ_ST_RUNNING);
    CHECK(early.start_us == 2000u);
}

TEST_CASE(the_order_and_the_deadlines_hold_across_the_64_bit_wrap)
{
    const uint64_t now = UINT64_MAX - 5u;
    i2c_txq_xfer_t past, before_wrap, after_wrap, not_yet;

    setup(now);
    i2c_txq_hold(&q, true);
    /* "At once" is the present time: 0 is 2^64 - now in the future. */
    rd2(&past, 0x40, I2C_TXQ_SOFT, now, now - 1u);
    rd2(&after_wrap, 0x41, I2C_TXQ_SOFT, now, 3000);
    rd2(&before_wrap, 0x42, I2C_TXQ_SOFT, now, UINT64_MAX - 1u);
    rd2(&not_yet, 0x43, I2C_TXQ_HARD, 2, 400);
    CHECK_EQ(i2c_txq_submit(&q, &past), I2C_TXQ_E_OK);
    CHECK_EQ(i2c_txq_submit(&q, &after_wrap), I2C_TXQ_E_OK);
    CHECK_EQ(i2c_txq_submit(&q, &before_wrap), I2C_TXQ_E_OK);
    CHECK_EQ(i2c_txq_submit(&q, &not_yet), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, now);
    /* Past is past, also when the deadline reads larger than 0. */
    CHECK_EQ(past.result, I2C_TXQ_DROPPED);
    CHECK_EQ(after_wrap.state, I2C_TXQ_ST_QUEUED);
    CHECK_EQ(before_wrap.state, I2C_TXQ_ST_QUEUED);
    i2c_txq_hold(&q, false);
    i2c_txq_poll(&q, now);
    /* The hard one waits for 2, after the wrap; the deadline before the
     * wrap goes first. */
    CHECK_EQ(before_wrap.state, I2C_TXQ_ST_RUNNING);
    CHECK_EQ(not_yet.state, I2C_TXQ_ST_QUEUED);
    model_run_to(&m, &q, 1000);
    CHECK_EQ(before_wrap.result, I2C_TXQ_OK);
    CHECK_EQ(not_yet.result, I2C_TXQ_OK);
    CHECK_EQ(after_wrap.result, I2C_TXQ_OK);
    CHECK(not_yet.start_us >= 2u && not_yet.start_us < 1000u);
    CHECK(done_log[1] == &before_wrap);
    CHECK(done_log[2] == &not_yet);
    CHECK(done_log[3] == &after_wrap);
    /* The bus time across the wrap: started at UINT64_MAX - 5, ended
     * 120us later on the far side. */
    CHECK(before_wrap.start_us == now);
    CHECK(before_wrap.done_us == now + 120u);
    CHECK_EQ(q.st.late_stamps, 0u);

    /* A not-before time of 0 seen from past 2^63 is far in the future. */
    i2c_txq_xfer_t zero;
    setup(UINT64_MAX - 5u);
    rd2(&zero, 0x40, I2C_TXQ_HARD, 0, 0);
    CHECK_EQ(i2c_txq_submit(&q, &zero), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, m.now);
    CHECK_EQ(zero.state, I2C_TXQ_ST_QUEUED);
    model_run_to(&m, &q, 0);
    CHECK_EQ(zero.state, I2C_TXQ_ST_RUNNING);
}

TEST_CASE(equal_deadlines_keep_submit_order_across_the_count_wrap)
{
    i2c_txq_xfer_t a, b;

    setup(1000);
    q.seq = UINT32_MAX;
    i2c_txq_hold(&q, true);
    rd2(&a, 0x40, I2C_TXQ_SOFT, 0, 3000);
    rd2(&b, 0x41, I2C_TXQ_SOFT, 0, 3000);
    CHECK_EQ(i2c_txq_submit(&q, &a), I2C_TXQ_E_OK);
    CHECK_EQ(i2c_txq_submit(&q, &b), I2C_TXQ_E_OK);
    CHECK_EQ(a.seq, UINT32_MAX);
    CHECK_EQ(b.seq, 0u);
    i2c_txq_hold(&q, false);
    model_run_to(&m, &q, 1300);
    CHECK(done_log[0] == &a);
    CHECK(done_log[1] == &b);
}

TEST_CASE(an_abort_takes_back_a_queued_one)
{
    i2c_txq_xfer_t a, b, stranger;

    setup(1000);
    i2c_txq_hold(&q, true);
    rd2(&a, 0x40, I2C_TXQ_SOFT, 0, 3000);
    rd2(&b, 0x41, I2C_TXQ_SOFT, 0, 3000);
    rd2(&stranger, 0x42, I2C_TXQ_SOFT, 0, 3000);
    CHECK_EQ(i2c_txq_submit(&q, &a), I2C_TXQ_E_OK);
    CHECK_EQ(i2c_txq_submit(&q, &b), I2C_TXQ_E_OK);
    CHECK(i2c_txq_abort(&q, &a, 1010));
    CHECK_EQ(a.result, I2C_TXQ_ABORTED);
    CHECK_EQ(a.state, I2C_TXQ_ST_DONE);
    CHECK(a.done_us == 1010u);
    CHECK_EQ(a.flags & I2C_TXQ_F_WIRE, 0u);
    CHECK_EQ(q.st.aborted, 1u);
    CHECK_EQ(i2c_txq_queued(&q), 1u);
    CHECK(!i2c_txq_abort(&q, &a, 1010));
    CHECK(!i2c_txq_abort(&q, &stranger, 1010));
    CHECK(!i2c_txq_abort(&q, NULL, 1010));
    CHECK(!i2c_txq_abort(NULL, &b, 1010));
    CHECK_EQ(m.aborts, 0u);
    i2c_txq_hold(&q, false);
    model_run_to(&m, &q, 1200);
    CHECK_EQ(b.result, I2C_TXQ_OK);
    CHECK_EQ(m.starts, 1u);
}

TEST_CASE(a_flush_ends_everything_aborted)
{
    i2c_txq_xfer_t x[4];

    setup(1000);
    for (unsigned i = 0; i < 4u; ++i) {
        rd2(&x[i], 0x40, I2C_TXQ_SOFT, 0, 9000);
        CHECK_EQ(i2c_txq_submit(&q, &x[i]), I2C_TXQ_E_OK);
    }
    i2c_txq_poll(&q, 1000);
    CHECK_EQ(x[0].state, I2C_TXQ_ST_RUNNING);
    /* The callback submits again: those stay. */
    resubmit = true;
    i2c_txq_flush(&q, 1005);
    resubmit = false;
    CHECK_EQ(m.resets, 1u);
    CHECK_EQ(done_n, 4u);
    for (unsigned i = 0; i < 4u; ++i) {
        CHECK_EQ(x[i].state, I2C_TXQ_ST_QUEUED);
    }
    CHECK_EQ(q.st.aborted, 4u);
    CHECK_EQ(i2c_txq_queued(&q), 4u);
    CHECK(i2c_txq_idle(&q));
    i2c_txq_flush(NULL, 0);
    i2c_txq_flush(&q, 1006);
    CHECK_EQ(i2c_txq_queued(&q), 0u);
    CHECK_EQ(q.st.aborted, 8u);
    /* A late report of the cut transaction is not one. */
    i2c_txq_complete(&q, 1100, 0, NULL, 0);
    CHECK_EQ(q.st.spurious, 1u);
}

TEST_CASE(a_held_queue_starts_nothing_and_still_drops)
{
    i2c_txq_xfer_t soft, hard;

    setup(1000);
    i2c_txq_hold(&q, true);
    i2c_txq_hold(NULL, true);
    rd2(&soft, 0x40, I2C_TXQ_SOFT, 0, 1500);
    rd2(&hard, 0x40, I2C_TXQ_HARD, 0, 1500);
    CHECK_EQ(i2c_txq_submit(&q, &soft), I2C_TXQ_E_OK);
    CHECK_EQ(i2c_txq_submit(&q, &hard), I2C_TXQ_E_OK);
    model_run_to(&m, &q, 2000);
    CHECK_EQ(m.starts, 0u);
    CHECK_EQ(soft.result, I2C_TXQ_DROPPED);
    CHECK_EQ(hard.state, I2C_TXQ_ST_QUEUED);
    i2c_txq_hold(&q, false);
    model_tick(&m, &q);
    CHECK_EQ(hard.state, I2C_TXQ_ST_RUNNING);
    CHECK(hard.flags & I2C_TXQ_F_LATE_START);
}

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

TEST_CASE(the_bus_time_follows_the_clock_count)
{
    i2c_txq_xfer_t x;

    memset(&x, 0, sizeof(x));
    /* A register read of 2: 9 * (3 + 2) + 3 clocks, 120us at 400kHz. */
    x.wr_len = 1;
    x.rd_len = 2;
    CHECK_EQ(i2c_txq_clocks(&x), 48u);
    CHECK_EQ(i2c_txq_bus_us(&x, 400000u), 120u);
    /* 6 bytes: 210us; 1 byte: 97.5us, rounded up. */
    x.rd_len = 6;
    CHECK_EQ(i2c_txq_bus_us(&x, 400000u), 210u);
    x.rd_len = 1;
    CHECK_EQ(i2c_txq_clocks(&x), 39u);
    CHECK_EQ(i2c_txq_bus_us(&x, 400000u), 98u);
    /* A write of a pointer and 2: 9 * (2 + 2) + 2 clocks, 95us. */
    x.wr_len = 3;
    x.rd_len = 0;
    CHECK_EQ(i2c_txq_clocks(&x), 38u);
    CHECK_EQ(i2c_txq_bus_us(&x, 400000u), 95u);
    /* A read with no pointer: the address and the byte, no RESTART. */
    x.wr_len = 0;
    x.rd_len = 1;
    CHECK_EQ(i2c_txq_clocks(&x), 20u);
    CHECK_EQ(i2c_txq_bus_us(&x, 400000u), 50u);
    /* The largest: 5 written, 6 read. */
    x.wr_len = 5;
    x.rd_len = 6;
    CHECK_EQ(i2c_txq_clocks(&x), 9u * 13u + 3u);
    CHECK_EQ(i2c_txq_bus_us(&x, 400000u), 300u);
    CHECK_EQ(i2c_txq_bus_us(&x, 100000u), 1200u);
    CHECK_EQ(i2c_txq_bus_us(&x, 1u), 120000000u);
    CHECK_EQ(i2c_txq_bus_us(&x, 0u), 0u);
    CHECK_EQ(i2c_txq_bus_us(NULL, 400000u), 0u);
    CHECK_EQ(i2c_txq_clocks(NULL), 0u);
}

TEST_CASE(the_abort_causes_map_to_one_result_each)
{
    CHECK_EQ(i2c_txq_abrt_result(0), I2C_TXQ_OK);
    CHECK_EQ(i2c_txq_abrt_result(I2C_TXQ_ABRT_ADDR_NOACK), I2C_TXQ_NACK_ADDR);
    CHECK_EQ(i2c_txq_abrt_result(I2C_TXQ_ABRT_DATA_NOACK), I2C_TXQ_NACK_DATA);
    CHECK_EQ(i2c_txq_abrt_result(I2C_TXQ_ABRT_ARB_LOST), I2C_TXQ_ARB_LOST);
    CHECK_EQ(i2c_txq_abrt_result(I2C_TXQ_ABRT_USER), I2C_TXQ_ABORTED);
    CHECK_EQ(i2c_txq_abrt_result(I2C_TXQ_ABRT_GCALL_NOACK), I2C_TXQ_FAULT);
    CHECK_EQ(i2c_txq_abrt_result(1u << 16), I2C_TXQ_ABORTED);
    CHECK_EQ(i2c_txq_abrt_result(1u << 15), I2C_TXQ_FAULT);
    /* The flush count above bit 16 is no cause. */
    CHECK_EQ(i2c_txq_abrt_result(0xFF800000u), I2C_TXQ_OK);
    CHECK_EQ(i2c_txq_abrt_result(1u << 17), I2C_TXQ_OK);
    /* The address before the data before the arbitration. */
    CHECK_EQ(i2c_txq_abrt_result(I2C_TXQ_ABRT_ADDR_NOACK
                                 | I2C_TXQ_ABRT_DATA_NOACK
                                 | I2C_TXQ_ABRT_ARB_LOST),
             I2C_TXQ_NACK_ADDR);
    CHECK_EQ(i2c_txq_abrt_result(I2C_TXQ_ABRT_DATA_NOACK
                                 | I2C_TXQ_ABRT_ARB_LOST
                                 | I2C_TXQ_ABRT_USER),
             I2C_TXQ_NACK_DATA);
    CHECK_EQ(i2c_txq_abrt_result(I2C_TXQ_ABRT_ARB_LOST | I2C_TXQ_ABRT_USER),
             I2C_TXQ_ARB_LOST);
    CHECK_STR_EQ(i2c_txq_res_name(I2C_TXQ_OK), "ok");
    CHECK_STR_EQ(i2c_txq_res_name(I2C_TXQ_NACK_ADDR), "nack-addr");
    CHECK_STR_EQ(i2c_txq_res_name(I2C_TXQ_FAULT), "fault");
    CHECK_STR_EQ(i2c_txq_res_name(I2C_TXQ_RESULTS), "?");
}

TEST_CASE(the_readers_take_null)
{
    CHECK(i2c_txq_idle(NULL));
    CHECK(!i2c_txq_clearing(NULL));
    CHECK_EQ(i2c_txq_clear_result(NULL), I2C_TXQ_OK);
    CHECK_EQ(i2c_txq_queued(NULL), 0u);
    CHECK(i2c_txq_stats(NULL) == NULL);
    CHECK(!i2c_txq_recover(NULL, 0));
    i2c_txq_poll(NULL, 0);
    i2c_txq_complete(NULL, 0, 0, NULL, 0);
    setup(0);
    CHECK(i2c_txq_stats(&q) == &q.st);
}

int main(void)
{
    RUN(the_set_up_refuses_a_missing_part);
    RUN(a_submit_is_refused_at_every_limit);
    RUN(a_submit_again_clears_what_the_last_end_left);
    RUN(the_queue_holds_16_and_refuses_the_17th);
    RUN(a_soft_deadline_at_now_minus_1_drops_and_at_now_runs);
    RUN(a_queued_soft_one_drops_one_microsecond_after_its_deadline);
    RUN(a_hard_one_runs_late_and_says_so);
    RUN(hard_goes_first_then_the_earliest_deadline_then_submit_order);
    RUN(the_not_before_time_holds_back_until_it_comes);
    RUN(the_order_and_the_deadlines_hold_across_the_64_bit_wrap);
    RUN(equal_deadlines_keep_submit_order_across_the_count_wrap);
    RUN(an_abort_takes_back_a_queued_one);
    RUN(a_flush_ends_everything_aborted);
    RUN(a_held_queue_starts_nothing_and_still_drops);
    RUN(a_done_callback_submits_again_without_a_loop);
    RUN(the_bus_time_follows_the_clock_count);
    RUN(the_abort_causes_map_to_one_result_each);
    RUN(the_readers_take_null);
    return test_summary("i2c_txq");
}
