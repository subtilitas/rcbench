/**
 * @file
 * @brief The IMU's reading as the bench keeps it: the newest angles, the
 *        frame rate, battery, temperature, the zero, the still time and
 *        the age.  Fed the UART's bytes through protocols/witmotion.
 *        Stale after IMU_STALE_MS.  Pure C.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "witmotion.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A reading older than this is not valid: the non-safety
 *         allowance of servo_gap.h (SERVO_GAP_MAX_MS, asserted equal). */
#define IMU_STALE_MS     300u
/** @brief The angle within this of the one before counts as still, °. */
#define IMU_STILL_DEG    0.5f
/** @brief The window the frame rate is counted over, ms. */
#define IMU_RATE_WIN_MS  1000u

/** @brief An axis of the sensor: roll, pitch, yaw. */
typedef enum { IMU_AXIS_X = 0, IMU_AXIS_Y = 1, IMU_AXIS_Z = 2 } imu_axis_t;

/** @brief The reading, as imu_reading() copies it. */
typedef struct {
    bool     valid;        /**< an angle frame within IMU_STALE_MS */
    uint32_t taken_ms;     /**< the newest angle frame's time */
    float    angle[3];     /**< roll, pitch, yaw, ° -180 to 180 */
    float    zero[3];      /**< the zero taken, ° */
    bool     zeroed;       /**< imu_zero() took zero[] */
    uint16_t still_ms;     /**< the angle within IMU_STILL_DEG for this long */
    uint16_t rate_dhz;     /**< angle frames in the last second, 0.1Hz */
    float    battery_v;    /**< from 0x52; NAN until one came */
    float    temp_c;       /**< from 0x51; NAN until one came */
    uint16_t version;      /**< from 0x53 */
    uint32_t frames;       /**< good frames, all types */
    uint32_t bad;          /**< bad sums + resync bytes */
} imu_reading_t;

/** @brief The model: the reading, the parser and the windows behind it. */
typedef struct {
    imu_reading_t r;              /**< the reading */
    wit_parser_t  p;              /**< the frame parser */
    uint32_t rate_from_ms;        /**< the rate window's start */
    uint16_t rate_count;          /**< angle frames in the window so far */
    float    still_ref[3];        /**< the angles the still time runs from */
    uint32_t still_from_ms;       /**< when they came */
} imu_t;

/** @brief Clear. @param m The model; NULL does nothing. */
void imu_init(imu_t *m);
/**
 * @brief Feed bytes at @p now_ms.
 * @param m      The model; NULL does nothing.
 * @param b      The bytes; NULL does nothing.
 * @param n      Their count.
 * @param now_ms The clock.
 */
void imu_feed(imu_t *m, const uint8_t *b, size_t n, uint32_t now_ms);
/** @brief Age the reading: valid goes false IMU_STALE_MS after the last
 *         angle frame; the rate window rolls.
 *  @param m      The model; NULL does nothing.
 *  @param now_ms The clock. */
void imu_tick(imu_t *m, uint32_t now_ms);
/** @brief Take the zero from the current angles. @param m The model.
 *  @return false while not valid. */
bool imu_zero(imu_t *m);
/** @brief Forget the zero. @param m The model; NULL does nothing. */
void imu_unzero(imu_t *m);
/** @brief Copy the reading. @param m The model. @param out Receives it.
 *  @return r.valid; false for NULL. */
bool imu_reading(const imu_t *m, imu_reading_t *out);
/**
 * @brief The angle of @p axis from the zero, wrapped to -180 to 180,
 *        negated with @p neg.
 * @param r    The reading.
 * @param axis The axis.
 * @param neg  The sign negative.
 * @return the angle, °; 0 while not valid.
 */
float imu_deg(const imu_reading_t *r, imu_axis_t axis, bool neg);
/** @brief "X", "Y" or "Z". @param axis The axis.
 *  @return The name; "" for another. */
const char *imu_axis_name(imu_axis_t axis);

#ifdef __cplusplus
}
#endif
