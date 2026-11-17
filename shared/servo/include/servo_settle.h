/**
 * @file
 * @brief The settle rule of a measure on the IMU: the angle a run takes
 *        has stayed within a band (IMU PRECISION) for a time (IMU SETTLING
 *        TIME), the band's span taken over the trailing window.
 *
 * Why a window and a band.  A reading that comes to its final value as
 * e(t) = e0 x exp(-t / tau) moves e(t) x (exp(W / tau) - 1) over the W
 * before t.  A trailing window W whose readings span at most P therefore
 * leaves at most P / (exp(W / tau) - 1) to go.  The 500ms still time of
 * the IMU (imu.h, IMU_STILL_DEG 0.5°) alone does not: the BWT901CL in
 * 6-AXIS keeps creeping after a servo step, the remaining error x0.55 a
 * second (tau about 1.7s, measured 2026-11-17 at 5.00V and 6.00V), and
 * passes that still test 0.6s to 2.3s after the surface arrives, 0.8° to
 * 0.9° short of where it settles.  Its tail is slower: measures taken 4.1s
 * to 6.2s after arrival under W 2.0s and P 0.20° lay 0.14° to 0.26° from
 * the value held 10s later, the reading still moving 0.07°/s to 0.1°/s
 * (2026-11-18), a second component with tau about 3.5s.  The defaults,
 * W 3.0s and P 0.10°, leave at most 0.08° at tau 3.5s and 0.03° at tau
 * 1.7s, inside RUDDER AUTO's smallest tolerance of 0.1°.
 *
 * The window is kept as SERVO_SETTLE_BUCKET_MS buckets of the lowest and
 * highest angle in each, so the span is that of the buckets that reach
 * back W from the newest reading: W to W + SERVO_SETTLE_BUCKET_MS of
 * readings, never less.  Angles are taken from the first reading, wrapped
 * to -180 to 180°, so a band across ±180° holds.
 *
 * Pure C, no clock of its own; its millisecond clock wraps at 2^32.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One bucket of the window, ms. */
#define SERVO_SETTLE_BUCKET_MS   100u
/** @brief The longest window, ms: IMU SETTLING TIME's maximum, 10.0s. */
#define SERVO_SETTLE_WIN_MAX_MS  10000u
/** @brief The buckets kept: the longest window and one at each end. */
#define SERVO_SETTLE_BUCKETS \
    (SERVO_SETTLE_WIN_MAX_MS / SERVO_SETTLE_BUCKET_MS + 2u)
/** @brief IMU SETTLING TIME's default, ms (see the file's comment). */
#define SERVO_SETTLE_WIN_DEFAULT_MS  3000u
/** @brief IMU PRECISION's default, ° (see the file's comment). */
#define SERVO_SETTLE_BAND_DEFAULT_DEG 0.1f
/** @brief What a float span may lie past the band and still count as
 *         within, °: 0.3 - 0.1 in float is 0.20000002. */
#define SERVO_SETTLE_EPS_DEG     0.0005f

/** @brief The readings of one measure, bucketed. */
typedef struct {
    bool     have;                       /**< a reading came */
    float    ref;                        /**< the first reading, ° */
    uint32_t from_ms;                    /**< its time, ms */
    uint32_t last_ms;                    /**< the newest reading's time */
    uint32_t last_k;                     /**< its bucket, from from_ms */
    float    lo[SERVO_SETTLE_BUCKETS];   /**< each bucket's lowest, from
                                              ref, °; above hi: empty */
    float    hi[SERVO_SETTLE_BUCKETS];   /**< each bucket's highest */
} servo_settle_t;

/**
 * @brief Forget every reading: the next one starts the window.
 * @param s The window; NULL does nothing.
 */
void servo_settle_reset(servo_settle_t *s);

/**
 * @brief One reading.  A reading older than the newest goes into the
 *        newest's bucket.
 * @param s      The window; NULL does nothing.
 * @param deg    The angle, °; one not finite is dropped.
 * @param now_ms Its time, ms.
 */
void servo_settle_add(servo_settle_t *s, float deg, uint32_t now_ms);

/**
 * @brief The span of the readings over the trailing @p win_ms.
 * @param s      The window.
 * @param win_ms The window, ms; above SERVO_SETTLE_WIN_MAX_MS taken as it.
 * @return The highest less the lowest, °; -1 while the readings reach
 *         back less than @p win_ms, and for NULL.
 */
float servo_settle_span(const servo_settle_t *s, uint32_t win_ms);

/**
 * @brief Whether the readings held within @p band_deg for @p win_ms.
 * @param s        The window.
 * @param win_ms   IMU SETTLING TIME, ms.
 * @param band_deg IMU PRECISION, °.
 * @return true when servo_settle_span() is at least 0 and at most
 *         @p band_deg (and SERVO_SETTLE_EPS_DEG).
 */
bool servo_settle_held(const servo_settle_t *s, uint32_t win_ms,
                       float band_deg);

#ifdef __cplusplus
}
#endif
