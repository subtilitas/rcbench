/*
 * Assembling a board photograph out of the blocks the link carries it in.
 *
 * The transfer is long -- three thousand-odd round trips for two hundred
 * kilobytes -- and its whole point is that it happens once and the result is
 * kept.  So the failure that matters is not a slow transfer but a quiet one:
 * a block lost and skipped, or a reply arriving for a block other than the
 * one asked for, assembled into a picture that is wrong in a way nobody sees
 * until an operator wires a lead by it.
 *
 * Blocks are therefore taken strictly in order, one at a time, and anything
 * else is refused rather than placed.  The whole payload is checked against
 * the CRC (cyclic redundancy check) the coprocessor declared before any of
 * it counts as the board's picture.
 *
 * Nothing here talks to the wire.  It is fed register windows and says which
 * block to ask for next.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_LINK_ARTXFER_H
#define RCBENCH_LINK_ARTXFER_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What the coprocessor says its picture is, off the ARTWORK page. */
typedef struct {
    uint16_t blocks;
    uint16_t width;
    uint16_t height;
    uint16_t format;   /**< a link_art_format_t                            */
    uint16_t crc;      /**< link_crc() over the payload, seeded zero        */
    uint32_t bytes;
} link_art_meta_t;

typedef struct {
    link_art_meta_t meta;
    uint8_t  *buf;     /**< where the payload lands                        */
    uint32_t  cap;
    uint32_t  at;      /**< bytes assembled so far                         */
    uint16_t  next;    /**< the block to ask for                           */
    bool      begun;
} link_artxfer_t;

/**
 * Whether these registers can describe a picture at all.
 *
 * Everything that does not depend on where the picture would go: a format
 * this build can read, a block count the length implies, and a length that
 * is the pixels claimed. Split out because the caller has to know the size
 * before it can find room, and a page that cannot be a picture should be
 * refused before anything is allocated for it rather than after.
 */
bool link_artxfer_meta_ok(const uint16_t *meta, link_art_meta_t *out);

/**
 * Read the metadata and get ready to assemble into @p buf.
 *
 * Refuses metadata that cannot describe a picture: no blocks, a format this
 * build cannot read, a length that disagrees with the block count or with
 * the pixels claimed, or one longer than @p cap. A transfer that cannot end
 * well should not take ten seconds of link first.
 */
bool link_artxfer_begin(link_artxfer_t *x, const uint16_t *meta,
                        uint8_t *buf, uint32_t cap);

/** Which block to ask for. Meaningless once the transfer is complete. */
uint16_t link_artxfer_next(const link_artxfer_t *x);

/**
 * Place one ART_DATA window.
 *
 * Refuses a window whose LINK_AD_BLOCK is not the one being waited for, and
 * places nothing: a block out of order is a reply to a question that was not
 * asked, and guessing where it belongs is how a hole is filled with the
 * wrong bytes. The caller asks again.
 */
bool link_artxfer_take(link_artxfer_t *x, const uint16_t *data);

/** Every block placed. Says nothing about whether they are the right bytes. */
bool link_artxfer_complete(const link_artxfer_t *x);

/**
 * And they are: the payload matches the CRC the coprocessor declared.
 *
 * Separate from complete() because they fail differently. Incomplete is a
 * transfer to carry on with; complete and wrong is one to throw away, and
 * the difference decides whether anything is written to flash.
 */
bool link_artxfer_verify(const link_artxfer_t *x);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_ARTXFER_H */
