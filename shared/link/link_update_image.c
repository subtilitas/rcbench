/*
 * The picobin block loop, as the RP2350 datasheet sections 5.1.5 and 5.9.1
 * describe it: a block opens with PICOBIN_BLOCK_MARKER_START (0xffffded3), holds
 * items, closes its item list with a LAST item that counts their words, then
 * a word with the signed byte offset of the next block and the marker
 * 0xab123579.  An item's first byte is its type; bit 7 of the type says its
 * size in words is the 16-bit field in bytes 1 and 2 rather than byte 1.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_update_image.h"

#include <stddef.h>
#include <string.h>

#define MARKER_START     0xffffded3u
#define MARKER_END       0xab123579u
#define ITEM_IMAGE_TYPE  0x42u
#define ITEM_LAST        0xffu
#define ITEM_SIZE_2BYTES 0x80u
/* The largest block the boot ROM reads (PICOBIN_MAX_BLOCK_SIZE). */
#define MAX_BLOCK_BYTES  0x280u

static uint32_t word_at(const uint8_t *p, uint32_t at)
{
    return (uint32_t)p[at] | ((uint32_t)p[at + 1u] << 8)
           | ((uint32_t)p[at + 2u] << 16) | ((uint32_t)p[at + 3u] << 24);
}

/*
 * The block at @p at: true when it is whole inside @p len, with the offset of
 * the next block in @p next and of its IMAGE_TYPE item in @p image_type (-1
 * for none).
 */
static bool parse_block(const uint8_t *img, uint32_t len, uint32_t at,
                        int64_t *next, int32_t *image_type)
{
    if (at % 4u != 0u || (uint64_t)at + 16u > len
        || word_at(img, at) != MARKER_START) {
        return false;
    }
    *image_type = -1;
    uint32_t item = at + 4u;
    uint32_t words = 0u;
    for (;;) {
        if ((uint64_t)item + 4u > len || item - at >= MAX_BLOCK_BYTES) {
            return false;
        }
        const uint8_t type = img[item];
        const uint32_t size = (type & ITEM_SIZE_2BYTES)
            ? ((uint32_t)img[item + 1u] | ((uint32_t)img[item + 2u] << 8))
            : (uint32_t)img[item + 1u];
        if (type == ITEM_LAST) {
            /* The LAST item counts the words of the items before it. */
            if (size != words || (uint64_t)item + 12u > len
                || word_at(img, item + 8u) != MARKER_END) {
                return false;
            }
            const int32_t link = (int32_t)word_at(img, item + 4u);
            *next = (int64_t)at + (int64_t)link;
            return true;
        }
        if (size == 0u) {
            return false;   /* an item of no words would never advance */
        }
        if (item == at + 4u && type == ITEM_IMAGE_TYPE && size == 1u) {
            *image_type = (int32_t)item;
        }
        words += size;
        item += size * 4u;
    }
}

int32_t link_update_tbyb_byte(const uint8_t *img, uint32_t len)
{
    if (img == NULL) {
        return -1;
    }
    const uint32_t scan = (len < LINK_IMAGE_SCAN_BYTES) ? len
                                                        : LINK_IMAGE_SCAN_BYTES;
    for (uint32_t first = 0u; first + 16u <= scan; first += 4u) {
        if (word_at(img, first) != MARKER_START) {
            continue;
        }
        /* Follow the loop from here; the last IMAGE_DEF on it is the one
         * the boot ROM uses. */
        int32_t  found = -1;
        uint32_t at = first;
        bool     closed = false;
        for (unsigned n = 0u; n < LINK_IMAGE_MAX_BLOCKS; ++n) {
            int64_t next = 0;
            int32_t image_type = -1;
            if (!parse_block(img, len, at, &next, &image_type)) {
                break;
            }
            if (image_type >= 0) {
                found = image_type;
            }
            if (next == (int64_t)first) {
                closed = true;
                break;
            }
            if (next < 0 || next >= (int64_t)len) {
                break;
            }
            at = (uint32_t)next;
        }
        if (closed && found >= 0) {
            return found + 3;
        }
    }
    return -1;
}

void link_image_hash_begin(link_image_hash_t *h, uint32_t len,
                           int32_t flag_at, bool flag_set)
{
    if (h == NULL) {
        return;
    }
    link_sha256_init(&h->sha);
    h->pos = 0u;
    h->len = len;
    h->flag_at = flag_at;
    h->flag_set = flag_set;
}

static uint8_t flagged(uint8_t v, bool set)
{
    return set ? (uint8_t)(v | LINK_IMAGE_TBYB_BIT)
               : (uint8_t)(v & (uint8_t)~LINK_IMAGE_TBYB_BIT);
}

uint8_t link_image_byte(const uint8_t *img, uint32_t at, int32_t flag_at,
                        bool flag_set)
{
    if (img == NULL) {
        return 0xFFu;
    }
    return (flag_at >= 0 && at == (uint32_t)flag_at)
               ? flagged(img[at], flag_set) : img[at];
}

bool link_image_hash_step(link_image_hash_t *h, const uint8_t *src,
                          uint32_t n)
{
    if (h == NULL) {
        return false;
    }
    if (n > h->len - h->pos) {
        n = h->len - h->pos;
    }
    if (src != NULL && n > 0u) {
        const int64_t rel = (int64_t)h->flag_at - (int64_t)h->pos;
        if (h->flag_at >= 0 && rel >= 0 && rel < (int64_t)n) {
            /* The flag's byte as it is read, between the bytes either side
             * of it. */
            const uint32_t k = (uint32_t)rel;
            const uint8_t flag = flagged(src[k], h->flag_set);
            link_sha256_update(&h->sha, src, k);
            link_sha256_update(&h->sha, &flag, 1u);
            link_sha256_update(&h->sha, src + k + 1u, n - k - 1u);
        } else {
            link_sha256_update(&h->sha, src, n);
        }
        h->pos += n;
    }
    return h->pos >= h->len;
}

void link_image_hash_end(link_image_hash_t *h, uint8_t out[LINK_SHA256_BYTES])
{
    if (h == NULL || out == NULL) {
        return;
    }
    link_sha256_final(&h->sha, out);
}

void link_update_build_hash(const uint8_t *img, uint32_t len,
                            uint8_t out[LINK_SHA256_BYTES])
{
    link_image_hash_t h;
    link_image_hash_begin(&h, len, link_update_tbyb_byte(img, len), false);
    (void)link_image_hash_step(&h, img, len);
    link_image_hash_end(&h, out);
}
