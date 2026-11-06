/**
 * @file
 * @brief The Bluetooth LE (BLE) link of the IMU: the central of
 *        protocols/bleuart for the BWT901CL's ISSC Transparent UART
 *        service, as the controller (imu_ctl.h) joins it to the sensor side
 *        (imu_sensor.h).  Pure C: the panel (firmware/panel/main/panel_ble.c)
 *        runs the NimBLE host on the board's ESP32-C6, performs the
 *        actions and hands in the events.
 *
 * The link runs while IMU is ON and the panel's Bluetooth is synced
 * (IMU_RADIO_READY).  A bound sensor is connected at once and again
 * BLEU_RETRY_MS after a failure or a drop.  SCAN lists for BLEU_SCAN_MS
 * the devices that advertise the 16-bit UUID 0x18F0 or the ISSC service;
 * the others are counted.  A tap on a listed sensor binds it at once: no
 * pairing, no PIN.  The link converts the scan's devices, its errors and
 * the C6's version into the view's types (imu_screen.h).
 *
 * For the sensor side the link is up once 1E4D's notifications are
 * subscribed; a 5-byte command can go while up, no write is in flight and
 * 6DAA's read is done.  A command write that fails drops the commands the
 * sensor side holds (imu_ble_write_failed()).
 *
 * Every function of one link is called from one task.  Times are
 * unsigned differences of a free-running 32-bit ms clock.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bleuart.h"
#include "imu_screen.h"
#include "witmotion.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Where the panel's Bluetooth stands (panel_ble.h's states). */
typedef enum {
    IMU_RADIO_OFF = 0,  /**< not started */
    IMU_RADIO_STARTING, /**< the start task runs */
    IMU_RADIO_NO_LINK,  /**< the C6's link failed: until a restart */
    IMU_RADIO_NO_SYNC,  /**< the host has no sync with the C6 */
    IMU_RADIO_READY,    /**< synced */
} imu_radio_t;

/** @brief A bind: what SETUP keeps. */
typedef struct {
    char addr[IMU_ADDR_MAX];  /**< "00:0C:BF:08:19:29 P" (bleu_addr_format) */
    char name[IMU_NAME_MAX];  /**< its name from the scan, "" none */
} imu_bound_t;

/** @brief The BLE link.  Its members are private but for the tests. */
typedef struct {
    bleu_t      b;              /**< the central */
    bool        en;             /**< IMU ON as last set */
    imu_radio_t radio;          /**< the panel's Bluetooth */
    char        version[IMU_VERSION_MAX]; /**< the C6's, "" unknown */
    bool        scan_open;      /**< a scan started, its end unread */
    imu_dev_t   found[IMU_FOUND_MAX]; /**< the last scan's sensors */
    uint8_t     found_n;        /**< their count */
    uint16_t    hidden;         /**< other devices the last scan heard */
    imu_err_t   op_err;         /**< the last scan's or write's error */
    uint16_t    op_code;        /**< its status */
    bleu_wr_t   wr_was;         /**< b.wr as last seen */
    bool        wr_failed;      /**< a write failed, unread */
} imu_ble_t;

/** @brief Clear: OFF, nothing bound.  @param l The link, not NULL. */
void imu_ble_init(imu_ble_t *l);

/**
 * @brief The panel's Bluetooth state and the C6's version.  The central
 *        runs while IMU is ON and the state is IMU_RADIO_READY.
 * @param l       The link, not NULL.
 * @param radio   The state.
 * @param version The C6's version as text, "" or NULL unknown.
 * @param now_ms  The clock.
 */
void imu_ble_radio(imu_ble_t *l, imu_radio_t radio, const char *version,
                   uint32_t now_ms);

/**
 * @brief IMU ON or OFF.  OFF stops a scan, cancels a connect, ends a link
 *        and forgets the list and the errors; the bound address stays.
 * @param l      The link, not NULL.
 * @param on     IMU ON.
 * @param now_ms The clock.
 */
void imu_ble_on(imu_ble_t *l, bool on, uint32_t now_ms);

/**
 * @brief The address SETUP keeps: bind it, or unbind for "" or a text
 *        bleu_addr_parse() does not take.
 * @param l      The link, not NULL.
 * @param text   "00:0C:BF:08:19:29 P" or "... R".
 * @param now_ms The clock.
 * @return Whether an address is bound now.
 */
bool imu_ble_set_addr(imu_ble_t *l, const char *text, uint32_t now_ms);

/** @brief Whether an address is bound.  @param l The link, not NULL.
 *  @return Whether it is. */
bool imu_ble_bound(const imu_ble_t *l);

/** @brief Whether SCAN may start: ON, synced, no scan running.
 *  @param l The link, not NULL.  @return Whether it may. */
bool imu_ble_ready(const imu_ble_t *l);

/** @brief SCAN: the list, the hidden count and the last scan's or write's
 *         error cleared, then a scan of BLEU_SCAN_MS.
 *  @param l The link, not NULL.  @param now_ms The clock.
 *  @return Whether it started. */
bool imu_ble_scan(imu_ble_t *l, uint32_t now_ms);

/**
 * @brief Bind sensor @p i of the last scan, at once.
 * @param l      The link, not NULL.
 * @param i      Its index; one past the list is refused.
 * @param now_ms The clock.
 * @param out    Takes the address with its type and the name.
 * @return Whether it bound.
 */
bool imu_ble_bind(imu_ble_t *l, uint8_t i, uint32_t now_ms,
                  imu_bound_t *out);

/**
 * @brief One event of the host stack.
 * @param l      The link, not NULL.
 * @param e      The event, not NULL.
 * @param now_ms The clock.
 * @return true when @p e carries the sensor's stream bytes, data[0] to
 *         data[len - 1].
 */
bool imu_ble_event(imu_ble_t *l, const bleu_evt_t *e, uint32_t now_ms);

/**
 * @brief The central's next action.
 * @param l      The link, not NULL.
 * @param now_ms The clock.
 * @param out    Takes the action.
 * @return true when @p out holds one.
 */
bool imu_ble_act(imu_ble_t *l, uint32_t now_ms, bleu_act_t *out);

/**
 * @brief Whether a command write failed since the last call; once.
 * @param l The link, not NULL.
 * @return true once per failure.
 */
bool imu_ble_write_failed(imu_ble_t *l);

/**
 * @brief The link's part of the link state.
 * @param l The link, not NULL.
 * @return STARTING, NO_BT or C6_FAILED from the radio; SCANNING while a
 *         scan runs; READY with no address bound or with the stream
 *         flowing; BOUND for a device that is not a WitMotion sensor;
 *         else CONNECTING.
 */
imu_link_t imu_ble_link(const imu_ble_t *l);

/** @brief Whether the sensor's stream flows: subscribed.
 *  @param l The link, not NULL.  @return Whether it does. */
bool imu_ble_up(const imu_ble_t *l);

/** @brief Whether a sensor command can go now.
 *  @param l The link, not NULL.  @return Whether it can. */
bool imu_ble_can_send(const imu_ble_t *l);

/**
 * @brief Send a sensor command on 8841; its action follows from
 *        imu_ble_act().
 * @param l      The link, not NULL.
 * @param cmd    WIT_CMD_LEN bytes.
 * @param now_ms The clock.
 * @return Whether the central took it.
 */
bool imu_ble_send(imu_ble_t *l, const uint8_t cmd[WIT_CMD_LEN],
                  uint32_t now_ms);

/**
 * @brief The link's part of the view: the C6's version, the last scan's
 *        sensors and hidden count, the error.  A scan's or a write's error
 *        comes before the link's.
 * @param l   The link, not NULL.
 * @param out Takes version, found_n, found, hidden, err, err_code.
 */
void imu_ble_view(const imu_ble_t *l, imu_view_t *out);

#ifdef __cplusplus
}
#endif
