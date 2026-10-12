/*
 * Keeping a board's photograph, so it is fetched once and not every time the
 * link comes up.
 *
 * The picture costs about ten seconds of link. That is fine once for a board
 * that has never been seen and absurd on every connect, so it is written to
 * a partition of the panel's own flash and looked up by the identity the
 * coprocessor reports.
 *
 * Fixed slots rather than a packed heap. A slot is erased, filled, and only
 * then given a header; nothing has to be moved when one is replaced, and the
 * store cannot be left half compacted by a power cut. The cost is the tail of
 * a slot a smaller picture does not use, which buys simplicity in code that
 * runs while a bench is live.
 *
 * The order of a write is the whole point: payload, read it back, and write
 * the header last. A header is what makes a slot findable, so a picture
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

/** One slot: a sector of header and the picture after it. */
#define ART_SLOT_BYTES  (256u * 1024u)

/**
 * The flash a store lives in.
 *
 * @p erase and @p write take offsets from the start of the partition. Erase
 * is whole sectors; write is anywhere in an erased one. Any of them may fail
 * and saying so is not optional: a store that believes a write that did not
 * happen is a store that hands back a picture that is not there.
 */
typedef struct {
    bool (*read)(void *ctx, uint32_t off, void *dst, uint32_t len);
    bool (*erase)(void *ctx, uint32_t off, uint32_t len);
    bool (*write)(void *ctx, uint32_t off, const void *src, uint32_t len);
    void    *ctx;
    uint32_t size;    /**< bytes of partition                              */
    uint32_t sector;  /**< erase granularity, 4096 on this part            */
} art_flash_t;

/** What a slot says it holds. */
typedef struct {
    uint16_t board;
    uint16_t width;
    uint16_t height;
    uint16_t crc;     /**< over the payload                                */
    uint32_t bytes;
} art_entry_t;

/** How many pictures this partition has room for. */
uint32_t art_store_slots(const art_flash_t *f);

/** The most bytes a picture may be for this partition to hold it. */
uint32_t art_store_capacity(const art_flash_t *f);

/**
 * What is kept for @p board, without reading the picture itself.
 *
 * False when nothing is, which includes a slot whose header did not survive
 * being written. The header carries its own checksum, so a half-written one
 * is not mistaken for a whole one.
 */
bool art_store_find(const art_flash_t *f, uint16_t board, art_entry_t *out);

/**
 * Read the picture for @p board into @p dst.
 *
 * Checks the payload against the checksum the header carries, so a picture
 * that decayed in flash is a miss rather than a wrong picture. False when
 * there is nothing kept, when @p cap cannot hold it, or when it does not
 * check out.
 */
bool art_store_read(const art_flash_t *f, uint16_t board, uint8_t *dst,
                    uint32_t cap);

/**
 * Keep @p payload for @p board, replacing anything kept for it already.
 *
 * The payload is read back and checked before the header is written, so a
 * slot only becomes findable once what is in it is known to be right.
 * Returns false and leaves nothing findable for the board if any of that
 * fails -- including when it fails part way, which is what an unplugged
 * board mid-transfer looks like.
 */
bool art_store_put(const art_flash_t *f, uint16_t board,
                   const art_entry_t *entry, const uint8_t *payload);

/** Forget what is kept for @p board. True even when nothing was. */
bool art_store_forget(const art_flash_t *f, uint16_t board);

/* ------------------------------------------------- a keep, a sector at a time */

/**
 * Bytes read back per read while a keep checks what it wrote.  The buffer is
 * in the keeper, so a step costs its caller's stack no more than its frame.
 */
#define ART_KEEP_READ_BYTES 256u

/** What a step of a keep did; see art_keep_step(). */
typedef enum {
    ART_KEEP_IDLE = 0,  /**< nothing is being kept                          */
    ART_KEEP_WAITING,   /**< not allowed to write: nothing was touched      */
    ART_KEEP_RUNNING,   /**< one operation done, more to come               */
    ART_KEEP_DONE,      /**< the header is written: the picture is findable */
    ART_KEEP_FAILED,    /**< an operation failed: nothing findable for it   */
} art_keep_status_t;

/**
 * The write art_store_put() does, cut into steps of at most one sector each,
 * so a caller can stop between any two and carry on later.
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
    uint8_t            buf[ART_KEEP_READ_BYTES];
} art_keep_t;

/**
 * Set @p k up to keep @p payload for @p board.  Touches no flash.
 *
 * False, with @p k idle, for what art_store_put() refuses before it writes,
 * except the payload's checksum: computing it here is a pass over the whole
 * picture in one call, and the read-back checks the same thing before the
 * header is written.  A caller that has not already checked the payload
 * (art_fetch_step() does) uses art_store_put().
 */
bool art_keep_begin(art_keep_t *k, const art_flash_t *f, uint16_t board,
                    const art_entry_t *entry, const uint8_t *payload);

/**
 * One step, when @p may_write is true: at most one sector erased, written
 * or read back, or the header written.  When it is false, WAITING, and no
 * flash is touched.
 *
 * DONE and FAILED are returned once; the keep is idle after them and a
 * further step returns IDLE.
 */
art_keep_status_t art_keep_step(art_keep_t *k, bool may_write);

/** Whether @p k has a keep under way, waiting or not. */
bool art_keep_busy(const art_keep_t *k);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ART_STORE_H */
