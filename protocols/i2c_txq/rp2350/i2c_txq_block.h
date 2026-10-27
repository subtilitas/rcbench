/**
 * @file
 * @brief The I2C (Inter-Integrated Circuit) queue on one of the RP2350's two
 *        I2C blocks: an i2c_txq_ops_t made of the block, its interrupt and
 *        its two pins.
 *
 * Resources.  i2c_txq_block_open() takes the block the pins belong to
 * (I2C0 or I2C1, from (SDA / 2) mod 2), its interrupt (I2C0_IRQ 36 or
 * I2C1_IRQ 37) through an exclusive handler on the calling core, and the
 * two pins.  i2c_txq_block_close() gives all of them back.  No DMA (direct
 * memory access) channel and no PIO (programmable input/output) state
 * machine is used.
 *
 * Pins.  SDA is an even GPIO (general-purpose input/output) and SCL the
 * next one, below NUM_BANK0_GPIOS.  The pads' pull-ups and pull-downs are
 * off while the bus is open: the bus needs external pull-ups (3.54kOhm at
 * most for 300ns of rise on 100pF at 400kHz, NXP UM10204).  The pull-ups
 * go on only for the queue's probe of a line low before a start.  The
 * close leaves both pads inputs with the pull-down on, as at reset.
 *
 * A transaction.  Its address goes to IC_TAR with the block disabled and
 * enabled again, as pico-sdk 2.3.0 i2c.c does (I2C_DYNAMIC_TAR_UPDATE is 0
 * on the RP2350), only when it differs from the last one.  Then every byte
 * goes to the 16-entry transmit FIFO, the reads as read commands, the first
 * read after a write with a RESTART and the last entry with the STOP, and
 * the interrupt is unmasked for STOP_DET and TX_ABRT.  The FIFO load is at
 * most 2 register writes for the address and 11 for the FIFO, with the
 * core's interrupts off and no wait: an interrupt between two entries
 * would let the FIFO run empty without a STOP, and the block would hold
 * SCL low until the next entry.  From the load on, the block finishes the
 * transaction without the CPU.  The interrupt reports the end after the
 * STOP: on STOP_DET, or on TX_ABRT once an abort asked for is done.  The
 * end is stamped with time_us_64() in the interrupt.
 *
 * Contexts.  Open, close, submit, abort, recover, hold and poll are called
 * from the core that opened the bus; they mask the block's interrupt on
 * that core while they work on the queue, and the done callback runs there
 * or in the interrupt.  Nothing waits with interrupts off.  The bus clear
 * is taken one half clock per poll: at a 1ms poll it takes 24 polls; at a
 * poll every 5us it takes about 120us.
 *
 * The system clock must not change while a bus is open.
 *
 * Not measured: this driver has not run on a chip.  The time the TAR
 * change takes, the gap the block leaves between a STOP and the next
 * START and the interrupt latency are not measured.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef I2C_TXQ_BLOCK_H
#define I2C_TXQ_BLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "i2c_txq.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Error codes of the RP2350 I2C block driver. */
typedef enum {
    I2C_TXQ_BLOCK_OK = 0,       /**< no error                                */
    I2C_TXQ_BLOCK_ERR_ARG,      /**< no bus object, or a bus clock of 0      */
    I2C_TXQ_BLOCK_ERR_PINS,     /**< not an even SDA with SCL the next pin,
                                     or a pin the chip does not have         */
    I2C_TXQ_BLOCK_ERR_BUSY,     /**< the block or its interrupt is taken     */
} i2c_txq_block_err_t;

/**
 * @brief One bus.
 *
 * The caller keeps the object; the fields are the driver's.
 */
typedef struct {
    i2c_txq_t q;        /**< the queue                                    */
    void     *hw;       /**< the block's i2c_inst_t                        */
    uint32_t  abrt;     /**< abort causes seen in this transaction         */
    uint32_t  irq;      /**< the block's interrupt number                  */
    uint8_t   sda;      /**< SDA's GPIO number                             */
    uint8_t   scl;      /**< SCL's GPIO number                             */
    uint8_t   block;    /**< 0 or 1                                        */
    uint8_t   tar;      /**< the address in IC_TAR; 0xFF for none          */
    bool      open;     /**< between a good open and the close             */
    bool      stopped;  /**< STOP_DET seen in this transaction             */
    bool      busy;     /**< a transaction is on the block                 */
} i2c_txq_block_t;

/**
 * @brief Mark @p b closed.
 *
 * Call once, before the first open.
 *
 * @param b  Bus object.
 */
void i2c_txq_block_init(i2c_txq_block_t *b);

/**
 * @brief Take the block of @p sda and @p scl, its interrupt and the pins,
 *        and set up an empty queue on them.
 *
 * The caller has stopped every other use of the block and the pins.  On an
 * error nothing stays taken and no pin is touched.  An open bus is closed
 * first.
 *
 * @param b    Bus object.
 * @param sda  SDA's GPIO number: even.
 * @param scl  SCL's GPIO number: @p sda + 1.
 * @param cfg  The queue's set-up; NULL for i2c_txq_cfg_default().  Its bus
 *             clock is set on the block, and the queue keeps the clock the
 *             block's divider gives.
 * @return I2C_TXQ_BLOCK_OK, or the error that refused the open.
 */
i2c_txq_block_err_t i2c_txq_block_open(i2c_txq_block_t *b, unsigned sda,
                                       unsigned scl, const i2c_txq_cfg_t *cfg);

/**
 * @brief End everything I2C_TXQ_ABORTED (i2c_txq_flush()), reset the block,
 *        give back the interrupt and leave both pads inputs with the
 *        pull-down on.
 *
 * Safe on a closed bus.
 *
 * @param b  Bus object.
 */
void i2c_txq_block_close(i2c_txq_block_t *b);

/**
 * @brief i2c_txq_submit(), then a poll, so a transaction on an idle bus
 *        starts at once.
 *
 * @param b  Bus object.
 * @param x  The transaction.
 * @return What i2c_txq_submit() answers; I2C_TXQ_E_ARG on a closed bus.
 */
i2c_txq_err_t i2c_txq_block_submit(i2c_txq_block_t *b, i2c_txq_xfer_t *x);

/**
 * @brief i2c_txq_abort() at the present time.
 *
 * @param b  Bus object.
 * @param x  The transaction.
 * @return What i2c_txq_abort() answers; false on a closed bus.
 */
bool i2c_txq_block_abort(i2c_txq_block_t *b, i2c_txq_xfer_t *x);

/**
 * @brief i2c_txq_recover() at the present time.
 *
 * @param b  Bus object.
 * @return What i2c_txq_recover() answers; false on a closed bus.
 */
bool i2c_txq_block_recover(i2c_txq_block_t *b);

/**
 * @brief i2c_txq_hold().
 *
 * @param b   Bus object.
 * @param on  True to hold.
 */
void i2c_txq_block_hold(i2c_txq_block_t *b, bool on);

/**
 * @brief The end of a transaction the interrupt has not taken yet, then
 *        i2c_txq_poll() at the present time.
 *
 * @param b  Bus object.
 */
void i2c_txq_block_poll(i2c_txq_block_t *b);

/**
 * @brief The queue, for its counters and state.
 *
 * Read it from the core that opened the bus.
 *
 * @param b  Bus object.
 * @return The queue; NULL for NULL.
 */
const i2c_txq_t *i2c_txq_block_queue(const i2c_txq_block_t *b);

#ifdef __cplusplus
}
#endif

#endif /* I2C_TXQ_BLOCK_H */
