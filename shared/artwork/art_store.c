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

/*
 * What art_store_put() and art_keep_begin() both refuse before anything is
 * written: an entry this flash cannot hold, or whose pixels are not its
 * length.  A header saying 500 x 206 over a payload that is not that many
 * bytes would be believed by whatever sized a buffer from it later; sixty-four
 * bits because two sixteen-bit sides multiplied are not sixteen bits.  An
 * empty picture is refused by its sides: no two of them that are not zero
 * make zero bytes.
 */
static bool entry_fits(const art_flash_t *f, const art_entry_t *e)
{
    return e->bytes <= art_store_capacity(f)
           && e->width != 0u && e->height != 0u
           && (uint64_t)e->width * (uint64_t)e->height * 2u
                  == (uint64_t)e->bytes;
}

bool art_store_put(const art_flash_t *f, uint16_t board,
                   const art_entry_t *entry, const uint8_t *payload)
{
    if (!usable(f) || entry == NULL || payload == NULL
        || !entry_fits(f, entry)) {
        return false;
    }
    if (link_crc(0u, payload, entry->bytes) != entry->crc) {
        return false;      /* not the picture it says it is */
    }
    art_keep_t k;
    if (!art_keep_begin(&k, f, board, entry, payload)) {
        return false;
    }
    art_keep_status_t st;
    do {
        st = art_keep_step(&k, true);
    } while (st == ART_KEEP_RUNNING);
    return st == ART_KEEP_DONE;
}

/* ------------------------------------------------- a keep, a sector at a time */

/*
 * The phases of a keep, in the order they run.  Erasing takes the old header
 * with it first, so from the first step until the new one lands the slot is
 * findable as nothing.  That is the state a power cut, or a keep that is
 * never resumed, has to leave behind.
 */
enum {
    KEEP_IDLE = 0,
    KEEP_ERASE,     /* the header's sector, then the payload's            */
    KEEP_WRITE,     /* the payload, a sector at a time                    */
    KEEP_VERIFY,    /* read back, a sector at a time, against the CRC     */
    KEEP_HEAD,      /* the header, last                                   */
};

bool art_keep_begin(art_keep_t *k, const art_flash_t *f, uint16_t board,
                    const art_entry_t *entry, const uint8_t *payload)
{
    if (k == NULL) {
        return false;
    }
    memset(k, 0, sizeof(*k));
    if (!usable(f) || entry == NULL || payload == NULL
        || !entry_fits(f, entry)) {
        return false;
    }
    k->f       = f;
    k->payload = payload;
    k->entry   = *entry;
    k->board   = board;
    /* The header's sector and the sectors the payload reaches into; the
     * rest of the slot is never read, so it is not erased. */
    k->span    = f->sector
                 + ((entry->bytes + f->sector - 1u) / f->sector) * f->sector;
    k->phase   = KEEP_ERASE;
    return true;
}

bool art_keep_busy(const art_keep_t *k)
{
    return k != NULL && k->phase != KEEP_IDLE;
}

/* The first step chooses the slot, and erases its header with the same
 * call's first sector. */
static bool keep_erase(art_keep_t *k)
{
    const art_flash_t *f = k->f;
    if (k->done == 0u) {
        k->at = slot_for(f, k->board) * ART_SLOT_BYTES;
    }
    if (!f->erase(f->ctx, k->at + k->done, f->sector)) {
        return false;
    }
    k->done += f->sector;
    if (k->done >= k->span) {
        k->phase = KEEP_WRITE;
        k->done  = 0u;
    }
    return true;
}

static bool keep_write(art_keep_t *k)
{
    const art_flash_t *f = k->f;
    uint32_t n = k->entry.bytes - k->done;
    if (n > f->sector) {
        n = f->sector;
    }
    if (!f->write(f->ctx, k->at + f->sector + k->done, k->payload + k->done,
                  n)) {
        return false;
    }
    k->done += n;
    if (k->done >= k->entry.bytes) {
        k->phase = KEEP_VERIFY;
        k->done  = 0u;
        k->crc   = 0u;
    }
    return true;
}

/*
 * Read back before the header is written.  A write that reported success and
 * did not take would otherwise be sealed with a header saying it did, and the
 * store would hand out a corrupt picture for as long as the board is known.
 */
static bool keep_verify(art_keep_t *k)
{
    const art_flash_t *f = k->f;
    uint32_t end = k->done + f->sector;
    if (end > k->entry.bytes) {
        end = k->entry.bytes;
    }
    while (k->done < end) {
        uint32_t n = end - k->done;
        if (n > sizeof(k->buf)) {
            n = sizeof(k->buf);
        }
        if (!f->read(f->ctx, k->at + f->sector + k->done, k->buf, n)) {
            return false;
        }
        k->crc = link_crc(k->crc, k->buf, n);
        k->done += n;
    }
    if (k->done >= k->entry.bytes) {
        if (k->crc != k->entry.crc) {
            return false;
        }
        k->phase = KEEP_HEAD;
    }
    return true;
}

static bool keep_head(art_keep_t *k)
{
    const art_flash_t *f = k->f;
    art_head_t h;
    memset(&h, 0, sizeof(h));
    h.magic   = ART_MAGIC;
    h.version = ART_VERSION;
    h.board   = k->board;
    h.width   = k->entry.width;
    h.height  = k->entry.height;
    h.crc     = k->entry.crc;
    h.bytes   = k->entry.bytes;
    h.head    = head_crc(&h);
    return f->write(f->ctx, k->at, &h, sizeof(h));
}

art_keep_status_t art_keep_step(art_keep_t *k, bool may_write)
{
    if (!art_keep_busy(k)) {
        return ART_KEEP_IDLE;
    }
    if (!may_write) {
        return ART_KEEP_WAITING;
    }
    bool ok;
    switch (k->phase) {
    case KEEP_ERASE:
        ok = keep_erase(k);
        break;
    case KEEP_WRITE:
        ok = keep_write(k);
        break;
    case KEEP_VERIFY:
        ok = keep_verify(k);
        break;
    default:
        ok = keep_head(k);
        k->phase = KEEP_IDLE;
        return ok ? ART_KEEP_DONE : ART_KEEP_FAILED;
    }
    if (!ok) {
        k->phase = KEEP_IDLE;
        return ART_KEEP_FAILED;
    }
    return ART_KEEP_RUNNING;
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
