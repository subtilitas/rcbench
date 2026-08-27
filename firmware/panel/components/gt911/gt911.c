/*
 * SPDX-License-Identifier: MIT
 */

#include "gt911.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "gt911";

#define GT911_I2C_TIMEOUT_MS 100
#define GT911_ADDR_PRIMARY   0x5D
#define GT911_ADDR_SECONDARY 0x14

struct gt911_dev_t {
    i2c_master_dev_handle_t dev;
    uint16_t addr;
};

static esp_err_t reg_read(gt911_handle_t h, uint16_t reg, uint8_t *buf, size_t len)
{
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(h->dev, addr, sizeof(addr), buf, len,
                                       GT911_I2C_TIMEOUT_MS);
}

static esp_err_t reg_write8(gt911_handle_t h, uint16_t reg, uint8_t value)
{
    uint8_t payload[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), value };
    return i2c_master_transmit(h->dev, payload, sizeof(payload),
                               GT911_I2C_TIMEOUT_MS);
}

esp_err_t gt911_new(i2c_master_bus_handle_t bus, const gt911_config_t *cfg,
                    gt911_handle_t *out)
{
    ESP_RETURN_ON_FALSE(bus && out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    gt911_config_t c = { 0 };
    if (cfg) {
        c = *cfg;
    }
    if (c.scl_speed_hz == 0) {
        c.scl_speed_hz = 400000;
    }

    uint16_t candidates[2];
    int n_candidates;
    if (c.i2c_addr != 0) {
        candidates[0] = c.i2c_addr;
        n_candidates = 1;
    } else {
        candidates[0] = GT911_ADDR_PRIMARY;
        candidates[1] = GT911_ADDR_SECONDARY;
        n_candidates = 2;
    }

    struct gt911_dev_t *h = calloc(1, sizeof(*h));
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "no mem");

    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (int i = 0; i < n_candidates; ++i) {
        const i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = candidates[i],
            .scl_speed_hz = c.scl_speed_hz,
        };
        err = i2c_master_bus_add_device(bus, &dev_cfg, &h->dev);
        if (err != ESP_OK) {
            continue;
        }
        h->addr = candidates[i];

        char id[5] = { 0 };
        if (gt911_info(h, id, NULL) == ESP_OK && id[0] == '9') {
            ESP_LOGI(TAG, "found GT%s at 0x%02x", id, h->addr);
            *out = h;
            return ESP_OK;
        }
        i2c_master_bus_rm_device(h->dev);
        h->dev = NULL;
        err = ESP_ERR_NOT_FOUND;
    }

    free(h);
    ESP_LOGE(TAG, "no GT911 responded on the bus");
    return err;
}

esp_err_t gt911_del(gt911_handle_t h)
{
    if (!h) {
        return ESP_OK;
    }
    if (h->dev) {
        i2c_master_bus_rm_device(h->dev);
    }
    free(h);
    return ESP_OK;
}

uint16_t gt911_address(gt911_handle_t h)
{
    return h ? h->addr : 0;
}

esp_err_t gt911_info(gt911_handle_t h, char product_id[5], uint16_t *fw_version)
{
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_ARG, TAG, "null handle");
    uint8_t buf[6] = { 0 };
    ESP_RETURN_ON_ERROR(reg_read(h, GT911_REG_PRODUCT_ID, buf, sizeof(buf)),
                        TAG, "read product id");
    if (product_id) {
        memcpy(product_id, buf, 4);
        product_id[4] = '\0';
    }
    if (fw_version) {
        *fw_version = (uint16_t)(buf[4] | ((uint16_t)buf[5] << 8));
    }
    return ESP_OK;
}

esp_err_t gt911_resolution(gt911_handle_t h, uint16_t *x_max, uint16_t *y_max)
{
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_ARG, TAG, "null handle");
    uint8_t buf[4] = { 0 };
    ESP_RETURN_ON_ERROR(reg_read(h, GT911_REG_X_MAX, buf, sizeof(buf)),
                        TAG, "read resolution");
    if (x_max) { *x_max = (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8)); }
    if (y_max) { *y_max = (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8)); }
    return ESP_OK;
}

esp_err_t gt911_read(gt911_handle_t h, touch_point_t *out, int max,
                     int *count, bool *fresh)
{
    ESP_RETURN_ON_FALSE(h && count, ESP_ERR_INVALID_ARG, TAG, "bad args");

    if (!out || max < 0) {
        max = 0;
    }
    if (fresh) {
        *fresh = false;
    }

    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(reg_read(h, GT911_REG_STATUS, &status, 1), TAG, "status");

    if (!(status & GT911_STATUS_READY)) {
        return ESP_OK; /* nothing new since the last read */
    }

    int n = status & GT911_STATUS_COUNT_MASK;
    if (n > TOUCH_MAX_POINTS) {
        /* Corrupt status byte -- clear it and try again next round. */
        (void)reg_write8(h, GT911_REG_STATUS, 0);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t buf[TOUCH_MAX_POINTS * GT911_POINT_STRIDE];
    if (n > 0) {
        esp_err_t err = reg_read(h, GT911_REG_POINT0, buf,
                                 (size_t)n * GT911_POINT_STRIDE);
        if (err != ESP_OK) {
            (void)reg_write8(h, GT911_REG_STATUS, 0);
            ESP_RETURN_ON_ERROR(err, TAG, "read points");
        }
    }

    /* Acknowledge before parsing so the controller can start on the next
     * sample while we work. */
    ESP_RETURN_ON_ERROR(reg_write8(h, GT911_REG_STATUS, 0), TAG, "ack");

    int written = (n < max) ? n : max;
    for (int i = 0; i < written; ++i) {
        const uint8_t *p = &buf[i * GT911_POINT_STRIDE];
        out[i].id = p[0];
        out[i].x = (int16_t)(p[1] | ((uint16_t)p[2] << 8));
        out[i].y = (int16_t)(p[3] | ((uint16_t)p[4] << 8));
        out[i].strength = (uint16_t)(p[5] | ((uint16_t)p[6] << 8));
    }

    *count = written;
    if (fresh) {
        *fresh = true;
    }
    return ESP_OK;
}
