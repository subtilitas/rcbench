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

/** Bus error counters, for the report. Any pointer may be NULL. */
void can_twai_errors(uint32_t *tx_err, uint32_t *rx_err, uint32_t *bus_err,
                     bool *bus_off);

#endif /* RCBENCH_CAN_TWAI_H */
