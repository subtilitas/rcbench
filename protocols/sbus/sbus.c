/**
 * @file
 * @brief S.BUS frame decoder and encoder: framing on the byte gap, footer
 *        check, channel unpacking and packing, and the microsecond mapping
 *        both ways.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sbus.h"

#include <string.h>

/**
 * @brief A footer of zero is what the specification says.
 *
 * Receivers in the field also send 0x04, 0x14, 0x24 and 0x34 -- the low nibble
 * carries a frame counter on some of them -- so the check is on the bits that
 * are always clear rather than on the whole byte.  Rejecting those would refuse
 * frames from hardware that works.
 */
static bool footer_ok(uint8_t b)
{
    return (b & 0x0Fu) == 0x00u || (b & 0x0Fu) == 0x04u;
}

void sbus_decoder_reset(sbus_decoder_t *d)
{
    if (d != NULL) {
        memset(d, 0, sizeof(*d));
    }
}

/**
 * @brief Sixteen channels of eleven bits, packed little-endian end to end
 *        across the twenty-two payload bytes with no alignment.
 *
 * Channel n starts at bit 11n, which lands mid-byte for all but the first, so
 * each value is read as a three-byte window shifted down and masked.
 *
 * Written as arithmetic rather than as sixteen expanded expressions: a
 * transposed shift in an expanded form produces a channel that looks
 * plausible and moves the wrong surface.
 */
static void unpack(const uint8_t *p, uint16_t *out)
{
    for (unsigned ch = 0; ch < SBUS_CHANNELS; ++ch) {
        const unsigned bit  = ch * 11u;
        const unsigned byte = 1u + (bit / 8u);   /* 1: past the header */
        const unsigned shift = bit % 8u;
        const uint32_t win = (uint32_t)p[byte]
                             | ((uint32_t)p[byte + 1u] << 8)
                             | ((uint32_t)p[byte + 2u] << 16);
        out[ch] = (uint16_t)((win >> shift) & 0x7FFu);
    }
}

/**
 * @brief The mirror of unpack(): channel n at bit 11n of the payload, least
 *        significant bit first, each value ORed into a three-byte window.
 *
 * @p p must be cleared beforehand; the windows of neighbouring channels
 * share bytes.  Every value is at most SBUS_RAW_MAX, so the window is at
 * most eighteen bits wide and fits the three bytes.
 */
static void pack(const uint16_t *in, uint8_t *p)
{
    for (unsigned ch = 0; ch < SBUS_CHANNELS; ++ch) {
        const unsigned bit  = ch * 11u;
        const unsigned byte = 1u + (bit / 8u);   /* 1: past the header */
        const uint32_t win  = (uint32_t)in[ch] << (bit % 8u);
        p[byte]      |= (uint8_t)(win & 0xFFu);
        p[byte + 1u] |= (uint8_t)((win >> 8) & 0xFFu);
        p[byte + 2u] |= (uint8_t)((win >> 16) & 0xFFu);
    }
}

bool sbus_encode(const sbus_frame_t *f, uint8_t *out)
{
    if (f == NULL || out == NULL) {
        return false;
    }
    /*
     * Refused rather than clamped or masked.  Masking sends 2048 as 0, the
     * other end of the travel; clamping sends a value the caller did not ask
     * for as full travel.  A value above eleven bits is a fault in the
     * caller, and the caller is the one that knows what to send instead.
     * Checked before the first write, so a refused frame leaves out as it
     * was.
     */
    for (unsigned ch = 0; ch < SBUS_CHANNELS; ++ch) {
        if (f->channel[ch] > SBUS_RAW_MAX) {
            return false;
        }
    }
    memset(out, 0, SBUS_FRAME_BYTES);
    out[0] = SBUS_HEADER;
    pack(f->channel, out);
    out[23] = (uint8_t)((f->ch17 ? SBUS_FLAG_CH17 : 0u)
                        | (f->ch18 ? SBUS_FLAG_CH18 : 0u)
                        | (f->frame_lost ? SBUS_FLAG_FRAME_LOST : 0u)
                        | (f->failsafe ? SBUS_FLAG_FAILSAFE : 0u));
    out[SBUS_FRAME_BYTES - 1u] = SBUS_FOOTER;
    return true;
}

bool sbus_decode_byte(sbus_decoder_t *d, uint8_t byte, uint32_t now_us,
                      sbus_frame_t *out)
{
    if (d == NULL) {
        return false;
    }

    /*
     * The gap is the frame boundary, and it outranks everything else.  S.BUS
     * has no checksum and 0x0F is an ordinary channel value, so a decoder that
     * frames on the header alone locks onto the middle of a frame, reports
     * sixteen plausible channels that are all wrong, and stays locked because
     * the next header it finds is in the same wrong place.
     */
    if (d->have_time && (uint32_t)(now_us - d->last_byte_us) >= SBUS_GAP_US) {
        if (d->len != 0u) {
            ++d->resyncs;
        }
        d->len = 0;
    }
    d->have_time    = true;
    d->last_byte_us = now_us;

    /* Nothing but a header can start a frame. */
    if (d->len == 0u && byte != SBUS_HEADER) {
        ++d->resyncs;
        return false;
    }

    d->buf[d->len++] = byte;
    if (d->len < SBUS_FRAME_BYTES) {
        return false;
    }
    d->len = 0;

    if (!footer_ok(d->buf[SBUS_FRAME_BYTES - 1u])) {
        /* Counted apart from a resync: a whole frame of the right length with
         * the wrong tail is a receiver speaking a variant, or a decoder framed
         * one byte out -- not noise. */
        ++d->bad_footer;
        return false;
    }

    ++d->frames;
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        unpack(d->buf, out->channel);
        const uint8_t flags = d->buf[23];
        out->ch17       = (flags & SBUS_FLAG_CH17) != 0u;
        out->ch18       = (flags & SBUS_FLAG_CH18) != 0u;
        out->frame_lost = (flags & SBUS_FLAG_FRAME_LOST) != 0u;
        out->failsafe   = (flags & SBUS_FLAG_FAILSAFE) != 0u;
    }
    return true;
}

uint16_t sbus_to_us(uint16_t raw)
{
    /*
     * Linear through the two points the protocol defines, in integers.  A
     * receiver may send outside them (172 and 1811 are where a transmitter at
     * its endpoints lands, not the limits of the field), so the result is
     * clamped to 800..2200us, a pulse width no servo is damaged by, rather
     * than to the raw range.
     */
    const int32_t span_raw = (int32_t)SBUS_RAW_2000US - (int32_t)SBUS_RAW_1000US;
    /* Rounded to the nearest, halves away from zero: C division truncates
     * toward zero, so the half is added above the 1000us point and
     * subtracted below it. */
    const int32_t num = ((int32_t)raw - (int32_t)SBUS_RAW_1000US) * 1000;
    int32_t us = 1000 + (num >= 0 ? (num + span_raw / 2) / span_raw
                                  : (num - span_raw / 2) / span_raw);
    if (us < 800) {
        us = 800;
    }
    if (us > 2200) {
        us = 2200;
    }
    return (uint16_t)us;
}

uint16_t sbus_from_us(uint16_t us)
{
    /*
     * The line of sbus_to_us() solved for the raw value, with the same
     * rounding: to the nearest, halves away from the 1000us point.  1639
     * counts per 1000us puts a half at 1500us (991.5), which goes to 992.
     * Rounding the raw value moves the pulse by at most half a count,
     * 0.31us, less than the 0.5us at which sbus_to_us() rounds to a
     * neighbour, so sbus_to_us() gives the same pulse back wherever the raw
     * value is not clamped.  The largest product, 64535 * 1639, fits an
     * int32_t.
     */
    const int32_t span_raw = (int32_t)SBUS_RAW_2000US - (int32_t)SBUS_RAW_1000US;
    const int32_t num = ((int32_t)us - 1000) * span_raw;
    int32_t raw = (int32_t)SBUS_RAW_1000US
                  + (num >= 0 ? (num + 500) / 1000 : (num - 500) / 1000);
    if (raw < (int32_t)SBUS_RAW_MIN) {
        raw = (int32_t)SBUS_RAW_MIN;
    }
    if (raw > (int32_t)SBUS_RAW_MAX) {
        raw = (int32_t)SBUS_RAW_MAX;
    }
    return (uint16_t)raw;
}
