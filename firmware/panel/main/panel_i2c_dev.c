/*
 * SPDX-License-Identifier: MIT
 */

#include "panel_i2c_dev.h"

#include <stddef.h>

#include "board.h"
#include "board_pins.h"
#include "esp_log.h"
#include "panel_i2c.h"

static const char *TAG = "i2c_dev";

esp_err_t panel_i2c_add_device(unsigned addr, i2c_master_dev_handle_t *out)
{
    *out = NULL;
    const char *owner = panel_i2c_owner(addr);
    if (owner != NULL) {
        ESP_LOGE(TAG, "0x%02x refused: %s answers there on this bus", addr,
                 owner);
        return ESP_ERR_INVALID_ARG;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = (uint16_t)addr,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    const esp_err_t err = i2c_master_bus_add_device(board_i2c_bus(), &cfg, out);
    if (err != ESP_OK) {
        *out = NULL;
    }
    return err;
}
