/*
 * KST servo programming protocol: limits on an edited image.
 *
 * A hard rule refuses an image.  A soft rule asks for a confirmation and
 * never refuses.  Each rule has an identifier that stays the same across
 * versions, so a user interface maps it to a text.
 *
 * The hard limits are the ranges the vendor tool prints on its controls,
 * the encodings that are not established and the states with no control
 * authority.  The soft limits mark values inside the vendor's range that
 * move away from the servo's own backed-up value in the hazardous
 * direction.  The numbers marked "proposed" have no measurement behind
 * them.  What a register does to the servo's motion is not measured for any
 * register, so the limits rest on the vendor's labels.
 *
 * The limits cannot see the mechanical travel of a gear train, a linkage,
 * the supply or the load.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KST_LIMITS_H
#define KST_LIMITS_H

#include <stdbool.h>
#include <stdint.h>

#include "kst_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Raw limits.  Pulse widths: 1 count is 0.8 us. */
#define KST_DUTY_RAW_MAX        250u   /**< 98 % */
#define KST_DUTY_RAW_MIN        26u    /**< 10 %, proposed */
#define KST_DUTY_RAW_HALF       128u   /**< 50 %, proposed */
#define KST_DUTY_RAW_90         230u   /**< the highest raw value shown as 90 % */
#define KST_BOOST_RAW_MAX       236u   /**< 3000 us */
#define KST_STRETCH_INDEX_MAX   5u     /**< factor 32 */
#define KST_SPD_RAW_MAX         1000u  /**< 100 % */
#define KST_SPD_RAW_MIN_ON      50u    /**< 5 % with spd_sel on, proposed */
#define KST_SPD_RAW_SLOW        300u   /**< 30 % with spd_sel on, proposed */
#define KST_DEAD_BAND_RAW_1US   60u    /**< 1.0 us; a higher raw value is narrower */
#define KST_PULSE_RAW_MIN       625u   /**< 500 us */
#define KST_PULSE_RAW_MAX       3125u  /**< 2500 us */
#define KST_NEUTRAL_RAW_MIN     2754u  /**< 1360 us */
#define KST_NEUTRAL_RAW_MAX     4156u  /**< 1640 us */
#define KST_UNCONT_TIME_RAW_MIN 4u     /**< 0.4 s */
#define KST_UNCONT_TIME_RAW_MAX 130u   /**< 13.0 s */
#define KST_UNCONT_TIME_RAW_SHORT 10u  /**< 1.0 s, proposed */
#define KST_ANGLE_RAW_SMALL     50u    /**< proposed */

/* Distances between pulse widths, in 0.1 us.  All proposed. */
#define KST_MARGIN_HARD_TENTH_US   1000  /**< Neutral to an end point: 100 us */
#define KST_MARGIN_SOFT_TENTH_US   3000  /**< Neutral to an end point: 300 us */
#define KST_SPAN_MIN_TENTH_US      4000  /**< Upper less Lower: 400 us */
#define KST_END_MOVE_TENTH_US      1000  /**< an end point against the backup: 100 us */
#define KST_NEUTRAL_MOVE_TENTH_US  200   /**< Neutral against the backup: 20 us */
#define KST_UNCONT_FAR_TENTH_US    3000  /**< Uncontrolled Pos from Neutral: 300 us */

/**
 * The rules.  Values are stable: a rule keeps its number, a new rule takes
 * the next free one.  KST_H_ rules are hard, KST_S_ rules are soft.
 */
typedef enum {
    /* Hard, on an image alone. */
    KST_H_DUTY_HIGH = 0,       /**< Max. Duty above 98 % */
    KST_H_DUTY_LOW = 1,        /**< Max. Duty below 10 % */
    KST_H_DUTY_COPY = 2,       /**< register 01 differs from 02 */
    KST_H_BOOST_HIGH = 3,      /**< Boost above 3000 us */
    KST_H_STRETCH_INDEX = 4,   /**< Stretch index 6 or 7 */
    KST_H_SPD_HIGH = 5,        /**< SPD above 100 % */
    KST_H_SPD_SEL_LOW_SPD = 6, /**< spd_sel on with SPD below 5 % */
    KST_H_UNCONT_POS_LOW = 7,  /**< Uncontrolled Pos below 500 us */
    KST_H_UNCONT_POS_HIGH = 8, /**< Uncontrolled Pos above 2500 us */
    KST_H_UNCONT_POS_SPAN = 9, /**< Uncontrolled Pos outside Lower..Upper */
    KST_H_UNCONT_TIME_LOW = 10,  /**< Uncontrolled Time below 0.4 s */
    KST_H_UNCONT_TIME_HIGH = 11, /**< Uncontrolled Time above 13.0 s */
    KST_H_LOWER_LOW = 12,      /**< Pulse Lower below 500 us */
    KST_H_LOWER_MARGIN = 13,   /**< Neutral less Lower below 100 us */
    KST_H_UPPER_HIGH = 14,     /**< Pulse Upper above 2500 us */
    KST_H_UPPER_MARGIN = 15,   /**< Upper less Neutral below 100 us */
    KST_H_SPAN = 16,           /**< Upper less Lower below 400 us */
    KST_H_NEUTRAL_LOW = 17,    /**< Neutral below 1360 us */
    KST_H_NEUTRAL_HIGH = 18,   /**< Neutral above 1640 us */
    KST_H_LEFT_ZERO = 19,      /**< Left Angle 0 */
    KST_H_RIGHT_ZERO = 20,     /**< Right Angle 0 */
    KST_H_GYRO_MODE = 21,      /**< Gyro Servo mode: its limits are not known */
    /* Hard, against the backup. */
    KST_H_LOCKED = 22,         /**< a locked field differs from the backup */
    KST_H_NEVER = 23,          /**< a field with no edit path differs */
    /* Soft. */
    KST_S_DUTY_RAISED = 24,        /**< Max. Duty above the backup */
    KST_S_DUTY_BELOW_HALF = 25,    /**< Max. Duty below 50 % */
    KST_S_BOOST_RAISED = 26,       /**< Boost above the backup */
    KST_S_STRETCH_RAISED = 27,     /**< Stretch above the backup */
    KST_S_SOFT_START_OFF = 28,     /**< Soft_Start switched off */
    KST_S_SPD_SEL_ON = 29,         /**< spd_sel switched on */
    KST_S_SPD_SLOW = 30,           /**< SPD below 30 % with spd_sel on */
    KST_S_DEAD_BAND_NARROWED = 31, /**< Dead Band below the backup */
    KST_S_DEAD_BAND_BELOW_1US = 32, /**< Dead Band below 1.0 us */
    KST_S_ALLOW_UNCONT_ON = 33,    /**< Allow Uncont switched on */
    KST_S_UNCONT_POS_FAR = 34,     /**< more than 300 us from Neutral, Allow Uncont on */
    KST_S_UNCONT_TIME_SHORT = 35,  /**< Uncontrolled Time below 1.0 s */
    KST_S_LOWER_NEAR = 36,         /**< Neutral less Lower below 300 us */
    KST_S_LOWER_MOVED = 37,        /**< Pulse Lower more than 100 us from the backup */
    KST_S_UPPER_NEAR = 38,         /**< Upper less Neutral below 300 us */
    KST_S_UPPER_MOVED = 39,        /**< Pulse Upper more than 100 us from the backup */
    KST_S_NEUTRAL_MOVED = 40,      /**< Neutral more than 20 us from the backup */
    KST_S_REVERSION_CHANGED = 41,  /**< Reversion differs from the backup */
    KST_S_LEFT_RAISED = 42,        /**< Left Angle above the backup */
    KST_S_LEFT_SMALL = 43,         /**< Left Angle below 50 */
    KST_S_RIGHT_RAISED = 44,       /**< Right Angle above the backup */
    KST_S_RIGHT_SMALL = 45,        /**< Right Angle below 50 */
    KST_S_GYRO_CHANGED = 46,       /**< Servo / Gyro Servo differs from the backup */
    KST_S_NARROW_BAND_GAIN = 47,   /**< Dead Band below 1.0 us with Stretch above 1
                                        or Boost above the backup */
    KST_S_HIGH_DUTY_TRAVEL = 48,   /**< Max. Duty above 90 % with an angle above
                                        the backup */
    KST_RULE_COUNT = 49
} kst_rule_t;

/** A set of rules, bit n for rule n. */
typedef uint64_t kst_rules_t;
#define KST_RULE_BIT(rule) ((kst_rules_t)1 << (rule))

/** The hard rules as a set. */
#define KST_RULES_HARD (KST_RULE_BIT(KST_S_DUTY_RAISED) - 1u)

/**
 * The hard rules an image breaks on its own.  Pulse Lower, Upper and
 * Uncontrolled Pos are compared with Neutral in microseconds, because
 * Neutral's unit differs from theirs.
 *
 * An image in Gyro Servo mode breaks KST_H_GYRO_MODE: the vendor tool halves
 * every microsecond value there, and what a servo does with them is not
 * measured.
 */
kst_rules_t kst_limits_image(const kst_image_t *img);

/**
 * The hard and soft rules the image @p target breaks as an edit of the
 * servo's image @p backup.
 *
 * @p unlocked lifts KST_ACCESS_LOCKED for the fields in it.  Nothing lifts
 * KST_ACCESS_NEVER; register 01 may follow 02.  @p refused, when not NULL,
 * receives the changed fields that the access class refuses.
 *
 * A soft rule about a value on its own (KST_S_DUTY_BELOW_HALF and the like)
 * is reported when the edit changes a field the rule reads, not for a state
 * the servo already has.
 */
kst_rules_t kst_limits_edit(const kst_image_t *backup,
                            const kst_image_t *target,
                            kst_field_set_t unlocked,
                            kst_field_set_t *refused);

#ifdef __cplusplus
}
#endif

#endif /* KST_LIMITS_H */
