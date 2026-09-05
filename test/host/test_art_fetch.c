/*
 * The order of the transactions that fetch a board's photograph.
 *
 * On the panel this runs in the task that beats the safety line, so it can
 * never be tested there. Here the far end is memory and the link is a
 * function that can be made to fail at a chosen transaction, which is how the
 * sequence gets interrupted where an unplugged board would interrupt it.
 *
 * What is under test is not the assembly -- link_artxfer owns that -- but the
 * order: that a block is named before it is read, that a step does the number
 * of blocks it is given and no more, and that every way this can end leaves
 * the caller able to tell whether anything may be kept.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "art_fetch.h"
#include "link_crc.h"
#include "link_pages.h"

#define W 30u
#define H 4u
#define BYTES (W * H * 2u)

static uint8_t  g_src[BYTES];
static uint16_t g_meta[LINK_AW_COUNT];
static uint16_t g_selected;      /* the block the far end was told to serve */

/* How many transactions before the link stops answering; -1 for never. */
static int      g_budget;
static unsigned g_reads, g_writes;
/* Answer with this block whatever was asked for, when >= 0. */
static int      g_serve_instead;

static void make_source(void)
{
    for (unsigned i = 0; i < BYTES; ++i) { g_src[i] = (uint8_t)(i * 5u + 3u); }
    const uint32_t blocks = (BYTES + LINK_AD_BYTES - 1u) / LINK_AD_BYTES;
    g_meta[LINK_AW_BLOCKS]   = (uint16_t)blocks;
    g_meta[LINK_AW_WIDTH]    = (uint16_t)W;
    g_meta[LINK_AW_HEIGHT]   = (uint16_t)H;
    g_meta[LINK_AW_FORMAT]   = (uint16_t)LINK_ART_RGB565;
    g_meta[LINK_AW_BYTES_LO] = (uint16_t)(BYTES & 0xFFFFu);
    g_meta[LINK_AW_BYTES_HI] = (uint16_t)(BYTES >> 16);
    g_meta[LINK_AW_CRC]      = link_crc(0u, g_src, BYTES);
}

static bool spent(void)
{
    return g_budget >= 0 && (int)(g_reads + g_writes) > g_budget;
}

static bool dev_read(void *ctx, uint8_t page, uint8_t count, uint16_t *regs)
{
    (void)ctx;
    ++g_reads;
    if (spent()) { return false; }
    if (page == (uint8_t)LINK_PAGE_ARTWORK) {
        for (uint8_t i = 0; i < count; ++i) { regs[i] = g_meta[i]; }
        return true;
    }
    if (page != (uint8_t)LINK_PAGE_ART_DATA) { return false; }
    const uint16_t block = (g_serve_instead >= 0) ? (uint16_t)g_serve_instead
                                                  : g_selected;
    for (uint8_t i = 0; i < count; ++i) { regs[i] = 0u; }
    regs[LINK_AD_BLOCK] = block;
    const uint32_t at = (uint32_t)block * LINK_AD_BYTES;
    for (unsigned i = 0; i < LINK_AD_BYTES && at + i < BYTES; ++i) {
        const uint16_t b = g_src[at + i];
        regs[LINK_AD_DATA + i / 2u] |=
            (uint16_t)((i & 1u) ? (uint16_t)(b << 8) : b);
    }
    return true;
}

static bool dev_write(void *ctx, uint8_t page, uint8_t reg, uint16_t value)
{
    (void)ctx;
    ++g_writes;
    if (spent()) { return false; }
    if (page != (uint8_t)LINK_PAGE_ART_DATA || reg != (uint8_t)LINK_AD_BLOCK) {
        return false;
    }
    g_selected = value;
    return true;
}

static art_transport_t link(void)
{
    art_transport_t t = { dev_read, dev_write, NULL };
    return t;
}

static void fresh(void)
{
    make_source();
    g_budget = -1;
    g_reads = g_writes = 0;
    g_selected = 0xFFFFu;
    g_serve_instead = -1;
}

TEST_CASE(a_photograph_arrives_a_block_at_a_time)
{
    fresh();
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    CHECK(art_fetch_meta(&t, meta, &bytes));
    CHECK_EQ(bytes, BYTES);

    uint8_t got[BYTES];
    art_fetch_t f;
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));

    unsigned steps = 0;
    while (art_fetch_step(&f, &t, 1u) == ART_FETCH_RUNNING) {
        if (++steps > 100u) { T_FAIL("the fetch does not end"); return; }
    }
    CHECK_EQ(art_fetch_step(&f, &t, 1u), ART_FETCH_DONE);
    CHECK_EQ(memcmp(got, g_src, BYTES), 0);
    CHECK_EQ(art_fetch_width(&f), W);
    CHECK_EQ(art_fetch_height(&f), H);
    CHECK_EQ(art_fetch_bytes(&f), BYTES);
    CHECK_EQ(art_fetch_crc(&f), g_meta[LINK_AW_CRC]);

    /* Two transactions a block: the block is named before it is read. */
    CHECK_EQ(g_writes, g_meta[LINK_AW_BLOCKS]);
}

TEST_CASE(a_step_does_the_blocks_it_is_given_and_no_more)
{
    /*
     * The caller owns how much of its own time to spend, because on the
     * panel it also beats the safety line. A step that ran to completion
     * would take that decision away.
     */
    fresh();
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    CHECK(art_fetch_meta(&t, meta, &bytes));
    uint8_t got[BYTES];
    art_fetch_t f;
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));

    const unsigned before = g_writes;
    CHECK_EQ(art_fetch_step(&f, &t, 2u), ART_FETCH_RUNNING);
    CHECK_EQ(g_writes - before, 2);
    CHECK_EQ(art_fetch_step(&f, &t, 1u), ART_FETCH_RUNNING);
    CHECK_EQ(g_writes - before, 3);
}

TEST_CASE(a_link_that_stops_answering_stops_the_fetch)
{
    /* An unplugged board mid-transfer. Nothing may be kept, and the caller
     * has to be able to tell. */
    fresh();
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    CHECK(art_fetch_meta(&t, meta, &bytes));
    uint8_t got[BYTES];
    art_fetch_t f;
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));

    g_budget = (int)(g_reads + g_writes) + 2;   /* one block, then silence */
    art_fetch_state_t st = ART_FETCH_RUNNING;
    for (int i = 0; i < 10 && st == ART_FETCH_RUNNING; ++i) {
        st = art_fetch_step(&f, &t, 1u);
    }
    CHECK_EQ(st, ART_FETCH_FAILED);
    CHECK(art_fetch_why(&f)[0] != '\0');
    /* And it stays failed rather than carrying on if it is stepped again. */
    g_budget = -1;
    CHECK_EQ(art_fetch_step(&f, &t, 1u), ART_FETCH_FAILED);
}

TEST_CASE(a_block_that_was_not_asked_for_stops_the_fetch)
{
    /*
     * The far end answering with the wrong block is the failure that would
     * otherwise be silent: the bytes are well formed and belong somewhere
     * else in the picture.
     */
    fresh();
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    CHECK(art_fetch_meta(&t, meta, &bytes));
    uint8_t got[BYTES];
    art_fetch_t f;
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));

    g_serve_instead = 2;                        /* always the third block */
    CHECK_EQ(art_fetch_step(&f, &t, 4u), ART_FETCH_FAILED);
    CHECK(art_fetch_why(&f)[0] != '\0');
}

TEST_CASE(a_picture_that_arrives_whole_and_wrong_is_not_done)
{
    fresh();
    g_meta[LINK_AW_CRC] ^= 0xFFFFu;             /* not this picture's sum */
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    CHECK(art_fetch_meta(&t, meta, &bytes));
    uint8_t got[BYTES];
    art_fetch_t f;
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));

    art_fetch_state_t st = ART_FETCH_RUNNING;
    for (int i = 0; i < 20 && st == ART_FETCH_RUNNING; ++i) {
        st = art_fetch_step(&f, &t, 1u);
    }
    CHECK_EQ(st, ART_FETCH_FAILED);             /* whole, and not the picture */
}

TEST_CASE(a_board_with_no_photograph_is_not_a_failure)
{
    /* The ordinary answer from a coprocessor that carries none, and from one
     * built before the page: neither is a fault, and neither starts a
     * ten-second transfer. */
    fresh();
    g_meta[LINK_AW_BLOCKS] = 0u;
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 1u;
    CHECK(!art_fetch_meta(&t, meta, &bytes));

    fresh();
    g_budget = 0;                               /* the page is not there */
    CHECK(!art_fetch_meta(&t, meta, &bytes));
}

TEST_CASE(a_picture_there_is_no_room_for_is_refused_before_the_wait)
{
    fresh();
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    CHECK(art_fetch_meta(&t, meta, &bytes));
    uint8_t small[BYTES - 1u];
    art_fetch_t f;
    CHECK(!art_fetch_begin(&f, meta, small, sizeof(small)));
    CHECK_EQ(art_fetch_step(&f, &t, 1u), ART_FETCH_FAILED);
    CHECK(art_fetch_why(&f)[0] != '\0');
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    fresh();
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    uint8_t got[BYTES];
    art_fetch_t f;

    CHECK(!art_fetch_meta(NULL, meta, &bytes));
    CHECK(!art_fetch_meta(&t, NULL, &bytes));
    CHECK(!art_fetch_meta(&t, meta, NULL));
    CHECK(!art_fetch_begin(NULL, meta, got, sizeof(got)));
    CHECK_EQ(art_fetch_step(NULL, &t, 1u), ART_FETCH_FAILED);

    CHECK(art_fetch_meta(&t, meta, &bytes));
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));
    CHECK_EQ(art_fetch_step(&f, NULL, 1u), ART_FETCH_FAILED);

    CHECK_EQ(art_fetch_width(NULL), 0);
    CHECK_EQ(art_fetch_height(NULL), 0);
    CHECK_EQ(art_fetch_crc(NULL), 0);
    CHECK_EQ(art_fetch_bytes(NULL), 0);
    CHECK(art_fetch_why(NULL)[0] == '\0');
}

int main(void)
{
    RUN(a_photograph_arrives_a_block_at_a_time);
    RUN(a_step_does_the_blocks_it_is_given_and_no_more);
    RUN(a_link_that_stops_answering_stops_the_fetch);
    RUN(a_block_that_was_not_asked_for_stops_the_fetch);
    RUN(a_picture_that_arrives_whole_and_wrong_is_not_done);
    RUN(a_board_with_no_photograph_is_not_a_failure);
    RUN(a_picture_there_is_no_room_for_is_refused_before_the_wait);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("art_fetch");
}
