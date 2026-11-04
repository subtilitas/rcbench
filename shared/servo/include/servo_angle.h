/**
 * @file
 * @brief The angle source of the servo runs: the AS5600's counts as they
 *        come, or the IMU's angle converted to the same counts
 *        (SERVO_ANGLE_COUNTS a turn, centre 0), so servo_test and
 *        servo_limrun read one form.  Pure C.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "servo_test_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SERVO_ANGLE_COUNTS 4096u   /**< the AS5600's; 0.088° a count */

/** @brief Where a run takes the angle from: SET_ANGLE_SOURCE's options. */
typedef enum { SERVO_ANGLE_AS5600 = 0, SERVO_ANGLE_IMU = 1 } servo_angle_src_t;

/** @brief What the IMU path hands in. */
typedef struct {
    bool     valid;      /**< a reading within IMU_STALE_MS */
    float    deg;        /**< from the zero, signed, -180 to 180 */
    uint16_t still_ms;   /**< the IMU's still time at the reading, ms */
    uint32_t taken_ms;   /**< when the panel had it; for one not valid,
                              when it was found so */
    bool     gap;        /**< readings before this one were lost */
} servo_angle_in_t;

/**
 * @brief The IMU reading as a servo_test_enc_t: raw = lround(deg x
 *        SERVO_ANGLE_COUNTS / 360) mod SERVO_ANGLE_COUNTS, centre 0; not
 *        valid carries taken_ms and gap, no_magnet false, weak and strong
 *        false.
 * @param in  The IMU path's reading.
 * @param out Receives the reading in counts; cleared for a NULL @p in.
 * @return out->valid; false for NULL.
 */
bool servo_angle_enc(const servo_angle_in_t *in, servo_test_enc_t *out);

/** @brief The centre count the source's degrees are taken from: @p enc_centre
 *         for the AS5600, 0 for the IMU.
 *  @param src        The source.
 *  @param enc_centre AS5600 centre, 0 to 4095.
 *  @return the centre count. */
uint16_t servo_angle_centre(servo_angle_src_t src, uint16_t enc_centre);

/**
 * @brief The source's name for the report and the CSV: "AS5600", "IMU X+",
 *        "IMU Z-".
 * @param src  The source.
 * @param axis 0 X, 1 Y, 2 Z.
 * @param neg  The sign.
 * @param buf  Receives the name; NULL writes nothing.
 * @param n    The size of @p buf; 0 writes nothing.
 * @return @p buf.
 */
const char *servo_angle_name(servo_angle_src_t src, uint8_t axis, bool neg,
                             char *buf, size_t n);

#ifdef __cplusplus
}
#endif
