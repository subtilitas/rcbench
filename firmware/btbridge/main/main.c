/**
 * @file main.c
 * @brief The Bluetooth bridge: an ESP32 that answers the panel's HC-05 AT
 *        commands (protocols/hc05emu) on UART2 and carries the BWT901CL's
 *        SPP (serial port profile) link in data mode.
 *
 * One task runs everything but the stack's callbacks: every BRIDGE_POLL_MS
 * it takes the stack's events, reads KEY, reads the UART (AT mode: to the
 * emulator; data mode with the link open: over the link), runs every
 * action the emulator hands out, and sets STATE and the LED.  The pins are
 * the CONFIG_BTBRIDGE_* options (main/Kconfig.projbuild).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bridge_bt.h"
#include "bridge_store.h"
#include "hc05emu.h"

/** @brief The console lines' tag. */
static const char *TAG = "btbridge";

/** @brief The panel's UART: UART0 is the console's. */
#define BRIDGE_UART    UART_NUM_2
/** @brief The UART's receive ring, bytes. */
#define BRIDGE_RX_RING 1024
/** @brief The UART's transmit ring, bytes. */
#define BRIDGE_TX_RING 2048
/** @brief Bytes read from the UART at a time. */
#define BRIDGE_READ_MAX 128
/** @brief The task's poll, ms: the UART read's wait. */
#define BRIDGE_POLL_MS 2
/** @brief Stack events the queue holds. */
#define BRIDGE_EV_Q    32
/** @brief The longest wait for the UART to send what it holds before a
 *         baud change, ms: 64 bytes at 4800 baud take 134ms. */
#define BRIDGE_TX_DRAIN_MS 150

_Static_assert(BRIDGE_POLL_MS >= 1, "the poll blocks: the task never spins");

/** @brief The emulator; the bridge task's own. */
static hc05emu_t s_emu;
/** @brief The stack's events to the task. */
static QueueHandle_t s_ev;

/**
 * @brief The free-running millisecond clock.
 * @return ms since the start, wrapping at 2^32.
 */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief KEY's level.
 * @return true for high: AT mode.
 */
static bool key_read(void)
{
    return gpio_get_level(CONFIG_BTBRIDGE_KEY_GPIO) != 0;
}

/**
 * @brief Light the LED or put it out, where there is one.
 * @param on Lit.
 */
static void led_set(bool on)
{
#if CONFIG_BTBRIDGE_LED_GPIO >= 0
#if CONFIG_BTBRIDGE_LED_ACTIVE_LOW
    on = !on;
#endif
    (void)gpio_set_level(CONFIG_BTBRIDGE_LED_GPIO, on ? 1 : 0);
#else
    (void)on;
#endif
}

/**
 * @brief STATE: high with the link open, where it is wired.
 * @param up The link is open.
 */
static void state_set(bool up)
{
#if CONFIG_BTBRIDGE_STATE_GPIO >= 0
    (void)gpio_set_level(CONFIG_BTBRIDGE_STATE_GPIO, up ? 1 : 0);
#else
    (void)up;
#endif
}

/**
 * @brief Set the UART: the baud, the stop bits and the parity, after what
 *        it holds has gone out at the old ones.
 * @param a The BAUD action.
 */
static void uart_apply(const hc05emu_act_t *a)
{
    static const uart_parity_t parity[3] = { UART_PARITY_DISABLE,
                                             UART_PARITY_ODD,
                                             UART_PARITY_EVEN };
    (void)uart_wait_tx_done(BRIDGE_UART, pdMS_TO_TICKS(BRIDGE_TX_DRAIN_MS));
    if (uart_set_baudrate(BRIDGE_UART, a->baud) != ESP_OK) {
        ESP_LOGW(TAG, "baud %lu refused", (unsigned long)a->baud);
    }
    (void)uart_set_stop_bits(BRIDGE_UART,
                             a->stop != 0u ? UART_STOP_BITS_2
                                           : UART_STOP_BITS_1);
    (void)uart_set_parity(BRIDGE_UART, parity[a->parity <= 2u ? a->parity
                                                               : 0u]);
    ESP_LOGI(TAG, "UART %lu baud, %u stop bit(s), parity %u",
             (unsigned long)a->baud, a->stop + 1u, (unsigned)a->parity);
}

/**
 * @brief One action of the emulator: the UART's here, the radio's in
 *        bridge_bt.c, SAVE to NVS.
 * @param a      The action.
 * @param now    The clock.
 */
static void perform(const hc05emu_act_t *a, uint32_t now)
{
    switch (a->kind) {
    case HC05EMU_ACT_SEND:
        (void)uart_write_bytes(BRIDGE_UART, a->data, a->len);
        break;
    case HC05EMU_ACT_BAUD:
        uart_apply(a);
        break;
    case HC05EMU_ACT_SAVE:
        (void)bridge_store_save(&s_emu.cfg);
        break;
    case HC05EMU_ACT_NONE:
        break;
    default:
        bridge_bt_perform(&s_emu, a, now);
        break;
    }
}

/**
 * @brief The bridge task: events, KEY, the UART, the actions, STATE and the
 *        LED, every BRIDGE_POLL_MS.
 * @param arg Unused.
 */
static void bridge_task(void *arg)
{
    (void)arg;
    hc05emu_mode_t mode_was = (hc05emu_mode_t)-1;
    hc05emu_link_t link_was = (hc05emu_link_t)-1;
    for (;;) {
        uint8_t rx[BRIDGE_READ_MAX];
        const int n = uart_read_bytes(BRIDGE_UART, rx, sizeof(rx),
                                      pdMS_TO_TICKS(BRIDGE_POLL_MS));
        const uint32_t now = now_ms();
        bridge_ev_t ev;
        while (xQueueReceive(s_ev, &ev, 0) == pdTRUE) {
            bridge_bt_event(&s_emu, &ev, now);
        }
        hc05emu_key(&s_emu, key_read(), now);
        if (n > 0 && hc05emu_at_mode(&s_emu)) {
            hc05emu_feed(&s_emu, rx, (size_t)n, now);
        } else if (n > 0 && hc05emu_forwarding(&s_emu)) {
            bridge_bt_write(rx, (size_t)n);
        }
        hc05emu_act_t act;
        while (hc05emu_step(&s_emu, now, &act)) {
            perform(&act, now);
        }
        bridge_bt_forward(hc05emu_forwarding(&s_emu));
        state_set(hc05emu_connected(&s_emu));
        led_set(hc05emu_led(&s_emu, now));
        if (s_emu.mode != mode_was || s_emu.link != link_was) {
            ESP_LOGI(TAG, "mode %d, link %d", (int)s_emu.mode,
                     (int)s_emu.link);
            mode_was = s_emu.mode;
            link_was = s_emu.link;
        }
    }
}

/**
 * @brief Set up KEY as an input, pulled down where the pin can be, and
 *        STATE and the LED as outputs.
 * @return true when KEY is set up.
 */
static bool pins_start(void)
{
    const gpio_config_t key = {
        .pin_bit_mask = 1ULL << CONFIG_BTBRIDGE_KEY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_BTBRIDGE_KEY_GPIO)
                            ? GPIO_PULLDOWN_ENABLE
                            : GPIO_PULLDOWN_DISABLE,
    };
    if (gpio_config(&key) != ESP_OK) {
        return false;
    }
    uint64_t outs = 0;
#if CONFIG_BTBRIDGE_STATE_GPIO >= 0
    outs |= 1ULL << CONFIG_BTBRIDGE_STATE_GPIO;
#endif
#if CONFIG_BTBRIDGE_LED_GPIO >= 0
    outs |= 1ULL << CONFIG_BTBRIDGE_LED_GPIO;
#endif
    if (outs != 0u) {
        const gpio_config_t out = { .pin_bit_mask = outs,
                                    .mode = GPIO_MODE_OUTPUT };
        (void)gpio_config(&out);
    }
    state_set(false);
    led_set(false);
    return true;
}

/**
 * @brief Start UART2 at the default data baud; the emulator's first action
 *        sets the baud it starts at.
 * @return true when it is set up.
 */
static bool uart_start(void)
{
    const uart_config_t uc = {
        .baud_rate = (int)HC05EMU_BAUD_DEFAULT,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    return uart_driver_install(BRIDGE_UART, BRIDGE_RX_RING, BRIDGE_TX_RING, 0,
                               NULL, 0) == ESP_OK
           && uart_param_config(BRIDGE_UART, &uc) == ESP_OK
           && uart_set_pin(BRIDGE_UART, CONFIG_BTBRIDGE_UART_TX_GPIO,
                           CONFIG_BTBRIDGE_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE) == ESP_OK;
}

/**
 * @brief The start: KEY, STATE, the LED and UART2, the setting from NVS,
 *        the emulator with KEY's level now, Bluetooth, then the task.
 */
void app_main(void)
{
    if (!pins_start() || !uart_start()) {
        ESP_LOGE(TAG, "KEY on GPIO%d or UART2 on GPIO%d/%d not set up",
                 CONFIG_BTBRIDGE_KEY_GPIO, CONFIG_BTBRIDGE_UART_TX_GPIO,
                 CONFIG_BTBRIDGE_UART_RX_GPIO);
        return;
    }
    hc05emu_cfg_t cfg;
    const bool stored = bridge_store_load(&cfg);
    /* hc05emu_init() cuts it to HC05EMU_VERSION_MAX - 1 bytes. */
    char version[48];
    (void)snprintf(version, sizeof(version), "btbridge-%s",
                   esp_app_get_description()->version);
    s_ev = xQueueCreate(BRIDGE_EV_Q, sizeof(bridge_ev_t));
    if (s_ev == NULL) {
        ESP_LOGE(TAG, "event queue not created");
        return;
    }
    hc05emu_init(&s_emu, stored ? &cfg : NULL, version, key_read(), now_ms());
    ESP_LOGI(TAG, "%s, %s setting, KEY %s", version,
             stored && hc05emu_cfg_valid(&cfg) ? "stored" : "fresh",
             key_read() ? "high" : "low");
    /* Without Bluetooth the AT commands still answer; every radio action
     * then fails as the stack refuses it. */
    (void)bridge_bt_start(s_ev, BRIDGE_UART);
    /* Core 1: Bluedroid's tasks run on core 0.  Priority 5, above the idle
     * and below the stack's.  Stack 4096 bytes: an action and the UART's
     * 128-byte read on the deepest chain, the UART driver's write. */
    if (xTaskCreatePinnedToCore(bridge_task, "bridge", 4096, NULL, 5, NULL, 1)
        != pdPASS) {
        ESP_LOGE(TAG, "task not created");
    }
}
