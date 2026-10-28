/**
 * @file
 * @brief The sensor bus on the RP2350's I2C block, through the I2C
 *        transaction queue.
 *
 * See sense_i2c.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_i2c.h"

#include "pico/stdlib.h"

#include "i2c_txq.h"
#include "i2c_txq_block.h"
#include "link_pages.h"
#include "sense_txq.h"

/* The sensor bus's timing is the queue's: each figure sense_i2c.h names is
 * the one the queue runs on. */
_Static_assert(SENSE_I2C_HZ == I2C_TXQ_HZ,
               "the sensor bus clock is the queue's");
_Static_assert(SENSE_I2C_TIMEOUT_US == I2C_TXQ_TIMEOUT_US,
               "one transaction's limit is the queue's timeout");
_Static_assert(SENSE_I2C_IDLE_US == I2C_TXQ_SETTLE_US,
               "the lines' time to come back high is the queue's settle");
_Static_assert(SENSE_I2C_PROBE_US == I2C_TXQ_PROBE_US,
               "the pad pull-up probe is the queue's");
_Static_assert(SENSE_I2C_CLEAR_HALF_US == I2C_TXQ_CLEAR_HALF_US,
               "the bus clear's half clock is the queue's");
_Static_assert(SENSE_I2C_CLEAR_STRETCH_US == I2C_TXQ_CLEAR_STRETCH_US,
               "the bus clear's stretch limit is the queue's");
_Static_assert(SENSE_TXQ_SETTLE_US == SENSE_I2C_IDLE_US,
               "a lost arbitration gets the lines' idle time");

static i2c_txq_block_t s_bus;   /**< the queue on the block of the pins */
static sense_txq_t     s_wire;  /**< the wire the drivers call through   */
static bool            s_init;  /**< s_bus set up by i2c_txq_block_init() */
static bool            s_open;  /**< between a good open and the close    */

/**
 * @brief sense_txq_io_t's submit: on the block, started on an idle bus.
 *
 * @param ctx  Unused.
 * @param x    The transaction.
 * @return What i2c_txq_block_submit() answers.
 */
static i2c_txq_err_t io_submit(void *ctx, i2c_txq_xfer_t *x)
{
    (void)ctx;
    return i2c_txq_block_submit(&s_bus, x);
}

/**
 * @brief sense_txq_io_t's pump: the end the interrupt has not taken, then
 *        a poll.
 *
 * @param ctx  Unused.
 */
static void io_pump(void *ctx)
{
    (void)ctx;
    tight_loop_contents();
    i2c_txq_block_poll(&s_bus);
}

/**
 * @brief sense_txq_io_t's abort.
 *
 * @param ctx  Unused.
 * @param x    The transaction.
 * @return What i2c_txq_block_abort() answers.
 */
static bool io_abort(void *ctx, i2c_txq_xfer_t *x)
{
    (void)ctx;
    return i2c_txq_block_abort(&s_bus, x);
}

/**
 * @brief sense_txq_io_t's recover.
 *
 * @param ctx  Unused.
 * @return What i2c_txq_block_recover() answers.
 */
static bool io_recover(void *ctx)
{
    (void)ctx;
    return i2c_txq_block_recover(&s_bus);
}

/**
 * @brief sense_txq_io_t's queue.
 *
 * @param ctx  Unused.
 * @return The block's queue.
 */
static const i2c_txq_t *io_queue(void *ctx)
{
    (void)ctx;
    return i2c_txq_block_queue(&s_bus);
}

/**
 * @brief sense_txq_io_t's clock: the 64-bit microsecond timer.
 *
 * @param ctx  Unused.
 * @return The present time, in us.
 */
static uint64_t io_now(void *ctx)
{
    (void)ctx;
    return time_us_64();
}

bool sense_i2c_open(uint8_t sda, uint8_t scl)
{
    if (!s_init) {
        i2c_txq_block_init(&s_bus);
        s_init = true;
    }
    sense_i2c_close();
    if (sda >= NUM_BANK0_GPIOS || scl >= NUM_BANK0_GPIOS
        || !link_sn_pins_pair(sda, scl)) {
        return false;
    }
    if (i2c_txq_block_open(&s_bus, sda, scl, NULL) != I2C_TXQ_BLOCK_OK) {
        return false;
    }
    const sense_txq_io_t io = {
        io_submit, io_pump, io_abort, io_recover, io_queue, io_now, NULL,
    };
    sense_txq_init(&s_wire, &io);
    s_open = true;
    return true;
}

void sense_i2c_close(void)
{
    if (!s_open) {
        return;
    }
    i2c_txq_block_close(&s_bus);
    s_open = false;
}

sense_err_t sense_i2c_read(void *ctx, uint8_t addr, uint8_t reg,
                           uint8_t *buf, size_t n)
{
    (void)ctx;
    if (!s_open) {
        return SENSE_BAD_ARG;
    }
    return sense_txq_read(&s_wire, addr, reg, buf, n);
}

sense_err_t sense_i2c_write(void *ctx, uint8_t addr, uint8_t reg,
                            const uint8_t *buf, size_t n)
{
    (void)ctx;
    if (!s_open) {
        return SENSE_BAD_ARG;
    }
    return sense_txq_write(&s_wire, addr, reg, buf, n);
}

sense_err_t sense_i2c_ask(uint8_t addr)
{
    if (!s_open) {
        return SENSE_BAD_ARG;
    }
    return sense_txq_ask(&s_wire, addr);
}

void sense_i2c_recover(void)
{
    if (s_open) {
        sense_txq_recover(&s_wire);
    }
}

void sense_i2c_poll(void)
{
    if (s_open) {
        i2c_txq_block_poll(&s_bus);
    }
}

uint64_t sense_i2c_done_us(void *ctx)
{
    (void)ctx;
    return s_open ? sense_txq_done_us(&s_wire) : time_us_64();
}
