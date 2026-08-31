/*
 * SPDX-License-Identifier: MIT
 */

#include "link_crc.h"

/*
 * Bitwise rather than table-driven, deliberately.  A frame caps at 64 bytes --
 * 512 iterations, a few microseconds on either processor -- against a 512-byte
 * table that would have to be identical in two firmwares and a test binary.
 * The only place the size argument would bite is a firmware image, and that
 * transfer is bounded by the wire, not by this loop.
 */
uint16_t link_crc(uint16_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;

    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) ? (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}
