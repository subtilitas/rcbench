/**
 * @file
 * @brief The Bluetooth IMU's controller: joins the HC-05 link (imu_hc05.h)
 *        and the sensor side (imu_sensor.h) on one UART, takes SETUP's
 *        values and the screen's commands, and gives the view the IMU
 *        screen and SETUP > SENSORS show.  Pure C: the panel's driver
 *        (firmware/panel/main/panel_imu.c) moves the bytes, drives KEY and
 *        sets the baud.
 *
 * The link decides where the UART's bytes go and when a sensor command
 * can go; the sensor side holds the reading, the calibration and the
 * commands.  The sensor's stream flows while IMU is ON, the link reads
 * IMU_LINK_READY and a sensor is bound.
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
#include "imu_hc05.h"
#include "imu_screen.h"
#include "imu_sensor.h"
#include "witmotion.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What SETUP sets: SET_IMU_EN, the bound sensor, RATE, ALGORITHM. */
typedef struct {
    bool    en;                       /**< IMU ON */
    char    addr[IMU_ADDR_MAX];       /**< SET_TEXT_IMU_ADDR: "" none,
                                           IMU_ADDR_ANY any sensor */
    char    name[IMU_NAME_MAX];       /**< SET_TEXT_IMU_NAME */
    uint8_t rate_code;                /**< wit_rate_t */
    bool    six;                      /**< ALGORITHM 6-AXIS (AXIS6 1) */
} imu_cfg_t;

/** @brief The controller.  Its members are private but for the tests. */
typedef struct {
    imu_cfg_t    cfg;            /**< SETUP's values, the bind's after one */
    bool         configured;     /**< a first imu_ctl_config() came */
    uint32_t     bind_seq;       /**< binds and ANYs that ended DONE */
    imu_hc05_t   hc05;           /**< the link */
    imu_sensor_t sensor;         /**< the sensor side */
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

#ifdef __cplusplus
}
#endif
