/**
 * @file
 * @brief The sensor bus on the I2C (Inter-Integrated Circuit) transaction
 * queue: sense_i2c_t's register read and write, the address ask of the
 * scan and the bus clear, each one transaction handed to an i2c_txq queue
 * (protocols/i2c_txq) and waited for.
 *
 * The drivers and the schedule above (ina228.h, ina3221.h, as5600.h,
 * sense_sched.h) take a read or a write as a call that returns with its
 * result, and keep doing so: this module is that call.  It hands the
 * transaction to the queue as a hard one, due now, and pumps the queue's
 * driver until the transaction has ended.  The wait runs with interrupts
 * on.  The transaction is in the controller whole, STOP included, so a
 * flash lock-out that parks the core in the wait does not stop it on the
 * wire; the end is stamped by the driver's interrupt, or with the computed
 * end when the report came over I2C_TXQ_LATE_US late (i2c_txq.h).
 *
 * What a transaction comes to (sense_err_t), from the queue's result:
 *
 *   I2C_TXQ_OK                 SENSE_OK
 *   I2C_TXQ_NACK_ADDR, _DATA   SENSE_NACK
 *   I2C_TXQ_NO_PULLUP          SENSE_NACK: nothing sent, no recovery owed
 *   I2C_TXQ_FAULT              SENSE_NACK: a byte count short, or a cause
 *                              the queue has no name for
 *   I2C_TXQ_BUS_LOW            SENSE_BUS_LOW
 *   I2C_TXQ_ARB_LOST           SENSE_NACK when both lines are high within
 *                              SENSE_TXQ_SETTLE_US of the end, SENSE_BUS_LOW
 *                              when one is still low
 *   I2C_TXQ_TIMEOUT            SENSE_TIMEOUT
 *   I2C_TXQ_DROPPED, _ABORTED  SENSE_TIMEOUT: not met with a hard
 *                              transaction the module waits for
 *
 * A transaction the queue does not take ends at once with nothing sent:
 * SENSE_TIMEOUT when the queue holds I2C_TXQ_DEPTH (16) others, or still
 * holds this module's last one; SENSE_BAD_ARG when it refuses the
 * transaction or is shut.
 *
 * A wait longer than SENSE_TXQ_WAIT_US asks the queue to abort the
 * transaction; one twice that long ends as SENSE_TIMEOUT and leaves the
 * descriptor with the queue.  Until the queue gives it back -- its end, or
 * the cut of a bus clear -- every transaction ends as SENSE_TIMEOUT, so
 * sense_bus marks the bus stuck and runs the clear.  The queue's own
 * timeout, I2C_TXQ_TIMEOUT_US, ends every transaction on the wire first
 * while the driver is pumped.
 *
 * Pure C, no SDK (software development kit).  Host-tested in
 * test_sense_txq and test_sense_txq_svc over the modelled bus of
 * test/host/i2c_txq_model.c.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "i2c_txq.h"
#include "sense_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A wait past this asks for an abort, in us; twice it ends. */
#define SENSE_TXQ_WAIT_US  5000u
/**
 * @brief After a lost arbitration, the lines get this long to come back
 *        high, in us: sense_i2c.h's SENSE_I2C_IDLE_US.
 */
#define SENSE_TXQ_SETTLE_US  10u

/** @brief The queue's driver, as the module reaches it. */
typedef struct {
    /** @brief i2c_txq_submit() and a poll: the start on an idle bus. */
    i2c_txq_err_t   (*submit)(void *ctx, i2c_txq_xfer_t *x);
    /** @brief The driver's end of a transaction taken, then a poll. */
    void            (*pump)(void *ctx);
    /** @brief i2c_txq_abort() of @p x at the present time. */
    bool            (*abort)(void *ctx, i2c_txq_xfer_t *x);
    /** @brief i2c_txq_recover() at the present time. */
    bool            (*recover)(void *ctx);
    /** @brief The queue, for its state. */
    const i2c_txq_t *(*queue)(void *ctx);
    /** @brief The queue's clock, in us. */
    uint64_t        (*now_us)(void *ctx);
    void            *ctx;            /**< handed to each function     */
} sense_txq_io_t;

/** @brief The wire: one transaction at a time, and what the waits took. */
typedef struct {
    sense_txq_io_t io;         /**< the queue's driver                 */
    i2c_txq_xfer_t x;          /**< the transaction under way or last  */
    bool     have_done;        /**< done_us holds an end               */
    uint64_t done_us;          /**< the last transaction's end, us     */
    uint32_t transactions;     /**< handed to the queue, mod 2^32      */
    uint32_t overruns;         /**< waits past SENSE_TXQ_WAIT_US       */
    uint32_t wait_max_us;      /**< the longest wait for one, us       */
} sense_txq_t;

/**
 * @brief Set up the wire over a queue's driver.
 *
 * @param t   The wire.
 * @param io  The driver; copied.
 */
void sense_txq_init(sense_txq_t *t, const sense_txq_io_t *io);

/**
 * @brief sense_i2c_t's read: the register pointer, a repeated START and
 *        @p n bytes.
 *
 * @param ctx   The sense_txq_t.
 * @param addr  I2C address of the device.
 * @param reg   Register pointer.
 * @param buf   Receives @p n bytes on SENSE_OK.
 * @param n     1 to I2C_TXQ_RD_MAX; others SENSE_BAD_ARG, nothing sent.
 * @return What the transaction came to.
 */
sense_err_t sense_txq_read(void *ctx, uint8_t addr, uint8_t reg,
                           uint8_t *buf, size_t n);

/**
 * @brief sense_i2c_t's write: the register pointer and @p n bytes.
 *
 * @param ctx   The sense_txq_t.
 * @param addr  I2C address of the device.
 * @param reg   Register pointer.
 * @param buf   The bytes.
 * @param n     1 to I2C_TXQ_WR_MAX - 1; others SENSE_BAD_ARG.
 * @return What the transaction came to.
 */
sense_err_t sense_txq_write(void *ctx, uint8_t addr, uint8_t reg,
                            const uint8_t *buf, size_t n);

/**
 * @brief Whether @p addr acknowledges: the address and one byte read.
 *
 * @param t     The wire.
 * @param addr  I2C address.
 * @return What the transaction came to.
 */
sense_err_t sense_txq_ask(sense_txq_t *t, uint8_t addr);

/**
 * @brief The bus clear (i2c_txq_recover()), waited for.  Nothing on a bus
 *        whose queue refuses it.
 *
 * @param t  The wire.
 */
void sense_txq_recover(sense_txq_t *t);

/**
 * @brief When the last transaction ended, on the queue's clock: the time
 *        the schedule stamps a read with (sense_sched_io_t.done_us).
 *
 * @param ctx  The sense_txq_t.
 * @return The last end; the present time before the first.
 */
uint64_t sense_txq_done_us(void *ctx);

/**
 * @brief The sense_err_t of a queue result, I2C_TXQ_ARB_LOST taken with
 *        the lines high.
 *
 * @param r  An i2c_txq_res_t.
 * @return The table above; SENSE_TIMEOUT for a value out of range.
 */
sense_err_t sense_txq_err(unsigned r);

#ifdef __cplusplus
}
#endif
