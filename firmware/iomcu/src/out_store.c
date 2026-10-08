/*
 * The output configuration in flash.  See out_store.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_store.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#include "link_crc.h"
#include "out_store_map.h"

/*
 * Where it goes: the last two sectors of the first four megabytes.
 *
 * Not PICO_FLASH_SIZE_BYTES.  The build's default board file is a
 * pimoroni_pico_plus2_rp2350 and claims sixteen megabytes, while the module
 * the bring-up runs on has four; a sector placed from that number would be
 * past the end of the part actually fitted, and an erase off the end of a
 * flash chip is not a diagnosable failure.  Four megabytes is the smaller of
 * the two and is inside both, and 4 MB - 8 kB is far past an image that is
 * under 300 kB.
 *
 * Two sectors rather than one.  A record is only ever written into an erased
 * slot and a sector is only ever erased while the live record is in the other
 * one, so a power cut during either leaves the binding from before the save
 * readable.  See out_store_map.h.
 */
#define STORE_SIZE_BYTES  (4u * 1024u * 1024u)
#define STORE_SECTORS     OUT_STORE_SECTORS
#define STORE_RECORDS     (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)
#define STORE_SLOTS       (STORE_SECTORS * STORE_RECORDS)
#define STORE_OFFSET      (STORE_SIZE_BYTES - STORE_SECTORS * FLASH_SECTOR_SIZE)

/*
 * Held at compile time as well as in the paragraph above.
 *
 * This catches a board file with less flash than the store needs -- a
 * -DPICO_BOARD for a smaller module would otherwise put the sectors past the
 * end and the erase would go somewhere nobody can diagnose.
 *
 * It cannot catch the mismatch that actually exists: the board file claims
 * sixteen megabytes and the module fitted has four, and no compiler can see
 * a part it is not told about. That direction is guarded after the link
 * instead, by the image-size check in this directory's CMakeLists.txt.
 */
_Static_assert(STORE_SIZE_BYTES <= PICO_FLASH_SIZE_BYTES,
               "the output store is past the end of this board's flash");
_Static_assert(STORE_SLOTS <= 255u,
               "the store has more slots than a slot index holds");

#define STORE_MAGIC    0x7263626FuL    /* "rcbo" */
/* Written at OUT_STORE_VERSION; versions 3 and 4 are still read, so the
 * output bindings and the supply's wiring an earlier build saved survive
 * the update (out_store_rec.h). */
#define STORE_VERSION  OUT_STORE_VERSION

/*
 * One flash page holds a record, so a save is one program.  The checksum is
 * the link's, because a half-written record and a corrupt frame are the same
 * problem and there is no reason for two answers to it.
 *
 * The checksum is not what says the record is whole, though, and a power cut
 * can leave one that is not.  Two ways, and they are opposites:
 *
 *   During an erase.  Bits lift towards 0xFF, so a superseded record's
 *   sequence number can grow past the live one's and outrank it.
 *   During a program.  Bits fall towards their value, so a record whose
 *   header has landed and whose configuration has not ranks above the record
 *   before it and carries a configuration that is part 0xFF.
 *
 * A 16-bit checksum makes each unlikely rather than impossible: the corrupted
 * record has to happen to check out, which is one chance in 65,536 per
 * candidate, over an enormous number of candidates.  This is the field a
 * guarantee was claimed for, so it gets one.
 *
 * zeros is how many 0 bits the record holds from crc onwards, written with
 * the record and stored beside its own complement.  Every partial write is
 * then caught by arithmetic rather than by luck:
 *
 *   an erase sets bits, so a partly erased record holds fewer 0 bits than
 *   zeros says;
 *   a program clears bits, so a partly programmed record has not reached the
 *   count yet either.
 *
 * Both are "fewer zeros than the record claims", and the claim cannot be
 * faked: for zeros and zeros_inv to still exclusive-or to 0xFFFFFFFF, both
 * words have to hold exactly what they were written with.  An erase would
 * have to clear a bit to compensate for one it set, and a program would have
 * to set one to compensate for one it cleared; neither can do the other's
 * work.
 *
 * The checksum stays for what it is good at: a bit that changed for a reason
 * other than an interrupted write.
 */
typedef struct {
    uint32_t magic;
    uint32_t zeros;           /**< 0 bits from crc onwards, as written */
    uint32_t zeros_inv;       /**< ~zeros; no partial write keeps the pair */
    uint16_t crc;             /**< over every byte from version onwards */
    uint16_t version;
    uint32_t seq;
    out_store_t cfg;
} record_t;

_Static_assert(sizeof(record_t) <= FLASH_PAGE_SIZE,
               "the record has outgrown one flash page");

/* Every version's header is this one's, and its configuration starts at
 * cfg and runs out_store_cfg_size() bytes: the checksum and the zero count
 * cover a record to its own end, so its size is that.  cfg is the last
 * field and ends the struct with no padding after it. */
_Static_assert(offsetof(record_t, cfg) + sizeof(out_store_t)
               == sizeof(record_t),
               "the configuration ends the record");

/* How many bytes a record of @p version spans, or 0 for one not read. */
static size_t record_size(uint16_t version)
{
    const size_t cfg = out_store_cfg_size(version);
    return (cfg == 0u) ? 0u : offsetof(record_t, cfg) + cfg;
}

static bool        s_pending;
static uint32_t    s_asked_ms;
static out_store_t s_want;
static bool        s_have_saved;
static out_store_t s_saved;
static uint8_t     s_last_record;

/*
 * How long the last erase and the last program each took, with interrupts
 * off.
 *
 * Two numbers rather than one, because they answer different questions.  The
 * program is what every save costs; the erase is what one save in sixteen
 * costs, or none of them when out_store_reclaim() has been given a quiet
 * moment first.  Measured rather than compensated for: what a window does to
 * the link is decided by its length, and the length belongs to the flash
 * part.
 */
static uint32_t    s_last_erase_us;
static uint32_t    s_last_program_us;

/*
 * Whether the store is worth looking at for a sector to erase.
 *
 * True at boot and after every record written, false once a look has found
 * nothing.  Without it out_store_reclaim() would read eight kilobytes of
 * flash on every pass of a loop that turns over in microseconds, to answer a
 * question that changes once per sixteen saves.
 */
static bool        s_reclaim_wanted = true;

/*
 * The staged page, as a union rather than a byte array with a cast.  The
 * record has 32-bit fields and a bare uint8_t array carries no alignment, so
 * writing through a pointer to it is a store the compiler is entitled to
 * assume is aligned when it is not.
 */
static union {
    uint8_t  bytes[FLASH_PAGE_SIZE];
    record_t rec;
} s_page;

static uint16_t record_crc(const record_t *r, size_t size)
{
    return link_crc(LINK_CRC_INIT, &r->version,
                    size - offsetof(record_t, version));
}

/*
 * The 0 bits from crc onwards: everything the record carries except the count
 * itself and the magic that says the slot has been written at all.
 */
static uint32_t record_zeros(const record_t *r, size_t size)
{
    const uint8_t *p = (const uint8_t *)(const void *)r
                       + offsetof(record_t, crc);
    const size_t n = size - offsetof(record_t, crc);
    uint32_t zeros = 0u;
    for (size_t i = 0; i < n; ++i) {
        uint8_t v = (uint8_t)~p[i];
        while (v != 0u) {
            zeros += (uint32_t)(v & 1u);
            v = (uint8_t)(v >> 1);
        }
    }
    return zeros;
}

static const record_t *record_at(uint8_t slot)
{
    return (const record_t *)(const void *)
           (XIP_BASE + STORE_OFFSET + (uint32_t)slot * FLASH_PAGE_SIZE);
}

/*
 * What is in the store, read out of flash rather than remembered.
 *
 * Eight kilobytes of cached reads per call, and a call happens only when
 * something is about to be written.  Holding the answer in RAM instead would
 * be a second copy of the truth that a torn write could disagree with.
 */
static void survey(out_store_rec_t *recs)
{
    for (uint8_t i = 0; i < STORE_SLOTS; ++i) {
        const uint8_t *p = (const uint8_t *)(const void *)record_at(i);
        bool erased = true;
        for (size_t b = 0; b < FLASH_PAGE_SIZE; ++b) {
            if (p[b] != 0xFFu) {
                erased = false;
                break;
            }
        }
        const record_t *r = record_at(i);
        const size_t size = erased ? 0u : record_size(r->version);
        recs[i].erased = erased;
        recs[i].valid  = !erased
                         && r->magic == STORE_MAGIC
                         && size != 0u
                         && out_store_intact(r->zeros, r->zeros_inv,
                                             record_zeros(r, size))
                         && r->crc == record_crc(r, size);
        recs[i].seq    = recs[i].valid ? r->seq : 0u;
    }
}

/*
 * One erase or one program, as flash_safe_execute() runs it: interrupts off
 * on this core, and core 1 parked in RAM by the multicore lock-out, for the
 * whole operation.  The flash cannot be read while it is being written, and
 * both cores execute from it.  That is also why none of this runs while the
 * bank is driving -- the caller's loop takes nothing off the CAN controller
 * and steps no output for the length of the window, so a disarm or a
 * command in flight waits for it.  Core 1 reads no sensor for it either,
 * and the bank is idle, so no run loses a reading.
 *
 * Both timestamps are inside the window, because the window is what is being
 * measured: taken either side of it they would also count the lock-out and
 * the restore, and the number is meant to be the time this core answered
 * nothing.  The timer is a peripheral and reads with interrupts off.
 */
typedef struct {
    uint32_t off;
    bool     erase;       /* a sector erase; else a page program of s_page */
    bool     done;        /* the operation ran                             */
    uint32_t window_us;
} flash_op_t;

static void flash_op_run(void *arg)
{
    flash_op_t *op = (flash_op_t *)arg;
    const absolute_time_t began = get_absolute_time();
    if (op->erase) {
        flash_range_erase(op->off, FLASH_SECTOR_SIZE);
    } else {
        flash_range_program(op->off, s_page.bytes, FLASH_PAGE_SIZE);
    }
    op->window_us = (uint32_t)absolute_time_diff_us(began, get_absolute_time());
    op->done = true;
}

/* When core 1 last failed to stop, and whether that is recent. */
static bool     s_refused;
static uint32_t s_refused_ms;

/* No window may open this boot: core 1 is not a lock-out victim
 * (out_store_off()).  Checked in front of every flash_safe_execute(). */
static bool     s_off;

void out_store_off(void)
{
    s_off     = true;
    s_pending = false;
}

bool out_store_is_off(void)
{
    return s_off;
}

static uint32_t clock_ms(void)
{
    return (uint32_t)to_ms_since_boot(get_absolute_time());
}

static bool refusing(void)
{
    if (s_refused
        && (uint32_t)(clock_ms() - s_refused_ms) >= OUT_STORE_REFUSED_WAIT_MS) {
        s_refused = false;
    }
    return s_refused;
}

/*
 * True when the operation ran.  flash_safe_execute() answers a timeout on
 * the way in (nothing ran) and on the way out (it ran) with the same code,
 * so the operation says for itself.
 */
static bool flash_window(flash_op_t *op)
{
    op->done = false;
    if (s_off) {
        return false;             /* the gate: never into the lock-out */
    }
    (void)flash_safe_execute(flash_op_run, op, OUT_STORE_LOCKOUT_MS);
    if (!op->done) {
        s_refused    = true;
        s_refused_ms = clock_ms();
    }
    return op->done;
}

static bool erase_sector(uint8_t sector, uint32_t *window_us)
{
    flash_op_t op = {
        .off   = STORE_OFFSET + (uint32_t)sector * FLASH_SECTOR_SIZE,
        .erase = true,
    };
    if (!flash_window(&op)) {
        return false;
    }
    *window_us = op.window_us;
    return true;
}

static bool program_record(uint8_t slot, uint32_t *window_us)
{
    flash_op_t op = {
        .off   = STORE_OFFSET + (uint32_t)slot * FLASH_PAGE_SIZE,
        .erase = false,
    };
    if (!flash_window(&op)) {
        return false;
    }
    *window_us = op.window_us;
    return true;
}

bool out_store_load(out_store_t *out)
{
    if (out == NULL) {
        return false;
    }
    out_store_rec_t recs[STORE_SLOTS];
    survey(recs);
    const int newest = out_store_map_newest(recs, STORE_SECTORS,
                                            STORE_RECORDS);
    if (newest < 0) {
        return false;
    }
    const record_t *r = record_at((uint8_t)newest);
    /* Valid, so its version is one out_store_cfg_size() knows. */
    if (!out_store_cfg_read(r->version, &r->cfg, out)) {
        return false;
    }
    s_saved = *out;
    s_have_saved = true;
    s_last_record = (uint8_t)newest;
    return true;
}

void out_store_save(const out_store_t *cfg, uint32_t now_ms)
{
    if (cfg == NULL || s_off) {
        return;                   /* off: the set-up lives in RAM only */
    }
    /* A record written to say what is already saved is a slot spent on
     * nothing, and the store is written every time an operator ticks a
     * pin. */
    if (s_have_saved && memcmp(&s_saved, cfg, sizeof(*cfg)) == 0) {
        s_pending = false;
        return;
    }
    s_want = *cfg;
    s_asked_ms = now_ms;
    s_pending = true;
}

bool out_store_pending(void) { return s_pending; }

uint32_t out_store_last_erase_us(void) { return s_last_erase_us; }

uint32_t out_store_last_program_us(void) { return s_last_program_us; }

uint8_t out_store_last_record(void) { return s_last_record; }

out_store_step_t out_store_tick(bool driving, uint32_t quiet_ms,
                                uint32_t now_ms)
{
    if (s_off || !s_pending || driving || refusing()) {
        return OUT_STORE_IDLE;
    }
    /*
     * And not until the writes have stopped.  CHAN_CFG and OUTPUTS arrive as
     * two transactions, so a save taken between them would record one page's
     * new content against the other's old, and that mismatched pair is what
     * would be restored at the next boot.
     */
    const uint32_t waited = (uint32_t)(now_ms - s_asked_ms);
    if (waited < OUT_STORE_SETTLE_MS) {
        return OUT_STORE_IDLE;
    }
    /*
     * Then into a gap in the traffic, because a frame that arrives while this
     * core is writing is a frame nobody collects.  Bounded: a bus that never
     * goes quiet must not lose the operator's binding.
     */
    if (quiet_ms < OUT_STORE_QUIET_MS
        && waited < OUT_STORE_SETTLE_MS + OUT_STORE_GAP_WAIT_MS) {
        return OUT_STORE_IDLE;
    }

    out_store_rec_t recs[STORE_SLOTS];
    survey(recs);
    out_store_write_t w;
    if (!out_store_map_write(recs, STORE_SECTORS, STORE_RECORDS, &w)) {
        return OUT_STORE_IDLE;   /* the geometry is a compile-time constant */
    }

    /*
     * An erase and the record that follows it are two windows on two passes,
     * not one long one.  The caller services the CAN controller between
     * passes, so each window meets the controller's two empty receive buffers
     * rather than one window meeting them once.
     */
    if (w.erase_first) {
        return erase_sector(w.erase_sector, &s_last_erase_us)
                   ? OUT_STORE_ERASED : OUT_STORE_REFUSED;
    }

    record_t *r = &s_page.rec;
    memset(s_page.bytes, 0xFF, sizeof(s_page.bytes));
    r->magic   = STORE_MAGIC;
    r->version = STORE_VERSION;
    r->seq     = w.seq;
    r->cfg     = s_want;
    r->crc     = record_crc(r, sizeof(*r));
    /* Last, because it counts everything above it. */
    r->zeros     = record_zeros(r, sizeof(*r));
    r->zeros_inv = ~r->zeros;

    if (!program_record(w.at, &s_last_program_us)) {
        return OUT_STORE_REFUSED;   /* still pending; nothing changed */
    }
    s_last_record = w.at;
    s_reclaim_wanted = true;   /* this record may have left a sector behind */
    s_saved = s_want;
    s_have_saved = true;
    s_pending = false;
    return OUT_STORE_WROTE;
}

bool out_store_reclaim(bool driving, uint32_t quiet_ms)
{
    /* A pending save comes first: it takes the erase it needs itself, and
     * two erases of the same sector would be one wasted cycle. */
    if (s_off || driving || s_pending || !s_reclaim_wanted
        || quiet_ms < OUT_STORE_QUIET_MS || refusing()) {
        return false;
    }
    out_store_rec_t recs[STORE_SLOTS];
    survey(recs);
    const int stale = out_store_map_stale(recs, STORE_SECTORS, STORE_RECORDS);
    if (stale < 0) {
        s_reclaim_wanted = false;
        return false;
    }
    return erase_sector((uint8_t)stale, &s_last_erase_us);
}
