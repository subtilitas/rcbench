/*
 * The IOmcu's sensor bus wire (firmware/iomcu/src/sense_i2c.c): the glue
 * between core 1 and the I2C transaction queue, built from its firmware
 * source over this suite's own i2c_txq_block_*() -- the queue on the
 * modelled bus of i2c_txq_model.c -- and the timer of
 * sense_i2c_stub/pico/stdlib.h.  The model's clock follows the timer; the
 * pump's busy-wait moves it 1us.
 *
 * Under test: the pins (a pair, not a pair, past the chip, a block taken),
 * a closed bus sending nothing; a read and a write as the queue carries
 * them, the end stamped for the schedule; pins nothing is wired to read
 * as nothing answering, with nothing sent and the bus not stuck; a line a
 * part holds as BUS_LOW 60us on; SCL held as a timeout 1000us after the
 * start, the block reset; the bus clear waited for; core 1's tick poll:
 * an end the interrupt has not taken, and a transaction queued from a
 * done callback, each taken at that poll and not before; the flash
 * lock-out landing mid-transaction.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "i2c_txq_block.h"
#include "i2c_txq_model.h"
#include "pico/stdlib.h"
#include "sense_bus.h"
#include "sense_i2c.h"

uint64_t fake_us;
uint32_t fake_park_us;

#define SDA  16u
#define SCL  17u
#define PART 0x40u
#define T0   20000000u

/* ------------------------------------------- the block, over the model */

static model_t          m;
static i2c_txq_block_t *g_bus;          /* the glue's, as opened */
static i2c_txq_block_err_t g_open_rc;   /* what the next open answers */
static unsigned g_opens, g_closes, g_polls;
static unsigned g_open_sda, g_open_scl;

/* The model's clock to the timer's. */
static void sync_clock(void)
{
    if (m.now < fake_us) {
        m.now = fake_us;
    }
}

void i2c_txq_block_init(i2c_txq_block_t *b)
{
    memset(b, 0, sizeof(*b));
}

i2c_txq_block_err_t i2c_txq_block_open(i2c_txq_block_t *b, unsigned sda,
                                       unsigned scl, const i2c_txq_cfg_t *cfg)
{
    g_opens++;
    g_open_sda = sda;
    g_open_scl = scl;
    if (g_open_rc != I2C_TXQ_BLOCK_OK) {
        return g_open_rc;
    }
    sync_clock();
    if (!i2c_txq_init(&b->q, &model_ops, &m, cfg)) {
        return I2C_TXQ_BLOCK_ERR_ARG;
    }
    b->open = true;
    b->sda = (uint8_t)sda;
    b->scl = (uint8_t)scl;
    g_bus = b;
    return I2C_TXQ_BLOCK_OK;
}

void i2c_txq_block_close(i2c_txq_block_t *b)
{
    g_closes++;
    if (b->open) {
        b->open = false;
        sync_clock();
        i2c_txq_shut(&b->q, m.now);
    }
}

i2c_txq_err_t i2c_txq_block_submit(i2c_txq_block_t *b, i2c_txq_xfer_t *x)
{
    if (!b->open) {
        return I2C_TXQ_E_CLOSED;
    }
    sync_clock();
    const i2c_txq_err_t e = i2c_txq_submit(&b->q, x);
    if (e == I2C_TXQ_E_OK) {
        i2c_txq_poll(&b->q, m.now);
    }
    return e;
}

bool i2c_txq_block_abort(i2c_txq_block_t *b, i2c_txq_xfer_t *x)
{
    sync_clock();
    return b->open && i2c_txq_abort(&b->q, x, m.now);
}

bool i2c_txq_block_recover(i2c_txq_block_t *b)
{
    sync_clock();
    return b->open && i2c_txq_recover(&b->q, m.now);
}

void i2c_txq_block_hold(i2c_txq_block_t *b, bool on)
{
    i2c_txq_hold(&b->q, on);
}

/* The interrupt is masked here: the poll takes an end the model has. */
void i2c_txq_block_poll(i2c_txq_block_t *b)
{
    g_polls++;
    if (!b->open) {
        return;
    }
    sync_clock();
    model_report(&m, &b->q);
    i2c_txq_poll(&b->q, m.now);
}

const i2c_txq_t *i2c_txq_block_queue(const i2c_txq_block_t *b)
{
    return b == NULL ? NULL : &b->q;
}

/* ------------------------------------------------------------- rig */

/* A bus with its pull-ups and a part at PART, open on SDA and SCL. */
static void wired(void)
{
    sense_i2c_close();
    fake_us = T0;
    fake_park_us = 0u;
    model_init(&m, T0);
    m.present[PART] = true;
    g_bus = NULL;
    g_open_rc = I2C_TXQ_BLOCK_OK;
    g_opens = g_closes = g_polls = 0u;
    CHECK(sense_i2c_open((uint8_t)SDA, (uint8_t)SCL));
}

/* --------------------------------------------------------------- cases */

TEST_CASE(the_bus_opens_on_one_blocks_pair_and_nothing_else)
{
    wired();
    CHECK_EQ(g_opens, 1u);
    CHECK_EQ(g_open_sda, SDA);
    CHECK_EQ(g_open_scl, SCL);
    /* Opening again closes first. */
    CHECK(sense_i2c_open(14u, 15u));
    CHECK_EQ(g_closes, 1u);
    CHECK_EQ(g_opens, 2u);
    /* Not a pair: refused before the block is asked, and left closed. */
    CHECK(!sense_i2c_open(15u, 16u));
    CHECK(!sense_i2c_open(16u, 18u));
    CHECK(!sense_i2c_open(46u, 48u));
    CHECK(!sense_i2c_open(48u, 49u));
    CHECK_EQ(g_opens, 2u);
    CHECK_EQ(g_closes, 2u);
    CHECK_EQ(sense_i2c_ask(PART), SENSE_BAD_ARG);
    /* A block or interrupt taken: refused, and closed. */
    g_open_rc = I2C_TXQ_BLOCK_ERR_BUSY;
    CHECK(!sense_i2c_open((uint8_t)SDA, (uint8_t)SCL));
    CHECK_EQ(sense_i2c_ask(PART), SENSE_BAD_ARG);
    CHECK_EQ(m.starts, 0u);
}

TEST_CASE(a_closed_bus_sends_nothing)
{
    wired();
    sense_i2c_close();
    CHECK_EQ(g_closes, 1u);
    sense_i2c_close();
    CHECK_EQ(g_closes, 1u);
    uint8_t buf[2] = {0};
    CHECK_EQ(sense_i2c_read(NULL, PART, 0x00, buf, 2), SENSE_BAD_ARG);
    CHECK_EQ(sense_i2c_write(NULL, PART, 0x00, buf, 2), SENSE_BAD_ARG);
    CHECK_EQ(sense_i2c_ask(PART), SENSE_BAD_ARG);
    const unsigned polls = g_polls;
    sense_i2c_recover();
    sense_i2c_poll();
    CHECK_EQ(g_polls, polls);
    CHECK_EQ(m.starts, 0u);
    CHECK_EQ(m.gpio_ons, 0u);
    fake_us += 7u;
    CHECK_EQ(sense_i2c_done_us(NULL), fake_us);
}

TEST_CASE(a_read_and_a_write_go_out_whole_and_are_stamped_at_their_end)
{
    wired();
    CHECK_EQ(sense_i2c_done_us(NULL), (uint64_t)T0);
    uint8_t buf[6] = {0};
    CHECK_EQ(sense_i2c_read(NULL, PART, 0x3Eu, buf, 2), SENSE_OK);
    CHECK_EQ(buf[0], (uint8_t)(PART + 0x11u));
    CHECK_EQ(m.last.wr[0], 0x3Eu);
    CHECK_EQ(m.last.rd_len, 2u);
    CHECK_EQ(m.last.cls, I2C_TXQ_HARD);
    /* Pointer and 2 bytes read: 48 clocks, 120us at 400kHz. */
    CHECK_EQ(sense_i2c_done_us(NULL), (uint64_t)T0 + 120u);
    CHECK_EQ(fake_us, (uint64_t)T0 + 120u);
    CHECK_EQ(sense_i2c_read(NULL, PART, 0x00, buf, 6), SENSE_OK);
    CHECK_EQ(sense_i2c_read(NULL, PART, 0x00, buf, 7), SENSE_BAD_ARG);
    const uint8_t w[4] = {0x71, 0x27, 0x00, 0x01};
    CHECK_EQ(sense_i2c_write(NULL, PART, 0x00, w, 2), SENSE_OK);
    CHECK_EQ(m.last.wr_len, 3u);
    CHECK_EQ(m.last.wr[1], 0x71u);
    CHECK_EQ(sense_i2c_write(NULL, PART, 0x00, w, 4), SENSE_OK);
    CHECK_EQ(sense_i2c_write(NULL, PART, 0x00, w, 5), SENSE_BAD_ARG);
    CHECK_EQ(sense_i2c_ask(PART), SENSE_OK);
    CHECK_EQ(sense_i2c_ask(0x41u), SENSE_NACK);
    CHECK_EQ(m.starts, 6u);
}

TEST_CASE(pins_nothing_is_wired_to_read_as_nothing_answering)
{
    wired();
    m.pullups = false;
    sense_bus_t bus;
    const sense_i2c_t io = { sense_i2c_read, sense_i2c_write, NULL };
    sense_bus_init(&bus, &io);
    uint8_t buf[2];
    for (int k = 0; k < 4; ++k) {
        CHECK_EQ(sense_bus_read(&bus, 0x45u, 0x3Eu, buf, sizeof(buf)),
                 SENSE_NACK);
        sense_bus_note(&bus, sense_i2c_ask(PART));
    }
    CHECK_EQ(m.starts, 0u);                  /* nothing was sent */
    CHECK(!m.pad_pu);                        /* the pulls off again */
    CHECK(!bus.stuck);
    CHECK(!sense_bus_recovery_due(&bus, 2000u));
}

TEST_CASE(a_line_a_part_holds_is_bus_low_60us_on)
{
    wired();
    m.sda_hold = -1;
    CHECK_EQ(sense_i2c_ask(PART), SENSE_BUS_LOW);
    CHECK_EQ(fake_us - T0,
             (uint64_t)(SENSE_I2C_IDLE_US + SENSE_I2C_PROBE_US));
    CHECK_EQ(m.starts, 0u);
    CHECK(!m.pad_pu);
    m.sda_hold = 0;
    m.scl_hold = true;
    CHECK_EQ(sense_i2c_ask(PART), SENSE_BUS_LOW);
}

TEST_CASE(scl_held_is_a_timeout_1000us_after_the_start_and_resets_the_block)
{
    wired();
    m.stall = true;
    uint8_t buf[2];
    CHECK_EQ(sense_i2c_read(NULL, PART, 0x00, buf, 2), SENSE_TIMEOUT);
    CHECK_EQ(fake_us - T0, (uint64_t)SENSE_I2C_TIMEOUT_US);
    CHECK_EQ(m.resets, 1u);
    /* The block starts the next one from its reset. */
    m.stall = false;
    CHECK_EQ(sense_i2c_read(NULL, PART, 0x00, buf, 2), SENSE_OK);
}

TEST_CASE(the_bus_clear_is_waited_for)
{
    wired();
    m.sda_hold = 5;             /* a part mid-byte, 5 clocks to go */
    sense_i2c_recover();
    CHECK(!i2c_txq_clearing(&g_bus->q));
    CHECK_EQ(i2c_txq_clear_result(&g_bus->q), I2C_TXQ_OK);
    CHECK_EQ(m.scl_rises, 10u); /* 9 clocks and the STOP's */
    CHECK_EQ(fake_us - T0, (uint64_t)(23u * SENSE_I2C_CLEAR_HALF_US));
    CHECK_EQ(sense_i2c_ask(PART), SENSE_OK);
}

TEST_CASE(core_1s_poll_takes_an_end_the_interrupt_left)
{
    wired();
    /* A soft read queued by another producer, its end not reported by
     * an interrupt: core 1's tick poll takes it. */
    i2c_txq_xfer_t x;
    memset(&x, 0, sizeof(x));
    x.addr = PART;
    x.wr_len = 1;
    x.rd_len = 2;
    x.cls = I2C_TXQ_SOFT;
    x.not_before_us = T0;
    x.deadline_us = T0 + 500u;
    CHECK_EQ(i2c_txq_block_submit(g_bus, &x), I2C_TXQ_E_OK);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    fake_us = T0 + 400u;                     /* the end at 120us passed */
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    sense_i2c_poll();
    CHECK_EQ(x.state, I2C_TXQ_ST_DONE);
    CHECK_EQ(x.result, I2C_TXQ_OK);
    /* Taken 280us late: the computed end. */
    CHECK_EQ(x.done_us, (uint64_t)T0 + 120u);
    CHECK((x.flags & I2C_TXQ_F_LATE_STAMP) != 0u);
}

/* A done callback that hands its transaction back once, not before 1ms. */
static int g_again;
static void again(void *user, i2c_txq_xfer_t *x)
{
    (void)user;
    if (g_again-- > 0) {
        x->not_before_us = fake_us + 1000u;
        x->deadline_us = fake_us + 5000u;
        (void)i2c_txq_submit(&g_bus->q, x);
    }
}

TEST_CASE(core_1s_poll_starts_one_a_callback_queued_during_a_clear)
{
    wired();
    i2c_txq_cfg_t cfg;
    i2c_txq_cfg_default(&cfg);
    cfg.done = again;
    (void)i2c_txq_init(&g_bus->q, &model_ops, &m, &cfg);
    i2c_txq_xfer_t x;
    memset(&x, 0, sizeof(x));
    x.addr = PART;
    x.rd_len = 1;
    x.cls = I2C_TXQ_SOFT;
    x.not_before_us = T0;
    x.deadline_us = T0 + 500u;
    CHECK_EQ(i2c_txq_block_submit(g_bus, &x), I2C_TXQ_E_OK);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    /* The clear cuts it ABORTED; its callback queues it again for 1ms on.
     * The clear's own polls end 115us on. */
    g_again = 1;
    sense_i2c_recover();
    CHECK_EQ(x.state, I2C_TXQ_ST_QUEUED);
    CHECK_EQ(fake_us, (uint64_t)T0 + 115u);
    const uint64_t due = x.not_before_us;
    CHECK_EQ(due, (uint64_t)T0 + 1000u);
    const unsigned starts = m.starts;
    /* Nothing pumps between ticks: the tick's poll starts it, not before
     * its time. */
    fake_us = due - 1u;
    sense_i2c_poll();
    CHECK_EQ(m.starts, starts);
    fake_us = due;
    sense_i2c_poll();
    CHECK_EQ(m.starts, starts + 1u);
    CHECK_EQ(x.state, I2C_TXQ_ST_RUNNING);
    CHECK_EQ(x.start_us, due);
}

TEST_CASE(a_flash_lock_out_mid_transaction_keeps_the_stamp_of_its_end)
{
    uint8_t buf[2];

    /* Parked 2ms from the first pump on: the block ends the transaction
     * on its own at 120us, and the end is taken 1881us late. */
    wired();
    fake_park_us = 2000u;
    CHECK_EQ(sense_i2c_read(NULL, PART, 0x00, buf, 2), SENSE_OK);
    CHECK_EQ(fake_us, (uint64_t)T0 + 2000u);
    CHECK_EQ(sense_i2c_done_us(NULL), (uint64_t)T0 + 120u);
    CHECK_EQ(g_bus->q.st.late_stamps, 1u);
    /* Parked 100us past the end: the time it was taken. */
    wired();
    fake_park_us = 220u;
    CHECK_EQ(sense_i2c_read(NULL, PART, 0x00, buf, 2), SENSE_OK);
    CHECK_EQ(sense_i2c_done_us(NULL), (uint64_t)T0 + 220u);
    CHECK_EQ(g_bus->q.st.late_stamps, 0u);
}

int main(void)
{
    RUN(the_bus_opens_on_one_blocks_pair_and_nothing_else);
    RUN(a_closed_bus_sends_nothing);
    RUN(a_read_and_a_write_go_out_whole_and_are_stamped_at_their_end);
    RUN(pins_nothing_is_wired_to_read_as_nothing_answering);
    RUN(a_line_a_part_holds_is_bus_low_60us_on);
    RUN(scl_held_is_a_timeout_1000us_after_the_start_and_resets_the_block);
    RUN(the_bus_clear_is_waited_for);
    RUN(core_1s_poll_takes_an_end_the_interrupt_left);
    RUN(core_1s_poll_starts_one_a_callback_queued_during_a_clear);
    RUN(a_flash_lock_out_mid_transaction_keeps_the_stamp_of_its_end);
    return test_summary("sense_i2c");
}
