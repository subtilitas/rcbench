/**
 * @file
 * @brief The FT3267 touch controller at I2C address 0x38 on the internal
 *        bus (SDA GPIO11, SCL GPIO12, 400kHz).
 *
 * The FT3267 answers the FT5x06 family's registers, as the board support
 * package drives it: 0x02 the number of touch points (low 4 bits), then
 * the first point's X high (low 4 bits), X low, Y high, Y low.  The read is
 * polled; the interrupt line (GPIO14) is not used in this slice.  Its reset
 * is the display's (GPIO8): dial_lcd_init() resets both, so this runs
 * after it.  The bus also carries the RTC (real-time clock) at 0x51 and
 * the RFID reader at 0x28, which the Dial does not use.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dial.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

/** @brief The controller's address, bus clock and one read's timeout. @{ */
#define TOUCH_ADDR       0x38u
#define TOUCH_HZ         400000u /**< the bus clock, Hz */
#define TOUCH_TIMEOUT_MS 10 /**< one read's timeout, ms */
/** @} */
/** @brief The first register a read covers: TD_STATUS. */
#define TOUCH_REG_STATUS 0x02u

/** @brief The log's tag. */
static const char *TAG = "touch";
/** @brief The bus and the device. @{ */
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev; /**< the controller on the bus */
/** @} */

bool dial_touch_init(void)
{
    const i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = DIAL_PIN_I2C_SDA,
        .scl_io_num = DIAL_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    const i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_ADDR,
        .scl_speed_hz = TOUCH_HZ,
    };
    uint8_t x;
    uint8_t y;
    if (i2c_new_master_bus(&bus, &s_bus) != ESP_OK
        || i2c_master_bus_add_device(s_bus, &dev, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "the I2C bus did not start");
        s_dev = NULL;
        return false;
    }
    if (i2c_master_probe(s_bus, TOUCH_ADDR, TOUCH_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "no answer at 0x%02X", (unsigned)TOUCH_ADDR);
        return false;
    }
    (void)dial_touch_read(&x, &y);
    return true;
}

bool dial_touch_read(uint8_t *x, uint8_t *y)
{
    const uint8_t reg = TOUCH_REG_STATUS;
    uint8_t d[5];
    unsigned px;
    unsigned py;
    if (s_dev == NULL
        || i2c_master_transmit_receive(s_dev, &reg, 1, d, sizeof d,
                                       TOUCH_TIMEOUT_MS) != ESP_OK) {
        return false;
    }
    if ((d[0] & 0x0Fu) == 0u || (d[0] & 0x0Fu) > 2u) {
        return false;   /* no point, or a count the part does not give */
    }
    px = ((unsigned)(d[1] & 0x0Fu) << 8) | d[2];
    py = ((unsigned)(d[3] & 0x0Fu) << 8) | d[4];
    *x = (uint8_t)(px > DK_TOUCH_MAX ? DK_TOUCH_MAX : px);
    *y = (uint8_t)(py > DK_TOUCH_MAX ? DK_TOUCH_MAX : py);
    return true;
}
