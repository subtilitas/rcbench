/*
 * The SENSE and SERVO_SENSE link pages at the coprocessor: the I2C bus of
 * the two current monitors, its set-up and what they last read, and the
 * servo rail's channels with a move capture.  The registers are in
 * link_pages.h (LINK_SN_*, LINK_SS_*).
 *
 * The set-up is refused while the bank is armed, on pins that are not one
 * I2C block's SDA and SCL, and on pins the board, an output or the SUPPLY
 * page already holds; the pins it takes are reserved from the outputs for
 * as long as either part is enabled (sense_page_pins()).  A capture arms
 * only on an armed bank, on INA3221 CH1 while the INA3221 reads it, and
 * for an output channel that is a surface on a PWM slot.
 *
 * Host-tested.  The page holds the contract and the checks; reading the
 * parts and filling the read-only registers is the coprocessor's, and
 * until it does they read 0, FLAGS included: no bus open.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "outputs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The set-up before anything is written: both parts off; GP16 and GP17,
 * I2C0's SDA and SCL, outside GP0 to GP7 where the guides put outputs;
 * 400 kHz; the MATEK I2C-INA-BM's INA228 at 0x45 on its 200 uOhm shunt,
 * ranged for 204.8 A; the DAOKAI INA3221 at 0x40 on its 0.1 Ohm shunts
 * (1.638 A full scale), reading CH1.
 */
#define SENSE_DEFAULT_SDA        16u
#define SENSE_DEFAULT_SCL        17u
#define SENSE_DEFAULT_KHZ        LINK_SN_KHZ_BUS
#define SENSE_DEFAULT_I228_ADDR  0x45u
#define SENSE_DEFAULT_I228_UOHM  200u
#define SENSE_DEFAULT_I228_DA    2048u
#define SENSE_DEFAULT_I3221_ADDR 0x40u
#define SENSE_DEFAULT_I3221_DMOHM 1000u
#define SENSE_DEFAULT_I3221_CH   0x01u

/** A capture armed before anything else is written: 100 mA of movement and
 *  a 50 mA band, the servo test's SERVO_TEST_MOVE_A and SERVO_TEST_BAND_A. */
#define SENSE_DEFAULT_MOVE_MA 100u
#define SENSE_DEFAULT_BAND_MA  50u

typedef struct {
    uint16_t sense[LINK_SN_COUNT];   /**< the SENSE page          */
    uint16_t servo[LINK_SS_COUNT];   /**< the SERVO_SENSE page    */
} sense_page_t;

void sense_page_init(sense_page_t *p);

/** The set-up as a page starts, into @p cfg (LINK_SN_CONFIG_COUNT
 *  registers): what a store record from before protocol 4.7 restores. */
void sense_page_defaults(uint16_t *cfg);

/**
 * The INA3221's full scale in mA for a shunt of @p shunt_dmohm tenths of a
 * milliohm: 163.8 mV across it.  1638 at 0.1 Ohm.  0 for no shunt.
 */
uint32_t sense_i3221_full_scale_ma(uint16_t shunt_dmohm);

/**
 * A SENSE write, validated whole before any of it is stored.  Refused: off
 * the page (BAD_RANGE); a read-only register (READ_ONLY); a value out of
 * its range, a reserved register written other than 0, SDA and SCL not
 * one I2C block's pair while a part is enabled, a pin past the bank,
 * reserved, bound to an output or in @p taken, the two parts on one
 * address while both are enabled, the INA3221 enabled with no channel, an
 * INA228 shunt and maximum ina228_calibrate() refuses (past 163.84 mV
 * across the shunt, or a full scale past INA228_FS_MA_LIMIT at the range
 * it chooses), and any change while @p o is driving (BAD_VALUE).  A write
 * of the set-up in force is taken armed or not.
 *
 * The INA228's calibration is the driver's, ina228_calibrate(), so the
 * page refuses exactly the set-ups the driver could not run.
 *
 * @p taken is the pins another page holds: the SUPPLY page's,
 * supply_page_pins().  The bank's reservation covers them on the
 * coprocessor as well; they are named here so the page refuses them by
 * itself.
 */
uint8_t sense_page_write(sense_page_t *p, uint8_t off, uint8_t n,
                         const uint16_t *in, const outputs_t *o,
                         uint64_t taken);

void sense_page_read(const sense_page_t *p, uint8_t off, uint8_t n,
                     uint16_t *out);

/**
 * A SERVO_SENSE write.  CAP_ARM written 0 at the head of the write
 * disarms, is never refused, and stores nothing else of the write.  Any
 * other write is an arm, LINK_SS_CAP_FRAME registers from CAP_ARM.
 * Refused: off the page (BAD_RANGE); a read-only register (READ_ONLY);
 * not the whole frame, CAP_ARM with bits it does not have, an INA3221
 * channel that is not 1 (LINK_SS_CAP_CH) or not read, an output channel
 * that is not a surface on a PWM slot, a level past 32767 mA, a movement or band of 0 or
 * past 32767 mA (BAD_VALUE); an arm while @p o is not driving (NOT_ARMED).
 * An arm restarts the capture: CAP_STATE armed, the results 0.
 */
uint8_t sense_servo_write(sense_page_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in, const outputs_t *o);

void sense_servo_read(const sense_page_t *p, uint8_t off, uint8_t n,
                      uint16_t *out);

/**
 * One pass: a bank that is not @p driving ends a capture that has not
 * finished -- CAP_ARM 0, CAP_STATE idle.  A finished capture keeps its
 * result until the next arm.
 */
void sense_page_step(sense_page_t *p, bool driving);

/** Whether either part is enabled, and so the bus runs. */
bool sense_page_enabled(const sense_page_t *p);

/** The bus's pins and clock. */
uint8_t  sense_page_sda(const sense_page_t *p);
uint8_t  sense_page_scl(const sense_page_t *p);
uint32_t sense_page_hz(const sense_page_t *p);

/** The pins it holds, as a reservation mask; 0 while neither part is
 *  enabled. */
uint64_t sense_page_pins(const sense_page_t *p);

/** LINK_NACK_BAD_VALUE for an OUTPUTS page (@p slots, LINK_OS_COUNT
 *  registers) that binds a slot to a pin the bus holds; 0 otherwise. */
uint8_t sense_page_slots_check(const sense_page_t *p, const uint16_t *slots);

#ifdef __cplusplus
}
#endif
