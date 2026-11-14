/**
 * @file panel_blec.h
 * @brief The Bluetooth LE clients on the panel: one task, `blec`, that
 *        runs every client's link through the hub (bleuart_hub.h) on the
 *        one NimBLE host (panel_ble.h).
 *
 * Every BLEC_POLL_MS (5ms) the task takes, in this order: each client's
 * values and commands from the frame task; the host stack's events
 * (panel_ble_get()), each routed by the hub to its client; the hub's
 * timers; each client's actions, each through the hub to panel_ble_do();
 * each client's view, published for the frame task.  A client is a
 * bleuart machine with its owner in its tokens (bleu_setup()).
 *
 * The clients and their owners:
 *
 * | Owner | Client | Code |
 * | --- | --- | --- |
 * | 0 | the Bluetooth IMU | panel_imu.c |
 * | 1 | the M5Stack Dial | panel_dial.c |
 * | 3 | the balance node | panel_bal.c |
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

/** @brief The Bluetooth IMU's owner: 0, the one bleu_init() gives. */
#define PANEL_BLEC_IMU 0u
/** @brief The M5Stack Dial's owner (dial_ctl.h, DIAL_BLE_OWNER). */
#define PANEL_BLEC_DIAL 1u
/** @brief The balance node's owner (bal_ctl.h, BAL_BLE_OWNER); 2 is left
 *         for the power node. */
#define PANEL_BLEC_BAL 3u

/**
 * @brief Start the task, once, after every client's start function.  Later
 *        calls do nothing.
 */
void panel_blec_start(void);
