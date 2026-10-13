/*
 * Assembling a board drawing out of the blocks the link carries it in.
 *
 * The failure under test is not a slow transfer, it is a quiet one: a block
 * lost and skipped, or a reply for a block other than the one asked for,
 * assembled into a drawing that is wrong in a way nobody sees until it has
 * been kept in flash and an operator wires a lead by it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_artxfer.h"
#include "link_crc.h"
#include "link_pages.h"

/* A payload small enough to hold here and big enough to need several
 * blocks: 240 bytes is three full blocks of 62 and a part one.  The
 * transfer does not read what the bytes say, so they need not be a
 * drawing. */
#define W 1000u
#define H 500u
#define BYTES 240u

static uint8_t  g_src[BYTES];
static uint16_t g_meta[LINK_AW_COUNT];

static void make_source(void)
{
    for (unsigned i = 0; i < BYTES; ++i) {
        g_src[i] = (uint8_t)(i * 7u + 1u);      /* anything not uniform */
    }
    link_artserve_meta(g_meta, (uint16_t)LINK_ART_DRAWING, (uint16_t)W,
                       (uint16_t)H, BYTES, link_crc(0u, g_src, BYTES),
                       link_fnv1a32(LINK_FNV_INIT, g_src, BYTES));
}

/* The window the coprocessor answers with for one block. */
static void serve(uint16_t block, uint16_t *out)
{
    link_artserve_block(out, g_src, BYTES, block);
}

TEST_CASE(a_drawing_arrives_byte_for_byte)
{
    make_source();
    uint8_t got[BYTES];
    link_artxfer_t x;
    CHECK(link_artxfer_begin(&x, g_meta, got, sizeof(got)));
    CHECK(!link_artxfer_complete(&x));

    unsigned rounds = 0;
    while (!link_artxfer_complete(&x)) {
        uint16_t win[LINK_AD_COUNT];
        serve(link_artxfer_next(&x), win);
        CHECK(link_artxfer_take(&x, win));
        if (++rounds > 100u) { T_FAIL("the transfer does not end"); }
    }
    CHECK_EQ(rounds, g_meta[LINK_AW_BLOCKS]);
    CHECK(link_artxfer_verify(&x));
    CHECK_EQ(memcmp(got, g_src, BYTES), 0);
}

TEST_CASE(a_block_that_was_not_asked_for_is_refused_rather_than_placed)
{
    make_source();
    uint8_t got[BYTES];
    memset(got, 0xAA, sizeof(got));
    link_artxfer_t x;
    CHECK(link_artxfer_begin(&x, g_meta, got, sizeof(got)));

    uint16_t win[LINK_AD_COUNT];
    /* The second block, while the first is what is wanted. */
    serve(1u, win);
    CHECK(!link_artxfer_take(&x, win));
    CHECK_EQ(link_artxfer_next(&x), 0);
    /* Nothing was written: the buffer is untouched. */
    for (unsigned i = 0; i < BYTES; ++i) { CHECK_EQ(got[i], 0xAA); }

    /* The one asked for is taken, and the same one again is then refused --
     * a duplicate reply must not advance the assembly past a block. */
    serve(0u, win);
    CHECK(link_artxfer_take(&x, win));
    CHECK_EQ(link_artxfer_next(&x), 1);
    serve(0u, win);
    CHECK(!link_artxfer_take(&x, win));
    CHECK_EQ(link_artxfer_next(&x), 1);
}

TEST_CASE(a_drawing_that_does_not_match_its_own_checksum_is_not_accepted)
{
    make_source();
    uint8_t got[BYTES];
    link_artxfer_t x;
    CHECK(link_artxfer_begin(&x, g_meta, got, sizeof(got)));

    /* Every block arrives, in order, and one byte of one of them is wrong --
     * a corruption the link's own CRC did not catch because the frame it
     * rode in was well formed. */
    const uint16_t total = g_meta[LINK_AW_BLOCKS];
    for (uint16_t b = 0; b < total; ++b) {
        uint16_t win[LINK_AD_COUNT];
        serve(b, win);
        if (b == 1u) { win[LINK_AD_DATA] ^= 0x0100u; }
        CHECK(link_artxfer_take(&x, win));
    }
    CHECK(link_artxfer_complete(&x));
    CHECK(!link_artxfer_verify(&x));       /* complete, and not the drawing */
}

TEST_CASE(metadata_that_cannot_describe_a_drawing_is_refused_before_the_wait)
{
    make_source();
    uint8_t got[BYTES];
    link_artxfer_t x;
    uint16_t m[LINK_AW_COUNT];

    /* No drawing at all, which is the ordinary answer from a coprocessor
     * that carries none. */
    memcpy(m, g_meta, sizeof(m));
    m[LINK_AW_BLOCKS] = 0u;
    CHECK(!link_artxfer_begin(&x, m, got, sizeof(got)));

    /* A format this build cannot read: an unknown one, and the photograph
     * a coprocessor of minor 19 or older serves. */
    memcpy(m, g_meta, sizeof(m));
    m[LINK_AW_FORMAT] = 99u;
    CHECK(!link_artxfer_begin(&x, m, got, sizeof(got)));
    m[LINK_AW_FORMAT] = (uint16_t)LINK_ART_RGB565;
    CHECK(!link_artxfer_begin(&x, m, got, sizeof(got)));
    m[LINK_AW_FORMAT] = (uint16_t)LINK_ART_NONE;
    CHECK(!link_artxfer_begin(&x, m, got, sizeof(got)));

    /*
     * A block count that disagrees with the length.  Blocks would run out
     * early or late, and the drawing is not what it says either way.
     */
    memcpy(m, g_meta, sizeof(m));
    m[LINK_AW_BLOCKS] = (uint16_t)(m[LINK_AW_BLOCKS] + 1u);
    CHECK(!link_artxfer_begin(&x, m, got, sizeof(got)));
    m[LINK_AW_BLOCKS] = (uint16_t)(m[LINK_AW_BLOCKS] - 2u);
    CHECK(!link_artxfer_begin(&x, m, got, sizeof(got)));

    /* An outline of no width or no height. */
    memcpy(m, g_meta, sizeof(m));
    m[LINK_AW_WIDTH] = 0u;
    CHECK(!link_artxfer_begin(&x, m, got, sizeof(got)));
    memcpy(m, g_meta, sizeof(m));
    m[LINK_AW_HEIGHT] = 0u;
    CHECK(!link_artxfer_begin(&x, m, got, sizeof(got)));

    /* And one there is nowhere to put: a transfer that cannot end well
     * does not take the link first. */
    CHECK(!link_artxfer_begin(&x, g_meta, got, BYTES - 1u));
}

/* One payload per block count, at both ends of what a drawing may be. */
static bool meta_for(uint32_t bytes, link_art_meta_t *out)
{
    uint16_t m[LINK_AW_COUNT];
    link_artserve_meta(m, (uint16_t)LINK_ART_DRAWING, (uint16_t)W,
                       (uint16_t)H, bytes, 0u, 0u);
    return link_artxfer_meta_ok(m, out);
}

TEST_CASE(a_length_from_a_header_to_the_largest_drawing_is_taken)
{
    /*
     * The length sizes an allocation before a byte is seen, so it is held
     * to what a drawing can be: a header at least, LINK_DRAW_MAX_BYTES at
     * most.  The 32-bit length could otherwise ask for four gigabytes.
     */
    link_art_meta_t meta;
    CHECK(!meta_for(LINK_DRAW_HEAD_BYTES - 1u, &meta));
    CHECK(meta_for(LINK_DRAW_HEAD_BYTES, &meta));
    CHECK_EQ(meta.blocks, 1);
    CHECK(meta_for(LINK_AD_BYTES, &meta));
    CHECK_EQ(meta.blocks, 1);
    CHECK(meta_for(LINK_AD_BYTES + 1u, &meta));
    CHECK_EQ(meta.blocks, 2);
    CHECK(meta_for(LINK_DRAW_MAX_BYTES, &meta));
    CHECK_EQ(meta.blocks,
             (LINK_DRAW_MAX_BYTES + LINK_AD_BYTES - 1u) / LINK_AD_BYTES);
    CHECK(!meta_for(LINK_DRAW_MAX_BYTES + 1u, &meta));
    CHECK(!meta_for(0xFFFFFFFFu, &meta));

    /* A length past 64 kB arrives in both halves, and is refused whole
     * rather than read as its low half. */
    uint16_t m[LINK_AW_COUNT];
    link_artserve_meta(m, (uint16_t)LINK_ART_DRAWING, (uint16_t)W,
                       (uint16_t)H, 0x10000u + 240u, 0u, 0u);
    CHECK_EQ(m[LINK_AW_BYTES_LO], 240);
    CHECK_EQ(m[LINK_AW_BYTES_HI], 1);
    CHECK(!link_artxfer_meta_ok(m, &meta));
}

TEST_CASE(the_hash_is_read_from_both_halves_and_checked)
{
    make_source();
    link_art_meta_t meta;
    CHECK(link_artxfer_meta_ok(g_meta, &meta));
    CHECK_EQ(meta.hash, link_fnv1a32(LINK_FNV_INIT, g_src, BYTES));
    CHECK_EQ(meta.crc, link_crc(0u, g_src, BYTES));
    CHECK_EQ(meta.bytes, BYTES);
    CHECK_EQ(meta.format, LINK_ART_DRAWING);

    /*
     * Every block arrives intact and the CRC agrees, but the hash the page
     * declares is another drawing's: refused.  The CRC alone is sixteen
     * bits, and the hash is what keeps two drawings that share one apart.
     */
    uint16_t m[LINK_AW_COUNT];
    for (unsigned half = 0; half < 2u; ++half) {
        memcpy(m, g_meta, sizeof(m));
        m[half ? LINK_AW_HASH_HI : LINK_AW_HASH_LO] ^= 0x0001u;
        uint8_t got[BYTES];
        link_artxfer_t x;
        CHECK(link_artxfer_begin(&x, m, got, sizeof(got)));
        while (!link_artxfer_complete(&x)) {
            uint16_t win[LINK_AD_COUNT];
            serve(link_artxfer_next(&x), win);
            CHECK(link_artxfer_take(&x, win));
        }
        CHECK(!link_artxfer_verify(&x));
    }
}

TEST_CASE(the_coprocessor_serves_what_the_panel_assembles)
{
    make_source();
    /* No payload: every register zero, which is "no drawing". */
    uint16_t m[LINK_AW_COUNT];
    memset(m, 0xA5, sizeof(m));
    link_artserve_meta(m, (uint16_t)LINK_ART_DRAWING, (uint16_t)W,
                       (uint16_t)H, 0u, 0x1234u, 0x12345678u);
    for (unsigned i = 0; i < LINK_AW_COUNT; ++i) { CHECK_EQ(m[i], 0); }

    /* A block's bytes, the low byte of a register first; the last block
     * pads with zeros, and a block past the end carries only its number. */
    uint16_t win[LINK_AD_COUNT];
    serve(0u, win);
    CHECK_EQ(win[LINK_AD_BLOCK], 0);
    CHECK_EQ(win[LINK_AD_DATA], (uint16_t)(g_src[0] | (g_src[1] << 8)));
    const uint16_t last = (uint16_t)(g_meta[LINK_AW_BLOCKS] - 1u);
    serve(last, win);
    CHECK_EQ(win[LINK_AD_BLOCK], last);
    const unsigned tail = BYTES - (unsigned)last * LINK_AD_BYTES;
    CHECK(tail < LINK_AD_BYTES);
    for (unsigned i = (tail + 1u) / 2u; i < LINK_AD_WORDS; ++i) {
        CHECK_EQ(win[LINK_AD_DATA + i], 0);
    }
    serve((uint16_t)(last + 1u), win);
    CHECK_EQ(win[LINK_AD_BLOCK], last + 1u);
    for (unsigned i = 0; i < LINK_AD_WORDS; ++i) {
        CHECK_EQ(win[LINK_AD_DATA + i], 0);
    }
    serve(0xFFFFu, win);
    CHECK_EQ(win[LINK_AD_BLOCK], 0xFFFF);
    CHECK_EQ(win[LINK_AD_DATA], 0);

    /* NULL registers are left alone; NULL payload serves zeros. */
    link_artserve_meta(NULL, 2u, 1u, 1u, 16u, 0u, 0u);
    link_artserve_block(NULL, g_src, BYTES, 0u);
    link_artserve_block(win, NULL, BYTES, 0u);
    CHECK_EQ(win[LINK_AD_DATA], 0);
}

TEST_CASE(a_transfer_that_has_ended_takes_nothing_more)
{
    make_source();
    uint8_t got[BYTES];
    link_artxfer_t x;
    CHECK(link_artxfer_begin(&x, g_meta, got, sizeof(got)));
    const uint16_t total = g_meta[LINK_AW_BLOCKS];
    for (uint16_t b = 0; b < total; ++b) {
        uint16_t win[LINK_AD_COUNT];
        serve(b, win);
        CHECK(link_artxfer_take(&x, win));
    }
    CHECK(link_artxfer_verify(&x));

    /* A late duplicate of the last block must not run off the buffer. */
    uint16_t win[LINK_AD_COUNT];
    serve((uint16_t)(total - 1u), win);
    CHECK(!link_artxfer_take(&x, win));
    CHECK(link_artxfer_verify(&x));
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    make_source();
    uint8_t got[BYTES];
    uint16_t win[LINK_AD_COUNT];
    link_artxfer_t x;
    CHECK(!link_artxfer_begin(NULL, g_meta, got, sizeof(got)));
    CHECK(!link_artxfer_begin(&x, NULL, got, sizeof(got)));
    CHECK(!link_artxfer_begin(&x, g_meta, NULL, sizeof(got)));
    CHECK_EQ(link_artxfer_next(NULL), 0);
    CHECK(!link_artxfer_complete(NULL));
    CHECK(!link_artxfer_verify(NULL));
    /* And nothing may be taken before a transfer has begun. */
    memset(&x, 0, sizeof(x));
    serve(0u, win);
    CHECK(!link_artxfer_take(&x, win));
    CHECK(!link_artxfer_take(NULL, win));
    CHECK(link_artxfer_begin(&x, g_meta, got, sizeof(got)));
    CHECK(!link_artxfer_take(&x, NULL));
}

int main(void)
{
    RUN(a_drawing_arrives_byte_for_byte);
    RUN(a_block_that_was_not_asked_for_is_refused_rather_than_placed);
    RUN(a_drawing_that_does_not_match_its_own_checksum_is_not_accepted);
    RUN(metadata_that_cannot_describe_a_drawing_is_refused_before_the_wait);
    RUN(a_length_from_a_header_to_the_largest_drawing_is_taken);
    RUN(the_hash_is_read_from_both_halves_and_checked);
    RUN(the_coprocessor_serves_what_the_panel_assembles);
    RUN(a_transfer_that_has_ended_takes_nothing_more);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("link_artxfer");
}
