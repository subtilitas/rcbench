/*
 * The output configuration in flash.  See out_store.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_store.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
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
#define STORE_VERSION  2u

/*
 * One flash page holds a record, so a save is one program.  The checksum is
 * the link's, because a half-written record and a corrupt frame are the same
 * problem and there is no reason for two answers to it.
 *
 * It covers the sequence number as well as the configuration, and the
 * sequence number is what decides which record is the live one.  An erase
 * lifts bits towards 0xFF, so a record caught half way through one can only
 * have a higher sequence number than it was written with; covering it makes
 * that record fail its check rather than outrank the record still wanted.
 */
typedef struct {
    uint32_t magic;
    uint16_t crc;             /**< over every byte after this field */
    uint16_t version;
    uint32_t seq;
    out_store_t cfg;
} record_t;

_Static_assert(sizeof(record_t) <= FLASH_PAGE_SIZE,
               "the record has outgrown one flash page");

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

static uint16_t record_crc(const record_t *r)
{
    return link_crc(LINK_CRC_INIT, &r->version,
                    sizeof(*r) - offsetof(record_t, version));
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
        recs[i].erased = erased;
        recs[i].valid  = !erased
                         && r->magic == STORE_MAGIC
                         && r->version == STORE_VERSION
                         && r->crc == record_crc(r);
        recs[i].seq    = recs[i].valid ? r->seq : 0u;
    }
}

/*
 * Interrupts off for the whole operation: the flash cannot be read while it
 * is being written, and this core executes from it.  That is also why none of
 * this runs while the bank is driving -- the caller's loop takes nothing off
 * the CAN controller and steps no output for the length of the window, so a
 * disarm or a command in flight waits for it.
 *
 * Both timestamps are inside the window, because the window is what is being
 * measured: taken either side of it they would also count the disable and
 * restore, and the number is meant to be the time this core answered nothing.
 * The timer is a peripheral and reads with interrupts off.
 */
static uint32_t erase_sector(uint8_t sector)
{
    const uint32_t off = STORE_OFFSET + (uint32_t)sector * FLASH_SECTOR_SIZE;
    const uint32_t irq = save_and_disable_interrupts();
    const absolute_time_t began = get_absolute_time();
    flash_range_erase(off, FLASH_SECTOR_SIZE);
    const uint32_t window = (uint32_t)absolute_time_diff_us(
        began, get_absolute_time());
    restore_interrupts(irq);
    return window;
}

static uint32_t program_record(uint8_t slot)
{
    const uint32_t off = STORE_OFFSET + (uint32_t)slot * FLASH_PAGE_SIZE;
    const uint32_t irq = save_and_disable_interrupts();
    const absolute_time_t began = get_absolute_time();
    flash_range_program(off, s_page.bytes, FLASH_PAGE_SIZE);
    const uint32_t window = (uint32_t)absolute_time_diff_us(
        began, get_absolute_time());
    restore_interrupts(irq);
    return window;
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
    *out = r->cfg;
    s_saved = r->cfg;
    s_have_saved = true;
    s_last_record = (uint8_t)newest;
    return true;
}

void out_store_save(const out_store_t *cfg, uint32_t now_ms)
{
    if (cfg == NULL) {
        return;
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
    if (!s_pending || driving) {
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
        s_last_erase_us = erase_sector(w.erase_sector);
        return OUT_STORE_ERASED;
    }

    record_t *r = &s_page.rec;
    memset(s_page.bytes, 0xFF, sizeof(s_page.bytes));
    r->magic   = STORE_MAGIC;
    r->version = STORE_VERSION;
    r->seq     = w.seq;
    r->cfg     = s_want;
    r->crc     = record_crc(r);

    s_last_program_us = program_record(w.at);
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
    if (driving || s_pending || !s_reclaim_wanted
        || quiet_ms < OUT_STORE_QUIET_MS) {
        return false;
    }
    out_store_rec_t recs[STORE_SLOTS];
    survey(recs);
    const int stale = out_store_map_stale(recs, STORE_SECTORS, STORE_RECORDS);
    if (stale < 0) {
        s_reclaim_wanted = false;
        return false;
    }
    s_last_erase_us = erase_sector((uint8_t)stale);
    return true;
}
