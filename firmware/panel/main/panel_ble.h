/**
 * @file panel_ble.h
 * @brief Bluetooth LE (Low Energy) on the panel: the NimBLE host on the P4,
 *        the controller on the board's ESP32-C6, HCI (host controller
 *        interface) over the SDIO link (c6_link.h), and the queue of events
 *        the host hands the IMU's link.
 *
 * panel_ble_start() starts it once and it stays until the panel restarts:
 * the host is never stopped.  The start runs on a task of its own,
 * `blestart`, because the C6's link can take about 25s on a C6 that does
 * not answer.  The steps and their console lines:
 *
 * 1. c6_link_up(); a failed link ends the start (PANEL_BLE_NO_LINK).
 * 2. The C6's controller init and enable requests (esp_hosted 2.5.2 and
 *    later), sent to a C6 that reports 2.5.2 or later.  A C6 that reports
 *    an older version or none, as the board's 0.0.0 does, has no such
 *    requests and runs its controller from its own start; it gets them only
 *    when the host has no sync without them.  An unanswered or refused
 *    request is logged and the start goes on.
 * 3. nimble_port_init() and the host task `nimble_host`; the host's sync,
 *    its HCI reset answered by the C6, is awaited for PANEL_BLE_SYNC_MS.
 *    Without it the state is PANEL_BLE_NO_SYNC and the host keeps trying;
 *    a sync later makes it PANEL_BLE_READY.
 *
 * The IMU's link (protocols/bleuart) runs on the IMU task: panel_ble_do()
 * turns each of its actions into a NimBLE call, and the host task's
 * callbacks turn NimBLE's answers into bleuart's events, each with the
 * token of the action it answers.  A call NimBLE refuses is answered at
 * once with its status.  The events go through a FreeRTOS queue of
 * PANEL_BLE_QUEUE_LEN elements of 64 bytes (bleu_evt_t); a notification
 * longer than BLEU_EVT_DATA bytes comes as several, in order.  A
 * notification or an advertising report takes a slot only while more than
 * BLEU_EVQ_RESERVE (8) are free, so the events a link's state hangs on
 * find room (bleu_evt_admit()).  A full queue drops the event and counts
 * it (panel_ble_dropped()); a dropped notification breaks a frame, which
 * the WitMotion parser counts as bad and resyncs.  A dropped DISCONNECT is
 * kept and handed out once the queue is empty, so the link still ends and
 * reconnects.  One task reads the queue (panel_ble_get()).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bleuart.h"
#include "esp_err.h"

/** @brief How long the start waits for the host's sync after the host task
 *         starts, ms. */
#define PANEL_BLE_SYNC_MS 3000u

/** @brief Events the queue holds. */
#define PANEL_BLE_QUEUE_LEN 32u

/** @brief Where the start is. */
typedef enum {
    PANEL_BLE_OFF = 0,  /**< panel_ble_start() not called */
    PANEL_BLE_STARTING, /**< the start task runs */
    PANEL_BLE_NO_LINK,  /**< the C6's link failed: nothing until a restart */
    PANEL_BLE_NO_SYNC,  /**< the host runs and has no sync with the C6 */
    PANEL_BLE_READY,    /**< synced: scans and connects can start */
} panel_ble_state_t;

_Static_assert(sizeof(bleu_evt_t) == 64u, "64-byte events");

/** @brief A report of a console scan, on the host task: BLEU_EV_ADV for
 *         each report, BLEU_EV_SCAN_END at its end. */
typedef void (*panel_ble_report_fn)(const bleu_evt_t *e);

/**
 * @brief Start Bluetooth LE: the queue and the `blestart` task, once.  Later
 *        calls do nothing.  Callable from any task; returns at once.
 */
void panel_ble_start(void);

/**
 * @brief Where the start is.
 * @return The state; callable from any task.
 */
panel_ble_state_t panel_ble_state(void);

/**
 * @brief The next event, without waiting.
 * @param out Receives it.
 * @return true when @p out holds one; false with the queue empty or before
 *         panel_ble_start().
 */
bool panel_ble_get(bleu_evt_t *out);

/**
 * @brief Perform one action of the IMU's link (bleuart.h): a scan, a
 *        connect, a GATT step, a write.  Its answer comes as an event with
 *        its token; a call NimBLE refuses is answered at once with the
 *        refusal's status.  BLEU_ACT_CANCEL, BLEU_ACT_DISCONNECT and
 *        BLEU_ACT_SCAN_STOP are answered by NimBLE's events or not at all.
 * @param a The action; NULL does nothing.
 */
void panel_ble_do(const bleu_act_t *a);

/**
 * @brief Events dropped on a full queue since start-up.
 * @return The count.
 */
uint32_t panel_ble_dropped(void);

/**
 * @brief A console scan: active, for @p ms, the controller's duplicate
 *        filter on; @p report gets each report and the end, on the host
 *        task, and nothing goes to the queue.
 * @param ms     The scan's length, 1 to 180,000ms.
 * @param report Takes the reports; not NULL.
 * @return ESP_OK when the scan started; ESP_ERR_INVALID_STATE before the
 *         sync or with a scan or connect under way; ESP_ERR_INVALID_ARG for
 *         a length out of range; ESP_FAIL when NimBLE refused it.
 */
esp_err_t panel_ble_scan(uint32_t ms, panel_ble_report_fn report);

/**
 * @brief The C6's controller init and enable requests again, on the
 *        caller's task: each answered or ended by esp_hosted after 5s, both
 *        logged with their time.
 * @return ESP_OK when they went; ESP_ERR_INVALID_STATE before
 *         panel_ble_start() or while its start task runs.
 */
esp_err_t panel_ble_controller(void);
