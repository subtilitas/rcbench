/**
 * @file panel_dial.c
 * @brief The M5Stack Dial on the panel (panel_dial.h): the client of the
 *        `blec` task that runs dial_ctl.h over Bluetooth LE, and the frame
 *        and control tasks' glue.
 *
 * The `blec` task runs it every 5ms beside the IMU: the queue's values and
 * commands, the Bluetooth state and the frame's model to the controller
 * (panel_dial_blec_poll()), the host stack's events the hub routes to it
 * (panel_dial_blec_event()), every action it hands out
 * (panel_dial_blec_step()), then the view and the input are published
 * (panel_dial_blec_publish()).  The first Dial ON starts Bluetooth
 * (panel_ble_start()), as the first IMU ON does.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_dial.h"

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
#include "ui_focus.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "wireless_pane.h"

/** @brief The console lines' tag. */
static const char *TAG = "dial";

/** @brief Values and commands the queue holds. */
#define DIAL_Q_LEN 8
/** @brief The console's counters line while linked, every this many ms. */
#define DIAL_LOG_MS 1000u

_Static_assert(PANEL_BLEC_DIAL == DIAL_BLE_OWNER,
               "the Dial's machine hands out the owner the hub added");

/** @brief What the frame task posts: SETUP's values or a command. */
typedef struct {
    bool       is_cfg; /**< cfg holds SETUP's values, else cmd */
    dial_cfg_t cfg;    /**< SETUP's values */
    dial_cmd_t cmd;    /**< SETUP's command */
} dial_msg_t;

/** @brief What the task publishes for the frame task. */
typedef struct {
    dial_view_t  v;        /**< SETUP's view */
    dial_input_t in;       /**< input not yet taken by a frame */
    uint32_t     bind_seq; /**< binds that ended, counted */
} dial_pub_t;

/** @brief The frame task's values and commands; NULL before the start. */
static QueueHandle_t s_q;
/** @brief Guards s_pub and s_model. */
static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
/** @brief Written by the `blec` task under s_mux, read by the frame. */
static dial_pub_t    s_pub;
/** @brief Written by the frame task under s_mux, read by `blec`. */
static dial_model_t  s_model;
/** @brief The controller, the `blec` task's own. */
static dial_ctl_t    s_ctl;
/** @brief The `blec` task's copy of the model: too big for its stack. */
static dial_model_t  s_model_blec;
/** @brief The `blec` task's view before it is published. */
static dial_view_t   s_view_next;
/** @brief The link's state as last logged, the `blec` task's own. */
static dial_link_t   s_was = DIAL_LINK_OFF;
/** @brief The frame task's copy of s_pub's view. */
static dial_view_t   s_seen;
/** @brief The binds the frame task kept in SETUP. */
static uint32_t      s_kept_seq;
/** @brief The model builder's state, the frame task's own. */
static dial_builder_t s_builder;
/** @brief The frame task's last link state, for the band's notices. */
static dial_link_t   s_frame_was = DIAL_LINK_OFF;
/** @brief ARMED or an arm waiting, from the control task. */
static atomic_bool   s_live;
/** @brief Losses while ARMED, from the `blec` task. */
static atomic_uint   s_errors;

/**
 * @brief SETUP's values as the controller takes them.
 * @param out Receives them.
 */
static void cfg_from_settings(dial_cfg_t *out)
{
    memset(out, 0, sizeof(*out));
    out->en = settings_get_bool(SET_DIAL_EN);
    strncpy(out->addr, settings_text(SET_TEXT_DIAL_ADDR),
            sizeof(out->addr) - 1u);
    strncpy(out->name, settings_text(SET_TEXT_DIAL_NAME),
            sizeof(out->name) - 1u);
    out->thr_tenths = dial_step_tenths(settings_get_int(SET_DIAL_THR));
    out->srv_tenths = dial_step_tenths(settings_get_int(SET_DIAL_SRV));
}

/** @brief SETUP's values to the task; dropped while its queue is full. */
static void post_cfg(void)
{
    if (s_q == NULL) {
        return;
    }
    dial_msg_t m = { .is_cfg = true };
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
static void run_command(const dial_cmd_t *cmd, uint32_t now)
{
    if (cmd->kind == DIAL_CMD_SCAN) {
        const dial_scan_t r = dial_ctl_scan(&s_ctl, atomic_load(&s_live),
                                            now);
        if (r == DIAL_SCAN_ARMED) {
            ESP_LOGW(TAG, "scan refused: the bench is ARMED (%u refused)",
                     (unsigned)s_ctl.refused);
        } else if (r == DIAL_SCAN_NOT_READY) {
            ESP_LOGW(TAG, "scan refused: Dial OFF, no sync or a scan runs");
        } else {
            ESP_LOGI(TAG, "scan for %ums", (unsigned)BLEU_SCAN_MS);
        }
    } else if (cmd->kind == DIAL_CMD_BIND) {
        if (dial_ctl_bind(&s_ctl, cmd->arg, now)) {
            ESP_LOGI(TAG, "bound %s %s", s_ctl.cfg.addr, s_ctl.cfg.name);
        }
    }
}

void panel_dial_blec_poll(uint32_t now)
{
    if (s_q == NULL) {
        return;
    }
    dial_msg_t m;
    while (xQueueReceive(s_q, &m, 0) == pdTRUE) {
        if (m.is_cfg) {
            if (m.cfg.en && panel_ble_state() == PANEL_BLE_OFF) {
                ESP_LOGI(TAG, "Dial ON: starting Bluetooth LE");
                panel_ble_start();
            }
            dial_ctl_config(&s_ctl, &m.cfg, now);
        } else {
            run_command(&m.cmd, now);
        }
    }
    dial_ctl_radio(&s_ctl, radio_now(), now);
    taskENTER_CRITICAL(&s_mux);
    s_model_blec = s_model;
    taskEXIT_CRITICAL(&s_mux);
    dial_ctl_model(&s_ctl, &s_model_blec);
}

void panel_dial_blec_event(const bleu_evt_t *e, uint32_t now)
{
    if (s_q != NULL) {
        dial_ctl_event(&s_ctl, e, now);
    }
}

bool panel_dial_blec_step(uint32_t now, bleu_act_t *out)
{
    if (s_q == NULL) {
        return false;
    }
    const uint32_t errors = s_ctl.errors;
    const bool acted = dial_ctl_step(&s_ctl, now, atomic_load(&s_live), out);
    if (s_ctl.errors != errors) {
        ESP_LOGE(TAG, "Dial lost while ARMED: an error, the bench to SAFE "
                      "(%u)", (unsigned)s_ctl.errors);
        atomic_store(&s_errors, s_ctl.errors);
    }
    return acted;
}

/** @brief Detents turned since the last counters line, either way. */
static uint32_t s_detents;

/**
 * @brief The console's counters line, once a second while linked: EVENTs
 *        and detents in the second, the round trip, the tracker's repeats,
 *        skipped seq numbers, stale and glitch references, EVENTs refused
 *        and dropped before INFO, panel packets written, and the queue's
 *        drops.
 * @param now The clock.
 */
static void log_counters(uint32_t now)
{
    static uint32_t at, events, writes;
    if (now - at < DIAL_LOG_MS) {
        return;
    }
    if (s_ctl.linked) {
        ESP_LOGI(TAG, "%u events in %ums, %u detents, rtt %u%s, repeats %u "
                      "skipped %u stale %u glitches %u, refused %u "
                      "unread %u, %u writes, %u events dropped",
                 (unsigned)(s_ctl.events - events), (unsigned)(now - at),
                 (unsigned)s_detents, (unsigned)s_ctl.rtt_ms,
                 s_ctl.rtt_valid ? "ms" : "ms not measured",
                 (unsigned)s_ctl.track.repeats,
                 (unsigned)s_ctl.track.skipped, (unsigned)s_ctl.track.stale,
                 (unsigned)s_ctl.track.glitches, (unsigned)s_ctl.bad,
                 (unsigned)s_ctl.unread, (unsigned)(s_ctl.writes - writes),
                 (unsigned)panel_ble_dropped());
    }
    at = now;
    events = s_ctl.events;
    writes = s_ctl.writes;
    s_detents = 0u;
}

void panel_dial_blec_publish(uint32_t now)
{
    if (s_q == NULL) {
        return;
    }
    dial_input_t in;
    dial_ctl_take(&s_ctl, &in);
    s_detents += (uint32_t)(in.detents < 0 ? -in.detents : in.detents);
    dial_ctl_view(&s_ctl, &s_view_next);
    const dial_link_t link = (dial_link_t)s_view_next.link;
    if (link != s_was) {
        ESP_LOGI(TAG, "link %d -> %d, error %d code 0x%X, protocol %u",
                 (int)s_was, (int)link, (int)s_ctl.b.err,
                 (unsigned)s_ctl.b.err_status, (unsigned)s_ctl.info_proto);
        s_was = link;
    }
    log_counters(now);
    taskENTER_CRITICAL(&s_mux);
    s_pub.v = s_view_next;
    s_pub.in.detents += in.detents;
    s_pub.in.presses += in.presses;
    s_pub.in.holds += in.holds;
    s_pub.bind_seq = s_ctl.bind_seq;
    taskEXIT_CRITICAL(&s_mux);
}

void panel_dial_start(void)
{
    dial_ctl_init(&s_ctl);
    s_q = xQueueCreate(DIAL_Q_LEN, sizeof(dial_msg_t));
    if (s_q == NULL) {
        ESP_LOGE(TAG, "queue not created; Dial off");
        return;
    }
    post_cfg();
}

void panel_dial_settings_changed(setting_id_t id)
{
    if (id == SET_DIAL_EN || id == SET_DIAL_THR || id == SET_DIAL_SRV) {
        post_cfg();
    }
}

void panel_dial_text_changed(setting_text_id_t id)
{
    if (id == SET_TEXT_DIAL_ADDR || id == SET_TEXT_DIAL_NAME) {
        post_cfg();
    }
}

dial_input_t panel_dial_take(void)
{
    dial_input_t in = { 0 };
    if (s_q == NULL) {
        return in;
    }
    taskENTER_CRITICAL(&s_mux);
    in = s_pub.in;
    memset(&s_pub.in, 0, sizeof(s_pub.in));
    taskEXIT_CRITICAL(&s_mux);
    return in;
}

/**
 * @brief The band's notices of the link: DEGRADED, and a loss while SAFE.
 *        A loss while ARMED is the control task's error with its alert.
 * @param link The link now.
 * @param armed The bench is ARMED.
 */
static void link_notices(dial_link_t link, bool armed)
{
    if (link == s_frame_was) {
        return;
    }
    if (link == DIAL_LINK_DEGRADED) {
        ui_router_set_notice(TR(DIAL_DEGRADED));
    } else if (link == DIAL_LINK_LOST && !armed) {
        ui_router_set_notice(TR(DIAL_LOST_SAFE));
    }
    s_frame_was = link;
}

/**
 * @brief Whether Bluetooth can scan for the Dial: synced and no C6 fault.
 * @param link The link (dial_link_t).
 * @return false while it starts, without Bluetooth or with a failed C6.
 */
static bool bt_ready(uint8_t link)
{
    return link != DIAL_LINK_OFF && link != DIAL_LINK_STARTING
           && link != DIAL_LINK_NO_BT && link != DIAL_LINK_C6_FAILED;
}

void panel_dial_frame(const dial_model_in_t *in)
{
    if (s_q == NULL || in == NULL) {
        return;
    }
    uint32_t bind_seq;
    taskENTER_CRITICAL(&s_mux);
    s_seen = s_pub.v;
    bind_seq = s_pub.bind_seq;
    taskEXIT_CRITICAL(&s_mux);
    wireless_pane_set_dial(&s_seen);
    link_notices((dial_link_t)s_seen.link, in->armed);
    /* The focus ring and the Dial's navigation only while it is linked.
     * The ARMED this frame read keeps the focus off a running bench: a
     * screen the focus navigates (ui_focus_screens.c) must never gain an
     * ARM control unless the focus layer checks ARMED again in the frame
     * that SELECTs it, or a SELECT could land on ARM before this flag
     * follows. */
    ui_focus_set_link(s_seen.link == DIAL_LINK_UP
                          || s_seen.link == DIAL_LINK_DEGRADED,
                      in->armed);
    /* A bind that ended is kept in SETUP, with a save asked, so the Dial
     * is the bound one after a restart. */
    if (bind_seq != s_kept_seq) {
        s_kept_seq = bind_seq;
        settings_set_text(SET_TEXT_DIAL_ADDR, s_seen.bound_addr);
        settings_set_text(SET_TEXT_DIAL_NAME, s_seen.bound_name);
        settings_request_save();
    }
    dial_msg_t m = { .is_cfg = false };
    while (wireless_pane_take_dial(&m.cmd)) {
        if (m.cmd.kind == DIAL_CMD_SCAN && in->armed) {
            ui_router_set_alert(TR(DIAL_SCAN_ARMED));
            continue;
        }
        if (m.cmd.kind == DIAL_CMD_SCAN && !bt_ready(s_seen.link)) {
            ui_router_set_alert(TR(DIAL_SCAN_OFF));
            continue;
        }
        if (xQueueSend(s_q, &m, 0) != pdTRUE) {
            ESP_LOGW(TAG, "queue full: command %d dropped", (int)m.cmd.kind);
        }
    }
    static dial_model_t model;
    dial_model_build(&s_builder, in, &model);
    taskENTER_CRITICAL(&s_mux);
    s_model = model;
    taskEXIT_CRITICAL(&s_mux);
}

void panel_dial_set_live(bool live)
{
    atomic_store(&s_live, live);
}

uint32_t panel_dial_errors(void)
{
    return atomic_load(&s_errors);
}
