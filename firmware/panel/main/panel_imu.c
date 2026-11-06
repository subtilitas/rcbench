/**
 * @file panel_imu.c
 * @brief The Bluetooth IMU on the panel (panel_imu.h): the task that runs
 *        imu_ctl.h over Bluetooth LE (panel_ble.h), and the frame task's
 *        glue.
 *
 * The task runs every IMU_POLL_MS: the queue's values and commands go to
 * the controller, then the Bluetooth state, then the host stack's events
 * (panel_ble_get()), then every action the controller hands out
 * (panel_ble_do()), then the view is published.  The first IMU ON starts
 * Bluetooth (panel_ble_start()); it stays until the panel restarts.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_imu.h"

#include <stdio.h>
#include <string.h>

#include "c6_link.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "imu_ctl.h"
#include "imu_screen.h"
#include "panel.h"
#include "panel_ble.h"
#include "sensors_rows.h"
#include "servo_angle.h"
#include "servo_screen.h"
#include "ui_screen.h"

/** @brief The console lines' tag. */
static const char *TAG = "imu";

/** @brief The task's poll, ms: the controller is stepped at least this often. */
#define IMU_POLL_MS   5
/** @brief Values and commands the queue holds. */
#define IMU_Q_LEN     8
/** @brief Host stack events taken in one poll at most: the whole queue. */
#define IMU_EVTS_MAX  PANEL_BLE_QUEUE_LEN
/** @brief Actions taken in one poll at most. */
#define IMU_ACTS_MAX  16
/** @brief The console's reading line while connected, every this many ms. */
#define IMU_LOG_MS    1000u

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
    uint32_t   gaps;      /**< stale stretches a frame ended (imu_sensor_t) */
    uint32_t   gap_at_ms; /**< when the last of them went stale */
    uint32_t   zero_gen;  /**< the zero's generation (imu_sensor_t) */
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
 * @brief The panel's Bluetooth state as the controller's, and the C6's
 *        version as text.
 * @param ver Takes "2.13.0", or "" when the C6 reported none.
 * @param n   Its size.
 * @return The state.
 */
static imu_radio_t radio_now(char *ver, size_t n)
{
    c6_link_version_t v;
    ver[0] = '\0';
    if (c6_link_version(&v)) {
        snprintf(ver, n, "%u.%u.%u", (unsigned)v.major, (unsigned)v.minor,
                 (unsigned)v.patch);
    }
    switch (panel_ble_state()) {
    case PANEL_BLE_STARTING: return IMU_RADIO_STARTING;
    case PANEL_BLE_NO_LINK:  return IMU_RADIO_NO_LINK;
    case PANEL_BLE_NO_SYNC:  return IMU_RADIO_NO_SYNC;
    case PANEL_BLE_READY:    return IMU_RADIO_READY;
    case PANEL_BLE_OFF:
    default:                 return IMU_RADIO_OFF;
    }
}

/**
 * @brief One action of the controller, to NimBLE; a sensor command written
 *        on 8841 is logged.
 * @param a The action.
 */
static void perform(const bleu_act_t *a)
{
    if ((a->kind == BLEU_ACT_WRITE || a->kind == BLEU_ACT_WRITE_NR)
        && a->len == WIT_CMD_LEN) {
        ESP_LOGI(TAG, "command %02X %02X %02X %02X %02X%s", a->data[0],
                 a->data[1], a->data[2], a->data[3], a->data[4],
                 a->kind == BLEU_ACT_WRITE ? "" : " (no response)");
    }
    panel_ble_do(a);
}

/**
 * @brief The console's reading line, once a second while connected: frames
 *        in the second, the rate, the angles, bad frames, the battery and
 *        the dropped events.
 * @param now The clock.
 */
static void log_reading(uint32_t now)
{
    static uint32_t at, frames;
    const imu_reading_t *r = &s_next.v.r;
    if (now - at < IMU_LOG_MS) {
        return;
    }
    if (s_next.v.link == IMU_LINK_CONNECTED) {
        ESP_LOGI(TAG, "%u frames in %ums, rate %.1fHz, roll %.3f pitch %.3f "
                      "yaw %.3f deg, bad %u, battery %.2fV, %u events dropped",
                 (unsigned)(r->frames - frames), (unsigned)(now - at),
                 (double)r->rate_dhz / 10.0, (double)r->angle[0],
                 (double)r->angle[1], (double)r->angle[2], (unsigned)r->bad,
                 (double)r->battery_v, (unsigned)panel_ble_dropped());
    }
    at = now;
    frames = r->frames;
}

/**
 * @brief The task: messages, the Bluetooth state, events, actions, the
 *        view, every IMU_POLL_MS.
 * @param arg Unused.
 */
static void imu_task(void *arg)
{
    (void)arg;
    imu_link_t was = IMU_LINK_OFF;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(IMU_POLL_MS));
        const uint32_t now = now_ms();
        imu_msg_t m;
        while (xQueueReceive(s_q, &m, 0) == pdTRUE) {
            if (m.is_cfg) {
                if (m.cfg.en && panel_ble_state() == PANEL_BLE_OFF) {
                    ESP_LOGI(TAG, "IMU ON: starting Bluetooth LE");
                    panel_ble_start();
                }
                imu_ctl_config(&s_ctl, &m.cfg, now);
            } else {
                (void)imu_ctl_command(&s_ctl, &m.cmd, now);
            }
        }
        char ver[IMU_VERSION_MAX];
        const imu_radio_t radio = radio_now(ver, sizeof(ver));
        imu_ctl_radio(&s_ctl, radio, ver, now);
        bleu_evt_t e;
        for (unsigned i = 0; i < IMU_EVTS_MAX && panel_ble_get(&e); ++i) {
            imu_ctl_event(&s_ctl, &e, now);
        }
        bleu_act_t act;
        for (int i = 0; i < IMU_ACTS_MAX && imu_ctl_step(&s_ctl, now, &act);
             ++i) {
            perform(&act);
        }
        imu_ctl_view(&s_ctl, &s_next.v);
        s_next.bind_seq = s_ctl.bind_seq;
        s_next.gaps = s_ctl.sensor.gaps;
        s_next.gap_at_ms = s_ctl.sensor.gap_at_ms;
        s_next.zero_gen = s_ctl.sensor.zero_gen;
        if (s_next.v.link != was) {
            ESP_LOGI(TAG, "link %d -> %d, error %d code 0x%X", (int)was,
                     (int)s_next.v.link, (int)s_next.v.err,
                     (unsigned)s_next.v.err_code);
            was = s_next.v.link;
        }
        log_reading(now);
        taskENTER_CRITICAL(&s_mux);
        s_pub = s_next;
        taskEXIT_CRITICAL(&s_mux);
    }
}

void panel_imu_start(void)
{
    imu_ctl_init(&s_ctl);
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
     * nothing with a deadline.  Stack 4096 bytes: the controller is
     * static; the deepest chain is a NimBLE call of panel_ble_do() or the
     * reading's console line, tools/stack_check.py holds it.
     */
    if (xTaskCreatePinnedToCore(imu_task, "imu", 4096, NULL, 2, NULL, 0)
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
    sensors_rows_set_imu_cal(s_seen.v.cal == IMU_CAL_RUNNING);
    /* A new IMU zero drops RUDDER's zero and MAX taken on the IMU. */
    servo_screen_set_imu_zero(s_seen.zero_gen);
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

bool panel_imu_angle(bool fresh, servo_test_enc_t *out)
{
    static imu_handout_t hand;
    const imu_reading_t *r = &s_seen.v.r;
    if (fresh) {
        imu_handout_reset(&hand);
    }
    /* The same frame is not handed twice: the screen draws faster than a
     * 10Hz sensor sends.  A stale stretch the frames ended before this
     * task looked goes first, as a stale reading timed when it began. */
    const imu_hand_t give = imu_handout_next(&hand, r, s_seen.gaps);
    if (give == IMU_HAND_NONE) {
        return false;
    }
    servo_angle_in_t in = { 0 };
    if (give == IMU_HAND_GAP) {
        in.taken_ms = s_seen.gap_at_ms;
    } else if (r->valid) {
        in.valid    = true;
        in.deg      = imu_deg(r, (imu_axis_t)settings_get_int(SET_IMU_AXIS),
                              settings_get_int(SET_IMU_SIGN) == 1);
        in.still_ms = r->still_ms;
        in.taken_ms = r->taken_ms;
    } else {
        /* When it was found so: a run times the silence from here. */
        in.taken_ms = now_ms();
    }
    (void)servo_angle_enc(&in, out);
    return true;
}
