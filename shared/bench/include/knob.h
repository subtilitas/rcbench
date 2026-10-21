/**
 * @file
 * @brief The rotary knob: an MT6701 magnetic angle sensor on the panel's I2C
 *        (Inter-Integrated Circuit) bus, read as relative motion.
 *
 * Owns no hardware.  It names the transactions of one poll, decodes the
 * bytes the panel reads and turns two successive angles into a signed count
 * of steps, so the same code is tested on the host.  The panel's task does
 * the bus transactions (firmware/panel/main/knob_task.c).
 *
 * The knob moves a slider by how far it turns, never to where it points:
 * the first reading after power-up, and after the sensor stopped answering,
 * sets a reference and reports no motion.  A knob that stops answering
 * stops contributing.
 *
 * From the MagnTek MT6701 datasheet, version 1.9 (2024-05), section 7.7:
 *
 * - The 7-bit address is 0b0000110, 0x06.  EEPROM (electrically erasable
 *   programmable read-only memory) programming moves it to 0b1000110, 0x46
 *   (version 0.1 of the datasheet, 2020-03, gives 0b0100110, 0x26).  The
 *   panel reads 0x06 only.  0x06 is outside the board's table in
 *   panel_i2c.h; 0x26 would be inside it.
 * - The 14-bit angle is in two registers: 0x03 holds Angle[13:6] in bits 7
 *   to 0, 0x04 holds Angle[5:0] in bits 7 to 2, and bits 1 and 0 of 0x04
 *   are not used.  Register 0x03 is read first, then 0x04, each in a
 *   transaction of its own (figure 20): the register address written, a
 *   repeated START, one byte read.
 * - Neither version says that reading 0x03 latches 0x04.  Version 0.1
 *   documents a burst read (section 6.6.3); version 1.9 no longer does.  So
 *   the poll reads 0x03, 0x04 and 0x03 again and takes the angle only when
 *   both 0x03 bytes agree: a high byte that changed between them can belong
 *   to another sample than the low byte, off by up to 64 counts.  A poll
 *   whose two 0x03 bytes differ moves nothing and keeps the reference.  A
 *   high byte that changes and changes back between the two 0x03 reads is
 *   accepted, so a still knob whose noise sits on a 64-count boundary can
 *   still give one step of up to 64 counts at a slider end stop (0.52 % of
 *   the span at 270 deg): the check narrows that case, it does not remove
 *   it.  How often it happens is not measured.
 * - The I2C registers carry no field-strength or status bit.  The
 *   magnet-too-strong, too-weak, push and loss-of-track bits Mg[3:0] are in
 *   the SSI (synchronous serial interface) frame only (section 7.8).  An
 *   answer is therefore taken as a reading; a sensor with no magnet over it
 *   gives an angle all the same.
 * - SCL (serial clock) period at least 1 us, so 1 MHz at most; the panel's
 *   bus runs at 400 kHz.  SDA and SCL rise and fall in 150 ns at most.
 * - The I2C interface needs the MODE pin high, which selects I2C/SSI over
 *   ABZ (figure 17 ties MODE and Z/CSN to VDD; figure 6, ABZ, ties MODE to
 *   GND).  MODE has an internal 200 kOhm pull-up.  No EEPROM setting is
 *   needed for I2C.
 *
 * Direction: the angle rises counter-clockwise seen from the chip's top
 * (figure 27) on the MT6701CT-STD and MT6701QT-STD with DIR, register 0x29
 * bit 1 in EEPROM, at its default 0 (section 8.1).  The knob counts that
 * mirrored, so a clockwise turn seen from the top, the magnet above the
 * chip's top face, raises the throttle.  An MT6701CT-STV, whose angle rises
 * clockwise (section 2), a part with DIR = 1, or a sensor seen from below
 * turns the knob the other way.  Not checked on hardware.
 *
 * A missing sensor: a read the sensor does not acknowledge makes the bus
 * driver log three error lines.  So a knob without a reference, after
 * power-up or after a read went unanswered, first probes its address, an
 * address byte alone, which the driver does not log, and reads only after
 * an acknowledge (knob_poll_probe_first()).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "panel_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The MT6701's default 7-bit address. */
#define KNOB_I2C_ADDR        0x06

_Static_assert(!PANEL_I2C_RESERVED(KNOB_I2C_ADDR),
               "the knob's address is one the panel board answers at");

/** @brief Angle[13:6], bits 7..0. */
#define KNOB_REG_ANGLE_HI    0x03
/** @brief Angle[5:0] in bits 7..2; bits 1..0 not used. */
#define KNOB_REG_ANGLE_LO    0x04

/**
 * @brief The reads of one poll, one byte each, in this order: 0x03, 0x04, 0x03.
 *
 * The angle is taken only when the first and the last agree.
 */
#define KNOB_POLL_READS      3
extern const uint8_t knob_poll_regs[KNOB_POLL_READS];

/** @brief Steps in one turn: 14 bits. */
#define KNOB_COUNTS          16384

/** @brief Poll period while the sensor answers, and while it does not. */
#define KNOB_POLL_MS         10
#define KNOB_POLL_IDLE_MS    100

/**
 * @brief The largest step between two readings taken as motion: a quarter turn,
 *        90 degrees in KNOB_POLL_MS.
 *
 * A hand turns a knob at a few degrees per
 * period; a bigger step is a glitch of the bus or the sensor and sets a new
 * reference instead of moving anything.
 */
#define KNOB_MAX_STEP        (KNOB_COUNTS / 4)

/**
 * @brief Degrees of knob per full slider span; the setting's default and range.
 */
#define KNOB_SCALE_DEG_DEFAULT 270
#define KNOB_SCALE_DEG_MIN     90
#define KNOB_SCALE_DEG_MAX     720

/**
 * @brief The datasheet's angle, 0..16383, rising counter-clockwise seen from
 *        the chip's top: @p hi read from KNOB_REG_ANGLE_HI, @p lo from
 *        KNOB_REG_ANGLE_LO.
 *
 * Bits 1 and 0 of @p lo are ignored.
 *
 * @param hi The byte read from KNOB_REG_ANGLE_HI.
 * @param lo The byte read from KNOB_REG_ANGLE_LO.
 * @return The 14-bit angle, 0 to 16383.
 */
uint16_t knob_decode(uint8_t hi, uint8_t lo);

/**
 * @brief @p angle mirrored, rising clockwise seen from the top: 0 stays 0.
 *
 * @param angle The angle, 0 to 16383.
 * @return The mirrored angle, 0 to 16383.
 */
uint16_t knob_clockwise(uint16_t angle);

/**
 * @brief The state of one knob: its attachment and its reference angle.
 */
typedef struct {
    bool     attached;    /**< knob_attach() took the address */
    uint8_t  addr;        /**< the address every poll goes to */
    bool     have_ref;
    uint16_t ref;
} knob_t;

/**
 * @brief Take @p addr as the sensor's address and forget the reference.
 *
 * An address the panel board answers at (panel_i2c.h) is refused: the knob
 * stays detached, knob_poll_addr() names no address and nothing is ever sent
 * for it.  Returns whether the address was taken.
 *
 * @param k    The knob to attach.
 * @param addr The 7-bit sensor address.
 * @return true when the address was taken.
 */
bool knob_attach(knob_t *k, unsigned addr);

/**
 * @brief The address the next poll reads, or -1 for a knob that is not
 *        attached: then no transaction is made.
 *
 * @param k The knob.
 * @return The address, or -1 for a knob that is not attached.
 */
int knob_poll_addr(const knob_t *k);

/**
 * @brief Whether the next poll probes the address before its reads: an attached
 *        knob with no reference.
 *
 * A probe that is not acknowledged ends the poll
 * as not answered, with no read made.
 *
 * @param k The knob.
 * @return true when the next poll probes the address first.
 */
bool knob_poll_probe_first(const knob_t *k);

/**
 * @brief Forget the reference: the next usable reading moves nothing.
 *
 * @param k The knob to reset.
 */
void knob_reset(knob_t *k);

/**
 * @brief Feed one poll.
 *
 * @p usable false (no answer) forgets the reference.
 * Returns the signed steps since the last usable reading, positive for a
 * rising @p raw, taking the short way across 16383 to 0; zero for the
 * first reading after a reset, and for a step over KNOB_MAX_STEP, which
 * becomes the new reference.
 *
 * @param k      The knob.
 * @param usable false for no answer, which forgets the reference.
 * @param raw    The angle, 0 to 16383.
 * @return The signed steps since the last usable reading.
 */
int knob_feed(knob_t *k, bool usable, uint16_t raw);

/**
 * @brief End one poll.
 *
 * @p answered: every read of knob_poll_regs answered, and
 * @p bytes[i] is what knob_poll_regs[i] gave.  Returns the steps, positive
 * clockwise seen from the top; a poll that was not answered, or a knob
 * that is not attached, returns 0 and forgets the reference.  A poll whose
 * two 0x03 bytes differ returns 0 and keeps the reference.
 *
 * @param k        The knob.
 * @param answered true when every read of knob_poll_regs answered.
 * @param bytes    The bytes read, one per knob_poll_regs entry.
 * @return The steps, positive clockwise seen from the top; 0 when the poll is
 *         not answered.
 */
int knob_poll_done(knob_t *k, bool answered,
                   const uint8_t bytes[KNOB_POLL_READS]);

/**
 * @brief Fraction of a slider's span that @p steps moves it when the span is @p
 *        scale_deg degrees of knob.
 *
 * @p scale_deg outside the setting's range is
 * clamped to it.
 *
 * @param steps     The signed steps moved.
 * @param scale_deg Degrees of knob per full slider span.
 * @return The fraction of the slider span that steps moves it.
 */
float knob_span_fraction(int steps, int scale_deg);

#ifdef __cplusplus
}
#endif
