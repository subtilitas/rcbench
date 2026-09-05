/*
 * Assembling a board photograph out of the blocks the link carries it in.
 *
 * The failure under test is not a slow transfer, it is a quiet one: a block
 * lost and skipped, or a reply for a block other than the one asked for,
 * assembled into a picture that is wrong in a way nobody sees until it has
 * been cached in flash and an operator wires a lead by it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_artxfer.h"
#include "link_crc.h"
#include "link_pages.h"

/* A picture small enough to hold here and big enough to need several blocks:
 * 30 x 4 pixels is 240 bytes, which is three full blocks and a part one. */
#define W 30u
#define H 4u
#define BYTES (W * H * 2u)

static uint8_t  g_src[BYTES];
static uint16_t g_meta[LINK_AW_COUNT];

static void make_source(void)
{
    for (unsigned i = 0; i < BYTES; ++i) {
        g_src[i] = (uint8_t)(i * 7u + 1u);      /* anything not uniform */
    }
    const uint32_t blocks = (BYTES + LINK_AD_BYTES - 1u) / LINK_AD_BYTES;
    g_meta[LINK_AW_BLOCKS]   = (uint16_t)blocks;
    g_meta[LINK_AW_WIDTH]    = (uint16_t)W;
    g_meta[LINK_AW_HEIGHT]   = (uint16_t)H;
    g_meta[LINK_AW_FORMAT]   = (uint16_t)LINK_ART_RGB565;
    g_meta[LINK_AW_BYTES_LO] = (uint16_t)(BYTES & 0xFFFFu);
    g_meta[LINK_AW_BYTES_HI] = (uint16_t)(BYTES >> 16);
    g_meta[LINK_AW_CRC]      = link_crc(0u, g_src, BYTES);
}

/* The window the coprocessor would answer with for one block. */
static void serve(uint16_t block, uint16_t *out)
{
    for (unsigned i = 0; i < LINK_AD_COUNT; ++i) { out[i] = 0u; }
    out[LINK_AD_BLOCK] = block;
    const uint32_t at = (uint32_t)block * LINK_AD_BYTES;
    for (unsigned i = 0; i < LINK_AD_BYTES && at + i < BYTES; ++i) {
        const uint8_t b = g_src[at + i];
        if (i & 1u) { out[LINK_AD_DATA + i / 2u] |= (uint16_t)((uint16_t)b << 8); }
        else        { out[LINK_AD_DATA + i / 2u] |= (uint16_t)b; }
    }
}

TEST_CASE(a_picture_arrives_byte_for_byte)
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

TEST_CASE(a_picture_that_does_not_match_its_own_checksum_is_not_accepted)
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
    CHECK(!link_artxfer_verify(&x));       /* complete, and not the picture */
}

TEST_CASE(metadata_that_cannot_describe_a_picture_is_refused_before_the_wait)
{
    make_source();
    uint8_t got[BYTES];
    link_artxfer_t x;
    uint16_t m[LINK_AW_COUNT];

    /* No picture at all, which is the ordinary answer from a coprocessor
     * that carries none. */
    memcpy(m, g_meta, sizeof(m));
    m[LINK_AW_BLOCKS] = 0u;
    CHECK(!link_artxfer_begin(&x, m, got, sizeof(got)));

    /* A format this build cannot read. */
    memcpy(m, g_meta, sizeof(m));
    m[LINK_AW_FORMAT] = 99u;
    CHECK(!link_artxfer_begin(&x, m, got, sizeof(got)));

    /*
     * A block count that disagrees with the length.  Blocks would run out
     * early or late, and the picture is not what it says either way.
     */
    memcpy(m, g_meta, sizeof(m));
    m[LINK_AW_BLOCKS] = (uint16_t)(m[LINK_AW_BLOCKS] + 1u);
    CHECK(!link_artxfer_begin(&x, m, got, sizeof(got)));

    /* A length that is not the pixels it claims. */
    memcpy(m, g_meta, sizeof(m));
    m[LINK_AW_WIDTH] = (uint16_t)(W + 1u);
    CHECK(!link_artxfer_begin(&x, m, got, sizeof(got)));

    /* And one there is nowhere to put.  Ten seconds of link for a transfer
     * that cannot end well is ten seconds wasted. */
    CHECK(!link_artxfer_begin(&x, g_meta, got, BYTES - 1u));
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
    RUN(a_picture_arrives_byte_for_byte);
    RUN(a_block_that_was_not_asked_for_is_refused_rather_than_placed);
    RUN(a_picture_that_does_not_match_its_own_checksum_is_not_accepted);
    RUN(metadata_that_cannot_describe_a_picture_is_refused_before_the_wait);
    RUN(a_transfer_that_has_ended_takes_nothing_more);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("link_artxfer");
}
