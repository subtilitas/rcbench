/**
 * @file
 * @brief The Bluetooth IMU's controller: joins the Bluetooth LE link
 *        (imu_ble.h) and the sensor side (imu_sensor.h), takes SETUP's
 *        values, the panel's Bluetooth state and the screen's commands, and
 *        gives the view the IMU screen and SETUP > SENSORS show.  Pure C:
 *        the panel's IMU task (firmware/panel/main/panel_imu.c) hands in the
 *        host stack's events and performs the actions.
 *
 * The sensor's stream flows while IMU is ON, a sensor is bound and the
 * link is subscribed to its notifications.  A bind is a tap on a scanned
 * sensor; SETUP keeps its address with its type and its name.
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

#include "bleuart.h"
#include "imu.h"
#include "imu_ble.h"
#include "imu_screen.h"
#include "imu_sensor.h"
#include "witmotion.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What SETUP sets: SET_IMU_EN, the bound sensor, RATE, ALGORITHM. */
typedef struct {
    bool    en;                       /**< IMU ON */
    char    addr[IMU_ADDR_MAX];       /**< SET_TEXT_IMU_ADDR: "" none, else
                                           "00:0C:BF:08:19:29 P" */
    char    name[IMU_NAME_MAX];       /**< SET_TEXT_IMU_NAME */
    uint8_t rate_code;                /**< wit_rate_t */
    bool    six;                      /**< ALGORITHM 6-AXIS (AXIS6 1) */
    uint8_t filter;                   /**< IMU FILTER, 1 to 50; 0 is 1 */
} imu_cfg_t;

/** @brief The controller.  Its members are private but for the tests. */
typedef struct {
    imu_cfg_t    cfg;            /**< SETUP's values, the bind's after one */
    bool         configured;     /**< a first imu_ctl_config() came */
    uint32_t     bind_seq;       /**< binds, counted */
    imu_ble_t    ble;            /**< the link */
    imu_sensor_t sensor;         /**< the sensor side */
} imu_ctl_t;

/** @brief Clear: IMU OFF, nothing bound. @param c NULL does nothing. */
void imu_ctl_init(imu_ctl_t *c);

/**
 * @brief Take SETUP's values.  IMU OFF stops a scan, ends the link and a
 *        calibration; ON connects a bound sensor once Bluetooth is synced.
 *        A RATE or an ALGORITHM that differs from the one before is sent
 *        once the sensor is connected; the first call only takes the
 *        values.  IMU FILTER holds from the next angle frame on.
 * @param c      The controller.
 * @param cfg    The values; NULL does nothing.
 * @param now_ms The clock.
 */
void imu_ctl_config(imu_ctl_t *c, const imu_cfg_t *cfg, uint32_t now_ms);

/**
 * @brief The panel's Bluetooth state and the C6's version.
 * @param c       The controller; NULL does nothing.
 * @param radio   The state.
 * @param version The C6's version as text, "" or NULL unknown.
 * @param now_ms  The clock.
 */
void imu_ctl_radio(imu_ctl_t *c, imu_radio_t radio, const char *version,
                   uint32_t now_ms);

/**
 * @brief A command of the IMU screen.  SCAN starts with IMU ON, Bluetooth
 *        synced and no scan running; BIND takes an index of the last scan;
 *        CALIBRATE only while connected and not calibrating; ZERO only
 *        with a valid reading.
 * @param c      The controller.
 * @param cmd    The command; NULL does nothing.
 * @param now_ms The clock.
 * @return true when the command was taken.
 */
bool imu_ctl_command(imu_ctl_t *c, const imu_cmd_t *cmd, uint32_t now_ms);

/**
 * @brief One event of the host stack: to the link, and a notification's
 *        bytes to the reading while IMU is ON.
 * @param c      The controller.
 * @param e      The event; NULL does nothing.
 * @param now_ms The clock.
 */
void imu_ctl_event(imu_ctl_t *c, const bleu_evt_t *e, uint32_t now_ms);

/**
 * @brief Advance to @p now_ms and take the next action for the host stack.
 *        Call until it returns false, at least every 10ms.
 * @param c      The controller.
 * @param now_ms The clock.
 * @param out    Receives the action.
 * @return true when @p out holds one.
 */
bool imu_ctl_step(imu_ctl_t *c, uint32_t now_ms, bleu_act_t *out);

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
