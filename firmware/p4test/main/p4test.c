/*
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"

static const char *TAG = "p4test";

/* What the chip says it is, so a log names the hardware it came from. */
static void report_chip(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash = 0;
    (void)esp_flash_get_size(NULL, &flash);
    ESP_LOGI(TAG, "board %s", BOARD_NAME);
    ESP_LOGI(TAG, "ESP32-P4 revision v%u.%u, %u cores, ESP-IDF %s",
             (unsigned)(chip.revision / 100), (unsigned)(chip.revision % 100),
             (unsigned)chip.cores, esp_get_idf_version());
    ESP_LOGI(TAG, "flash %" PRIu32 " MB, PSRAM %u MB",
             flash / (1024u * 1024u),
             (unsigned)(esp_psram_get_size() / (1024u * 1024u)));
}

/*
 * Every 7-bit address from 0x08 to 0x77 on the display's I2C bus.  The
 * devices found say which display electronics are fitted before anything is
 * written to them.
 */
static i2c_master_bus_handle_t scan_i2c(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    if (i2c_new_master_bus(&cfg, &bus) != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus on SDA %d SCL %d did not start",
                 BOARD_I2C_SDA, BOARD_I2C_SCL);
        return NULL;
    }
    unsigned found = 0;
    for (uint16_t a = 0x08; a <= 0x77; ++a) {
        if (i2c_master_probe(bus, a, 20) == ESP_OK) {
            ESP_LOGI(TAG, "I2C device at 0x%02X", a);
            ++found;
        }
    }
    ESP_LOGI(TAG, "I2C scan on SDA %d SCL %d: %u device(s)",
             BOARD_I2C_SDA, BOARD_I2C_SCL, found);
    return bus;
}

void app_main(void)
{
    report_chip();
    i2c_master_bus_handle_t bus = scan_i2c();
    (void)bus;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
