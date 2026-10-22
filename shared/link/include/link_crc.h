/**
 * @file
 * @brief CRC-16 (cyclic redundancy check): polynomial 0x1021, no reflection, no
 * final XOR, seed passed by the caller.
 *
 * With the seed 0xFFFF this is CRC-16/CCITT-FALSE, whose published check
 * value is 0x29B1 over the ASCII string "123456789".  With the seed 0x0000 it
 * is CRC-16/XMODEM, which the OpenYGE protocol uses (check value 0x31C3);
 * protocols/openyge carries its own routine and does not call this one.
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

/** @brief The value a fresh accumulator starts at. */
#define LINK_CRC_INIT 0xFFFFu

/**
 * @brief The published check value: link_crc(LINK_CRC_INIT, "123456789", 9).
 */
#define LINK_CRC_CHECK 0x29B1u

/**
 * @brief Fold `len` bytes into `crc` and return the new accumulator.
 *
 * Incremental, so a receiver can check a frame as it arrives without buffering
 * it first.
 *
 * @param crc The accumulator: LINK_CRC_INIT, or the value a previous call
 *       returned.
 * @param data The bytes to fold in.
 * @param len Number of bytes in data.
 * @return The new accumulator.
 */
uint16_t link_crc(uint16_t crc, const void *data, size_t len);

/**
 * @brief The FNV-1a (Fowler-Noll-Vo) 32-bit offset basis, a fresh accumulator.
 */
#define LINK_FNV_INIT 0x811C9DC5uL

/**
 * @brief The published check value: link_fnv1a32(LINK_FNV_INIT, "123456789",
 * 9).
 */
#define LINK_FNV_CHECK 0xBB86B11CuL

/**
 * @brief FNV-1a, 32 bits: per byte, XOR then multiply by 16777619.
 *
 * The content key of the board drawing (LINK_AW_HASH_LO/HI) next to its CRC.
 * Not a cryptographic hash; it tells two drawings apart, it does not
 * authenticate one.  Incremental like link_crc().
 *
 * @param hash The hash: LINK_FNV_INIT, or the value a previous call returned.
 * @param data The bytes to fold in.
 * @param len Number of bytes in data.
 * @return The new hash.
 */
uint32_t link_fnv1a32(uint32_t hash, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_CRC_H */
