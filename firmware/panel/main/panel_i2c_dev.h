/*
 * A device of the builder's on the panel's I2C (Inter-Integrated Circuit)
 * bus, board_i2c_bus(): the bus of the panel's 4-pin I2C terminal, shared
 * with the CH422G expander and the GT911 touch controller.
 *
 * Every device the panel adds to that bus beside the board's own goes
 * through panel_i2c_add_device().  It refuses an address in the board's
 * table (shared/bench/include/panel_i2c.h), so no transaction of such a
 * device can reach the CH422G as a command.
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
 * owns it once and leaves *@p out NULL.  Call before any task uses the
 * bus: the driver's add is not safe against a transaction under way.
 */
esp_err_t panel_i2c_add_device(unsigned addr, i2c_master_dev_handle_t *out);
