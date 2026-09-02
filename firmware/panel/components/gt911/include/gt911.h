/*
 * GT911 capacitive touch controller driver over I2C (Inter-Integrated
 * Circuit).
 *
 * The GT911 uses 16-bit big-endian register addresses.  Coordinates live in a
 * buffer whose ready flag the host clears after each read; while the flag is
 * set the controller does not update the buffer.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register map (only what this driver needs). */
#define GT911_REG_COMMAND       0x8040
#define GT911_REG_CONFIG        0x8047
#define GT911_REG_X_MAX         0x8048
#define GT911_REG_Y_MAX         0x804A
#define GT911_REG_PRODUCT_ID    0x8140
#define GT911_REG_FW_VERSION    0x8144
#define GT911_REG_STATUS        0x814E
#define GT911_REG_POINT0        0x814F

#define GT911_STATUS_READY      0x80
#define GT911_STATUS_COUNT_MASK 0x0F
#define GT911_POINT_STRIDE      8

typedef struct gt911_dev_t *gt911_handle_t;

typedef struct {
    uint16_t i2c_addr;      /**< 0 -> probe 0x5D then 0x14      */
    uint32_t scl_speed_hz;  /**< 0 -> 400 kHz                   */
} gt911_config_t;

/** Attach to a controller already out of reset. */
esp_err_t gt911_new(i2c_master_bus_handle_t bus, const gt911_config_t *cfg,
                    gt911_handle_t *out);

esp_err_t gt911_del(gt911_handle_t h);

/** I2C address the driver settled on. */
uint16_t gt911_address(gt911_handle_t h);

/** Product id string ("911") and firmware version. */
esp_err_t gt911_info(gt911_handle_t h, char product_id[5], uint16_t *fw_version);

/** Resolution the controller was configured with. */
esp_err_t gt911_resolution(gt911_handle_t h, uint16_t *x_max, uint16_t *y_max);

/**
 * Read the current contacts and acknowledge the buffer.
 *
 * @param out       receives up to @p max points, in controller coordinates
 * @param max       capacity of @p out
 * @param count     receives the number of contacts (0 when all fingers lifted)
 * @param fresh     optional; false means the controller had no new data and
 *                  @p count/@p out were left untouched
 */
esp_err_t gt911_read(gt911_handle_t h, touch_point_t *out, int max,
                     int *count, bool *fresh);

#ifdef __cplusplus
}
#endif
