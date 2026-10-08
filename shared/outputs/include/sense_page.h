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
 * Host-tested.  The page holds the contract and the checks.  Reading the
 * parts is core 1's (sense_svc.h); the page turns core 0's view of the
 * page into the order core 1 runs (sense_page_cmd()), and what core 1
 * read into the read-only registers (sense_page_publish()) and into the
 * BENCH page's numbers (sense_page_bench()).  Until a snapshot of the
 * set-up in force arrives the read-only registers read 0, FLAGS included:
 * no bus open.
 *
 * Generations.  cfg_gen moves with every change of the set-up, cap_gen
 * with every arm, every disarm and every capture a stopped bank ends.  A
 * snapshot taken under an earlier set-up publishes nothing but the pins
 * core 1 still holds (sense_page_held()); one taken
 * under an earlier capture order leaves the capture's registers as the
 * page set them, so CAP_STATE never steps back from armed to the last
 * capture's result.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bench_state.h"
#include "link_pages.h"
#include "outputs.h"
#include "sense_svc.h"

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
    uint16_t cfg_gen;   /**< moves with each change of the set-up        */
    uint16_t cap_gen;   /**< moves with each arm and each end of a
                             capture's order                             */
    bool     sensed;    /**< BENCH carries the INA228's numbers          */
    uint8_t  bound;     /**< bit n: output slot n is bound to silicon
                             (sense_page_bound()); 0 until told           */
    uint64_t held;      /**< the pins core 1 still holds, as of its last
                             snapshot, whatever set-up it was under      */
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
 *
 * A set-up taken clears every register core 1 fills -- FLAGS, PRESENT,
 * the IDs, ERRORS, the readings, the SERVO_SENSE windows and a finished
 * capture's result, CAP_ARM with it -- so nothing read under the old
 * set-up shows until core 1 has read under the new one.  The ESC's
 * telemetry and CAP_SEQ stay.
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
 * that is not a surface on a PWM slot bound to silicon
 * (sense_page_bound()), a level past 32767 mA, a movement or band of 0 or
 * past 32767 mA (BAD_VALUE); an arm while @p o is not driving (NOT_ARMED).
 * An arm restarts the capture: CAP_STATE armed, the results 0.
 */
uint8_t sense_servo_write(sense_page_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in, const outputs_t *o);

/** Which output slots the silicon bound (bit n slot n): a capture arms
 *  only on a PWM slot that renders frames.  The coprocessor says so after
 *  every change of the bindings; until it does, no slot counts as bound. */
void sense_page_bound(sense_page_t *p, uint8_t slots);

void sense_servo_read(const sense_page_t *p, uint8_t off, uint8_t n,
                      uint16_t *out);

/**
 * One pass: a bank that is not @p driving ends a capture that has not
 * finished -- CAP_ARM 0, CAP_STATE idle -- and returns true.  A finished
 * capture keeps its result until the next arm.
 */
bool sense_page_step(sense_page_t *p, bool driving);

/**
 * The order for core 1 from the page: the set-up and its generation, and
 * the capture's.  The INA228's maximum in mA, the INA3221's shunt in µΩ;
 * a capture's level before the command left to CH1
 * (SENSE_CAP_RISE_AUTO).  run_gen, edge_set and edge_us are the caller's,
 * and left as they are.
 */
void sense_page_cmd(const sense_page_t *p, sense_cmd_t *cmd);

/**
 * What core 1 read into the read-only registers of both pages, when @p s
 * was taken under the set-up in force.  SENSE: FLAGS, PRESENT, the IDs,
 * ERRORS, the die temperature, DIAG_ALRT, and the charge and energy while
 * they are the totals of run @p run_gen (sense_run_t.totals_ok), 0
 * otherwise.  SERVO_SENSE: each channel's last window and its flags, the
 * window number, and the capture when @p s was taken under the capture
 * order in force.  Values are rounded to their register's step and held
 * to its range.
 *
 * FLAGS bit 3 (LINK_SN_I228_CLIPPED) is set when the INA228's last window
 * held a clipped current, or the run's peaks lack one.
 */
void sense_page_publish(sense_page_t *p, const sense_snap_t *s,
                        uint16_t run_gen);

/** The ESC's own telemetry voltage and current, each with its valid bit,
 *  into SENSE registers 23 to 25. */
void sense_page_esc(sense_page_t *p, bool v_ok, float volts, bool i_ok,
                    float amps);

/**
 * The BENCH page's numbers from the INA228, over what the ESC's telemetry
 * put in @p b, once the peaks of @p b are tracked.
 *
 * The INA228 is BENCH's source (LINK_BN_SENSED) while it is enabled and
 * online, and stays it to the end of a run it was online in: a part that
 * drops out while @p driving leaves voltage, current and power empty, not
 * the ESC's.  Voltage and current are the last 50 ms window's means, each
 * valid with a sample in it; power is their product, valid with both.
 * The peaks are run @p run_gen's, from the 500 Hz samples, and until
 * core 1 has started that run, the live readings.  Charge and energy are
 * the part's ENERGY and CHARGE, with LINK_BN_TOTALS_OK, while they are the
 * run's totals; otherwise 0 and the bit clear.  Nothing changes while the
 * INA228 is not BENCH's source.
 */
void sense_page_bench(sense_page_t *p, const sense_snap_t *s,
                      uint16_t run_gen, bool driving, bench_state_t *b);

/**
 * The capability bits the set-up makes true: LINK_CAP_PACK_SENSE while
 * the INA228 is enabled, LINK_CAP_SERVO_SENSE while the INA3221 is --
 * fitted as configured, not online now.  They change only with a SENSE
 * write taken, because a panel reads the identity page at link-up and
 * again after it writes SENSE, not on every poll; whether a part answers
 * is FLAGS' and BENCH bit 5's, which it polls.
 */
uint16_t sense_page_caps(const sense_page_t *p);

/** Whether either part is enabled, and so the bus runs. */
bool sense_page_enabled(const sense_page_t *p);

/** The bus's pins and clock. */
uint8_t  sense_page_sda(const sense_page_t *p);
uint8_t  sense_page_scl(const sense_page_t *p);
uint32_t sense_page_hz(const sense_page_t *p);

/** The pins the set-up holds, as a reservation mask; 0 while neither part
 *  is enabled. */
uint64_t sense_page_pins(const sense_page_t *p);

/** Those and the pins core 1 has not let go of yet (sense_page_t.held): a
 *  bus moved or closed keeps its old pins for about 1 ms.  What no output
 *  may have. */
uint64_t sense_page_held(const sense_page_t *p);

/** LINK_NACK_BAD_VALUE for an OUTPUTS page (@p slots, LINK_OS_COUNT
 *  registers) that binds a slot to a pin sense_page_held() names, the
 *  refusal the SUPPLY page gives for its own; 0 otherwise.  Taken instead,
 *  such a slot would be left unbound with nothing on the wire to say so. */
uint8_t sense_page_slots_check(const sense_page_t *p, const uint16_t *slots);

#ifdef __cplusplus
}
#endif
