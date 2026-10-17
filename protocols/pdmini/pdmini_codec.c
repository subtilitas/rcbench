/*
 * The PD mini's codec: the CRC8 (cyclic redundancy check, 8 bits) of a
 * frame and the framing of a reply.  See pdmini.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pdmini.h"

uint8_t pdmini_crc8(const uint8_t *p, size_t n)
{
    uint8_t crc = 0xFFu;
    for (size_t i = 0; p != NULL && i < n; ++i) {
        crc = (uint8_t)(crc ^ p[i]);
        for (int b = 0; b < 8; ++b) {
            if ((crc & 0x80u) != 0u) {
                crc = (uint8_t)(((unsigned)crc << 1) ^ 0x31u);
            } else {
                crc = (uint8_t)((unsigned)crc << 1);
            }
        }
    }
    return crc;
}

/* A fixed-length reply's whole length, or 0 for one that is not. */
static size_t fixed_len(uint8_t cmd)
{
    switch (cmd) {
    case PDMINI_READ_STATE:
    case PDMINI_READ_ID:
        return 3u;
    case PDMINI_READ_DISPLAY:
        return 6u;
    case PDMINI_READ_DATA:
    case PDMINI_READ_INPUT:
        return 7u;
    default:
        return 0u;
    }
}

pdmini_frame_t pdmini_reply_frame(uint8_t cmd, const uint8_t *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return PDMINI_MORE;
    }
    if (buf[0] != cmd) {
        return PDMINI_BAD;
    }
    size_t len = fixed_len(cmd);
    if (len == 0u) {
        if (cmd != PDMINI_WHO_AM_I) {
            return PDMINI_BAD;   /* nothing else this driver reads */
        }
        if (n < 2u) {
            return PDMINI_MORE;
        }
        len = 3u + (size_t)buf[1];   /* command, length, text, end */
    }
    if (n < len) {
        return PDMINI_MORE;
    }
    if (n > len) {
        return PDMINI_BAD;
    }
    const uint8_t last = buf[len - 1u];
    if (last == pdmini_crc8(buf, len - 1u)) {
        return PDMINI_DONE;
    }
    /* WHO_AM_I's reply ends in 0x0A by the vendor's sheet, unchecked on the
     * bench: either is taken. */
    if (cmd == PDMINI_WHO_AM_I && last == 0x0Au) {
        return PDMINI_DONE;
    }
    return PDMINI_BAD;
}
