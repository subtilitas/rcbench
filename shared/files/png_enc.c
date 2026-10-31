/**
 * @file
 * @brief The PNG encoder (png_enc.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "png_enc.h"

#include <stdlib.h>
#include <string.h>

#include "fm_crc.h"

/** @brief Deflate's shortest match. */
#define MIN_MATCH 3
/** @brief Deflate's longest match. */
#define MAX_MATCH 258
/** @brief The Adler-32 modulus. */
#define ADLER_MOD 65521u
/** @brief Bytes Adler-32 can add before its sums need the modulus. */
#define ADLER_RUN 5552u

/** @brief Length codes 257 to 285: each one's shortest length. */
static const uint16_t k_len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258,
};
/** @brief Length codes 257 to 285: their extra bits. */
static const uint8_t k_len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
    5, 5, 5, 5, 0,
};
/** @brief Distance codes 0 to 29: each one's shortest distance. */
static const uint16_t k_dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
    769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
/** @brief Distance codes 0 to 29: their extra bits. */
static const uint8_t k_dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10,
    11, 11, 12, 12, 13, 13,
};

/**
 * @brief Hand bytes to the sink unless it has refused.
 * @param e     the state
 * @param data  the bytes
 * @param n     how many
 */
static void emit(png_enc_t *e, const uint8_t *data, size_t n)
{
    if (e->ok && !e->sink(data, n, e->ctx)) {
        e->ok = false;
    }
}

/**
 * @brief Store @p v big-endian.
 * @param p  takes 4 bytes
 * @param v  the value
 */
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/**
 * @brief One whole chunk: length, type, data, CRC-32 over type and data.
 * @param e     the state
 * @param type  the 4 letters
 * @param data  the data; may be NULL with @p n 0
 * @param n     its bytes
 */
static void chunk(png_enc_t *e, const char *type, const uint8_t *data,
                  size_t n)
{
    uint8_t b[4];
    put_be32(b, (uint32_t)n);
    emit(e, b, 4u);
    emit(e, (const uint8_t *)type, 4u);
    uint32_t crc = fm_crc32(0u, type, 4u);
    if (n != 0u) {
        emit(e, data, n);
        crc = fm_crc32(crc, data, n);
    }
    put_be32(b, crc);
    emit(e, b, 4u);
}

/**
 * @brief One byte of the zlib stream; a full buffer goes out as an IDAT.
 * @param e  the state
 * @param v  the byte
 */
static void out_byte(png_enc_t *e, uint8_t v)
{
    e->out[e->idat++] = v;
    if (e->idat == PNG_ENC_IDAT) {
        chunk(e, "IDAT", e->out, e->idat);
        e->idat = 0u;
    }
}

/**
 * @brief Bits of deflate's stream, least significant first.
 * @param e  the state
 * @param v  the bits
 * @param n  how many, up to 16
 */
static void put_bits(png_enc_t *e, uint32_t v, int n)
{
    e->bits |= v << e->nbits;
    e->nbits += n;
    while (e->nbits >= 8) {
        out_byte(e, (uint8_t)e->bits);
        e->bits >>= 8;
        e->nbits -= 8;
    }
}

/**
 * @brief A Huffman code: deflate sends it from its top bit down.
 * @param e     the state
 * @param code  the code
 * @param n     its bits, up to 9
 */
static void put_code(png_enc_t *e, uint32_t code, int n)
{
    uint32_t r = 0u;
    for (int i = 0; i < n; ++i) {
        r = (r << 1) | ((code >> i) & 1u);
    }
    put_bits(e, r, n);
}

/**
 * @brief A literal or a length symbol in deflate's fixed code.
 * @param e    the state
 * @param sym  the symbol, 0 to 287
 */
static void put_sym(png_enc_t *e, unsigned sym)
{
    if (sym < 144u) {
        put_code(e, 0x30u + sym, 8);
    } else if (sym < 256u) {
        put_code(e, 0x190u + (sym - 144u), 9);
    } else if (sym < 280u) {
        put_code(e, sym - 256u, 7);
    } else {
        put_code(e, 0xC0u + (sym - 280u), 8);
    }
}

/**
 * @brief A match: its length and distance symbols with their extra bits.
 * @param e     the state
 * @param len   the length, MIN_MATCH to MAX_MATCH
 * @param dist  the distance, 1 to 32,768
 */
static void put_match(png_enc_t *e, int len, unsigned dist)
{
    int lc = 28;
    while (k_len_base[lc] > len) {
        --lc;
    }
    put_sym(e, 257u + (unsigned)lc);
    if (k_len_extra[lc] != 0u) {
        put_bits(e, (uint32_t)(len - k_len_base[lc]), k_len_extra[lc]);
    }
    int dc = 29;
    while (k_dist_base[dc] > dist) {
        --dc;
    }
    put_code(e, (uint32_t)dc, 5);
    if (k_dist_extra[dc] != 0u) {
        put_bits(e, dist - k_dist_base[dc], k_dist_extra[dc]);
    }
}

/**
 * @brief Add bytes to the stream's Adler-32.
 * @param e  the state
 * @param p  the bytes
 * @param n  how many
 */
static void adler_add(png_enc_t *e, const uint8_t *p, size_t n)
{
    uint32_t a = e->adler_a;
    uint32_t b = e->adler_b;
    while (n != 0u) {
        size_t run = (n < ADLER_RUN) ? n : ADLER_RUN;
        n -= run;
        while (run-- != 0u) {
            a += *p++;
            b += a;
        }
        a %= ADLER_MOD;
        b %= ADLER_MOD;
    }
    e->adler_a = a;
    e->adler_b = b;
}

bool png_enc_start(png_enc_t *e, int w, int h, png_enc_sink_t sink, void *ctx)
{
    static const uint8_t k_sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A,
                                      0x0A };
    if (e == NULL || sink == NULL || w < 1 || w > PNG_ENC_MAX_W || h < 1) {
        return false;
    }
    e->sink = sink;
    e->ctx = ctx;
    e->w = w;
    e->h = h;
    e->rows = 0;
    e->ok = true;
    e->adler_a = 1u;
    e->adler_b = 0u;
    e->bits = 0u;
    e->nbits = 0;
    e->idat = 0u;
    e->pos = 0u;
    for (size_t i = 0u; i < PNG_ENC_HASH; ++i) {
        e->head[i] = -1;
    }
    memset(e->raw, 0, sizeof(e->raw));
    memset(e->win, 0, sizeof(e->win));
    emit(e, k_sig, sizeof(k_sig));
    /* 8 bits a sample, colour type 2 (RGB), deflate, adaptive filters, no
     * interlace. */
    uint8_t ihdr[13];
    put_be32(ihdr, (uint32_t)w);
    put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8u;
    ihdr[9] = 2u;
    ihdr[10] = 0u;
    ihdr[11] = 0u;
    ihdr[12] = 0u;
    chunk(e, "IHDR", ihdr, sizeof(ihdr));
    /* zlib: deflate with a 32KiB window, fastest level, no dictionary. */
    out_byte(e, 0x78u);
    out_byte(e, 0x01u);
    /* One block, final, fixed codes: BFINAL 1, BTYPE 01. */
    put_bits(e, 1u, 1);
    put_bits(e, 1u, 2);
    return e->ok;
}

/**
 * @brief Row @p row in RGB, the bit copied into the low bits so 0x1F is 255.
 * @param e    the state
 * @param row  the pixels
 */
static void row_rgb(png_enc_t *e, const uint16_t *row)
{
    memcpy(e->raw_prev, e->raw, (size_t)e->w * 3u);
    uint8_t *p = e->raw;
    for (int x = 0; x < e->w; ++x) {
        const unsigned v = row[x];
        const unsigned r = (v >> 11) & 0x1Fu;
        const unsigned g = (v >> 5) & 0x3Fu;
        const unsigned b = v & 0x1Fu;
        *p++ = (uint8_t)((r << 3) | (r >> 2));
        *p++ = (uint8_t)((g << 2) | (g >> 4));
        *p++ = (uint8_t)((b << 3) | (b >> 2));
    }
}

/**
 * @brief A filtered byte's score: its distance from 0 as a signed value.
 * @param v  the byte
 * @return 0 to 128
 */
static inline unsigned score(uint8_t v)
{
    return (v < 128u) ? v : 256u - v;
}

/**
 * @brief Filter the row into @p f: the type byte, then the bytes.
 * @param e  the state, row in @c raw and the one above in @c raw_prev
 * @param f  takes 1 + 3 * width bytes
 */
static void row_filter(const png_enc_t *e, uint8_t *f)
{
    const size_t n = (size_t)e->w * 3u;
    const uint8_t *a = e->raw;
    const uint8_t *u = e->raw_prev;
    unsigned none = 0u;
    unsigned sub = 0u;
    unsigned up = 0u;
    for (size_t i = 0u; i < n; ++i) {
        none += score(a[i]);
        sub += score((uint8_t)(a[i] - ((i >= 3u) ? a[i - 3u] : 0u)));
        up += score((uint8_t)(a[i] - u[i]));
    }
    if (up <= sub && up <= none) {
        f[0] = 2u;
        for (size_t i = 0u; i < n; ++i) {
            f[1u + i] = (uint8_t)(a[i] - u[i]);
        }
    } else if (sub <= none) {
        f[0] = 1u;
        for (size_t i = 0u; i < n; ++i) {
            f[1u + i] = (uint8_t)(a[i] - ((i >= 3u) ? a[i - 3u] : 0u));
        }
    } else {
        f[0] = 0u;
        memcpy(f + 1, a, n);
    }
}

/**
 * @brief The match table's slot of 3 bytes.
 * @param p  the bytes
 * @return the slot
 */
static inline unsigned hash3(const uint8_t *p)
{
    const uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                       | ((uint32_t)p[2] << 16);
    return (v * 2654435761u) >> (32u - 12u);
}

/**
 * @brief How many bytes match between @p a and @p b.
 * @param a    the earlier place
 * @param b    the later place
 * @param max  the most to compare
 * @return the count
 */
static inline int match_len(const uint8_t *a, const uint8_t *b, int max)
{
    int n = 0;
    while (n < max && a[n] == b[n]) {
        ++n;
    }
    return n;
}

bool png_enc_row(png_enc_t *e, const uint16_t *row)
{
    if (e == NULL || row == NULL || e->rows >= e->h) {
        return false;
    }
    const int len = 1 + e->w * 3;
    /* The rows before this one stand in win[0..len), this one after them;
     * a table entry older than the previous row is out of reach. */
    memcpy(e->win, e->win + len, (size_t)len);
    uint8_t *cur = e->win + len;
    row_rgb(e, row);
    row_filter(e, cur);
    adler_add(e, cur, (size_t)len);

    /* The stream position of win[0]; below 0 on the first row. */
    const int64_t base = (int64_t)e->pos - len;
    const int low = (e->rows > 0) ? 0 : len;
    int i = len;
    const int end = 2 * len;
    while (i < end) {
        int best = 0;
        unsigned dist = 0u;
        const int left = end - i;
        const int max = (left < MAX_MATCH) ? left : MAX_MATCH;
        if (left >= MIN_MATCH) {
            const unsigned h = hash3(e->win + i);
            const int32_t cand = e->head[h];
            e->head[h] = (int32_t)(base + i);
            /* The byte before and the pixel before cost one compare each
             * and catch every flat run; the table the rest. */
            if (i - 1 >= low) {
                best = match_len(e->win + i - 1, e->win + i, max);
                dist = 1u;
            }
            if (best < max && i - 3 >= low) {
                const int m = match_len(e->win + i - 3, e->win + i, max);
                if (m > best) {
                    best = m;
                    dist = 3u;
                }
            }
            if (best < max && cand >= 0) {
                const int64_t at = (int64_t)cand - base;
                if (at >= low && at < i) {
                    const int m = match_len(e->win + at, e->win + i, max);
                    if (m > best) {
                        best = m;
                        dist = (unsigned)(i - (int)at);
                    }
                }
            }
        }
        if (best >= MIN_MATCH) {
            put_match(e, best, dist);
            i += best;
        } else {
            put_sym(e, e->win[i]);
            ++i;
        }
    }
    e->pos += (uint32_t)len;
    ++e->rows;
    return e->ok;
}

bool png_enc_end(png_enc_t *e)
{
    if (e == NULL || e->rows != e->h) {
        return false;
    }
    put_sym(e, 256u);       /* the block's end */
    if (e->nbits > 0) {
        put_bits(e, 0u, 8 - e->nbits);
    }
    const uint32_t adler = (e->adler_b << 16) | e->adler_a;
    out_byte(e, (uint8_t)(adler >> 24));
    out_byte(e, (uint8_t)(adler >> 16));
    out_byte(e, (uint8_t)(adler >> 8));
    out_byte(e, (uint8_t)adler);
    if (e->idat != 0u) {
        chunk(e, "IDAT", e->out, e->idat);
        e->idat = 0u;
    }
    chunk(e, "IEND", NULL, 0u);
    return e->ok;
}
