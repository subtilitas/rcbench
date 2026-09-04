/*
 * The output configuration in flash.  See out_store.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_store.h"

#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "link_crc.h"

/*
 * Where it goes: the last sector of the first four megabytes.
 *
 * Not PICO_FLASH_SIZE_BYTES.  The build's default board file is a
 * pimoroni_pico_plus2_rp2350 and claims sixteen megabytes, while the module
 * the bring-up runs on has four; a sector placed from that number would be
 * past the end of the part actually fitted, and an erase off the end of a
 * flash chip is not a diagnosable failure.  Four megabytes is the smaller of
 * the two and is inside both, and 4 MB - 4 kB is far past an image that is
 * under 64 kB.
 */
#define STORE_SIZE_BYTES  (4u * 1024u * 1024u)
#define STORE_OFFSET      (STORE_SIZE_BYTES - FLASH_SECTOR_SIZE)

#define STORE_MAGIC    0x7263626FuL    /* "rcbo" */
#define STORE_VERSION  1u

/*
 * One flash page holds it, so a save is one erase and one program.  The
 * checksum is the link's, because a half-written record and a corrupt frame
 * are the same problem and there is no reason for two answers to it.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t crc;             /**< over everything after this field */
    out_store_t cfg;
} record_t;

_Static_assert(sizeof(record_t) <= FLASH_PAGE_SIZE,
               "the record has outgrown one flash page");

static bool        s_pending;
static uint32_t    s_asked_ms;
static out_store_t s_want;
static bool        s_have_saved;
static out_store_t s_saved;

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
    return link_crc(LINK_CRC_INIT, &r->cfg, sizeof(r->cfg));
}

bool out_store_load(out_store_t *out)
{
    if (out == NULL) {
        return false;
    }
    const record_t *r = (const record_t *)(const void *)
                        (XIP_BASE + STORE_OFFSET);
    if (r->magic != STORE_MAGIC || r->version != STORE_VERSION) {
        return false;
    }
    if (r->crc != record_crc(r)) {
        return false;
    }
    *out = r->cfg;
    s_saved = r->cfg;
    s_have_saved = true;
    return true;
}

void out_store_save(const out_store_t *cfg, uint32_t now_ms)
{
    if (cfg == NULL) {
        return;
    }
    /* An erase cycle to write what is already there is an erase cycle spent
     * on nothing, and this sector is rewritten every time an operator ticks
     * a pin. */
    if (s_have_saved && memcmp(&s_saved, cfg, sizeof(*cfg)) == 0) {
        s_pending = false;
        return;
    }
    s_want = *cfg;
    s_asked_ms = now_ms;
    s_pending = true;
}

bool out_store_pending(void) { return s_pending; }

bool out_store_tick(bool driving, uint32_t now_ms)
{
    if (!s_pending || driving) {
        return false;
    }
    /*
     * And not until the writes have stopped.  CHAN_CFG and OUTPUTS arrive as
     * two transactions, so a save taken between them would record one page's
     * new content against the other's old, and that mismatched pair is what
     * would be restored at the next boot.
     */
    if ((uint32_t)(now_ms - s_asked_ms) < OUT_STORE_SETTLE_MS) {
        return false;
    }

    record_t *r = &s_page.rec;
    memset(s_page.bytes, 0xFF, sizeof(s_page.bytes));
    r->magic   = STORE_MAGIC;
    r->version = STORE_VERSION;
    r->cfg     = s_want;
    r->crc     = record_crc(r);

    /*
     * Interrupts off for the whole erase and program: the flash cannot be
     * read while it is being written, and this core executes from it.  That
     * is also why this only runs while the bank is idle -- the heartbeat
     * monitor will miss every edge in the window and have to re-acquire.
     */
    const uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(STORE_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(STORE_OFFSET, s_page.bytes, FLASH_PAGE_SIZE);
    restore_interrupts(irq);

    s_saved = s_want;
    s_have_saved = true;
    s_pending = false;
    return true;
}
