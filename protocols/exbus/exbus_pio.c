/*
 * The numbers and words of the EX Bus pin driver.  See exbus_pio.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "exbus_pio.h"

#include <string.h>

#define PS_PER_US 1000000u

bool exbus_pio_clock(uint32_t sys_hz, uint32_t baud, exbus_pio_clock_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (baud == 0u || sys_hz == 0u) {
        return false;
    }
    /* sys_hz / (8 * baud) in 1/256: sys_hz * 32 / baud, rounded. */
    const uint64_t div256 = ((uint64_t)sys_hz * 64u + baud) / (2u * (uint64_t)baud);

    if (div256 < 256u || div256 > 0xFFFFFFu) {
        return false;
    }
    /* The rate it gives, in uHz, against the rate asked for.  Rounded to
     * 1/256 of a divider of at least 1, it is within 0.2 %. */
    const uint64_t got = (uint64_t)sys_hz * 32u * 1000000u / div256;
    const uint64_t want = (uint64_t)baud * 1000000u;
    const uint64_t diff = got > want ? got - want : want - got;
    const uint64_t ppm = diff / baud;

    out->sys_hz = sys_hz;
    out->baud = baud;
    out->div256 = (uint32_t)div256;
    out->error_ppm = (uint32_t)ppm;
    out->cycle_ps = (div256 * 1000000000000u + 128u * (uint64_t)sys_hz)
                    / (256u * (uint64_t)sys_hz);
    return true;
}

uint32_t exbus_pio_cycles_ns(const exbus_pio_clock_t *clock, uint64_t cycles)
{
    if (clock == NULL) {
        return 0;
    }
    return (uint32_t)((cycles * clock->cycle_ps + 500u) / 1000u);
}

uint32_t exbus_pio_window_ticks(const exbus_pio_clock_t *clock,
                                uint32_t window_us)
{
    if (clock == NULL || clock->cycle_ps == 0u) {
        return 0;
    }
    /* Cycles the window needs, rounded up; the release is 2 per tick and
     * EXBUS_PIO_RELEASE_EXTRA more. */
    const uint64_t cycles = ((uint64_t)window_us * PS_PER_US + clock->cycle_ps
                             - 1u) / clock->cycle_ps;
    const uint64_t ticks = cycles <= EXBUS_PIO_RELEASE_EXTRA
        ? 0u
        : (cycles - EXBUS_PIO_RELEASE_EXTRA + 1u) / EXBUS_PIO_TICK_CYCLES;

    return ticks > EXBUS_PIO_WINDOW_MAX ? 0u : (uint32_t)ticks;
}

uint32_t exbus_pio_release_us(const exbus_pio_clock_t *clock,
                              uint32_t window_ticks, uint32_t bytes)
{
    if (clock == NULL) {
        return 0;
    }
    const uint64_t cycles = (uint64_t)window_ticks * EXBUS_PIO_TICK_CYCLES
                            + EXBUS_PIO_RELEASE_EXTRA
                            + (uint64_t)bytes * EXBUS_PIO_BYTE_MAX_CYCLES;

    return (uint32_t)((cycles * clock->cycle_ps + PS_PER_US - 1u) / PS_PER_US);
}

unsigned exbus_pio_tx(const exbus_pio_clock_t *clock, const uint8_t *tx,
                      unsigned n, uint32_t window_us, uint32_t *words,
                      exbus_pio_run_t *run)
{
    if (clock == NULL || tx == NULL || words == NULL || run == NULL
        || n == 0u || n > EXBUS_TX_MAX) {
        return 0;
    }
    const uint32_t ticks = exbus_pio_window_ticks(clock, window_us);

    if (ticks == 0u) {
        return 0;
    }
    words[0] = n - 1u;
    words[1] = ticks;
    for (unsigned i = 0; i < n; ++i) {
        words[2u + i] = tx[i] | (EXBUS_PIO_QUIET_TICKS << 8);
    }
    run->window_ticks = ticks;
    run->tx_bytes = n;
    return n + 2u;
}

/* Cycles to microseconds, rounded, saturated at 0xFFFF. */
static uint16_t cycles_us16(const exbus_pio_clock_t *clock, uint64_t cycles)
{
    const uint32_t us = (exbus_pio_cycles_ns(clock, cycles) + 500u) / 1000u;

    return (uint16_t)(us > 0xFFFFu ? 0xFFFFu : us);
}

void exbus_pio_capture(const exbus_pio_clock_t *clock,
                       const exbus_pio_run_t *run, const uint32_t *words,
                       unsigned n, bool lost, exbus_capture_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (clock == NULL || run == NULL || (words == NULL && n != 0u)) {
        out->overflow = 1;
        return;
    }
    const uint32_t w = run->window_ticks;
    /* The sample that saw the last byte, in cycles after the release. */
    uint64_t seen = 0;

    out->overflow = (uint8_t)(lost ? 1u : 0u);
    for (unsigned k = 0; k < n; ++k) {
        const uint32_t x = words[k] & EXBUS_PIO_WINDOW_MAX;

        /* The first byte: 1 cycle to the first sample, 2 a tick of the
         * window.  A later byte: EXBUS_PIO_BYTE_CYCLES after the one
         * before, then 2 a tick of the quiet time, which starts at its
         * count less 1. */
        if (k >= EXBUS_RX_MAX
            || (k == 0u && x > w)
            || (k > 0u && x >= EXBUS_PIO_QUIET_TICKS)) {
            out->overflow = 1;
            break;
        }
        seen = k == 0u
            ? 1u + (uint64_t)(w - x) * EXBUS_PIO_TICK_CYCLES
            : seen + EXBUS_PIO_BYTE_CYCLES
              + (uint64_t)(EXBUS_PIO_QUIET_TICKS - 1u - x)
                * EXBUS_PIO_TICK_CYCLES;
        /* The sample read the pin as it was SYNC cycles before; the one
         * before it saw it high 2 cycles earlier still. */
        const uint64_t edge = seen > EXBUS_PIO_SYNC_CYCLES + 1u
                                  ? seen - EXBUS_PIO_SYNC_CYCLES - 1u : 0u;

        out->bytes[k].value = (uint8_t)((words[k] >> 23) & 0xFFu);
        out->bytes[k].flags = (uint8_t)((words[k] & 0x80000000u) != 0u
                                            ? 0u : EXBUS_RXB_FRAMING);
        out->bytes[k].at_us = cycles_us16(clock, edge);
        out->n = (uint8_t)(k + 1u);
    }
    /* The release ends 2 cycles after the sample the count runs out on. */
    const uint64_t release = out->n == 0u
        ? (uint64_t)w * EXBUS_PIO_TICK_CYCLES + EXBUS_PIO_RELEASE_EXTRA
        : seen + EXBUS_PIO_BYTE_CYCLES
          + (uint64_t)EXBUS_PIO_QUIET_TICKS * EXBUS_PIO_TICK_CYCLES;

    out->release_us = cycles_us16(clock, release);
}
