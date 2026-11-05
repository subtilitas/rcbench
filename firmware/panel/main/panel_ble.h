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
 *    later).  A C6 firmware without them runs its controller from its own
 *    start; an unanswered or refused request is logged and the start goes
 *    on.
 * 3. nimble_port_init() and the host task `nimble_host`; the host's sync,
 *    its HCI reset answered by the C6, is awaited for PANEL_BLE_SYNC_MS.
 *    Without it the state is PANEL_BLE_NO_SYNC and the host keeps trying;
 *    a sync later makes it PANEL_BLE_READY.
 *
 * The events come from the host task's callbacks through a FreeRTOS queue of
 * PANEL_BLE_QUEUE_LEN elements of 64 bytes.  A full queue drops the event
 * and counts it (panel_ble_dropped()).  One task reads the queue
 * (panel_ble_get()).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief How long the start waits for the host's sync after the host task
 *         starts, ms. */
#define PANEL_BLE_SYNC_MS 3000u

/** @brief Events the queue holds. */
#define PANEL_BLE_QUEUE_LEN 32u

/** @brief Bytes of data an event carries: a legacy advertising report's 31
 *         fit. */
#define PANEL_BLE_DATA_MAX 48u

/** @brief Where the start is. */
typedef enum {
    PANEL_BLE_OFF = 0,  /**< panel_ble_start() not called */
    PANEL_BLE_STARTING, /**< the start task runs */
    PANEL_BLE_NO_LINK,  /**< the C6's link failed: nothing until a restart */
    PANEL_BLE_NO_SYNC,  /**< the host runs and has no sync with the C6 */
    PANEL_BLE_READY,    /**< synced: scans and connects can start */
} panel_ble_state_t;

/** @brief What an event says. */
typedef enum {
    PANEL_BLE_EV_SYNC = 1,    /**< the host synced with the C6 */
    PANEL_BLE_EV_RESET,       /**< the host lost its sync; status: NimBLE's
                                   reason */
    PANEL_BLE_EV_ADV,         /**< an advertising report: address, type,
                                   RSSI, its data */
    PANEL_BLE_EV_SCAN_DONE,   /**< a scan ended; status: NimBLE's reason */
} panel_ble_kind_t;

/** @brief One event, 64 bytes. */
typedef struct {
    uint8_t kind;      /**< panel_ble_kind_t */
    uint8_t addr_type; /**< BLE_ADDR_PUBLIC 0, RANDOM 1, PUBLIC_ID 2,
                            RANDOM_ID 3 */
    int8_t  rssi;      /**< dBm, PANEL_BLE_EV_ADV */
    uint8_t len;       /**< bytes in data */
    int16_t status;    /**< NimBLE's status or reason, 0 for none; for
                            PANEL_BLE_EV_ADV the report's event type
                            (BLE_HCI_ADV_RPT_EVTYPE_*) */
    uint16_t conn;     /**< connection handle; 0xFFFF for none */
    uint16_t handle;   /**< attribute handle; 0 for none */
    uint8_t addr[6];   /**< the peer's address, least significant byte
                            first, as NimBLE gives it */
    uint8_t data[PANEL_BLE_DATA_MAX]; /**< the advertising data */
} panel_ble_evt_t;

_Static_assert(sizeof(panel_ble_evt_t) == 64u, "64-byte events");

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
bool panel_ble_get(panel_ble_evt_t *out);

/**
 * @brief Events dropped on a full queue since start-up.
 * @return The count.
 */
uint32_t panel_ble_dropped(void);

/**
 * @brief An active scan for @p ms with the controller's duplicate filter
 *        on: its reports (PANEL_BLE_EV_ADV), then PANEL_BLE_EV_SCAN_DONE
 *        with reason 0 at its end.
 * @param ms The scan's length, 1 to 180,000ms.
 * @return ESP_OK when the scan started; ESP_ERR_INVALID_STATE before the
 *         sync or with a scan or connect under way; ESP_ERR_INVALID_ARG for
 *         a length out of range; ESP_FAIL when NimBLE refused it.
 */
esp_err_t panel_ble_scan(uint32_t ms);
