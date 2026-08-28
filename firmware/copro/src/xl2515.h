/*
 * The XL2515 over SPI: the part of the CAN driver that touches hardware.
 *
 * Deliberately thin.  Everything that can be computed rather than poked --
 * the bit timing, the identifier's layout across four registers, the register
 * map itself -- lives in shared/can with tests, so what is left here is
 * chip-select edges and a handful of register writes in the order the
 * datasheet gives them.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_XL2515_H
#define RCBENCH_XL2515_H

#include <stdbool.h>
#include <stdint.h>

#include "link_can.h"

/**
 * Reset, set the bit timing for @p bitrate, and enter normal mode.
 *
 * False if the part does not answer, which on a fresh board almost always
 * means the SPI wiring rather than the CAN wiring -- and the two are worth
 * telling apart before anything is scoped. The check is real: the datasheet
 * guarantees the part is in configuration mode after a reset, so a CANSTAT
 * that says otherwise means nothing is listening on the bus this driver
 * thinks it has.
 */
bool xl2515_init(uint32_t bitrate);

/** Queue one frame in TXB0 and request transmission. False if the buffer is
 *  still busy with the previous one. */
bool xl2515_send(const link_can_frame_t *f);

/** Take one frame from whichever receive buffer has one. False if none. */
bool xl2515_recv(link_can_frame_t *f);

/** The controller's own error counters, for the bring-up report. Either
 *  pointer may be NULL. */
void xl2515_errors(uint8_t *tx_errors, uint8_t *rx_errors, uint8_t *flags);

#endif /* RCBENCH_XL2515_H */
