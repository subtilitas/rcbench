/**
 * @file panel_imu.h
 * @brief The Bluetooth IMU on the panel: the client of the `blec` task
 *        (panel_blec.h) that runs the controller (imu_ctl.h) over Bluetooth
 *        LE (panel_ble.h), and the frame task's side of it: the IMU
 *        screen's view and commands, SETUP's values, the bind kept in
 *        SETUP, and the angle source of the servo runs.
 *
 * The `blec` task owns the controller, through the panel_imu_blec_*()
 * functions.  The frame task posts SETUP's values and the screen's
 * commands to it through a queue, and reads what it publishes under a
 * spinlock.  The IMU's reading is no safety reading: it is stale
 * after IMU_STALE_MS, and a run rides through its gaps as through the
 * AS5600's (servo_gap.h).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bleuart.h"
#include "servo_test_io.h"
#include "settings.h"

/**
 * @brief Set up the controller and its queue, and hand it SETUP's values.
 *        Once, at start-up, after the settings are loaded and before
 *        panel_blec_start().  Bluetooth starts at the first IMU ON.
 */
void panel_imu_start(void);

/**
 * @brief The `blec` task's turn, first: SETUP's values and the screen's
 *        commands to the controller, a first IMU ON starting Bluetooth,
 *        then the panel's Bluetooth state.  Nothing before
 *        panel_imu_start().
 * @param now The clock, ms.
 */
void panel_imu_blec_poll(uint32_t now);

/**
 * @brief An event of the host stack the hub routes to the IMU, on the
 *        `blec` task.
 * @param e   The event.
 * @param now The clock, ms.
 */
void panel_imu_blec_event(const bleu_evt_t *e, uint32_t now);

/**
 * @brief The controller's next action, on the `blec` task; a sensor command
 *        written on 8841 is logged.
 * @param now The clock, ms.
 * @param out Receives the action.
 * @return true when @p out holds one.
 */
bool panel_imu_blec_step(uint32_t now, bleu_act_t *out);

/**
 * @brief The `blec` task's turn, last: the view published for the frame
 *        task, a change of the link's state and once a second the reading
 *        logged.
 * @param now The clock, ms.
 */
void panel_imu_blec_publish(uint32_t now);

/**
 * @brief A value changed: SET_IMU_* go to the task; RATE and ALGORITHM give
 *        their notice (sensors_rows_imu_notice()); AXIS and SIGN name the
 *        servo runs' source (servo_screen_set_imu_axis()).  Before
 *        panel_imu_start() nothing is posted.
 * @param id The setting.
 */
void panel_imu_settings_changed(setting_id_t id);

/**
 * @brief A string changed: the bound sensor's address and name go to the
 *        task.
 * @param id The string.
 */
void panel_imu_text_changed(setting_text_id_t id);

/**
 * @brief The frame task's turn: the view to the IMU screen and to SETUP >
 *        SENSORS, the screen's commands to the task, and a bind that ended
 *        into SETUP's strings with a save asked.
 */
void panel_imu_frame(void);

/**
 * @brief The IMU's angle as the servo runs read it, each reading once: the
 *        axis and the sign SETUP sets, from the zero, in AS5600 counts
 *        (servo_angle_enc()); a reading that went stale once, timed now.
 * @param fresh true in the first frame of ANGLE SOURCE IMU: the reading
 *              goes whatever went before, valid or not, so SERVO drops the
 *              AS5600's state.
 * @param out   Receives the reading.
 * @return true when @p out holds one not handed before; call again in the
 *         same frame while it is true, as a stale stretch and the reading
 *         that ended it come as two.
 */
bool panel_imu_angle(bool fresh, servo_test_enc_t *out);
