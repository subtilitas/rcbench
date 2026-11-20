/*
 * A session through the panel's half of the ESC_WIRE pages: entry, the read copy
 * as it arrives and is checked, a write of a draft with its blocks, its stale
 * case, RESTORE with the backup's identity, BY HAND.  The two bounds of the
 * fetch that hold without the restart in absorb(): the block count of the
 * fetch's own LEN, and no block past MUSIC into the module.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>

#include "esc_link_rig.h"

/* The AM32 sim's page. */
static uint8_t g_page[ESC_LINK_PAGE_MAX];

TEST_CASE(the_read_copy_arrives_and_is_checked)
{
    esc_link_outcome_t o;
    const esc_link_image_t *im = esc_link_image(&g_l);

    up_am_rig();
    link_up();
    CHECK_EQ(im->gen, 0u);
    link_ready(2u);
    CHECK_EQ(esc_link_read(&g_l, 5u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 20000u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_DONE);
    CHECK_EQ(o.op, LINK_ESC_OP_READ);
    CHECK_EQ(o.result, LINK_ESC_RESULT_DONE);
    link_run(100u);
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_READY);
    CHECK(im->gen != 0u);
    CHECK_EQ(im->len, EW_MAP_LEN_AM);
    CHECK_EQ(im->reads, reg(LINK_EW_READS));
    CHECK_EQ(im->read_crc, reg(LINK_EW_READ_CRC));
    CHECK(memcmp(im->page, esc.am, EW_MAP_LEN_AM) == 0);
    /* The same read copy is not fetched again. */
    const uint32_t gen = im->gen;

    link_run(500u);
    CHECK_EQ(im->gen, gen);
}

TEST_CASE(a_read_copy_with_a_wrong_byte_is_fetched_again_and_given_up)
{
    esc_link_outcome_t o;
    const esc_link_image_t *im = esc_link_image(&g_l);

    up_am_rig();
    link_up();
    g_corrupt = 1u;
    link_ready(2u);
    if (im->gen == 0u) {
        CHECK_EQ(esc_link_read(&g_l, 5u), ESC_LINK_START_OK);
        CHECK(link_wait(&o, 20000u));
    }
    link_run(500u);
    CHECK_EQ(im->gen, 0u);
    /* Given up: requests go, and nothing spins on it. */
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_READY);
    const int before = g_exchanges;

    link_run(1000u);
    CHECK_EQ(g_exchanges - before, 10);
    /* A new read copy is another try. */
    g_corrupt = 0u;
    CHECK_EQ(esc_link_read(&g_l, 6u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 20000u));
    link_run(100u);
    memcpy(g_page, esc.am, EW_MAP_LEN_AM);
    g_page[0x18] = 48u;
    CHECK_EQ(esc_link_write(&g_l, g_page, EW_MAP_LEN_AM, reg(LINK_EW_READS),
                            reg(LINK_EW_READ_CRC), 7u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 20000u));
    link_run(100u);
    CHECK(im->gen != 0u);
    CHECK_EQ(im->read_crc, reg(LINK_EW_READ_CRC));
}

TEST_CASE(a_write_goes_with_its_blocks_and_its_key)
{
    esc_link_outcome_t o;
    const esc_link_image_t *im = esc_link_image(&g_l);

    up_am_rig();
    link_up();
    link_ready(2u);
    CHECK_EQ(esc_link_read(&g_l, 5u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 20000u));
    link_run(100u);
    memcpy(g_page, im->page, im->len);
    g_page[0x18] = 48u;
    CHECK_EQ(esc_link_write(&g_l, g_page, 0u, im->reads, im->read_crc, 6u),
             ESC_LINK_START_ARG);
    CHECK_EQ(esc_link_write(&g_l, g_page, ESC_LINK_PAGE_MAX + 1u, im->reads,
                            im->read_crc, 6u), ESC_LINK_START_ARG);
    CHECK_EQ(esc_link_write(&g_l, g_page, im->len, im->reads, im->read_crc, 6u),
             ESC_LINK_START_OK);
    CHECK_EQ(esc_link_phase(&g_l), ESC_LINK_SENDING);
    CHECK_EQ(esc_link_read(&g_l, 7u), ESC_LINK_START_BUSY);
    CHECK(link_wait(&o, 20000u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_DONE);
    CHECK_EQ(o.op, LINK_ESC_OP_WRITE);
    CHECK_EQ(o.result, LINK_ESC_RESULT_DONE);
    CHECK_EQ(esc.am[0x18], 48u);
    CHECK_EQ(esc.programs, 1);
    /* The read-back comes as the new copy. */
    const uint16_t stale_reads = im->reads;
    const uint16_t stale_crc = im->read_crc;

    link_run(100u);
    CHECK_EQ(im->reads, reg(LINK_EW_READS));
    CHECK_EQ(im->page[0x18], 48u);
    /* A draft of the copy before it is refused by the page. */
    g_page[0x18] = 24u;
    CHECK_EQ(esc_link_write(&g_l, g_page, im->len, stale_reads, stale_crc, 8u),
             ESC_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_REFUSED);
    CHECK_EQ(o.refusal, LINK_ESC_REF_STALE);
    CHECK_EQ(esc.programs, 1);
}

TEST_CASE(by_hand_waits_for_its_power)
{
    esc_link_outcome_t o;
    const esc_link_power_t hand = { LINK_ESC_BY_HAND, 0u, 0u, 500u, 0u };

    up_am_rig();
    link_up();
    CHECK_EQ(esc_link_open(&g_l, 2u, 1u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(esc_link_enter(&g_l, &hand, 2u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_DONE);
    CHECK_EQ(o.state, LINK_ESC_HAND_WAIT);
    CHECK_EQ(g.ons, 0);
    CHECK_EQ(esc_link_powered(&g_l, 3u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 20000u));
    CHECK_EQ(o.state, LINK_ESC_READY);
    /* A leave. */
    CHECK_EQ(esc_link_leave(&g_l, 4u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 5000u));
    CHECK_EQ(o.op, LINK_ESC_OP_LEAVE);
    CHECK_EQ(o.result, LINK_ESC_RESULT_DONE);
}

TEST_CASE(a_restore_carries_the_backups_identity)
{
    esc_link_outcome_t o;
    uint8_t identity[LINK_ED_BLOCK_BYTES];
    uint8_t keep[EW_MAP_LEN_AM];
    const esc_link_image_t *im = esc_link_image(&g_l);

    up_am_rig();
    memcpy(keep, esc.am, sizeof(keep));
    memset(esc.am, 0xFF, sizeof(keep));
    link_up();
    link_ready(2u);
    CHECK(flag(LINK_EW_F_ERASED));
    CHECK_EQ(esc_link_read(&g_l, 5u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 20000u));
    link_run(100u);
    /* The identity the page reports; a backup of another ESC is refused. */
    memset(identity, 0, sizeof(identity));
    const uint16_t contact = reg(LINK_EW_CONTACT);

    identity[0] = (uint8_t)(contact & 0xFFu);
    identity[1] = (uint8_t)(contact >> 8);
    identity[2] = (uint8_t)reg(LINK_EW_PROTOCOL);
    memcpy(&identity[4], im->name, sizeof(identity) - 4u);
    identity[0] ^= 0x01u;
    CHECK_EQ(esc_link_restore(&g_l, keep, sizeof(keep), identity, im->reads,
                              im->read_crc, 6u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 20000u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_REFUSED);
    CHECK_EQ(esc.programs, 0);
    identity[0] ^= 0x01u;
    CHECK_EQ(esc_link_restore(&g_l, keep, sizeof(keep), identity, im->reads,
                              im->read_crc, 7u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 20000u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_DONE);
    CHECK_EQ(o.op, LINK_ESC_OP_RESTORE);
    CHECK_EQ(o.result, LINK_ESC_RESULT_DONE);
    CHECK_EQ(esc.am[0], 0x01u);
}

/* The AM32 page's blocks, then NAME, 2 of INFO and 2 of MUSIC: the third
 * block of MUSIC is fetch_at's. */
#define MUSIC_THIRD ((EW_MAP_LEN_AM / LINK_ED_BLOCK_BYTES) + 1u + 2u + 2u)

/* One exchange of @p l by hand: asked, made on the rig, its answer given.
 * Returns the exchange. */
static esc_link_xfer_t step(esc_link_t *l)
{
    esc_link_xfer_t x;

    memset(&x, 0, sizeof(x));
    run_ms(5u);
    if (esc_link_next(l, esc.now / 1000u, &x)) {
        static uint16_t regs[LINK_MAX_REGS];
        const int r = link_xchg(NULL, &x, regs);

        esc_link_done(l, r, (!x.write && r == ESC_LINK_ACK) ? regs : NULL);
    }
    return x;
}

/* A READ on the AM32 rig with a `music` of 255 characters, its copy fetched up
 * to the third block of MUSIC: the block is chosen on the page and its DATA read
 * is next. */
static void fetch_to_music_third(void)
{
    esc_link_outcome_t o;

    up_am_rig();
    link_up();
    link_ready(2u);
    for (size_t i = 0u; i + 1u < sizeof(g_p.ses.music); ++i) {
        g_p.ses.music[i] = (char)('a' + (char)(i % 26u));
    }
    g_p.ses.music[sizeof(g_p.ses.music) - 1u] = '\0';
    CHECK_EQ(esc_link_read(&g_l, 5u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 20000u));
    for (int i = 0; i < 400 && !(g_l.fetching && g_l.fetch_at == MUSIC_THIRD
                                 && g_l.block_set); ++i) {
        (void)step(&g_l);
    }
    CHECK(g_l.fetching);
    CHECK_EQ(g_l.fetch_at, MUSIC_THIRD);
    CHECK(g_l.block_set);
}

/* The block count is the LEN the fetch started with, not the LEN of the status
 * now.  The status is written here without absorb(), so no restart comes: the
 * page's 3 blocks still count, MUSIC's last 2 blocks land in the copy and the
 * copy ends whole. */
TEST_CASE(a_fetch_counts_its_blocks_by_the_len_it_started_with)
{
    const esc_link_image_t *im = esc_link_image(&g_l);

    fetch_to_music_third();
    g_l.status[LINK_EW_LEN] = 10u;          /* one block */
    g_blocks = 0u;
    for (int i = 0; i < 20 && g_l.fetching; ++i) {
        const esc_link_xfer_t x = step(&g_l);

        CHECK(x.page == LINK_PAGE_ESC_DATA || x.write);
    }
    CHECK(!g_l.fetching);
    CHECK((g_blocks & (1ul << (LINK_ED_MUSIC + 3u))) != 0u);
    CHECK(im->gen != 0u);
    CHECK_EQ(im->len, EW_MAP_LEN_AM);
    CHECK_EQ(memcmp(im->music, g_p.ses.music, sizeof(im->music)), 0);
}

/* The module with 64 bytes behind it: a block copied past `music` lands there,
 * not in another test's data. */
static struct {
    esc_link_t l;
    uint8_t    past[LINK_ED_BLOCK_BYTES];
} s_w;

/* A block number from LINK_ED_BACKUP on copies nothing into the copy, however
 * the count came to it: with a fetch_len of one block, fetch_at's block is
 * NAME + 7, LINK_ED_BACKUP.  The bytes behind the copy stay as they were. */
TEST_CASE(a_block_past_music_lands_nowhere)
{
    static uint8_t tail[sizeof(s_w)];
    const size_t behind = offsetof(esc_link_t, image) + sizeof(s_w.l.image);

    fetch_to_music_third();
    memcpy(&s_w.l, &g_l, sizeof(g_l));
    memset(s_w.past, 0xA5, sizeof(s_w.past));
    s_w.l.fetch_len = 10u;                  /* one block */
    memcpy(tail, (const uint8_t *)&s_w + behind, sizeof(s_w) - behind);
    const esc_link_xfer_t x = step(&s_w.l);

    CHECK_EQ(x.page, LINK_PAGE_ESC_DATA);
    CHECK(!x.write);
    CHECK_EQ(memcmp(tail, (const uint8_t *)&s_w + behind,
                    sizeof(s_w) - behind), 0);
}

int main(void)
{
    RUN(the_read_copy_arrives_and_is_checked);
    RUN(a_read_copy_with_a_wrong_byte_is_fetched_again_and_given_up);
    RUN(a_write_goes_with_its_blocks_and_its_key);
    RUN(by_hand_waits_for_its_power);
    RUN(a_restore_carries_the_backups_identity);
    RUN(a_fetch_counts_its_blocks_by_the_len_it_started_with);
    RUN(a_block_past_music_lands_nowhere);
    return test_summary("esc_link_run");
}
