/*
 * The arithmetic of the S.BUS PIO (programmable input/output) pin driver.
 * See sbus_pio.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sbus_pio.h"

#include <string.h>

/* The state machine's clock: 8 cycles per bit at 100 kbaud. */
#define SM_HZ (SBUS_PIO_BAUD * SBUS_PIO_BIT_CYCLES)

/* One bit, in ps. */
#define BIT_PS 10000000u

/* Cycles from the hand-over of a stream to its first bit: the `out` of the
 * count and the pull that feeds it. */
#define TX_START_CYCLES 2u

bool sbus_pio_clock(uint32_t sys_hz, sbus_pio_clock_t *out)
{
    if (out == NULL || sys_hz < SM_HZ) {
        return false;
    }
    /* The divider in 1/256 parts, rounded to nearest: sys_hz x 256 / 800 kHz
     * is sys_hz / 3125. */
    const uint32_t div256 = (uint32_t)(((uint64_t)sys_hz + 1562u) / 3125u);

    /* At most 1,374,390 for a 32-bit clock: the integer part fits 16 bits.
     * One cycle is div256 / 256 / sys_hz s, 3906250000 x div256 / sys_hz
     * ps. */
    const uint64_t num = (uint64_t)div256 * 3906250000u;
    const uint32_t cycle_ps = (uint32_t)((num + sys_hz / 2u) / sys_hz);
    const uint32_t bit_ps = cycle_ps * SBUS_PIO_BIT_CYCLES;
    const uint32_t err = bit_ps > BIT_PS ? bit_ps - BIT_PS : BIT_PS - bit_ps;

    /* ppm of 10 us: err ps x 10^6 / 10^7 ps. */
    if (err / 10u > SBUS_PIO_MAX_ERROR_PPM) {
        return false;
    }
    out->sys_hz = sys_hz;
    out->div_int = (uint16_t)(div256 >> 8);
    out->div_frac = (uint8_t)(div256 & 0xFFu);
    out->cycle_ps = cycle_ps;
    return true;
}

unsigned sbus_pio_parity(uint8_t b)
{
    unsigned p = 0;

    for (unsigned i = 0; i < 8u; ++i) {
        p ^= ((unsigned)b >> i) & 1u;
    }
    return p;
}

/* Cycles of the clock to ns, rounded to nearest. */
static uint32_t cycles_ns(const sbus_pio_clock_t *clock, uint32_t cycles)
{
    return (uint32_t)(((uint64_t)cycles * clock->cycle_ps + 500u) / 1000u);
}

unsigned sbus_pio_tx(const sbus_pio_clock_t *clock, const uint8_t *bytes,
                     unsigned n, uint32_t window_us, uint32_t *words,
                     sbus_pio_run_t *run)
{
    if (clock == NULL || clock->cycle_ps == 0u || bytes == NULL
        || words == NULL || run == NULL || n == 0u
        || n > SBUS_PIO_MAX_BYTES) {
        return 0;
    }
    /* A sample is 2 cycles; the window is rounded up to whole samples. */
    const uint64_t sample_ps = (uint64_t)clock->cycle_ps
                               * SBUS_PIO_SAMPLE_CYCLES;
    const uint64_t samples = ((uint64_t)window_us * 1000000u + sample_ps - 1u)
                             / sample_ps;

    if (samples > SBUS_PIO_MAX_SAMPLES) {
        return 0;
    }
    const unsigned frame_bits = n * SBUS_PIO_BYTE_BITS;
    const unsigned lead_bits = (32u - frame_bits % 32u) % 32u;
    const unsigned total = lead_bits + frame_bits;
    const unsigned n_bit_words = total / 32u;

    memset(words, 0, SBUS_PIO_TX_WORDS * sizeof(words[0]));
    words[0] = total - 1u;
    /* Bit i of the stream is bit i % 32 of word 1 + i / 32: the program
     * shifts right and clocks the least significant bit out first. */
    for (unsigned i = 0; i < lead_bits; ++i) {
        words[1u + i / 32u] |= 1u << (i % 32u);
    }
    for (unsigned b = 0; b < n; ++b) {
        /* start 0, data, parity, stop 1, stop 1 */
        const uint32_t cell = ((uint32_t)bytes[b] << 1)
                              | ((uint32_t)sbus_pio_parity(bytes[b]) << 9)
                              | (3u << 10);
        for (unsigned k = 0; k < SBUS_PIO_BYTE_BITS; ++k) {
            const unsigned i = lead_bits + b * SBUS_PIO_BYTE_BITS + k;

            words[1u + i / 32u] |= ((cell >> k) & 1u) << (i % 32u);
        }
    }
    words[1u + n_bit_words] = (uint32_t)samples;

    run->window_samples = (uint32_t)samples;
    run->window_us = window_us;
    run->lead_ns = cycles_ns(clock, TX_START_CYCLES
                                    + total * SBUS_PIO_BIT_CYCLES);
    return n_bit_words + 2u;
}

void sbus_pio_capture(const sbus_pio_clock_t *clock, const sbus_pio_run_t *run,
                      const uint32_t *words, unsigned n, bool lost,
                      sbus2_capture_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (clock == NULL || run == NULL || (words == NULL && n > 0u)) {
        out->overflow = true;
        return;
    }
    out->overflow = lost || n > SBUS2_CAPTURE_MAX;
    if (n > SBUS2_CAPTURE_MAX) {
        n = SBUS2_CAPTURE_MAX;
    }
    uint32_t prev = 0;

    for (unsigned j = 0; j < n; ++j) {
        const uint32_t w = words[j];
        const uint32_t x = w >> 10;

        /* Every byte counts the counter down by at least 1, and none is
         * read before the window's first sample. */
        if (x > run->window_samples || (j > 0u && x >= prev)) {
            out->overflow = true;
            return;
        }
        prev = x;
        const uint32_t d = run->window_samples - x;
        /* The sample that saw the start bit; the edge lies in the 2 cycles
         * before it, so the middle of them is the estimate. */
        const uint32_t sample = SBUS_PIO_FIRST_SAMPLE
                                + SBUS_PIO_SAMPLE_CYCLES * d
                                + (SBUS_PIO_BYTE_CYCLES
                                   - SBUS_PIO_SAMPLE_CYCLES) * j;
        sbus2_rx_byte_t *b = &out->byte[out->n++];

        b->data = (uint8_t)(w & 0xFFu);
        b->parity_ok = ((w >> 8) & 1u) == sbus_pio_parity(b->data);
        b->stop_ok = ((w >> 9) & 1u) != 0u;
        b->t_ns = cycles_ns(clock, sample - 1u);
    }
}
