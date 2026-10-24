/**
 * @file can_twai.h
 * @brief The panel's CAN (Controller Area Network) driver.
 *
 * On the ESP32-P4's TWAI
 * (Two-Wire Automotive Interface) controller 0, GPIO22 TX and GPIO21 RX
 * (board_pins.h), clocked from the 40MHz crystal.  The bit timing comes
 * from shared/can, which is host-tested; this file holds the peripheral's
 * configuration and the send and receive calls.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_CAN_TWAI_H
#define RCBENCH_CAN_TWAI_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "link_can.h"

/**
 * @brief Start TWAI at a bit rate made exactly from the 40MHz crystal.
 * @param bitrate Bits per second; PANEL_CAN_BITRATE on the link.
 * @return ESP_OK; ESP_ERR_INVALID_ARG for a rate the crystal does not
 *         make exactly; otherwise the TWAI driver's error, with the driver
 *         uninstalled again.
 */
esp_err_t can_twai_start(uint32_t bitrate);

/** @brief Stop TWAI and uninstall its driver.  Does nothing when it is
 *         not running. */
void can_twai_stop(void);

/** Queue one frame. @p timeout_ms of 0 does not block. */
bool can_twai_send(const link_can_frame_t *f, uint32_t timeout_ms);

/** Take one frame, if one arrives within @p timeout_ms. */
bool can_twai_recv(link_can_frame_t *f, uint32_t timeout_ms);

/** What the controller is doing, as can_twai_recover() found it. */
typedef enum {
    CAN_TWAI_UNKNOWN = 0, /**< not started, or the status would not read */
    CAN_TWAI_RUNNING,     /**< on the bus */
    CAN_TWAI_RECOVERING,  /**< counting the bus-free signals before stopping */
    CAN_TWAI_STOPPED,     /**< idle, and would not start */
    CAN_TWAI_BUS_OFF,     /**< off the bus, and recovery would not begin */
} can_twai_health_t;

/**
 * Look at the controller and put it back on the bus if it has fallen off.
 *
 * A transmitter nobody answers reaches the bus-off threshold in about four
 * milliseconds at 1Mbit/s, and this peripheral does not recover on its own:
 * without this call every later transmit fails and the link is gone until the
 * panel is power-cycled. Call it while the link is down; it takes one step
 * per call and never blocks.
 */
can_twai_health_t can_twai_recover(void);

/**
 * The same reading without acting on it.
 *
 * For anything that reports the controller's state rather than repairing it,
 * so a diagnosis does not start a recovery as a side effect of being looked
 * at.
 */
can_twai_health_t can_twai_health(void);

/**
 * Bus error counters, for the report. Any pointer may be NULL.
 *
 * False when the controller is not running or its status would not read; the
 * outputs are then left as the caller set them, because zeros would read as a
 * healthy bus.
 */
bool can_twai_errors(uint32_t *tx_err, uint32_t *rx_err, uint32_t *bus_err,
                     bool *bus_off);

#endif /* RCBENCH_CAN_TWAI_H */
