/*
 * SPDX-License-Identifier: MIT
 */

#include "board.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

typedef struct {
    bool initialised;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t ch422g_set;
    i2c_master_dev_handle_t ch422g_io;
    i2c_master_dev_handle_t ch422g_oc;
    i2c_master_dev_handle_t ch422g_rd;
    uint8_t exio;
} board_state_t;

static board_state_t s_board;

#define I2C_TIMEOUT_MS 100

static esp_err_t add_dev(uint16_t addr, i2c_master_dev_handle_t *out)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_board.bus, &cfg, out);
}

static esp_err_t ch422g_write(i2c_master_dev_handle_t dev, uint8_t value)
{
    return i2c_master_transmit(dev, &value, 1, I2C_TIMEOUT_MS);
}

esp_err_t board_init(void)
{
    if (s_board.initialised) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_PIN_SDA,
        .scl_io_num = BOARD_I2C_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_board.bus), TAG,
                        "i2c bus init failed");

    ESP_RETURN_ON_ERROR(add_dev(BOARD_CH422G_ADDR_WR_SET, &s_board.ch422g_set),
                        TAG, "ch422g WR_SET");
    ESP_RETURN_ON_ERROR(add_dev(BOARD_CH422G_ADDR_WR_IO, &s_board.ch422g_io),
                        TAG, "ch422g WR_IO");
    ESP_RETURN_ON_ERROR(add_dev(BOARD_CH422G_ADDR_WR_OC, &s_board.ch422g_oc),
                        TAG, "ch422g WR_OC");
    ESP_RETURN_ON_ERROR(add_dev(BOARD_CH422G_ADDR_RD_IO, &s_board.ch422g_rd),
                        TAG, "ch422g RD_IO");

    /* IO0..IO7 as push-pull outputs. */
    ESP_RETURN_ON_ERROR(ch422g_write(s_board.ch422g_set, BOARD_CH422G_SET_IO_OE),
                        TAG, "ch422g mode");

    s_board.exio = BOARD_EXIO_DEFAULT;
    ESP_RETURN_ON_ERROR(ch422g_write(s_board.ch422g_io, s_board.exio), TAG,
                        "ch422g default state");

    s_board.initialised = true;
    ESP_LOGI(TAG, "I2C%d up on SDA=%d SCL=%d, CH422G state=0x%02x",
             BOARD_I2C_PORT, BOARD_I2C_PIN_SDA, BOARD_I2C_PIN_SCL, s_board.exio);
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_board.bus;
}

esp_err_t board_exio_write(uint8_t mask)
{
    ESP_RETURN_ON_FALSE(s_board.ch422g_io, ESP_ERR_INVALID_STATE, TAG,
                        "board_init() first");
    ESP_RETURN_ON_ERROR(ch422g_write(s_board.ch422g_io, mask), TAG, "exio write");
    s_board.exio = mask;
    return ESP_OK;
}

esp_err_t board_exio_set(int exio, bool level)
{
    ESP_RETURN_ON_FALSE(exio >= 0 && exio <= 7, ESP_ERR_INVALID_ARG, TAG,
                        "exio %d out of range", exio);
    uint8_t mask = s_board.exio;
    if (level) {
        mask |= (uint8_t)(1u << exio);
    } else {
        mask &= (uint8_t)~(1u << exio);
    }
    return board_exio_write(mask);
}

uint8_t board_exio_state(void)
{
    return s_board.exio;
}

esp_err_t board_exio_read(uint8_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");
    ESP_RETURN_ON_FALSE(s_board.ch422g_rd, ESP_ERR_INVALID_STATE, TAG,
                        "board_init() first");
    return i2c_master_receive(s_board.ch422g_rd, out, 1, I2C_TIMEOUT_MS);
}

esp_err_t board_backlight(bool on)
{
    return board_exio_set(BOARD_EXIO_DISP, on);
}

esp_err_t board_lcd_reset(bool released)
{
    return board_exio_set(BOARD_EXIO_LCD_RST, released);
}

esp_err_t board_sd_cs(bool asserted)
{
    return board_exio_set(BOARD_EXIO_SD_CS, !asserted);
}

esp_err_t board_select_can(bool can)
{
    return board_exio_set(BOARD_EXIO_USB_SEL, can);
}

esp_err_t board_touch_reset_sequence(void)
{
    ESP_RETURN_ON_ERROR(board_init(), TAG, "board_init");

    /* Drive INT low first: the GT911 samples it on the rising edge of RST to
     * pick between I2C address 0x5D (low) and 0x14 (high). */
    const gpio_config_t int_out = {
        .pin_bit_mask = 1ULL << BOARD_TOUCH_PIN_INT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_out), TAG, "touch int as output");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_TOUCH_PIN_INT, 0), TAG, "int low");

    ESP_RETURN_ON_ERROR(board_exio_set(BOARD_EXIO_TOUCH_RST, false), TAG,
                        "assert touch reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(board_exio_set(BOARD_EXIO_TOUCH_RST, true), TAG,
                        "release touch reset");
    /* GT911 needs >5 ms with INT still held before it samples the address, and
     * ~50 ms in total before it answers on the bus. */
    vTaskDelay(pdMS_TO_TICKS(10));

    const gpio_config_t int_in = {
        .pin_bit_mask = 1ULL << BOARD_TOUCH_PIN_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_in), TAG, "touch int as input");
    vTaskDelay(pdMS_TO_TICKS(60));

    ESP_LOGI(TAG, "GT911 reset done (INT held low -> address 0x%02x)",
             BOARD_TOUCH_I2C_ADDR);
    return ESP_OK;
}
