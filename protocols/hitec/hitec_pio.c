/**
 * @file
 * @brief The arithmetic of the Hitec PIO pin driver.
 *
 * See include/hitec_pio.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hitec_pio.h"

#include <stddef.h>
#include <string.h>

/**
 * @brief The longest window, lead and level fit their fields at the fastest bit
 *        rate, with the divider 0.05% short.
 */
_Static_assert((uint64_t)HITEC_TIME_MAX_US * HITEC_D_BIT_RATE
                   * HITEC_PIO_CYCLES_PER_BIT / HITEC_PIO_TICK_CYCLES
                   / 1000000u * 1001u / 1000u
               < HITEC_PIO_MAX_WINDOW_TICKS,
               "the longest window does not fit the stamp");
_Static_assert((uint64_t)HITEC_TIME_MAX_US * HITEC_D_BIT_RATE
                   * HITEC_PIO_CYCLES_PER_BIT / 2u / 1000000u * 1001u / 1000u
               < HITEC_PIO_MAX_LEAD,
               "the longest level does not fit the lead count");

/**
 * @brief A bit time as the pad makes it: bit 0 the direction (1 drives), bit 1
 *        the output level.
 */
#define PAIR(dir, level) ((uint8_t)((dir) | ((level) << 1)))
#define RELEASED PAIR(0u, 0u)

/** @brief Bit times of the longest SEND, padded. */
#define MAX_BITS ((HITEC_PIO_TX_WORDS - 3u) * 16u)

bool hitec_pio_clock(uint32_t sys_hz, uint8_t family, hitec_pio_clock_t *out)
{
    const uint32_t rate = hitec_bit_rate(family);
    uint64_t per;
    uint64_t div256;
    uint64_t got;
    uint64_t want;
    uint64_t err;
    hitec_pio_clock_t c;

    if (out == NULL || rate == 0u || sys_hz == 0u) {
        return false;
    }
    /* Divider times 256, rounded to nearest. */
    per = (uint64_t)HITEC_PIO_CYCLES_PER_BIT * rate;
    div256 = ((uint64_t)sys_hz * 256u + per / 2u) / per;
    /* The bit time error: the divider's bit against the family's, both
     * as system clock cycles times 256 per bit. */
    got = div256 * per;
    want = (uint64_t)sys_hz * 256u;
    err = got > want ? got - want : want - got;
    c.sys_hz = sys_hz;
    c.div256 = (uint32_t)div256;
    c.err_ppm = (uint32_t)((err * 1000000u + want - 1u) / want);
    c.family = family;
    if (div256 < (uint64_t)HITEC_PIO_MIN_DIV * 256u
        || (div256 >> 8) > HITEC_PIO_MAX_DIV
        || c.err_ppm > HITEC_PIO_MAX_ERROR_PPM) {
        return false;
    }
    *out = c;
    return true;
}

uint32_t hitec_pio_bit_ns(const hitec_pio_clock_t *clock)
{
    /* 16 cycles of div256 / 256 system clock cycles each. */
    return (uint32_t)(((uint64_t)clock->div256
                       * (HITEC_PIO_CYCLES_PER_BIT * 1000000000ull / 256u)
                       + clock->sys_hz / 2u) / clock->sys_hz);
}

uint32_t hitec_pio_cycles_ns(const hitec_pio_clock_t *clock, uint64_t cycles)
{
    const uint64_t q = cycles * clock->div256;   /* system cycles x 256 */
    const uint64_t sc = q >> 8;
    const uint64_t frac_ns = (q & 0xFFu) * 1000000000u / 256u;
    const uint64_t ns = sc / clock->sys_hz * 1000000000u
                        + ((sc % clock->sys_hz) * 1000000000u + frac_ns)
                          / clock->sys_hz;

    return ns > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)ns;
}

/**
 * @brief @p cycles state machine cycles of @p clock, in us, rounded up and held
 *        at UINT32_MAX.
 *
 * The seconds come apart from the rest, so no product leaves 64 bits for any
 * count a uint64_t holds times a divider.
 */
static uint32_t cycles_us_up(const hitec_pio_clock_t *clock, uint64_t cycles)
{
    const uint64_t q = cycles * clock->div256;   /* system cycles x 256 */
    const uint64_t den = (uint64_t)clock->sys_hz * 256u;
    const uint64_t us = q / den * 1000000u
                        + ((q % den) * 1000000u + den - 1u) / den;

    return us > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)us;
}

/**
 * @brief @p us as state machine cycles of @p clock, rounded to nearest, or up
 *        with @p up.
 */
static uint64_t us_cycles(const hitec_pio_clock_t *clock, uint32_t us,
                          bool up)
{
    const uint64_t den = (uint64_t)clock->div256 * 1000000u;

    return ((uint64_t)us * clock->sys_hz * 256u + (up ? den - 1u : den / 2u))
           / den;
}

/**
 * @brief The bit time of logical @p bit (1 the idle level) for @p family and @p
 *        drive.
 *
 * Classic: the wire carries the bit.  D series: the wire carries it inverted.
 * Open drain drives a low wire and releases a high one.
 */
static uint8_t pair_of(uint8_t family, uint8_t drive, unsigned bit)
{
    const unsigned wire = family == HITEC_D ? (bit ^ 1u) : bit;

    if (drive == HITEC_DRIVE_PUSH_PULL) {
        return PAIR(1u, wire);
    }
    return wire != 0u ? RELEASED : PAIR(1u, 0u);
}

/** @brief Start bit, 8 data bits least significant first, the stop bit. */
static unsigned put_byte(uint8_t family, uint8_t drive, uint8_t value,
                         bool stop, uint8_t *pairs, unsigned at)
{
    pairs[at++] = pair_of(family, drive, 0u);
    for (unsigned b = 0; b < 8u; ++b) {
        pairs[at++] = pair_of(family, drive, (value >> b) & 1u);
    }
    if (stop) {
        pairs[at++] = pair_of(family, drive, 1u);
    }
    return at;
}

/** @brief 16 bit times from @p pairs as one word. */
static uint32_t pair_word(const uint8_t *pairs)
{
    uint32_t w = 0;

    for (unsigned i = 0; i < 16u; ++i) {
        w |= (uint32_t)pairs[i] << (2u * i);
    }
    return w;
}

/**
 * @brief Build the word of a lead: level, direction and count.
 *
 */
static uint32_t lead_word(uint8_t pair, uint64_t count)
{
    /* The program shifts the level out first, then the direction. */
    return (uint32_t)((pair >> 1) & 1u) | (uint32_t)(pair & 1u) << 1
           | (uint32_t)count << 2;
}

unsigned hitec_pio_tx(const hitec_pio_clock_t *clock, uint8_t drive,
                      const hitec_xfer_t *xfer, uint32_t *words,
                      hitec_pio_run_t *run)
{
    uint8_t pairs[MAX_BITS];
    uint8_t lead_pair = RELEASED;
    uint64_t lead_count = 0;
    uint64_t window = 0;
    unsigned bits = 0;
    unsigned n = 0;
    uint64_t span;

    if (clock == NULL || words == NULL || run == NULL
        || !hitec_xfer_valid(xfer) || xfer->family != clock->family
        || drive > HITEC_DRIVE_PUSH_PULL
        || (drive == HITEC_DRIVE_PUSH_PULL && xfer->family != HITEC_D)) {
        return 0;
    }
    if (xfer->window_us != 0u) {
        window = (us_cycles(clock, xfer->window_us, false) + 1u)
                 / HITEC_PIO_TICK_CYCLES;
    }

    if (xfer->kind == HITEC_XFER_SEND) {
        unsigned pad;
        uint64_t want;
        uint64_t have;

        lead_pair = pair_of(xfer->family, drive, 1u);
        /* Every byte with its stop bit and the gap, except: classic ends
         * before the last stop bit, the D series sends it and one released
         * bit time.  The front is padded below. */
        for (unsigned i = 0; i < xfer->n_tx; ++i) {
            const bool last = i + 1u == xfer->n_tx;

            bits = put_byte(xfer->family, drive, xfer->tx[i],
                            !last || xfer->family == HITEC_D, pairs, bits);
            for (unsigned g = 0; !last && g < xfer->gap_bits; ++g) {
                pairs[bits++] = lead_pair;
            }
        }
        if (xfer->family == HITEC_D) {
            for (unsigned s = 0; s < HITEC_D_SETTLE_BITS; ++s) {
                pairs[bits++] = RELEASED;
            }
        }
        pad = (16u - bits % 16u) % 16u;
        memmove(&pairs[pad], &pairs[0], bits);
        memset(&pairs[0], lead_pair, pad);
        bits += pad;
        /* The lead: the count, then the padding bit times. */
        want = us_cycles(clock, xfer->lead_us, true);
        have = HITEC_PIO_LEAD_CYCLES + (uint64_t)pad * HITEC_PIO_CYCLES_PER_BIT;
        /* Never shorter than asked. */
        if (want > have) {
            lead_count = (want - have + 1u) / 2u;
        }
    } else if (xfer->kind == HITEC_XFER_LEVEL) {
        const uint64_t want = us_cycles(clock, xfer->level_us, true);

        if (xfer->level != 0u && drive != HITEC_DRIVE_PUSH_PULL) {
            return 0;
        }
        lead_pair = PAIR(1u, xfer->level);
        if (want > HITEC_PIO_LEAD_CYCLES) {
            lead_count = (want - HITEC_PIO_LEAD_CYCLES + 1u) / 2u;
        }
    }

    words[n++] = lead_word(lead_pair, lead_count);
    words[n++] = (uint32_t)window;
    words[n++] = bits;
    for (unsigned w = 0; w < bits / 16u; ++w) {
        words[n++] = pair_word(pairs + (size_t)w * 16u);
    }
    if (xfer->kind == HITEC_XFER_LISTEN && xfer->answer != 0u) {
        uint8_t a[HITEC_PIO_ANSWER_BITS];
        unsigned at = put_byte(xfer->family, drive, xfer->answer_byte, true,
                               a, 0);

        while (at < HITEC_PIO_ANSWER_BITS) {
            a[at++] = RELEASED;
        }
        words[n++] = pair_word(a);
    }

    memset(run, 0, sizeof(*run));
    run->family = xfer->family;
    run->window_ns = xfer->window_us * 1000u;
    run->window_ticks = (uint32_t)window;
    run->max_rx = xfer->max_rx;
    run->answer = xfer->kind == HITEC_XFER_LISTEN ? xfer->answer : 0u;
    run->lead_ns = hitec_pio_cycles_ns(
        clock, 2u * lead_count + HITEC_PIO_LEAD_CYCLES
                   + (uint64_t)bits * HITEC_PIO_CYCLES_PER_BIT);
    /* The hand-over and the pulls, the lead and the bits, the waiting with
     * a transition per byte, the bytes, the answer, the drain, one bit of
     * slack.  In us straight from the cycles: a lead and a window of 4s
     * each are more ns than a uint32_t holds. */
    span = 4u + 2u * lead_count + HITEC_PIO_LEAD_CYCLES
           + (uint64_t)bits * HITEC_PIO_CYCLES_PER_BIT
           + HITEC_PIO_TICK_CYCLES * (window + 2u)
           + (uint64_t)xfer->max_rx * (HITEC_PIO_BYTE_CYCLES + 2u)
           + (run->answer != 0u ? HITEC_PIO_ANSWER_CYCLES : 0u)
           + HITEC_PIO_DRAIN_CYCLES + HITEC_PIO_CYCLES_PER_BIT;
    run->span_us = cycles_us_up(clock, span);
    return n;
}

bool hitec_pio_answer_due(const hitec_pio_run_t *run, unsigned n,
                          unsigned pc)
{
    if (run == NULL || run->answer == 0u || n != 1u) {
        return false;
    }
    return pc == HITEC_PIO_ASK_PC || pc == HITEC_PIO_ANSWER_PC
           || pc == HITEC_PIO_ANSWER_PC + 1u
           || (pc >= HITEC_PIO_BIT_PC && pc < HITEC_PIO_RELEASE_PC);
}

void hitec_pio_capture(const hitec_pio_clock_t *clock,
                       const hitec_pio_run_t *run, const uint32_t *stamps,
                       unsigned n, bool full, bool lost, bool aborted,
                       bool cut, hitec_capture_t *out)
{
    const uint32_t sync_ns = (uint32_t)(((uint64_t)HITEC_PIO_SYNC_SYS_CYCLES
                                         * 1000000000u + clock->sys_hz / 2u)
                                        / clock->sys_hz);
    uint32_t prev = run->window_ticks;

    memset(out, 0, sizeof(*out));
    out->family = run->family;
    out->window_ns = run->window_ns;
    out->full = full ? 1u : 0u;
    out->overflow = lost ? 1u : 0u;
    out->aborted = aborted ? 1u : 0u;
    if (n > HITEC_RX_MAX) {
        n = HITEC_RX_MAX;
        out->overflow = 1;
    }
    for (unsigned k = 0; k < n; ++k) {
        const uint32_t s = stamps[k];
        const uint32_t x = s >> 9;
        uint64_t d;

        if (x > prev) {
            out->overflow = 1;
            break;
        }
        prev = x;
        d = 2u + 2u * (uint64_t)(run->window_ticks - x)
            + (uint64_t)HITEC_PIO_BYTE_CYCLES * k
            + (run->answer != 0u && k > 0u ? HITEC_PIO_ANSWER_CYCLES : 0u);
        out->byte[k].at_ns = hitec_pio_cycles_ns(clock, d - 1u) - sync_ns;
        out->byte[k].value = (uint8_t)(s & 0xFFu);
        out->byte[k].framing = ((s >> 8) & 1u) == 0u ? 1u : 0u;
        out->n = (uint8_t)(k + 1u);
    }
    out->answered = run->answer != 0u && out->n > 0u && !cut ? 1u : 0u;
}
