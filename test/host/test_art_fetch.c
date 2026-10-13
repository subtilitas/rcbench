/*
 * The order of the transactions that fetch a board's drawing.
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
#include "drawing_fixture.h"
#include "link_crc.h"
#include "link_drawing.h"
#include "link_pages.h"

/* A drawing of 4 pads and 30 shapes, 408 bytes: six full blocks of 62
 * and a part one. */
#define PADS   4u
#define SHAPES 30u
#define W 1000u
#define H 500u
#define BYTES (LINK_DRAW_HEAD_BYTES + PADS * LINK_DRAW_PAD_BYTES \
               + SHAPES * LINK_DRAW_SHAPE_BYTES)

static uint8_t  g_src[BYTES];
static uint16_t g_meta[LINK_AW_COUNT];
static uint16_t g_selected;      /* the block the far end was told to serve */

/* How many transactions before the link stops answering; -1 for never. */
static int      g_budget;
static unsigned g_reads, g_writes;
/* Answer with this block whatever was asked for, when >= 0. */
static int      g_serve_instead;

/* The ARTWORK registers for whatever g_src holds now. */
static void describe_source(void)
{
    link_artserve_meta(g_meta, (uint16_t)LINK_ART_DRAWING, (uint16_t)W,
                       (uint16_t)H, BYTES, link_crc(0u, g_src, BYTES),
                       link_fnv1a32(LINK_FNV_INIT, g_src, BYTES));
}

static void make_source(void)
{
    uint8_t small[FX_SMALL_BYTES];
    (void)fx_draw_small(small);
    fx_draw_head(g_src, PADS, SHAPES, W, H);
    memcpy(g_src + LINK_DRAW_HEAD_BYTES, small + LINK_DRAW_HEAD_BYTES,
           PADS * LINK_DRAW_PAD_BYTES);
    for (unsigned i = 0; i < SHAPES; ++i) {
        fx_draw_shape(g_src, PADS, i, (uint8_t)LINK_DRAW_RECT, 0u,
                      (uint16_t)(0x1000u + i), (uint16_t)(400u + i * 10u),
                      (uint16_t)(50u + i), 50u, 40u);
    }
    describe_source();
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
    /* The coprocessor's own code serves the block. */
    uint16_t full[LINK_AD_COUNT];
    link_artserve_block(full, g_src, BYTES, block);
    for (uint8_t i = 0; i < count; ++i) { regs[i] = full[i]; }
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

TEST_CASE(a_drawing_arrives_a_block_at_a_time)
{
    fresh();
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
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
    CHECK_EQ(art_fetch_format(&f), LINK_ART_DRAWING);
    CHECK_EQ(art_fetch_width(&f), W);
    CHECK_EQ(art_fetch_height(&f), H);
    CHECK_EQ(art_fetch_bytes(&f), BYTES);
    CHECK_EQ(art_fetch_crc(&f), g_meta[LINK_AW_CRC]);
    CHECK_EQ(art_fetch_hash(&f), link_fnv1a32(LINK_FNV_INIT, g_src, BYTES));

    /* Two transactions a block: the block is named before it is read. */
    CHECK_EQ(g_meta[LINK_AW_BLOCKS], 7);
    CHECK_EQ(g_writes, g_meta[LINK_AW_BLOCKS]);

    /* And what arrived is a drawing the panel reads. */
    link_drawing_t d;
    CHECK(link_drawing_parse(&d, got, BYTES, NULL));
    CHECK_EQ(d.pads, PADS);
    CHECK_EQ(d.shapes, SHAPES);
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
    CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
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
    CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
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

TEST_CASE(a_link_lost_at_any_transaction_never_ends_done)
{
    /*
     * Every transaction of the fetch, in turn, is the one the link fails
     * at: the ARTWORK read, any block number written, any block read.  None
     * of them ends DONE, and each names why.
     */
    fresh();
    const unsigned total = 1u + 2u * ((BYTES + LINK_AD_BYTES - 1u)
                                      / LINK_AD_BYTES);
    for (unsigned cut = 0; cut < total; ++cut) {
        fresh();
        g_budget = (int)cut;
        const art_transport_t t = link();
        uint16_t meta[LINK_AW_COUNT];
        uint32_t bytes = 0;
        const char *why = NULL;
        if (!art_fetch_meta(&t, meta, &bytes, &why)) {
            CHECK_EQ(cut, 0);
            CHECK_EQ(bytes, 0);
            CHECK(why != NULL && why[0] != '\0');
            continue;
        }
        uint8_t got[BYTES];
        art_fetch_t f;
        CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));
        art_fetch_state_t st = ART_FETCH_RUNNING;
        for (unsigned i = 0; i < 40u && st == ART_FETCH_RUNNING; ++i) {
            st = art_fetch_step(&f, &t, 1u);
        }
        CHECK_EQ(st, ART_FETCH_FAILED);
        CHECK(art_fetch_why(&f)[0] != '\0');
    }
    /* One transaction more and it is whole. */
    fresh();
    g_budget = (int)total;
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
    uint8_t got[BYTES];
    art_fetch_t f;
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));
    art_fetch_state_t st = ART_FETCH_RUNNING;
    for (unsigned i = 0; i < 40u && st == ART_FETCH_RUNNING; ++i) {
        st = art_fetch_step(&f, &t, 1u);
    }
    CHECK_EQ(st, ART_FETCH_DONE);
}

TEST_CASE(a_fetch_abandoned_mid_way_and_begun_again_starts_from_block_0)
{
    /*
     * The panel stops a fetch when the link drops or the board changes and
     * begins a new one when it comes back.  The new one asks for block 0
     * and assembles the drawing the far end serves now, not a mixture.
     */
    fresh();
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
    uint8_t got[BYTES];
    art_fetch_t f;
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));
    CHECK_EQ(art_fetch_step(&f, &t, 3u), ART_FETCH_RUNNING);
    CHECK_EQ(link_artxfer_next(&f.x), 3);

    /* Meanwhile the coprocessor was flashed with another drawing. */
    fx_draw_shape(g_src, PADS, 0, (uint8_t)LINK_DRAW_RECT, 0u, 0xF800u,
                  400u, 50u, 60u, 40u);
    describe_source();
    CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));
    CHECK_EQ(link_artxfer_next(&f.x), 0);
    art_fetch_state_t st = ART_FETCH_RUNNING;
    for (unsigned i = 0; i < 40u && st == ART_FETCH_RUNNING; ++i) {
        st = art_fetch_step(&f, &t, 1u);
    }
    CHECK_EQ(st, ART_FETCH_DONE);
    CHECK_EQ(memcmp(got, g_src, BYTES), 0);

    /* Had the old fetch carried on over the new drawing, its checksums
     * would have refused the mixture. */
    fresh();
    CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));
    CHECK_EQ(art_fetch_step(&f, &t, 3u), ART_FETCH_RUNNING);
    fx_draw_shape(g_src, PADS, SHAPES - 1u, (uint8_t)LINK_DRAW_RECT, 0u,
                  0xF800u, 400u, 50u, 60u, 40u);
    st = ART_FETCH_RUNNING;
    for (unsigned i = 0; i < 40u && st == ART_FETCH_RUNNING; ++i) {
        st = art_fetch_step(&f, &t, 1u);
    }
    CHECK_EQ(st, ART_FETCH_FAILED);
}

TEST_CASE(a_block_that_was_not_asked_for_stops_the_fetch)
{
    /*
     * The far end answering with the wrong block is the failure that would
     * otherwise be silent: the bytes are well formed and belong somewhere
     * else in the drawing.
     */
    fresh();
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
    uint8_t got[BYTES];
    art_fetch_t f;
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));

    g_serve_instead = 2;                        /* always the third block */
    CHECK_EQ(art_fetch_step(&f, &t, 4u), ART_FETCH_FAILED);
    CHECK(art_fetch_why(&f)[0] != '\0');
}

TEST_CASE(a_drawing_that_arrives_whole_and_wrong_is_not_done)
{
    /* The CRC wrong, the hash's either half wrong: each refused. */
    for (int which = 0; which < 3; ++which) {
        fresh();
        g_meta[(which == 0) ? LINK_AW_CRC
               : (which == 1) ? LINK_AW_HASH_LO : LINK_AW_HASH_HI] ^= 0x0001u;
        const art_transport_t t = link();
        uint16_t meta[LINK_AW_COUNT];
        uint32_t bytes = 0;
        CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
        uint8_t got[BYTES];
        art_fetch_t f;
        CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));

        art_fetch_state_t st = ART_FETCH_RUNNING;
        for (int i = 0; i < 20 && st == ART_FETCH_RUNNING; ++i) {
            st = art_fetch_step(&f, &t, 1u);
        }
        CHECK_EQ(st, ART_FETCH_FAILED);     /* whole, and not the drawing */
        CHECK_STR_EQ(art_fetch_why(&f), "it did not match its own checksums");
    }
}

TEST_CASE(a_malformed_drawing_with_good_checksums_is_refused_by_the_reader)
{
    /*
     * The transfer checks that the bytes are the ones the coprocessor
     * meant; it does not read them.  A coprocessor that serves a drawing
     * with a shape off its outline delivers it whole and DONE, and
     * link_drawing_parse() is what refuses it.
     */
    fresh();
    fx_draw_shape(g_src, PADS, 7, (uint8_t)LINK_DRAW_RECT, 0u, 0u,
                  990u, 50u, 20u, 40u);       /* 990 + 20 is past 1000 */
    describe_source();
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
    uint8_t got[BYTES];
    art_fetch_t f;
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));
    art_fetch_state_t st = ART_FETCH_RUNNING;
    for (int i = 0; i < 20 && st == ART_FETCH_RUNNING; ++i) {
        st = art_fetch_step(&f, &t, 1u);
    }
    CHECK_EQ(st, ART_FETCH_DONE);
    link_drawing_t d;
    const char *why = NULL;
    CHECK(!link_drawing_parse(&d, got, BYTES, &why));
    CHECK(why != NULL && why[0] != '\0');
}

TEST_CASE(the_three_ways_there_is_no_drawing_are_told_apart)
{
    /*
     * No drawing, no page, and a page that disagrees with itself all end the
     * same way, and only the last is a fault. A log line that reported all
     * three as "carries no drawing" would hide the one worth reading.
     */
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 1u;
    const char *a = NULL, *b = NULL, *c = NULL;

    fresh();
    g_meta[LINK_AW_BLOCKS] = 0u;
    CHECK(!art_fetch_meta(&t, meta, &bytes, &a));
    CHECK_EQ(bytes, 0);                       /* nothing to allocate from */

    fresh();
    g_budget = 0;                             /* the page is not there */
    bytes = 1u;
    CHECK(!art_fetch_meta(&t, meta, &bytes, &b));
    CHECK_EQ(bytes, 0);

    fresh();
    g_meta[LINK_AW_BLOCKS] = (uint16_t)(g_meta[LINK_AW_BLOCKS] + 1u);
                                              /* disagrees with itself */
    bytes = 1u;
    CHECK(!art_fetch_meta(&t, meta, &bytes, &c));
    CHECK_EQ(bytes, 0);

    /* Three reasons, and none of them the same sentence. */
    CHECK(a != NULL && b != NULL && c != NULL);
    CHECK(a[0] != '\0' && b[0] != '\0' && c[0] != '\0');
    CHECK(strcmp(a, b) != 0);
    CHECK(strcmp(a, c) != 0);
    CHECK(strcmp(b, c) != 0);

    /* A photograph from a coprocessor of minor 19 is the third kind: a page
     * this build cannot use. */
    fresh();
    g_meta[LINK_AW_FORMAT] = (uint16_t)LINK_ART_RGB565;
    const char *d = NULL;
    CHECK(!art_fetch_meta(&t, meta, &bytes, &d));
    CHECK_STR_EQ(d, c);
}

TEST_CASE(the_key_of_a_drawing_is_every_field_of_its_page)
{
    fresh();
    art_entry_t key;
    CHECK(art_fetch_key(g_meta, 7u, &key));
    CHECK_EQ(key.board, 7);
    CHECK_EQ(key.format, LINK_ART_DRAWING);
    CHECK_EQ(key.width, W);
    CHECK_EQ(key.height, H);
    CHECK_EQ(key.bytes, BYTES);
    CHECK_EQ(key.crc, link_crc(0u, g_src, BYTES));
    CHECK_EQ(key.hash, link_fnv1a32(LINK_FNV_INIT, g_src, BYTES));

    /* Another drawing for the same board: another key. */
    art_entry_t other;
    fx_draw_shape(g_src, PADS, 3, (uint8_t)LINK_DRAW_RECT, 0u, 0xFFFFu,
                  430u, 53u, 50u, 40u);
    describe_source();
    CHECK(art_fetch_key(g_meta, 7u, &other));
    CHECK(!art_entry_same(&key, &other));
    CHECK_EQ(other.bytes, key.bytes);

    /* A page that cannot be a drawing has no key, and leaves none. */
    g_meta[LINK_AW_FORMAT] = (uint16_t)LINK_ART_RGB565;
    memset(&other, 0xA5, sizeof(other));
    CHECK(!art_fetch_key(g_meta, 7u, &other));
    CHECK_EQ(other.bytes, 0);
    CHECK_EQ(other.hash, 0);
    CHECK(!art_fetch_key(NULL, 7u, &other));
    CHECK(!art_fetch_key(g_meta, 7u, NULL));
}

TEST_CASE(a_board_with_no_drawing_is_not_a_failure)
{
    /* The ordinary answer from a coprocessor that carries none, and from one
     * built before the page: neither is a fault, and neither starts a
     * transfer. */
    fresh();
    g_meta[LINK_AW_BLOCKS] = 0u;
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 1u;
    CHECK(!art_fetch_meta(&t, meta, &bytes, NULL));

    fresh();
    g_budget = 0;                               /* the page is not there */
    CHECK(!art_fetch_meta(&t, meta, &bytes, NULL));
}

TEST_CASE(a_drawing_there_is_no_room_for_is_refused_before_the_wait)
{
    fresh();
    const art_transport_t t = link();
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0;
    CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
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

    CHECK(!art_fetch_meta(NULL, meta, &bytes, NULL));
    CHECK(!art_fetch_meta(&t, NULL, &bytes, NULL));
    CHECK(!art_fetch_meta(&t, meta, NULL, NULL));
    CHECK(!art_fetch_begin(NULL, meta, got, sizeof(got)));
    CHECK_EQ(art_fetch_step(NULL, &t, 1u), ART_FETCH_FAILED);

    CHECK(art_fetch_meta(&t, meta, &bytes, NULL));
    CHECK(art_fetch_begin(&f, meta, got, sizeof(got)));
    CHECK_EQ(art_fetch_step(&f, NULL, 1u), ART_FETCH_FAILED);

    CHECK_EQ(art_fetch_width(NULL), 0);
    CHECK_EQ(art_fetch_height(NULL), 0);
    CHECK_EQ(art_fetch_crc(NULL), 0);
    CHECK_EQ(art_fetch_format(NULL), 0);
    CHECK_EQ(art_fetch_hash(NULL), 0);
    CHECK_EQ(art_fetch_bytes(NULL), 0);
    CHECK(art_fetch_why(NULL)[0] == '\0');
}

int main(void)
{
    RUN(a_drawing_arrives_a_block_at_a_time);
    RUN(a_step_does_the_blocks_it_is_given_and_no_more);
    RUN(a_link_that_stops_answering_stops_the_fetch);
    RUN(a_link_lost_at_any_transaction_never_ends_done);
    RUN(a_fetch_abandoned_mid_way_and_begun_again_starts_from_block_0);
    RUN(a_block_that_was_not_asked_for_stops_the_fetch);
    RUN(a_drawing_that_arrives_whole_and_wrong_is_not_done);
    RUN(a_malformed_drawing_with_good_checksums_is_refused_by_the_reader);
    RUN(the_three_ways_there_is_no_drawing_are_told_apart);
    RUN(the_key_of_a_drawing_is_every_field_of_its_page);
    RUN(a_board_with_no_drawing_is_not_a_failure);
    RUN(a_drawing_there_is_no_room_for_is_refused_before_the_wait);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("art_fetch");
}
