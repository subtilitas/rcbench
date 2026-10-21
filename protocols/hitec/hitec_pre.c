/**
 * @file
 * @brief Receive a D series answer by precharge and sample.
 *
 * See
 * include/hitec_pre.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hitec_pre.h"

#include <stddef.h>
#include <string.h>

#include "hitec_pio.h"

/** @brief Samples a second the period aims at. */
#define SAMPLE_RATE (HITEC_D_BIT_RATE * HITEC_PRE_SAMPLES_PER_BIT)

/**
 * @brief PIO instruction words, without side-set: the delay in bits 8 to 12.
 */
#define PIO_DELAY(d)          ((uint16_t)((d) << 8))
#define PIO_SET_PINDIRS(v, d) ((uint16_t)(0xE080u | (v) | PIO_DELAY(d)))
#define PIO_IN_PINS_1(d)      ((uint16_t)(0x4001u | PIO_DELAY(d)))
#define PIO_NOP(d)            ((uint16_t)(0xA042u | PIO_DELAY(d)))  /* mov y, y */
#define PIO_DELAY_MAX 31u

_Static_assert(HITEC_RX_MAX <= 16u, "framing is a 16-bit mask");

/** @brief @p ns at @p sys_hz in cycles, rounded to nearest. */
static uint32_t ns_cycles(uint32_t sys_hz, uint32_t ns)
{
    return (uint32_t)(((uint64_t)ns * sys_hz + 500000000u) / 1000000000u);
}

uint32_t hitec_pre_cycles_ns(const hitec_pre_timing_t *t, uint64_t cycles)
{
    const uint64_t ns = (cycles * 1000000000u + t->sys_hz / 2u) / t->sys_hz;

    return ns > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)ns;
}

bool hitec_pre_timing(uint32_t sys_hz, hitec_pre_timing_t *out)
{
    hitec_pre_timing_t t;
    uint64_t per_bit;
    uint32_t charge_ns;
    uint32_t wait_ns;

    if (out == NULL || sys_hz < HITEC_PRE_MIN_HZ
        || sys_hz > HITEC_PRE_MAX_HZ) {
        return false;
    }
    memset(&t, 0, sizeof(t));
    t.sys_hz = sys_hz;
    t.period = (sys_hz + SAMPLE_RATE / 2u) / SAMPLE_RATE;
    t.charge = ns_cycles(sys_hz, HITEC_PRE_CHARGE_NS);
    t.wait = ns_cycles(sys_hz, HITEC_PRE_SAMPLE_NS);
    charge_ns = hitec_pre_cycles_ns(&t, t.charge);
    wait_ns = hitec_pre_cycles_ns(&t, t.wait);
    if (t.charge == 0u || t.charge > PIO_DELAY_MAX + 1u
        || t.wait == 0u || t.wait > PIO_DELAY_MAX + 1u
        || t.charge + t.wait + 1u > t.period
        || charge_ns < HITEC_PRE_CHARGE_MIN_NS
        || charge_ns > HITEC_PRE_CHARGE_MAX_NS
        || wait_ns < HITEC_PRE_SAMPLE_MIN_NS
        || wait_ns > HITEC_PRE_SAMPLE_MAX_NS) {
        return false;
    }
    per_bit = (uint64_t)HITEC_D_BIT_RATE * t.period;
    t.spb_q16 = (uint32_t)((((uint64_t)sys_hz << 16) + per_bit / 2u)
                           / per_bit);
    /* floor((j + 1/2) samples a bit): the middle of bit j from the sample
     * after the edge, the edge half a sample before that sample. */
    for (unsigned j = 0; j < HITEC_PRE_FRAME_BITS; ++j) {
        t.mid[j] = (uint16_t)(((uint64_t)(2u * j + 1u) * t.spb_q16) >> 17);
    }
    *out = t;
    return true;
}

unsigned hitec_pre_program(const hitec_pre_timing_t *t, uint16_t *words,
                           unsigned max)
{
    uint32_t rest;
    uint32_t d;
    unsigned n = 0;

    if (t == NULL || words == NULL || t->charge == 0u || t->wait == 0u
        || t->charge + t->wait + 1u > t->period) {
        return 0;
    }
    rest = t->period - t->charge - t->wait - 1u;
    /* 3 instructions, then a nop for every 32 cycles left past the
     * sample's own delay. */
    if (max < 3u + (rest > PIO_DELAY_MAX
                        ? (rest - PIO_DELAY_MAX + 31u) / 32u
                        : 0u)) {
        return 0;
    }
    words[n++] = PIO_SET_PINDIRS(1u, t->charge - 1u);
    words[n++] = PIO_SET_PINDIRS(0u, t->wait - 1u);
    d = rest > PIO_DELAY_MAX ? PIO_DELAY_MAX : rest;
    words[n++] = PIO_IN_PINS_1(d);
    rest -= d;
    while (rest > 0u) {
        d = rest > PIO_DELAY_MAX + 1u ? PIO_DELAY_MAX + 1u : rest;
        words[n++] = PIO_NOP(d - 1u);
        rest -= d;
    }
    return n;
}

uint32_t hitec_pre_window_samples(const hitec_pre_timing_t *t,
                                  uint32_t window_us)
{
    const uint64_t den = (uint64_t)t->period * 1000000u;

    return (uint32_t)(((uint64_t)window_us * t->sys_hz + den / 2u) / den);
}

uint32_t hitec_pre_words(const hitec_pre_timing_t *t, uint32_t window_us,
                         uint8_t max_rx)
{
    uint64_t samples;
    uint64_t words;

    if (t == NULL || window_us > HITEC_PRE_WINDOW_MAX_US) {
        return 0;
    }
    samples = (uint64_t)hitec_pre_window_samples(t, window_us)
              + (uint64_t)max_rx * (t->mid[HITEC_PRE_FRAME_BITS - 1u] + 1u);
    words = (samples + 31u) / 32u + 1u;
    return words > HITEC_PRE_WORDS ? 0u : (uint32_t)words;
}

/* --- the decoder ---------------------------------------------------------- */

void hitec_pre_dec_init(hitec_pre_dec_t *d, const hitec_pre_timing_t *t,
                        uint32_t window_us, uint8_t max_rx)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    d->window = t != NULL ? hitec_pre_window_samples(t, window_us) : 0u;
    d->max_rx = max_rx > HITEC_RX_MAX ? (uint8_t)HITEC_RX_MAX : max_rx;
    d->phase = (uint8_t)HITEC_PRE_WAIT_REST;
}

/**
 * @brief Mark the decoder done and record why it ended.
 *
 */
static void dec_end(hitec_pre_dec_t *d, hitec_pre_end_t why)
{
    d->phase = (uint8_t)HITEC_PRE_DEC_DONE;
    d->end = (uint8_t)why;
}

/**
 * @brief The byte whose start bit is at d->start[d->n], all of its samples in.
 */
static void dec_byte(hitec_pre_dec_t *d, const hitec_pre_timing_t *t,
                     const uint32_t *words)
{
    const uint32_t s = d->start[d->n];
    unsigned v = 0;

    if (hitec_pre_sample(words, s + t->mid[0]) == 0u) {
        /* Low in the start bit's middle: a glitch.  The sample waited, and
         * the rest level comes before the next start bit. */
        d->at = s + 1u;
        d->waited++;
        d->phase = (uint8_t)HITEC_PRE_WAIT_REST;
        return;
    }
    for (unsigned k = 0; k < 8u; ++k) {
        if (hitec_pre_sample(words, s + t->mid[k + 1u]) == 0u) {
            v |= 1u << k;   /* low is a 1 */
        }
    }
    /* A stop bit at the rest level is the low sample a start bit follows,
     * so a byte back to back from a servo up to 4 % fast is not missed; a
     * stop bit read high waits for the rest level. */
    if (hitec_pre_sample(words, s + t->mid[HITEC_PRE_FRAME_BITS - 1u])
        != 0u) {
        d->framing |= (uint16_t)(1u << d->n);
        d->phase = (uint8_t)HITEC_PRE_WAIT_REST;
    } else {
        d->phase = (uint8_t)HITEC_PRE_WAIT_START;
    }
    d->value[d->n] = (uint8_t)v;
    d->n++;
    d->at = s + t->mid[HITEC_PRE_FRAME_BITS - 1u] + 1u;
    if (d->n >= d->max_rx) {
        dec_end(d, HITEC_PRE_END_FULL);
    }
}

bool hitec_pre_dec_feed(hitec_pre_dec_t *d, const hitec_pre_timing_t *t,
                        const uint32_t *words, uint32_t n_words)
{
    uint32_t avail;

    if (d == NULL) {
        return false;
    }
    if (t == NULL || words == NULL) {
        return d->phase == (uint8_t)HITEC_PRE_DEC_DONE;
    }
    avail = (n_words > HITEC_PRE_WORDS ? HITEC_PRE_WORDS : n_words) * 32u;
    while (d->phase != (uint8_t)HITEC_PRE_DEC_DONE) {
        unsigned want;

        if (d->phase == (uint8_t)HITEC_PRE_IN_BYTE) {
            if (d->start[d->n] + t->mid[HITEC_PRE_FRAME_BITS - 1u]
                >= avail) {
                return false;
            }
            dec_byte(d, t, words);
            continue;
        }
        if (d->waited >= d->window) {
            dec_end(d, HITEC_PRE_END_WINDOW);
            break;
        }
        if (d->at >= avail) {
            return false;
        }
        want = d->phase == (uint8_t)HITEC_PRE_WAIT_REST ? 0u : 1u;
        /* A whole word of the other level: 32 waits in one step. */
        if ((d->at & 31u) == 0u && d->at + 32u <= avail
            && d->window - d->waited >= 32u
            && words[d->at >> 5] == (want != 0u ? 0u : 0xFFFFFFFFu)) {
            d->at += 32u;
            d->waited += 32u;
            continue;
        }
        if (hitec_pre_sample(words, d->at) != want) {
            d->at++;
            d->waited++;
        } else if (want == 0u) {
            d->at++;
            d->waited++;
            d->phase = (uint8_t)HITEC_PRE_WAIT_START;
        } else {
            d->start[d->n] = d->at;
            d->phase = (uint8_t)HITEC_PRE_IN_BYTE;
        }
    }
    return true;
}

void hitec_pre_dec_close(hitec_pre_dec_t *d)
{
    if (d != NULL && d->phase != (uint8_t)HITEC_PRE_DEC_DONE) {
        dec_end(d, HITEC_PRE_END_SHORT);
    }
}

/* --- the run -------------------------------------------------------------- */

hitec_pre_plan_t hitec_pre_begin(hitec_pre_run_t *r,
                                 const hitec_pre_timing_t *t,
                                 const hitec_xfer_t *x)
{
    if (r == NULL) {
        return HITEC_PRE_PLAN_REFUSE;
    }
    memset(r, 0, sizeof(*r));
    if (t == NULL || !hitec_xfer_valid(x) || x->family != (uint8_t)HITEC_D) {
        return HITEC_PRE_PLAN_REFUSE;
    }
    if (x->kind == (uint8_t)HITEC_XFER_LEVEL
        || (x->kind == (uint8_t)HITEC_XFER_SEND && x->window_us == 0u)) {
        return HITEC_PRE_PLAN_LINE;
    }
    if (x->kind == (uint8_t)HITEC_XFER_LISTEN && x->answer != 0u) {
        return HITEC_PRE_PLAN_REFUSE;
    }
    r->words = hitec_pre_words(t, x->window_us, x->max_rx);
    if (r->words == 0u) {
        return HITEC_PRE_PLAN_REFUSE;
    }
    hitec_pre_dec_init(&r->dec, t, x->window_us, x->max_rx);
    r->window_ns = x->window_us * 1000u;
    if (x->kind == (uint8_t)HITEC_XFER_SEND) {
        r->phase = (uint8_t)HITEC_PRE_TX;
        return HITEC_PRE_PLAN_SEND;
    }
    r->phase = (uint8_t)HITEC_PRE_SAMPLE;
    return HITEC_PRE_PLAN_LISTEN;
}

void hitec_pre_tx_xfer(const hitec_xfer_t *x, hitec_xfer_t *out)
{
    if (x == NULL || out == NULL) {
        return;
    }
    *out = *x;
    out->window_us = 0u;
    out->max_rx = 0u;
    out->answer = 0u;
}

void hitec_pre_tx_done(hitec_pre_run_t *r)
{
    if (r != NULL && r->phase == (uint8_t)HITEC_PRE_TX) {
        r->phase = (uint8_t)HITEC_PRE_SAMPLE;
    }
}

bool hitec_pre_step(hitec_pre_run_t *r, const hitec_pre_timing_t *t,
                    const uint32_t *words, uint32_t n_words, bool all_in)
{
    if (r == NULL) {
        return false;
    }
    if (r->phase != (uint8_t)HITEC_PRE_SAMPLE) {
        return r->phase == (uint8_t)HITEC_PRE_DONE;
    }
    if (n_words > r->words) {
        n_words = r->words;
    }
    if (!hitec_pre_dec_feed(&r->dec, t, words, n_words) && !all_in) {
        return false;
    }
    hitec_pre_dec_close(&r->dec);
    r->phase = (uint8_t)HITEC_PRE_DONE;
    return true;
}

void hitec_pre_abort(hitec_pre_run_t *r, const hitec_pre_timing_t *t,
                     const uint32_t *words, uint32_t n_words)
{
    if (r == NULL || r->phase == (uint8_t)HITEC_PRE_IDLE
        || r->phase == (uint8_t)HITEC_PRE_DONE) {
        return;
    }
    if (r->phase == (uint8_t)HITEC_PRE_SAMPLE) {
        if (n_words > r->words) {
            n_words = r->words;
        }
        (void)hitec_pre_dec_feed(&r->dec, t, words, n_words);
    }
    hitec_pre_dec_close(&r->dec);
    r->aborted = 1;
    r->phase = (uint8_t)HITEC_PRE_DONE;
}

void hitec_pre_lost(hitec_pre_run_t *r)
{
    if (r == NULL || r->phase == (uint8_t)HITEC_PRE_IDLE) {
        return;
    }
    hitec_pre_dec_close(&r->dec);
    r->lost = 1;
    r->phase = (uint8_t)HITEC_PRE_DONE;
}

void hitec_pre_capture(const hitec_pre_run_t *r, const hitec_pre_timing_t *t,
                       int32_t offset_ns, hitec_capture_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->family = (uint8_t)HITEC_D;
    if (r == NULL || t == NULL) {
        return;
    }
    out->window_ns = r->window_ns;
    out->n = r->dec.n;
    out->full = r->dec.end == (uint8_t)HITEC_PRE_END_FULL ? 1u : 0u;
    out->aborted = r->aborted;
    out->overflow = r->lost != 0u
                            || (r->aborted == 0u
                                && r->dec.end == (uint8_t)HITEC_PRE_END_SHORT)
                        ? 1u
                        : 0u;
    for (unsigned k = 0; k < r->dec.n && k < HITEC_RX_MAX; ++k) {
        /* The edge in half cycles: the start sample's instant, less the
         * synchroniser and half a period. */
        const uint64_t half =
            2u * ((uint64_t)r->dec.start[k] * t->period + t->charge + t->wait)
            - (uint64_t)HITEC_PIO_SYNC_SYS_CYCLES * 2u - t->period;
        const int64_t at =
            (int64_t)((hitec_pre_cycles_ns(t, half) + 1u) / 2u)
            + (int64_t)offset_ns;

        out->byte[k].at_ns = at < 0 ? 0u
                             : at > 0xFFFFFFFF ? 0xFFFFFFFFu
                                               : (uint32_t)at;
        out->byte[k].value = r->dec.value[k];
        out->byte[k].framing = (uint8_t)((r->dec.framing >> k) & 1u);
    }
}
