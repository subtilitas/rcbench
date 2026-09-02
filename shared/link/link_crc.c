/*
 * SPDX-License-Identifier: MIT
 */

#include "link_crc.h"

/*
 * Bitwise rather than table-driven: 8 iterations per byte, a few
 * microseconds for a 64-byte frame on either processor, against a 512-byte
 * table that would have to be identical in two firmwares and the test
 * binary.
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
