/*
 * Keeping a board's photograph. See art_store.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "art_store.h"

#include <stddef.h>
#include <string.h>

#include "link_crc.h"

/*
 * On flash at the start of a slot.
 *
 * Written last and checksummed, so it is what makes a slot findable and a
 * half-written one is not mistaken for a whole one.  The fields are fixed
 * width and the checksum covers everything before it, which is why `head`
 * sits at the end.
 */
#define ART_MAGIC   0x61626372uL   /* "rcba" */
#define ART_VERSION 1u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t board;
    uint16_t width;
    uint16_t height;
    uint16_t crc;      /* of the payload */
    uint16_t pad;      /* keeps bytes aligned and the layout explicit */
    uint32_t bytes;
    uint16_t head;     /* of everything above */
} art_head_t;

static uint16_t head_crc(const art_head_t *h)
{
    return link_crc(0u, h, offsetof(art_head_t, head));
}

static bool usable(const art_flash_t *f)
{
    return f != NULL && f->read != NULL && f->erase != NULL
           && f->write != NULL && f->sector != 0u
           && f->size >= ART_SLOT_BYTES
           && (ART_SLOT_BYTES % f->sector) == 0u
           && f->sector >= sizeof(art_head_t);
}

uint32_t art_store_slots(const art_flash_t *f)
{
    return usable(f) ? (f->size / ART_SLOT_BYTES) : 0u;
}

uint32_t art_store_capacity(const art_flash_t *f)
{
    return usable(f) ? (ART_SLOT_BYTES - f->sector) : 0u;
}

/* The header of slot @p i, if it has one that checks out. */
static bool slot_head(const art_flash_t *f, uint32_t i, art_head_t *out)
{
    art_head_t h;
    if (!f->read(f->ctx, i * ART_SLOT_BYTES, &h, sizeof(h))) {
        return false;
    }
    if (h.magic != ART_MAGIC || h.version != ART_VERSION) {
        return false;
    }
    if (h.head != head_crc(&h)) {
        return false;      /* never finished being written */
    }
    if (h.bytes == 0u || h.bytes > art_store_capacity(f)) {
        return false;      /* a length this slot could not hold */
    }
    *out = h;
    return true;
}

/* Which slot holds @p board, or the count when none does. */
static uint32_t find_slot(const art_flash_t *f, uint16_t board)
{
    const uint32_t n = art_store_slots(f);
    for (uint32_t i = 0; i < n; ++i) {
        art_head_t h;
        if (slot_head(f, i, &h) && h.board == board) {
            return i;
        }
    }
    return n;
}

bool art_store_find(const art_flash_t *f, uint16_t board, art_entry_t *out)
{
    if (!usable(f) || out == NULL) {
        return false;
    }
    const uint32_t i = find_slot(f, board);
    if (i >= art_store_slots(f)) {
        return false;
    }
    art_head_t h;
    if (!slot_head(f, i, &h)) {
        return false;
    }
    out->board  = h.board;
    out->width  = h.width;
    out->height = h.height;
    out->crc    = h.crc;
    out->bytes  = h.bytes;
    return true;
}

bool art_store_read(const art_flash_t *f, uint16_t board, uint8_t *dst,
                    uint32_t cap)
{
    art_entry_t e;
    if (dst == NULL || !art_store_find(f, board, &e) || e.bytes > cap) {
        return false;
    }
    const uint32_t i = find_slot(f, board);
    if (!f->read(f->ctx, i * ART_SLOT_BYTES + f->sector, dst, e.bytes)) {
        return false;
    }
    /*
     * Checked on the way out as well as on the way in.  Flash that decayed
     * since it was written should be a miss, which costs ten seconds of link
     * to fetch again, rather than a picture that is wrong.
     */
    return link_crc(0u, dst, e.bytes) == e.crc;
}

/* A slot to put a picture in: the board's own, else an empty one, else the
 * first -- something has to give, and a store this small is not worth an
 * eviction policy nobody can predict. */
static uint32_t slot_for(const art_flash_t *f, uint16_t board)
{
    const uint32_t n = art_store_slots(f);
    const uint32_t own = find_slot(f, board);
    if (own < n) {
        return own;
    }
    for (uint32_t i = 0; i < n; ++i) {
        art_head_t h;
        if (!slot_head(f, i, &h)) {
            return i;
        }
    }
    return 0u;
}

bool art_store_put(const art_flash_t *f, uint16_t board,
                   const art_entry_t *entry, const uint8_t *payload)
{
    if (!usable(f) || entry == NULL || payload == NULL) {
        return false;
    }
    if (entry->bytes == 0u || entry->bytes > art_store_capacity(f)) {
        return false;
    }
    if (link_crc(0u, payload, entry->bytes) != entry->crc) {
        return false;      /* not the picture it says it is */
    }

    const uint32_t slot = slot_for(f, board);
    const uint32_t at   = slot * ART_SLOT_BYTES;

    /*
     * Erasing takes the old header with it first, so from here until the new
     * one lands the slot is findable as nothing.  That is the state a power
     * cut has to leave behind.
     */
    if (!f->erase(f->ctx, at, ART_SLOT_BYTES)) {
        return false;
    }
    if (!f->write(f->ctx, at + f->sector, payload, entry->bytes)) {
        return false;
    }

    /*
     * Read back before the header is written.  A write that reported success
     * and did not take would otherwise be sealed with a header saying it did,
     * and the store would hand out a corrupt picture for as long as the board
     * is known.
     */
    uint8_t buf[256];
    uint16_t crc = 0u;
    for (uint32_t done = 0; done < entry->bytes; ) {
        uint32_t n = entry->bytes - done;
        if (n > sizeof(buf)) {
            n = sizeof(buf);
        }
        if (!f->read(f->ctx, at + f->sector + done, buf, n)) {
            return false;
        }
        crc = link_crc(crc, buf, n);
        done += n;
    }
    if (crc != entry->crc) {
        return false;
    }

    art_head_t h;
    memset(&h, 0, sizeof(h));
    h.magic   = ART_MAGIC;
    h.version = ART_VERSION;
    h.board   = board;
    h.width   = entry->width;
    h.height  = entry->height;
    h.crc     = entry->crc;
    h.bytes   = entry->bytes;
    h.head    = head_crc(&h);
    return f->write(f->ctx, at, &h, sizeof(h));
}

bool art_store_forget(const art_flash_t *f, uint16_t board)
{
    if (!usable(f)) {
        return false;
    }
    const uint32_t i = find_slot(f, board);
    if (i >= art_store_slots(f)) {
        return true;       /* nothing kept is the state asked for */
    }
    /* The header sector alone: the payload is unreachable without it, and
     * erasing a quarter of a megabyte to forget one board is time the bench
     * does not have to spend. */
    return f->erase(f->ctx, i * ART_SLOT_BYTES, f->sector);
}
