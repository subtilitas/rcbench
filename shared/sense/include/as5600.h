/*
 * The ams OSRAM AS5600: a 12-bit magnetic rotary position sensor on I2C
 * (Inter-Integrated Circuit).  The register codec and a driver that talks
 * to one part through sense_bus.h.
 *
 * Written from the datasheet, ams AS5600 DS000365 v1-06.  The part sits at
 * the fixed address 0x36 and sends the most significant byte first.  A
 * register pointer write followed by a repeated START reads one register
 * or, with the pointer incrementing, the next ones.
 *
 * Registers used:
 *
 *   0x0B STATUS     bit 5 MD (magnet detected), bit 4 ML (magnet too weak),
 *                   bit 3 MH (magnet too strong); bits 7, 6, 2, 1 and 0 read 0
 *   0x0C RAW ANGLE  bits 11-0 in 0x0C bits 3-0 and 0x0D: the angle before
 *                   the zero position, the maximum angle and the filters
 *   0x0E ANGLE      the same 12 bits after them; equal to RAW ANGLE while
 *                   ZPOS and MPOS are not burned
 *   0x1A AGC        automatic gain control, 0 to 255 at 5 V and 0 to 128 at
 *                   3.3 V; a magnet at the right distance reads near the
 *                   middle of the range
 *   0x1B MAGNITUDE  bits 11-0 in 0x1B bits 3-0 and 0x1C: the CORDIC
 *                   magnitude, a measure of the field
 *
 * One count is 360 / 4096 = 0.0879 degrees.  The count wraps from 4095 to 0;
 * differences between two angles are taken on the circle
 * (as5600_delta()).
 *
 * Reads.  STATUS and RAW ANGLE are contiguous: as5600_read_angle() gets both
 * in one 3-byte read, 0.19 ms of bus time at 400 kHz without the
 * controller's own time between transactions, which is not measured.
 * as5600_read_magnitude() reads AGC and MAGNITUDE in one 3-byte read.
 *
 * Identity.  The part has no identity register.  A probe reads STATUS and
 * RAW ANGLE; a STATUS with a reserved bit set is not an AS5600's and the
 * part is not used (SENSE_PART_WRONG_ID).  An AS5600L answers at 0x40 by
 * default and is not addressed here.
 *
 * Pure C, no SDK (software development kit): host-tested in test_as5600.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sense_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AS5600_ADDR          0x36u
#define AS5600_REG_STATUS    0x0Bu
#define AS5600_REG_RAW_ANGLE 0x0Cu
#define AS5600_REG_ANGLE     0x0Eu
#define AS5600_REG_AGC       0x1Au
#define AS5600_REG_MAGNITUDE 0x1Bu

#define AS5600_STATUS_MH     0x08u
#define AS5600_STATUS_ML     0x10u
#define AS5600_STATUS_MD     0x20u
/** The bits STATUS defines; any other set is not an AS5600. */
#define AS5600_STATUS_MASK   (AS5600_STATUS_MH | AS5600_STATUS_ML | AS5600_STATUS_MD)

/** Counts in one turn: 12 bits. */
#define AS5600_COUNTS        4096u
/** Hundredths of a degree in one turn. */
#define AS5600_CDEG_TURN     36000

/** A 12-bit register pair, most significant byte first: RAW ANGLE, ANGLE or
 *  MAGNITUDE.  0 to 4095. */
uint16_t as5600_u12(const uint8_t *b);

/** Whether @p status has only the bits the datasheet defines. */
bool as5600_status_valid(uint8_t status);

/** The magnet bits of @p status. */
bool as5600_md(uint8_t status);
bool as5600_ml(uint8_t status);
bool as5600_mh(uint8_t status);

/** @p a minus @p b on the circle, -2048 to 2047 counts: the shortest way
 *  from @p b to @p a.  Both are taken modulo 4096. */
int16_t as5600_delta(uint16_t a, uint16_t b);

/** @p counts in hundredths of a degree, rounded to the nearest, halves away
 *  from zero. */
int32_t as5600_cdeg(int32_t counts);

/* -------------------------------------------------------------- driver */

typedef struct {
    sense_part_t part;
} as5600_t;

/** A part on @p bus at AS5600_ADDR, not yet probed. */
void as5600_init(as5600_t *d, sense_bus_t *bus);

/** Every tick: probes the part when it is due -- at the first call, and
 *  SENSE_RETRY_MS after it was found absent, wrong or offline.  Returns
 *  whether it is online. */
bool as5600_step(as5600_t *d, uint32_t now_ms);

sense_state_t as5600_state(const as5600_t *d);

/** STATUS and RAW ANGLE in one read.  On anything but SENSE_OK both are
 *  left as they were. */
sense_err_t as5600_read_angle(as5600_t *d, uint8_t *status, uint16_t *raw);

/** AGC and MAGNITUDE in one read; the same rule. */
sense_err_t as5600_read_magnitude(as5600_t *d, uint8_t *agc,
                                  uint16_t *magnitude);

#ifdef __cplusplus
}
#endif
