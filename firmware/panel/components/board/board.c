/**
 * @file board.c
 * @brief Board bring-up for the 7-inch ESP32-P4 display board: touch I2C
 *        bus, backlight PWM and the GT911 reset.
 *
 * SPDX-License-Identifier: MIT
 */

#include "board.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

/** @brief The board's state, set once by board_init(). */
static struct {
    /**
     * @var s_board
     * @brief The board's state, set once by board_init().
     */
    i2c_master_bus_handle_t bus;    /**< the touch bus                     */
} s_board;

/** @brief The largest LEDC duty at BOARD_BL_DUTY_BITS. */
#define BL_DUTY_MAX ((1u << BOARD_BL_DUTY_BITS) - 1u)

/**
 * @brief Set up the backlight's LEDC timer and channel, dark.
 *
 * LEDC low-speed mode, inverted output: duty 0 holds GPIO32 high, the
 * dimmest state, from the moment the channel exists.  Before that the pin
 * is undriven and the converter's own resistors set the brightest state
 * (from the schematic, not measured).
 *
 * @return ESP_OK, or the LEDC error.
 */
static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = (ledc_timer_bit_t)BOARD_BL_DUTY_BITS,
        .timer_num = (ledc_timer_t)BOARD_BL_LEDC_TIMER,
        .freq_hz = BOARD_BL_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    const ledc_channel_config_t ch = {
        .gpio_num = BOARD_BL_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = (ledc_channel_t)BOARD_BL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = (ledc_timer_t)BOARD_BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 1,
    };
    return ledc_channel_config(&ch);
}

esp_err_t board_init(void)
{
    if (s_board.initialised) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

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

    s_board.initialised = true;
    ESP_LOGI(TAG, "I2C%d up on SDA=%d SCL=%d, backlight on GPIO%d",
             BOARD_I2C_PORT, BOARD_I2C_PIN_SDA, BOARD_I2C_PIN_SCL,
             BOARD_BL_PIN);
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_board.bus;
}

esp_err_t board_backlight_percent(int percent)
{
    ESP_RETURN_ON_FALSE(s_board.initialised, ESP_ERR_INVALID_STATE, TAG,
                        "board_init() first");
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    const uint32_t duty = (BL_DUTY_MAX * (uint32_t)percent) / 100u;
    const ledc_channel_t ch = (ledc_channel_t)BOARD_BL_LEDC_CHANNEL;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty), TAG,
                        "backlight duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}

esp_err_t board_backlight(bool on)
{
    return board_backlight_percent(on ? 100 : 0);
}

esp_err_t board_touch_reset_sequence(void)
{
    ESP_RETURN_ON_ERROR(board_init(), TAG, "board_init");
    /* A variable, so a build with GPIO_NUM_NC compiles no negative shift. */
    const int pin = (int)BOARD_TOUCH_PIN_RST;
    if (pin < 0) {
        ESP_LOGI(TAG, "GT911 reset not wired; no reset sequence");
        return ESP_OK;
    }
    const gpio_config_t rst = {
        .pin_bit_mask = 1ULL << (unsigned)pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst), TAG, "touch reset as output");
    ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t)pin, 0), TAG,
                        "assert touch reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t)pin, 1), TAG,
                        "release touch reset");
    /* The GT911 answers on the bus about 50ms after the reset ends. */
    vTaskDelay(pdMS_TO_TICKS(60));
    ESP_LOGI(TAG, "GT911 reset on GPIO%d done", pin);
    return ESP_OK;
}
