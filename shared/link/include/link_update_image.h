/**
 * @file
 * @brief What both ends of the IOmcu update need to know about an RP2350 image:
 * where its TBYB (try before you buy) flag is, and the hash that names a build
 * whatever that flag says.
 *
 * An RP2350 image carries its metadata in a loop of picobin blocks, the first
 * inside its first 4 KiB.  The boot ROM uses the last IMAGE_DEF block of the
 * loop, and that block's IMAGE_TYPE item holds the TBYB flag in bit 15 of its
 * flags: byte 3 of the item, bit 7.  The build seals the image with a SHA-256
 * hash in that last block, computed with the flag read as clear, so the flag
 * can be set after the build without breaking the hash.
 *
 * No image this build makes carries the flag.  The panel sets it in the copy it
 * sends, and the boot ROM clears it in flash when the IOmcu buys the image.  A
 * build is therefore named by the SHA-256 of its bytes with that one bit read
 * as clear: the image in the release, the copy on trial and the copy bought all
 * hash the same.
 *
 * Pure C, fed a pointer and a length.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_UPDATE_IMAGE_H
#define RCBENCH_LINK_UPDATE_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "link_sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The first block of the loop starts in the image's first 4 KiB.
 */
#define LINK_IMAGE_SCAN_BYTES 4096u
/**
 * @brief Blocks followed before a loop that has not closed is given up on.
 */
#define LINK_IMAGE_MAX_BLOCKS 8u
/**
 * @brief The TBYB flag's bit in the byte link_update_tbyb_byte() names.
 */
#define LINK_IMAGE_TBYB_BIT 0x80u

/**
 * @brief The byte offset of the TBYB flag in @p img: byte 3 of the IMAGE_TYPE
 * item of the last IMAGE_DEF block of the block loop.
 *
 * -1 when the first 4 KiB hold no block loop that closes inside @p len bytes,
 * or no block of it is an IMAGE_DEF.
 *
 * @param img The image bytes; NULL gives -1.
 * @param len Length of img in bytes.
 * @return The offset of the flag byte, or -1.
 */
int32_t link_update_tbyb_byte(const uint8_t *img, uint32_t len);

/**
 * @brief A SHA-256 over @p len bytes fed in pieces, the byte at @p flag_at read
 * with LINK_IMAGE_TBYB_BIT set or clear as @p flag_set says (-1: every byte as
 * it is).
 */
typedef struct {
    link_sha256_t sha;
    uint32_t      pos;
    uint32_t      len;
    int32_t       flag_at;
    bool          flag_set;
} link_image_hash_t;

/**
 * @brief Start a hash of an image.
 *
 * @param h The hash state to set up; NULL is ignored.
 * @param len Length of the image in bytes.
 * @param flag_at Offset of the flag byte, or -1 for none.
 * @param flag_set true to set the TBYB bit at flag_at, false to clear it.
 */
void link_image_hash_begin(link_image_hash_t *h, uint32_t len,
                           int32_t flag_at, bool flag_set);

/**
 * @brief The byte at @p at of @p img as the hash and the transfer read it: the
 * TBYB bit set or cleared at @p flag_at, as @p flag_set says.
 *
 * @param img The image bytes; NULL gives 0xFF.
 * @param at Offset of the byte.
 * @param flag_at Offset of the flag byte, or -1 for none.
 * @param flag_set true to set the TBYB bit at flag_at, false to clear it.
 * @return The byte, with the TBYB bit forced when at is flag_at.
 */
uint8_t link_image_byte(const uint8_t *img, uint32_t at, int32_t flag_at,
                        bool flag_set);

/**
 * @brief Fold in up to @p n bytes from @p src, which holds the image's bytes
 * from h->pos on.
 *
 * No more than the bytes left are taken.  True once all @p len bytes are in.
 *
 * @param h The hash state, from link_image_hash_begin().
 * @param src The next bytes of the image; NULL takes nothing.
 * @param n Number of bytes offered in src.
 * @return true once all len bytes are in.
 */
bool link_image_hash_step(link_image_hash_t *h, const uint8_t *src,
                          uint32_t n);

/**
 * @brief Write the digest; only meaningful once link_image_hash_step() said so.
 *
 * @param h The hash state.
 * @param out Receives the 32-byte digest.
 */
void link_image_hash_end(link_image_hash_t *h,
                         uint8_t out[LINK_SHA256_BYTES]);

/**
 * @brief The SHA-256 that names the build in @p img: every byte, the TBYB flag
 * read as clear.
 *
 * In one call, for an image in memory.
 *
 * @param img Image bytes.
 * @param len Length of img in bytes.
 * @param out Receives the 32-byte digest.
 */
void link_update_build_hash(const uint8_t *img, uint32_t len,
                            uint8_t out[LINK_SHA256_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_UPDATE_IMAGE_H */
