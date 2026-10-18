/*
 * SHA-256 (Secure Hash Algorithm, 256-bit digest), as FIPS (Federal
 * Information Processing Standard) 180-4 section 6.2 states it.
 *
 * The IOmcu update checks an image with it at both ends: the panel
 * over the image it sends, the IOmcu over what it wrote to flash.  One
 * routine in shared/link, so the two ends and the host suite compute the
 * same function and the suite holds it to the standard's test vectors.
 *
 * Fed in pieces of any length; the state is 108 bytes and no allocation.
 * Portable C, no hardware unit used on either chip.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_SHA256_H
#define RCBENCH_LINK_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes in a digest. */
#define LINK_SHA256_BYTES 32u

typedef struct {
    uint32_t h[8];       /**< the running hash value H(i)                */
    uint64_t length;     /**< bytes fed so far                            */
    uint8_t  block[64];  /**< the message block being filled              */
} link_sha256_t;

void link_sha256_init(link_sha256_t *s);

/** Fold @p len bytes in.  @p data may be NULL when @p len is 0. */
void link_sha256_update(link_sha256_t *s, const void *data, size_t len);

/** Pad, finish and write the 32-byte digest.  @p s is spent afterwards. */
void link_sha256_final(link_sha256_t *s, uint8_t out[LINK_SHA256_BYTES]);

/** The digest of @p len bytes in one call. */
void link_sha256(const void *data, size_t len, uint8_t out[LINK_SHA256_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_SHA256_H */
