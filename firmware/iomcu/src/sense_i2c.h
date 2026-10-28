/**
 * @file
 * @brief The sensor bus on the RP2350's I2C (Inter-Integrated Circuit)
 * block: the wire under shared/sense, as sense_svc_io_t asks for it.
 *
 * Called from core 1 only.  The bus is an I2C transaction queue
 * (protocols/i2c_txq) on the block of the pins (i2c_txq_block.h), and each
 * transaction goes through shared/sense/sense_txq.h: handed to the queue
 * as a hard one, due now, and waited for with interrupts on.  The block
 * holds the whole transaction, STOP included, in its transmit FIFO, so the
 * flash lock-out (flash_safe_execute() on core 0, which interrupts core 1
 * to park it in RAM) may land during a transaction and the transaction
 * still ends on the wire; its end is stamped in the block's interrupt
 * (I2C0_IRQ 36 or I2C1_IRQ 37, on core 1), or with the computed end when
 * the interrupt came over I2C_TXQ_LATE_US (100us) late.  The schedule
 * stamps each read with that end (sense_i2c_done_us()).
 *
 * The block runs at 400kHz, fast mode, on the SDA and SCL the SENSE page
 * names (GPIO number mod 4 0 or 2 for SDA, SCL the next one; I2C0 or I2C1
 * from (SDA / 2) mod 2, pico-sdk io_bank0.h).
 *
 * Pull-ups.  The pads' own pull-ups and pull-downs are off on both pins.
 * The bus needs pull-ups strong enough for 400kHz -- 300ns of rise,
 * 3.54kOhm at most on 100pF (UM10204) -- and the pad's pull-up is 32 to
 * 86kOhm, a rise of microseconds: it cannot carry the bus, and a bus
 * missing its external pull-ups would run slow and marginal on it rather
 * than fail at its first transaction.  The modules carry pull-ups (the
 * DAOKAI INA3221 10kOhm to VS) and the build guide adds 2.2kOhm to 3V3 at
 * the IOmcu.
 *
 * One use of the pad's pull-up stays: telling a line held low from one
 * nothing holds.  Pins with nothing wired to them -- no part, no pull-up
 * -- read low.  A line low SENSE_I2C_IDLE_US before a start gets the pad's
 * pull-up for up to SENSE_I2C_PROBE_US; one that rises on it is held by
 * nothing, and the pull-ups go off again either way.
 *
 * What a transaction comes to (sense_err_t; the full table in
 * sense_txq.h):
 *
 *   SENSE_BUS_LOW   SDA or SCL low SENSE_I2C_IDLE_US before the start and
 *                   still low on the pad's pull-up after SENSE_I2C_PROBE_US,
 *                   nothing sent; or a lost arbitration after which a line
 *                   is still low SENSE_I2C_IDLE_US on: a part holding the
 *                   bus.
 *   SENSE_TIMEOUT   not finished SENSE_I2C_TIMEOUT_US after its start: SCL
 *                   held low by a part.  The block is reset.
 *   SENSE_NACK      the address or a byte not acknowledged: the bus works
 *                   and nothing answers there.  Also lines low that rise on
 *                   the pad's pull-up: a bus without pull-ups, with nothing
 *                   sent, and no recovery owed (sense_bus.h).
 *
 * The figures below are the queue's (i2c_txq.h); sense_i2c.c asserts each
 * pair equal at build time.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_SENSE_I2C_H
#define RCBENCH_SENSE_I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sense_bus.h"

/**
 * @brief The bus clock: fast mode, the one both parts and the SENSE page
 * take.  I2C_TXQ_HZ.
 */
#define SENSE_I2C_HZ          400000u
/**
 * @brief One transaction's limit, from its start: I2C_TXQ_TIMEOUT_US.
 *
 * The longest, a 6-byte read with its pointer write, is 84 bit times,
 * 210us at 400kHz.
 */
#define SENSE_I2C_TIMEOUT_US    1000u
/**
 * @brief How long the lines may take to come back high after a STOP:
 * I2C_TXQ_SETTLE_US.
 */
#define SENSE_I2C_IDLE_US         10u
/**
 * @brief How long a line low at idle has to rise on the pad's own pull-up,
 * 32 to 86kOhm by the RP2350 datasheet's pad table: 50us is 13 time
 * constants at 86kOhm on 45pF of pin, lead and probe, and a line a part
 * holds low never rises.  I2C_TXQ_PROBE_US.
 *
 * The pin capacitance of a bench lead is not measured.
 */
#define SENSE_I2C_PROBE_US        50u
/**
 * @brief Half a clock of the bus clear, 100kHz: I2C_TXQ_CLEAR_HALF_US.
 */
#define SENSE_I2C_CLEAR_HALF_US    5u
/**
 * @brief How long SCL may be held low by a part during the bus clear,
 * each clock: I2C_TXQ_CLEAR_STRETCH_US.
 */
#define SENSE_I2C_CLEAR_STRETCH_US 100u

/**
 * @brief The block on @p sda and @p scl, its interrupt on this core.
 *
 * False, and nothing touched, when they are not one block's pair on this
 * package, or when the block or its interrupt is taken.  Opening again
 * closes first.
 *
 * @param sda  GPIO number of SDA.
 * @param scl  GPIO number of SCL.
 * @return True when the bus opened.
 */
bool sense_i2c_open(uint8_t sda, uint8_t scl);

/**
 * @brief The queue shut, the block reset, its interrupt given back and its
 * pins back to plain inputs, pulled down as at reset.
 *
 * Nothing when closed.
 */
void sense_i2c_close(void);

/**
 * @brief sense_i2c_t's read: the register pointer, a repeated START and @p n
 * bytes, most significant first.
 *
 * @param ctx  Unused.
 * @param addr  I2C address of the device.
 * @param reg  Register pointer, sent before the repeated START.
 * @param buf  Receives @p n bytes.
 * @param n  Number of bytes, 1 to I2C_TXQ_RD_MAX (6).  Other counts, and
 *           a closed bus, are refused with SENSE_BAD_ARG.
 * @return SENSE_OK, or the error the transaction came to.
 */
sense_err_t sense_i2c_read(void *ctx, uint8_t addr, uint8_t reg,
                           uint8_t *buf, size_t n);

/**
 * @brief sense_i2c_t's write: the register pointer and @p n bytes.
 *
 * @param ctx  Unused.
 * @param addr  I2C address of the device.
 * @param reg  Register pointer, sent before the bytes.
 * @param buf  The bytes to write.
 * @param n  Number of bytes, 1 to 4. Other counts, and a closed bus, are
 *           refused with SENSE_BAD_ARG.
 * @return SENSE_OK, or the error the transaction came to.
 */
sense_err_t sense_i2c_write(void *ctx, uint8_t addr, uint8_t reg,
                            const uint8_t *buf, size_t n);

/**
 * @brief Whether @p addr acknowledges: the address and one byte read.
 *
 * @param addr  I2C address of the device.
 * @return SENSE_OK when the device acknowledges, or the error the transaction
 *         came to.
 */
sense_err_t sense_i2c_ask(uint8_t addr);

/**
 * @brief The bus clear, waited for: the block reset, SCL clocked 9 times as
 * an open-drain GPIO, so a part that was sending a byte when the bus
 * stopped finishes it and lets SDA go; then a STOP (SDA rising while SCL
 * is high); then the block set up again on its pins (NXP UM10204 §3.1.16).
 * 24 half clocks of SENSE_I2C_CLEAR_HALF_US, about 0.12ms, with
 * interrupts on.
 */
void sense_i2c_recover(void);

/**
 * @brief The queue polled: an end the interrupt has not taken, and the
 * start of a transaction that waits.  Core 1 calls it once a tick
 * (SENSE_TICK_US); a transaction a done callback handed to the queue
 * during a flush, an abort or a bus clear starts at this poll at the
 * latest.  Nothing when closed.
 */
void sense_i2c_poll(void);

/**
 * @brief sense_sched_io_t's done_us: the end of the last transaction, on
 * the time_us_64() clock.
 *
 * @param ctx  Unused.
 * @return The last end; the present time before the first and when closed.
 */
uint64_t sense_i2c_done_us(void *ctx);

#endif /* RCBENCH_SENSE_I2C_H */
