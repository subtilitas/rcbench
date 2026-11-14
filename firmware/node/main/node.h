/**
 * @file node.h
 * @brief The node firmware's parts and how they meet: the role (power or
 *        balance, chosen at build time), Bluetooth LE (Low Energy), the
 *        battery, sleep and the console.
 *
 * Tasks: `node` (node_main.c) runs the once-a-second step: battery,
 * status, console line, sleep policy, and the control writes.  NimBLE's host
 * task serves the GATT (generic attribute profile) server.  `ble_tx` sends
 * the STREAM packets the role queues.  The power role adds `pwr`, its
 * sampler; the balance role adds `bal`, which reads the ADC stream.  The console's REPL (read-eval-print loop) runs in its own task.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rcnode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The console's log tag. */
#define NODE_TAG "node"

/** @brief The Waveshare ESP32-C6-Zero's BOOT key: GPIO9, the strapping pin
 *         the key pulls low; once the node runs, the confirming key of a
 *         firmware update over Bluetooth LE (firmware/devota). */
#define NODE_PIN_BOOT 9

/* ------------------------------------------------------------- the role */

/** @brief What a role does; node_pwr.c or node_bal.c defines node_role. */
typedef struct {
    uint8_t     role;  /**< RCNODE_ROLE_* */
    const char *word;  /**< "pwr" or "bal", for the console */
    /** @brief Start the role's sensor and tasks. */
    void (*start)(void);
    /**
     * @brief Take a control write the role owns (RUN, BLADES, RPM).
     * @param c The write.
     */
    void (*control)(const rcnode_ctl_t *c);
    /**
     * @brief Fill the role's fields of the status: sensor, errors, resets,
     *        and RCNODE_ST_SENSOR and RCNODE_ST_PACK in flags.
     * @param s The status.
     */
    void (*status)(rcnode_status_t *s);
    /**
     * @brief Fill the role's part of the info.
     * @param i The info.
     */
    void (*info)(rcnode_info_t *i);
    /**
     * @brief The role's part of the console's line.
     * @param buf The buffer.
     * @param n Its size.
     */
    void (*line)(char *buf, size_t n);
    /** @brief Whether something holds the node awake: a pack on the XT60.
     *  @return true when it does. */
    bool (*busy)(void);
    /** @brief Put the sensor in its lowest-current state before sleep. */
    void (*sleep)(void);
    /** @brief Whether the node's sleep wakes on the NODE_WAKE_S timer to
     *  check for its work; false: it sleeps until the RESET key. */
    bool timer_wake;
    /** @brief At a timer wake, before anything else is started: the
     *  check for a pack (node_policy_wake() decides).
     *  @param vbus_uv Receives the bus voltage, µV.
     *  @return true when the sensor answered. */
    bool (*wake)(int32_t *vbus_uv);
} node_role_t;

/** @brief The role built into this image. */
extern const node_role_t node_role;

/* ---------------------------------------------------------- Bluetooth */

/**
 * @brief Start NimBLE: the GATT server, the name and the advertising.
 *
 * @param role RCNODE_ROLE_*.
 */
void node_ble_start(uint8_t role);

/**
 * @brief Queue a STREAM packet for notification.  Without a subscriber the
 *        packet is dropped and not counted; a full queue or a stack without
 *        a buffer drops it and counts it in node_ble_dropped().
 *
 * @param b The packet.
 * @param n Its length, RCNODE_PKT_MAX or less.
 */
void node_ble_stream(const uint8_t *b, size_t n);

/**
 * @brief The next STREAM sequence number.
 *
 * @return The number, wrapping at 256.
 */
uint8_t node_ble_stream_seq(void);

/**
 * @brief Publish a status: kept for reads, notified when STATUS is
 *        subscribed.
 *
 * @param s The status; its seq and t_node are written here.
 */
void node_ble_status(rcnode_status_t *s);

/** @brief Whether a central is connected.  @return true when one is. */
bool node_ble_linked(void);

/** @brief Whether STREAM notifications are on.  @return true when they are. */
bool node_ble_subscribed(void);

/** @brief STREAM packets not sent, wrapping.  @return The count. */
uint16_t node_ble_dropped(void);

/** @brief The link's RSSI (received signal strength indication).
 *  @param dbm Receives dBm.  @return true with a link. */
bool node_ble_rssi(int8_t *dbm);

/** @brief The advertised name, "" before the stack is up.  @return The name. */
const char *node_ble_name(void);

/** @brief End the link and stop advertising, before a sleep. */
void node_ble_stop(void);

/**
 * @brief A control write the node itself handles (SLEEP, OFF), or any write
 *        for the role, handed from the host task to the `node` task.
 *
 * @param c The write.
 */
void node_control_post(const rcnode_ctl_t *c);

/* ------------------------------------------------------------ battery */

/** @brief Set up the ADC (analogue-to-digital converter) on the divider's
 *  pin: a one-shot read on the power node (node_bat.c), the calibration of
 *  the stream's battery channel on the balance node (node_bal.c). */
void node_bat_init(void);

/**
 * @brief Read the cell: 16 samples averaged on the power node, the mean of
 *        the stream's battery samples since the last read on the balance
 *        node.
 *
 * @return mV; NODE_BAT_ERR (node_policy.h) when the ADC or its calibration
 *         fails.
 */
uint16_t node_bat_read(void);

/* ---------------------------------------------------------- the node */

/** @brief The console's hold: true keeps the node awake.  @param on The hold. */
void node_stay(bool on);

/** @brief Whether the console holds the node awake.  @return The hold. */
bool node_staying(void);

/** @brief Ask the `node` task to sleep now (the console's `sleep`). */
void node_sleep_now(void);

/** @brief Ask the `node` task to switch off now (the console's `off`). */
void node_off_now(void);

/** @brief The status as last published.  @param s Receives it. */
void node_last_status(rcnode_status_t *s);

/** @brief Start the console's REPL on USB Serial/JTAG. */
void node_console_start(void);

/** @brief The node's µs clock, 64 bits.  @return µs since the start. */
uint64_t node_us(void);

#ifdef __cplusplus
}
#endif
