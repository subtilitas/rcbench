/**
 * @file
 * @brief The IMU's reading as the bench keeps it: the newest angles
 *        through the low-pass filter, the frame rate, battery,
 *        temperature, the zero, the still time and the age.  Fed the
 *        sensor's stream through protocols/witmotion.  Stale after
 *        IMU_STALE_MS.  Pure C.
 *
 * The filter takes each angle frame, per axis: deg += (new - deg) / k,
 * with k the IMU FILTER setting, 1 to 50; k 1 passes each frame through.
 * The difference is wrapped to -180 to 180 first, so a crossing from
 * 179.9° to -179.9° moves 0.2° and not 359.8°.  The first angle frame
 * after imu_init(), imu_reseed() or a stale reading seeds it.  The
 * time constant is about k frames: the angle covers 63% of a step in
 * about k frames, so k 10 at 10Hz takes about 1s.
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
/** @brief IMU FILTER's least value: the divisor of the low-pass; 1 passes
 *         each frame through. */
#define IMU_FILTER_MIN     1u
/** @brief IMU FILTER's largest value. */
#define IMU_FILTER_MAX     50u
/** @brief IMU FILTER's default: no filtering. */
#define IMU_FILTER_DEFAULT 1u
/** @brief The filtered angle counts as settled once it lies within this
 *         of the sensor's, °: half of AUTO's smallest tolerance, 0.1°. */
#define IMU_SETTLE_DEG     0.05f

/** @brief An axis of the sensor: roll, pitch, yaw. */
typedef enum { IMU_AXIS_X = 0, IMU_AXIS_Y = 1, IMU_AXIS_Z = 2 } imu_axis_t;

/** @brief The reading, as imu_reading() copies it. */
typedef struct {
    bool     valid;        /**< an angle frame within IMU_STALE_MS */
    uint32_t taken_ms;     /**< the newest angle frame's time */
    float    angle[3];     /**< roll, pitch, yaw through the filter, °
                                -180 to 180 */
    float    zero[3];      /**< the zero taken, ° */
    bool     zeroed;       /**< imu_zero() took zero[] */
    uint16_t still_ms;     /**< the sensor's angle within IMU_STILL_DEG for
                                this long; 0 while the filtered angle
                                has not settled to IMU_SETTLE_DEG, and
                                0 on any frame it lies further than
                                that from the sensor's */
    uint16_t rate_dhz;     /**< angle frames in the last second, 0.1Hz */
    float    battery_v;    /**< from 0x52; NAN until one came */
    float    temp_c;       /**< from 0x51; NAN until one came, and
                                with RSW 0x0C the sensor sends none */
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
    uint8_t  filter;              /**< IMU FILTER, IMU_FILTER_MIN to _MAX */
    bool     seed;                /**< the next angle frame seeds the filter */
    bool     settled;             /**< the filter settled in this still time */
    float    resid;               /**< the filter's distance from the sensor's
                                       angle as the still time began, decayed
                                       a frame at a time, ° */
    uint16_t content;             /**< WIT_RSW_* bits of the frame types
                                       since imu_content_restart() */
    uint8_t  content_n;           /**< good frames since then, held at 255 */
} imu_t;

/** @brief Clear, IMU FILTER 1, the filter to seed.
 *  @param m The model; NULL does nothing. */
void imu_init(imu_t *m);
/**
 * @brief Set IMU FILTER, held to IMU_FILTER_MIN to IMU_FILTER_MAX.  The
 *        filtered angle goes on from where it is.
 * @param m      The model; NULL does nothing.
 * @param filter The setting as stored.
 */
void imu_set_filter(imu_t *m, int32_t filter);
/** @brief The next angle frame seeds the filter: a connect or a bind.
 *  @param m The model; NULL does nothing. */
void imu_reseed(imu_t *m);
/** @brief Count the frame types again from the next frame on: content and
 *         content_n to 0.  @param m The model; NULL does nothing. */
void imu_content_restart(imu_t *m);
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

/**
 * @brief IMU FILTER as the filter takes it.
 * @param v The setting as stored, any value.
 * @return @p v held to IMU_FILTER_MIN to IMU_FILTER_MAX.
 */
uint8_t imu_filter_clamp(int32_t v);
/**
 * @brief One step of the low-pass: @p deg moved by the wrapped difference
 *        to @p now over @p filter.
 * @param deg    The filtered angle, °.
 * @param now    The new frame's angle, °.
 * @param filter IMU FILTER, held to its range.
 * @return The new filtered angle, ° -180 to under 180; @p now itself for
 *         IMU FILTER 1.
 */
float imu_filter_step(float deg, float now, int32_t filter);
/**
 * @brief The longest the filtered angle takes to settle after the sensor
 *        stopped: the frames a distance of 180° needs to come within
 *        IMU_SETTLE_DEG, at 1.5 times the frame period measured, a frame
 *        at most IMU_STALE_MS apart (a slower stream is stale).
 * @param filter   IMU FILTER, held to its range.
 * @param rate_dhz The angle frames measured, 0.1Hz; 0 takes IMU_STALE_MS
 *                 a frame.
 * @return The time, ms; 0 for IMU FILTER 1.
 */
uint32_t imu_filter_settle_ms(int32_t filter, uint16_t rate_dhz);
/**
 * @brief An angle with two decimals, rounded half away from zero at the
 *        hundredth: "-12.35", "+0.00" with @p plus, never "-0.00".
 * @param buf  Takes the text; 9 bytes hold "-180.00".
 * @param n    Its size, bytes.
 * @param deg  The angle, °.
 * @param plus A '+' before a value that is not negative.
 * @return @p buf; "---" for an angle that is not finite or beyond
 *         ±100000°.
 */
const char *imu_deg_text(char *buf, size_t n, float deg, bool plus);

#ifdef __cplusplus
}
#endif
