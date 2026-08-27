/*
 * Board bring-up: the shared I2C bus and the CH422G I/O expander that sits
 * between the SoC and the panel's DISP/backlight, LCD reset, touch reset,
 * SD chip-select and USB/CAN mux.
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
 * Bring up the I2C master bus and put the CH422G into push-pull output mode
 * with BOARD_EXIO_DEFAULT applied.  Safe to call more than once.
 */
esp_err_t board_init(void);

/** The shared I2C bus (touch controller, expander, external sensors). */
i2c_master_bus_handle_t board_i2c_bus(void);

/** Write the whole EXIO output byte at once. */
esp_err_t board_exio_write(uint8_t mask);

/** Drive a single EXIO line (0..7) and remember the new state. */
esp_err_t board_exio_set(int exio, bool level);

/** Last value written to the expander. */
uint8_t board_exio_state(void);

/** Read the CH422G input byte. */
esp_err_t board_exio_read(uint8_t *out);

/** Panel DISP + backlight enable. */
esp_err_t board_backlight(bool on);

/** Release (true) or assert (false) the panel reset line. */
esp_err_t board_lcd_reset(bool released);

/**
 * Run the GT911 reset sequence.  INT is held low across the rising edge of
 * RST so the controller latches I2C address 0x5D.  Leaves INT as an input.
 */
esp_err_t board_touch_reset_sequence(void);

/** Assert (true) or release (false) the SD card chip select. */
esp_err_t board_sd_cs(bool asserted);

/** false routes the USB-C data pins to the SoC, true routes them to the CAN PHY. */
esp_err_t board_select_can(bool can);

#ifdef __cplusplus
}
#endif
