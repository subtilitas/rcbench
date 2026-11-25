/**
 * @file
 * @brief The literal-only LZMA1 encoder (lzlit.h), a port of
 *        tools/lzlit.py.
 *
 * SPDX-License-Identifier: MIT
 */
#include "lzlit.h"

/** @brief The range coder normalises below this range. */
#define TOP (1u << 24)
/** @brief Bits of a probability. */
#define PROB_BITS 11u
/** @brief A probability's adaptation shift. */
#define MOVE_BITS 5u
/** @brief A probability's start, one half. */
#define PROB_INIT ((uint16_t)(1u << (PROB_BITS - 1u)))

/**
 * @brief Write one output byte, or mark the overflow.
 * @param z the state
 * @param b the byte
 */
static void emit(lzlit_t *z, uint8_t b)
{
    if (z->n < z->cap) {
        z->out[z->n++] = b;
    } else {
        z->over = true;
    }
}

/**
 * @brief Move the top byte of low out, through the cache that waits for
 *        a carry.
 * @param z the state
 */
static void shift_low(lzlit_t *z)
{
    if (z->low < 0xFF000000u || z->low >= ((uint64_t)1 << 32)) {
        const uint8_t carry = (uint8_t)(z->low >> 32);
        uint8_t temp = z->cache;
        do {
            emit(z, (uint8_t)(temp + carry));
            temp = 0xFFu;
        } while (--z->cache_size != 0u);
        z->cache = (uint8_t)(z->low >> 24);
    }
    ++z->cache_size;
    z->low = (z->low << 8) & 0xFFFFFFFFu; /* (UInt32)low << 8 */
}

/**
 * @brief Code one bit with an adaptive probability.
 * @param z the state
 * @param p the probability, updated
 * @param b the bit, 0 or 1
 */
static void put_bit(lzlit_t *z, uint16_t *p, unsigned b)
{
    const uint32_t bound = (z->range >> PROB_BITS) * *p;
    if (b == 0u) {
        z->range = bound;
        *p = (uint16_t)(*p + (((1u << PROB_BITS) - *p) >> MOVE_BITS));
    } else {
        z->low += bound;
        z->range -= bound;
        *p = (uint16_t)(*p - (*p >> MOVE_BITS));
    }
    while (z->range < TOP) {
        z->range <<= 8;
        shift_low(z);
    }
}

void lzlit_begin(lzlit_t *z, uint8_t *out, size_t cap, uint32_t raw_len)
{
    for (size_t i = 0; i < LZLIT_LIT_PROBS; ++i) {
        z->lit[i] = PROB_INIT;
    }
    for (size_t i = 0; i < 4u; ++i) {
        z->is_match[i] = PROB_INIT;
    }
    z->low = 0u;
    z->range = 0xFFFFFFFFu;
    z->cache = 0u;
    z->cache_size = 1u;
    z->out = out;
    z->cap = (out != NULL) ? cap : 0u;
    z->n = 0u;
    z->over = false;
    z->prev = 0u;
    z->pos = 0u;
    emit(z, (uint8_t)LZLIT_PROPS);
    for (unsigned i = 0; i < 4u; ++i) {
        emit(z, (uint8_t)(LZLIT_DICT >> (8u * i)));
    }
    for (unsigned i = 0; i < 4u; ++i) {
        emit(z, (uint8_t)(raw_len >> (8u * i)));
    }
    for (unsigned i = 0; i < 4u; ++i) {
        emit(z, 0u);
    }
}

void lzlit_put(lzlit_t *z, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        const uint8_t b = data[i];
        put_bit(z, &z->is_match[z->pos & 3u], 0u);
        uint16_t *probs = &z->lit[(size_t)0x300u * (size_t)(z->prev >> 5)];
        unsigned sym = 1u;
        for (int k = 7; k >= 0; --k) {
            const unsigned v = ((unsigned)b >> (unsigned)k) & 1u;
            put_bit(z, &probs[sym], v);
            sym = (sym << 1) | v;
        }
        z->prev = b;
        ++z->pos;
    }
}

size_t lzlit_end(lzlit_t *z)
{
    for (int i = 0; i < 5; ++i) {
        shift_low(z);
    }
    return z->over ? 0u : z->n;
}
