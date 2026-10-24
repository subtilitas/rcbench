/**
 * @file
 * @brief SHA-256 (Secure Hash Algorithm, 256 bit), FIPS 180-4, in portable C.
 *
 * The panel's application image carries the SHA-256 of everything before it
 * in its last 32 bytes; the update checks it before the first erase.  Written
 * here rather than taken from mbedTLS so the check is the same code on the
 * host and on the panel, and the host suite can feed it the cases a card
 * produces.  Speed is not the point: a 1.2MB image is hashed once per
 * update.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_SHA256_H
#define RCBENCH_FW_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FW_SHA256_BYTES 32u

typedef struct {
    uint32_t h[8];
    uint64_t bits;        /**< message length so far */
    uint8_t  block[64];   /**< the block being filled */
    uint32_t fill;        /**< bytes in @c block */
} fw_sha256_t;

void fw_sha256_init(fw_sha256_t *s);
void fw_sha256_add(fw_sha256_t *s, const void *data, size_t n);
void fw_sha256_done(fw_sha256_t *s, uint8_t out[FW_SHA256_BYTES]);

/** The digest of @p n bytes at @p data in one call. */
void fw_sha256(const void *data, size_t n, uint8_t out[FW_SHA256_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_SHA256_H */
