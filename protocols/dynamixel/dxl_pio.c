/*
 * The arithmetic of the PIO pin driver for a ROBOTIS Dynamixel bus.  See
 * dxl_pio.h.
 *
 * ROBOTIS and Dynamixel are names or trademarks of their owner; they are
 * used here only to say which servos this module works with.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dxl_pio.h"

#include <string.h>

/* The divider: 1 to 65536 (the register's integer 0), in 1/256 steps. */
#define DIV_MIN_256 256u
#define DIV_MAX_256 (65536u * 256u)

bool dxl_pio_divider(uint32_t sys_hz, uint32_t baud, dxl_pio_div_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (sys_hz == 0u || baud == 0u) {
        return false;
    }
    /* sys_hz x 256 / (8 x baud) = sys_hz x 32 / baud, to the nearest. */
    const uint64_t div256 = ((uint64_t)sys_hz * 32u + baud / 2u) / baud;

    if (div256 < DIV_MIN_256 || div256 > DIV_MAX_256) {
        return false;
    }
    out->div_int = (uint32_t)(div256 >> 8);
    out->div_frac = (uint8_t)(div256 & 0xFFu);

    /* actual = sys_hz x 32 / div256; error = actual / baud - 1. */
    const int64_t num = (int64_t)sys_hz * 32 * 1000000;
    const int64_t den = (int64_t)baud * (int64_t)div256;
    const int64_t diff = num - den * 1000000;
    const int64_t ppm = (diff >= 0 ? diff + den / 2 : diff - den / 2) / den;

    out->error_ppm = (int32_t)ppm;
    out->actual = (uint32_t)(((uint64_t)sys_hz * 32u + div256 / 2u) / div256);
    /* One system clock as parts of a bit: baud / sys_hz, rounded up. */
    out->jitter_ppm = out->div_frac == 0u
                          ? 0u
                          : (uint32_t)(((uint64_t)baud * 1000000u + sys_hz - 1u)
                                       / sys_hz);
    return ppm <= DXL_PIO_ERROR_MAX_PPM && ppm >= -DXL_PIO_ERROR_MAX_PPM;
}

size_t dxl_pio_tx(const uint8_t *packet, size_t n, uint8_t *out, size_t cap)
{
    if (packet == NULL || out == NULL || n == 0u || n > DXL_PIO_TX_MAX
        || n + 1u > cap) {
        return 0;
    }
    out[0] = (uint8_t)(n - 1u);
    memcpy(&out[1], packet, n);
    return n + 1u;
}
