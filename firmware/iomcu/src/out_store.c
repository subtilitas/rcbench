/**
 * @file
 * @brief The output configuration in flash.
 *
 * See out_store.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_store.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#include "out_store_map.h"

/**
 * @brief Where it goes: the data partition "store" of partitions.json, two
 * sectors at 4MiB - 16KiB (0x3FC000), below the last 8KiB the layout
 * leaves unused.
 *
 * The build reads the offset out of that file (IOMCU_STORE_OFFSET).
 *
 * Read by its storage address, through the XIP (execute in place) window
 * that neither translates nor caches.  The image runs from slot A or B and
 * the boot ROM maps that slot to XIP_BASE, so XIP_BASE + offset is an
 * address inside the running slot, not the store.  Uncached, because an
 * erase or a program does not pass through the cache.
 *
 * Two sectors rather than one.  A record is only ever written into an erased
 * slot and a sector is only ever erased while the live record is in the other
 * one, so a power cut during either leaves the binding from before the save
 * readable.  See out_store_map.h.
 *
 * Not PICO_FLASH_SIZE_BYTES.  The build's default board file is a
 * pimoroni_pico_plus2_rp2350 and claims sixteen megabytes, while the module
 * has four; the partition table is written for the four.
 */
#define STORE_FLASH_BYTES (4u * 1024u * 1024u)
#define STORE_SECTORS     OUT_STORE_SECTORS
#define STORE_SLOT_BYTES  OUT_STORE_SLOT_BYTES
#define STORE_RECORDS     (FLASH_SECTOR_SIZE / STORE_SLOT_BYTES)
#define STORE_SLOTS       (STORE_SECTORS * STORE_RECORDS)
/**
 * @brief The one-page slots of version 7 and older, read until the first
 * version 8 record is written (out_store_rec.h).
 */
#define STORE_V7_BYTES    OUT_STORE_SLOT_BYTES_V7
#define STORE_V7_RECORDS  (FLASH_SECTOR_SIZE / STORE_V7_BYTES)
#define STORE_V7_SLOTS    (STORE_SECTORS * STORE_V7_RECORDS)
#define STORE_OFFSET      IOMCU_STORE_OFFSET

/**
 * @brief Where a build without a partition table kept the store: the last
 * two sectors of the 4MiB.  Read once, by out_store_load(), when the store
 * holds no record yet, so the bindings survive the USB load that installs
 * the partition table.  Never written.
 */
#define STORE_OLD_OFFSET  (STORE_FLASH_BYTES - STORE_SECTORS * FLASH_SECTOR_SIZE)

#define STORE_READ_BASE   XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE

/*
 * Held at compile time as well as in the paragraph above.
 *
 * This catches a board file with less flash than the store needs, and a
 * partition table whose store partition is not the two sectors this
 * file writes.
 */
_Static_assert(STORE_FLASH_BYTES <= PICO_FLASH_SIZE_BYTES,
               "the output store is past the end of this board's flash");
_Static_assert(IOMCU_STORE_BYTES == STORE_SECTORS * FLASH_SECTOR_SIZE,
               "partitions.json: the store partition is not two sectors");
_Static_assert(STORE_OFFSET + IOMCU_STORE_BYTES <= STORE_OLD_OFFSET,
               "partitions.json: the store overlaps the old store's sectors");
_Static_assert(STORE_V7_SLOTS <= 255u,
               "the store has more slots than a slot index holds");
_Static_assert(STORE_V7_BYTES == FLASH_PAGE_SIZE
               && STORE_SLOT_BYTES == 2u * FLASH_PAGE_SIZE,
               "a record slot is one flash page before version 8, two from it");

/* Written at OUT_STORE_VERSION; versions 3 to 7 are still read, so the
 * output bindings, the supply's wiring, the sensor bus's set-up, the
 * phase tap's and the ports an earlier build saved survive the update
 * (out_store_rec.h). */

/**
 * @brief Two flash pages hold a version 8 record, and a save programs both
 * in one window.
 *
 * The record's layout, its checksum and its zero count are
 * out_store_rec.c's (host-tested); the checksum is the link's, because a
 * half-written record and a corrupt frame are the same problem and there is
 * no reason for two answers to it.
 *
 * The checksum is not what says the record is whole, though, and a power cut
 * can leave one that is not.  Two ways, and they are opposites:
 *
 *   During an erase.  Bits lift towards 0xFF, so a superseded record's
 *   sequence number can grow past the live one's and outrank it.
 *   During a program.  Bits fall towards their value, so a record whose
 *   first page has landed and whose second has not ranks above the record
 *   before it and carries a configuration that is part 0xFF.
 *
 * A 16-bit checksum makes each unlikely rather than impossible.  The zero
 * count is what makes it impossible: zeros is how many 0 bits the record
 * holds from crc to its end, across both pages, written beside its own
 * complement.  An erase sets bits and a program clears them, so either one
 * cut off leaves fewer 0 bits than zeros says, and neither can keep the
 * pair: an erase would have to clear a bit to pay for one it set, a program
 * set one to pay for one it cleared (out_store_map.h).
 */
static bool        s_pending;
static uint32_t    s_asked_ms;
static out_store_t s_want;
static bool        s_have_saved;
static out_store_t s_saved;
static uint8_t     s_last_record;

/**
 * @brief How long the last erase and the last program each took, with
 * interrupts off.
 *
 * Two numbers rather than one, because they answer different questions.  The
 * program is what every save costs; the erase is what one save in sixteen
 * costs, or none of them when out_store_reclaim() has been given a quiet moment
 * first.  Measured rather than compensated for: what a window does to the link
 * is decided by its length, and the length belongs to the flash part.
 */
static uint32_t    s_last_erase_us;
static uint32_t    s_last_program_us;

/**
 * @brief Whether the store is worth looking at for a sector to erase.
 *
 * True at boot and after every record written, false once a look has found
 * nothing.  Without it out_store_reclaim() would read eight kilobytes of
 * flash on every pass of a loop that turns over in microseconds, to answer a
 * question that changes once per sixteen saves.
 */
static bool        s_reclaim_wanted = true;

/**
 * @brief The staged page, as a union rather than a byte array with a cast.
 *
 * The record has 32-bit fields and a bare uint8_t array carries no
 * alignment, so writing through a pointer to it is a store the compiler is
 * entitled to assume is aligned when it is not.
 */
static union {
    uint8_t  bytes[STORE_SLOT_BYTES];
    uint32_t align;
} s_page;

/**
 * @brief The live record of the layout before version 8, by sector, while the
 * store holds no version 8 record: never erased until the first one is written.
 *
 * -1 for none.
 */
static int s_keep_sector = -1;

/**
 * @brief The store read is at STORE_OFFSET, or at STORE_OLD_OFFSET while
 * out_store_load() looks there.
 */
static uint32_t s_read_offset = STORE_OFFSET;

/**
 * @brief Returns the address of a record slot in the store, read through the
 * uncached window.
 *
 * @param slot  Slot index.
 * @param bytes  Bytes per slot.
 * @return The address of the slot.
 */
static const uint8_t *slot_at(uint8_t slot, uint32_t bytes)
{
    return (const uint8_t *)(STORE_READ_BASE + s_read_offset
                             + (uint32_t)slot * bytes);
}

/**
 * @brief What is in the store, read out of flash rather than remembered: @p
 * n slots of @p bytes, a record of @p min_version to @p max_version taken.
 *
 * Eight kilobytes of uncached reads per call, and a call happens only when
 * something is about to be written.  Holding the answer in RAM instead would
 * be a second copy of the truth that a torn write could disagree with.
 *
 * @param recs  Receives one entry per slot.
 * @param n  Number of slots.
 * @param bytes  Bytes per slot.
 * @param min_version  Lowest record version taken.
 * @param max_version  Highest record version taken.
 */
static void survey(out_store_rec_t *recs, uint8_t n, uint32_t bytes,
                   uint16_t min_version, uint16_t max_version)
{
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t *p = slot_at(i, bytes);
        bool erased = true;
        for (size_t b = 0; b < bytes; ++b) {
            if (p[b] != 0xFFu) {
                erased = false;
                break;
            }
        }
        uint32_t seq = 0u;
        recs[i].erased = erased;
        recs[i].valid  = !erased
                         && out_store_rec_check(p, bytes, min_version,
                                                max_version, NULL, &seq);
        recs[i].seq    = recs[i].valid ? seq : 0u;
    }
}

/**
 * @brief One erase or one program, run inside a flash window.
 *
 * One erase or one program: interrupts off on this core, and core 1 either
 * not launched yet or parked in RAM by the multicore lock-out
 * (flash_window()), for the whole operation.  The flash cannot be read while it is being written, and
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
    bool     erase;       /* a sector erase; else a program of data        */
    bool     done;                /**< the operation ran                       */
    uint32_t window_us;           /**< how long the window held interrupts off, µs */
    const uint8_t *data;                /**< what a program writes */
    uint32_t len;                 /**< and how many bytes of it                */
    void   (*call)(void *);       /**< in place of either: the boot ROM's BUY  */
    void    *arg;                 /**< the argument of call                    */
} flash_op_t;

/**
 * @brief Runs the operation and records how long the window held
 * interrupts off.
 *
 * @param arg  The flash_op_t to run.
 */
static void flash_op_run(void *arg)
{
    flash_op_t *op = (flash_op_t *)arg;
    const absolute_time_t began = get_absolute_time();
    if (op->call != NULL) {
        op->call(op->arg);
    } else if (op->erase) {
        flash_range_erase(op->off, FLASH_SECTOR_SIZE);
    } else {
        flash_range_program(op->off, op->data, op->len);
    }
    op->window_us = (uint32_t)absolute_time_diff_us(began, get_absolute_time());
    op->done = true;
}

/**
 * @brief When core 1 last failed to stop, and whether that is recent.
 */
static bool     s_refused;
static uint32_t s_refused_ms;

/**
 * @brief How a window opens, by where core 1 stands (out_store.h):
 *
 *   ALONE     core 1 has not been launched.  Interrupts off on this core
 *             and the operation run directly: nothing else executes from
 *             flash.  flash_safe_execute() is not used here, because
 *             whether it lets this through depends on a pico-sdk option.
 *             In 2.3.0, multicore_lockout_ready() answers true for a
 *             core 1 that never ran only while
 *             PICO_MULTICORE_LOCKOUT_BEFORE_CORE1_STARTED is set
 *             (multicore.c:374-381; it defaults to 1).  Without it,
 *             default_enter_safe_zone_timeout_ms() reaches the
 *             assert(false) at flash.c:189-190 under
 *             PICO_FLASH_ASSERT_ON_UNSAFE, or refuses the window.
 *   PARKABLE  core 1 registered for the lock-out (out_store_core1_parkable()):
 *             every window through flash_safe_execute(), which parks it.
 *   OFF       core 1 was launched and did not register (out_store_off()):
 *             no window opens at all, since the direct path would fault a
 *             core 1 executing from flash and flash_safe_execute() would
 *             take that same assert, or refuse.
 */
typedef enum { STORE_ALONE = 0, STORE_PARKABLE, STORE_OFF } store_mode_t;
static store_mode_t s_mode = STORE_ALONE;

void out_store_core1_parkable(void)
{
    if (s_mode == STORE_ALONE) {
        s_mode = STORE_PARKABLE;
    }
}

void out_store_off(void)
{
    s_mode    = STORE_OFF;
    s_pending = false;
}

bool out_store_is_off(void)
{
    return s_mode == STORE_OFF;
}

/**
 * @brief Reads the clock in milliseconds since boot.
 *
 * @return The time since boot, in milliseconds.
 */
static uint32_t clock_ms(void)
{
    return (uint32_t)to_ms_since_boot(get_absolute_time());
}

/**
 * @brief Tells whether windows are refused because core 1 did not stop.
 *
 * @return True while a window is refused. The refusal lapses
 *         after OUT_STORE_REFUSED_WAIT_MS.
 */
static bool refusing(void)
{
    if (s_refused
        && (uint32_t)(clock_ms() - s_refused_ms) >= OUT_STORE_REFUSED_WAIT_MS) {
        s_refused = false;
    }
    return s_refused;
}

/**
 * @brief True when the operation ran.
 *
 * flash_safe_execute() answers a timeout on
 * the way in (nothing ran) and on the way out (it ran) with the same code,
 * so the operation says for itself.
 *
 * @param op  The operation. Its done field is set.
 * @return True when the operation ran.
 */
static bool flash_window(flash_op_t *op)
{
    op->done = false;
    switch (s_mode) {
    case STORE_ALONE: {
        const uint32_t irq = save_and_disable_interrupts();
        flash_op_run(op);
        restore_interrupts(irq);
        return op->done;
    }
    case STORE_PARKABLE:
        (void)flash_safe_execute(flash_op_run, op, OUT_STORE_LOCKOUT_MS);
        if (!op->done) {
            s_refused    = true;
            s_refused_ms = clock_ms();
        }
        return op->done;
    case STORE_OFF:
    default:
        return false;             /* the gate: no window, no lock-out */
    }
}

/**
 * @brief Erases one sector of the store inside a flash window.
 *
 * @param sector  Sector index within the store.
 * @param window_us  Receives how long the window held interrupts off, in
 *                   microseconds. Written only when the erase ran.
 * @return True when the erase ran.
 */
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

/**
 * @brief The staged slot, both of its pages, in one window.
 *
 * @param slot  Slot index within the store.
 * @param window_us  Receives how long the window held interrupts off, in
 *                   microseconds. Written only when the program ran.
 * @return True when the program ran.
 */
static bool program_record(uint8_t slot, uint32_t *window_us)
{
    flash_op_t op = {
        .off   = STORE_OFFSET + (uint32_t)slot * STORE_SLOT_BYTES,
        .erase = false,
        .data  = s_page.bytes,
        .len   = STORE_SLOT_BYTES,
    };
    if (!flash_window(&op)) {
        return false;
    }
    *window_us = op.window_us;
    return true;
}

/**
 * @brief The newest record of @p n slots of @p bytes, @p min_version to @p
 * max_version, into @p out; its slot, or -1 when none is valid.
 *
 * @param recs  Receives the survey of the slots.
 * @param n  Number of slots.
 * @param per_sector  Slots per sector.
 * @param bytes  Bytes per slot.
 * @param min_version  Lowest record version taken.
 * @param max_version  Highest record version taken.
 * @param out  Receives the configuration.
 * @return The slot the record was read from, or -1 when none is valid.
 */
static int load_from(out_store_rec_t *recs, uint8_t n, uint8_t per_sector,
                     uint32_t bytes, uint16_t min_version,
                     uint16_t max_version, out_store_t *out)
{
    survey(recs, n, bytes, min_version, max_version);
    const int newest = out_store_map_newest(recs, STORE_SECTORS, per_sector);
    if (newest < 0) {
        return -1;
    }
    const uint8_t *p = slot_at((uint8_t)newest, bytes);
    out_store_hdr_t h;
    memcpy(&h, p, sizeof(h));
    /* Valid, so its version is one out_store_cfg_size() knows. */
    return out_store_cfg_read(h.version, p + sizeof(h), out) ? newest : -1;
}

bool out_store_load(out_store_t *out)
{
    if (out == NULL) {
        return false;
    }
    out_store_rec_t recs[STORE_V7_SLOTS];
    s_keep_sector = -1;
    int at = load_from(recs, STORE_SLOTS, STORE_RECORDS, STORE_SLOT_BYTES,
                       OUT_STORE_VERSION, OUT_STORE_VERSION, out);
    if (at >= 0) {
        /* Saved as it is. */
        s_saved = *out;
        s_have_saved = true;
        s_last_record = (uint8_t)at;
        return true;
    }
    /* No version 8 record: one of version 7 or older in its one-page slot,
     * kept until the first version 8 record is written.  The first save
     * writes it in the new layout. */
    at = load_from(recs, STORE_V7_SLOTS, STORE_V7_RECORDS, STORE_V7_BYTES,
                   OUT_STORE_VERSION_V3, OUT_STORE_VERSION_V7, out);
    if (at >= 0) {
        s_keep_sector = at / (int)STORE_V7_RECORDS;
        return true;
    }
    /* Nothing saved since the partition table went in: the bindings of
     * the build before it, where that build kept them. */
    s_read_offset = STORE_OLD_OFFSET;
    at = load_from(recs, STORE_V7_SLOTS, STORE_V7_RECORDS, STORE_V7_BYTES,
                   OUT_STORE_VERSION_V3, OUT_STORE_VERSION_V7, out);
    s_read_offset = STORE_OFFSET;
    return at >= 0;
}

void out_store_save(const out_store_t *cfg, uint32_t now_ms)
{
    if (cfg == NULL || s_mode == STORE_OFF) {
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
    if (s_mode == STORE_OFF || !s_pending || driving || refusing()) {
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
    survey(recs, STORE_SLOTS, STORE_SLOT_BYTES, OUT_STORE_VERSION,
           OUT_STORE_VERSION);
    out_store_write_t w;
    if (!out_store_map_write_keep(recs, STORE_SECTORS, STORE_RECORDS,
                                  s_keep_sector, &w)) {
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

    (void)out_store_rec_build(s_page.bytes, sizeof(s_page.bytes),
                              OUT_STORE_VERSION, w.seq, &s_want);
    if (!program_record(w.at, &s_last_program_us)) {
        return OUT_STORE_REFUSED;   /* still pending; nothing changed */
    }
    s_last_record = w.at;
    s_keep_sector = -1;        /* the layout before is superseded */
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
    if (s_mode == STORE_OFF || driving || s_pending || !s_reclaim_wanted
        || quiet_ms < OUT_STORE_QUIET_MS || refusing()) {
        return false;
    }
    out_store_rec_t recs[STORE_SLOTS];
    survey(recs, STORE_SLOTS, STORE_SLOT_BYTES, OUT_STORE_VERSION,
           OUT_STORE_VERSION);
    const int stale = out_store_map_stale_keep(recs, STORE_SECTORS,
                                               STORE_RECORDS, s_keep_sector);
    if (stale < 0) {
        s_reclaim_wanted = false;
        return false;
    }
    return erase_sector((uint8_t)stale, &s_last_erase_us);
}

/* ------------------------------------------------- the firmware update's */

bool out_store_window_open(void)
{
    return s_mode != STORE_OFF && !s_pending && !refusing();
}

bool out_store_flash_erase(uint32_t off)
{
    if (off % FLASH_SECTOR_SIZE != 0u
        || off + FLASH_SECTOR_SIZE > STORE_OFFSET) {
        return false;
    }
    flash_op_t op = { .off = off, .erase = true };
    return flash_window(&op);
}

bool out_store_flash_program(uint32_t off, const uint8_t *data, uint32_t len)
{
    if (data == NULL || off % FLASH_SECTOR_SIZE != 0u
        || len % FLASH_PAGE_SIZE != 0u || len > FLASH_SECTOR_SIZE
        || off + len > STORE_OFFSET) {
        return false;
    }
    flash_op_t op = { .off = off, .erase = false, .data = data, .len = len };
    return flash_window(&op);
}

bool out_store_flash_call(void (*fn)(void *), void *arg)
{
    if (fn == NULL) {
        return false;
    }
    flash_op_t op = { .call = fn, .arg = arg };
    return flash_window(&op);
}
