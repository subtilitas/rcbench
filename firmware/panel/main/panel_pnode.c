/**
 * @file panel_pnode.c
 * @brief The power node on the panel (panel_pnode.h): the client of the
 *        `blec` task that runs pnode_ctl.h over Bluetooth LE, and the frame
 *        and control tasks' glue.
 *
 * The `blec` task runs it every 5ms beside the IMU and the Dial: the
 * queue's values and commands and the Bluetooth state to the controller
 * (panel_pnode_blec_poll()), the host stack's events the hub routes to it
 * (panel_pnode_blec_event()), every action it hands out
 * (panel_pnode_blec_step()), then the view and the readings are published
 * (panel_pnode_blec_publish()).  The first POWER NODE ON starts Bluetooth
 * (panel_ble_start()), as the first IMU ON does.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_pnode.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "panel.h"
#include "panel_ble.h"
#include "panel_blec.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "wireless_pane.h"

/** @brief The console lines' tag. */
static const char *TAG = "node";

/** @brief Values and commands the queue holds. */
#define PNODE_Q_LEN 8
/** @brief The console's counters line while linked, every this many ms. */
#define PNODE_LOG_MS 1000u

_Static_assert(PANEL_BLEC_PNODE == PNODE_BLE_OWNER,
               "the power node's machine hands out the owner the hub added");

/** @brief What the frame task posts: SETUP's values or a command. */
typedef struct {
    bool        is_cfg; /**< cfg holds SETUP's values, else cmd */
    pnode_cfg_t cfg;    /**< SETUP's values */
    pnode_cmd_t cmd;    /**< SETUP's command */
} pnode_msg_t;

/** @brief The frame task's values and commands; NULL before the start. */
static QueueHandle_t   s_q;
/** @brief Guards s_view, s_bind_seq, s_reading, s_pub_src and s_pub_on. */
static portMUX_TYPE    s_mux = portMUX_INITIALIZER_UNLOCKED;
/** @brief Written by the `blec` task under s_mux, read by the frame. */
static pnode_view_t    s_view;
/** @brief Binds that ended, counted; with s_view. */
static uint32_t        s_bind_seq;
/** @brief Written by the `blec` task under s_mux, read by control. */
static pnode_reading_t s_reading;
/** @brief INA228 SOURCE NODE as the controller holds it, with s_reading:
 *         the control task's readings and refusal follow the same value
 *         the dropout rule does. */
static bool            s_pub_src;
/** @brief POWER NODE ON as the controller holds it, with s_reading. */
static bool            s_pub_on;
/** @brief The controller, the `blec` task's own. */
static pnode_ctl_t     s_ctl;
/** @brief The `blec` task's view before it is published. */
static pnode_view_t    s_view_next;
/** @brief The link's state as last logged, the `blec` task's own. */
static pnode_link_t    s_was = PNODE_LINK_OFF;
/** @brief The frame task's copy of s_view. */
static pnode_view_t    s_seen;
/** @brief The binds the frame task kept in SETUP. */
static uint32_t        s_kept_seq;
/** @brief The frame task's last link state, for the band's notices. */
static pnode_link_t    s_frame_was = PNODE_LINK_OFF;
/** @brief The control task's copy of the last take. */
static pnode_reading_t s_taken;
/** @brief s_taken holds a take. */
static bool            s_taken_ok;
/** @brief ARMED or an arm waiting, from the control task. */
static atomic_bool     s_live;
/** @brief The run reads BENCH (the throttle armed), from the control
 *         task. */
static atomic_bool     s_bench_run;
/** @brief Dropouts that ended a run, from the `blec` task. */
static atomic_uint     s_errors;

/**
 * @brief SETUP's values as the controller takes them.
 * @param out Receives them.
 */
static void cfg_from_settings(pnode_cfg_t *out)
{
    memset(out, 0, sizeof(*out));
    out->en = settings_get_bool(SET_PNODE_EN);
    out->src_node = settings_get_int(SET_I228_SRC) == 1;
    strncpy(out->addr, settings_text(SET_TEXT_PNODE_ADDR),
            sizeof(out->addr) - 1u);
    strncpy(out->name, settings_text(SET_TEXT_PNODE_NAME),
            sizeof(out->name) - 1u);
}

/** @brief SETUP's values to the task; dropped while its queue is full. */
static void post_cfg(void)
{
    if (s_q == NULL) {
        return;
    }
    pnode_msg_t m = { .is_cfg = true };
    cfg_from_settings(&m.cfg);
    if (xQueueSend(s_q, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full: settings not handed over");
    }
}

/** @brief The panel's Bluetooth state as the controller's. @return It. */
static imu_radio_t radio_now(void)
{
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
 * @brief One command of SETUP, with its console line.
 * @param cmd The command.
 * @param now The clock.
 */
static void run_command(const pnode_cmd_t *cmd, uint32_t now)
{
    if (cmd->kind == PNODE_CMD_SCAN) {
        const pnode_scan_t r = pnode_ctl_scan(&s_ctl, atomic_load(&s_live),
                                              now);
        if (r == PNODE_SCAN_ARMED) {
            ESP_LOGW(TAG, "scan refused: the bench is ARMED (%u refused)",
                     (unsigned)s_ctl.refused);
        } else if (r == PNODE_SCAN_NOT_READY) {
            ESP_LOGW(TAG, "scan refused: node OFF, no sync or a scan runs");
        } else {
            ESP_LOGI(TAG, "scan for %ums", (unsigned)BLEU_SCAN_MS);
        }
    } else if (cmd->kind == PNODE_CMD_BIND) {
        if (pnode_ctl_bind(&s_ctl, cmd->arg, now)) {
            ESP_LOGI(TAG, "bound %s %s", s_ctl.cfg.addr, s_ctl.cfg.name);
        }
    }
}

void panel_pnode_blec_poll(uint32_t now)
{
    if (s_q == NULL) {
        return;
    }
    pnode_msg_t m;
    while (xQueueReceive(s_q, &m, 0) == pdTRUE) {
        if (m.is_cfg) {
            if (m.cfg.en && panel_ble_state() == PANEL_BLE_OFF) {
                ESP_LOGI(TAG, "power node ON: starting Bluetooth LE");
                panel_ble_start();
            }
            pnode_ctl_config(&s_ctl, &m.cfg, now);
        } else {
            run_command(&m.cmd, now);
        }
    }
    pnode_ctl_radio(&s_ctl, radio_now(), now);
}

void panel_pnode_blec_event(const bleu_evt_t *e, uint32_t now)
{
    if (s_q != NULL) {
        pnode_ctl_event(&s_ctl, e, now);
    }
}

bool panel_pnode_blec_step(uint32_t now, bleu_act_t *out)
{
    if (s_q == NULL) {
        return false;
    }
    const uint32_t errors = s_ctl.m.errors;
    const bool acted = pnode_ctl_step(&s_ctl, now, atomic_load(&s_live),
                                      atomic_load(&s_bench_run), out);
    if (s_ctl.m.errors != errors) {
        ESP_LOGE(TAG, "readings lost while ARMED on NODE: an error, the "
                      "bench to SAFE (gap %ums, %u gaps, %u)",
                 (unsigned)s_ctl.m.gap.longest_ms,
                 (unsigned)s_ctl.m.gap.count, (unsigned)s_ctl.m.errors);
        atomic_store(&s_errors, s_ctl.m.errors);
    }
    return acted;
}

/**
 * @brief The console's counters line, once a second while linked: windows
 *        and packets in the second, the readings' windows, losses by seq,
 *        refused and unread packets, accumulator restarts, the cell, RUN
 *        writes, and the queue's drops.
 * @param now The clock.
 */
static void log_counters(uint32_t now)
{
    static uint32_t at, windows, packets;
    if (now - at < PNODE_LOG_MS) {
        return;
    }
    const pnode_meter_t *m = &s_ctl.m;
    if (s_ctl.linked) {
        ESP_LOGI(TAG, "%u windows in %ums, %u readings, %u packets, lost %u "
                      "bad %u unread %u, restarts %u, cell %umV, runs %u, "
                      "%u events dropped",
                 (unsigned)(m->windows - windows), (unsigned)(now - at),
                 (unsigned)m->r.win_n, (unsigned)(s_ctl.packets - packets),
                 (unsigned)m->lost, (unsigned)m->bad, (unsigned)s_ctl.unread,
                 (unsigned)m->restarts,
                 (unsigned)(m->have_status ? m->status.vbat : 0u),
                 (unsigned)s_ctl.runs_sent, (unsigned)panel_ble_dropped());
    }
    at = now;
    windows = m->windows;
    packets = s_ctl.packets;
}

void panel_pnode_blec_publish(uint32_t now)
{
    if (s_q == NULL) {
        return;
    }
    pnode_ctl_view(&s_ctl, now, &s_view_next);
    const pnode_link_t link = (pnode_link_t)s_view_next.link;
    if (link != s_was) {
        ESP_LOGI(TAG, "link %d -> %d, error %d code 0x%X, protocol %u",
                 (int)s_was, (int)link, (int)s_ctl.b.err,
                 (unsigned)s_ctl.b.err_status, (unsigned)s_ctl.info_proto);
        s_was = link;
    }
    log_counters(now);
    taskENTER_CRITICAL(&s_mux);
    s_view = s_view_next;
    s_bind_seq = s_ctl.bind_seq;
    pnode_meter_reading(&s_ctl.m, &s_reading);
    s_pub_src = s_ctl.cfg.src_node;
    s_pub_on = s_ctl.cfg.en;
    taskEXIT_CRITICAL(&s_mux);
}

void panel_pnode_start(void)
{
    pnode_ctl_init(&s_ctl);
    s_q = xQueueCreate(PNODE_Q_LEN, sizeof(pnode_msg_t));
    if (s_q == NULL) {
        ESP_LOGE(TAG, "queue not created; power node off");
        return;
    }
    post_cfg();
}

void panel_pnode_settings_changed(setting_id_t id)
{
    if (id == SET_PNODE_EN || id == SET_I228_SRC) {
        post_cfg();
    }
}

void panel_pnode_text_changed(setting_text_id_t id)
{
    if (id == SET_TEXT_PNODE_ADDR || id == SET_TEXT_PNODE_NAME) {
        post_cfg();
    }
}

/**
 * @brief The band's notices of the link: no reading for 300ms, and a loss
 *        while SAFE.  A run's dropout while ARMED on NODE is the control
 *        task's error with its alert.
 * @param link  The link now.
 * @param armed The bench is ARMED.
 */
static void link_notices(pnode_link_t link, bool armed)
{
    if (link == s_frame_was) {
        return;
    }
    if (link == PNODE_LINK_STALE) {
        ui_router_set_notice(TR(PNODE_STALE));
    } else if (link == PNODE_LINK_LOST && !armed) {
        ui_router_set_notice(TR(PNODE_LOST_SAFE));
    }
    s_frame_was = link;
}

/**
 * @brief Whether Bluetooth can scan for the node: synced and no C6 fault.
 * @param link The link (pnode_link_t).
 * @return false while it starts, without Bluetooth or with a failed C6.
 */
static bool bt_ready(uint8_t link)
{
    return link != PNODE_LINK_OFF && link != PNODE_LINK_STARTING
           && link != PNODE_LINK_NO_BT && link != PNODE_LINK_C6_FAILED;
}

void panel_pnode_frame(bool armed)
{
    if (s_q == NULL) {
        return;
    }
    uint32_t bind_seq;
    taskENTER_CRITICAL(&s_mux);
    s_seen = s_view;
    bind_seq = s_bind_seq;
    taskEXIT_CRITICAL(&s_mux);
    wireless_pane_set_pnode(&s_seen);
    link_notices((pnode_link_t)s_seen.link, armed);
    /* A bind that ended is kept in SETUP, with a save asked, so the node
     * is the bound one after a restart. */
    if (bind_seq != s_kept_seq) {
        s_kept_seq = bind_seq;
        settings_set_text(SET_TEXT_PNODE_ADDR, s_seen.bound_addr);
        settings_set_text(SET_TEXT_PNODE_NAME, s_seen.bound_name);
        settings_request_save();
    }
    pnode_msg_t m = { .is_cfg = false };
    while (wireless_pane_take_pnode(&m.cmd)) {
        if (m.cmd.kind == PNODE_CMD_SCAN && armed) {
            ui_router_set_alert(TR(PNODE_SCAN_ARMED));
            continue;
        }
        if (m.cmd.kind == PNODE_CMD_SCAN && !bt_ready(s_seen.link)) {
            ui_router_set_alert(TR(PNODE_SCAN_OFF));
            continue;
        }
        if (xQueueSend(s_q, &m, 0) != pdTRUE) {
            ESP_LOGW(TAG, "queue full: command %d dropped", (int)m.cmd.kind);
        }
    }
}

void panel_pnode_bench(bench_state_t *b)
{
    if (b == NULL) {
        return;
    }
    pnode_reading_t now;
    bool src = false;
    if (s_q != NULL) {
        taskENTER_CRITICAL(&s_mux);
        now = s_reading;
        src = s_pub_src;
        taskEXIT_CRITICAL(&s_mux);
    }
    if (!src) {
        b->from_node = false;
        s_taken_ok = false;
        return;
    }
    (void)pnode_reading_bench(s_taken_ok ? &s_taken : NULL, &now, now_ms(),
                              b);
    s_taken = now;
    s_taken_ok = true;
}

void panel_pnode_set_live(bool live, bool bench_run)
{
    atomic_store(&s_bench_run, bench_run);
    atomic_store(&s_live, live);
}

bool panel_pnode_arm_refused(void)
{
    if (s_q == NULL) {
        return false;
    }
    pnode_reading_t r;
    bool src;
    bool on;
    taskENTER_CRITICAL(&s_mux);
    r = s_reading;
    src = s_pub_src;
    on = s_pub_on;
    taskEXIT_CRITICAL(&s_mux);
    return pnode_arm_refused(src, on, &r, now_ms());
}

uint32_t panel_pnode_errors(void)
{
    return atomic_load(&s_errors);
}
