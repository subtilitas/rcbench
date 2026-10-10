/*
 * KST servo programming protocol: the register image and its fields.
 *
 * A servo holds 32 registers of 8 bits.  This file names every bit of them
 * as a field, converts a field's raw value to the value the vendor tool
 * shows and back, and checks an image against the layout the wire readings
 * come from.
 *
 * The image stores raw values.  A displayed value is derived from the raw
 * value and never written back on its own, so a field that is not edited
 * keeps its bits.  kst_field_set() changes the bits of one field and no
 * other.
 *
 * Displayed values are integers in the unit the field's kst_unit_t names.
 * No floating point is used.  Rounding, where a conversion rounds, is to the
 * nearest value with a tie going up, except Max. Duty, whose tie goes down
 * as the vendor tool's does (50 % is raw 127).
 *
 * The conversions are what the vendor tool displays and writes.  The effect
 * of a register on servo motion is not measured for any register.  The
 * scaling the vendor tool applies in Gyro Servo mode (register 0x1F bit 0:
 * half the microsecond values, 0.05 s time unit) is not applied here.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KST_REG_H
#define KST_REG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kst_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The 32 registers, by address. */
typedef struct {
    uint8_t r[KST_REG_COUNT];
} kst_image_t;

/* Register addresses that code outside the field table names. */
#define KST_REG_DUTY_COPY 0x01u
#define KST_REG_DUTY      0x02u
#define KST_REG_NODE_ADDR 0x1Du

/**
 * Every bit of the 32 registers belongs to exactly one field.  Fields named
 * KST_F_Rxx are bits the vendor tool gives no name.
 */
typedef enum {
    KST_F_R00 = 0,
    KST_F_DUTY_COPY,     /**< 01: the vendor tool writes the value of 02 */
    KST_F_DUTY,          /**< 02: Max. Duty */
    KST_F_BOOST,         /**< 03 */
    KST_F_STRETCH,       /**< 04 bits 2-0 */
    KST_F_SOFT_START,    /**< 04 bit 3 */
    KST_F_R04_B54,
    KST_F_20K_SEL,       /**< 04 bit 6 */
    KST_F_SPD_SEL,       /**< 04 bit 7 */
    KST_F_DEAD_BAND,     /**< 05 bits 5-0 */
    KST_F_LOCK,          /**< 05 bit 6; meaning unknown */
    KST_F_ALLOW_UNCONT,  /**< 05 bit 7 */
    KST_F_R06,
    KST_F_PULSE_LOWER,   /**< 07 and 09 bits 3-0 */
    KST_F_PULSE_UPPER,   /**< 08 and 09 bits 7-4 */
    KST_F_NEUTRAL,       /**< 0B and 0A bits 4-0 */
    KST_F_R0A_B5,
    KST_F_REVERSION,     /**< 0A bit 6 */
    KST_F_R0A_B7,
    KST_F_UNCONT_POS,    /**< 0C and 0D bits 3-0 */
    KST_F_R0D_B76,
    KST_F_UNCONT_TIME,   /**< 0E */
    KST_F_R0F,
    KST_F_LEFT_ANGLE,    /**< 10 */
    KST_F_RIGHT_ANGLE,   /**< 11 */
    KST_F_R12_B20,
    KST_F_PROT1_CANCLE,  /**< 12 bit 3; the vendor's spelling */
    KST_F_R12_B54,
    KST_F_PROT1_EN,      /**< 12 bit 6 */
    KST_F_R12_B7,
    KST_F_PROT1_TIME,    /**< 13 and 0D bits 5-4 */
    KST_F_R14,
    KST_F_PROT1_PWM,     /**< 15 */
    KST_F_R16,
    KST_F_PROT_POT_ERR,  /**< 17 and 18 bits 3-0 */
    KST_F_R18_B74,
    KST_F_POT_SAM_TIMES, /**< 19 */
    KST_F_PROT_PWM_ERR,  /**< 1A */
    KST_F_SPD,           /**< 1B and 1C bits 3-0 */
    KST_F_R1C_B74,
    KST_F_NODE_ADDR,     /**< 1D: the pairing value of the vendor's card */
    KST_F_R1E,
    KST_F_GYRO,          /**< 1F bit 0: Servo or Gyro Servo */
    KST_F_R1F_B71,
    KST_F_COUNT
} kst_field_id_t;

/** A set of fields, bit n for field n. */
typedef uint64_t kst_field_set_t;
#define KST_FIELD_BIT(id) ((kst_field_set_t)1 << (id))

/** Who may change a field.  A decision about a field changes this column of
 *  the table and no code. */
typedef enum {
    KST_ACCESS_EDITABLE = 0, /**< an edit may change it */
    KST_ACCESS_LOCKED,       /**< read-only until its function is measured;
                                  an unlock set passed to the checks lifts it */
    KST_ACCESS_NEVER,        /**< no edit path; only a restore writes it */
} kst_access_t;

/** Unit of a displayed value. */
typedef enum {
    KST_UNIT_RAW = 0,        /**< the raw value, no unit known */
    KST_UNIT_BOOL,           /**< 0 or 1 */
    KST_UNIT_TENTH_US,       /**< 0.1 us */
    KST_UNIT_PERCENT,        /**< 1 % */
    KST_UNIT_TENTH_PERCENT,  /**< 0.1 % */
    KST_UNIT_TENTH_S,        /**< 0.1 s */
    KST_UNIT_FACTOR,         /**< a multiplier: 1, 2, 4, 8, 16, 32 */
    KST_UNIT_COUNT_OF,       /**< a count: 0 to the field's width in bits */
} kst_unit_t;

/** One row of the field table. */
typedef struct {
    const char *name;   /**< the vendor tool's label; "" for unnamed bits */
    uint8_t lo_reg;     /**< register of the low part */
    uint8_t lo_shift;   /**< position of the low part's bit 0 */
    uint8_t lo_bits;    /**< width of the low part */
    uint8_t hi_reg;     /**< register of the high part */
    uint8_t hi_shift;
    uint8_t hi_bits;    /**< 0: the field lies in one register */
    uint8_t access;     /**< kst_access_t */
    uint8_t unit;       /**< kst_unit_t */
    uint8_t conv;       /**< conversion, private to kst_reg.c */
} kst_field_t;

/** The table row of @p id; NULL for an id outside the table. */
const kst_field_t *kst_field(kst_field_id_t id);

/** Largest raw value the field's bits hold; 0 for an id outside the table. */
uint16_t kst_field_raw_max(kst_field_id_t id);

/** Bits of register @p reg that belong to @p id. */
uint8_t kst_field_reg_mask(kst_field_id_t id, uint8_t reg);

/** Raw value of a field; 0 for a bad argument. */
uint16_t kst_field_get(const kst_image_t *img, kst_field_id_t id);

/** Set a field's bits.  No other bit of the image changes.  False, and no
 *  change, when @p raw does not fit the field. */
bool kst_field_set(kst_image_t *img, kst_field_id_t id, uint16_t raw);

/**
 * Set a field as an edit does: kst_field_set(), and for KST_F_DUTY the copy
 * in register 0x01 as well.  Both known images hold 0x01 equal to 0x02.
 */
bool kst_field_edit(kst_image_t *img, kst_field_id_t id, uint16_t raw);

/**
 * Raw value to displayed value.  False where the displayed value of that
 * raw value is not established: Stretch index 6 and 7.
 *
 *   Max. Duty      round(raw x 100 / 255) %
 *   Boost          50 us + 12.5 us x raw
 *   Stretch        1 << index
 *   Dead Band      0.2 us x (65 - raw)
 *   Pulse Lower, Pulse Upper, Uncontrolled Pos   0.8 us x raw
 *   Neutral        810 us + raw x 409 / 2048 us, to 0.1 us; the constants
 *                  are a fit through 3 values and uncertain by about 1 us
 *   Uncontrolled Time, prot1_time   0.1 s x raw; prot1_time shown capped
 *                  at 100.0 s
 *   prot1_pwm      round(raw x 97 / 255) %
 *   prot_pot_err, prot_pwm_err      count of 1 bits upward from bit 0.  The
 *                  vendor tool shows 0 for registers 17 = FF, 18 = FF; here
 *                  that image gives 12
 *   SPD            0.1 % x raw
 *   all others     raw
 */
bool kst_field_display(kst_field_id_t id, uint16_t raw, int32_t *display);

/**
 * Displayed value to the nearest raw value.  False when the value lies
 * outside what the field encodes, when Stretch is not a power of two from
 * 1 to 32, and for prot1_time and prot1_pwm, whose encoding on write is
 * not observed.
 */
bool kst_field_raw_from_display(kst_field_id_t id, int32_t display,
                                uint16_t *raw);

/* --- layout fingerprint --------------------------------------------------- */

/**
 * What both known images have in common and the vendor tool forces on every
 * write.  An image that breaks one of these does not have the measured
 * layout, and nothing is written to that servo.
 */
typedef enum {
    KST_FP_R00 = 0,         /**< 00 reads 00 */
    KST_FP_DUTY_COPY,       /**< 01 equals 02 */
    KST_FP_R06,             /**< 06 reads 04 */
    KST_FP_R1E,             /**< 1E reads 00 */
    KST_FP_R04_B54,         /**< 04 bits 5-4 are 0 */
    KST_FP_R0A_B5_B7,       /**< 0A bits 5 and 7 are 0 */
    KST_FP_R12_UNNAMED,     /**< 12 bits 0, 1, 2, 4, 5, 7 are 0 */
    KST_FP_R1F_B71,         /**< 1F bits 7-1 are 0 */
    KST_FP_STRETCH_INDEX,   /**< the stretch index is 5 or less */
    KST_FP_COUNT
} kst_fp_rule_t;

typedef struct {
    uint16_t rules;   /**< bit n: rule n of kst_fp_rule_t is broken */
    uint32_t regs;    /**< bit n: register n deviates */
} kst_fingerprint_t;

/** True when the image has the measured layout.  @p out may be NULL. */
bool kst_fingerprint(const kst_image_t *img, kst_fingerprint_t *out);

#ifdef __cplusplus
}
#endif

#endif /* KST_REG_H */
