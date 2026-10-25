/**
 * @file
 * @brief Assembling a board drawing.
 *
 * See link_artxfer.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_artxfer.h"

#include <stddef.h>
#include <string.h>

#include "link_crc.h"

/**
 * @brief How many blocks a payload of this many bytes takes, rounded up.
 *
 * @param bytes Payload length in bytes.
 * @return Blocks of LINK_AD_BYTES needed for bytes.
 */
static uint32_t blocks_for(uint32_t bytes)
{
    return (bytes + LINK_AD_BYTES - 1u) / LINK_AD_BYTES;
}

bool link_artxfer_meta_ok(const uint16_t *meta, link_art_meta_t *out)
{
    if (meta == NULL || out == NULL) {
        return false;
    }
    link_art_meta_t m;
    m.blocks = meta[LINK_AW_BLOCKS];
    m.width  = meta[LINK_AW_WIDTH];
    m.height = meta[LINK_AW_HEIGHT];
    m.format = meta[LINK_AW_FORMAT];
    m.crc    = meta[LINK_AW_CRC];
    m.hash   = (uint32_t)meta[LINK_AW_HASH_LO]
               | ((uint32_t)meta[LINK_AW_HASH_HI] << 16);
    m.bytes  = (uint32_t)meta[LINK_AW_BYTES_LO]
               | ((uint32_t)meta[LINK_AW_BYTES_HI] << 16);

    if (m.blocks == 0u || m.bytes == 0u) {
        return false;              /* this board carries no drawing */
    }
    if (m.format != (uint16_t)LINK_ART_DRAWING) {
        return false;              /* a format this build does not read */
    }
    if (m.width == 0u || m.height == 0u) {
        return false;              /* a drawing of no outline */
    }
    /*
     * A header at least, and no more than the largest drawing the format
     * allows: the length sizes an allocation before a byte of it is seen.
     */
    if (m.bytes < LINK_DRAW_HEAD_BYTES || m.bytes > LINK_DRAW_MAX_BYTES) {
        return false;
    }
    /*
     * The block count has to be the one the length implies.  An IOmcu
     * that disagrees with itself is one whose blocks would run out early or
     * late, and either way the drawing is not what it says it is.
     */
    if ((uint32_t)m.blocks != blocks_for(m.bytes)) {
        return false;
    }
    *out = m;
    return true;
}

bool link_artxfer_begin(link_artxfer_t *x, const uint16_t *meta,
                        uint8_t *buf, uint32_t cap)
{
    if (x == NULL || meta == NULL || buf == NULL) {
        return false;
    }
    memset(x, 0, sizeof(*x));

    link_art_meta_t m;
    if (!link_artxfer_meta_ok(meta, &m)) {
        return false;
    }
    if (m.bytes > cap) {
        return false;              /* nowhere to put it */
    }

    x->meta  = m;
    x->buf   = buf;
    x->cap   = cap;
    x->at    = 0u;
    x->next  = 0u;
    x->begun = true;
    return true;
}

uint16_t link_artxfer_next(const link_artxfer_t *x)
{
    return (x != NULL && x->begun) ? x->next : 0u;
}

bool link_artxfer_take(link_artxfer_t *x, const uint16_t *data)
{
    if (x == NULL || data == NULL || !x->begun) {
        return false;
    }
    if (x->at >= x->meta.bytes) {
        return false;              /* already whole; there is no next block */
    }
    if (data[LINK_AD_BLOCK] != x->next) {
        /* An answer to a question that was not asked.  Placing it would put
         * bytes where a lost block belongs. */
        return false;
    }

    uint32_t left = x->meta.bytes - x->at;
    if (left > LINK_AD_BYTES) {
        left = LINK_AD_BYTES;
    }
    for (uint32_t i = 0; i < left; ++i) {
        const uint16_t w = data[LINK_AD_DATA + (i / 2u)];
        /* Low byte first, as LINK_PAGE_ART_DATA lays it out. */
        x->buf[x->at + i] = (uint8_t)((i & 1u) ? (w >> 8) : (w & 0xFFu));
    }
    x->at += left;
    ++x->next;
    return true;
}

bool link_artxfer_complete(const link_artxfer_t *x)
{
    return x != NULL && x->begun && x->at == x->meta.bytes;
}

bool link_artxfer_verify(const link_artxfer_t *x)
{
    if (!link_artxfer_complete(x)) {
        return false;
    }
    return link_crc(0u, x->buf, (size_t)x->meta.bytes) == x->meta.crc
           && link_fnv1a32(LINK_FNV_INIT, x->buf, (size_t)x->meta.bytes)
                  == x->meta.hash;
}

void link_artserve_meta(uint16_t *regs, uint16_t format, uint16_t width,
                        uint16_t height, uint32_t bytes, uint16_t crc,
                        uint32_t hash)
{
    if (regs == NULL) {
        return;
    }
    memset(regs, 0, sizeof(uint16_t) * (size_t)LINK_AW_COUNT);
    if (bytes == 0u) {
        return;
    }
    regs[LINK_AW_BLOCKS]   = (uint16_t)blocks_for(bytes);
    regs[LINK_AW_WIDTH]    = width;
    regs[LINK_AW_HEIGHT]   = height;
    regs[LINK_AW_FORMAT]   = format;
    regs[LINK_AW_BYTES_LO] = (uint16_t)(bytes & 0xFFFFu);
    regs[LINK_AW_BYTES_HI] = (uint16_t)(bytes >> 16);
    regs[LINK_AW_CRC]      = crc;
    regs[LINK_AW_HASH_LO]  = (uint16_t)(hash & 0xFFFFu);
    regs[LINK_AW_HASH_HI]  = (uint16_t)(hash >> 16);
}

void link_artserve_block(uint16_t *regs, const uint8_t *payload,
                         uint32_t bytes, uint16_t block)
{
    if (regs == NULL) {
        return;
    }
    memset(regs, 0, sizeof(uint16_t) * (size_t)LINK_AD_COUNT);
    regs[LINK_AD_BLOCK] = block;
    if (payload == NULL) {
        return;
    }
    const uint32_t at = (uint32_t)block * LINK_AD_BYTES;
    for (uint32_t i = 0; i < LINK_AD_BYTES && at < bytes && i < bytes - at;
         ++i) {
        const uint16_t b = payload[at + i];
        regs[LINK_AD_DATA + i / 2u] |= (uint16_t)((i & 1u) ? (b << 8) : b);
    }
}
