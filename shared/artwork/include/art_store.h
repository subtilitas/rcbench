/**
 * @file
 * @brief Keeping a board's drawing, so it is fetched once and not every time
 *        the link comes up.
 *
 * The drawing is written to a partition of the panel's own flash with the
 * identity the IOmcu reports and everything its ARTWORK page says
 * about the payload: format, outline, length, CRC (cyclic redundancy check)
 * and hash.  The kept drawing is the board's only while all of those still
 * match the page, so an IOmcu flashed with another drawing under the
 * same board ID is fetched again instead of shown from the cache.
 *
 * Fixed slots rather than a packed heap. A slot is erased, filled, and only
 * then given a header; nothing has to be moved when one is replaced, and the
 * store cannot be left half compacted by a power cut. The cost is the tail of
 * a slot a smaller drawing does not use, which buys simplicity in code that
 * runs while a bench is live.
 *
 * The order of a write is the whole point: payload, read it back, and write
 * the header last. A header is what makes a slot findable, so a drawing
 * interrupted by an unplugged board or a power cut is never found -- rather
 * than found and wrong, which is the failure that would survive in flash
 * until somebody wired a lead by it.
 *
 * Nothing here knows about esp_partition. The flash is passed in, so the
 * whole store runs on the host against memory.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_ART_STORE_H
#define RCBENCH_ART_STORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One slot: a sector of header and the payload after it. */
#define ART_SLOT_BYTES  (16u * 1024u)

/**
 * @brief The flash a store lives in.
 *
 * @p erase and @p write take offsets from the start of the partition. Erase
 * is whole sectors; write is anywhere in an erased one. Any of them may fail
 * and saying so is not optional: a store that believes a write that did not
 * happen is a store that hands back a drawing that is not there.
 */
typedef struct {
    bool (*read)(void *ctx, uint32_t off, void *dst, uint32_t len);
    bool (*erase)(void *ctx, uint32_t off, uint32_t len);
    bool (*write)(void *ctx, uint32_t off, const void *src, uint32_t len);
    void    *ctx;
    uint32_t size;    /**< bytes of partition                              */
    uint32_t sector;  /**< erase granularity, 4096 on this part            */
} art_flash_t;

/**
 * @brief What a slot says it holds: the board and the ARTWORK page it came
 *        from.
 */
typedef struct {
    uint16_t board;
    uint16_t format;  /**< a link_art_format_t, never LINK_ART_NONE         */
    uint16_t width;
    uint16_t height;
    uint16_t crc;     /**< link_crc() over the payload, seeded zero        */
    uint32_t hash;    /**< link_fnv1a32() over the payload                 */
    uint32_t bytes;
} art_entry_t;

/**
 * @brief Whether two entries describe the same payload: every field equal.
 *
 * The content key of the cache -- a kept drawing is shown only for an ARTWORK
 * page that says exactly what the entry says.
 *
 * @param a First entry.
 * @param b Second entry.
 * @return true when every field is equal.
 */
bool art_entry_same(const art_entry_t *a, const art_entry_t *b);

/**
 * @brief How many drawings this partition has room for.
 *
 * @param f Flash the store lives in.
 * @return The number of slots.
 */
uint32_t art_store_slots(const art_flash_t *f);

/**
 * @brief The most bytes a drawing may be for this partition to hold it.
 *
 * @param f Flash the store lives in.
 * @return The largest drawing in bytes.
 */
uint32_t art_store_capacity(const art_flash_t *f);

/**
 * @brief What is kept for @p board, without reading the drawing itself.
 *
 * False when nothing is, which includes a slot whose header did not survive
 * being written. The header carries its own checksum, so a half-written one
 * is not mistaken for a whole one.
 *
 * @param f Flash the store lives in.
 * @param board Board ID.
 * @param out Receives the entry.
 * @return true when a drawing is kept for @p board.
 */
bool art_store_find(const art_flash_t *f, uint16_t board, art_entry_t *out);

/**
 * @brief Read the drawing for @p board into @p dst.
 *
 * Checks the payload against the CRC and the hash the header carries, so a
 * drawing that decayed in flash is a miss rather than a wrong drawing. False
 * when there is nothing kept, when @p cap cannot hold it, or when it does
 * not check out.
 *
 * @param f Flash the store lives in.
 * @param board Board ID.
 * @param dst Buffer to read the drawing into.
 * @param cap Size of @p dst in bytes.
 * @return true when the drawing was read and checks out.
 */
bool art_store_read(const art_flash_t *f, uint16_t board, uint8_t *dst,
                    uint32_t cap);

/**
 * @brief Read the drawing @p key describes into @p dst: only when the entry
 *        kept for key->board equals @p key in every field (art_entry_same())
 *        and the payload checks out.
 *
 * False for nothing kept, for another drawing kept under the same board, and
 * for one that decayed -- each a fetch, never a stale or wrong drawing.
 *
 * @param f Flash the store lives in.
 * @param key Entry the kept drawing must equal.
 * @param dst Buffer to read the drawing into.
 * @param cap Size of @p dst in bytes.
 * @return true when the drawing was read and checks out.
 */
bool art_store_read_same(const art_flash_t *f, const art_entry_t *key,
                         uint8_t *dst, uint32_t cap);

/**
 * @brief Keep @p payload for @p board, replacing anything kept for it already.
 *
 * The payload is read back and checked before the header is written, so a
 * slot only becomes findable once what is in it is known to be right.
 * Returns false and leaves nothing findable for the board if any of that
 * fails -- including when it fails part way, which is what an unplugged
 * board mid-transfer looks like.
 *
 * @param f Flash the store lives in.
 * @param board Board ID.
 * @param entry Entry describing the payload.
 * @param payload Payload bytes.
 * @return true when the drawing is kept.
 */
bool art_store_put(const art_flash_t *f, uint16_t board,
                   const art_entry_t *entry, const uint8_t *payload);

/**
 * @brief Forget what is kept for @p board.
 *
 * True even when nothing was.
 *
 * @param f Flash the store lives in.
 * @param board Board ID.
 * @return true when nothing is kept for @p board afterwards.
 */
bool art_store_forget(const art_flash_t *f, uint16_t board);

/* ------------------------------------------------- a keep, a sector at a time */

/**
 * @brief Bytes read back per read while a keep checks what it wrote.
 *
 * The buffer is in the keeper, so a step costs its caller's stack no more than
 * its frame.
 */
#define ART_KEEP_READ_BYTES 256u

/** @brief What a step of a keep did; see art_keep_step(). */
typedef enum {
    ART_KEEP_IDLE = 0,  /**< nothing is being kept                          */
    ART_KEEP_WAITING,   /**< not allowed to write: nothing was touched      */
    ART_KEEP_RUNNING,   /**< one operation done, more to come               */
    ART_KEEP_DONE,      /**< the header is written: the drawing is findable */
    ART_KEEP_FAILED,    /**< an operation failed: nothing findable for it   */
} art_keep_status_t;

/**
 * @brief The write art_store_put() does, cut into steps of at most one sector
 *        each, so a caller can stop between any two and carry on later.
 *
 * The steps, in order: choose the slot; erase each sector the header and the
 * payload occupy, the header's first; write the payload a sector at a time;
 * read it back a sector at a time against the entry's checksum; write the
 * header.  The header's sector is erased before anything else is written
 * and the header is written last, so a keep stopped between any two steps --
 * or never resumed, by a power cut -- leaves nothing findable for the board.
 * Resumed, it carries on from the step it stopped before: what it already
 * wrote is still in flash, because nothing else writes the slot in between.
 *
 * Owned by one caller; the payload must stay valid and unchanged until the
 * keep is DONE or FAILED.
 */
typedef struct {
    const art_flash_t *f;
    const uint8_t     *payload;
    art_entry_t        entry;
    uint16_t           board;
    uint8_t            phase;    /**< see art_store.c                        */
    uint32_t           at;       /**< the slot's offset                      */
    uint32_t           span;     /**< bytes from the slot's start to erase   */
    uint32_t           done;     /**< bytes of the phase done                */
    uint16_t           crc;      /**< of what has been read back so far      */
    uint32_t           hash;     /**< the same, link_fnv1a32()               */
    uint8_t            buf[ART_KEEP_READ_BYTES];
} art_keep_t;

/**
 * @brief Set @p k up to keep @p payload for @p board.
 *
 * Touches no flash.
 *
 * False, with @p k idle, for what art_store_put() refuses before it writes,
 * except the payload's checksums: computing them here is a pass over the
 * whole payload in one call, and the read-back checks the same thing before
 * the header is written.  A caller that has not already checked the payload
 * (art_fetch_step() does) uses art_store_put().
 *
 * @param k Keep state to set up.
 * @param f Flash the store lives in.
 * @param board Board ID.
 * @param entry Entry describing the payload.
 * @param payload Payload bytes; must stay valid until the keep ends.
 * @return true when the keep may start.
 */
bool art_keep_begin(art_keep_t *k, const art_flash_t *f, uint16_t board,
                    const art_entry_t *entry, const uint8_t *payload);

/**
 * @brief One step, when @p may_write is true: at most one sector erased,
 *        written or read back, or the header written.
 *
 * When it is false, WAITING, and no flash is touched.
 *
 * DONE and FAILED are returned once; the keep is idle after them and a
 * further step returns IDLE.
 *
 * @param k Keep state.
 * @param may_write true when a flash write is allowed now.
 * @return The status after this step.
 */
art_keep_status_t art_keep_step(art_keep_t *k, bool may_write);

/**
 * @brief Whether @p k has a keep under way, waiting or not.
 *
 * @param k Keep state.
 * @return true while a keep is under way.
 */
bool art_keep_busy(const art_keep_t *k);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ART_STORE_H */
