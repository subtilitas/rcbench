/*
 * The sensor bus on the I2C transaction queue (shared/sense/sense_txq.c),
 * over the modelled bus of i2c_txq_model.c, through sense_txq_rig.h.
 *
 * Under test:
 *  - a read: its bytes, its end stamped on the queue's clock, the wait;
 *    the stamp before the first transaction;
 *  - the lengths at their limits: a read of 0, 1, 6 and 7 bytes, a write
 *    of 0, 4 and 5, the address ask;
 *  - what each end comes to: a NACK on the address and on a data byte,
 *    lines without pull-ups, a line a part holds (BUS_LOW 60us after the
 *    submit), SCL held (TIMEOUT 1000us after the start), a lost
 *    arbitration with SDA back high 10us after the end and 11us after it;
 *  - the queue full at 16 and with room at 15, nothing sent;
 *  - a read behind another on the wire started late and not dropped;
 *  - a report 100us late keeps its stamp, 101us late takes the computed
 *    end: core 1 parked by the flash lock-out mid-transaction;
 *  - a driver that never reports: the abort asked at 5000us, the wait
 *    given up at 10000us, every transaction a timeout with nothing sent
 *    while the queue holds the descriptor, and the bus clear that gives
 *    it back;
 *  - the bus clear waited for, the trial after it, a clear the queue
 *    refuses.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "sense_txq_rig.h"

#define T0   20000000u          /* 20s: the model's line times are 32-bit */
#define PART 0x40u

static txq_rig_t tr;

static void setup(void)
{
    txq_rig_init(&tr, T0);
    tr.m.present[PART] = true;
}

/* The computed bus time of the wire's last transaction, us. */
static uint32_t bus_us(void)
{
    return i2c_txq_bus_us(&tr.t.x, I2C_TXQ_HZ);
}

/* --------------------------------------------------------------- cases */

TEST_CASE(a_read_brings_its_bytes_and_its_end)
{
    setup();
    CHECK_EQ(sense_txq_done_us(&tr.t), (uint64_t)T0);
    uint8_t buf[6] = {0};
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x02, buf, 2), SENSE_OK);
    CHECK_EQ(buf[0], 0x51u);
    CHECK_EQ(buf[1], 0x62u);
    CHECK_EQ(tr.m.last.wr_len, 1u);
    CHECK_EQ(tr.m.last.wr[0], 0x02u);
    CHECK_EQ(tr.m.last.rd_len, 2u);
    CHECK_EQ(tr.m.last.cls, I2C_TXQ_HARD);
    CHECK_EQ(tr.t.x.start_us, (uint64_t)T0);
    const uint64_t end = T0 + bus_us();
    CHECK_EQ(tr.t.x.done_us, end);
    CHECK_EQ(sense_txq_done_us(&tr.t), end);
    CHECK_EQ(tr.m.now, end);
    CHECK_EQ(tr.t.transactions, 1u);
    CHECK_EQ(tr.t.wait_max_us, bus_us());
    CHECK_EQ(tr.t.overruns, 0u);
    /* The next one starts where the last ended. */
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x04, buf, 2), SENSE_OK);
    CHECK_EQ(tr.t.x.start_us, end);
    CHECK_EQ(tr.t.transactions, 2u);
}

TEST_CASE(the_lengths_hold_at_their_limits)
{
    setup();
    uint8_t buf[8] = {0};
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 0), SENSE_BAD_ARG);
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 7), SENSE_BAD_ARG);
    CHECK_EQ(tr.m.starts, 0u);
    CHECK_EQ(tr.t.transactions, 0u);
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 1), SENSE_OK);
    CHECK_EQ(tr.m.last.rd_len, 1u);
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 6), SENSE_OK);
    CHECK_EQ(tr.m.last.rd_len, 6u);
    CHECK_EQ(buf[5], (uint8_t)(PART + 0x66u));

    const uint8_t w[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
    CHECK_EQ(sense_txq_write(&tr.t, PART, 0x05, w, 0), SENSE_BAD_ARG);
    CHECK_EQ(sense_txq_write(&tr.t, PART, 0x05, w, 5), SENSE_BAD_ARG);
    CHECK_EQ(tr.m.starts, 2u);
    CHECK_EQ(sense_txq_write(&tr.t, PART, 0x05, w, 4), SENSE_OK);
    CHECK_EQ(tr.m.last.wr_len, 5u);
    CHECK_EQ(tr.m.last.wr[0], 0x05u);
    CHECK_EQ(tr.m.last.wr[4], 0x44u);
    CHECK_EQ(tr.m.last.rd_len, 0u);

    CHECK_EQ(sense_txq_ask(&tr.t, PART), SENSE_OK);
    CHECK_EQ(tr.m.last.wr_len, 0u);
    CHECK_EQ(tr.m.last.rd_len, 1u);
    CHECK_EQ(sense_txq_ask(&tr.t, 0x41u), SENSE_NACK);
    CHECK_EQ(tr.t.transactions, 5u);
}

TEST_CASE(a_nack_on_the_address_or_on_a_byte_is_a_nack)
{
    setup();
    uint8_t buf[2] = {0xEE, 0xEE};
    CHECK_EQ(sense_txq_read(&tr.t, 0x41u, 0x00, buf, 2), SENSE_NACK);
    CHECK_EQ(tr.t.x.result, I2C_TXQ_NACK_ADDR);
    CHECK_EQ(buf[0], 0xEEu);
    tr.m.nack_byte = 1;
    const uint8_t w[2] = {1, 2};
    CHECK_EQ(sense_txq_write(&tr.t, PART, 0x00, w, 2), SENSE_NACK);
    CHECK_EQ(tr.t.x.result, I2C_TXQ_NACK_DATA);
    CHECK_EQ(tr.m.resets, 0u);
}

TEST_CASE(lines_without_pull_ups_are_a_nack_with_nothing_sent)
{
    setup();
    tr.m.pullups = false;
    uint8_t buf[2];
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_NACK);
    CHECK_EQ(tr.t.x.result, I2C_TXQ_NO_PULLUP);
    CHECK_EQ(tr.m.starts, 0u);
    CHECK_EQ(tr.m.pullup_ons, 1u);
    CHECK(!tr.m.pad_pu);
}

TEST_CASE(a_held_line_is_bus_low_60us_after_the_submit)
{
    setup();
    tr.m.sda_hold = -1;
    uint8_t buf[2];
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_BUS_LOW);
    CHECK_EQ(tr.m.starts, 0u);
    CHECK_EQ(tr.m.now - T0, (uint64_t)(I2C_TXQ_SETTLE_US + I2C_TXQ_PROBE_US));
    CHECK(!tr.m.pad_pu);
}

TEST_CASE(scl_held_is_a_timeout_1000us_after_the_start)
{
    setup();
    tr.m.stall = true;
    uint8_t buf[2];
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_TIMEOUT);
    CHECK_EQ(tr.t.x.result, I2C_TXQ_TIMEOUT);
    CHECK_EQ(tr.m.now - T0, (uint64_t)I2C_TXQ_TIMEOUT_US);
    CHECK_EQ(tr.m.resets, 1u);
    CHECK_EQ(tr.t.overruns, 0u);
    CHECK_EQ(tr.aborts, 0u);
}

/* The part at PART loses arbitration on every transaction. */
static model_answer_t lose(void *ctx, const i2c_txq_xfer_t *x, uint8_t *rx)
{
    (void)ctx;
    (void)x;
    (void)rx;
    return MODEL_ARB_LOST;
}

TEST_CASE(a_lost_arbitration_is_a_nack_with_sda_high_within_10us)
{
    setup();
    tr.m.respond = lose;
    uint8_t buf[2];
    tr.m.arb_hold_us = 0;
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_NACK);
    CHECK_EQ(tr.t.x.result, I2C_TXQ_ARB_LOST);
    tr.m.now += 100u;
    tr.m.arb_hold_us = SENSE_TXQ_SETTLE_US;
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_NACK);
    CHECK_EQ(tr.m.now - tr.t.x.done_us, (uint64_t)SENSE_TXQ_SETTLE_US);
    tr.m.now += 100u;
    tr.m.arb_hold_us = SENSE_TXQ_SETTLE_US + 1u;
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_BUS_LOW);
    CHECK_EQ(tr.m.now - tr.t.x.done_us, (uint64_t)SENSE_TXQ_SETTLE_US);
}

/* @p n soft reads queued, not before 100ms on. */
static void fill(i2c_txq_xfer_t *xs, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        memset(&xs[i], 0, sizeof(xs[i]));
        xs[i].addr = PART;
        xs[i].rd_len = 1;
        xs[i].cls = I2C_TXQ_SOFT;
        xs[i].not_before_us = tr.m.now + 100000u;
        xs[i].deadline_us = tr.m.now + 200000u;
        CHECK_EQ(i2c_txq_submit(&tr.q, &xs[i]), I2C_TXQ_E_OK);
    }
}

TEST_CASE(a_full_queue_is_a_timeout_with_nothing_sent)
{
    static i2c_txq_xfer_t xs[I2C_TXQ_DEPTH];
    uint8_t buf[2];

    setup();
    fill(xs, I2C_TXQ_DEPTH - 1u);
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_OK);
    CHECK_EQ(tr.t.transactions, 1u);

    setup();
    fill(xs, I2C_TXQ_DEPTH);
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_TIMEOUT);
    CHECK_EQ(sense_txq_ask(&tr.t, PART), SENSE_TIMEOUT);
    CHECK_EQ(tr.m.starts, 0u);
    CHECK_EQ(tr.t.transactions, 0u);
    CHECK_EQ(tr.m.now, (uint64_t)T0);
}

TEST_CASE(a_read_behind_another_on_the_wire_starts_late_and_is_sent)
{
    setup();
    i2c_txq_xfer_t other;
    memset(&other, 0, sizeof(other));
    other.addr = PART;
    other.rd_len = 6;
    other.wr_len = 1;
    other.cls = I2C_TXQ_SOFT;
    other.not_before_us = T0;
    other.deadline_us = T0 + 1000u;
    CHECK_EQ(i2c_txq_submit(&tr.q, &other), I2C_TXQ_E_OK);
    i2c_txq_poll(&tr.q, tr.m.now);
    CHECK_EQ(other.state, I2C_TXQ_ST_RUNNING);
    uint8_t buf[2];
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_OK);
    CHECK_EQ(other.state, I2C_TXQ_ST_DONE);
    CHECK_EQ(tr.t.x.start_us, other.done_us);
    CHECK((tr.t.x.flags & I2C_TXQ_F_LATE_START) != 0u);
    CHECK_EQ(tr.t.wait_max_us,
             (uint32_t)(tr.t.x.done_us - T0));
}

TEST_CASE(a_report_past_100us_late_takes_the_computed_end)
{
    uint8_t buf[2];

    setup();
    /* Core 1 parked from the start: the end is seen 100us after it. */
    tr.freeze_n = i2c_txq_bus_us(&(i2c_txq_xfer_t){ .wr_len = 1, .rd_len = 2 },
                                I2C_TXQ_HZ) + I2C_TXQ_LATE_US - 1u;
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_OK);
    CHECK_EQ(sense_txq_done_us(&tr.t), T0 + bus_us() + I2C_TXQ_LATE_US);
    CHECK_EQ(tr.t.x.flags & I2C_TXQ_F_LATE_STAMP, 0u);

    setup();
    tr.freeze_n = i2c_txq_bus_us(&(i2c_txq_xfer_t){ .wr_len = 1, .rd_len = 2 },
                                I2C_TXQ_HZ) + I2C_TXQ_LATE_US;
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_OK);
    CHECK_EQ(sense_txq_done_us(&tr.t), T0 + bus_us());
    CHECK((tr.t.x.flags & I2C_TXQ_F_LATE_STAMP) != 0u);
    CHECK_EQ(tr.m.now, T0 + bus_us() + I2C_TXQ_LATE_US + 1u);
}

TEST_CASE(a_driver_that_never_reports_is_aborted_at_5ms_and_left_at_10ms)
{
    setup();
    tr.frozen = true;
    uint8_t buf[2] = {0xEE, 0xEE};
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_TIMEOUT);
    CHECK_EQ(tr.aborts, 1u);
    CHECK_EQ(tr.abort_at, T0 + SENSE_TXQ_WAIT_US);
    CHECK_EQ(tr.t.overruns, 1u);
    CHECK_EQ(tr.m.now, T0 + 2u * SENSE_TXQ_WAIT_US);
    CHECK_EQ(tr.t.x.state, I2C_TXQ_ST_RUNNING);
    CHECK_EQ(buf[0], 0xEEu);
    /* The queue holds the descriptor: nothing is sent. */
    const unsigned pumps = tr.pumps;
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_TIMEOUT);
    CHECK_EQ(sense_txq_write(&tr.t, PART, 0x00, buf, 1), SENSE_TIMEOUT);
    CHECK_EQ(sense_txq_ask(&tr.t, PART), SENSE_TIMEOUT);
    CHECK_EQ(tr.pumps, pumps);
    CHECK_EQ(tr.t.transactions, 1u);
    CHECK_EQ(tr.m.starts, 1u);
    /* The bus clear gives it back, and the next one is sent. */
    tr.frozen = false;
    sense_txq_recover(&tr.t);
    CHECK(!i2c_txq_clearing(&tr.q));
    CHECK_EQ(tr.t.x.state, I2C_TXQ_ST_DONE);
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_OK);
    CHECK_EQ(tr.t.transactions, 2u);
}

TEST_CASE(the_bus_clear_is_waited_for_and_the_next_is_its_trial)
{
    setup();
    tr.m.sda_hold = 3;       /* a part mid-byte, 3 clocks to go */
    sense_txq_recover(&tr.t);
    CHECK(!i2c_txq_clearing(&tr.q));
    CHECK_EQ(i2c_txq_clear_result(&tr.q), I2C_TXQ_OK);
    CHECK_EQ(tr.m.sda_hold, 0);
    CHECK_EQ(tr.m.scl_rises, 10u);
    CHECK_EQ(tr.m.gpio_offs, 1u);
    /* The first of the 24 steps at once, the last 23 half clocks on. */
    CHECK_EQ(tr.m.now - T0, (uint64_t)(23u * I2C_TXQ_CLEAR_HALF_US));
    uint8_t buf[2];
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_OK);
    CHECK((tr.t.x.flags & I2C_TXQ_F_TRIAL) != 0u);

    /* A queue that refuses the clear: nothing waited for. */
    setup();
    i2c_txq_shut(&tr.q, tr.m.now);
    sense_txq_recover(&tr.t);
    CHECK_EQ(tr.pumps, 0u);
    CHECK_EQ(tr.m.gpio_ons, 0u);
    CHECK_EQ(sense_txq_read(&tr.t, PART, 0x00, buf, 2), SENSE_BAD_ARG);
}

TEST_CASE(every_queue_result_maps_to_one_sense_error)
{
    CHECK_EQ(sense_txq_err(I2C_TXQ_OK), SENSE_OK);
    CHECK_EQ(sense_txq_err(I2C_TXQ_NACK_ADDR), SENSE_NACK);
    CHECK_EQ(sense_txq_err(I2C_TXQ_NACK_DATA), SENSE_NACK);
    CHECK_EQ(sense_txq_err(I2C_TXQ_ARB_LOST), SENSE_NACK);
    CHECK_EQ(sense_txq_err(I2C_TXQ_TIMEOUT), SENSE_TIMEOUT);
    CHECK_EQ(sense_txq_err(I2C_TXQ_BUS_LOW), SENSE_BUS_LOW);
    CHECK_EQ(sense_txq_err(I2C_TXQ_NO_PULLUP), SENSE_NACK);
    CHECK_EQ(sense_txq_err(I2C_TXQ_DROPPED), SENSE_TIMEOUT);
    CHECK_EQ(sense_txq_err(I2C_TXQ_ABORTED), SENSE_TIMEOUT);
    CHECK_EQ(sense_txq_err(I2C_TXQ_FAULT), SENSE_NACK);
    CHECK_EQ(sense_txq_err(I2C_TXQ_RESULTS), SENSE_TIMEOUT);
}

int main(void)
{
    RUN(a_read_brings_its_bytes_and_its_end);
    RUN(the_lengths_hold_at_their_limits);
    RUN(a_nack_on_the_address_or_on_a_byte_is_a_nack);
    RUN(lines_without_pull_ups_are_a_nack_with_nothing_sent);
    RUN(a_held_line_is_bus_low_60us_after_the_submit);
    RUN(scl_held_is_a_timeout_1000us_after_the_start);
    RUN(a_lost_arbitration_is_a_nack_with_sda_high_within_10us);
    RUN(a_full_queue_is_a_timeout_with_nothing_sent);
    RUN(a_read_behind_another_on_the_wire_starts_late_and_is_sent);
    RUN(a_report_past_100us_late_takes_the_computed_end);
    RUN(a_driver_that_never_reports_is_aborted_at_5ms_and_left_at_10ms);
    RUN(the_bus_clear_is_waited_for_and_the_next_is_its_trial);
    RUN(every_queue_result_maps_to_one_sense_error);
    return test_summary("sense_txq");
}
