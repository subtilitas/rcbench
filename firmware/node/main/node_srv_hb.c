/**
 * @file node_srv_hb.c
 * @brief The servo node's two safety pins: HEARTBEAT on IO21 to the
 *        Core2350B's GPIO3 and the master switch's monostable, and RUN's
 *        MOSFET on IO14.
 *
 * IO21 carries edges, one every HEARTBEAT_PERIOD_MS (20ms) while the policy
 * says the bench may run (heartbeat_gen_step() in node_srv_link.c); low
 * otherwise.  IO14 drives the gate of an N-channel MOSFET whose drain pulls
 * the Core2350B's RUN low: IO14 high for NODE_SRV_RUN_PULSE_MS (10ms)
 * restarts the Core2350B, which starts with its arm latch set.  Both pins
 * are driven low from node_srv_hb_init() on.
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"
#include "node_srv.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

/** @brief The one-shot timer that ends a RUN pulse. */
static esp_timer_handle_t s_run_timer;

/**
 * @brief End of a RUN pulse: IO14 low, the MOSFET off.
 * @param arg Not used.
 */
static void run_end(void *arg)
{
    (void)arg;
    (void)gpio_set_level(NODE_PIN_SRV_RUN, 0);
}

void node_srv_hb_init(void)
{
    (void)gpio_set_level(NODE_PIN_SRV_HB, 0);
    (void)gpio_set_level(NODE_PIN_SRV_RUN, 0);
    const gpio_config_t out = {
        .pin_bit_mask = (1ull << NODE_PIN_SRV_HB) | (1ull << NODE_PIN_SRV_RUN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    (void)gpio_config(&out);
    (void)gpio_set_level(NODE_PIN_SRV_HB, 0);
    (void)gpio_set_level(NODE_PIN_SRV_RUN, 0);
    const esp_timer_create_args_t t = {
        .callback = run_end,
        .name = "srv_run",
    };
    if (esp_timer_create(&t, &s_run_timer) != ESP_OK) {
        ESP_LOGE(NODE_TAG, "srv: no RUN timer: the Core2350B is not restarted");
        s_run_timer = NULL;
    }
}

void node_srv_hb_set(bool level)
{
    (void)gpio_set_level(NODE_PIN_SRV_HB, level ? 1u : 0u);
}

void node_srv_hb_run_pulse(void)
{
    if (s_run_timer == NULL || esp_timer_is_active(s_run_timer)) {
        return;
    }
    (void)gpio_set_level(NODE_PIN_SRV_RUN, 1);
    if (esp_timer_start_once(s_run_timer,
                             (uint64_t)NODE_SRV_RUN_PULSE_MS * 1000u) != ESP_OK) {
        (void)gpio_set_level(NODE_PIN_SRV_RUN, 0);
    }
}
