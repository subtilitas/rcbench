/*
 * The panel's CAN (Controller Area Network) driver, on the ESP32-S3's TWAI
 * (Two-Wire Automotive Interface) controller.  The bit timing comes from
 * shared/can, which is host-tested; this file holds the peripheral's
 * configuration and the send and receive calls.
 *
 * Starting CAN routes the board's multiplexer away from native USB
 * (Universal Serial Bus), because both live on GPIO19 and GPIO20.  That is
 * why it is an explicit call rather than part of board_init().
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
 * Route the board's multiplexer to CAN, then start TWAI at @p bitrate.
 *
 * Native USB is unavailable while it runs (board_pins.h); the console is on
 * UART0 (universal asynchronous receiver-transmitter) for that reason.
 */
esp_err_t can_twai_start(uint32_t bitrate);

/** Stop, and give the multiplexer back to USB. */
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
 * milliseconds at 1 Mbit/s, and this peripheral does not recover on its own:
 * without this call every later transmit fails and the link is gone until the
 * panel is power-cycled. Call it while the link is down; it takes one step
 * per call and never blocks.
 */
can_twai_health_t can_twai_recover(void);

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
