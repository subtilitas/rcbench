/**
 * @file
 * @brief The XL2515 over SPI (Serial Peripheral Interface): the part of the CAN
 *        (Controller Area Network) driver that touches hardware.
 *
 * Everything that can be computed rather than poked (the bit timing, the
 * identifier's layout across four registers, the register map) lives in
 * shared/can with tests. The XL2515 is an MCP2515-compatible controller.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_XL2515_H
#define RCBENCH_XL2515_H

#include <stdbool.h>
#include <stdint.h>

#include "link_can.h"

/**
 * @brief Resets the controller, sets the bit timing for @p bitrate, and enters
 *        normal mode.
 *
 * The datasheet guarantees configuration mode after a reset, so a CANSTAT that
 * says otherwise means nothing is listening on the SPI bus: an SPI wiring
 * fault, not a CAN wiring fault.
 *
 * @param bitrate  CAN bit rate in bit/s. The crystal must make this rate; see
 *                 can_timing.
 * @return True when the part answers and is in normal mode. False when the
 *         part does not answer, or the crystal cannot make @p bitrate.
 */
bool xl2515_init(uint32_t bitrate);

/**
 * @brief Queues one frame in TXB0 and requests its transmission.
 *
 * @param f  The frame to send. A DLC above 8 is refused.
 * @return True when the frame is queued. False when the buffer is still busy
 *         with the previous frame, or @p f is NULL or invalid.
 */
bool xl2515_send(const link_can_frame_t *f);

/**
 * @brief Takes one frame from whichever receive buffer has one.
 *
 * A buffer holding a frame this link does not carry is consumed, and the other
 * buffer is tried.
 *
 * @param f        Receives the frame.
 * @param refused  Incremented by 1 for each consumed frame this link does not
 *                 carry (modulo 2^32). NULL counts nothing.
 * @return True when a valid frame was taken. False when no buffer holds one.
 */
bool xl2515_recv(link_can_frame_t *f, uint32_t *refused);

/**
 * @brief Reads the controller's own error counters, for the bring-up report.
 *
 * @param tx_errors  Receives the transmit error counter (TEC). May be NULL.
 * @param rx_errors  Receives the receive error counter (REC). May be NULL.
 * @param flags      Receives the error flags register (EFLG). May be NULL.
 */
void xl2515_errors(uint8_t *tx_errors, uint8_t *rx_errors, uint8_t *flags);

/**
 * @brief Tells whether a frame arrived with both receive buffers full since
 *        the last call.
 *
 * The flags are sticky and the MCU (microcontroller unit) clears them, which
 * makes this the one record of a frame lost with nothing wrong on the wire:
 * it arrived, it was correct, and there was nowhere to put it. Reading clears
 * them, so the answer covers the time since the last call.
 *
 * @return True when an overflow happened since the last call.
 */
bool xl2515_take_overflow(void);

#endif /* RCBENCH_XL2515_H */
