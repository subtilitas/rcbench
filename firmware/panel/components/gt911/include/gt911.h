/**
 * @file
 * @brief GT911 capacitive touch controller driver over I2C (Inter-Integrated
 *        Circuit).
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
#define GT911_REG_COMMAND       0x8040  /**< The command register. */
/** @brief The first register of the configuration block. */
#define GT911_REG_CONFIG        0x8047
#define GT911_REG_X_MAX         0x8048  /**< The X resolution, 2 bytes. */
#define GT911_REG_Y_MAX         0x804A  /**< The Y resolution, 2 bytes. */
/** @brief The product id string, 4 characters. */
#define GT911_REG_PRODUCT_ID    0x8140
#define GT911_REG_FW_VERSION    0x8144  /**< The firmware version, 2 bytes. */
/** @brief The status: the ready flag and the contact count. */
#define GT911_REG_STATUS        0x814E
#define GT911_REG_POINT0        0x814F  /**< The first contact record. */

/** @brief Status bit: a new reading is ready. */
#define GT911_STATUS_READY      0x80
/** @brief Status bits holding the contact count. */
#define GT911_STATUS_COUNT_MASK 0x0F
#define GT911_POINT_STRIDE      8  /**< Bytes of one contact record. */

/** @brief One GT911 on an I2C bus. */
typedef struct gt911_dev_t *gt911_handle_t;

/** @brief Options of gt911_new(). */
typedef struct {
    uint16_t i2c_addr;      /**< 0 -> probe 0x5D then 0x14      */
    uint32_t scl_speed_hz;  /**< 0 -> 400kHz                    */
} gt911_config_t;

/**
 * @brief Attach to a controller already out of reset.
 * @param bus the I2C bus the controller is on
 * @param cfg options; NULL for the defaults
 * @param out takes the instance
 * @return ESP_OK; ESP_ERR_INVALID_ARG for a NULL @p bus or @p out;
 *         ESP_ERR_NOT_FOUND when no GT911 answered; otherwise the I2C error
 */
esp_err_t gt911_new(i2c_master_bus_handle_t bus, const gt911_config_t *cfg,
                    gt911_handle_t *out);

/**
 * @brief Release an instance and its I2C device.
 * @param h the instance; NULL does nothing
 * @return ESP_OK
 */
esp_err_t gt911_del(gt911_handle_t h);

/**
 * @brief I2C address the driver settled on.
 * @param h the instance
 * @return the address the controller answered on, 0 for a NULL @p h
 */
uint16_t gt911_address(gt911_handle_t h);

/**
 * @brief Product id string ("911") and firmware version.
 * @param h the instance
 * @param product_id takes the product id, 4 characters and the terminator
 * @param fw_version takes the firmware version
 * @return ESP_OK, or the I2C error
 */
esp_err_t gt911_info(gt911_handle_t h, char product_id[5], uint16_t *fw_version);

/**
 * @brief Resolution the controller was configured with.
 * @param h the instance
 * @param x_max takes the X resolution
 * @param y_max takes the Y resolution
 * @return ESP_OK, or the I2C error
 */
esp_err_t gt911_resolution(gt911_handle_t h, uint16_t *x_max, uint16_t *y_max);

/**
 * @brief Read the current contacts and acknowledge the buffer.
 * @param h the instance
 * @param out receives up to @p max points, in controller coordinates
 * @param max capacity of @p out
 * @param count receives the number of contacts (0 when all fingers lifted)
 * @param fresh optional; false means the controller had no new data and
 *        @p count and @p out were left untouched
 * @return ESP_OK, or the I2C error
 */
esp_err_t gt911_read(gt911_handle_t h, touch_point_t *out, int max,
                     int *count, bool *fresh);

#ifdef __cplusplus
}
#endif
