/*
 * SHA-256, written from FIPS 180-4: the constants of section 4.2.2, the
 * initial hash value of section 5.3.3, the padding of section 5.1.1 and the
 * computation of section 6.2.2.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_sha256.h"

#include <string.h>

/* The first 32 bits of the fractional parts of the cube roots of the first
 * 64 primes (section 4.2.2). */
static const uint32_t k_round[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t rotr(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

/* One 512-bit block (section 6.2.2, steps 1 to 4). */
static void compress(uint32_t h[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (size_t t = 0; t < 16u; ++t) {
        w[t] = ((uint32_t)block[4u * t] << 24)
               | ((uint32_t)block[4u * t + 1u] << 16)
               | ((uint32_t)block[4u * t + 2u] << 8)
               | (uint32_t)block[4u * t + 3u];
    }
    for (unsigned t = 16; t < 64u; ++t) {
        const uint32_t s0 = rotr(w[t - 15u], 7) ^ rotr(w[t - 15u], 18)
                            ^ (w[t - 15u] >> 3);
        const uint32_t s1 = rotr(w[t - 2u], 17) ^ rotr(w[t - 2u], 19)
                            ^ (w[t - 2u] >> 10);
        w[t] = w[t - 16u] + s0 + w[t - 7u] + s1;
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (unsigned t = 0; t < 64u; ++t) {
        const uint32_t big1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch   = (e & f) ^ (~e & g);
        const uint32_t t1   = hh + big1 + ch + k_round[t] + w[t];
        const uint32_t big0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj  = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2   = big0 + maj;
        hh = g;
        g  = f;
        f  = e;
        e  = d + t1;
        d  = c;
        c  = b;
        b  = a;
        a  = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

void link_sha256_init(link_sha256_t *s)
{
    /* The first 32 bits of the fractional parts of the square roots of the
     * first 8 primes (section 5.3.3). */
    static const uint32_t k_init[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    if (s == NULL) {
        return;
    }
    memcpy(s->h, k_init, sizeof(s->h));
    s->length = 0u;
    memset(s->block, 0, sizeof(s->block));
}

void link_sha256_update(link_sha256_t *s, const void *data, size_t len)
{
    if (s == NULL || data == NULL) {
        return;
    }
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0u) {
        const size_t used = (size_t)(s->length % 64u);
        size_t take = 64u - used;
        if (take > len) {
            take = len;
        }
        memcpy(&s->block[used], p, take);
        s->length += take;
        p += take;
        len -= take;
        if (used + take == 64u) {
            compress(s->h, s->block);
        }
    }
}

void link_sha256_final(link_sha256_t *s, uint8_t out[LINK_SHA256_BYTES])
{
    if (s == NULL || out == NULL) {
        return;
    }
    /* A 1 bit, zeros to 56 bytes into a block, then the length in bits as
     * a 64-bit big-endian number (section 5.1.1). */
    const uint64_t bits = s->length * 8u;
    size_t used = (size_t)(s->length % 64u);
    s->block[used++] = 0x80u;
    if (used > 56u) {
        memset(&s->block[used], 0, 64u - used);
        compress(s->h, s->block);
        used = 0u;
    }
    memset(&s->block[used], 0, 56u - used);
    for (unsigned i = 0; i < 8u; ++i) {
        s->block[56u + i] = (uint8_t)(bits >> (56u - 8u * i));
    }
    compress(s->h, s->block);

    for (size_t i = 0; i < 8u; ++i) {
        out[4u * i]      = (uint8_t)(s->h[i] >> 24);
        out[4u * i + 1u] = (uint8_t)(s->h[i] >> 16);
        out[4u * i + 2u] = (uint8_t)(s->h[i] >> 8);
        out[4u * i + 3u] = (uint8_t)s->h[i];
    }
}

void link_sha256(const void *data, size_t len, uint8_t out[LINK_SHA256_BYTES])
{
    link_sha256_t s;
    link_sha256_init(&s);
    link_sha256_update(&s, data, len);
    link_sha256_final(&s, out);
}
