/**
 * @file panel_scan.h
 * @brief The QR scanner on the panel: a GM861S module on UART1, TX IO4
 *        and RX IO5 of header P3 (board_pins.h), run by the task `scan`.
 *
 * The task runs every PANEL_SCAN_PERIOD_MS (20ms) on core 0 at priority 2.
 * Each pass it takes SETUP's values (SET_SCANNER_ENABLE, SET_SCANNER_MODE),
 * reads up to PANEL_SCAN_READ_MAX (64) bytes without waiting, hands them,
 * every framing or parity error and every overflow (scan_port_flush()) to
 * the port (scan_port.h), steps the GM861
 * sequence, re-opens the UART at the rate the sequence asks for, writes its
 * request, and publishes the view.
 *
 * - OFF: UART1 is not installed; IO4 and IO5 are inputs.
 * - ON: 9600 baud 8N1, a receive buffer of 256 bytes, no transmit buffer;
 *   RX has the pull-down on, so a pin with no module reads low (NOT
 *   CONNECTED) and a module idling high is seen.
 * - The code lines wait in the port's queue of 4 for panel_scan_take(); a
 *   full queue drops its oldest line.
 *
 * No scanner is not a fault: no state of the scanner raises an alert or
 * blocks anything.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gm861.h"
#include "scan_line.h"
#include "scan_port.h"
#include "settings.h"

/** @brief The task's period, ms. */
#define PANEL_SCAN_PERIOD_MS 20u
/** @brief Bytes read in one pass at most. */
#define PANEL_SCAN_READ_MAX 64u

/** @brief What the task last published. */
typedef struct {
    bool          on;          /**< SETUP's Scanner row is ON */
    gm861_state_t state;       /**< the sequence's state */
    uint32_t      baud;        /**< the rate UART1 is open at, 0 closed */
    uint16_t      bad_zone;    /**< the zone refused, or GM861_NO_ZONE */
    scan_counts_t counts;      /**< the framer's counts */
    uint32_t      drops;       /**< lines the full queue dropped */
    uint32_t      errors;      /**< bytes with a UART error */
    uint32_t      rev;         /**< counts each publish */
} panel_scan_view_t;

/**
 * @brief Start the task, once, after the settings are loaded.  Later calls
 *        do nothing.
 */
void panel_scan_start(void);

/**
 * @brief SETUP changed a setting: the scanner's two go to the task.
 * @param id The setting.
 */
void panel_scan_settings_changed(setting_id_t id);

/**
 * @brief The oldest code line, for the frame task.
 * @param out Receives it.
 * @return false when none waits.
 */
bool panel_scan_take(scan_event_t *out);

/**
 * @brief The last published view.
 * @param out Receives it.
 */
void panel_scan_view(panel_scan_view_t *out);

/**
 * @brief Zones 0x0000 to 0x0060 as the last pass read them before its first
 *        write (gm861_found()), for the console's dump.
 * @param out GM861_ZONES bytes.
 * @param cut Receives zone 0x00B0 as read.
 */
void panel_scan_found(uint8_t out[GM861_ZONES], uint8_t *cut);

/**
 * @brief Queue @p text as a scanned line (scan_port_inject()): the console
 *        build's `scan <text>`.
 * @param text The text: printable ASCII, 1 to SCAN_LINE_MAX bytes.
 * @return false when refused.
 */
bool panel_scan_inject(const char *text);
