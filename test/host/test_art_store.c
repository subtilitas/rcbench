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

static bool fake_read(void *ctx, uint32_t off, void *dst, uint32_t len)
{
    (void)ctx;
    if ((uint64_t)off + len > PART) { return false; }
    memcpy(dst, g_flash + off, len);
    return true;
}

static bool fake_erase(void *ctx, uint32_t off, uint32_t len)
{
    (void)ctx;
    if ((uint64_t)off + len > PART || (off % SECTOR) || (len % SECTOR)) {
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
    if (g_flash == NULL) { g_flash = malloc(PART); }
    memset(g_flash, 0xFF, PART);
    g_budget = -1;
    g_writes = 0;
}

/* A picture, and the entry describing it. */
static uint8_t *make(uint32_t bytes, uint16_t board, art_entry_t *e)
{
    static uint8_t *buf;
    static uint32_t cap;
    if (cap < bytes) { buf = realloc(buf, bytes); cap = bytes; }
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
    uint8_t *src = make(206000u, 7u, &e);
    CHECK(!art_store_find(&f, 7u, &e));      /* nothing kept yet */

    src = make(206000u, 7u, &e);
    CHECK(art_store_put(&f, 7u, &e, src));

    art_entry_t got;
    CHECK(art_store_find(&f, 7u, &got));
    CHECK_EQ(got.board, 7);
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
    uint8_t *src = make(50000u, 3u, &e);

    g_budget = 0;                            /* no write survives */
    CHECK(!art_store_put(&f, 3u, &e, src));
    CHECK(!art_store_find(&f, 3u, &e));

    /* The payload lands and the header does not: still nothing. */
    fresh();
    src = make(50000u, 3u, &e);
    g_budget = 1;                            /* the payload only */
    CHECK(!art_store_put(&f, 3u, &e, src));
    CHECK(!art_store_find(&f, 3u, &e));
    CHECK_EQ(g_writes, 2);                   /* it did try the header */
}

TEST_CASE(replacing_a_picture_takes_the_old_one_with_it)
{
    fresh();
    const art_flash_t f = flash();
    art_entry_t e;
    uint8_t *src = make(20000u, 5u, &e);
    CHECK(art_store_put(&f, 5u, &e, src));

    /* A different picture for the same board goes in the same slot. */
    art_entry_t e2;
    uint8_t *src2 = make(30000u, 5u, &e2);
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
        uint8_t *p = make(1000u, b, &x);
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
    uint8_t *src = make(20000u, 9u, &e);
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
    uint8_t *src = make(20000u, 4u, &e);
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
    uint8_t *src = make(20000u, 6u, &e);
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
        uint8_t *p = make(5000u, b, &x);
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
    uint8_t *src = make(1000u, 1u, &e);

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
    uint8_t *src = make(1000u, 1u, &e);
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
    return test_summary("art_store");
}
