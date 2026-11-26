/**
 * @file lzlit_dec.h
 * @brief A decoder of literal-only LZMA streams for the host suites: the
 *        range decoder of the LZMA format with properties lc 3, lp 0, pb 2,
 *        every symbol a literal.  It shares no code with lzlit.c, so a
 *        stream it decodes back to its input was encoded as LZMA decodes.
 *        A match bit of 1 (a stream with matches) is refused.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LZLIT_DEC_H
#define LZLIT_DEC_H

#include <stddef.h>
#include <stdint.h>

/** @brief The range decoder's state. */
typedef struct {
    const uint8_t *in;   /**< the stream after the header */
    size_t         n;    /**< its length */
    size_t         at;   /**< the next byte */
    uint32_t       range; /**< the range */
    uint32_t       code; /**< the code */
} lzd_rc_t;

/**
 * @brief The next input byte, 0 past the end.
 * @param r the decoder
 * @return the byte
 */
static inline uint8_t lzd_byte(lzd_rc_t *r)
{
    return r->at < r->n ? r->in[r->at++] : 0u;
}

/**
 * @brief Decode one bit with its probability.
 * @param r the decoder
 * @param p the probability, 11 bits
 * @return the bit
 */
static inline unsigned lzd_bit(lzd_rc_t *r, uint16_t *p)
{
    const uint32_t bound = (r->range >> 11) * *p;
    unsigned bit;
    if (r->code < bound) {
        r->range = bound;
        *p = (uint16_t)(*p + ((2048u - *p) >> 5));
        bit = 0u;
    } else {
        r->code -= bound;
        r->range -= bound;
        *p = (uint16_t)(*p - (*p >> 5));
        bit = 1u;
    }
    if (r->range < (1u << 24)) {
        r->range <<= 8;
        r->code = (r->code << 8) | lzd_byte(r);
    }
    return bit;
}

/**
 * @brief Decode a literal-only LZMA stream.
 * @param z   the stream: the 13-byte header, then the range coder's bytes
 * @param zn  its length
 * @param out receives the bytes
 * @param cap its room
 * @return the length the header gives, or (size_t)-1 for a header other
 *         than properties 0x5D, a length over @p cap, or a match bit
 */
static inline size_t lzd_decode(const uint8_t *z, size_t zn, uint8_t *out,
                                size_t cap)
{
    static uint16_t is_match[12u * 16u];
    static uint16_t lit[0x300u << 3];
    if (zn < 13u || z[0] != 0x5Du) {
        return (size_t)-1;
    }
    uint64_t len = 0u;
    for (unsigned i = 0; i < 8u; ++i) {
        len |= (uint64_t)z[5u + i] << (8u * i);
    }
    if (len > cap) {
        return (size_t)-1;
    }
    for (size_t i = 0; i < sizeof is_match / sizeof is_match[0]; ++i) {
        is_match[i] = 1024u;
    }
    for (size_t i = 0; i < sizeof lit / sizeof lit[0]; ++i) {
        lit[i] = 1024u;
    }
    lzd_rc_t r = { .in = z + 13u, .n = zn - 13u, .at = 0u,
                   .range = 0xFFFFFFFFu, .code = 0u };
    (void)lzd_byte(&r);
    for (unsigned i = 0; i < 4u; ++i) {
        r.code = (r.code << 8) | lzd_byte(&r);
    }
    uint8_t prev = 0u;
    for (size_t pos = 0; pos < (size_t)len; ++pos) {
        /* state 0 throughout: a literal after a literal */
        if (lzd_bit(&r, &is_match[pos & 3u]) != 0u) {
            return (size_t)-1;
        }
        uint16_t *probs = &lit[0x300u * (unsigned)(prev >> 5)];
        unsigned sym = 1u;
        while (sym < 0x100u) {
            sym = (sym << 1) | lzd_bit(&r, &probs[sym]);
        }
        prev = (uint8_t)sym;
        out[pos] = prev;
    }
    return (size_t)len;
}

#endif /* LZLIT_DEC_H */
