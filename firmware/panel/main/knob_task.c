/*
 * SPDX-License-Identifier: MIT
 */

#include "knob_task.h"

#include <stdatomic.h>

#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "knob.h"

static const char *TAG = "knob";

/*
 * Short, because the bus is shared with the touch controller the control
 * task reads every 5 ms: a sensor that holds the bus costs that task at most
 * this long, far under the 150 ms the safety line is watched against.  One
 * burst of 18 bytes at 400 kHz is about 0.5 ms.
 */
#define KNOB_I2C_TIMEOUT_MS 5

static atomic_bool s_on;
static atomic_int  s_steps;

static void knob_task(void *arg)
{
    (void)arg;
    knob_t k;
    knob_reset(&k);
    i2c_master_dev_handle_t dev = NULL;
    uint32_t period_ms = KNOB_POLL_IDLE_MS;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(period_ms));
        if (!atomic_load(&s_on)) {
            knob_reset(&k);
            if (dev != NULL) {
                i2c_master_bus_rm_device(dev);
                dev = NULL;
            }
            period_ms = KNOB_POLL_IDLE_MS;
            continue;
        }
        if (dev == NULL) {
            const i2c_device_config_t cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = KNOB_I2C_ADDR,
                .scl_speed_hz = BOARD_I2C_FREQ_HZ,
            };
            if (i2c_master_bus_add_device(board_i2c_bus(), &cfg, &dev) != ESP_OK) {
                dev = NULL;
                continue;
            }
        }
        const uint8_t reg = KNOB_REG_STATUS;
        uint8_t regs[KNOB_BURST_LEN];
        const bool read_ok =
            i2c_master_transmit_receive(dev, &reg, 1, regs, sizeof regs,
                                        KNOB_I2C_TIMEOUT_MS) == ESP_OK;
        knob_reading_t r = { 0 };
        const bool usable = read_ok && knob_decode(regs, &r);
        const int d = knob_feed(&k, usable, r.raw);
        if (d != 0) {
            atomic_fetch_add(&s_steps, d);
        }
        period_ms = read_ok ? KNOB_POLL_MS : KNOB_POLL_IDLE_MS;
    }
}

void knob_task_start(void)
{
    /*
     * Core 0 beside the renderer, below it in nothing it needs: 10 ms polls
     * of 0.5 ms each.  Priority 2, under the card task's 3, and not on
     * core 1 where the control task owns the deadlines.  Stack 3072 bytes:
     * the task's deepest chain is the I2C driver's transaction.
     */
    if (xTaskCreatePinnedToCore(knob_task, "knob", 3072, NULL, 2, NULL, 0)
        != pdPASS) {
        ESP_LOGE(TAG, "task not created");
    }
}

void knob_task_set_enabled(bool on)
{
    atomic_store(&s_on, on);
}

int knob_task_take(void)
{
    return atomic_exchange(&s_steps, 0);
}
