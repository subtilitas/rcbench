/*
 * CRC-16 (cyclic redundancy check): polynomial 0x1021, no reflection, no
 * final XOR, seed passed by the caller.
 *
 * With the seed 0xFFFF this is CRC-16/CCITT-FALSE, whose published check
 * value is 0x29B1 over the ASCII string "123456789".  With the seed 0x0000 it
 * is CRC-16/XMODEM, which the OpenYGE protocol uses (check value 0x31C3).
 *
 * The panel link carries no CRC of its own: CAN (Controller Area Network)
 * provides a 15-bit CRC, an acknowledge slot and retransmission in silicon.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_CRC_H
#define RCBENCH_LINK_CRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The value a fresh accumulator starts at. */
#define LINK_CRC_INIT 0xFFFFu

/* The published check value: link_crc(LINK_CRC_INIT, "123456789", 9). */
#define LINK_CRC_CHECK 0x29B1u

/*
 * Fold `len` bytes into `crc` and return the new accumulator.  Incremental,
 * so a receiver can check a frame as it arrives without buffering it first.
 */
uint16_t link_crc(uint16_t crc, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_CRC_H */
