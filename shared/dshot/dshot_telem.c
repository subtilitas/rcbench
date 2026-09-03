/*
 * The inbound half of DShot: the GCR (group-coded recording) burst an ESC
 * (electronic speed controller) sends back on the signal line.  See dshot.h
 * for the model.
 *
 * SPDX-License-Identifier: MIT
 */

#include "dshot.h"

#include <stddef.h>

/*
 * Sixteen five-bit codes, one per nibble.
 *
 * The point of the code is that no code word has two adjacent zeros and none
 * has more than two adjacent ones at a join, so the line changes level often
 * enough for a receiver to keep its phase without a separate clock.  The
 * table is the protocol's; it is not derived from anything.
 */
static const uint8_t k_quintet[16] = {
    0x19u, 0x1Bu, 0x12u, 0x13u, 0x1Du, 0x15u, 0x16u, 0x17u,
    0x1Au, 0x09u, 0x0Au, 0x0Bu, 0x1Eu, 0x0Du, 0x0Eu, 0x0Fu,
};

/** 0xFF for the sixteen codes that are not in the table. */
static uint8_t nibble_of(uint8_t quintet)
{
    for (uint8_t n = 0; n < 16u; ++n) {
        if (k_quintet[n] == quintet) {
            return n;
        }
    }
    return 0xFFu;
}

/*
 * The checksum, folded over the twelve bits that carry the reading.  The
 * response inverts it, which is what stops a decoder locked onto the wrong
 * bit boundary from accepting a shifted copy of a valid frame.
 */
static uint16_t fold(uint16_t v)
{
    uint16_t c = v;
    c ^= (uint16_t)(c >> 4);
    c ^= (uint16_t)(c >> 8);
    return (uint16_t)(c & 0x0Fu);
}

/* ------------------------------------------------------------- the code */

/*
 * The line is differential: each bit is the one before it exclusive-ored with
 * the data, so the data is recovered by exclusive-oring the line with itself
 * shifted down one.  Bit 20 is the seed and is not data; it is sent as zero,
 * because the line idles high in bidirectional DShot and the response has to
 * begin with a falling edge for a receiver to find it at all.
 */
#define GCR_DATA_MASK  0x000FFFFFu   /* bits 19..0: four quintets */

uint32_t dshot_gcr_encode(uint16_t value)
{
    uint32_t gcr = 0u;
    for (int n = 3; n >= 0; --n) {
        const uint8_t nib = (uint8_t)((value >> (n * 4)) & 0x0Fu);
        gcr = (gcr << 5) | k_quintet[nib];
    }

    /* Walk down from the seed, because each line bit depends on the one
     * above it. */
    uint32_t line = 0u;
    uint32_t prev = 0u;                 /* the seed, sent as a falling edge */
    for (int i = 19; i >= 0; --i) {
        const uint32_t bit = ((gcr >> i) & 1u) ^ prev;
        line |= bit << i;
        prev = bit;
    }
    return line;                        /* seed bit is 0, so nothing to set */
}

bool dshot_gcr_decode(uint32_t line, uint16_t *out)
{
    if (out == NULL) {
        return false;
    }
    const uint32_t gcr = (line ^ (line >> 1)) & GCR_DATA_MASK;
    uint16_t value = 0u;
    for (unsigned n = 0; n < 4u; ++n) {
        const uint8_t nib = nibble_of((uint8_t)((gcr >> (n * 5u)) & 0x1Fu));
        if (nib == 0xFFu) {
            return false;
        }
        value |= (uint16_t)((uint16_t)nib << (n * 4u));
    }
    *out = value;
    return true;
}

/* --------------------------------------------------------- what it meant */

/*
 * Extended telemetry types.  The type is the top nibble of the twelve-bit
 * payload and is always even, because an eRPM payload always has bit 8 set:
 * an ESC with extended telemetry enabled normalises its exponent so that it
 * does.  An ESC without it sends nothing but eRPM, and then bit 8 means what
 * it says -- which is why the caller has to declare whether the mode was
 * turned on rather than this file guessing from the bits.
 */
#define EDT_TYPE_TEMPERATURE  0x02u
#define EDT_TYPE_VOLTAGE      0x04u
#define EDT_TYPE_CURRENT      0x06u
#define EDT_TYPE_DEBUG1       0x08u
#define EDT_TYPE_DEBUG2       0x0Au
#define EDT_TYPE_STRESS       0x0Cu
#define EDT_TYPE_STATUS       0x0Eu

/** The payload an ESC sends for a motor that is not turning. */
#define ERPM_STOPPED  0x0FFFu

/** One electrical revolution in microseconds, to electrical rpm. */
#define US_PER_MINUTE  60000000u

static void erpm_from_payload(uint16_t payload, dshot_telem_t *out)
{
    out->kind = DSHOT_TELEM_ERPM;
    /* Nine bits of mantissa shifted by three bits of exponent: 1 us to
     * 65408 us, which is 917 rpm to 60 million at one pole pair. */
    out->period_us = (uint32_t)(payload & 0x01FFu) << (payload >> 9);
    out->erpm = (payload == ERPM_STOPPED || out->period_us == 0u)
                    ? 0u
                    : (US_PER_MINUTE / out->period_us);
}

bool dshot_telem_from_value(uint16_t value, bool edt, dshot_telem_t *out)
{
    if (out == NULL) {
        return false;
    }
    if (fold(value) != 0x0Fu) {
        return false;
    }
    const uint16_t payload = (uint16_t)(value >> 4);

    out->payload   = payload;
    out->period_us = 0u;
    out->erpm      = 0u;
    out->value     = (uint8_t)(payload & 0x00FFu);

    if (!edt || (payload & 0x0100u) != 0u) {
        erpm_from_payload(payload, out);
        return true;
    }
    switch ((unsigned)(payload >> 8)) {
    case EDT_TYPE_TEMPERATURE: out->kind = DSHOT_TELEM_TEMPERATURE; break;
    case EDT_TYPE_VOLTAGE:     out->kind = DSHOT_TELEM_VOLTAGE;     break;
    case EDT_TYPE_CURRENT:     out->kind = DSHOT_TELEM_CURRENT;     break;
    case EDT_TYPE_DEBUG1:      out->kind = DSHOT_TELEM_DEBUG1;      break;
    case EDT_TYPE_DEBUG2:      out->kind = DSHOT_TELEM_DEBUG2;      break;
    case EDT_TYPE_STRESS:      out->kind = DSHOT_TELEM_STRESS;      break;
    case EDT_TYPE_STATUS:      out->kind = DSHOT_TELEM_STATUS;      break;
    default:
        /* Type zero with bit 8 clear: an eRPM frame short enough that the
         * exponent is zero, from an ESC that has extended telemetry on and
         * did not normalise it.  Read as what it is rather than refused. */
        erpm_from_payload(payload, out);
        break;
    }
    return true;
}

bool dshot_telem_decode(uint32_t line, bool edt, dshot_telem_t *out)
{
    uint16_t value = 0u;
    if (!dshot_gcr_decode(line, &value)) {
        return false;
    }
    return dshot_telem_from_value(value, edt, out);
}

uint16_t dshot_telem_value(dshot_telem_kind_t kind, uint16_t payload)
{
    uint16_t p;
    switch (kind) {
    case DSHOT_TELEM_TEMPERATURE:
        p = (uint16_t)((EDT_TYPE_TEMPERATURE << 8) | (payload & 0xFFu)); break;
    case DSHOT_TELEM_VOLTAGE:
        p = (uint16_t)((EDT_TYPE_VOLTAGE << 8) | (payload & 0xFFu));     break;
    case DSHOT_TELEM_CURRENT:
        p = (uint16_t)((EDT_TYPE_CURRENT << 8) | (payload & 0xFFu));     break;
    case DSHOT_TELEM_DEBUG1:
        p = (uint16_t)((EDT_TYPE_DEBUG1 << 8) | (payload & 0xFFu));      break;
    case DSHOT_TELEM_DEBUG2:
        p = (uint16_t)((EDT_TYPE_DEBUG2 << 8) | (payload & 0xFFu));      break;
    case DSHOT_TELEM_STRESS:
        p = (uint16_t)((EDT_TYPE_STRESS << 8) | (payload & 0xFFu));      break;
    case DSHOT_TELEM_STATUS:
        p = (uint16_t)((EDT_TYPE_STATUS << 8) | (payload & 0xFFu));      break;
    case DSHOT_TELEM_ERPM:
    default:
        p = (uint16_t)(payload & 0x0FFFu);                               break;
    }
    return (uint16_t)(((uint16_t)(p << 4)) | (uint16_t)(~fold(p) & 0x0Fu));
}

uint32_t dshot_rpm(uint32_t erpm, uint8_t pole_pairs)
{
    if (pole_pairs == 0u) {
        return 0u;
    }
    return erpm / pole_pairs;
}

/* ------------------------------------------------------------ the capture */

static bool sample_at(const uint32_t *samples, size_t i)
{
    return ((samples[i / 32u] >> (31u - (i % 32u))) & 1u) != 0u;
}

bool dshot_rx_bits(const uint32_t *samples, size_t words, uint8_t oversample,
                   uint32_t *line)
{
    if (samples == NULL || line == NULL || words == 0u || oversample < 2u) {
        return false;
    }
    const size_t total = words * 32u;

    /*
     * The response is found by the falling edge that starts it, and an edge
     * needs the idle level in front of it: a capture that begins already low
     * carries no phase, only a level.
     */
    size_t i = 0u;
    while (i < total && !sample_at(samples, i)) {
        ++i;                            /* skip a low run before the idle */
    }
    while (i < total && sample_at(samples, i)) {
        ++i;                            /* the idle itself                */
    }
    if (i >= total) {
        return false;
    }

    /*
     * Phase is reset at every transition, so the sample point is always in
     * the middle of the bit it belongs to however far the ESC's clock has
     * drifted from this end's.  Without it, a percent of error walks the
     * sample point a fifth of a bit over the 21 bits of a response.
     */
    const uint8_t centre = (uint8_t)(oversample / 2u);
    bool     level = false;             /* the falling edge, at index i    */
    uint8_t  phase = 0u;
    unsigned got   = 0u;
    uint32_t out   = 0u;

    for (; i < total && got < DSHOT_TELEM_BITS; ++i) {
        const bool now = sample_at(samples, i);
        if (now != level) {
            level = now;
            phase = 0u;
        }
        if (phase == centre) {
            out = (out << 1) | (level ? 1u : 0u);
            ++got;
        }
        ++phase;
        if (phase >= oversample) {
            phase = 0u;
        }
    }
    if (got < DSHOT_TELEM_BITS) {
        return false;
    }
    *line = out;
    return true;
}
