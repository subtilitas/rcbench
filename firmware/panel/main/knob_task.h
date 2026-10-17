/*
 * The rotary knob's reader: an MT6701 at 0x06 on the panel's I2C
 * (Inter-Integrated Circuit) bus, polled from a task of its own.
 *
 * The sensor is registered on the bus once, before the control task
 * starts, and the task reads it only while the setting is on, so a panel
 * with the knob off sends nothing to 0x06.  While the knob has no
 * reference the task probes 0x06 before reading, so a panel with the knob
 * on and no sensor logs nothing per poll.  What it hands over is
 * motion, a count of steps since the last take, never an angle: a knob that
 * stops answering stops adding to it.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Register the sensor on the bus.  Call once, after board_init().  The
 * touch task already reads the bus by then; ESP-IDF v5.4's
 * i2c_master_bus_add_device() inserts the device under the bus lock, so the
 * add waits out a touch transaction instead of racing it.
 */
void knob_task_attach(void);

/** Create the task.  Call once, after knob_task_attach(). */
void knob_task_start(void);

/** Poll the sensor (true) or leave the bus alone (false). */
void knob_task_set_enabled(bool on);

/** The steps turned since the last call, signed; clears the count. */
int knob_task_take(void);
