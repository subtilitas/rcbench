/*
 * The rotary knob's reader: an AS5600 at 0x36 on the panel's I2C
 * (Inter-Integrated Circuit) bus, polled from a task of its own.
 *
 * The sensor is registered on the bus once, before the control task
 * starts, and the task reads it only while the setting is on, so a panel
 * with the knob off sends nothing to 0x36.  What it hands over is
 * motion, a count of steps since the last take, never an angle: a knob that
 * stops answering stops adding to it.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Register the sensor on the bus.  Call once, after board_init() and before
 * any other task uses the bus.
 */
void knob_task_attach(void);

/** Create the task.  Call once, after knob_task_attach(). */
void knob_task_start(void);

/** Poll the sensor (true) or leave the bus alone (false). */
void knob_task_set_enabled(bool on);

/** The steps turned since the last call, signed; clears the count. */
int knob_task_take(void);
