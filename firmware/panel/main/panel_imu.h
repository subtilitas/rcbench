/**
 * @file panel_imu.h
 * @brief The Bluetooth IMU on the panel: the HC-05's UART and KEY line, the
 *        task that runs the controller (imu_ctl.h), and the frame task's
 *        side of it: the IMU screen's view and commands, SETUP's values,
 *        the bind kept in SETUP, and the angle source of the servo runs.
 *
 * The task owns the controller.  The frame task posts SETUP's values and
 * the screen's commands to it through a queue, and reads what it publishes
 * under a spinlock.  The IMU's reading is no safety reading: it is stale
 * after IMU_STALE_MS, and a run rides through its gaps as through the
 * AS5600's (servo_gap.h).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "servo_test_io.h"
#include "settings.h"

/**
 * @brief Set up the UART (PANEL_IMU_UART_NUM at PANEL_IMU_BAUD), the KEY line
 *        low, the queue and the task, and hand it SETUP's values.  Once, at
 *        start-up, after the settings are loaded.
 */
void panel_imu_start(void);

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
 * @param out Receives the reading.
 * @return true when @p out holds one not handed before.
 */
bool panel_imu_angle(servo_test_enc_t *out);
