/*
 * SPDX-License-Identifier: MIT
 */

#include "feetech_pio.h"

#include <stddef.h>

bool feetech_pio_clock(uint32_t sys_hz, uint32_t baud,
                       feetech_pio_clock_t *out)
{
    uint64_t want;     /* the divider in 256ths, rounded to nearest */
    uint64_t got_num;  /* the bit time it gives, as a fraction of the one
                        * asked: got_num / want_num */
    uint64_t want_num;
    uint64_t err;

    if (out == NULL || sys_hz == 0u || baud == 0u) {
        return false;
    }
    /* sys_hz / (8 baud), in 256ths. */
    want = ((uint64_t)sys_hz * 256u + (uint64_t)baud * 4u)
           / ((uint64_t)FEETECH_PIO_CYCLES_PER_BIT * baud);
    if (want < 256u || want > (uint64_t)65535u * 256u) {
        return false;
    }
    /* The bit time the divider gives over the one asked:
     * (want / 256) * 8 / sys_hz  against  1 / baud. */
    got_num = want * FEETECH_PIO_CYCLES_PER_BIT * baud;
    want_num = (uint64_t)sys_hz * 256u;
    err = got_num > want_num ? got_num - want_num : want_num - got_num;
    out->sys_hz = sys_hz;
    out->baud = baud;
    out->div_int = (uint16_t)(want / 256u);
    out->div_frac = (uint8_t)(want % 256u);
    out->error_ppm = (uint32_t)((err * 1000000u + want_num - 1u) / want_num);
    return out->error_ppm <= FEETECH_PIO_MAX_ERROR_PPM;
}

unsigned feetech_pio_tx(const uint8_t *bytes, unsigned n, uint32_t *words)
{
    if (bytes == NULL || words == NULL || n == 0u || n > FEETECH_FRAME_MAX) {
        return 0;
    }
    words[0] = n - 1u;
    for (unsigned i = 0; i < n; ++i) {
        words[1u + i] = bytes[i];
    }
    return n + 1u;
}

bool feetech_pio_rx(uint32_t word, uint8_t *byte)
{
    if (byte != NULL) {
        *byte = (uint8_t)((word >> 23) & 0xFFu);
    }
    return (word & 0x80000000u) != 0u;
}

uint32_t feetech_pio_tx_us(const feetech_pio_clock_t *clock, unsigned n)
{
    uint64_t cycles;
    uint64_t div256;

    if (clock == NULL || clock->sys_hz == 0u || n == 0u) {
        return 0;
    }
    /* Start bit, 8 data bits and the stop bit, and the gap between two
     * bytes. */
    cycles = FEETECH_PIO_LEAD_CYCLES
             + (uint64_t)n * FEETECH_BITS_PER_BYTE * FEETECH_PIO_CYCLES_PER_BIT
             + (uint64_t)(n - 1u) * FEETECH_PIO_GAP_CYCLES;
    div256 = (uint64_t)clock->div_int * 256u + clock->div_frac;
    return (uint32_t)((cycles * div256 * 1000000u
                       + (uint64_t)clock->sys_hz * 256u - 1u)
                      / ((uint64_t)clock->sys_hz * 256u));
}
