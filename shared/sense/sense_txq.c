/**
 * @file
 * @brief The sensor bus on the I2C transaction queue.
 *
 * See sense_txq.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_txq.h"

#include <string.h>

void sense_txq_init(sense_txq_t *t, const sense_txq_io_t *io)
{
    memset(t, 0, sizeof(*t));
    t->io = *io;
}

/**
 * @brief The queue's clock.
 *
 * @param t  The wire.
 * @return The present time, in us.
 */
static uint64_t now(const sense_txq_t *t)
{
    return t->io.now_us(t->io.ctx);
}

sense_err_t sense_txq_err(unsigned r)
{
    switch (r) {
    case I2C_TXQ_OK:        return SENSE_OK;
    case I2C_TXQ_NACK_ADDR:
    case I2C_TXQ_NACK_DATA:
    case I2C_TXQ_NO_PULLUP:
    case I2C_TXQ_FAULT:
    case I2C_TXQ_ARB_LOST:  return SENSE_NACK;
    case I2C_TXQ_BUS_LOW:   return SENSE_BUS_LOW;
    default:                return SENSE_TIMEOUT;
    }
}

/**
 * @brief After a lost arbitration: both lines high within
 *        SENSE_TXQ_SETTLE_US, a NACK; a line still low, a part holds it.
 *
 * @param t  The wire.
 * @return SENSE_NACK or SENSE_BUS_LOW.
 */
static sense_err_t after_arb_lost(sense_txq_t *t)
{
    const uint64_t t0 = now(t);

    for (;;) {
        if (i2c_txq_lines_high(t->io.queue(t->io.ctx))) {
            return SENSE_NACK;
        }
        if (now(t) - t0 >= SENSE_TXQ_SETTLE_US) {
            return SENSE_BUS_LOW;
        }
        t->io.pump(t->io.ctx);
    }
}

/**
 * @brief The transaction in t->x handed to the queue and waited for.
 *
 * @param t  The wire.
 * @return What it came to.
 */
static sense_err_t run(sense_txq_t *t)
{
    i2c_txq_xfer_t *x = &t->x;
    const uint64_t t0 = now(t);

    x->cls = I2C_TXQ_HARD;
    x->not_before_us = t0;
    x->deadline_us = t0;
    const i2c_txq_err_t e = t->io.submit(t->io.ctx, x);
    if (e == I2C_TXQ_E_BUSY || e == I2C_TXQ_E_FULL) {
        return SENSE_TIMEOUT;           /* held, or no room: none sent */
    }
    if (e != I2C_TXQ_E_OK) {
        return SENSE_BAD_ARG;           /* refused, or the queue shut */
    }
    t->transactions++;
    bool asked = false;
    for (;;) {
        if (x->state == I2C_TXQ_ST_DONE) {
            break;
        }
        const uint64_t waited = now(t) - t0;
        if (!asked && waited >= SENSE_TXQ_WAIT_US) {
            asked = true;
            t->overruns++;
            (void)t->io.abort(t->io.ctx, x);
        } else if (waited >= 2u * (uint64_t)SENSE_TXQ_WAIT_US) {
            return SENSE_TIMEOUT;
        }
        t->io.pump(t->io.ctx);
    }
    const uint64_t waited = now(t) - t0;
    if (waited > t->wait_max_us) {
        t->wait_max_us = waited > UINT32_MAX ? UINT32_MAX : (uint32_t)waited;
    }
    t->have_done = true;
    t->done_us = x->done_us;
    if (x->result == I2C_TXQ_ARB_LOST) {
        return after_arb_lost(t);
    }
    return sense_txq_err(x->result);
}

/**
 * @brief Whether the wire's descriptor may take a new transaction: not
 *        while the queue holds it.
 *
 * @param t  The wire.
 * @return True when it is free.
 */
static bool free_x(const sense_txq_t *t)
{
    return t->x.state != I2C_TXQ_ST_QUEUED
           && t->x.state != I2C_TXQ_ST_RUNNING;
}

/**
 * @brief Fill the descriptor for a new transaction, keeping its state.
 *
 * @param t     The wire.
 * @param addr  I2C address.
 */
static void fresh(sense_txq_t *t, uint8_t addr)
{
    const uint8_t state = t->x.state;

    memset(&t->x, 0, sizeof(t->x));
    t->x.state = state;
    t->x.addr = addr;
}

sense_err_t sense_txq_read(void *ctx, uint8_t addr, uint8_t reg,
                           uint8_t *buf, size_t n)
{
    sense_txq_t *t = ctx;

    if (n == 0u || n > I2C_TXQ_RD_MAX) {
        return SENSE_BAD_ARG;
    }
    if (!free_x(t)) {
        return SENSE_TIMEOUT;
    }
    fresh(t, addr);
    t->x.wr[0] = reg;
    t->x.wr_len = 1;
    t->x.rd_len = (uint8_t)n;
    const sense_err_t e = run(t);
    if (e == SENSE_OK) {
        memcpy(buf, t->x.rd, n);
    }
    return e;
}

sense_err_t sense_txq_write(void *ctx, uint8_t addr, uint8_t reg,
                            const uint8_t *buf, size_t n)
{
    sense_txq_t *t = ctx;

    if (n == 0u || n > I2C_TXQ_WR_MAX - 1u) {
        return SENSE_BAD_ARG;
    }
    if (!free_x(t)) {
        return SENSE_TIMEOUT;
    }
    fresh(t, addr);
    t->x.wr[0] = reg;
    memcpy(&t->x.wr[1], buf, n);
    t->x.wr_len = (uint8_t)(n + 1u);
    return run(t);
}

sense_err_t sense_txq_ask(sense_txq_t *t, uint8_t addr)
{
    if (!free_x(t)) {
        return SENSE_TIMEOUT;
    }
    fresh(t, addr);
    t->x.rd_len = 1;
    return run(t);
}

void sense_txq_recover(sense_txq_t *t)
{
    if (!t->io.recover(t->io.ctx)) {
        return;
    }
    const uint64_t t0 = now(t);
    while (i2c_txq_clearing(t->io.queue(t->io.ctx))
           && now(t) - t0 < 2u * (uint64_t)SENSE_TXQ_WAIT_US) {
        t->io.pump(t->io.ctx);
    }
}

uint64_t sense_txq_done_us(void *ctx)
{
    const sense_txq_t *t = ctx;

    return t->have_done ? t->done_us : now(t);
}
