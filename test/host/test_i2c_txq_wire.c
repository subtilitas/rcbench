/*
 * The I2C transaction queue on a wire (protocols/i2c_txq), over the
 * modelled bus of i2c_txq_model.c, on a clock advanced 1us at a time.
 *
 * Under test:
 *  - a read: its bytes, its stamp, the stamp of a late report at the
 *    late limit and 1us past it;
 *  - a NACK on the address against one on a data byte, a lost
 *    arbitration, a short read;
 *  - a line low before the start: rising inside the settle time, at its
 *    end, on the pad pull-ups, held by a part to the end of the probe;
 *  - the abort mid-transaction, an abort that loses the race to the end,
 *    an abort that never ends;
 *  - the timeout at 999us and 1000us, also across the 64-bit wrap, and a
 *    report after it;
 *  - the bus clear: its clocks and their times, a part released by it, a
 *    part that holds on, SCL stretched; nothing started during it; the
 *    trial after it, kept by an abort and spent by any other end.
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

/* An empty queue over a bus with a part at 0x40, at time @p now. */
static void setup(uint64_t now)
{
    model_init(&m, now);
    m.present[0x40] = true;
    if (!i2c_txq_init(&q, &model_ops, &m, NULL)) {
        T_FAIL("init");
    }
}

/* A register read of @p n bytes from @p addr, soft, due in 10ms. */
static void rd(i2c_txq_xfer_t *x, uint8_t addr, uint8_t n)
{
    memset(x, 0, sizeof(*x));
    x->addr = addr;
    x->wr_len = 1;
    x->wr[0] = 0x02;
    x->rd_len = n;
    x->cls = I2C_TXQ_SOFT;
    x->not_before_us = m.now;
    x->deadline_us = m.now + 10000u;
}

/* A write of a pointer and 2 bytes to @p addr, soft, due in 10ms. */
static void wr3(i2c_txq_xfer_t *x, uint8_t addr)
{
    memset(x, 0, sizeof(*x));
    x->addr = addr;
    x->wr_len = 3;
    x->wr[0] = 0x00;
    x->wr[1] = 0x71;
    x->wr[2] = 0x27;
    x->cls = I2C_TXQ_SOFT;
    x->not_before_us = m.now;
    x->deadline_us = m.now + 10000u;
}

/* Submit @p x and poll once at the present time. */
static void go(i2c_txq_xfer_t *x)
{
    CHECK_EQ(i2c_txq_submit(&q, x), I2C_TXQ_E_OK);
    i2c_txq_poll(&q, m.now);
}

/* --------------------------------------------------------------- cases */

TEST_CASE(a_read_brings_its_bytes_and_its_stamp)
{
    i2c_txq_xfer_t x;

    setup(1000);
    rd(&x, 0x40, 2);
    go(&x);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    CHECK_EQ(m.starts, 1u);
    CHECK_EQ(m.last.addr, 0x40);
    CHECK_EQ(m.last.wr[0], 0x02);
    CHECK_EQ(m.last.rd_len, 2);
    CHECK(!i2c_txq_idle(&q));
    model_run_to(&m, &q, 1119);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    model_tick(&m, &q);
    CHECK_EQ(x.state, I2C_TXQ_ST_DONE);
    CHECK_EQ(x.result, I2C_TXQ_OK);
    CHECK_EQ(x.rd[0], 0x51);
    CHECK_EQ(x.rd[1], 0x62);
    CHECK(x.start_us == 1000u);
    CHECK(x.done_us == 1120u);
    CHECK_EQ(x.flags, I2C_TXQ_F_WIRE);
    CHECK_EQ(q.st.ok, 1u);
    CHECK(q.st.busy_us == 120u);
    CHECK(i2c_txq_idle(&q));
}

TEST_CASE(a_late_report_takes_the_computed_end_past_100us)
{
    i2c_txq_xfer_t x;

    /* 100us late: the stamp is the report's. */
    setup(1000);
    m.extra_us = 100;
    rd(&x, 0x40, 2);
    go(&x);
    model_run_to(&m, &q, 1220);
    CHECK_EQ(x.result, I2C_TXQ_OK);
    CHECK(x.done_us == 1220u);
    CHECK_EQ(x.flags & I2C_TXQ_F_LATE_STAMP, 0u);
    /* 101us late: the computed end, flagged. */
    setup(1000);
    m.extra_us = 101;
    rd(&x, 0x40, 2);
    go(&x);
    model_run_to(&m, &q, 1221);
    CHECK_EQ(x.result, I2C_TXQ_OK);
    CHECK(x.done_us == 1120u);
    CHECK(x.flags & I2C_TXQ_F_LATE_STAMP);
    CHECK_EQ(q.st.late_stamps, 1u);
    /* A failed one keeps the report's time. */
    setup(1000);
    m.extra_us = 500;
    rd(&x, 0x41, 2);
    go(&x);
    model_run_to(&m, &q, 1523);
    CHECK_EQ(x.result, I2C_TXQ_NACK_ADDR);
    CHECK(x.done_us == 1523u);
    CHECK_EQ(q.st.late_stamps, 0u);
}

TEST_CASE(a_nack_on_the_address_is_told_from_one_on_data)
{
    i2c_txq_xfer_t x;

    setup(1000);
    rd(&x, 0x41, 2);
    go(&x);
    model_run_to(&m, &q, 1100);
    CHECK_EQ(x.result, I2C_TXQ_NACK_ADDR);
    CHECK_EQ(x.flags, I2C_TXQ_F_WIRE);

    /* The pointer taken, the first data byte not. */
    m.nack_byte = 1;
    wr3(&x, 0x40);
    go(&x);
    model_run_to(&m, &q, m.now + 100u);
    CHECK_EQ(x.result, I2C_TXQ_NACK_DATA);
    /* The last byte not taken. */
    m.nack_byte = 2;
    wr3(&x, 0x40);
    go(&x);
    model_run_to(&m, &q, m.now + 100u);
    CHECK_EQ(x.result, I2C_TXQ_NACK_DATA);
    /* Past the last byte: nothing is refused. */
    m.nack_byte = 3;
    wr3(&x, 0x40);
    go(&x);
    model_run_to(&m, &q, m.now + 100u);
    CHECK_EQ(x.result, I2C_TXQ_OK);
    /* An address NACK with a lost arbitration beside it is the NACK. */
    m.nack_byte = -1;
    m.abort_extra = I2C_TXQ_ABRT_ARB_LOST;
    rd(&x, 0x41, 2);
    go(&x);
    model_run_to(&m, &q, m.now + 100u);
    CHECK_EQ(x.result, I2C_TXQ_NACK_ADDR);
    CHECK_EQ(q.st.failed, 4u);
    CHECK_EQ(q.st.ok, 1u);
    CHECK_EQ(m.resets, 0u);
}

TEST_CASE(a_lost_arbitration_and_a_short_read_are_their_own_results)
{
    i2c_txq_xfer_t x;
    const uint8_t two[2] = {1, 2};

    setup(1000);
    rd(&x, 0x40, 2);
    go(&x);
    m.pending = false;          /* the report is the test's */
    i2c_txq_complete(&q, 1050, I2C_TXQ_ABRT_ARB_LOST, NULL, 0);
    CHECK_EQ(x.result, I2C_TXQ_ARB_LOST);
    CHECK(x.done_us == 1050u);

    m.short_read = true;
    rd(&x, 0x40, 2);
    go(&x);
    model_run_to(&m, &q, m.now + 130u);
    CHECK_EQ(x.result, I2C_TXQ_FAULT);
    m.short_read = false;

    /* Bytes said and none handed over. */
    rd(&x, 0x40, 2);
    go(&x);
    m.pending = false;
    i2c_txq_complete(&q, m.now + 120u, 0, NULL, 2);
    CHECK_EQ(x.result, I2C_TXQ_FAULT);
    /* More bytes than asked for. */
    rd(&x, 0x40, 1);
    go(&x);
    m.pending = false;
    i2c_txq_complete(&q, m.now + 120u, 0, two, 2);
    CHECK_EQ(x.result, I2C_TXQ_FAULT);
    /* A cause the queue has no name for. */
    rd(&x, 0x40, 1);
    go(&x);
    m.pending = false;
    i2c_txq_complete(&q, m.now + 120u, 1u << 7, two, 1);
    CHECK_EQ(x.result, I2C_TXQ_FAULT);
    CHECK_EQ(q.st.failed, 5u);
}

TEST_CASE(a_line_that_rises_inside_the_settle_time_is_sent_on)
{
    i2c_txq_xfer_t x;

    /* Low until 1005: sent at 1005, no pull-up probe. */
    setup(1000);
    m.sda_hold_us = 1005;
    rd(&x, 0x40, 2);
    go(&x);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    CHECK_EQ(m.starts, 0u);
    model_run_to(&m, &q, 1004);
    CHECK_EQ(m.starts, 0u);
    model_tick(&m, &q);
    CHECK_EQ(m.starts, 1u);
    CHECK(x.start_us == 1005u);
    CHECK_EQ(m.pullup_ons, 0u);
    model_run_to(&m, &q, 1200);
    CHECK_EQ(x.result, I2C_TXQ_OK);

    /* Low until 1010, the end of the settle time: still sent. */
    setup(1000);
    m.sda_hold_us = 1010;
    rd(&x, 0x40, 2);
    go(&x);
    model_run_to(&m, &q, 1010);
    CHECK_EQ(m.starts, 1u);
    CHECK_EQ(m.pullup_ons, 0u);

    /* Low until 1011: the probe starts at 1010, and a line that rises in
     * it is no pull-up's -- nothing is sent. */
    setup(1000);
    m.sda_hold_us = 1011;
    rd(&x, 0x40, 2);
    go(&x);
    model_run_to(&m, &q, 1010);
    CHECK_EQ(m.pullup_ons, 1u);
    CHECK(m.pad_pu);
    model_tick(&m, &q);
    CHECK_EQ(x.result, I2C_TXQ_NO_PULLUP);
    CHECK(x.done_us == 1011u);
    CHECK(!m.pad_pu);
    CHECK_EQ(m.starts, 0u);
    CHECK_EQ(x.flags & I2C_TXQ_F_WIRE, 0u);
}

TEST_CASE(a_soft_one_whose_deadline_passes_in_the_settle_is_dropped)
{
    i2c_txq_xfer_t x;
    /* SDA rises at 1005: the release.  A deadline before it drops, one at
     * it or after it starts. */
    static const struct {
        uint64_t deadline;
        uint8_t  result;
        unsigned starts;
    } row[] = {
        {1002, I2C_TXQ_DROPPED, 0},
        {1004, I2C_TXQ_DROPPED, 0},
        {1005, I2C_TXQ_OK, 1},
        {1006, I2C_TXQ_OK, 1},
    };

    for (size_t i = 0; i < sizeof(row) / sizeof(row[0]); ++i) {
        setup(1000);
        m.sda_hold_us = 1005;
        rd(&x, 0x40, 2);
        x.deadline_us = row[i].deadline;
        go(&x);
        CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
        model_run_to(&m, &q, 1005);
        CHECK_EQ(m.starts, row[i].starts);
        model_run_to(&m, &q, 1200);
        CHECK_EQ(x.result, row[i].result);
        CHECK_EQ(q.st.dropped, row[i].starts == 0u ? 1u : 0u);
        if (row[i].starts == 0u) {
            CHECK(x.done_us == 1005u);
            CHECK_EQ(x.flags & I2C_TXQ_F_WIRE, 0u);
            CHECK_EQ(m.pullup_ons, 0u);
        }
    }

    /* A hard one in the same place runs late and says so. */
    setup(1000);
    m.sda_hold_us = 1005;
    rd(&x, 0x40, 2);
    x.cls = I2C_TXQ_HARD;
    x.deadline_us = 1004;
    go(&x);
    model_run_to(&m, &q, 1005);
    CHECK_EQ(m.starts, 1u);
    CHECK(x.start_us == 1005u);
    model_run_to(&m, &q, 1200);
    CHECK_EQ(x.result, I2C_TXQ_OK);
    CHECK(x.flags & I2C_TXQ_F_LATE_START);
    CHECK_EQ(q.st.dropped, 0u);

    /* A trial dropped in the settle passes the trial on. */
    i2c_txq_xfer_t next;
    setup(1000);
    CHECK(i2c_txq_recover(&q, 1000));
    model_run_to(&m, &q, 1115);
    m.sda_hold_us = 1120;
    rd(&x, 0x40, 2);
    x.deadline_us = 1117;
    go(&x);
    CHECK(x.flags & I2C_TXQ_F_TRIAL);
    model_run_to(&m, &q, 1120);
    CHECK_EQ(x.result, I2C_TXQ_DROPPED);
    rd(&next, 0x40, 2);
    go(&next);
    model_run_to(&m, &q, 1300);
    CHECK_EQ(next.result, I2C_TXQ_OK);
    CHECK(next.flags & I2C_TXQ_F_TRIAL);
}

TEST_CASE(lines_without_pull_ups_read_as_no_pull_up)
{
    i2c_txq_xfer_t x, next;

    setup(1000);
    m.pullups = false;
    rd(&x, 0x40, 2);
    rd(&next, 0x40, 2);
    go(&x);
    CHECK_EQ(i2c_txq_submit(&q, &next), I2C_TXQ_E_OK);
    model_run_to(&m, &q, 1010);
    CHECK(m.pad_pu);
    model_tick(&m, &q);
    CHECK_EQ(x.result, I2C_TXQ_NO_PULLUP);
    CHECK_EQ(q.st.failed, 1u);
    /* The next one is picked in the same poll and probes again. */
    CHECK_EQ(next.state, I2C_TXQ_ST_RUNNING);
    CHECK(!m.pad_pu);
    CHECK_EQ(m.starts, 0u);
}

TEST_CASE(a_line_a_part_holds_is_bus_low_at_the_end_of_the_probe)
{
    i2c_txq_xfer_t x;

    setup(1000);
    m.sda_hold = -1;
    rd(&x, 0x40, 2);
    go(&x);
    model_run_to(&m, &q, 1059);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    CHECK(m.pad_pu);
    model_tick(&m, &q);
    CHECK_EQ(x.result, I2C_TXQ_BUS_LOW);
    CHECK(x.done_us == 1060u);
    CHECK(!m.pad_pu);
    CHECK_EQ(m.starts, 0u);

    /* SCL the same way. */
    setup(1000);
    m.scl_hold = true;
    rd(&x, 0x40, 2);
    go(&x);
    model_run_to(&m, &q, 1060);
    CHECK_EQ(x.result, I2C_TXQ_BUS_LOW);
    CHECK_EQ(m.starts, 0u);
}

TEST_CASE(an_abort_mid_transaction_ends_it_after_the_byte)
{
    i2c_txq_xfer_t x, next;

    setup(1000);
    rd(&x, 0x40, 6);
    rd(&next, 0x40, 2);
    go(&x);
    CHECK_EQ(i2c_txq_submit(&q, &next), I2C_TXQ_E_OK);
    model_run_to(&m, &q, 1050);
    CHECK(i2c_txq_abort(&q, &x, 1050));
    CHECK_EQ(m.aborts, 1u);
    CHECK(i2c_txq_abort(&q, &x, 1051));
    CHECK_EQ(m.aborts, 1u);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    model_run_to(&m, &q, 1072);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    model_tick(&m, &q);
    CHECK_EQ(x.result, I2C_TXQ_ABORTED);
    CHECK(x.done_us == 1073u);
    CHECK(x.flags & I2C_TXQ_F_ABORT);
    CHECK(x.flags & I2C_TXQ_F_WIRE);
    CHECK_EQ(m.resets, 0u);
    CHECK_EQ(q.st.aborted, 1u);
    /* The next one goes at once. */
    CHECK_EQ(next.state, I2C_TXQ_ST_RUNNING);
    CHECK(next.start_us == 1073u);
}

TEST_CASE(an_abort_that_comes_too_late_leaves_the_result)
{
    i2c_txq_xfer_t x;

    setup(1000);
    rd(&x, 0x40, 2);
    go(&x);
    model_run_to(&m, &q, 1110);
    CHECK(i2c_txq_abort(&q, &x, 1110));
    model_run_to(&m, &q, 1120);
    CHECK_EQ(x.result, I2C_TXQ_OK);
    CHECK_EQ(x.rd[0], 0x51);
    CHECK(x.flags & I2C_TXQ_F_ABORT);
    /* A NACK under an abort is the abort. */
    rd(&x, 0x41, 2);
    m.extra_us = 50;
    go(&x);
    CHECK(i2c_txq_abort(&q, &x, m.now));
    model_run_to(&m, &q, m.now + 100u);
    CHECK_EQ(x.result, I2C_TXQ_ABORTED);
}

TEST_CASE(an_abort_before_the_wire_ends_it_at_once)
{
    i2c_txq_xfer_t x;

    /* In the settle time. */
    setup(1000);
    m.sda_hold = -1;
    rd(&x, 0x40, 2);
    go(&x);
    model_run_to(&m, &q, 1005);
    CHECK(i2c_txq_abort(&q, &x, 1005));
    CHECK_EQ(x.result, I2C_TXQ_ABORTED);
    CHECK_EQ(m.aborts, 0u);
    CHECK(i2c_txq_idle(&q));
    /* In the probe: the pull-ups go off. */
    rd(&x, 0x40, 2);
    go(&x);
    model_run_to(&m, &q, m.now + 20u);
    CHECK(m.pad_pu);
    CHECK(i2c_txq_abort(&q, &x, m.now));
    CHECK_EQ(x.result, I2C_TXQ_ABORTED);
    CHECK(!m.pad_pu);
}

TEST_CASE(a_transaction_that_does_not_end_times_out_at_1000us)
{
    i2c_txq_xfer_t x, next;

    setup(1000);
    m.stall = true;
    rd(&x, 0x40, 2);
    rd(&next, 0x40, 2);
    go(&x);
    m.stall = false;
    CHECK_EQ(i2c_txq_submit(&q, &next), I2C_TXQ_E_OK);
    model_run_to(&m, &q, 1999);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    CHECK_EQ(m.resets, 0u);
    model_tick(&m, &q);
    CHECK_EQ(x.result, I2C_TXQ_TIMEOUT);
    CHECK(x.done_us == 2000u);
    CHECK_EQ(m.resets, 1u);
    CHECK_EQ(q.st.timeouts, 1u);
    CHECK_EQ(q.st.failed, 1u);
    /* The next one starts in the same poll. */
    CHECK_EQ(next.state, I2C_TXQ_ST_RUNNING);
    CHECK(next.start_us == 2000u);
    /* An end reported for the cut one after the next has ended is not
     * one. */
    model_run_to(&m, &q, 2200);
    CHECK_EQ(next.result, I2C_TXQ_OK);
    i2c_txq_complete(&q, 2200, 0, NULL, 0);
    CHECK_EQ(q.st.spurious, 1u);

    /* An abort that never ends: the timeout ends it. */
    m.stall = true;
    rd(&x, 0x40, 2);
    go(&x);
    const uint64_t t0 = m.now;
    CHECK(i2c_txq_abort(&q, &x, t0 + 10u));
    model_run_to(&m, &q, t0 + 1000u);
    CHECK_EQ(x.result, I2C_TXQ_TIMEOUT);
    CHECK_EQ(m.resets, 2u);
}

TEST_CASE(the_timeout_holds_across_the_64_bit_wrap)
{
    i2c_txq_xfer_t x;
    const uint64_t t0 = UINT64_MAX - 100u;

    setup(t0);
    m.stall = true;
    rd(&x, 0x40, 2);
    go(&x);
    model_run_to(&m, &q, t0 + 999u);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    model_tick(&m, &q);
    CHECK_EQ(x.result, I2C_TXQ_TIMEOUT);
    CHECK(x.done_us == 899u);
}

TEST_CASE(the_bus_clear_clocks_9_times_then_stops)
{
    setup(1000);
    CHECK(i2c_txq_recover(&q, 1000));
    CHECK(i2c_txq_clearing(&q));
    CHECK(!i2c_txq_idle(&q));
    CHECK_EQ(m.gpio_ons, 1u);
    CHECK(m.gpio);
    CHECK(!i2c_txq_recover(&q, 1000));
    model_run_to(&m, &q, 1114);
    CHECK(i2c_txq_clearing(&q));
    CHECK_EQ(m.gpio_offs, 0u);
    model_tick(&m, &q);
    CHECK(!i2c_txq_clearing(&q));
    CHECK(i2c_txq_idle(&q));
    CHECK_EQ(m.gpio_offs, 1u);
    CHECK(!m.gpio);
    /* 9 clocks 10us apart, then the STOP's SCL rise and the SDA rise. */
    CHECK_EQ(m.scl_rises, 10u);
    for (unsigned k = 0; k < 9u; ++k) {
        CHECK(m.scl_rise_at[k] == 1010u + 10u * k);
    }
    CHECK(m.scl_rise_at[9] == 1105u);
    CHECK_EQ(m.sda_rises, 1u);
    CHECK_EQ(i2c_txq_clear_result(&q), I2C_TXQ_OK);
    CHECK_EQ(q.st.clears, 1u);
    CHECK_EQ(m.resets, 0u);
}

TEST_CASE(the_bus_clear_frees_a_part_mid_byte_and_reports_one_that_holds)
{
    /* 5 bits still to send: free after the fifth clock. */
    setup(1000);
    m.sda_hold = 5;
    CHECK(i2c_txq_recover(&q, 1000));
    model_run_to(&m, &q, 1200);
    CHECK_EQ(m.sda_hold, 0);
    CHECK_EQ(i2c_txq_clear_result(&q), I2C_TXQ_OK);
    /* 9 bits: the ninth clock frees it. */
    setup(1000);
    m.sda_hold = 9;
    CHECK(i2c_txq_recover(&q, 1000));
    model_run_to(&m, &q, 1200);
    CHECK_EQ(i2c_txq_clear_result(&q), I2C_TXQ_OK);
    /* 11 bits: still low at the end. */
    setup(1000);
    m.sda_hold = 11;
    CHECK(i2c_txq_recover(&q, 1000));
    model_run_to(&m, &q, 1200);
    CHECK(!i2c_txq_clearing(&q));
    CHECK_EQ(i2c_txq_clear_result(&q), I2C_TXQ_BUS_LOW);
    CHECK_EQ(q.st.clears, 1u);
}

TEST_CASE(scl_held_by_a_part_gets_100us_a_clock)
{
    setup(1000);
    m.scl_hold = true;
    CHECK(i2c_txq_recover(&q, 1000));
    /* 10 releases of SCL, each waited on for 100us. */
    model_run_to(&m, &q, 2114);
    CHECK(i2c_txq_clearing(&q));
    model_tick(&m, &q);
    CHECK(!i2c_txq_clearing(&q));
    CHECK_EQ(m.scl_rises, 10u);
    CHECK(m.scl_rise_at[1] == 1120u);
    CHECK_EQ(i2c_txq_clear_result(&q), I2C_TXQ_BUS_LOW);
    /* Let go after the third clock: on time from there. */
    setup(1000);
    m.scl_hold = true;
    CHECK(i2c_txq_recover(&q, 1000));
    while (m.scl_rises < 3u) {
        model_tick(&m, &q);
    }
    const uint64_t third = m.now;
    m.scl_hold = false;
    while (i2c_txq_clearing(&q)) {
        model_tick(&m, &q);
    }
    /* SCL seen high 1us after the release, then 17 actions 5us apart. */
    CHECK(m.now == third + 1u + 17u * 5u);
    CHECK_EQ(i2c_txq_clear_result(&q), I2C_TXQ_OK);
}

TEST_CASE(nothing_starts_in_a_clear_and_the_first_after_it_is_the_trial)
{
    i2c_txq_xfer_t run, a, b, soft;

    setup(1000);
    rd(&run, 0x40, 2);
    go(&run);
    CHECK_EQ(run.state, I2C_TXQ_ST_RUNNING);
    /* A clear cuts the one on the wire. */
    CHECK(i2c_txq_recover(&q, 1000));
    CHECK_EQ(run.result, I2C_TXQ_ABORTED);
    CHECK_EQ(m.resets, 1u);
    rd(&a, 0x40, 2);
    a.cls = I2C_TXQ_HARD;
    rd(&b, 0x40, 2);
    rd(&soft, 0x40, 2);
    soft.deadline_us = 1050;
    CHECK_EQ(i2c_txq_submit(&q, &a), I2C_TXQ_E_OK);
    CHECK_EQ(i2c_txq_submit(&q, &b), I2C_TXQ_E_OK);
    CHECK_EQ(i2c_txq_submit(&q, &soft), I2C_TXQ_E_OK);
    model_run_to(&m, &q, 1114);
    CHECK_EQ(m.starts, 1u);
    CHECK_EQ(a.state, I2C_TXQ_ST_QUEUED);
    /* Dropped while the clear ran. */
    CHECK_EQ(soft.result, I2C_TXQ_DROPPED);
    model_tick(&m, &q);
    CHECK_EQ(a.state, I2C_TXQ_ST_RUNNING);
    CHECK(a.start_us == 1115u);
    model_run_to(&m, &q, 1400);
    CHECK_EQ(a.result, I2C_TXQ_OK);
    CHECK(a.flags & I2C_TXQ_F_TRIAL);
    CHECK_EQ(b.result, I2C_TXQ_OK);
    CHECK_EQ(b.flags & I2C_TXQ_F_TRIAL, 0u);
}

TEST_CASE(a_failed_trial_is_spent_and_an_aborted_one_is_not)
{
    i2c_txq_xfer_t a, b, c;

    /* A trial that ends with a NACK is the trial. */
    setup(1000);
    CHECK(i2c_txq_recover(&q, 1000));
    model_run_to(&m, &q, 1115);
    rd(&a, 0x41, 2);
    rd(&b, 0x40, 2);
    go(&a);
    CHECK_EQ(i2c_txq_submit(&q, &b), I2C_TXQ_E_OK);
    model_run_to(&m, &q, 1400);
    CHECK_EQ(a.result, I2C_TXQ_NACK_ADDR);
    CHECK(a.flags & I2C_TXQ_F_TRIAL);
    CHECK_EQ(b.flags & I2C_TXQ_F_TRIAL, 0u);

    /* A trial taken back before the wire passes the trial on. */
    setup(1000);
    CHECK(i2c_txq_recover(&q, 1000));
    model_run_to(&m, &q, 1115);
    m.sda_hold_us = 1120;
    rd(&a, 0x40, 2);
    go(&a);
    CHECK(a.flags & I2C_TXQ_F_TRIAL);
    CHECK(i2c_txq_abort(&q, &a, m.now));
    CHECK_EQ(a.result, I2C_TXQ_ABORTED);
    rd(&c, 0x40, 2);
    go(&c);
    model_run_to(&m, &q, 1400);
    CHECK_EQ(c.result, I2C_TXQ_OK);
    CHECK(c.flags & I2C_TXQ_F_TRIAL);

    /* A trial that finds the line still low is the trial. */
    setup(1000);
    CHECK(i2c_txq_recover(&q, 1000));
    model_run_to(&m, &q, 1115);
    m.sda_hold = -1;
    rd(&a, 0x40, 2);
    go(&a);
    model_run_to(&m, &q, 1200);
    CHECK_EQ(a.result, I2C_TXQ_BUS_LOW);
    CHECK(a.flags & I2C_TXQ_F_TRIAL);
    CHECK(!q.trial);
}

TEST_CASE(a_flush_cuts_a_clear)
{
    setup(1000);
    CHECK(i2c_txq_recover(&q, 1000));
    model_run_to(&m, &q, 1050);
    i2c_txq_flush(&q, 1050);
    CHECK(!i2c_txq_clearing(&q));
    CHECK_EQ(m.gpio_offs, 1u);
    CHECK(!m.gpio);
    CHECK_EQ(i2c_txq_clear_result(&q), I2C_TXQ_ABORTED);
    CHECK_EQ(q.st.clears, 0u);
    /* A new clear runs from the start. */
    CHECK(i2c_txq_recover(&q, 1050));
    model_run_to(&m, &q, 1165);
    CHECK_EQ(i2c_txq_clear_result(&q), I2C_TXQ_OK);
    CHECK_EQ(q.st.clears, 1u);
}

int main(void)
{
    RUN(a_read_brings_its_bytes_and_its_stamp);
    RUN(a_late_report_takes_the_computed_end_past_100us);
    RUN(a_nack_on_the_address_is_told_from_one_on_data);
    RUN(a_lost_arbitration_and_a_short_read_are_their_own_results);
    RUN(a_line_that_rises_inside_the_settle_time_is_sent_on);
    RUN(a_soft_one_whose_deadline_passes_in_the_settle_is_dropped);
    RUN(lines_without_pull_ups_read_as_no_pull_up);
    RUN(a_line_a_part_holds_is_bus_low_at_the_end_of_the_probe);
    RUN(an_abort_mid_transaction_ends_it_after_the_byte);
    RUN(an_abort_that_comes_too_late_leaves_the_result);
    RUN(an_abort_before_the_wire_ends_it_at_once);
    RUN(a_transaction_that_does_not_end_times_out_at_1000us);
    RUN(the_timeout_holds_across_the_64_bit_wrap);
    RUN(the_bus_clear_clocks_9_times_then_stops);
    RUN(the_bus_clear_frees_a_part_mid_byte_and_reports_one_that_holds);
    RUN(scl_held_by_a_part_gets_100us_a_clock);
    RUN(nothing_starts_in_a_clear_and_the_first_after_it_is_the_trial);
    RUN(a_failed_trial_is_spent_and_an_aborted_one_is_not);
    RUN(a_flush_cuts_a_clear);
    return test_summary("i2c_txq_wire");
}
