/*
 * The sensor bus on the RP2350's I2C (Inter-Integrated Circuit) block: the
 * wire under shared/sense, as sense_svc_io_t asks for it.
 *
 * Called from core 1 only.  Every transaction runs with core 1's
 * interrupts off, so the flash lock-out (flash_safe_execute() on core 0,
 * which interrupts core 1 to park it in RAM) lands between transactions
 * and never stops one half way with SCL held low.  The longest such
 * stretch is one transaction's timeout, SENSE_I2C_TIMEOUT_US, plus the
 * block's re-initialisation; a bus clear is about 0.12 ms.
 *
 * The block runs at 400 kHz, fast mode, on the SDA and SCL the SENSE page
 * names (GPIO number mod 4 0 or 2 for SDA, SCL the next one; I2C0 or I2C1
 * from (SDA / 2) mod 2, pico-sdk io_bank0.h).
 *
 * Pull-ups.  The pads' own pull-ups and pull-downs are switched off on
 * both pins.  The bus needs pull-ups strong enough for 400 kHz -- 300 ns
 * of rise, 3.54 kOhm at most on 100 pF (UM10204) -- and the pad's pull-up
 * is tens of kOhm (not measured here), a rise of microseconds: it cannot
 * carry the bus, and a bus missing its external pull-ups would run slow
 * and marginal on it rather than fail at its first transaction.  The
 * pad's pull-down, on at reset, would only fight the external pull-ups.
 * The modules carry pull-ups (the DAOKAI INA3221 10 kOhm to VS) and the
 * build guide adds 2.2 kOhm to 3V3 at the IOmcu.
 *
 * One use of the pad's pull-up stays: telling a line held low from one
 * nothing holds.  Pins with nothing wired to them -- no part, no pull-up
 * -- read low.  A line low at idle gets the pad's pull-up for up to
 * SENSE_I2C_PROBE_US; one that rises on it is held by nothing, and the
 * pull-ups go off again either way.
 *
 * What a transaction comes to (sense_err_t):
 *
 *   SENSE_BUS_LOW   SDA or SCL low with the bus idle, before anything is
 *                   sent, or a failed transaction after which a line stays
 *                   low for SENSE_I2C_IDLE_US, and still low on the pad's
 *                   pull-up after SENSE_I2C_PROBE_US: a part holding the
 *                   bus.
 *   SENSE_TIMEOUT   not finished in SENSE_I2C_TIMEOUT_US, measured from
 *                   its start: SCL held low by a part.
 *   SENSE_NACK      the address or a byte not acknowledged, the lines back
 *                   high: the bus works and nothing answers there.  Also
 *                   lines low at idle that rise on the pad's pull-up: a
 *                   bus without pull-ups, with nothing sent, and no
 *                   recovery owed (sense_bus.h); the parts read as not
 *                   answering.
 *
 * After any failure the block is reset and set up again, so the next
 * transaction starts from a known state; the pins keep their function.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_SENSE_I2C_H
#define RCBENCH_SENSE_I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sense_bus.h"

/** The bus clock: fast mode, the one both parts and the SENSE page take. */
#define SENSE_I2C_HZ          400000u
/** One transaction's limit.  The longest, a 5-byte read with its pointer
 *  write, is 75 bit times, 188 us at 400 kHz. */
#define SENSE_I2C_TIMEOUT_US    1000u
/** How long the lines may take to come back high after a STOP. */
#define SENSE_I2C_IDLE_US         10u
/** How long a line low at idle has to rise on the pad's own pull-up, 32 to
 *  86 kOhm by the RP2350 datasheet's pad table: 50 us is 13 time constants
 *  at 86 kOhm on 45 pF of pin, lead and probe, and a line a part holds low
 *  never rises.  The pin capacitance of a bench lead is not measured. */
#define SENSE_I2C_PROBE_US        50u
/** Half a clock of the bus clear: 100 kHz. */
#define SENSE_I2C_CLEAR_HALF_US    5u
/** How long SCL may be held low by a part during the bus clear, each
 *  clock. */
#define SENSE_I2C_CLEAR_STRETCH_US 100u

/** The block on @p sda and @p scl.  False, and nothing touched, when they
 *  are not one block's pair on this package.  Opening again closes
 *  first. */
bool sense_i2c_open(uint8_t sda, uint8_t scl);

/** The block reset and its pins back to plain inputs, pulled down as at
 *  reset.  Nothing when closed. */
void sense_i2c_close(void);

/** sense_i2c_t's read: the register pointer, a repeated START and @p n
 *  bytes, most significant first. */
sense_err_t sense_i2c_read(void *ctx, uint8_t addr, uint8_t reg,
                           uint8_t *buf, size_t n);

/** sense_i2c_t's write: the register pointer and @p n bytes. */
sense_err_t sense_i2c_write(void *ctx, uint8_t addr, uint8_t reg,
                            const uint8_t *buf, size_t n);

/** Whether @p addr acknowledges: the address and one byte read. */
sense_err_t sense_i2c_ask(uint8_t addr);

/**
 * The bus clear: the block reset, SCL clocked 9 times as an open-drain
 * GPIO, so a part that was sending a byte when the bus stopped finishes
 * it and lets SDA go; then a STOP (SDA rising while SCL is high); then the
 * block set up again on its pins (NXP UM10204 §3.1.16).
 */
void sense_i2c_recover(void);

#endif /* RCBENCH_SENSE_I2C_H */
