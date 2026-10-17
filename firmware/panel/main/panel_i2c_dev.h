/*
 * A device of the builder's on the panel's I2C (Inter-Integrated Circuit)
 * bus, board_i2c_bus(): the bus of the panel's 4-pin I2C terminal, shared
 * with the CH422G expander and the GT911 touch controller.
 *
 * Every device the panel adds to that bus beside the board's own goes
 * through panel_i2c_add_device(), and every probe of one through
 * panel_i2c_probe().  Both refuse an address in the board's table
 * (shared/bench/include/panel_i2c.h), so no transaction of such a device
 * can reach the CH422G as a command.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/**
 * Register a device at 7-bit address @p addr on board_i2c_bus(), clocked
 * at the bus's BOARD_I2C_FREQ_HZ.  Registering sends nothing.  An address
 * the board answers at returns ESP_ERR_INVALID_ARG, logs the chip that
 * owns it once and leaves *@p out NULL.  The driver inserts the device
 * under its bus lock (ESP-IDF v5.4), so other tasks may be using the bus.
 */
esp_err_t panel_i2c_add_device(unsigned addr, i2c_master_dev_handle_t *out);

/**
 * Probe 7-bit address @p addr on board_i2c_bus(): a START, the address
 * byte, a STOP, at the driver's 100 kHz.  ESP_OK on an acknowledge,
 * ESP_ERR_NOT_FOUND without one; the driver logs neither.  An address the
 * board answers at returns ESP_ERR_INVALID_ARG with nothing sent and
 * nothing logged, so a caller polling it does not flood the console.
 */
esp_err_t panel_i2c_probe(unsigned addr, int timeout_ms);
