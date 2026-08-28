/*
 * The panel's CAN, on the ESP32-S3's TWAI controller.
 *
 * Thin for the same reason the coprocessor's driver is: the bit timing comes
 * from shared/can, which is tested, so what is here is the peripheral's own
 * configuration and two calls.
 *
 * One thing this does that a plain wrapper would not: selecting CAN on the
 * board's multiplexer takes native USB away, because both live on GPIO19/20.
 * That is why it is a deliberate call rather than something board_init does.
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
 * Costs native USB for as long as it is running -- see board_pins.h. The
 * console is on UART for exactly this reason.
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
