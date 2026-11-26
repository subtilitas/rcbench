/**
 * @file
 * @brief A literal-only LZMA1 encoder: every byte is coded as a literal,
 *        with no match finder, no dictionary in memory and no end marker.
 *
 * The output is an LZMA "alone" stream that any LZMA decoder takes: a
 * 13-byte header (properties 0x5D = lc 3, lp 0, pb 2; dictionary 8,192
 * bytes, little-endian 32 bits; the exact uncompressed length,
 * little-endian 64 bits) and the range-coded body.  The dictionary size is
 * only written to the header: with no match the decoder never reads back.
 *
 * Per byte the encoder codes one is_match bit of 0 and the 8 literal bits
 * through the literal coder of the previous byte's top 3 bits.  With
 * literals only the coder's state stays 0, so is_match needs 4
 * probabilities (one per position state of pb 2), not 192.  Probabilities
 * are 11 bits, start at 1,024 and adapt by a shift of 5.  The range coder
 * is the one of LzmaEnc.c: a 33-bit low with carry, a 32-bit range, a cache
 * byte and its count, normalised when the range drops under 2^24 and
 * flushed by 5 shifts.
 *
 * The stream is byte-identical to tools/lzlit.py's.  The input may arrive
 * in pieces of any size (lzlit_put() any number of times), so a caller
 * feeds one 4,096-byte print buffer at a time.
 *
 * Pure C: no heap, no clock, no I/O.  State: 12,336 bytes (lzlit_t),
 * best kept off a task's stack.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LZLIT_H
#define LZLIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bytes of the stream's header: properties, dictionary, length. */
#define LZLIT_HEADER 13u
/** @brief The properties byte: (pb * 5 + lp) * 9 + lc with lc 3, lp 0,
 *         pb 2. */
#define LZLIT_PROPS 0x5Du
/** @brief The dictionary size the header names, in bytes.  A larger one
 *         fails on the printer's RAM (heeen/supvan-cups). */
#define LZLIT_DICT 8192u
/** @brief Literal probabilities: 0x300 per context, 8 contexts (lc 3). */
#define LZLIT_LIT_PROBS (0x300u << 3)
/**
 * @brief The longest stream for @p n input bytes: the header, the input,
 *        one byte in 8 for the worst-case growth and 16 for the flush.
 *
 * A literal costs at most 9 bits at a probability of 31/2048, the floor
 * a shift of 5 leaves, which is under 9/8 of a byte.
 */
#define LZLIT_BOUND(n) (LZLIT_HEADER + (n) + (n) / 8u + 16u)

/** @brief The encoder's state.  Fields are private to lzlit.c. */
typedef struct {
    uint16_t lit[LZLIT_LIT_PROBS]; /**< literal probabilities, 12,288 bytes */
    uint16_t is_match[4];          /**< state 0, position state 0 to 3 */
    uint64_t low;                  /**< 33 bits: bit 32 is the carry */
    uint32_t range;                /**< the coder's range */
    uint32_t cache_size;           /**< bytes held back for a carry */
    uint8_t *out;                  /**< the caller's output buffer */
    size_t cap;                    /**< its size in bytes */
    size_t n;                      /**< bytes written so far */
    uint32_t pos;                  /**< input bytes coded so far */
    uint8_t cache;                 /**< the byte held back for a carry */
    uint8_t prev;                  /**< the previous input byte */
    bool over;                     /**< the output ran past cap */
} lzlit_t;

/**
 * @brief Start a stream: reset the coder and write the header.
 * @param z       the state
 * @param out     the output buffer; LZLIT_BOUND(raw_len) bytes never overflow
 * @param cap     its size in bytes
 * @param raw_len the exact number of bytes the lzlit_put() calls will give
 */
void lzlit_begin(lzlit_t *z, uint8_t *out, size_t cap, uint32_t raw_len);

/**
 * @brief Code @p n bytes.  Pieces may be of any size, 0 included.
 * @param z    the state
 * @param data the bytes
 * @param n    their count
 */
void lzlit_put(lzlit_t *z, const uint8_t *data, size_t n);

/**
 * @brief Flush the coder.
 * @param z the state
 * @return the stream's length in bytes; 0 when the output buffer was too
 *         small (the buffer then holds a cut stream)
 */
size_t lzlit_end(lzlit_t *z);

#ifdef __cplusplus
}
#endif

#endif /* LZLIT_H */
