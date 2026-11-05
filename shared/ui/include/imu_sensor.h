/**
 * @file
 * @brief The sensor side of the Bluetooth IMU, whichever link carries it:
 *        the BWT901CL's reading (imu.h) from its stream, the outbox of its
 *        WitMotion commands, the calibration, the RATE and ALGORITHM writes
 *        due, the stale stretches counted, and the handout to SERVO.  Pure
 *        C, no radio: the controller (imu_ctl.h) joins it to a link.
 *
 * The link tells the sensor two things: whether the sensor's stream flows
 * (link_up: the HC-05 runs no AT operation and is bound) and whether a
 * 5-byte command can go now (imu_sensor_next()'s caller asks the link
 * first).  The stream's bytes come in order, in chunks of any length.
 * The sensor is connected while the link is up and the reading is valid.
 *
 * The commands go each after the unlock, IMU_CMD_GAP_MS apart: CALIBRATE
 * is CALSW 1, IMU_CAL_MS, CALSW 0, SAVE; a RATE or ALGORITHM change is the
 * register and SAVE, queued while the sensor is connected or when it next
 * connects.
 *
 * Every function of one sensor is called from one task.  Times are
 * unsigned differences of a free-running 32-bit ms clock.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "imu.h"
#include "imu_screen.h"
#include "witmotion.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The accelerometer calibration's still time, ms. */
#define IMU_CAL_MS         3000u
/** @brief The gap between two sensor commands, ms. */
#define IMU_CMD_GAP_MS     50u
/** @brief Sensor commands held to send at most. */
#define IMU_OUTBOX         8u

/** @brief The sensor side.  Its members are private but for the tests. */
typedef struct {
    imu_t        m;              /**< the reading */
    bool         had_frames;     /**< a valid reading since IMU ON or the
                                      last imu_sensor_rebind() */
    uint8_t      rate_code;      /**< SETUP's RATE, wit_rate_t */
    bool         six;            /**< SETUP's ALGORITHM 6-AXIS */
    imu_cal_t    cal;            /**< the calibration */
    uint32_t     cal_from_ms;    /**< when it started */
    bool         cal_lost;       /**< the reading went stale in it */
    uint32_t     cal_left_ms;    /**< as of the last tick */
    bool         rate_due;       /**< RATE to send once connected */
    bool         algo_due;       /**< ALGORITHM to send once connected */
    uint8_t      out[IMU_OUTBOX][WIT_CMD_LEN]; /**< sensor commands held */
    uint8_t      out_n;          /**< how many */
    uint8_t      out_head;       /**< the oldest's index */
    uint32_t     out_ms;         /**< when the last command went */
    bool         out_sent;       /**< out_ms holds a time */
    bool         angle_seen;     /**< an angle frame since IMU ON */
    uint32_t     gaps;           /**< angle frames that came after the
                                      reading went stale, counted */
    uint32_t     gap_at_ms;      /**< when the last of those went stale */
    uint32_t     zero_gen;       /**< ZERO, CLEAR ZERO, and IMU OFF with a
                                      zero kept, counted */
} imu_sensor_t;

/** @brief Clear: no reading, nothing held, RATE 50Hz.
 *  @param s The sensor, not NULL. */
void imu_sensor_init(imu_sensor_t *s);

/**
 * @brief Take SETUP's RATE and ALGORITHM.  With @p send a value that
 *        differs from the one before is due to the sensor once it is
 *        connected; without it the values are only taken.
 * @param s         The sensor, not NULL.
 * @param rate_code wit_rate_t.
 * @param six       ALGORITHM 6-AXIS (AXIS6 1).
 * @param send      false for the first values a controller takes.
 */
void imu_sensor_values(imu_sensor_t *s, uint8_t rate_code, bool six,
                       bool send);

/**
 * @brief IMU OFF: a running calibration ends FAILED with its exit queued,
 *        a zero kept counts a new zero generation, the reading is
 *        forgotten.  The outbox keeps what it holds.
 * @param s The sensor, not NULL.
 */
void imu_sensor_off(imu_sensor_t *s);

/** @brief Another sensor is bound: NO FRAMES waits for its first valid
 *         reading.  @param s The sensor, not NULL. */
void imu_sensor_rebind(imu_sensor_t *s);

/**
 * @brief Bytes of the sensor's stream into the reading.  An angle frame
 *        IMU_STALE_MS or more after the one before counts a gap, timed at
 *        the stale point, and fails a running calibration.
 * @param s      The sensor, not NULL.
 * @param b      The bytes, not NULL.
 * @param n      Their count.
 * @param now_ms The clock.
 */
void imu_sensor_feed(imu_sensor_t *s, const uint8_t *b, size_t n,
                     uint32_t now_ms);

/**
 * @brief Age the reading, run the calibration's clock, and queue the
 *        RATE and ALGORITHM writes due on a connected sensor: unlock, the
 *        writes, SAVE, once all of them fit in the outbox and no
 *        calibration runs.
 * @param s       The sensor, not NULL.
 * @param now_ms  The clock.
 * @param link_up The sensor's stream flows.
 */
void imu_sensor_tick(imu_sensor_t *s, uint32_t now_ms, bool link_up);

/**
 * @brief The next sensor command, noted as sent: the oldest held, once
 *        IMU_CMD_GAP_MS passed since the one before.  Ask only when the
 *        link can send.
 * @param s      The sensor, not NULL.
 * @param now_ms The clock.
 * @param out    Takes WIT_CMD_LEN bytes.
 * @return true when @p out holds a command.
 */
bool imu_sensor_next(imu_sensor_t *s, uint32_t now_ms,
                     uint8_t out[WIT_CMD_LEN]);

/**
 * @brief Start CALIBRATE: unlock and CALSW 1 queued, IMU_CAL_MS to run.
 * @param s       The sensor, not NULL.
 * @param link_up The sensor's stream flows.
 * @param now_ms  The clock.
 * @return false while not connected or while a calibration runs.
 */
bool imu_sensor_calibrate(imu_sensor_t *s, bool link_up, uint32_t now_ms);

/** @brief ZERO: the current angles as zero.  @param s The sensor, not
 *  NULL.  @return false without a valid reading. */
bool imu_sensor_zero(imu_sensor_t *s);

/** @brief CLEAR ZERO.  @param s The sensor, not NULL. */
void imu_sensor_unzero(imu_sensor_t *s);

/**
 * @brief Whether the sensor leaves the link alone: no calibration runs and
 *        no command waits to go out.  An HC-05 AT operation, which takes
 *        the UART from the sensor, starts only then.
 * @param s The sensor, not NULL.
 * @return Whether it does.
 */
bool imu_sensor_quiet(const imu_sensor_t *s);

/**
 * @brief The sensor's part of the link state, with its stream flowing.
 * @param s The sensor, not NULL.
 * @return IMU_LINK_CONNECTED with a valid reading, IMU_LINK_NO_FRAMES when
 *         one came since IMU ON or the bind, else IMU_LINK_BOUND.
 */
imu_link_t imu_sensor_link(const imu_sensor_t *s);

/**
 * @brief The sensor's part of the view: the calibration and the reading.
 * @param s   The sensor, not NULL.
 * @param out Takes cal, cal_left_ms and r.
 */
void imu_sensor_view(const imu_sensor_t *s, imu_view_t *out);

/**
 * @brief Which IMU readings go on to SERVO: each frame once, and a reading
 *        that went stale once, also one that was valid again before SERVO
 *        looked.
 */
typedef struct {
    bool     given;      /**< one went on since the reset */
    bool     was_valid;  /**< that one was valid */
    uint32_t last_taken; /**< taken_ms of the last valid one */
    uint32_t gaps;       /**< imu_sensor_t's gaps as last seen */
} imu_handout_t;

/** @brief What imu_handout_next() says goes on to SERVO. */
typedef enum {
    IMU_HAND_NONE,     /**< nothing: the reading went on already */
    IMU_HAND_READING,  /**< the reading, valid or stale */
    IMU_HAND_GAP,      /**< a stale reading, timed at gap_at_ms: the reading
                            went stale and back before SERVO looked; the
                            next call hands the reading */
} imu_hand_t;

/**
 * @brief Forget what went on: the next reading goes, valid or not.  Called
 *        when ANGLE SOURCE turns to IMU, so SERVO learns the IMU's state
 *        in place of the AS5600's.
 * @param h The handout.
 */
void imu_handout_reset(imu_handout_t *h);

/**
 * @brief What goes on to SERVO now, noted as gone.
 * @param h    The handout.
 * @param r    The reading.
 * @param gaps imu_sensor_t's gaps as published with @p r.
 * @return IMU_HAND_GAP when the reading went stale and back since a valid
 *         one went on; IMU_HAND_READING for a valid reading of a frame not
 *         handed before, and for the first stale one after a valid one or
 *         a reset; IMU_HAND_NONE otherwise.
 */
imu_hand_t imu_handout_next(imu_handout_t *h, const imu_reading_t *r,
                            uint32_t gaps);

#ifdef __cplusplus
}
#endif
