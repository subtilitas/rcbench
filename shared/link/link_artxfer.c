/*
 * Assembling a board photograph. See link_artxfer.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_artxfer.h"

#include <stddef.h>
#include <string.h>

#include "link_crc.h"

/* How many blocks a payload of this many bytes takes, rounded up. */
static uint32_t blocks_for(uint32_t bytes)
{
    return (bytes + LINK_AD_BYTES - 1u) / LINK_AD_BYTES;
}

bool link_artxfer_begin(link_artxfer_t *x, const uint16_t *meta,
                        uint8_t *buf, uint32_t cap)
{
    if (x == NULL || meta == NULL || buf == NULL) {
        return false;
    }
    memset(x, 0, sizeof(*x));

    link_art_meta_t m;
    m.blocks = meta[LINK_AW_BLOCKS];
    m.width  = meta[LINK_AW_WIDTH];
    m.height = meta[LINK_AW_HEIGHT];
    m.format = meta[LINK_AW_FORMAT];
    m.crc    = meta[LINK_AW_CRC];
    m.bytes  = (uint32_t)meta[LINK_AW_BYTES_LO]
               | ((uint32_t)meta[LINK_AW_BYTES_HI] << 16);

    if (m.blocks == 0u || m.bytes == 0u) {
        return false;              /* this board carries no picture */
    }
    if (m.format != (uint16_t)LINK_ART_RGB565) {
        return false;              /* a format this build cannot read */
    }
    /*
     * The block count has to be the one the length implies.  A coprocessor
     * that disagrees with itself is one whose blocks would run out early or
     * late, and either way the picture is not what it says it is.
     */
    if ((uint32_t)m.blocks != blocks_for(m.bytes)) {
        return false;
    }
    /*
     * And the length has to be the pixels it claims: two bytes each.
     *
     * In sixty-four bits, because the two are sixteen bits each and their
     * product is not: 65535 x 32769 x 2 overflows a uint32_t and wraps to
     * 65534, which is a length that agrees with a plausible block count.
     * Such a page would pass, assemble 65534 bytes with a good CRC, and
     * hand back something calling itself a 65535 x 32769 image -- and
     * whatever drew it would read four gigabytes past the buffer.
     */
    if ((uint64_t)m.width * (uint64_t)m.height * 2u != (uint64_t)m.bytes) {
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
        /* Low byte first: the order an RGB565 framebuffer is already in. */
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
    return link_crc(0u, x->buf, (size_t)x->meta.bytes) == x->meta.crc;
}
