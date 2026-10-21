/*
 * SPDX-License-Identifier: MIT
 */

#include "knob_task.h"

#include <stdatomic.h>
#include <stddef.h>

#include "board_pins.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "knob.h"

static const char *TAG = "knob";

/*
 * The bus is the knob's alone, so a transaction waits for nothing else.
 * The three transactions of one poll take about 0.3 ms at 400 kHz.  A
 * failed one costs more: the driver resets the controller and clears the
 * bus, which waits up to 50 ms (I2C_CLR_BUS_TIMEOUT_MS) while SDA is held
 * low.
 */
#define KNOB_I2C_TIMEOUT_MS 5

static atomic_bool s_on;
static atomic_int  s_steps;
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
/* Written by knob_task_attach() before the task exists, then the task's. */
static knob_t s_knob;

/*
 * One register per transaction, Angle[13:6] at 0x03 first and Angle[5:0] at
 * 0x04 second, as the MT6701 datasheet's figure 20 reads them, then 0x03
 * again to catch a high byte that moved in between (knob.h).
 */
static bool knob_read(uint8_t reg, uint8_t *byte)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, byte, 1,
                                       KNOB_I2C_TIMEOUT_MS) == ESP_OK;
}

static void knob_task(void *arg)
{
    (void)arg;
    knob_reset(&s_knob);
    uint32_t period_ms = KNOB_POLL_IDLE_MS;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(period_ms));
        if (!atomic_load(&s_on)) {
            knob_reset(&s_knob);
            period_ms = KNOB_POLL_IDLE_MS;
            continue;
        }
        uint8_t bytes[KNOB_POLL_READS] = { 0 };
        const int addr = knob_poll_addr(&s_knob);
        bool answered = addr >= 0;
        /* No reference: an unlogged probe first, so a missing sensor costs
         * one address byte a poll and no driver error lines. */
        if (answered && knob_poll_probe_first(&s_knob)) {
            answered = i2c_master_probe(s_bus, (uint16_t)addr,
                                        KNOB_I2C_TIMEOUT_MS) == ESP_OK;
        }
        for (int i = 0; answered && i < KNOB_POLL_READS; ++i) {
            answered = knob_read(knob_poll_regs[i], &bytes[i]);
        }
        const int d = knob_poll_done(&s_knob, answered, bytes);
        if (d != 0) {
            atomic_fetch_add(&s_steps, d);
        }
        /* A sensor that does not answer backs off to the idle period. */
        period_ms = answered ? KNOB_POLL_MS : KNOB_POLL_IDLE_MS;
    }
}

void knob_task_attach(void)
{
    /*
     * The knob's own controller on BOARD_KNOB_PIN_SDA and _SCL, with the
     * internal pull-ups.  The bus and the device handle are made once and
     * stay; the setting only gates the polling.  knob_attach() refuses an
     * address of panel_i2c.h's table, and the host tests hold it.
     * Registering sends nothing.
     */
    if (!knob_attach(&s_knob, KNOB_I2C_ADDR)) {
        ESP_LOGE(TAG, "0x%02x refused; knob off", KNOB_I2C_ADDR);
        return;
    }
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_KNOB_I2C_PORT,
        .sda_io_num = BOARD_KNOB_PIN_SDA,
        .scl_io_num = BOARD_KNOB_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        s_bus = NULL;
        ESP_LOGE(TAG, "bus on GPIO%d/%d not set up; knob off",
                 (int)BOARD_KNOB_PIN_SDA, (int)BOARD_KNOB_PIN_SCL);
        return;
    }
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = KNOB_I2C_ADDR,
        .scl_speed_hz = BOARD_KNOB_I2C_FREQ_HZ,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) {
        s_dev = NULL;
        ESP_LOGE(TAG, "sensor not registered on the bus");
    }
}

void knob_task_start(void)
{
    if (s_dev == NULL) {
        return;     /* nothing to poll; the knob stays inert */
    }
    /*
     * Core 0 beside the renderer, below it in nothing it needs: 10 ms polls
     * of about 0.2 ms each.  Priority 2, under the card task's 3, and not on
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
