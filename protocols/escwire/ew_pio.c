/**
 * @file ew_pio.c
 * @brief The arithmetic of the PIO (programmable input/output) pin driver; see
 *        ew_pio.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_pio.h"

/** @brief Where a received word's upper half keeps the data. */
#define RX_DATA_SHIFT 7u
/** @brief Where a received word's upper half keeps the stop bit. */
#define RX_STOP_SHIFT 15u

_Static_assert(EW_PIO_RX_WORDS >= EW_RX_MAX, "a reply of protocol A must fit");

/**
 * @brief The rate as a fraction of the system clock: a bit is @p num / @p den
 *        seconds over the cycles of one bit.
 * @param rate an ew_rate_t, known to be valid
 * @param num  takes the numerator
 * @param den  takes the denominator
 */
static void rate_fraction(uint8_t rate, uint64_t *num, uint64_t *den)
{
    if (rate == (uint8_t)EW_RATE_A) {
        *num = EW_BIT_A_NS;
        *den = 1000000000ull * EW_PIO_CYCLES_PER_BIT;
    } else {
        *num = 1u;
        *den = (uint64_t)EW_PIO_TEXT_BAUD * EW_PIO_CYCLES_PER_BIT;
    }
}

bool ew_pio_clock(uint32_t sys_hz, uint8_t rate, ew_pio_clock_t *out)
{
    uint64_t num;
    uint64_t den;
    uint64_t want;
    uint64_t got;
    uint64_t div256;
    uint64_t err;

    if (out == NULL || sys_hz == 0u || rate >= (uint8_t)EW_RATES) {
        return false;
    }
    rate_fraction(rate, &num, &den);
    want = (uint64_t)sys_hz * 256u * num;
    div256 = (want + den / 2u) / den;
    got = div256 * den;
    err = got > want ? got - want : want - got;
    if (div256 < (uint64_t)EW_PIO_MIN_DIV * 256u) {
        return false;
    }
    out->sys_hz = sys_hz;
    out->div256 = (uint32_t)div256;
    out->err_ppm = (uint32_t)((err * 1000000u + want - 1u) / want);
    return out->err_ppm <= EW_PIO_MAX_ERROR_PPM;
}

uint32_t ew_pio_bit_ns(const ew_pio_clock_t *clock)
{
    /* 16 cycles of div256 / 256 system clock cycles each. */
    return (uint32_t)(((uint64_t)clock->div256
                       * (EW_PIO_CYCLES_PER_BIT * 1000000000ull / 256u)
                       + clock->sys_hz / 2u) / clock->sys_hz);
}

uint32_t ew_pio_ticks(const ew_pio_clock_t *clock, uint32_t us)
{
    const uint64_t den = (uint64_t)clock->div256 * EW_PIO_TICK_CYCLES * 1000000u;
    const uint64_t num = (uint64_t)us * clock->sys_hz * 256u;

    return (uint32_t)((num + den - 1u) / den);
}

/**
 * @brief Put bits at the end of the stream.
 * @param words the stream
 * @param at    bits already in it; moves on
 * @param value the bits, least significant first
 * @param count how many, up to 10
 */
static void put_bits(uint32_t *words, uint32_t *at, uint32_t value,
                     uint32_t count)
{
    const uint32_t w = *at / 32u;
    const uint32_t off = *at % 32u;

    words[w] |= value << off;
    if (off + count > 32u) {
        words[w + 1u] = value >> (32u - off);
    }
    *at += count;
}

uint32_t ew_pio_stream(const ew_pio_clock_t *clock, const ew_xfer_t *x,
                       uint32_t *words, ew_pio_run_t *run)
{
    uint32_t *bits;
    uint32_t count;
    uint32_t at = 0u;
    uint32_t n_bit_words;

    if (clock == NULL || words == NULL || run == NULL || !ew_xfer_valid(x)) {
        return 0u;
    }
    count = (uint32_t)x->n_tx * 10u + x->pause_bits;
    n_bit_words = (count + 31u) / 32u;
    bits = &words[2];
    for (uint32_t i = 0u; i < n_bit_words; ++i) {
        bits[i] = 0u;
    }
    for (uint32_t i = 0u; i < x->n_tx; ++i) {
        if (i == x->n_head && x->pause_bits != 0u) {
            put_bits(bits, &at, (1u << x->pause_bits) - 1u, x->pause_bits);
        }
        /* The start bit 0, the data, the stop bit 1. */
        put_bits(bits, &at, ((uint32_t)x->tx[i] << 1) | 0x200u, 10u);
    }
    run->first_ticks = x->max_rx != 0u ? ew_pio_ticks(clock, x->first_us) : 0u;
    run->gap_ticks = x->max_rx != 0u ? ew_pio_ticks(clock, x->gap_us)
                                           + EW_PIO_HALF_BIT_TICKS
                                     : 0u;
    run->span_us = ew_xfer_span_us(x);
    run->max_rx = x->max_rx;
    run->rate = x->rate;
    run->rx = x->rx;
    run->n_words = n_bit_words + 3u;
    words[0] = count - 1u;
    words[1] = run->first_ticks;
    words[n_bit_words + 2u] = run->gap_ticks;
    return run->n_words;
}

bool ew_pio_late(const ew_pio_run_t *run, uint32_t now, uint32_t t0)
{
    const uint64_t limit = (uint64_t)run->span_us + EW_PIO_GRACE_US;

    return (uint64_t)ew_elapsed_us(now, t0) > limit;
}

void ew_pio_capture(const ew_pio_run_t *run, const uint16_t *words, uint32_t n,
                    bool stalled, bool cut, ew_capture_t *out)
{
    const uint32_t count = n < run->max_rx ? n : run->max_rx;

    out->n = (uint16_t)count;
    out->bad_stop = 0u;
    out->aborted = cut;
    out->overflow = stalled && count < run->max_rx;
    for (uint32_t i = 0u; i < count; ++i) {
        const uint16_t w = words[i];

        run->rx[i] = (uint8_t)(w >> RX_DATA_SHIFT);
        if (((w >> RX_STOP_SHIFT) & 1u) == 0u && out->bad_stop == 0u) {
            out->bad_stop = (uint16_t)(i + 1u);
        }
    }
}
