/*
 * SPDX-License-Identifier: MIT
 */

#include "knob_task.h"

#include <stdatomic.h>
#include <stddef.h>

#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "knob.h"

static const char *TAG = "knob";

/*
 * Short, because the bus is shared with the touch controller the control
 * task reads every 5 ms: a sensor that holds the bus costs that task at most
 * this long per transaction, far under the 150 ms the safety line is watched
 * against.  The three transactions of one poll take about 0.3 ms at 400 kHz.
 */
#define KNOB_I2C_TIMEOUT_MS 5

static atomic_bool s_on;
static atomic_int  s_steps;
static i2c_master_dev_handle_t s_dev;

/*
 * One register per transaction.  From the ams AS5600 datasheet, "Automatic
 * Increment of the Address Pointer for ANGLE, RAW ANGLE and MAGNITUDE
 * Registers": "These are special registers which suppress the automatic
 * increment of the address pointer on reads".  A read that starts at STATUS
 * therefore does not walk on to RAW ANGLE and MAGNITUDE; each is addressed
 * by its own write of the register address, and read from its high byte.
 */
static bool knob_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len,
                                       KNOB_I2C_TIMEOUT_MS) == ESP_OK;
}

static void knob_task(void *arg)
{
    (void)arg;
    knob_t k;
    knob_reset(&k);
    uint32_t period_ms = KNOB_POLL_IDLE_MS;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(period_ms));
        if (!atomic_load(&s_on)) {
            knob_reset(&k);
            period_ms = KNOB_POLL_IDLE_MS;
            continue;
        }
        uint8_t status = 0;
        uint8_t raw[KNOB_WORD_LEN];
        uint8_t mag[KNOB_WORD_LEN];
        const bool read_ok = knob_read(KNOB_REG_STATUS, &status, KNOB_STATUS_LEN)
                          && knob_read(KNOB_REG_RAW_ANGLE, raw, KNOB_WORD_LEN)
                          && knob_read(KNOB_REG_MAGNITUDE, mag, KNOB_WORD_LEN);
        knob_reading_t r = { 0 };
        const bool usable = read_ok && knob_decode(status, raw, mag, &r);
        const int d = knob_feed(&k, usable, r.raw);
        if (d != 0) {
            atomic_fetch_add(&s_steps, d);
        }
        /* A sensor that answers with a flagged reading is as good as absent:
         * both back off to the idle period. */
        period_ms = usable ? KNOB_POLL_MS : KNOB_POLL_IDLE_MS;
    }
}

void knob_task_attach(void)
{
    /*
     * The bus driver's add and remove calls are not thread-safe against a
     * transaction on the same bus, and the control task reads the touch
     * controller on it every 5 ms.  The handle is allocated once, before that
     * task exists, and stays registered; the setting only gates the polling.
     * Registering sends nothing to 0x36.
     */
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = KNOB_I2C_ADDR,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    if (i2c_master_bus_add_device(board_i2c_bus(), &cfg, &s_dev) != ESP_OK) {
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
