/*
 * Keeping a board's photograph in the panel's flash.
 *
 * The failure under test is a picture that survives a write it should not
 * have survived. A transfer costs ten seconds of link, so what is kept is
 * kept for as long as the board is known; a slot that became findable while
 * it was half written would hand out a wrong picture until somebody wired a
 * lead by it.
 *
 * The flash is memory here, with a budget on it, so a write can be stopped
 * part way on purpose and the store asked what it thinks it has.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "art_store.h"
#include "link_crc.h"

#define SECTOR  4096u
#define SLOTS   4u
#define PART    (SLOTS * ART_SLOT_BYTES)

static uint8_t *g_flash;
/* Writes left before the part stops taking them, or -1 for no limit: a power
 * cut, an unplugged board, a sector that has worn out. */
static int      g_budget;
static unsigned g_writes;
/* Every call, and the largest of each kind, so a keep can be held to one
 * sector a step. */
static unsigned g_erases;
static unsigned g_reads;
static uint32_t g_erase_max;
static uint32_t g_write_max;
static uint32_t g_read_max;
static uint32_t g_read_bytes;
/* Erases left before the part stops taking them, or -1 for no limit. */
static int      g_erase_budget;

static bool fake_read(void *ctx, uint32_t off, void *dst, uint32_t len)
{
    (void)ctx;
    if ((uint64_t)off + len > PART) { return false; }
    ++g_reads;
    g_read_bytes += len;
    if (len > g_read_max) { g_read_max = len; }
    memcpy(dst, g_flash + off, len);
    return true;
}

static bool fake_erase(void *ctx, uint32_t off, uint32_t len)
{
    (void)ctx;
    if ((uint64_t)off + len > PART || (off % SECTOR) || (len % SECTOR)) {
        return false;
    }
    ++g_erases;
    if (len > g_erase_max) { g_erase_max = len; }
    if (g_erase_budget >= 0 && (int)g_erases > g_erase_budget) {
        return false;
    }
    memset(g_flash + off, 0xFF, len);       /* erased flash is ones */
    return true;
}

static bool fake_write(void *ctx, uint32_t off, const void *src, uint32_t len)
{
    (void)ctx;
    if ((uint64_t)off + len > PART) { return false; }
    ++g_writes;
    if (len > g_write_max) { g_write_max = len; }
    if (g_budget >= 0 && (int)g_writes > g_budget) {
        return false;                       /* the part stopped taking them */
    }
    memcpy(g_flash + off, src, len);
    return true;
}

static art_flash_t flash(void)
{
    art_flash_t f = { fake_read, fake_erase, fake_write, NULL, PART, SECTOR };
    return f;
}

static void fresh(void)
{
    if (g_flash == NULL) {
        g_flash = malloc(PART);
        REQUIRE_ALLOC(g_flash);
    }
    memset(g_flash, 0xFF, PART);
    g_budget = -1;
    g_writes = 0;
    g_erase_budget = -1;
    g_erases = 0;
    g_reads = 0;
    g_erase_max = 0;
    g_write_max = 0;
    g_read_max = 0;
    g_read_bytes = 0;
}

/* The flash calls made so far, all kinds together. */
static unsigned calls(void)
{
    return g_erases + g_writes + g_reads;
}

/* A picture, and the entry describing it. */
static uint8_t *make(uint32_t bytes, uint16_t board, art_entry_t *e)
{
    static uint8_t *buf;
    static uint32_t cap;
    if (cap < bytes) {
        uint8_t *grown = realloc(buf, bytes);
        REQUIRE_ALLOC(grown);
        buf = grown;
        cap = bytes;
    }
    for (uint32_t i = 0; i < bytes; ++i) {
        buf[i] = (uint8_t)(i * 31u + board);
    }
    e->board  = board;
    e->width  = 100;
    e->height = (uint16_t)(bytes / 200u);
    e->bytes  = bytes;
    e->crc    = link_crc(0u, buf, bytes);
    return buf;
}

TEST_CASE(a_picture_comes_back_the_way_it_went_in)
{
    fresh();
    const art_flash_t f = flash();
    CHECK_EQ(art_store_slots(&f), SLOTS);
    CHECK_EQ(art_store_capacity(&f), ART_SLOT_BYTES - SECTOR);

    art_entry_t e;
    (void)make(206000u, 7u, &e);
    CHECK(!art_store_find(&f, 7u, &e));      /* nothing kept yet */

    const uint8_t *src = make(206000u, 7u, &e);
    CHECK(art_store_put(&f, 7u, &e, src));

    art_entry_t got;
    CHECK(art_store_find(&f, 7u, &got));
    CHECK_EQ(got.board, 7);
    CHECK_EQ(got.width, 100);
    CHECK_EQ(got.height, 1030);
    CHECK_EQ(got.bytes, 206000);
    CHECK_EQ(got.crc, e.crc);

    uint8_t *back = malloc(206000u);
    CHECK(art_store_read(&f, 7u, back, 206000u));
    CHECK_EQ(memcmp(back, src, 206000u), 0);

    /* And a buffer that cannot hold it is a refusal, not an overrun. */
    CHECK(!art_store_read(&f, 7u, back, 206000u - 1u));
    free(back);
}

TEST_CASE(a_write_that_stops_part_way_leaves_nothing_findable)
{
    /*
     * The order is payload, read it back, header last. A board unplugged
     * mid-transfer, or a power cut, has to leave a slot that is found as
     * nothing rather than found and wrong.
     */
    fresh();
    const art_flash_t f = flash();
    art_entry_t e;
    const uint8_t *src = make(50000u, 3u, &e);

    g_budget = 0;                            /* no write survives */
    CHECK(!art_store_put(&f, 3u, &e, src));
    CHECK(!art_store_find(&f, 3u, &e));

    /* The payload lands and the header does not: still nothing.  The
     * payload goes a sector a call, so it is 13 writes for 50000 bytes. */
    fresh();
    src = make(50000u, 3u, &e);
    const unsigned chunks = (50000u + SECTOR - 1u) / SECTOR;
    CHECK_EQ(chunks, 13);
    g_budget = (int)chunks;                  /* the payload only */
    CHECK(!art_store_put(&f, 3u, &e, src));
    CHECK(!art_store_find(&f, 3u, &e));
    CHECK_EQ(g_writes, chunks + 1u);         /* it did try the header */

    /* And an erase that fails part way writes nothing at all. */
    fresh();
    src = make(50000u, 3u, &e);
    g_erase_budget = 3;
    CHECK(!art_store_put(&f, 3u, &e, src));
    CHECK_EQ(g_writes, 0);
    CHECK(!art_store_find(&f, 3u, &e));
}

TEST_CASE(replacing_a_picture_takes_the_old_one_with_it)
{
    fresh();
    const art_flash_t f = flash();
    art_entry_t e;
    const uint8_t *src = make(20000u, 5u, &e);
    CHECK(art_store_put(&f, 5u, &e, src));

    /* A different picture for the same board goes in the same slot. */
    art_entry_t e2;
    const uint8_t *src2 = make(30000u, 5u, &e2);
    CHECK(art_store_put(&f, 5u, &e2, src2));
    CHECK_EQ(art_store_slots(&f), SLOTS);

    art_entry_t got;
    CHECK(art_store_find(&f, 5u, &got));
    CHECK_EQ(got.bytes, 30000);
    uint8_t *back = malloc(30000u);
    CHECK(art_store_read(&f, 5u, back, 30000u));
    CHECK_EQ(memcmp(back, src2, 30000u), 0);
    free(back);

    /* One board, one slot: three more boards still fit. */
    for (uint16_t b = 10; b < 13u; ++b) {
        art_entry_t x;
        const uint8_t *p = make(1000u, b, &x);
        CHECK(art_store_put(&f, b, &x, p));
    }
    for (uint16_t b = 10; b < 13u; ++b) {
        art_entry_t x;
        CHECK(art_store_find(&f, b, &x));
    }
    CHECK(art_store_find(&f, 5u, &got));
}

TEST_CASE(a_picture_that_decayed_in_flash_is_a_miss_not_a_wrong_picture)
{
    fresh();
    const art_flash_t f = flash();
    art_entry_t e;
    const uint8_t *src = make(20000u, 9u, &e);
    CHECK(art_store_put(&f, 9u, &e, src));

    /* One bit, somewhere in the middle of the payload. */
    g_flash[ART_SLOT_BYTES * 0u + SECTOR + 9000u] ^= 0x08u;

    art_entry_t got;
    CHECK(art_store_find(&f, 9u, &got));     /* the header is still good */
    uint8_t *back = malloc(20000u);
    CHECK(!art_store_read(&f, 9u, back, 20000u));   /* and the picture is not */
    free(back);
}

TEST_CASE(a_header_that_did_not_finish_being_written_is_not_a_header)
{
    fresh();
    const art_flash_t f = flash();
    art_entry_t e;
    const uint8_t *src = make(20000u, 4u, &e);
    CHECK(art_store_put(&f, 4u, &e, src));
    CHECK(art_store_find(&f, 4u, &e));

    /* Corrupt one field of the header and leave its checksum alone, which is
     * what half a write looks like. */
    g_flash[6] ^= 0x01u;
    CHECK(!art_store_find(&f, 4u, &e));
}

TEST_CASE(a_payload_that_is_not_what_it_claims_is_refused_before_it_is_written)
{
    fresh();
    const art_flash_t f = flash();
    art_entry_t e;
    const uint8_t *src = make(20000u, 6u, &e);
    e.crc ^= 0xFFFFu;                        /* not this picture's checksum */
    CHECK(!art_store_put(&f, 6u, &e, src));
    CHECK_EQ(g_writes, 0);                   /* and nothing was written */
    CHECK(!art_store_find(&f, 6u, &e));

    /*
     * Nor one whose pixels are not its length.  A header saying 500 x 206
     * over a payload that is not 206000 bytes would be believed by whatever
     * sized a buffer from it later.
     */
    src = make(20000u, 6u, &e);
    e.height = (uint16_t)(e.height + 1u);
    CHECK(!art_store_put(&f, 6u, &e, src));
    src = make(20000u, 6u, &e);
    e.width = 0u;
    CHECK(!art_store_put(&f, 6u, &e, src));
    CHECK_EQ(g_writes, 0);

    /* Nor one larger than a slot can hold. */
    src = make(20000u, 6u, &e);
    e.bytes = art_store_capacity(&f) + 1u;
    CHECK(!art_store_put(&f, 6u, &e, src));
    CHECK_EQ(g_writes, 0);
}

TEST_CASE(forgetting_a_board_leaves_the_others_alone)
{
    fresh();
    const art_flash_t f = flash();
    for (uint16_t b = 1; b < 4u; ++b) {
        art_entry_t x;
        const uint8_t *p = make(5000u, b, &x);
        CHECK(art_store_put(&f, b, &x, p));
    }
    CHECK(art_store_forget(&f, 2u));
    art_entry_t x;
    CHECK(art_store_find(&f, 1u, &x));
    CHECK(!art_store_find(&f, 2u, &x));
    CHECK(art_store_find(&f, 3u, &x));
    /* Forgetting one that is not kept is the state asked for, not a fault. */
    CHECK(art_store_forget(&f, 99u));
}

TEST_CASE(a_flash_that_cannot_hold_a_slot_holds_nothing)
{
    fresh();
    art_flash_t f = flash();
    art_entry_t e;
    const uint8_t *src = make(1000u, 1u, &e);

    f.size = ART_SLOT_BYTES - 1u;            /* not even one slot */
    CHECK_EQ(art_store_slots(&f), 0);
    CHECK_EQ(art_store_capacity(&f), 0);
    CHECK(!art_store_put(&f, 1u, &e, src));
    CHECK(!art_store_find(&f, 1u, &e));

    /* Nor one whose sector does not divide a slot. */
    f = flash();
    f.sector = 3000u;
    CHECK_EQ(art_store_slots(&f), 0);
    CHECK(!art_store_put(&f, 1u, &e, src));
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    fresh();
    const art_flash_t f = flash();
    art_entry_t e;
    const uint8_t *src = make(1000u, 1u, &e);
    uint8_t back[16];
    CHECK_EQ(art_store_slots(NULL), 0);
    CHECK_EQ(art_store_capacity(NULL), 0);
    CHECK(!art_store_find(NULL, 1u, &e));
    CHECK(!art_store_find(&f, 1u, NULL));
    CHECK(!art_store_read(NULL, 1u, back, sizeof(back)));
    CHECK(!art_store_read(&f, 1u, NULL, sizeof(back)));
    CHECK(!art_store_put(NULL, 1u, &e, src));
    CHECK(!art_store_put(&f, 1u, NULL, src));
    CHECK(!art_store_put(&f, 1u, &e, NULL));
    CHECK(!art_store_forget(NULL, 1u));
}

/* ------------------------------------------------- a keep, a sector a step */

/* The steps a keep of @p bytes takes with nothing in its way: the header's
 * sector and the payload's erased, the payload written and read back a
 * sector at a time, the header. */
static unsigned keep_steps(uint32_t bytes)
{
    const unsigned sectors = (bytes + SECTOR - 1u) / SECTOR;
    return (1u + sectors) + sectors + sectors + 1u;
}

/* Steps until the keep is no longer running. */
static art_keep_status_t keep_out(art_keep_t *k)
{
    art_keep_status_t st;
    do {
        st = art_keep_step(k, true);
    } while (st == ART_KEEP_RUNNING);
    return st;
}

static bool fake_fail_read(void *ctx, uint32_t off, void *dst, uint32_t len)
{
    (void)ctx;
    (void)off;
    (void)dst;
    (void)len;
    return false;
}

TEST_CASE(a_keep_does_at_most_one_sector_a_step)
{
    fresh();
    const art_flash_t f = flash();
    art_entry_t e;
    const uint8_t *src = make(206000u, 7u, &e);
    art_keep_t k;
    CHECK(art_keep_begin(&k, &f, 7u, &e, src));
    CHECK(art_keep_busy(&k));
    CHECK_EQ(calls(), 0);                    /* beginning touches nothing */

    unsigned steps = 0;
    art_keep_status_t st;
    do {
        const unsigned erases = g_erases;
        const unsigned writes = g_writes;
        const uint32_t read   = g_read_bytes;
        st = art_keep_step(&k, true);
        ++steps;
        /* One erase or one write, never both and never two. */
        CHECK(g_erases + g_writes - erases - writes <= 1u);
        /* And at most a sector read. */
        CHECK(g_read_bytes - read <= SECTOR);
    } while (st == ART_KEEP_RUNNING);
    CHECK_EQ(st, ART_KEEP_DONE);
    CHECK(!art_keep_busy(&k));
    /* 51 sectors of payload: 52 erases, 51 writes, 51 read-backs, a header. */
    CHECK_EQ(steps, keep_steps(206000u));
    CHECK_EQ(steps, 155);
    CHECK_EQ(g_erase_max, SECTOR);
    CHECK_EQ(g_write_max, SECTOR);
    /* The read-back goes through the keeper's 256-byte buffer, never more
     * at once: 16 reads for a full sector. */
    CHECK_EQ(g_read_max, 256u);
    CHECK_EQ(g_erases, 52);
    CHECK_EQ(g_writes, 52);

    uint8_t *back = malloc(206000u);
    CHECK(art_store_read(&f, 7u, back, 206000u));
    CHECK_EQ(memcmp(back, src, 206000u), 0);
    free(back);

    /* Once done it is idle, and a further step does nothing. */
    const unsigned before = calls();
    CHECK_EQ(art_keep_step(&k, true), ART_KEEP_IDLE);
    CHECK_EQ(calls(), before);
}

TEST_CASE(a_keep_that_may_not_write_touches_no_flash_at_any_step)
{
    /*
     * Every step of the way: before the slot is chosen, mid-erase, mid-write,
     * mid-read-back and with only the header left.  WAITING is a promise
     * that nothing reached the flash.
     */
    fresh();
    const art_flash_t f = flash();
    art_entry_t e;
    const uint8_t *src = make(30000u, 2u, &e);
    art_keep_t k;
    CHECK(art_keep_begin(&k, &f, 2u, &e, src));
    const unsigned total = keep_steps(30000u);
    for (unsigned i = 0; i < total; ++i) {
        const unsigned before = calls();
        for (int w = 0; w < 3; ++w) {
            CHECK_EQ(art_keep_step(&k, false), ART_KEEP_WAITING);
        }
        CHECK_EQ(calls(), before);
        CHECK(art_keep_busy(&k));
        const art_keep_status_t st = art_keep_step(&k, true);
        CHECK_EQ(st, (i + 1u < total) ? ART_KEEP_RUNNING : ART_KEEP_DONE);
    }
    CHECK(!art_keep_busy(&k));
    art_entry_t got;
    CHECK(art_store_find(&f, 2u, &got));
    CHECK_EQ(got.crc, e.crc);
}

TEST_CASE(a_keep_paused_after_any_step_is_found_as_nothing_and_resumes_whole)
{
    /*
     * The pause is where the bench armed or the supply came on.  While it
     * lasts the slot is never findable -- not the old picture, which the
     * first step's erase took, and not a part of the new one -- and the
     * picker reading it then draws the board from its outline.  Resumed,
     * the picture that lands is the one handed over, byte for byte.
     */
    static uint8_t back[20000];
    art_entry_t old;
    art_entry_t e;
    const unsigned total = keep_steps(20000u);
    for (unsigned pause = 1; pause < total; ++pause) {
        fresh();
        const art_flash_t f = flash();
        /* An older picture for the same board, which the keep replaces. */
        const uint8_t *was = make(10000u, 4u, &old);
        CHECK(art_store_put(&f, 4u, &old, was));

        const uint8_t *src = make(20000u, 4u, &e);
        art_keep_t k;
        CHECK(art_keep_begin(&k, &f, 4u, &e, src));
        for (unsigned i = 0; i < pause; ++i) {
            CHECK_EQ(art_keep_step(&k, true), ART_KEEP_RUNNING);
        }
        art_entry_t got;
        CHECK(!art_store_find(&f, 4u, &got));
        for (int w = 0; w < 50; ++w) {
            CHECK_EQ(art_keep_step(&k, false), ART_KEEP_WAITING);
        }
        CHECK(!art_store_find(&f, 4u, &got));

        CHECK_EQ(keep_out(&k), ART_KEEP_DONE);
        CHECK(art_store_find(&f, 4u, &got));
        CHECK_EQ(got.bytes, 20000);
        CHECK(art_store_read(&f, 4u, back, sizeof(back)));
        CHECK_EQ(memcmp(back, src, sizeof(back)), 0);
    }
}

TEST_CASE(a_keep_never_resumed_leaves_nothing_findable_and_the_slot_reusable)
{
    /* A power cut during the pause: the keeper is gone with the RAM it was
     * in, and the next boot fetches the picture again. */
    const unsigned total = keep_steps(20000u);
    for (unsigned cut = 1; cut < total; ++cut) {
        fresh();
        const art_flash_t f = flash();
        art_entry_t e;
        const uint8_t *src = make(20000u, 8u, &e);
        art_keep_t k;
        CHECK(art_keep_begin(&k, &f, 8u, &e, src));
        for (unsigned i = 0; i < cut; ++i) {
            CHECK_EQ(art_keep_step(&k, true), ART_KEEP_RUNNING);
        }
        art_entry_t got;
        CHECK(!art_store_find(&f, 8u, &got));
        CHECK(art_store_put(&f, 8u, &e, src));
        CHECK(art_store_find(&f, 8u, &got));
    }
}

TEST_CASE(flash_changed_during_a_pause_fails_the_keep_without_a_header)
{
    /*
     * Nothing else writes the slot, so this is decay or a fault rather than
     * a design case; the read-back is what stops it being sealed.
     */
    fresh();
    const art_flash_t f = flash();
    art_entry_t e;
    const uint8_t *src = make(20000u, 6u, &e);
    art_keep_t k;
    CHECK(art_keep_begin(&k, &f, 6u, &e, src));
    const unsigned sectors = (20000u + SECTOR - 1u) / SECTOR;
    /* Erased and written, the read-back not started. */
    for (unsigned i = 0; i < (1u + sectors) + sectors; ++i) {
        CHECK_EQ(art_keep_step(&k, true), ART_KEEP_RUNNING);
    }
    CHECK_EQ(art_keep_step(&k, false), ART_KEEP_WAITING);
    g_flash[SECTOR + 12345u] ^= 0x40u;
    CHECK_EQ(keep_out(&k), ART_KEEP_FAILED);
    CHECK(!art_keep_busy(&k));
    art_entry_t got;
    CHECK(!art_store_find(&f, 6u, &got));
    CHECK_EQ(art_keep_step(&k, true), ART_KEEP_IDLE);
}

TEST_CASE(a_keep_fails_on_the_first_flash_call_that_does)
{
    /* A write mid-payload, the header's write, a read mid-read-back: each
     * ends the keep FAILED with nothing findable. */
    art_entry_t e;
    art_entry_t got;
    art_keep_t k;
    const unsigned sectors = (20000u + SECTOR - 1u) / SECTOR;

    fresh();
    art_flash_t f = flash();
    const uint8_t *src = make(20000u, 1u, &e);
    g_budget = 2;
    CHECK(art_keep_begin(&k, &f, 1u, &e, src));
    CHECK_EQ(keep_out(&k), ART_KEEP_FAILED);
    CHECK_EQ(g_writes, 3);
    CHECK(!art_store_find(&f, 1u, &got));

    /* The header: every payload write lands and the last write does not. */
    fresh();
    src = make(20000u, 1u, &e);
    g_budget = (int)sectors;
    CHECK(art_keep_begin(&k, &f, 1u, &e, src));
    CHECK_EQ(keep_out(&k), ART_KEEP_FAILED);
    CHECK_EQ(g_writes, sectors + 1u);
    CHECK(!art_store_find(&f, 1u, &got));

    /* A read during the read-back.  The keep holds the flash it was given,
     * so the read is swapped under it. */
    fresh();
    src = make(20000u, 1u, &e);
    CHECK(art_keep_begin(&k, &f, 1u, &e, src));
    for (unsigned i = 0; i < (1u + sectors) + sectors + 1u; ++i) {
        CHECK_EQ(art_keep_step(&k, true), ART_KEEP_RUNNING);
    }
    f.read = fake_fail_read;
    CHECK_EQ(art_keep_step(&k, true), ART_KEEP_FAILED);
    /* cppcheck-suppress redundantAssignment
     * the keep reads through its own pointer to f */
    f.read = fake_read;
    CHECK(!art_store_find(&f, 1u, &got));
}

TEST_CASE(a_keep_refuses_at_its_start_what_put_refuses_before_writing)
{
    fresh();
    const art_flash_t f = flash();
    art_entry_t e;
    const uint8_t *src = make(20000u, 3u, &e);
    art_keep_t k;

    CHECK(!art_keep_begin(NULL, &f, 3u, &e, src));
    CHECK(!art_keep_begin(&k, NULL, 3u, &e, src));
    CHECK(!art_keep_busy(&k));
    CHECK(!art_keep_begin(&k, &f, 3u, NULL, src));
    CHECK(!art_keep_begin(&k, &f, 3u, &e, NULL));
    e.height = (uint16_t)(e.height + 1u);
    CHECK(!art_keep_begin(&k, &f, 3u, &e, src));
    src = make(20000u, 3u, &e);
    e.width = 0u;
    CHECK(!art_keep_begin(&k, &f, 3u, &e, src));
    src = make(20000u, 3u, &e);
    e.height = 0u;
    CHECK(!art_keep_begin(&k, &f, 3u, &e, src));
    src = make(20000u, 3u, &e);
    e.bytes = 0u;
    CHECK(!art_keep_begin(&k, &f, 3u, &e, src));
    CHECK(!art_keep_busy(&k));
    CHECK_EQ(art_keep_step(&k, true), ART_KEEP_IDLE);
    CHECK_EQ(art_keep_step(NULL, true), ART_KEEP_IDLE);
    CHECK(!art_keep_busy(NULL));
    CHECK_EQ(calls(), 0);

    /* The largest picture a slot holds is taken, and two bytes more are not:
     * 126 x 1024 pixels of two bytes is a slot less its header's sector. */
    const uint32_t cap = art_store_capacity(&f);
    CHECK_EQ(cap, 258048);
    e.bytes  = cap;
    e.width  = 126u;
    e.height = 1024u;
    CHECK(art_keep_begin(&k, &f, 3u, &e, src));
    CHECK(art_keep_busy(&k));
    e.bytes  = cap + 2u;                     /* 25 x 5161 pixels */
    e.width  = 25u;
    e.height = 5161u;
    CHECK(!art_keep_begin(&k, &f, 3u, &e, src));
    CHECK(!art_keep_busy(&k));
    CHECK_EQ(calls(), 0);

    /* One pixel is the narrowest side there is, either way round. */
    e.bytes  = 2000u;
    e.width  = 1u;
    e.height = 1000u;
    CHECK(art_keep_begin(&k, &f, 3u, &e, src));
    e.width  = 1000u;
    e.height = 1u;
    CHECK(art_keep_begin(&k, &f, 3u, &e, src));
    CHECK_EQ(calls(), 0);
}

TEST_CASE(a_keep_erases_the_sectors_it_uses_and_leaves_the_rest_of_the_slot)
{
    /*
     * The sector after the payload's last is never read, so it is not
     * erased: one erase is one stall of both cores, and a 206000 byte
     * picture takes 52 of them rather than the slot's 64.  A smaller
     * picture replacing a larger one leaves the larger's tail there,
     * unreached.
     */
    fresh();
    const art_flash_t f = flash();
    art_entry_t e;
    const uint32_t tail = SECTOR + ((20000u + SECTOR - 1u) / SECTOR) * SECTOR;
    g_flash[tail] = 0x5Au;                   /* the first byte past it */
    g_flash[tail - 1u] = 0x5Au;              /* the last byte inside it */
    const uint8_t *src = make(20000u, 9u, &e);
    CHECK(art_store_put(&f, 9u, &e, src));
    CHECK_EQ(g_flash[tail], 0x5A);
    CHECK_EQ(g_flash[tail - 1u], 0xFF);
    CHECK_EQ(g_erases, tail / SECTOR);
}

int main(void)
{
    RUN(a_picture_comes_back_the_way_it_went_in);
    RUN(a_write_that_stops_part_way_leaves_nothing_findable);
    RUN(replacing_a_picture_takes_the_old_one_with_it);
    RUN(a_picture_that_decayed_in_flash_is_a_miss_not_a_wrong_picture);
    RUN(a_header_that_did_not_finish_being_written_is_not_a_header);
    RUN(a_payload_that_is_not_what_it_claims_is_refused_before_it_is_written);
    RUN(forgetting_a_board_leaves_the_others_alone);
    RUN(a_flash_that_cannot_hold_a_slot_holds_nothing);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    RUN(a_keep_does_at_most_one_sector_a_step);
    RUN(a_keep_that_may_not_write_touches_no_flash_at_any_step);
    RUN(a_keep_paused_after_any_step_is_found_as_nothing_and_resumes_whole);
    RUN(a_keep_never_resumed_leaves_nothing_findable_and_the_slot_reusable);
    RUN(flash_changed_during_a_pause_fails_the_keep_without_a_header);
    RUN(a_keep_fails_on_the_first_flash_call_that_does);
    RUN(a_keep_refuses_at_its_start_what_put_refuses_before_writing);
    RUN(a_keep_erases_the_sectors_it_uses_and_leaves_the_rest_of_the_slot);
    return test_summary("art_store");
}
