/*
 * Board bring-up for the 7-inch ESP32-P4 display board: the touch I2C
 * (Inter-Integrated Circuit) bus, the backlight's PWM (pulse-width
 * modulation) and the touch controller's reset.  Every line is a GPIO
 * (general-purpose input/output) of the SoC (system on chip); the board
 * has no I/O expander.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "board_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bring up the touch I2C bus and the backlight PWM, the backlight at 0 %
 * (dark).  Safe to call repeatedly.
 */
esp_err_t board_init(void);

/** The touch bus: the GT911 and, in a build with it, the touch injector. */
i2c_master_bus_handle_t board_i2c_bus(void);

/** Backlight brightness, 0 to 100 %; values outside are clamped. */
esp_err_t board_backlight_percent(int percent);

/** Backlight at 100 % (true) or 0 % (false). */
esp_err_t board_backlight(bool on);

/**
 * Run the GT911 reset on BOARD_TOUCH_PIN_RST: low 20 ms, high, then 60 ms
 * before the first read.  Does nothing with GPIO_NUM_NC.  INT is not
 * wired to the SoC, so the address the GT911 takes is its own choice;
 * gt911_new() probes 0x5D, then 0x14.
 */
esp_err_t board_touch_reset_sequence(void);

#ifdef __cplusplus
}
#endif
