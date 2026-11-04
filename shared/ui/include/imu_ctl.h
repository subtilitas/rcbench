/**
 * @file
 * @brief The Bluetooth IMU's controller: the HC-05's AT set-up
 *        (protocols/hc05) and the BWT901CL's reading (imu.h) on one UART,
 *        the screen's commands, the sensor's own commands, and the view the
 *        IMU screen and SETUP > SENSORS show.  Pure C: the panel's driver
 *        (firmware/panel/main/panel_imu.c) moves the bytes, drives KEY and
 *        sets the baud.
 *
 * With IMU ON the controller probes the HC-05 (AT at 115200, 38400 and
 * 9600 baud, then AT+VERSION?) and retries every IMU_PROBE_RETRY_MS while
 * no module answers.  While an AT operation runs the UART's bytes go to
 * the HC-05's machine; at any other time they are the sensor's frames.
 * SCAN, BIND and CONNECT TO ANY SENSOR start the hc05 operations of the
 * same names with the BWT901CL's PIN (IMU_PIN).  The sensor's commands
 * (CALIBRATE, RATE, ALGORITHM) go over the Bluetooth link as WitMotion
 * commands, each after the unlock, IMU_CMD_GAP_MS apart: CALIBRATE is
 * CALSW 1, IMU_CAL_MS, CALSW 0, SAVE; a RATE or ALGORITHM change is the
 * register and SAVE, sent while the sensor is connected or when it next
 * connects.
 *
 * Every function of one controller is called from one task.  Times are
 * unsigned differences of a free-running 32-bit ms clock.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hc05.h"
#include "imu.h"
#include "imu_screen.h"
#include "witmotion.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The BWT901CL's PIN (its HC-06 radio's). */
#define IMU_PIN            "1234"
/** @brief The address kept for CONNECT TO ANY SENSOR (AT+CMODE=1). */
#define IMU_ADDR_ANY       "*"
/** @brief A probe again this long after one no module answered, ms. */
#define IMU_PROBE_RETRY_MS 10000u
/** @brief The accelerometer calibration's still time, ms. */
#define IMU_CAL_MS         3000u
/** @brief The gap between two sensor commands, ms. */
#define IMU_CMD_GAP_MS     50u
/** @brief Sensor commands held to send at most. */
#define IMU_OUTBOX         8u

/** @brief What SETUP sets: SET_IMU_EN, the bound sensor, RATE, ALGORITHM. */
typedef struct {
    bool    en;                       /**< IMU ON */
    char    addr[HC05_ADDR_MAX];      /**< SET_TEXT_IMU_ADDR: "" none,
                                           IMU_ADDR_ANY any sensor */
    char    name[HC05_NAME_MAX];      /**< SET_TEXT_IMU_NAME */
    uint8_t rate_code;                /**< wit_rate_t */
    bool    six;                      /**< ALGORITHM 6-AXIS (AXIS6 1) */
} imu_cfg_t;

/** @brief The controller.  Its members are private but for the tests. */
typedef struct {
    imu_cfg_t    cfg;            /**< SETUP's values, the bind's after one */
    bool         configured;     /**< a first imu_ctl_config() came */
    hc05_t       bt;             /**< the HC-05's machine */
    imu_t        m;              /**< the reading */
    bool         op_open;        /**< an operation started, its end unread */
    bool         probed;         /**< the HC-05 answered AT since IMU ON */
    uint32_t     probe_at_ms;    /**< when the next probe goes out */
    bool         had_frames;     /**< an angle frame since the probe or bind */
    hc05_err_t   err;            /**< the last operation's error */
    uint8_t      err_code;       /**< its ERROR:(hh) */
    bool         inq_refused;    /**< the last scan ended ERROR:(1F) */
    hc05_dev_t   found[HC05_FOUND_MAX]; /**< the last scan's devices */
    uint8_t      found_n;        /**< their count */
    uint32_t     bind_seq;       /**< binds and ANYs that ended DONE */
    imu_cal_t    cal;            /**< the calibration */
    uint32_t     cal_from_ms;    /**< when it started */
    bool         cal_lost;       /**< the reading went stale in it */
    uint32_t     cal_left_ms;    /**< as of the last step */
    bool         rate_due;       /**< RATE to send once connected */
    bool         algo_due;       /**< ALGORITHM to send once connected */
    uint8_t      out[IMU_OUTBOX][WIT_CMD_LEN]; /**< sensor commands held */
    uint8_t      out_n;          /**< how many */
    uint8_t      out_head;       /**< the oldest's index */
    uint32_t     out_ms;         /**< when the last command went */
    bool         out_sent;       /**< out_ms holds a time */
} imu_ctl_t;

/** @brief Clear: IMU OFF, nothing bound. @param c NULL does nothing. */
void imu_ctl_init(imu_ctl_t *c);

/**
 * @brief Take SETUP's values.  IMU OFF ends a running operation and a
 *        calibration; ON starts the probe at the next step.  A RATE or an
 *        ALGORITHM that differs from the one before is sent once the sensor
 *        is connected; the first call only takes the values.
 * @param c      The controller.
 * @param cfg    The values; NULL does nothing.
 * @param now_ms The clock.
 */
void imu_ctl_config(imu_ctl_t *c, const imu_cfg_t *cfg, uint32_t now_ms);

/**
 * @brief A command of the IMU screen.  SCAN, BIND and ANY start only with
 *        the HC-05 answering and no operation running; BIND takes an index
 *        of the last scan; CALIBRATE only while connected and not
 *        calibrating; ZERO only with a valid reading.
 * @param c      The controller.
 * @param cmd    The command; NULL does nothing.
 * @param now_ms The clock.
 * @return true when the command was taken.
 */
bool imu_ctl_command(imu_ctl_t *c, const imu_cmd_t *cmd, uint32_t now_ms);

/**
 * @brief Bytes from the UART: to the HC-05's machine while an operation
 *        runs, else to the reading while IMU is ON.
 * @param c      The controller.
 * @param b      The bytes.
 * @param n      Their count.
 * @param now_ms The clock.
 */
void imu_ctl_feed(imu_ctl_t *c, const uint8_t *b, size_t n, uint32_t now_ms);

/**
 * @brief Advance to @p now_ms and take the next action: KEY, a baud, or
 *        bytes to send.  Call until it returns false, at least every 10ms.
 * @param c      The controller.
 * @param now_ms The clock.
 * @param out    Receives the action (hc05_act_t's kinds).
 * @return true when @p out holds one.
 */
bool imu_ctl_step(imu_ctl_t *c, uint32_t now_ms, hc05_act_t *out);

/**
 * @brief What the screens show, as of the last step.
 * @param c   The controller.
 * @param out Receives the view.
 */
void imu_ctl_view(const imu_ctl_t *c, imu_view_t *out);

/**
 * @brief The link state of @p c (imu_view_t.link).
 * @param c The controller; NULL is IMU_LINK_OFF.
 * @return The state.
 */
imu_link_t imu_ctl_link(const imu_ctl_t *c);

/**
 * @brief The RATE option of SET_IMU_RATE as a WitMotion code.
 * @param option 0 10Hz, 1 20Hz, 2 50Hz, 3 100Hz, 4 200Hz; another is 50Hz.
 * @return The wit_rate_t code.
 */
uint8_t imu_ctl_rate_code(int option);

/**
 * @brief Which IMU readings go on to SERVO: each frame once, and a reading
 *        that went stale once.
 */
typedef struct {
    bool     given;      /**< one went on since the reset */
    bool     was_valid;  /**< that one was valid */
    uint32_t last_taken; /**< taken_ms of the last valid one */
} imu_handout_t;

/**
 * @brief Forget what went on: the next reading goes, valid or not.  Called
 *        when ANGLE SOURCE turns to IMU, so SERVO learns the IMU's state
 *        in place of the AS5600's.
 * @param h The handout.
 */
void imu_handout_reset(imu_handout_t *h);

/**
 * @brief Whether @p r goes on to SERVO, and note it when it does.
 * @param h The handout.
 * @param r The reading.
 * @return true for a valid reading of a frame not handed before, and for
 *         the first stale reading after a valid one or after a reset.
 */
bool imu_handout_due(imu_handout_t *h, const imu_reading_t *r);

#ifdef __cplusplus
}
#endif
