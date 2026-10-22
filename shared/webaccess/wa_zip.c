/*
 * The card as a ZIP archive. See wa_zip.h.
 *
 * Layouts from PKWARE's APPNOTE.TXT 6.3.10: local file header 4.3.7, data
 * descriptor 4.3.9, central directory header 4.3.12, ZIP64 end of central
 * directory record 4.3.14 and locator 4.3.15, end of central directory
 * record 4.3.16, ZIP64 extended information 4.5.3.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_zip.h"

#include <string.h>

#define SIG_LOCAL    0x04034b50u
#define SIG_DESC     0x08074b50u
#define SIG_CENTRAL  0x02014b50u
#define SIG_END64    0x06064b50u
#define SIG_LOC64    0x07064b50u
#define SIG_END      0x06054b50u

/* Bit 3: sizes and CRC in a descriptor after the data.  Bit 11: UTF-8. */
#define FLAGS        0x0808u
#define VERSION_20   20u
#define VERSION_45   45u
#define ATTR_DIR     0x10u

enum { PH_ENTRIES = 0, PH_CENTRAL, PH_END, PH_DONE, PH_FAILED };

/* ------------------------------------------------------------- CRC */

static uint32_t s_table[256];
static bool     s_table_made;

uint32_t wa_crc32(uint32_t crc, const void *data, size_t n)
{
    if (!s_table_made) {
        for (uint32_t i = 0; i < 256u; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            s_table[i] = c;
        }
        s_table_made = true;
    }
    const uint8_t *p = data;
    crc = ~crc;
    while (n-- > 0u) {
        crc = s_table[(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

/* ------------------------------------------------------------- time */

void wa_dos_time(int64_t t, uint16_t *time, uint16_t *date)
{
    /* 1980-01-01 and 2107-12-31 23:59:58, in seconds since 1970. */
    if (t < 315532800) {
        t = 315532800;
    }
    if (t > 4354819198) {
        t = 4354819198;
    }
    const wa_civil_t c = wa_civil(t);
    *date = (uint16_t)(((c.year - 1980) << 9) | (c.month << 5) | c.day);
    *time = (uint16_t)((c.hour << 11) | (c.min << 5) | (c.sec / 2));
}

/* ------------------------------------------------------------- pieces */

static uint8_t *put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    return p + 2;
}

static uint8_t *put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    return p + 4;
}

static uint8_t *put64(uint8_t *p, uint64_t v)
{
    p = put32(p, (uint32_t)v);
    return put32(p, (uint32_t)(v >> 32));
}

static uint32_t clamp32(uint64_t v)
{
    return (v >= WA_ZIP_MAX32) ? WA_ZIP_MAX32 : (uint32_t)v;
}

bool wa_zip_entry64(uint64_t size)
{
    return size >= WA_ZIP_MAX32;
}

bool wa_zip_end64(uint64_t entries, uint64_t cd_bytes, uint64_t cd_offset)
{
    return entries >= WA_ZIP_MAX16 || cd_bytes >= WA_ZIP_MAX32
           || cd_offset >= WA_ZIP_MAX32;
}

uint32_t wa_zip_local_bytes(size_t name_len, uint64_t size)
{
    return 30u + (uint32_t)name_len + (wa_zip_entry64(size) ? 20u : 0u);
}

uint32_t wa_zip_desc_bytes(uint64_t size)
{
    return wa_zip_entry64(size) ? 24u : 16u;
}

/* Whether a central header carries its sizes in the ZIP64 field.  An
 * entry past 4 GiB carries them too, set to the mark in the header: Info-ZIP
 * unzip 6.0 reads the field by the sizes of the entry before when it lists,
 * and an entry of exactly 0xFFFFFFFF bytes then shifts the next one's. */
static bool central_sizes64(uint64_t size, uint64_t offset)
{
    return wa_zip_entry64(size) || offset >= WA_ZIP_MAX32;
}

static unsigned central_fields(uint64_t size, uint64_t offset)
{
    return (central_sizes64(size, offset) ? 2u : 0u)
           + ((offset >= WA_ZIP_MAX32) ? 1u : 0u);
}

uint32_t wa_zip_central_bytes(size_t name_len, uint64_t size, uint64_t offset)
{
    const unsigned k = central_fields(size, offset);
    return 46u + (uint32_t)name_len + ((k > 0u) ? 4u + 8u * k : 0u);
}

uint32_t wa_zip_end_bytes(uint64_t entries, uint64_t cd_bytes,
                          uint64_t cd_offset)
{
    return 22u + (wa_zip_end64(entries, cd_bytes, cd_offset) ? 56u + 20u : 0u);
}

size_t wa_zip_local(uint8_t *out, const char *name, size_t name_len,
                    uint64_t size, int64_t mtime, bool dir)
{
    (void)dir;
    uint16_t t, d;
    wa_dos_time(mtime, &t, &d);
    const bool z64 = wa_zip_entry64(size);
    uint8_t *p = put32(out, SIG_LOCAL);
    p = put16(p, z64 ? VERSION_45 : VERSION_20);
    p = put16(p, FLAGS);
    p = put16(p, 0u);                    /* stored */
    p = put16(p, t);
    p = put16(p, d);
    p = put32(p, 0u);                    /* CRC: in the descriptor */
    p = put32(p, z64 ? WA_ZIP_MAX32 : 0u);
    p = put32(p, z64 ? WA_ZIP_MAX32 : 0u);
    p = put16(p, (uint32_t)name_len);
    p = put16(p, z64 ? 20u : 0u);
    memcpy(p, name, name_len);
    p += name_len;
    if (z64) {
        /* The sizes follow in the descriptor, 8 bytes each. */
        p = put16(p, 0x0001u);
        p = put16(p, 16u);
        p = put64(p, 0u);
        p = put64(p, 0u);
    }
    return (size_t)(p - out);
}

size_t wa_zip_desc(uint8_t *out, uint32_t crc, uint64_t size)
{
    uint8_t *p = put32(out, SIG_DESC);
    p = put32(p, crc);
    if (wa_zip_entry64(size)) {
        p = put64(p, size);
        p = put64(p, size);
    } else {
        p = put32(p, (uint32_t)size);
        p = put32(p, (uint32_t)size);
    }
    return (size_t)(p - out);
}

size_t wa_zip_central(uint8_t *out, const char *name, size_t name_len,
                      uint64_t size, uint32_t crc, uint64_t offset,
                      int64_t mtime, bool dir)
{
    uint16_t t, d;
    wa_dos_time(mtime, &t, &d);
    const unsigned k = central_fields(size, offset);
    const bool big = central_sizes64(size, offset);
    uint8_t *p = put32(out, SIG_CENTRAL);
    p = put16(p, VERSION_45);            /* made by: MS-DOS, 4.5 */
    p = put16(p, (k > 0u) ? VERSION_45 : VERSION_20);
    p = put16(p, FLAGS);
    p = put16(p, 0u);
    p = put16(p, t);
    p = put16(p, d);
    p = put32(p, crc);
    p = put32(p, big ? WA_ZIP_MAX32 : (uint32_t)size);
    p = put32(p, big ? WA_ZIP_MAX32 : (uint32_t)size);
    p = put16(p, (uint32_t)name_len);
    p = put16(p, (k > 0u) ? 4u + 8u * k : 0u);
    p = put16(p, 0u);                    /* comment */
    p = put16(p, 0u);                    /* disk */
    p = put16(p, 0u);                    /* internal attributes */
    p = put32(p, dir ? ATTR_DIR : 0u);
    p = put32(p, clamp32(offset));
    memcpy(p, name, name_len);
    p += name_len;
    if (k > 0u) {
        p = put16(p, 0x0001u);
        p = put16(p, 8u * k);
        if (big) {
            p = put64(p, size);
            p = put64(p, size);
        }
        if (offset >= WA_ZIP_MAX32) {
            p = put64(p, offset);
        }
    }
    return (size_t)(p - out);
}

size_t wa_zip_end(uint8_t *out, uint64_t entries, uint64_t cd_bytes,
                  uint64_t cd_offset)
{
    uint8_t *p = out;
    if (wa_zip_end64(entries, cd_bytes, cd_offset)) {
        const uint64_t end64_at = cd_offset + cd_bytes;
        p = put32(p, SIG_END64);
        p = put64(p, 44u);
        p = put16(p, VERSION_45);
        p = put16(p, VERSION_45);
        p = put32(p, 0u);
        p = put32(p, 0u);
        p = put64(p, entries);
        p = put64(p, entries);
        p = put64(p, cd_bytes);
        p = put64(p, cd_offset);
        p = put32(p, SIG_LOC64);
        p = put32(p, 0u);
        p = put64(p, end64_at);
        p = put32(p, 1u);
    }
    const uint32_t n16 = (entries >= WA_ZIP_MAX16) ? WA_ZIP_MAX16
                                                   : (uint32_t)entries;
    p = put32(p, SIG_END);
    p = put16(p, 0u);
    p = put16(p, 0u);
    p = put16(p, n16);
    p = put16(p, n16);
    p = put32(p, clamp32(cd_bytes));
    p = put32(p, clamp32(cd_offset));
    p = put16(p, 0u);
    return (size_t)(p - out);
}

/* ------------------------------------------------------------- length */

void wa_zip_len_init(wa_zip_len_t *l)
{
    memset(l, 0, sizeof(*l));
}

void wa_zip_len_add(wa_zip_len_t *l, size_t name_len, uint64_t size)
{
    l->cd += wa_zip_central_bytes(name_len, size, l->at);
    l->at += wa_zip_local_bytes(name_len, size) + size
             + wa_zip_desc_bytes(size);
    ++l->entries;
}

uint64_t wa_zip_len_total(const wa_zip_len_t *l)
{
    return l->at + l->cd + wa_zip_end_bytes(l->entries, l->cd, l->at);
}

/* ------------------------------------------------------------- walks */

/* The entry a walk event stands for: its name in the walk's path buffer,
 * a folder's with "/" added; false for the root and the walk's end. */
static bool entry_of(const wa_walk_t *w, wa_walk_ev_t ev, size_t *len,
                     bool *dir)
{
    if (ev != WA_WALK_DIR && ev != WA_WALK_FILE) {
        return false;
    }
    size_t n = strlen(w->path);
    if (ev == WA_WALK_DIR) {
        if (n == 0u) {
            return false;    /* the root is not an entry */
        }
        /* The buffer holds WA_PATH_MAX + 1 bytes; the "/" goes where the
         * terminator was and is taken off again on the next event. */
        *dir = true;
        *len = n + 1u;
        return true;
    }
    *dir = false;
    *len = n;
    return true;
}

/**
 * @brief FNV-1a hash of the path of the entry a walk is at.
 * @param w   the walk, its path without a folder's "/"
 * @param dir whether the entry is a folder, hashed as a "/" after the path
 * @return The hash.
 */
static uint32_t path_hash(const wa_walk_t *w, bool dir)
{
    uint32_t h = 2166136261u;
    for (const char *p = w->path; *p != '\0'; ++p) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    return dir ? (h ^ (uint8_t)'/') * 16777619u : h;
}

/**
 * @brief The first walk: add up the archive's length, and keep each entry's
 *        size and path hash when @p slot is given.
 * @param w     the walk to use
 * @param fs    the card
 * @param out   the plan, filled
 * @param slot  the slots, or NULL to keep nothing
 * @param slots how many @p slot holds
 * @return WA_ZIP_OK; WA_ZIP_NO_CARD or WA_ZIP_READ when the walk fails;
 *         WA_ZIP_MEMORY when @p slot is given and the card has more entries
 *         than @p slots.  Nothing is left open.
 */
static wa_zip_err_t plan_walk(wa_walk_t *w, const wa_fs_t *fs,
                              wa_zip_plan_t *out, wa_zip_slot_t *slot,
                              uint64_t slots)
{
    memset(out, 0, sizeof(*out));
    wa_walk_begin(w, fs);
    wa_zip_len_t l;
    wa_zip_len_init(&l);
    for (;;) {
        const wa_walk_ev_t ev = wa_walk_next(w);
        if (ev == WA_WALK_END) {
            break;
        }
        if (ev == WA_WALK_NO_CARD) {
            return WA_ZIP_NO_CARD;
        }
        if (ev == WA_WALK_ERROR) {
            return WA_ZIP_READ;
        }
        size_t len;
        bool dir;
        if (!entry_of(w, ev, &len, &dir)) {
            continue;
        }
        const uint64_t size = dir ? 0u : w->ent.size;
        if (slot != NULL) {
            if (l.entries >= slots) {
                wa_walk_end(w);
                return WA_ZIP_MEMORY;
            }
            slot[l.entries] = (wa_zip_slot_t){ size, 0u, path_hash(w, dir) };
        }
        wa_zip_len_add(&l, len, size);
        if (!dir) {
            ++out->files;
            out->file_bytes += size;
        }
    }
    out->entries   = l.entries;
    out->cd_offset = l.at;
    out->bytes     = wa_zip_len_total(&l);
    out->left_out  = w->left_out;
    return WA_ZIP_OK;
}

wa_zip_err_t wa_zip_plan(const wa_fs_t *fs, wa_zip_plan_t *out)
{
    static wa_walk_t w;   /* 1.6 kB: off the calling task's stack */
    return plan_walk(&w, fs, out, NULL, 0u);
}

wa_zip_err_t wa_zip_begin(wa_zip_t *z, const wa_fs_t *fs, wa_zip_slot_t *slot,
                          uint64_t slots)
{
    memset(z, 0, sizeof(*z));
    z->fs   = fs;
    z->slot = slot;
    const wa_zip_err_t e = plan_walk(&z->walk, fs, &z->plan, slot,
                                     (slot != NULL) ? slots : 0u);
    if (e == WA_ZIP_OK && slot == NULL && z->plan.entries > 0u) {
        z->phase = PH_FAILED;
        z->err   = WA_ZIP_MEMORY;
        return WA_ZIP_MEMORY;
    }
    if (e != WA_ZIP_OK) {
        z->phase = PH_FAILED;
        z->err   = e;
        return e;
    }
    z->phase = PH_ENTRIES;
    wa_walk_begin(&z->walk, fs);
    return WA_ZIP_OK;
}

void wa_zip_end_early(wa_zip_t *z)
{
    if (z->file != NULL) {
        z->fs->close(z->fs->ctx, z->file);
        z->file = NULL;
    }
    wa_walk_end(&z->walk);
}

static int32_t fail(wa_zip_t *z, wa_zip_err_t e)
{
    wa_zip_end_early(z);
    z->phase = PH_FAILED;
    z->err   = e;
    return -1;
}

static wa_zip_err_t walk_err(wa_walk_ev_t ev)
{
    return (ev == WA_WALK_NO_CARD) ? WA_ZIP_NO_CARD : WA_ZIP_READ;
}

/* Queue a piece; the caller sends it before anything else. */
static void queue(wa_zip_t *z, size_t n)
{
    z->piece_len = n;
    z->piece_off = 0u;
}

/**
 * @brief The slot of the entry a later walk is at.
 * @param z   the archive, its walk at a file or folder
 * @param dir whether the entry is a folder
 * @return The next slot when the entry is the one the first walk found
 *         there; NULL for an entry added since, which the walk passes by.
 */
static wa_zip_slot_t *planned(wa_zip_t *z, bool dir)
{
    if (z->index >= z->plan.entries
        || z->slot[z->index].path != path_hash(&z->walk, dir)) {
        return NULL;
    }
    return &z->slot[z->index];
}

/**
 * @brief The next piece of the second walk: a local header, a folder's
 *        with its descriptor, and a file opened for its data.
 * @param z the archive
 * @return true with a piece queued or the third walk begun; false with
 *         @p z->err set.
 */
static bool next_entry(wa_zip_t *z)
{
    for (;;) {
        const wa_walk_ev_t ev = wa_walk_next(&z->walk);
        if (ev == WA_WALK_END) {
            if (z->index != z->plan.entries || z->at != z->plan.cd_offset) {
                z->err = WA_ZIP_READ;   /* an entry of the first walk is gone */
                return false;
            }
            z->phase = PH_CENTRAL;
            wa_walk_begin(&z->walk, z->fs);
            return true;
        }
        if (ev == WA_WALK_NO_CARD || ev == WA_WALK_ERROR) {
            z->err = walk_err(ev);
            return false;
        }
        size_t len;
        bool dir;
        if (!entry_of(&z->walk, ev, &len, &dir)) {
            continue;
        }
        wa_zip_slot_t *slot = planned(z, dir);
        if (slot == NULL) {
            continue;
        }
        char *name = z->walk.path;
        if (dir) {
            name[len - 1u] = '/';
        }
        const uint64_t size = slot->size;
        size_t n = wa_zip_local(z->piece, name, len, size, z->walk.ent.mtime,
                                dir);
        if (dir) {
            name[len - 1u] = '\0';
            n += wa_zip_desc(z->piece + n, 0u, 0u);
            slot->crc = 0u;
            ++z->index;
            queue(z, n);
            return true;
        }
        z->size     = size;
        z->left     = size;
        z->file_crc = 0u;
        z->in_file  = true;
        if (size > 0u) {
            z->file = z->fs->open(z->fs->ctx, name);
            if (z->file == NULL) {
                z->err = WA_ZIP_READ;
                return false;
            }
        }
        queue(z, n);
        return true;
    }
}

/**
 * @brief The next piece of the third walk: a central header, or the end.
 * @param z the archive
 * @return true with a piece queued; false with @p z->err set.
 */
static bool next_central(wa_zip_t *z)
{
    for (;;) {
        const wa_walk_ev_t ev = wa_walk_next(&z->walk);
        if (ev == WA_WALK_END) {
            if (z->index != z->plan.entries) {
                z->err = WA_ZIP_READ;
                return false;
            }
            queue(z, wa_zip_end(z->piece, z->plan.entries, z->cd_bytes,
                                z->plan.cd_offset));
            z->phase = PH_END;
            return true;
        }
        if (ev == WA_WALK_NO_CARD || ev == WA_WALK_ERROR) {
            z->err = walk_err(ev);
            return false;
        }
        size_t len;
        bool dir;
        if (!entry_of(&z->walk, ev, &len, &dir)) {
            continue;
        }
        const wa_zip_slot_t *slot = planned(z, dir);
        if (slot == NULL) {
            continue;
        }
        char *name = z->walk.path;
        if (dir) {
            name[len - 1u] = '/';
        }
        const uint64_t size = slot->size;
        const size_t n = wa_zip_central(z->piece, name, len, size, slot->crc,
                                        z->cd_at, z->walk.ent.mtime, dir);
        if (dir) {
            name[len - 1u] = '\0';
        }
        z->cd_at += wa_zip_local_bytes(len, size) + size
                    + wa_zip_desc_bytes(size);
        z->cd_bytes += n;
        ++z->index;
        queue(z, n);
        return true;
    }
}

/**
 * @brief Fail the archive part way through a call of wa_zip_next().
 * @param z   the archive
 * @param e   why
 * @param got bytes already in the caller's buffer
 * @return @p got when there are some, all inside the announced length: they
 *         are the archive's up to the failure and go out, and the next call
 *         returns -1.  Otherwise -1.
 */
static int32_t fail_after(wa_zip_t *z, wa_zip_err_t e, uint32_t got)
{
    (void)fail(z, e);
    return (got > 0u && z->at <= z->plan.bytes) ? (int32_t)got : -1;
}

int32_t wa_zip_next(wa_zip_t *z, uint8_t *buf, uint32_t cap)
{
    if (z->phase == PH_FAILED) {
        return -1;
    }
    uint32_t got = 0u;
    while (got < cap) {
        if (z->piece_off < z->piece_len) {
            size_t n = z->piece_len - z->piece_off;
            if (n > cap - got) {
                n = cap - got;
            }
            memcpy(buf + got, z->piece + z->piece_off, n);
            z->piece_off += n;
            got += (uint32_t)n;
            z->at += n;
            continue;
        }
        if (z->at > z->plan.bytes) {
            return fail_after(z, WA_ZIP_READ, got);
        }
        if (z->in_file) {
            if (z->left > 0u) {
                uint32_t want = cap - got;
                if (want > z->left) {
                    want = (uint32_t)z->left;
                }
                const int32_t r = z->fs->read(z->fs->ctx, z->file, buf + got,
                                              want);
                if (r <= 0 || (uint32_t)r > want) {
                    return fail_after(z, WA_ZIP_READ, got);
                }
                z->file_crc = wa_crc32(z->file_crc, buf + got, (size_t)r);
                got += (uint32_t)r;
                z->at += (uint32_t)r;
                z->left -= (uint32_t)r;
                continue;
            }
            if (z->file != NULL) {
                z->fs->close(z->fs->ctx, z->file);
                z->file = NULL;
            }
            z->in_file = false;
            z->slot[z->index++].crc = z->file_crc;
            queue(z, wa_zip_desc(z->piece, z->file_crc, z->size));
            continue;
        }
        if (z->phase == PH_ENTRIES) {
            if (!next_entry(z)) {
                return fail_after(z, z->err, got);
            }
            if (z->phase == PH_CENTRAL) {
                z->index = 0u;
            }
            continue;
        }
        if (z->phase == PH_CENTRAL) {
            if (!next_central(z)) {
                return fail_after(z, z->err, got);
            }
            continue;
        }
        if (z->phase == PH_END) {
            z->phase = PH_DONE;
            if (z->at != z->plan.bytes) {
                /* The end record of an archive of another length than
                 * announced does not go out: -1 now. */
                return fail(z, WA_ZIP_READ);
            }
        }
        break;
    }
    return (int32_t)got;
}
