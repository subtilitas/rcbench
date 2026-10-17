/*
 * SPDX-License-Identifier: MIT
 */

#include "knob_task.h"

#include <stdatomic.h>
#include <stddef.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "knob.h"
#include "panel_i2c_dev.h"

static const char *TAG = "knob";

/*
 * Short, because the bus is shared with the touch controller, which the touch
 * task reads every 10 ms by default (and on the touch interrupt): a sensor
 * that holds the bus costs that task at most this long per transaction, far
 * under the 150 ms the safety line is watched against.  The two
 * transactions of one poll take about 0.2 ms at 400 kHz.
 */
#define KNOB_I2C_TIMEOUT_MS 5

static atomic_bool s_on;
static atomic_int  s_steps;
static i2c_master_dev_handle_t s_dev;
/* Written by knob_task_attach() before the task exists, then the task's. */
static knob_t s_knob;

/*
 * One register per transaction, Angle[13:6] at 0x03 first and Angle[5:0] at
 * 0x04 second, as the MT6701 datasheet's figure 20 reads them (knob.h).
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
        bool answered = knob_poll_addr(&s_knob) >= 0;
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
     * The bus driver's add and remove calls are not thread-safe against a
     * transaction on the same bus, and the touch task reads the touch
     * controller on it every 10 ms by default, and on the touch interrupt.
     * The handle is allocated once, before that task exists, and stays
     * registered; the setting only gates the polling.
     *
     * Two guards against an address the board answers at, which on this
     * bus would be a CH422G command: knob_attach() refuses it for the polls
     * (and is the one the host tests hold), and panel_i2c_add_device()
     * refuses to register it.  Registering sends nothing.
     */
    if (!knob_attach(&s_knob, KNOB_I2C_ADDR)) {
        s_dev = NULL;
        ESP_LOGE(TAG, "0x%02x is the board's on this bus; knob off",
                 KNOB_I2C_ADDR);
        return;
    }
    if (panel_i2c_add_device(KNOB_I2C_ADDR, &s_dev) != ESP_OK) {
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
