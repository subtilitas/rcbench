/**
 * @file panel_scan.c
 * @brief The QR scanner's task and UART1 (panel_scan.h).
 *
 * The task owns the port (scan_port.h) and UART1.  The frame task reaches
 * the port's line queue and the published view under s_mux; SETUP's values
 * come in through two atomics.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_scan.h"

#include <stdatomic.h>
#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "panel.h"
#include "scanner_pane.h"

/** @brief The console lines' tag. */
static const char *TAG = "scan";

/** @brief The UART driver's receive buffer, bytes. */
#define SCAN_UART_RX 256
/** @brief The UART driver's event queue: framing and parity errors. */
#define SCAN_UART_EVENTS 8

_Static_assert(PANEL_SCAN_READ_MAX <= SCAN_UART_RX,
               "a pass reads no more than the buffer holds");
_Static_assert(pdMS_TO_TICKS(PANEL_SCAN_PERIOD_MS) >= 1,
               "the pass blocks for a tick at least: the task never spins");

/** @brief The port, the task's own; its queue under s_mux. */
static scan_port_t s_port;
/** @brief The view, published under s_mux. */
static panel_scan_view_t s_view;
/** @brief The zones found, published under s_mux. */
static uint8_t s_found[GM861_ZONES];
/** @brief Zone 0x00B0 found, published under s_mux. */
static uint8_t s_found_cut;
/** @brief The lock of the queue, the view and the zones found. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
/** @brief SETUP's Scanner row, for the task. */
static atomic_bool s_want_on;
/** @brief SETUP asked for gm861_recheck(), for the task. */
static atomic_bool s_want_recheck;
/** @brief SETUP asked for gm861_factory_reset(), for the task. */
static atomic_bool s_want_reset;
/** @brief SETUP's READ MODE, for the task (gm861_mode_t). */
static atomic_int s_want_mode;
/** @brief The UART driver's event queue while UART1 is installed. */
static QueueHandle_t s_events;
/** @brief The task was started. */
static bool s_started;

/**
 * @brief The read mode SETUP holds.
 * @return GM861_MODE_SENSING or GM861_MODE_CONTINUOUS.
 */
static gm861_mode_t mode_of_settings(void)
{
    return settings_get_int(SET_SCANNER_MODE) == 1 ? GM861_MODE_CONTINUOUS
                                                   : GM861_MODE_SENSING;
}

/**
 * @brief Install UART1 on IO4 and IO5 at @p baud, RX pulled down.
 * @param baud The rate.
 * @return true when the driver runs.
 */
static bool uart_open(uint32_t baud)
{
    const uart_config_t cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(BOARD_SCAN_UART, SCAN_UART_RX, 0,
                            SCAN_UART_EVENTS, &s_events, 0) != ESP_OK) {
        s_events = NULL;
        return false;
    }
    if (uart_param_config(BOARD_SCAN_UART, &cfg) != ESP_OK
        || uart_set_pin(BOARD_SCAN_UART, BOARD_SCAN_PIN_TX, BOARD_SCAN_PIN_RX,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        (void)uart_driver_delete(BOARD_SCAN_UART);
        s_events = NULL;
        return false;
    }
    (void)gpio_pullup_dis(BOARD_SCAN_PIN_RX);
    (void)gpio_pulldown_en(BOARD_SCAN_PIN_RX);
    return true;
}

/**
 * @brief Remove UART1 and leave IO4 and IO5 inputs without pulls.
 */
static void uart_close(void)
{
    if (s_events != NULL) {
        (void)uart_driver_delete(BOARD_SCAN_UART);
        s_events = NULL;
    }
    const gpio_config_t in = {
        .pin_bit_mask = (1ULL << BOARD_SCAN_PIN_TX) | (1ULL << BOARD_SCAN_PIN_RX),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&in);
}

/**
 * @brief SETUP's values to the port and the UART: on, off, a new mode.
 * @param on   The Scanner row.
 * @param mode The READ MODE.
 */
static void apply(bool on, gm861_mode_t mode)
{
    static bool s_on;
    static gm861_mode_t s_mode;
    if (on && !s_on) {
        portENTER_CRITICAL(&s_mux);
        scan_port_start(&s_port, mode);
        portEXIT_CRITICAL(&s_mux);
        ESP_LOGI(TAG, "on: UART%d TX IO%d RX IO%d", BOARD_SCAN_UART,
                 (int)BOARD_SCAN_PIN_TX, (int)BOARD_SCAN_PIN_RX);
    } else if (!on && s_on) {
        portENTER_CRITICAL(&s_mux);
        scan_port_stop(&s_port);
        portEXIT_CRITICAL(&s_mux);
        uart_close();
        /* A recheck or reset asked just before OFF is not kept for the
         * next ON. */
        atomic_store(&s_want_recheck, false);
        atomic_store(&s_want_reset, false);
        ESP_LOGI(TAG, "off");
    } else if (on && mode != s_mode) {
        portENTER_CRITICAL(&s_mux);
        scan_port_set_mode(&s_port, mode);
        portEXIT_CRITICAL(&s_mux);
    }
    s_on = on;
    s_mode = mode;
}

/**
 * @brief The UART's bytes and errors to the port.
 *
 * The bytes are read before the driver's events are drained.  The driver
 * posts a framing or parity error, or a FIFO overflow, when it happens, and
 * moves the bytes later, so a bad byte or a byte after a loss in this read
 * has its event drained here; one still in the FIFO comes within the burst
 * scan_port_flush() drops.  On any of them the read is dropped whole.
 *
 * @param now The clock.
 */
static void receive(uint32_t now)
{
    uint8_t buf[PANEL_SCAN_READ_MAX];
    const int n = uart_read_bytes(BOARD_SCAN_UART, buf, sizeof(buf), 0);
    bool lost = false;
    bool error = false;
    uart_event_t ev;
    while (xQueueReceive(s_events, &ev, 0) == pdTRUE) {
        if (ev.type == UART_FRAME_ERR || ev.type == UART_PARITY_ERR) {
            error = true;
        } else if (ev.type == UART_FIFO_OVF || ev.type == UART_BUFFER_FULL) {
            lost = true;
        }
    }
    if (lost) {
        (void)uart_flush_input(BOARD_SCAN_UART);
        xQueueReset(s_events);
    }
    portENTER_CRITICAL(&s_mux);
    if (error) {
        scan_port_error(&s_port, now);
    } else if (lost) {
        scan_port_flush(&s_port, now);
    }
    if (n > 0) {
        /* After a loss the port drops these too: it is in its discard. */
        scan_port_rx(&s_port, buf, (size_t)n, now);
    }
    portEXIT_CRITICAL(&s_mux);
}

/**
 * @brief One step of the sequence: the re-open at the rate it asks, then
 *        its request.
 * @param now The clock.
 */
static void step(uint32_t now)
{
    const bool high = gpio_get_level(BOARD_SCAN_PIN_RX) != 0;
    uint8_t tx[GM861_REQ_LEN];
    uint32_t reopen = 0u;
    portENTER_CRITICAL(&s_mux);
    const size_t n = scan_port_step(&s_port, now, high, tx, sizeof(tx),
                                    &reopen);
    portEXIT_CRITICAL(&s_mux);
    if (reopen != 0u) {
        if (s_events == NULL) {
            if (!uart_open(reopen)) {
                ESP_LOGE(TAG, "UART%d not installed", BOARD_SCAN_UART);
            }
        } else {
            (void)uart_set_baudrate(BOARD_SCAN_UART, reopen);
            (void)uart_flush_input(BOARD_SCAN_UART);
        }
    }
    if (n != 0u && s_events != NULL) {
        (void)uart_write_bytes(BOARD_SCAN_UART, tx, n);
    }
}

/**
 * @brief The port's state into the view and the zones found.
 */
static void publish(void)
{
    portENTER_CRITICAL(&s_mux);
    s_view.on = s_port.on;
    s_view.state = gm861_state(&s_port.g);
    s_view.baud = s_port.baud;
    s_view.bad_zone = gm861_bad_zone(&s_port.g);
    s_view.counts = scan_line_counts(&s_port.line);
    s_view.drops = s_port.drops;
    s_view.errors = s_port.errors;
    s_view.flushes = s_port.flushes;
    ++s_view.rev;
    memcpy(s_found, gm861_found(&s_port.g, &s_found_cut), GM861_ZONES);
    portEXIT_CRITICAL(&s_mux);
}

/**
 * @brief SETUP's commands to the sequencer: a recheck, a factory reset.
 */
static void commands(void)
{
    const bool recheck = atomic_exchange(&s_want_recheck, false);
    const bool reset = atomic_exchange(&s_want_reset, false);
    if (!recheck && !reset) {
        return;
    }
    portENTER_CRITICAL(&s_mux);
    if (reset) {
        gm861_factory_reset(&s_port.g);
    } else {
        gm861_recheck(&s_port.g);
    }
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "%s", reset ? "factory reset asked" : "recheck asked");
}

/**
 * @brief The task: SETUP's values, the bytes, the step and the view, every
 *        PANEL_SCAN_PERIOD_MS.
 * @param arg Unused.
 */
static void scan_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(PANEL_SCAN_PERIOD_MS));
        const uint32_t now = now_ms();
        apply(atomic_load(&s_want_on),
              (gm861_mode_t)atomic_load(&s_want_mode));
        if (s_port.on) {
            commands();
            if (s_events != NULL) {
                receive(now);
            }
            step(now);
        }
        publish();
    }
}

void panel_scan_start(void)
{
    if (s_started) {
        return;
    }
    s_started = true;
    scan_port_init(&s_port);
    atomic_store(&s_want_on, settings_get_bool(SET_SCANNER_ENABLE));
    atomic_store(&s_want_mode, (int)mode_of_settings());
    uart_close();
    /*
     * Core 0 beside the renderer, at the knob's priority 2: passes of 20ms,
     * nothing with a deadline.  Stack 3072 bytes: the port is static; the
     * deepest chain is a UART driver call, tools/stack_check.py holds it.
     */
    if (xTaskCreatePinnedToCore(scan_task, "scan", 3072, NULL, 2, NULL, 0)
        != pdPASS) {
        ESP_LOGE(TAG, "task not created; scanner off");
    }
}

void panel_scan_settings_changed(setting_id_t id)
{
    if (id == SET_SCANNER_ENABLE) {
        atomic_store(&s_want_on, settings_get_bool(SET_SCANNER_ENABLE));
    } else if (id == SET_SCANNER_MODE) {
        atomic_store(&s_want_mode, (int)mode_of_settings());
    }
}

bool panel_scan_take_line(char *line, size_t n, bool *too_long)
{
    portENTER_CRITICAL(&s_mux);
    const bool got = scan_port_take_line(&s_port, line, n, too_long);
    portEXIT_CRITICAL(&s_mux);
    return got;
}

void panel_scan_frame(uint32_t now)
{
    panel_scan_view_t pv;
    panel_scan_view(&pv);
    const scanner_view_t v = {
        .on = pv.on,
        .state = (uint8_t)pv.state,
        .baud = pv.baud,
        .bad_zone = pv.bad_zone,
        .lines = pv.counts.lines,
        .noise = pv.counts.noise,
        .partial = pv.counts.partial,
        .longs = pv.counts.overlong,
        .bad_crc = pv.counts.bad_crc,
    };
    scanner_pane_set_view(&v, now);
    scanner_cmd_t cmd;
    while (scanner_pane_take(&cmd)) {
        if (cmd == SCANNER_CMD_RESET) {
            atomic_store(&s_want_reset, true);
        } else if (cmd == SCANNER_CMD_RECHECK) {
            atomic_store(&s_want_recheck, true);
        }
    }
}

bool panel_scan_has_line(void)
{
    portENTER_CRITICAL(&s_mux);
    const bool has = scan_port_has_line(&s_port);
    portEXIT_CRITICAL(&s_mux);
    return has;
}

void panel_scan_view(panel_scan_view_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_view;
    portEXIT_CRITICAL(&s_mux);
}

void panel_scan_found(uint8_t out[GM861_ZONES], uint8_t *cut)
{
    portENTER_CRITICAL(&s_mux);
    memcpy(out, s_found, GM861_ZONES);
    *cut = s_found_cut;
    portEXIT_CRITICAL(&s_mux);
}
