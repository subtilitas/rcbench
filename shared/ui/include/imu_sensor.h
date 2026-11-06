/**
 * @file
 * @brief The sensor side of the Bluetooth IMU, whichever link carries it:
 *        the BWT901CL's reading (imu.h) from its stream, the outbox of its
 *        WitMotion commands, the calibration, the RSW, RATE and ALGORITHM
 *        writes due, the stale stretches counted, and the handout to SERVO.  Pure
 *        C, no radio: the controller (imu_ctl.h) joins it to a link.
 *
 * The link tells the sensor two things: whether the sensor's stream flows
 * (link_up: the BLE link is subscribed to the sensor's notifications) and whether a
 * 5-byte command can go now (imu_sensor_next()'s caller asks the link
 * first).  The stream's bytes come in order, in chunks of any length.
 * The sensor is connected while the link is up and the reading is valid.
 *
 * The commands go each after the unlock, IMU_CMD_GAP_MS apart: CALIBRATE
 * is CALSW 1, IMU_CAL_MS, CALSW 0, SAVE; a RATE or ALGORITHM change is the
 * register and SAVE, queued while the sensor is connected or when it next
 * connects.  RSW IMU_RSW and SAVE go once on a connection whose first
 * IMU_RSW_FRAMES frames show other types than IMU_RSW's: the stream is
 * the read-back, so a sensor that holds IMU_RSW gets no write.
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
/** @brief The frames the panel asks the sensor for (register RSW): 0x52
 *         for the battery, 0x53 for the angles; 2 frames a sample. */
#define IMU_RSW            (WIT_RSW_ANGLE | WIT_RSW_GYRO)
/** @brief Good frames of a connection the RSW check waits for: 4 samples
 *         at IMU_RSW, 2 at WIT_RSW_DEFAULT. */
#define IMU_RSW_FRAMES     8u

/** @name What a held command is for, as bits: a group a failed write
 *  drops is due again at the next connection.
 *  @{ */
#define IMU_OUT_EXIT  0x01u  /**< the calibration's exit */
#define IMU_OUT_RATE  0x02u  /**< a RATE write */
#define IMU_OUT_ALGO  0x04u  /**< an ALGORITHM write */
#define IMU_OUT_CAL   0x08u  /**< the calibration's start: a failure
                                  fails it, nothing comes back */
#define IMU_OUT_RSW   0x10u  /**< an RSW write */
/** @} */

/** @brief The sensor side.  Its members are private but for the tests. */
typedef struct {
    imu_t        m;              /**< the reading */
    bool         had_frames;     /**< a valid reading since IMU ON or the
                                      last imu_sensor_rebind() */
    uint8_t      rate_code;      /**< SETUP's RATE, wit_rate_t */
    uint8_t      filter;         /**< SETUP's IMU FILTER, 1 to 50 */
    bool         link_was_up;    /**< the stream flowed at the last tick */
    bool         six;            /**< SETUP's ALGORITHM 6-AXIS */
    imu_cal_t    cal;            /**< the calibration */
    uint32_t     cal_from_ms;    /**< when it started */
    bool         cal_lost;       /**< the reading went stale in it */
    uint32_t     cal_left_ms;    /**< as of the last tick */
    bool         rate_due;       /**< RATE to send once connected */
    bool         algo_due;       /**< ALGORITHM to send once connected */
    bool         rsw_due;        /**< RSW IMU_RSW to send once connected */
    bool         rsw_checked;    /**< this connection's frame types were
                                      held against IMU_RSW */
    uint8_t      out[IMU_OUTBOX][WIT_CMD_LEN]; /**< sensor commands held */
    uint8_t      out_n;          /**< how many */
    uint8_t      out_head;       /**< the oldest's index */
    uint32_t     out_ms;         /**< when the last command went */
    bool         out_sent;       /**< out_ms holds a time */
    uint8_t      out_for[IMU_OUTBOX]; /**< IMU_OUT_* of each held command */
    uint8_t      sent_for;       /**< IMU_OUT_* of the last command sent */
    bool         exit_due;       /**< the calibration's exit to queue again */
    bool         held;           /**< a write failed: what it dropped waits
                                      for the next connection */
    bool         angle_seen;     /**< an angle frame since IMU ON */
    uint32_t     gaps;           /**< angle frames that came after the
                                      reading went stale, counted */
    uint32_t     gap_at_ms;      /**< when the last of those went stale */
    uint32_t     zero_gen;       /**< ZERO, CLEAR ZERO, and IMU OFF with a
                                      zero kept, counted */
} imu_sensor_t;

/** @brief Clear: no reading, nothing held, RATE 50Hz, IMU FILTER 1.
 *  @param s The sensor, not NULL. */
void imu_sensor_init(imu_sensor_t *s);

/**
 * @brief Take SETUP's IMU FILTER, held to 1 to 50 (imu_filter_clamp()).
 *        It holds from the next angle frame on, also after IMU OFF.
 * @param s      The sensor, not NULL.
 * @param filter The setting as stored.
 */
void imu_sensor_filter(imu_sensor_t *s, int32_t filter);

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
 *         reading, which seeds the filter, and its frame types are held
 *         against IMU_RSW again.  @param s The sensor, not NULL. */
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
 *        RSW, RATE and ALGORITHM writes due on a connected sensor: unlock,
 *        the writes, SAVE, once all of them fit in the outbox and no
 *        calibration runs.  A stream that flows again after it stopped
 *        (a connect or a reconnect) seeds the filter with its next angle
 *        frame and counts its frame types again: once IMU_RSW_FRAMES good
 *        frames came, types other than IMU_RSW's make RSW due.
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
 *        no command waits to go out.
 * @param s The sensor, not NULL.
 * @return Whether it does.
 */
bool imu_sensor_quiet(const imu_sensor_t *s);

/**
 * @brief Drop the commands held: the link did not take one, and the rest
 *        of its group (unlock, register, SAVE) must not go alone.  What
 *        the dropped commands and the failed one were for is due again:
 *        the calibration's exit (unlock, CALSW 0, SAVE), an RSW, a RATE,
 *        an ALGORITHM.  It is queued at the next connection (imu_sensor_tick()
 *        with the stream flowing again), with SETUP's values then.  A
 *        running calibration goes on and queues its exit at its end.
 * @param s The sensor, not NULL.
 */
void imu_sensor_flush(imu_sensor_t *s);

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
