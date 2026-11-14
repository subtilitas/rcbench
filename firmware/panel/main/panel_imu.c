/**
 * @file panel_imu.c
 * @brief The Bluetooth IMU on the panel (panel_imu.h): the client of the
 *        `blec` task (panel_blec.h) that runs imu_ctl.h over Bluetooth LE
 *        (panel_ble.h), and the frame task's glue.
 *
 * The `blec` task runs it every 5ms: the queue's values and commands go to
 * the controller, then the Bluetooth state (panel_imu_blec_poll()), then
 * the host stack's events the hub routes to it (panel_imu_blec_event()),
 * then every action the controller hands out (panel_imu_blec_step()), then
 * the view is published (panel_imu_blec_publish()).  The first IMU ON
 * starts Bluetooth (panel_ble_start()); it stays until the panel restarts.
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
#include "panel_blec.h"
#include "sensors_rows.h"
#include "servo_angle.h"
#include "servo_screen.h"
#include "ui_screen.h"

/** @brief The console lines' tag. */
static const char *TAG = "imu";

/** @brief Values and commands the queue holds. */
#define IMU_Q_LEN     8
/** @brief The console's reading line while connected, every this many ms. */
#define IMU_LOG_MS    1000u

_Static_assert(PANEL_BLEC_IMU == 0u,
               "bleu_init() gives the IMU's machine owner 0");

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
/** @brief Written by the `blec` task under s_mux, read by the frame task. */
static imu_pub_t     s_pub;
/** @brief The `blec` task's own. */
static imu_ctl_t     s_ctl;
/** @brief The `blec` task's copy before it is published: too big for its
 *         stack. */
static imu_pub_t     s_next;
/** @brief The link's state as last logged, the `blec` task's own. */
static imu_link_t    s_was = IMU_LINK_OFF;
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
    out->filter = imu_filter_clamp(settings_get_int(SET_IMU_FILTER));
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
 * @brief Log a sensor command the controller writes on 8841.
 * @param a The action.
 */
static void log_command(const bleu_act_t *a)
{
    if ((a->kind == BLEU_ACT_WRITE || a->kind == BLEU_ACT_WRITE_NR)
        && a->len == WIT_CMD_LEN) {
        ESP_LOGI(TAG, "command %02X %02X %02X %02X %02X%s", a->data[0],
                 a->data[1], a->data[2], a->data[3], a->data[4],
                 a->kind == BLEU_ACT_WRITE ? "" : " (no response)");
    }
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

void panel_imu_blec_poll(uint32_t now)
{
    if (s_q == NULL) {
        return;
    }
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
}

void panel_imu_blec_event(const bleu_evt_t *e, uint32_t now)
{
    if (s_q != NULL) {
        imu_ctl_event(&s_ctl, e, now);
    }
}

bool panel_imu_blec_step(uint32_t now, bleu_act_t *out)
{
    if (s_q == NULL || !imu_ctl_step(&s_ctl, now, out)) {
        return false;
    }
    log_command(out);
    return true;
}

void panel_imu_blec_publish(uint32_t now)
{
    if (s_q == NULL) {
        return;
    }
    imu_ctl_view(&s_ctl, &s_next.v);
    s_next.bind_seq = s_ctl.bind_seq;
    s_next.gaps = s_ctl.sensor.gaps;
    s_next.gap_at_ms = s_ctl.sensor.gap_at_ms;
    s_next.zero_gen = s_ctl.sensor.zero_gen;
    if (s_next.v.link != s_was) {
        ESP_LOGI(TAG, "link %d -> %d, error %d code 0x%X", (int)s_was,
                 (int)s_next.v.link, (int)s_next.v.err,
                 (unsigned)s_next.v.err_code);
        s_was = s_next.v.link;
    }
    log_reading(now);
    taskENTER_CRITICAL(&s_mux);
    s_pub = s_next;
    taskEXIT_CRITICAL(&s_mux);
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
}

void panel_imu_settings_changed(setting_id_t id)
{
    if (id == SET_IMU_AXIS || id == SET_IMU_SIGN) {
        servo_screen_set_imu_axis((uint8_t)settings_get_int(SET_IMU_AXIS),
                                  settings_get_int(SET_IMU_SIGN) == 1);
        return;
    }
    if (id != SET_IMU_EN && id != SET_IMU_RATE && id != SET_IMU_ALGO
        && id != SET_IMU_FILTER) {
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
    /* AUTO waits this much longer for the filtered angle to settle. */
    servo_screen_set_imu_settle(imu_filter_settle_ms(
        settings_get_int(SET_IMU_FILTER), s_seen.v.r.rate_dhz));
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
    /* The angle of IMU AXIS with that axis' own still time: pitch or roll
     * moving does not hold a still yaw at 0. */
    servo_angle_in_t in = { 0 };
    if (give == IMU_HAND_GAP) {
        in.taken_ms = s_seen.gap_at_ms;
    } else if (!imu_handout_angle(r,
                                  (imu_axis_t)settings_get_int(SET_IMU_AXIS),
                                  settings_get_int(SET_IMU_SIGN) == 1, &in)) {
        /* When it was found so: a run times the silence from here. */
        in.taken_ms = now_ms();
    }
    (void)servo_angle_enc(&in, out);
    return true;
}
