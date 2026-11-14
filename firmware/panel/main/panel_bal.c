/**
 * @file panel_bal.c
 * @brief The balance node on the panel (panel_bal.h).
 *
 * The `blec` task runs it every 5ms beside the IMU and the Dial: the
 * queue's values and commands, the Bluetooth state and the frame's blade
 * count and rpm to the controller (panel_bal_blec_poll()), the events the
 * hub routes to it (panel_bal_blec_event()), every action it hands out
 * (panel_bal_blec_step()), then the view and the last report are published
 * (panel_bal_blec_publish()).  The first BALANCE NODE ON starts Bluetooth
 * (panel_ble_start()), as the IMU's and the Dial's do.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_bal.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bal_ctl.h"
#include "card_tree.h"
#include "node_balance.h"
#include "panel.h"
#include "panel_ble.h"
#include "panel_blec.h"
#include "storage.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "wireless_bal.h"

/** @brief The console lines' tag. */
static const char *TAG = "bal";

/** @brief Values and commands the queue holds. */
#define BAL_Q_LEN 8
/** @brief CSV rows waiting for the logger. */
#define BAL_CSV_Q_LEN 2
/** @brief The console's counters line while linked, every this many ms. */
#define BAL_LOG_MS 5000u

_Static_assert(PANEL_BLEC_BAL == BAL_BLE_OWNER,
               "the node's machine hands out the owner the hub added");

/** @brief What the frame task posts: SETUP's values or a command. */
typedef struct {
    bool      is_cfg; /**< cfg holds SETUP's values, else cmd */
    bal_cfg_t cfg;    /**< SETUP's values */
    bal_cmd_t cmd;    /**< SETUP's command */
} bal_msg_t;

/** @brief A CSV row and its number, for the logger. */
typedef struct {
    uint32_t seq;              /**< node_balance_csv_seq() of the row */
    char     row[NB_CSV_MAX];  /**< the row, with its newline */
} bal_row_t;

/** @brief What the `blec` task publishes for the frame task. */
typedef struct {
    bal_view_t          v;        /**< SETUP's view */
    bool                present;  /**< ON and a node bound */
    bool                linked;   /**< INFO read on a link that stands */
    bool                have;     /**< rep holds a report */
    rcnode_bal_report_t rep;      /**< the last report */
    uint32_t            rep_ms;   /**< when it came, now_ms() */
    uint32_t            bind_seq; /**< binds that ended, counted */
} bal_pub_t;

/** @brief What the frame task hands the controller. */
typedef struct {
    uint8_t  blades; /**< the rotor's blade count */
    uint32_t rpm;    /**< the bench's rpm, 0 not known */
} bal_drive_t;

/** @brief The frame task's values and commands; NULL before the start. */
static QueueHandle_t s_q;
/** @brief The solutions' CSV rows for the logger. */
static QueueHandle_t s_csv_q;
/** @brief Guards s_pub and s_wants. */
static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
/** @brief Written by the `blec` task under s_mux, read by the frame. */
static bal_pub_t     s_pub;
/** @brief Written by the frame task under s_mux, read by `blec`. */
static bal_drive_t   s_wants = { 2u, 0u };
/** @brief The controller, the `blec` task's own. */
static bal_ctl_t     s_ctl;
/** @brief The `blec` task's publication before it goes out. */
static bal_pub_t     s_pub_next;
/** @brief The frame task's copy of s_pub. */
static bal_pub_t     s_seen;
/** @brief The link's state as last logged, the `blec` task's own. */
static bal_link_t    s_was = BAL_LINK_OFF;
/** @brief The binds the frame task kept in SETUP. */
static uint32_t      s_kept_seq;
/** @brief ARMED or an arm waiting, from the control task. */
static atomic_bool   s_live;
/** @brief The logger's last answer: the row's number shifted left by one,
 *         bit 0 set when it was written; 0 none.  Read by the frame. */
static atomic_uint   s_csv_done;
/** @brief The answer the frame task took last. */
static uint32_t      s_csv_seen;

/**
 * @brief SETUP's values as the controller takes them.
 * @param out Receives them.
 */
static void cfg_from_settings(bal_cfg_t *out)
{
    memset(out, 0, sizeof(*out));
    out->en = settings_get_bool(SET_BNODE_EN);
    strncpy(out->addr, settings_text(SET_TEXT_BNODE_ADDR),
            sizeof(out->addr) - 1u);
    strncpy(out->name, settings_text(SET_TEXT_BNODE_NAME),
            sizeof(out->name) - 1u);
}

/** @brief SETUP's values to the task; dropped while its queue is full. */
static void post_cfg(void)
{
    if (s_q == NULL) {
        return;
    }
    bal_msg_t m = { .is_cfg = true };
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
static void run_command(const bal_cmd_t *cmd, uint32_t now)
{
    if (cmd->kind == BAL_CMD_SCAN) {
        const bal_scan_t r = bal_ctl_scan(&s_ctl, atomic_load(&s_live), now);
        if (r == BAL_SCAN_ARMED) {
            ESP_LOGW(TAG, "scan refused: the bench is ARMED (%u refused)",
                     (unsigned)s_ctl.refused);
        } else if (r == BAL_SCAN_NOT_READY) {
            ESP_LOGW(TAG, "scan refused: node OFF, no sync or a scan runs");
        } else {
            ESP_LOGI(TAG, "scan for %ums", (unsigned)BLEU_SCAN_MS);
        }
    } else if (cmd->kind == BAL_CMD_BIND) {
        if (bal_ctl_bind(&s_ctl, cmd->arg, now)) {
            ESP_LOGI(TAG, "bound %s %s", s_ctl.cfg.addr, s_ctl.cfg.name);
        }
    }
}

void panel_bal_blec_poll(uint32_t now)
{
    if (s_q == NULL) {
        return;
    }
    bal_msg_t m;
    while (xQueueReceive(s_q, &m, 0) == pdTRUE) {
        if (m.is_cfg) {
            if (m.cfg.en && panel_ble_state() == PANEL_BLE_OFF) {
                ESP_LOGI(TAG, "balance node ON: starting Bluetooth LE");
                panel_ble_start();
            }
            bal_ctl_config(&s_ctl, &m.cfg, now);
        } else {
            run_command(&m.cmd, now);
        }
    }
    bal_ctl_radio(&s_ctl, radio_now(), now);
    taskENTER_CRITICAL(&s_mux);
    const bal_drive_t d = s_wants;
    taskEXIT_CRITICAL(&s_mux);
    bal_ctl_drive(&s_ctl, d.blades, d.rpm);
}

void panel_bal_blec_event(const bleu_evt_t *e, uint32_t now)
{
    if (s_q != NULL) {
        bal_ctl_event(&s_ctl, e, now);
    }
}

bool panel_bal_blec_step(uint32_t now, bleu_act_t *out)
{
    if (s_q == NULL) {
        return false;
    }
    return bal_ctl_step(&s_ctl, now, atomic_load(&s_live), out);
}

/**
 * @brief The console's counters line, every 5s while linked: reports in
 *        the period, reports lost by their seq, packets that were no
 *        report, CONTROL writes and refusals, the cell.
 * @param now The clock.
 */
static void log_counters(uint32_t now)
{
    static uint32_t at, reports, writes;
    if (now - at < BAL_LOG_MS) {
        return;
    }
    if (s_ctl.linked) {
        ESP_LOGI(TAG, "%u reports in %ums, lost %u, bad %u, unread %u, "
                      "%u writes, %u failed, cell %umV%s",
                 (unsigned)(s_ctl.reports - reports), (unsigned)(now - at),
                 (unsigned)s_ctl.lost, (unsigned)s_ctl.bad,
                 (unsigned)s_ctl.unread, (unsigned)(s_ctl.writes - writes),
                 (unsigned)s_ctl.wr_failed, (unsigned)s_ctl.bat_mv,
                 s_ctl.bat_known ? "" : " not read");
    }
    at = now;
    reports = s_ctl.reports;
    writes = s_ctl.writes;
}

void panel_bal_blec_publish(uint32_t now)
{
    if (s_q == NULL) {
        return;
    }
    bal_ctl_view(&s_ctl, now, &s_pub_next.v);
    const bal_link_t link = (bal_link_t)s_pub_next.v.link;
    if (link != s_was) {
        ESP_LOGI(TAG, "link %d -> %d, error %d code 0x%X, role %u "
                      "protocol %u",
                 (int)s_was, (int)link, (int)s_ctl.b.err,
                 (unsigned)s_ctl.b.err_status, (unsigned)s_ctl.info_role,
                 (unsigned)s_ctl.info_proto);
        s_was = link;
    }
    log_counters(now);
    uint32_t age = 0u;
    s_pub_next.present = link != BAL_LINK_OFF && s_pub_next.v.bound_addr[0];
    s_pub_next.linked = link >= BAL_LINK_WAITING && link != BAL_LINK_WRONG;
    s_pub_next.have = bal_ctl_report(&s_ctl, now, &s_pub_next.rep, &age);
    s_pub_next.rep_ms = now - age;
    s_pub_next.bind_seq = s_ctl.bind_seq;
    taskENTER_CRITICAL(&s_mux);
    s_pub = s_pub_next;
    taskEXIT_CRITICAL(&s_mux);
}

void panel_bal_start(void)
{
    bal_ctl_init(&s_ctl);
    node_balance_reset();
    s_csv_q = xQueueCreate(BAL_CSV_Q_LEN, sizeof(bal_row_t));
    s_q = xQueueCreate(BAL_Q_LEN, sizeof(bal_msg_t));
    if (s_q == NULL || s_csv_q == NULL) {
        ESP_LOGE(TAG, "queue not created; balance node off");
        s_q = NULL;
        return;
    }
    post_cfg();
}

void panel_bal_settings_changed(setting_id_t id)
{
    if (id == SET_BNODE_EN) {
        post_cfg();
    }
}

void panel_bal_text_changed(setting_text_id_t id)
{
    if (id == SET_TEXT_BNODE_ADDR || id == SET_TEXT_BNODE_NAME) {
        post_cfg();
    }
}

/**
 * @brief Whether Bluetooth can scan for the node: synced and no C6 fault.
 * @param link The link (bal_link_t).
 * @return false while it starts, without Bluetooth or with a failed C6.
 */
static bool bt_ready(uint8_t link)
{
    return link != BAL_LINK_OFF && link != BAL_LINK_STARTING
           && link != BAL_LINK_NO_BT && link != BAL_LINK_C6_FAILED;
}

void panel_bal_frame(bool armed, int blades, uint32_t rpm)
{
    if (s_q == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    s_seen = s_pub;
    s_wants.blades = (uint8_t)(blades < 1 ? 1 : blades > 12 ? 12 : blades);
    s_wants.rpm = rpm;
    taskEXIT_CRITICAL(&s_mux);
    wireless_pane_set_bal(&s_seen.v);
    /* A bind that ended is kept in SETUP, with a save asked, so the node
     * is the bound one after a restart. */
    if (s_seen.bind_seq != s_kept_seq) {
        s_kept_seq = s_seen.bind_seq;
        settings_set_text(SET_TEXT_BNODE_ADDR, s_seen.v.bound_addr);
        settings_set_text(SET_TEXT_BNODE_NAME, s_seen.v.bound_name);
        settings_request_save();
    }
    bal_msg_t m = { .is_cfg = false };
    while (wireless_pane_take_bal(&m.cmd)) {
        if (m.cmd.kind == BAL_CMD_SCAN && armed) {
            ui_router_set_alert(TR(BNODE_SCAN_ARMED));
            continue;
        }
        if (m.cmd.kind == BAL_CMD_SCAN && !bt_ready(s_seen.v.link)) {
            ui_router_set_alert(TR(BNODE_SCAN_OFF));
            continue;
        }
        if (xQueueSend(s_q, &m, 0) != pdTRUE) {
            ESP_LOGW(TAG, "queue full: command %d dropped", (int)m.cmd.kind);
        }
    }
    /* ON and bound with the link down: "--" and the kept result, never
     * the model; the model only while OFF or unbound. */
    const uint32_t age = now_ms() - s_seen.rep_ms;
    node_balance_feed(s_seen.present,
                      s_seen.linked && s_seen.have ? &s_seen.rep : NULL, age);
    const uint32_t done = atomic_load(&s_csv_done);
    if (done != s_csv_seen) {
        s_csv_seen = done;
        node_balance_csv_done(done >> 1, (done & 1u) != 0u);
    }
    static bal_row_t row;
    if (node_balance_take_csv(blades, row.row, sizeof(row.row)) > 0u) {
        row.seq = node_balance_csv_seq();
        if (xQueueSend(s_csv_q, &row, 0) != pdTRUE) {
            ESP_LOGW(TAG, "CSV queue full: a solution's row dropped");
            node_balance_csv_done(row.seq, false);
        }
    }
}

void panel_bal_set_live(bool live)
{
    atomic_store(&s_live, live);
}

/**
 * @brief One row appended to the CSV file, its header first in a new file.
 *        A file that does not end in a newline, a row torn by a power cut
 *        or a card pulled, gets one first, so the new row starts a line.
 * @param row The row, with its newline.
 * @return true when it was written and synced.
 */
static bool append_row(const char *row)
{
    static char path[sizeof(STORAGE_MOUNT_POINT) + sizeof(CARD_BALANCE)
                     + sizeof(PANEL_BAL_CSV) + 2u];
    if (card_make_dirs(CARD_BALANCE, panel_card_make_dir, NULL)
        != CARD_DIR_OK) {
        return false;
    }
    snprintf(path, sizeof(path), "%s/%s/%s", STORAGE_MOUNT_POINT,
             CARD_BALANCE, PANEL_BAL_CSV);
    FILE *f = fopen(path, "a+");
    if (f == NULL) {
        return false;
    }
    bool ok = fseek(f, 0L, SEEK_END) == 0;
    const long size = ok ? ftell(f) : -1L;
    bool torn = false;
    if (size > 0L && fseek(f, -1L, SEEK_END) == 0) {
        torn = fgetc(f) != '\n';
    }
    /* A read is followed by a seek before the writes; "a+" appends at the
     * end whatever the position. */
    ok = (fseek(f, 0L, SEEK_END) == 0) && ok;
    if (size == 0L) {
        ok = (fputs(nb_csv_header(), f) >= 0) && ok;
    } else if (torn) {
        ok = (fputc('\n', f) != EOF) && ok;
    }
    ok = (fputs(row, f) >= 0) && ok;
    ok = (fflush(f) == 0) && ok;
    ok = (fsync(fileno(f)) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    return ok;
}

void panel_bal_card_serve(void)
{
    static bal_row_t row;
    if (s_csv_q == NULL) {
        return;
    }
    while (xQueueReceive(s_csv_q, &row, 0) == pdTRUE) {
        const bool held = storage_hold();
        const bool ok = held && append_row(row.row);
        if (held) {
            storage_release();
        }
        atomic_store(&s_csv_done, (row.seq << 1) | (ok ? 1u : 0u));
        if (ok) {
            ESP_LOGI(TAG, "row written to %s/%s", CARD_BALANCE,
                     PANEL_BAL_CSV);
        } else {
            ESP_LOGW(TAG, "row not written: no card or the write failed");
        }
    }
}
