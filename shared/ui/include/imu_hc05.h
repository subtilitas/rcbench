/**
 * @file
 * @brief The HC-05 link of the Bluetooth IMU: the module's AT set-up
 *        (protocols/hc05) on the sensor's UART, as the controller
 *        (imu_ctl.h) joins it to the sensor side (imu_sensor.h).  Pure C:
 *        the panel's driver (firmware/panel/main/panel_imu.c) moves the
 *        bytes, drives KEY and sets the baud.
 *
 * With IMU ON the link probes the HC-05 (AT at 115200, 38400 and 9600
 * baud, then AT+VERSION?) and probes again IMU_PROBE_RETRY_MS after a
 * probe no module answered.  While an AT operation runs the UART's bytes
 * go to the HC-05's machine; at any other time they are the sensor's
 * stream.  SCAN, BIND and CONNECT TO ANY SENSOR start the hc05 operations
 * of the same names with the BWT901CL's PIN (IMU_PIN).  The link converts
 * the module's devices, errors and version into the view's types
 * (imu_screen.h).
 *
 * For the sensor side the link is up while it runs no AT operation and
 * the HC-05 answered; it can send a sensor command while KEY is low as
 * well.
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

#include "hc05.h"
#include "imu_screen.h"
#include "witmotion.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The BWT901CL's PIN (its HC-06 radio's). */
#define IMU_PIN            "1234"
/** @brief The address kept for CONNECT TO ANY SENSOR (AT+CMODE=1). */
#define IMU_ADDR_ANY       "*"
/** @brief A probe again this long after one no module answered, ms. */
#define IMU_PROBE_RETRY_MS 10000u

/** @brief A bind that ended DONE: what SETUP keeps. */
typedef struct {
    char addr[IMU_ADDR_MAX];  /**< the address, IMU_ADDR_ANY for any */
    char name[IMU_NAME_MAX];  /**< its name from the scan, "" none */
} imu_bound_t;

/** @brief The HC-05 link.  Its members are private but for the tests. */
typedef struct {
    hc05_t     bt;             /**< the HC-05's machine */
    bool       op_open;        /**< an operation started, its end unread */
    bool       probed;         /**< the HC-05 answered AT since IMU ON */
    uint32_t   probe_at_ms;    /**< when the next probe goes out */
    imu_err_t  err;            /**< the last operation's error */
    uint8_t    err_code;       /**< its ERROR:(hh) */
    bool       inq_refused;    /**< the last scan ended ERROR:(1F) */
    imu_dev_t  found[IMU_FOUND_MAX]; /**< the last scan's devices */
    uint8_t    found_n;        /**< their count */
} imu_hc05_t;

/** @brief Clear: no probe, nothing found.  @param l The link, not NULL. */
void imu_hc05_init(imu_hc05_t *l);

/** @brief IMU ON: the probe is due at once.
 *  @param l The link, not NULL.  @param now_ms The clock. */
void imu_hc05_on(imu_hc05_t *l, uint32_t now_ms);

/**
 * @brief IMU OFF: a running operation is aborted (KEY goes low once the
 *        machine has fenced its late answers); the probe, the scan, the
 *        offer of CONNECT TO ANY SENSOR and the error are forgotten.
 * @param l The link, not NULL.
 */
void imu_hc05_off(imu_hc05_t *l);

/**
 * @brief Whether an AT operation may start: the HC-05 answered and none
 *        runs.  The caller also holds it back while the sensor side is
 *        not quiet (imu_sensor_quiet()).
 * @param l The link, not NULL.
 * @return Whether one may.
 */
bool imu_hc05_ready(const imu_hc05_t *l);

/** @brief SCAN: the list and the offer are cleared, then the inquiry
 *         starts.  @param l The link, not NULL.  @param now_ms The clock.
 *  @return Whether it started. */
bool imu_hc05_scan(imu_hc05_t *l, uint32_t now_ms);

/**
 * @brief BIND device @p i of the last scan, with IMU_PIN.
 * @param l      The link, not NULL.
 * @param i      Its index; one past the list is refused.
 * @param now_ms The clock.
 * @return Whether it started.
 */
bool imu_hc05_bind(imu_hc05_t *l, uint8_t i, uint32_t now_ms);

/** @brief CONNECT TO ANY SENSOR (AT+CMODE=1), with IMU_PIN.
 *  @param l The link, not NULL.  @param now_ms The clock.
 *  @return Whether it started. */
bool imu_hc05_any(imu_hc05_t *l, uint32_t now_ms);

/**
 * @brief Bytes from the UART, to the HC-05's machine while an operation
 *        runs.
 * @param l The link, not NULL.
 * @param b The bytes.
 * @param n Their count.
 * @return true when the machine took them; false when they are the
 *         sensor's.
 */
bool imu_hc05_take(imu_hc05_t *l, const uint8_t *b, size_t n);

/**
 * @brief The machine's next action: KEY, a baud, or AT bytes to send.
 *        Also when idle: it lowers KEY after an end.
 * @param l      The link, not NULL.
 * @param now_ms The clock.
 * @param out    Takes the action.
 * @return true when @p out holds one.
 */
bool imu_hc05_act(imu_hc05_t *l, uint32_t now_ms, hc05_act_t *out);

/**
 * @brief Read the end of the operation that ran, once.  A probe or any
 *        operation that found no AT drops the probe and sets the next one
 *        IMU_PROBE_RETRY_MS on; a refused inquiry offers CONNECT TO ANY
 *        SENSOR; a scan keeps its devices; a bind or ANY that ended DONE
 *        fills @p bound.
 * @param l      The link, not NULL.
 * @param en     IMU ON: with it off an end is read and nothing taken.
 * @param now_ms The clock.
 * @param bound  Takes the bind's address and name.
 * @return true when @p bound holds a bind.
 */
bool imu_hc05_ended(imu_hc05_t *l, bool en, uint32_t now_ms,
                    imu_bound_t *bound);

/**
 * @brief Start the probe when one is due: IMU ON, not probed, no
 *        operation running, its time come.
 * @param l      The link, not NULL.
 * @param en     IMU ON.
 * @param now_ms The clock.
 * @return Whether it started; its first action comes from
 *         imu_hc05_act().
 */
bool imu_hc05_probe(imu_hc05_t *l, bool en, uint32_t now_ms);

/**
 * @brief The link's part of the link state.
 * @param l The link, not NULL.
 * @return IMU_LINK_SCANNING or IMU_LINK_BINDING while that operation runs,
 *         IMU_LINK_NO_MODULE before the HC-05 answered, else
 *         IMU_LINK_READY: the sensor's stream may flow.
 */
imu_link_t imu_hc05_link(const imu_hc05_t *l);

/**
 * @brief Whether a sensor command can go now: no operation runs and KEY
 *        is low, so the module is in data mode.
 * @param l The link, not NULL.
 * @return Whether it can.
 */
bool imu_hc05_can_send(const imu_hc05_t *l);

/**
 * @brief A sensor command as the action that sends it.
 * @param cmd WIT_CMD_LEN bytes.
 * @param out Takes HC05_ACT_SEND with them.
 */
void imu_hc05_send(const uint8_t cmd[WIT_CMD_LEN], hc05_act_t *out);

/**
 * @brief The link's part of the view, converted: the version once the
 *        HC-05 answered, the last scan's devices, the error and the offer.
 * @param l   The link, not NULL.
 * @param out Takes version, found_n, found, err, err_code, inq_refused.
 */
void imu_hc05_view(const imu_hc05_t *l, imu_view_t *out);

#ifdef __cplusplus
}
#endif
