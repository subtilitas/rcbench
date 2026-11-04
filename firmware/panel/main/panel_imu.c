/**
 * @file panel_imu.c
 * @brief The Bluetooth IMU on the panel (panel_imu.h): the UART driver on
 *        PANEL_IMU_UART_NUM, the KEY line, the task that runs imu_ctl.h,
 *        and the frame task's glue.
 *
 * The task polls the UART every IMU_POLL_MS: the bytes go to the controller,
 * then the queue's values and commands, then every action the controller
 * hands out (KEY, a baud, bytes to send), then the view is published.  The
 * UART's receive ring holds IMU_RX_RING bytes: 116ms of the sensor's 2200
 * bytes/s at 50Hz.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_imu.h"

#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "imu_ctl.h"
#include "imu_screen.h"
#include "panel.h"
#include "sensors_rows.h"
#include "servo_angle.h"
#include "servo_screen.h"
#include "ui_screen.h"

/** @brief The console lines' tag. */
static const char *TAG = "imu";

/** @brief The UART's receive ring, bytes. */
#define IMU_RX_RING   256
/** @brief The task's poll, ms: the controller is stepped at least this often. */
#define IMU_POLL_MS   5
/** @brief Values and commands the queue holds. */
#define IMU_Q_LEN     8
/** @brief Bytes read from the UART at a time. */
#define IMU_READ_MAX  64
/** @brief Actions taken in one poll at most. */
#define IMU_ACTS_MAX  16

_Static_assert(pdMS_TO_TICKS(IMU_POLL_MS) >= 1,
               "the poll blocks for a tick at least: the task never spins");

/** @brief What the frame task posts: SETUP's values or a screen command. */
typedef struct {
    bool      is_cfg;   /**< cfg holds SETUP's values, else cmd a command */
    imu_cfg_t cfg;      /**< SETUP's values */
    imu_cmd_t cmd;      /**< the screen's command */
} imu_msg_t;

/** @brief What the task publishes: the view and the binds counted. */
typedef struct {
    imu_view_t v;         /**< the view */
    uint32_t   bind_seq;  /**< binds that ended DONE (imu_ctl_t) */
} imu_pub_t;

/** @brief The frame task's values and commands to the task; NULL off. */
static QueueHandle_t s_q;
/** @brief Guards s_pub. */
static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
/** @brief Written by the task under s_mux, read by the frame task. */
static imu_pub_t     s_pub;
/** @brief The task's own. */
static imu_ctl_t     s_ctl;
/** @brief The task's copy before it is published: too big for its stack. */
static imu_pub_t     s_next;
/** @brief The frame task's copy of s_pub. */
static imu_pub_t     s_seen;
/** @brief The binds the frame task kept in SETUP. */
static uint32_t      s_kept_seq;

/**
 * @brief SETUP's values as the controller takes them.
 * @param out Receives them.
 */
static void cfg_from_settings(imu_cfg_t *out)
{
    memset(out, 0, sizeof(*out));
    out->en = settings_get_bool(SET_IMU_EN);
    strncpy(out->addr, settings_text(SET_TEXT_IMU_ADDR), sizeof(out->addr) - 1u);
    strncpy(out->name, settings_text(SET_TEXT_IMU_NAME), sizeof(out->name) - 1u);
    out->rate_code = imu_ctl_rate_code(settings_get_int(SET_IMU_RATE));
    out->six = settings_get_int(SET_IMU_ALGO) == 0;
}

/** @brief SETUP's values to the task; dropped while its queue is full. */
static void post_cfg(void)
{
    if (s_q == NULL) {
        return;
    }
    imu_msg_t m = { .is_cfg = true };
    cfg_from_settings(&m.cfg);
    if (xQueueSend(s_q, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full: settings not handed over");
    }
}

/**
 * @brief One action of the controller on the pins and the UART.
 * @param a The action.
 */
static void perform(const hc05_act_t *a)
{
    switch (a->kind) {
    case HC05_ACT_KEY:
        gpio_set_level(PANEL_IMU_PIN_KEY, a->key ? 1 : 0);
        break;
    case HC05_ACT_BAUD:
        if (uart_set_baudrate(PANEL_IMU_UART_NUM, a->baud) != ESP_OK) {
            ESP_LOGW(TAG, "baud %lu refused", (unsigned long)a->baud);
        }
        break;
    case HC05_ACT_SEND:
        (void)uart_write_bytes(PANEL_IMU_UART_NUM, a->data, a->len);
        break;
    case HC05_ACT_NONE:
    default:
        break;
    }
}

/**
 * @brief The task: bytes, messages, actions, the view, every IMU_POLL_MS.
 * @param arg Unused.
 */
static void imu_task(void *arg)
{
    (void)arg;
    imu_link_t was = IMU_LINK_OFF;
    int state_was = -1;
    for (;;) {
        uint8_t rx[IMU_READ_MAX];
        const int n = uart_read_bytes(PANEL_IMU_UART_NUM, rx, sizeof(rx),
                                      pdMS_TO_TICKS(IMU_POLL_MS));
        const uint32_t now = now_ms();
        if (n > 0) {
            imu_ctl_feed(&s_ctl, rx, (size_t)n, now);
        }
        imu_msg_t m;
        while (xQueueReceive(s_q, &m, 0) == pdTRUE) {
            if (m.is_cfg) {
                imu_ctl_config(&s_ctl, &m.cfg, now);
            } else {
                (void)imu_ctl_command(&s_ctl, &m.cmd, now);
            }
        }
        hc05_act_t act;
        for (int i = 0; i < IMU_ACTS_MAX && imu_ctl_step(&s_ctl, now, &act);
             ++i) {
            perform(&act);
        }
        imu_ctl_view(&s_ctl, &s_next.v);
        s_next.bind_seq = s_ctl.bind_seq;
        if (s_next.v.link != was) {
            ESP_LOGI(TAG, "link %d -> %d", (int)was, (int)s_next.v.link);
            was = s_next.v.link;
        }
        /* STATE, where it is wired, on the console: the link state itself
         * is the reading's age. */
        if (PANEL_IMU_PIN_STATE != GPIO_NUM_NC) {
            const int st = gpio_get_level(PANEL_IMU_PIN_STATE);
            if (st != state_was) {
                ESP_LOGI(TAG, "HC-05 STATE %s", st ? "high" : "low");
                state_was = st;
            }
        }
        taskENTER_CRITICAL(&s_mux);
        s_pub = s_next;
        taskEXIT_CRITICAL(&s_mux);
    }
}

void panel_imu_start(void)
{
    imu_ctl_init(&s_ctl);
    const gpio_config_t key = {
        .pin_bit_mask = 1ULL << PANEL_IMU_PIN_KEY,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_set_level(PANEL_IMU_PIN_KEY, 0);
    if (gpio_config(&key) != ESP_OK) {
        ESP_LOGE(TAG, "KEY on GPIO%d not set up; IMU off",
                 (int)PANEL_IMU_PIN_KEY);
        return;
    }
    gpio_set_level(PANEL_IMU_PIN_KEY, 0);
    const uart_config_t uc = {
        .baud_rate = PANEL_IMU_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(PANEL_IMU_UART_NUM, IMU_RX_RING, 0, 0, NULL, 0)
            != ESP_OK
        || uart_param_config(PANEL_IMU_UART_NUM, &uc) != ESP_OK
        || uart_set_pin(PANEL_IMU_UART_NUM, PANEL_IMU_PIN_TX, PANEL_IMU_PIN_RX,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "UART%d on GPIO%d/%d not set up; IMU off",
                 (int)PANEL_IMU_UART_NUM, (int)PANEL_IMU_PIN_TX,
                 (int)PANEL_IMU_PIN_RX);
        return;
    }
    if (PANEL_IMU_PIN_STATE != GPIO_NUM_NC) {
        (void)gpio_set_direction(PANEL_IMU_PIN_STATE, GPIO_MODE_INPUT);
    }
    s_q = xQueueCreate(IMU_Q_LEN, sizeof(imu_msg_t));
    if (s_q == NULL) {
        ESP_LOGE(TAG, "queue not created; IMU off");
        return;
    }
    post_cfg();
    servo_screen_set_imu_axis((uint8_t)settings_get_int(SET_IMU_AXIS),
                              settings_get_int(SET_IMU_SIGN) == 1);
    /*
     * Core 0 beside the renderer, at the knob's priority 2: polls of 5ms,
     * nothing with a deadline.  Stack 3072 bytes: the controller is
     * static; the deepest chain is the UART driver's read.
     */
    if (xTaskCreatePinnedToCore(imu_task, "imu", 3072, NULL, 2, NULL, 0)
        != pdPASS) {
        ESP_LOGE(TAG, "task not created; IMU off");
        vQueueDelete(s_q);
        s_q = NULL;
    }
}

void panel_imu_settings_changed(setting_id_t id)
{
    if (id == SET_IMU_AXIS || id == SET_IMU_SIGN) {
        servo_screen_set_imu_axis((uint8_t)settings_get_int(SET_IMU_AXIS),
                                  settings_get_int(SET_IMU_SIGN) == 1);
        return;
    }
    if (id != SET_IMU_EN && id != SET_IMU_RATE && id != SET_IMU_ALGO) {
        return;
    }
    const char *notice = sensors_rows_imu_notice(id);
    if (notice != NULL && s_q != NULL) {
        ui_router_set_notice(notice);
    }
    post_cfg();
}

void panel_imu_text_changed(setting_text_id_t id)
{
    if (id == SET_TEXT_IMU_ADDR || id == SET_TEXT_IMU_NAME) {
        post_cfg();
    }
}

void panel_imu_frame(void)
{
    if (s_q == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    s_seen = s_pub;
    taskEXIT_CRITICAL(&s_mux);
    imu_screen_set(&s_seen.v);
    sensors_rows_set_imu(s_seen.v.link, s_seen.v.r.rate_dhz);
    /* A bind that ended is kept in SETUP: the address and the name, with a
     * save asked, so the sensor is the bound one after a restart. */
    if (s_seen.bind_seq != s_kept_seq) {
        s_kept_seq = s_seen.bind_seq;
        settings_set_text(SET_TEXT_IMU_ADDR, s_seen.v.bound_addr);
        settings_set_text(SET_TEXT_IMU_NAME, s_seen.v.bound_name);
        settings_request_save();
    }
    imu_msg_t m = { .is_cfg = false };
    while (imu_screen_take(&m.cmd)) {
        if (xQueueSend(s_q, &m, 0) != pdTRUE) {
            ESP_LOGW(TAG, "queue full: command %d dropped", (int)m.cmd.kind);
        }
    }
}

bool panel_imu_angle(servo_test_enc_t *out)
{
    static bool     given;
    static bool     was_valid;
    static uint32_t last_taken;
    const imu_reading_t *r = &s_seen.v.r;
    servo_angle_in_t in = { 0 };
    if (r->valid) {
        /* The same frame is not handed twice: the screen draws faster
         * than a 10Hz sensor sends. */
        if (given && was_valid && r->taken_ms == last_taken) {
            return false;
        }
        in.valid    = true;
        in.deg      = imu_deg(r, (imu_axis_t)settings_get_int(SET_IMU_AXIS),
                              settings_get_int(SET_IMU_SIGN) == 1);
        in.still_ms = r->still_ms;
        in.taken_ms = r->taken_ms;
        last_taken  = r->taken_ms;
    } else {
        if (given && !was_valid) {
            return false;
        }
        /* When it was found so: a run times the silence from here. */
        in.taken_ms = now_ms();
    }
    given     = true;
    was_valid = r->valid;
    (void)servo_angle_enc(&in, out);
    return true;
}
