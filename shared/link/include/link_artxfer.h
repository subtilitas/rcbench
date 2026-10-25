/**
 * @file
 * @brief Assembling a board drawing out of the blocks the link carries it in.
 *
 * The transfer happens once and the result is kept.  So the failure that
 * matters is not a slow transfer but a quiet one: a block lost and skipped,
 * or a reply arriving for a block other than the one asked for, assembled
 * into a drawing that is wrong in a way nobody sees until an operator wires
 * a lead by it.
 *
 * Blocks are therefore taken strictly in order, one at a time, and anything
 * else is refused rather than placed.  The whole payload is checked against
 * the CRC (cyclic redundancy check) and the hash the IOmcu declared
 * before any of it counts as the board's drawing.
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

/** @brief What the IOmcu says its drawing is, off the ARTWORK page. */
typedef struct {
    uint16_t blocks;
    uint16_t width;    /**< outline, 0.01mm                                */
    uint16_t height;   /**< outline, 0.01mm                                */
    uint16_t format;   /**< a link_art_format_t                            */
    uint16_t crc;      /**< link_crc() over the payload, seeded zero        */
    uint32_t hash;     /**< link_fnv1a32() over the payload                */
    uint32_t bytes;    /**< the payload's size                             */
} link_art_meta_t;

/**
 * @brief State of one board drawing transfer.
 */
typedef struct {
    link_art_meta_t meta;
    uint8_t  *buf;     /**< where the payload lands                        */
    uint32_t  cap;                      /**< capacity of buf, bytes */
    uint32_t  at;      /**< bytes assembled so far                         */
    uint16_t  next;    /**< the block to ask for                           */
    bool      begun;                    /**< whether the transfer started */
} link_artxfer_t;

/**
 * @brief Whether these LINK_AW_COUNT registers can describe a drawing at all.
 *
 * Everything that does not depend on where the drawing would go: the one
 * format this build reads, LINK_ART_DRAWING; an outline; a length from a
 * drawing header up to LINK_DRAW_MAX_BYTES; and the block count that length
 * implies. Split out because the caller has to know the size before it can
 * find room, and a page that cannot be a drawing should be refused before
 * anything is allocated for it rather than after.
 *
 * @param meta The LINK_AW_COUNT registers of the ARTWORK page.
 * @param out Receives the decoded metadata when the test passes.
 * @return true when the registers describe a drawing, else false.
 */
bool link_artxfer_meta_ok(const uint16_t *meta, link_art_meta_t *out);

/**
 * @brief Read the metadata and get ready to assemble into @p buf.
 *
 * Refuses what link_artxfer_meta_ok() refuses, and a length longer than
 * @p cap. A transfer that cannot end well should not take the link first.
 *
 * @param x The transfer state to set up.
 * @param meta The LINK_AW_COUNT registers of the ARTWORK page.
 * @param buf Receives the payload.
 * @param cap Size of buf, in bytes.
 * @return true when the transfer may start, else false.
 */
bool link_artxfer_begin(link_artxfer_t *x, const uint16_t *meta,
                        uint8_t *buf, uint32_t cap);

/**
 * @brief Which block to ask for.
 *
 * Meaningless once the transfer is complete.
 *
 * @param x The transfer state.
 * @return The block number, or 0 when x is NULL or not begun.
 */
uint16_t link_artxfer_next(const link_artxfer_t *x);

/**
 * @brief Place one ART_DATA window.
 *
 * Refuses a window whose LINK_AD_BLOCK is not the one being waited for, and
 * places nothing: a block out of order is a reply to a question that was not
 * asked, and guessing where it belongs is how a hole is filled with the
 * wrong bytes. The caller asks again.
 *
 * @param x The transfer state.
 * @param data The LINK_AD_COUNT registers of the ART_DATA window.
 * @return true when the block was placed, else false.
 */
bool link_artxfer_take(link_artxfer_t *x, const uint16_t *data);

/**
 * @brief Every block placed.
 *
 * Says nothing about whether they are the right bytes.
 *
 * @param x The transfer state.
 * @return true when all payload bytes are placed.
 */
bool link_artxfer_complete(const link_artxfer_t *x);

/**
 * @brief And they are: the payload matches the CRC and the hash the IOmcu
 * declared.
 *
 * Separate from complete() because they fail differently. Incomplete is a
 * transfer to carry on with; complete and wrong is one to throw away, and
 * the difference decides whether anything is written to flash.
 *
 * @param x The transfer state.
 * @return true when the transfer is complete and both checks match.
 */
bool link_artxfer_verify(const link_artxfer_t *x);

/* --------------------------------------------- the IOmcu's side */

/**
 * @brief The LINK_AW_COUNT registers ARTWORK answers for @p bytes of payload,
 * with the CRC and hash the generator emitted beside it.
 *
 * All zero for no payload: LINK_AW_BLOCKS of zero is how a board says it
 * carries none.
 *
 * @param regs Receives the LINK_AW_COUNT registers; NULL is ignored.
 * @param format The format, a link_art_format_t.
 * @param width Outline width, in 0.01mm.
 * @param height Outline height, in 0.01mm.
 * @param bytes Payload length in bytes.
 * @param crc CRC of the payload.
 * @param hash FNV-1a hash of the payload.
 */
void link_artserve_meta(uint16_t *regs, uint16_t format, uint16_t width,
                        uint16_t height, uint32_t bytes, uint16_t crc,
                        uint32_t hash);

/**
 * @brief The LINK_AD_COUNT registers ART_DATA answers while LINK_AD_BLOCK holds
 * @p block: the block number, then its bytes, the low byte of a register first.
 *
 * A block past the payload carries its number and zeros.
 *
 * @param regs Receives the LINK_AD_COUNT registers; NULL is ignored.
 * @param payload The payload bytes; NULL leaves the data zero.
 * @param bytes Payload length in bytes.
 * @param block Block number requested.
 */
void link_artserve_block(uint16_t *regs, const uint8_t *payload,
                         uint32_t bytes, uint16_t block);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_ARTXFER_H */
