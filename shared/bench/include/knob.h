/*
 * The rotary knob: an AS5600 magnetic angle sensor on the panel's I2C
 * (Inter-Integrated Circuit) bus, read as relative motion.
 *
 * Owns no hardware.  It decodes the bytes the panel reads and turns two
 * successive angles into a signed count of steps, so the same code is
 * tested on the host.  The panel's task does the bus transaction.
 *
 * The knob moves a slider by how far it turns, never to where it points:
 * the first reading after power-up, after the sensor stopped answering, or
 * after a reading the sensor flagged unusable sets a reference and reports
 * no motion.  A knob that stops answering stops contributing.
 *
 * Address assumption: the sensor sits at 0x36.  That is outside the
 * CH422G's command addresses in its datasheet, and Waveshare's wiki
 * reserves 0x30 to 0x3F on this bus for the CH422G.  Not checked on
 * hardware.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The AS5600's fixed 7-bit address. */
#define KNOB_I2C_ADDR        0x36

/** STATUS: bit 5 MD (magnet detected), bit 4 ML (too weak), bit 3 MH (too strong). */
#define KNOB_REG_STATUS      0x0B
/** RAW ANGLE: 12 bits over 0x0C (high, bits 3..0) and 0x0D (low). */
#define KNOB_REG_RAW_ANGLE   0x0C
/** MAGNITUDE: 12 bits over 0x1B (high, bits 3..0) and 0x1C (low). */
#define KNOB_REG_MAGNITUDE   0x1B

/**
 * Each register is its own transaction: the address byte, then 1 byte
 * (STATUS) or 2 bytes, high then low (RAW ANGLE, MAGNITUDE).  RAW ANGLE and
 * MAGNITUDE are special registers that suppress the address pointer's
 * automatic increment, so one read from 0x0B does not reach them.
 */
#define KNOB_STATUS_LEN      1
#define KNOB_WORD_LEN        2

#define KNOB_STATUS_MH       0x08u
#define KNOB_STATUS_ML       0x10u
#define KNOB_STATUS_MD       0x20u

/** Steps in one turn: 12 bits. */
#define KNOB_COUNTS          4096

/** Poll period while the sensor answers, and while it does not. */
#define KNOB_POLL_MS         10
#define KNOB_POLL_IDLE_MS    100

/**
 * The largest step between two readings taken as motion: a quarter turn, 90
 * degrees in KNOB_POLL_MS.  A hand turns a knob at a few degrees per
 * period; a bigger step is a glitch of the bus or the sensor and sets a new
 * reference instead of moving anything.
 */
#define KNOB_MAX_STEP        (KNOB_COUNTS / 4)

/** Degrees of knob per full slider span; the setting's default and range. */
#define KNOB_SCALE_DEG_DEFAULT 270
#define KNOB_SCALE_DEG_MIN     90
#define KNOB_SCALE_DEG_MAX     720

typedef struct {
    bool     magnet;      /**< MD set */
    bool     too_weak;    /**< ML set */
    bool     too_strong;  /**< MH set */
    uint16_t raw;         /**< RAW ANGLE, 0..4095 */
    uint16_t magnitude;   /**< MAGNITUDE, 0..4095 */
} knob_reading_t;

/**
 * Decode the three reads: the STATUS byte, the KNOB_WORD_LEN bytes of
 * RAW ANGLE read from KNOB_REG_RAW_ANGLE and the KNOB_WORD_LEN bytes of
 * MAGNITUDE read from KNOB_REG_MAGNITUDE, each high byte first.
 *
 * Returns whether the reading can move a slider: the magnet is detected,
 * neither too weak nor too strong, and the magnitude is not zero.  @p out is
 * filled either way.
 */
bool knob_decode(uint8_t status, const uint8_t raw[KNOB_WORD_LEN],
                 const uint8_t magnitude[KNOB_WORD_LEN], knob_reading_t *out);

typedef struct {
    bool     have_ref;
    uint16_t ref;
} knob_t;

/** Forget the reference: the next usable reading moves nothing. */
void knob_reset(knob_t *k);

/**
 * Feed one poll.  @p usable false (no answer, or a reading knob_decode()
 * refused) forgets the reference.  Returns the signed steps since the last
 * usable reading, positive for a rising angle, taking the short way across
 * 4095 to 0; zero for the first reading after a reset, and for a step over
 * KNOB_MAX_STEP, which becomes the new reference.
 */
int knob_feed(knob_t *k, bool usable, uint16_t raw);

/**
 * Fraction of a slider's span that @p steps moves it when the span is
 * @p scale_deg degrees of knob.  @p scale_deg outside the setting's range is
 * clamped to it.
 */
float knob_span_fraction(int steps, int scale_deg);

#ifdef __cplusplus
}
#endif
