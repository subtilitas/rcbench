/**
 * @file
 * @brief The XL2515 over SPI (Serial Peripheral Interface): the part of the
 * CAN (Controller Area Network) driver that touches hardware.
 *
 * Everything that can be computed rather than poked (the bit timing, the
 * identifier's layout across four registers, the register map) lives in
 * shared/can with tests.  This file holds chip-select edges and the register
 * writes in the order the datasheet gives them.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_XL2515_H
#define RCBENCH_XL2515_H

#include <stdbool.h>
#include <stdint.h>

#include "link_can.h"

/**
 * @brief Reset, set the bit timing for @p bitrate, and enter normal mode.
 *
 * False if the part does not answer.  The datasheet guarantees configuration
 * mode after a reset, so a CANSTAT that says otherwise means nothing is
 * listening on the SPI bus: an SPI wiring fault, not a CAN wiring fault.
 *
 * @param bitrate  CAN bit rate in bit/s.
 * @return True when the controller is in normal mode. False when the part
 *         does not answer, or the crystal cannot make @p bitrate.
 */
bool xl2515_init(uint32_t bitrate);

/**
 * @brief Queue one frame in TXB0 and request transmission.
 *
 * False if the buffer is still busy with the previous one.
 *
 * @param f  The frame to send. NULL is refused, and so is a DLC above 8.
 * @return True when the frame is queued. False when the buffer is still busy
 *         with the previous frame, or the identifier cannot be packed.
 */
bool xl2515_send(const link_can_frame_t *f);

/**
 * @brief Take one frame from whichever receive buffer has one.
 *
 * A buffer holding a frame this link does not carry is consumed, adds 1 to
 * @p refused (modulo 2^32, NULL counts nothing) and the other buffer is
 * tried.  False if none holds a valid frame.
 *
 * @param f  Receives the frame.
 * @param refused  Counts the frames this link does not carry. NULL counts
 *                 nothing.
 * @return True when a valid frame was taken. False when no buffer holds one.
 */
bool xl2515_recv(link_can_frame_t *f, uint32_t *refused);

/**
 * @brief The controller's own error counters, for the bring-up report.
 *
 * Either pointer may be NULL.
 *
 * @param tx_errors  Receives the transmit error counter (TEC). May be NULL.
 * @param rx_errors  Receives the receive error counter (REC). May be NULL.
 * @param flags  Receives the error flags register (EFLG). May be NULL.
 */
void xl2515_errors(uint8_t *tx_errors, uint8_t *rx_errors, uint8_t *flags);

/**
 * @brief True if a frame arrived with both receive buffers full since the
 * last call.
 *
 * The flags are sticky and the MCU (microcontroller unit) clears them, which
 * makes this the one record of a frame lost with nothing wrong on the wire:
 * it arrived, it was correct, and there was nowhere to put it.  Reading
 * clears them, so the answer covers the time since the last call.
 *
 * @return True when an overflow was seen since the last call.
 */
bool xl2515_take_overflow(void);

#endif /* RCBENCH_XL2515_H */
