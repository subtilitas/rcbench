/**
 * @file
 * @brief Templates: a frame from tokens, with its length and checksum
 *        tokens filled in over the finished frame (prn_job.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "prn_job.h"

unsigned prn_enc_width(uint8_t enc)
{
    switch (enc) {
    case PRN_E_U8:
        return 1u;
    case PRN_E_LE16:
    case PRN_E_BE16:
        return 2u;
    case PRN_E_LE32:
    case PRN_E_BE32:
        return 4u;
    default:
        return 0u;
    }
}

unsigned prn_ck_width(uint8_t kind)
{
    switch (kind) {
    case PRN_CK_SUM8:
    case PRN_CK_XOR8:
    case PRN_CK_CRC8:
        return 1u;
    case PRN_CK_SUM16LE:
    case PRN_CK_SUM16BE:
    case PRN_CK_CRC16CCITT_LE:
    case PRN_CK_CRC16CCITT_BE:
    case PRN_CK_SVPAGE:
        return 2u;
    case PRN_CK_CRC32LE:
        return 4u;
    default:
        return 0u;
    }
}

/**
 * @brief CRC-8, polynomial 0x07, init 0, not reflected.
 * @param p the bytes
 * @param n their count
 * @return the CRC
 */
static uint8_t crc8(const uint8_t *p, size_t n)
{
    uint8_t c = 0u;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (unsigned b = 0; b < 8u; ++b) {
            c = (uint8_t)((c & 0x80u) ? (((unsigned)c << 1) ^ 0x07u)
                                     : ((unsigned)c << 1));
        }
    }
    return c;
}

/**
 * @brief CRC-16, polynomial 0x1021, init 0xFFFF, not reflected.
 * @param p the bytes
 * @param n their count
 * @return the CRC
 */
static uint16_t crc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0xFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= (uint16_t)(p[i] << 8);
        for (unsigned b = 0; b < 8u; ++b) {
            c = (uint16_t)((c & 0x8000u) ? (((unsigned)c << 1) ^ 0x1021u)
                                        : ((unsigned)c << 1));
        }
    }
    return c;
}

/**
 * @brief CRC-32 of IEEE 802.3: reflected, polynomial 0xEDB88320, init and
 *        final XOR 0xFFFFFFFF.
 * @param p the bytes
 * @param n their count
 * @return the CRC
 */
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (unsigned b = 0; b < 8u; ++b) {
            c = (c & 1u) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
        }
    }
    return ~c;
}

uint32_t prn_ck(uint8_t kind, const uint8_t *p, size_t n)
{
    uint32_t v = 0u;
    switch (kind) {
    case PRN_CK_SUM8:
    case PRN_CK_SUM16LE:
    case PRN_CK_SUM16BE:
        for (size_t i = 0; i < n; ++i) {
            v += p[i];
        }
        return (kind == PRN_CK_SUM8) ? (v & 0xFFu) : (v & 0xFFFFu);
    case PRN_CK_XOR8:
        for (size_t i = 0; i < n; ++i) {
            v ^= p[i];
        }
        return v;
    case PRN_CK_CRC8:
        return crc8(p, n);
    case PRN_CK_CRC16CCITT_LE:
    case PRN_CK_CRC16CCITT_BE:
        return crc16(p, n);
    case PRN_CK_CRC32LE:
        return crc32(p, n);
    default:
        return 0u;
    }
}

/**
 * @brief Write a value in an encoding.
 * @param out the bytes
 * @param enc prn_enc_t
 * @param v   the value
 * @return false when it does not fit the encoding
 */
static bool put(uint8_t *out, uint8_t enc, uint64_t v)
{
    const unsigned w = prn_enc_width(enc);
    if (w == 0u || (v >> (8u * w)) != 0u) { /* w is 1, 2 or 4 */
        return false;
    }
    const bool be = enc == PRN_E_BE16 || enc == PRN_E_BE32;
    for (unsigned i = 0; i < w; ++i) {
        const uint8_t b = (uint8_t)(v >> (8u * i));
        out[be ? (w - 1u - i) : i] = b;
    }
    return true;
}

/**
 * @brief A range's first and last byte in a frame of @p len bytes.
 * @param a    the first, negative from the end
 * @param b    the last, negative from the end
 * @param len  the frame's length
 * @param from set to the first
 * @param to   set to the last
 * @return false when the range is not inside the frame
 */
static bool range(int8_t a, int8_t b, size_t len, size_t *from, size_t *to)
{
    const long f = (a < 0) ? (long)len + a : a;
    const long t = (b < 0) ? (long)len + b : b;
    if (f < 0 || t < f || (size_t)t >= len) {
        return false;
    }
    *from = (size_t)f;
    *to = (size_t)t;
    return true;
}

/**
 * @brief Bytes a token takes in a frame.
 * @param t   the token
 * @param ctx the values
 * @return its width
 */
static size_t width(const prn_tok_t *t, const prn_ctx_t *ctx)
{
    switch (t->type) {
    case PRN_T_BYTE:
        return 1u;
    case PRN_T_ARG:
    case PRN_T_VAR:
    case PRN_T_LEN:
        return prn_enc_width(t->x);
    case PRN_T_CK:
        return prn_ck_width(t->x);
    default: /* PRN_T_DATA */
        return (size_t)ctx->data_len + ctx->data_pad;
    }
}

/**
 * @brief Fill the length or checksum tokens of a finished frame.
 * @param s    the spec
 * @param seq  the tokens
 * @param ctx  the values
 * @param out  the frame
 * @param len  its length
 * @param type PRN_T_LEN or PRN_T_CK
 * @return false when a range is not inside the frame or a length does
 *         not fit its encoding
 */
static bool fill(const prn_spec_t *s, prn_tseq_t seq, const prn_ctx_t *ctx,
                 uint8_t *out, size_t len, uint8_t type)
{
    size_t at = 0u;
    for (unsigned i = 0; i < seq.n; ++i) {
        const prn_tok_t *t = &s->tok[seq.first + i];
        const size_t w = width(t, ctx);
        if (t->type == type && !(type == PRN_T_CK && t->x == PRN_CK_SVPAGE)) {
            size_t from;
            size_t to;
            if (!range(t->a, t->b, len, &from, &to)) {
                return false;
            }
            if (type == PRN_T_LEN) {
                if (!put(out + at, t->x, (uint64_t)(to - from + 1u))) {
                    return false;
                }
            } else {
                const uint32_t v = prn_ck(t->x, out + from, to - from + 1u);
                const bool be = t->x == PRN_CK_SUM16BE
                                || t->x == PRN_CK_CRC16CCITT_BE;
                for (unsigned k = 0; k < w; ++k) {
                    out[at + (be ? (w - 1u - k) : k)] = (uint8_t)(v >> (8u * k));
                }
            }
        }
        at += w;
    }
    return true;
}

bool prn_tmpl_render(const prn_spec_t *s, prn_tseq_t seq,
                     const prn_ctx_t *ctx, uint8_t *out, size_t cap,
                     size_t *len)
{
    size_t total = 0u;
    if ((size_t)seq.first + seq.n > PRN_TOKENS) {
        return false;
    }
    for (unsigned i = 0; i < seq.n; ++i) {
        total += width(&s->tok[seq.first + i], ctx);
    }
    if (total > cap) {
        return false;
    }
    size_t at = 0u;
    for (unsigned i = 0; i < seq.n; ++i) {
        const prn_tok_t *t = &s->tok[seq.first + i];
        const size_t w = width(t, ctx);
        bool ok = true;
        switch (t->type) {
        case PRN_T_BYTE:
            out[at] = t->x;
            break;
        case PRN_T_ARG:
            ok = t->a >= 0 && (unsigned)t->a < PRN_ARGS
                 && put(out + at, t->x, ctx->arg[t->a]);
            break;
        case PRN_T_VAR:
            ok = t->a > 0 && t->a < (int)PRN_V_COUNT
                 && put(out + at, t->x, ctx->var[t->a]);
            break;
        case PRN_T_DATA:
            if (ctx->data_len != 0u) {
                if (ctx->data == NULL) {
                    return false;
                }
                memcpy(out + at, ctx->data, ctx->data_len);
            }
            memset(out + at + ctx->data_len, ctx->pad_byte, ctx->data_pad);
            break;
        default: /* PRN_T_LEN, PRN_T_CK: after the frame is whole */
            memset(out + at, 0, w);
            break;
        }
        if (!ok) {
            return false;
        }
        at += w;
    }
    if (!fill(s, seq, ctx, out, total, PRN_T_LEN)
        || !fill(s, seq, ctx, out, total, PRN_T_CK)) {
        return false;
    }
    *len = total;
    return true;
}
