/*
 * SPDX-License-Identifier: MIT
 */

#include "kst_pio.h"

#include <stddef.h>
#include <string.h>

/* @p ns as cycles of @p hz, rounded to nearest. */
static uint32_t cycles_of(uint32_t hz, uint32_t ns)
{
    return (uint32_t)(((uint64_t)hz * ns + 500000000u) / 1000000000u);
}

/* @p cycles of @p hz as ns, rounded to nearest. */
static uint32_t ns_of(uint32_t hz, uint64_t cycles)
{
    return (uint32_t)((cycles * 1000000000u + hz / 2u) / hz);
}

bool kst_pio_clock(uint32_t sys_hz, kst_pio_clock_t *out)
{
    kst_pio_clock_t c;
    uint64_t got_ps;
    uint64_t want_ps;
    uint64_t err_ps;

    if (out == NULL || sys_hz == 0u) {
        return false;
    }
    c.sys_hz = sys_hz;
    c.half_cycles = cycles_of(sys_hz, KST_HALF_CELL_NS);
    c.release_cycles = cycles_of(sys_hz, KST_RELEASE_NS);
    c.quiet_ticks = cycles_of(sys_hz, KST_PIO_QUIET_NS) / KST_PIO_TICK_CYCLES;
    /* A stamp resolves one tick. */
    if ((uint64_t)KST_PIO_TICK_CYCLES * 1000000000u
        > (uint64_t)KST_PIO_MAX_TICK_NS * sys_hz) {
        return false;
    }
    /* The error of the half-cell the count gives, in ps. */
    got_ps = (uint64_t)c.half_cycles * 1000000000000u / sys_hz;
    want_ps = (uint64_t)KST_HALF_CELL_NS * 1000u;
    err_ps = got_ps > want_ps ? got_ps - want_ps : want_ps - got_ps;
    if (err_ps * 1000000u > want_ps * KST_PIO_MAX_ERROR_PPM) {
        return false;
    }
    *out = c;
    return true;
}

uint32_t kst_pio_half_cell_ns(const kst_pio_clock_t *clock)
{
    return ns_of(clock->sys_hz, clock->half_cycles);
}

uint32_t kst_pio_tick_ns(const kst_pio_clock_t *clock)
{
    return (uint32_t)(((uint64_t)KST_PIO_TICK_CYCLES * 1000000000u
                       + clock->sys_hz - 1u) / clock->sys_hz);
}

/* Cycles from the last edge to the first sample of the pin, as the pin was
 * when the sample was taken. */
static uint32_t first_sample_cycles(const kst_pio_clock_t *clock)
{
    return clock->release_cycles + KST_PIO_SAMPLE_LEAD - KST_PIO_SYNC_CYCLES;
}

unsigned kst_pio_tx(const kst_pio_clock_t *clock, const kst_frame_t *frame,
                    uint32_t window_ns, uint32_t *words, kst_pio_run_t *run)
{
    unsigned total;
    unsigned pad;
    unsigned n = 0;
    unsigned level_words;
    uint32_t window_cycles;
    uint32_t first;

    if (clock == NULL || frame == NULL || words == NULL || run == NULL
        || frame->n_half == 0u
        || frame->n_half > KST_FRAME_MAX_HALF_CELLS) {
        return 0;
    }
    window_cycles = cycles_of(clock->sys_hz, window_ns);
    first = first_sample_cycles(clock) + KST_PIO_SYNC_CYCLES;
    if (window_cycles <= first) {
        return 0;
    }

    /* The frame and the low half-cell that makes its last edge, with low
     * half-cells in front up to a word boundary. */
    total = (unsigned)frame->n_half + 1u;
    pad = (32u - total % 32u) % 32u;
    level_words = (pad + total) / 32u;

    words[n++] = clock->half_cycles - KST_PIO_BIT_OVERHEAD;
    words[n++] = pad + total - 1u;
    for (unsigned w = 0; w < level_words; ++w) {
        uint32_t bits = 0;

        for (unsigned b = 0; b < 32u; ++b) {
            const unsigned at = w * 32u + b;

            if (at >= pad && kst_frame_level(frame, at - pad)) {
                bits |= 0x80000000u >> b;
            }
        }
        words[n++] = bits;
    }
    words[n++] = clock->release_cycles - clock->half_cycles
                 - KST_PIO_LOW_OVERHEAD;
    run->window_ns = window_ns;
    /* The last sample is the last one inside the window. */
    run->window_ticks = (window_cycles - first) / KST_PIO_TICK_CYCLES;
    run->lead_ns = ns_of(clock->sys_hz,
                         (uint64_t)(pad + frame->n_half) * clock->half_cycles);
    words[n++] = run->window_ticks;
    words[n++] = clock->quiet_ticks;
    return n;
}

void kst_pio_capture(const kst_pio_clock_t *clock, const kst_pio_run_t *run,
                     const uint32_t *stamps, unsigned n, bool lost,
                     kst_capture_t *out)
{
    uint64_t at;   /* cycles from the last edge to the latest rising edge */

    memset(out, 0, sizeof(*out));
    out->window_ns = run->window_ns;
    out->overflow = lost ? 1u : 0u;
    if (n == 0u) {
        return;
    }
    /* The first stamp counts from the window. */
    if (stamps[0] > run->window_ticks) {
        out->overflow = 1;
        return;
    }
    at = first_sample_cycles(clock)
         + (uint64_t)(run->window_ticks - stamps[0]) * KST_PIO_TICK_CYCLES;
    if (stamps[0] == run->window_ticks) {
        out->level0 = 1;
    } else {
        out->edge_ns[out->n_edges++] = ns_of(clock->sys_hz, at);
    }
    /* Every later one counts from the rising edge before it.  Rising and
     * falling edges alternate, and the first stamp is a rising edge. */
    for (unsigned i = 1; i < n; ++i) {
        uint64_t t;

        if (stamps[i] > clock->quiet_ticks
            || out->n_edges >= KST_CAP_MAX_EDGES) {
            out->overflow = 1;
            return;
        }
        t = at + (uint64_t)(clock->quiet_ticks - stamps[i] + 1u)
                 * KST_PIO_TICK_CYCLES;
        out->edge_ns[out->n_edges++] = ns_of(clock->sys_hz, t);
        if ((i % 2u) == 0u) {
            at = t;
        }
    }
}
