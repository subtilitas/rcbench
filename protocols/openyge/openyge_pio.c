/**
 * @file openyge_pio.c
 * @brief The arithmetic of the PIO (programmable input/output) pin driver of
 *        programming for YGE ESCs; see openyge_pio.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "openyge_pio.h"

/**
 * @brief Cycles from a stream's hand-over to the first bit's `out`, at most: a
 *        byte being received, from the look just before its start bit to the
 *        next look for a stream (OY_PIO_STOP_SAMPLE + 7), the `pull`, the
 *        count, the drive and the first `pull ifempty` (5), and 4 of slack.
 */
#define LEAD_CYCLES 166u
/** @brief Cycles of the release after the last bit time, with one of slack. */
#define TAIL_CYCLES 2u

_Static_assert(OY_PIO_TX_WORDS <= 45u, "the longest stream is 45 words");
_Static_assert(OY_PIO_RX_ENTRIES >= OY_PIO_TX_MAX,
               "the longest frame fits the receive buffer");

bool oy_pio_clock(uint32_t sys_hz, oy_pio_clock_t *out)
{
    const uint64_t den = (uint64_t)OY_PIO_BAUD * OY_PIO_CYCLES_PER_BIT;
    uint64_t want;
    uint64_t got;
    uint64_t div256;
    uint64_t err;
    uint32_t ppm;

    if (out == NULL || sys_hz == 0u) {
        return false;
    }
    want = (uint64_t)sys_hz * 256u;
    div256 = (want + den / 2u) / den;
    got = div256 * den;
    err = got > want ? got - want : want - got;
    ppm = (uint32_t)((err * 1000000u + want - 1u) / want);
    if (div256 < (uint64_t)OY_PIO_MIN_DIV * 256u
        || ppm > OY_PIO_MAX_ERROR_PPM) {
        return false;
    }
    out->sys_hz = sys_hz;
    out->div256 = (uint32_t)div256;
    out->err_ppm = ppm;
    return true;
}

uint32_t oy_pio_bit_ns(const oy_pio_clock_t *clock)
{
    /* 16 cycles of div256 / 256 system clock cycles each. */
    return (uint32_t)(((uint64_t)clock->div256
                       * (OY_PIO_CYCLES_PER_BIT * 1000000000ull / 256u)
                       + clock->sys_hz / 2u) / clock->sys_hz);
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

uint32_t oy_pio_stream(const uint8_t *frame, size_t n, uint32_t *words)
{
    uint32_t *bits;
    uint32_t count;
    uint32_t n_bit_words;
    uint32_t at = 0u;

    if (frame == NULL || words == NULL || n == 0u || n > OY_PIO_TX_MAX) {
        return 0u;
    }
    count = (uint32_t)n * 10u;
    n_bit_words = (count + 31u) / 32u;
    bits = &words[1];
    for (uint32_t i = 0u; i < n_bit_words; ++i) {
        bits[i] = 0u;
    }
    for (size_t i = 0u; i < n; ++i) {
        /* The start bit 0, the data, the stop bit 1. */
        put_bits(bits, &at, ((uint32_t)frame[i] << 1) | 0x200u, 10u);
    }
    words[0] = count - 1u;
    return n_bit_words + 1u;
}

uint32_t oy_pio_span_us(const oy_pio_clock_t *clock, size_t n)
{
    const uint64_t cycles = LEAD_CYCLES + TAIL_CYCLES
                            + (uint64_t)n * 10u * OY_PIO_CYCLES_PER_BIT;
    const uint64_t num = cycles * clock->div256 * 1000000u;
    const uint64_t den = (uint64_t)clock->sys_hz * 256u;

    return (uint32_t)((num + den - 1u) / den);
}

bool oy_pio_late(uint32_t span_us, uint32_t now, uint32_t t0)
{
    /* Unsigned subtraction: the clock's wrap cancels out. */
    return (uint64_t)(uint32_t)(now - t0)
           > (uint64_t)span_us + OY_PIO_GRACE_US;
}

bool oy_pio_sending(unsigned pc)
{
    return pc >= OY_PIO_SEND_PC && pc <= OY_PIO_RELEASE_PC;
}

void oy_pio_rx_reset(oy_pio_rx_t *r)
{
    r->rd = 0u;
    r->bytes = 0u;
    r->framing = 0u;
    r->full = 0u;
    r->filled = false;
}

size_t oy_pio_take(oy_pio_rx_t *r, const uint16_t *ent, uint32_t written,
                   uint8_t *buf, size_t cap)
{
    size_t n = 0u;

    if (written > OY_PIO_RX_ENTRIES) {
        written = OY_PIO_RX_ENTRIES;
    }
    if (written == OY_PIO_RX_ENTRIES && !r->filled) {
        r->filled = true;
        ++r->full;
    }
    while (r->rd < written && n < cap) {
        const uint16_t e = ent[r->rd++];

        buf[n++] = (uint8_t)(e >> OY_PIO_RX_DATA_SHIFT);
        if (((e >> OY_PIO_RX_STOP_SHIFT) & 1u) == 0u) {
            ++r->framing;
        }
    }
    r->bytes += (uint32_t)n;
    return n;
}

uint32_t oy_pio_rearm(oy_pio_rx_t *r, uint32_t written, bool *drop)
{
    if (written > OY_PIO_RX_ENTRIES) {
        written = OY_PIO_RX_ENTRIES;
    }
    *drop = false;
    if (r->rd < written) {
        return written;
    }
    if (written == OY_PIO_RX_ENTRIES) {
        if (!r->filled) {
            ++r->full;
        }
        *drop = true;
    }
    r->rd = 0u;
    r->filled = false;
    return 0u;
}
