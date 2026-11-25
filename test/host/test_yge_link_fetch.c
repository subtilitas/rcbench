/*
 * The read copy through the panel's half of the YGE_PROG pages: fetched by
 * blocks of YGE_DATA and held against LINK_YP_READ_CRC; a copy with another
 * CRC fetched again, 3 fetches in all, then given up until the page names
 * another copy; a table of 40 entries in 2 blocks; no request while it comes;
 * none kept over a free page or a lost link.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>

#include "yge_link_rig.h"

/* One exchange at most, 1ms on. */
static void link_step(void)
{
    yge_link_xfer_t x;

    pass();
    if (yge_link_next(&g_l, now_ms(), &x)) {
        static uint16_t regs[LINK_YD_COUNT];
        const int a = link_xchg(NULL, &x, x.write ? NULL : regs);

        yge_link_done(&g_l, a, (!x.write && a == YGE_LINK_ACK) ? regs : NULL);
    }
}

TEST_CASE(the_read_copy_arrives_and_is_checked)
{
    const yge_link_image_t *im = yge_link_image(&g_l);
    yge_link_outcome_t o;

    rig_up();
    link_up();
    CHECK_EQ(im->gen, 0u);
    link_ready(0u);
    CHECK_EQ(im->gen, 0u);
    CHECK_EQ(yge_link_read(&g_l, 3u), YGE_LINK_START_OK);
    for (int i = 0; i < 30000 && !yge_link_outcome(&g_l, &o); ++i) {
        link_step();
    }
    CHECK_EQ(o.kind, YGE_LINK_OUT_DONE);
    /* The copy comes after the outcome; no request is taken meanwhile. */
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_FETCHING);
    CHECK_EQ(yge_link_read(&g_l, 4u), YGE_LINK_START_BUSY);
    link_step();
    link_step();
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_READY);
    link_run(50u);
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_READY);
    CHECK(im->gen != 0u);
    CHECK_EQ(im->entries, 32u);
    CHECK_EQ(im->reads, reg(LINK_YP_READS));
    CHECK_EQ(im->read_crc, reg(LINK_YP_READ_CRC));
    CHECK_EQ(memcmp(im->value, sim.table, 32u * sizeof(im->value[0])), 0);
    CHECK_EQ(im->value[32], 0u);
    CHECK_EQ(g_data_reads, 1);
    CHECK(flag(LINK_YP_F_HAVE_READ));
    CHECK_EQ(refusal(), LINK_YGE_REF_NONE);
    /* The same copy is not fetched again. */
    const uint32_t gen = im->gen;

    link_run(1000u);
    CHECK_EQ(im->gen, gen);
    CHECK_EQ(g_data_reads, 1);
    /* A new read is a new copy, also with equal values. */
    link_read(5u);
    CHECK_EQ(im->gen, gen + 1u);
    CHECK_EQ(im->reads, reg(LINK_YP_READS));
    CHECK_EQ(g_data_reads, 2);
}

TEST_CASE(a_copy_with_a_wrong_value_is_fetched_again)
{
    const yge_link_image_t *im = yge_link_image(&g_l);

    rig_up();
    link_up();
    link_ready(0u);
    g_corrupt = 2u;
    link_read(3u);
    CHECK_EQ(g_data_reads, 3);
    CHECK(im->gen != 0u);
    CHECK_EQ(im->read_crc, reg(LINK_YP_READ_CRC));
    CHECK_EQ(memcmp(im->value, sim.table, 32u * sizeof(im->value[0])), 0);
}

TEST_CASE(three_fetches_with_a_wrong_crc_give_the_copy_up)
{
    const yge_link_image_t *im = yge_link_image(&g_l);

    rig_up();
    link_up();
    link_ready(0u);
    g_corrupt = 0xFFu;
    link_read(3u);
    CHECK_EQ(g_data_reads, 3);
    CHECK_EQ(im->gen, 0u);
    /* Given up: requests go, and nothing spins on it. */
    CHECK_EQ(yge_link_phase(&g_l), YGE_LINK_READY);
    const int before = g_exchanges;

    link_run(1000u);
    CHECK_EQ(g_data_reads, 3);
    CHECK_EQ(g_exchanges - before, 10);
    /* Another copy is another 3 tries. */
    g_corrupt = 0u;
    link_read(4u);
    CHECK_EQ(g_data_reads, 4);
    CHECK(im->gen != 0u);
    CHECK_EQ(im->reads, reg(LINK_YP_READS));
}

TEST_CASE(a_table_of_40_entries_comes_in_2_blocks)
{
    const yge_link_image_t *im = yge_link_image(&g_l);
    yge_link_readout_t r;

    rig_up();
    sim.table[0] = 40u;
    sim.cycle = 40u;
    for (unsigned i = 32u; i < 40u; ++i) {
        sim.table[i] = (uint16_t)(1000u + i);
    }
    link_up();
    link_ready(0u);
    link_read(3u);
    CHECK_EQ(g_data_reads, 2);
    CHECK(im->gen != 0u);
    CHECK_EQ(im->entries, 40u);
    CHECK_EQ(memcmp(im->value, sim.table, 40u * sizeof(im->value[0])), 0);
    CHECK_EQ(im->value[40], 0u);
    /* Read-only: a count other than 32. */
    yge_link_readout(&g_l, &r);
    CHECK_EQ(r.entries, 40u);
    CHECK((r.flags & LINK_YP_F_WRITABLE) == 0u);
    CHECK_EQ(r.ro_why, OY_REF_RO_COUNT);
    /* A wrong value in the second block is as wrong. */
    g_corrupt = 0u;
    link_read(4u);
    CHECK_EQ(g_data_reads, 4);
}

TEST_CASE(no_copy_over_a_free_page_or_a_lost_link)
{
    const yge_link_image_t *im = yge_link_image(&g_l);
    yge_link_outcome_t o;
    yge_link_readout_t r;

    rig_up();
    link_up();
    link_ready(0u);
    link_read(3u);
    CHECK(im->gen != 0u);
    yge_link_readout(&g_l, &r);
    CHECK_EQ(r.type, 5712u);
    CHECK_EQ(r.firmware, SIM_FW);
    CHECK_EQ(r.serial, 0x1234u);
    CHECK_EQ(r.seen, 32u);
    CHECK_EQ(r.writing, 0xFFu);
    /* Lost: the session held over the gap is another's as far as this end
     * knows.  Back: the copy comes again. */
    yge_link_lost(&g_l);
    CHECK_EQ(im->gen, 0u);
    yge_link_came_up(&g_l, YGE_LINK_MINOR);
    link_run(100u);
    CHECK(im->gen != 0u);
    CHECK_EQ(memcmp(im->value, sim.table, 32u * sizeof(im->value[0])), 0);
    /* Free: none. */
    CHECK_EQ(yge_link_close(&g_l, 4u), YGE_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    link_run(10u);
    CHECK_EQ(im->gen, 0u);
}

int main(void)
{
    RUN(the_read_copy_arrives_and_is_checked);
    RUN(a_copy_with_a_wrong_value_is_fetched_again);
    RUN(three_fetches_with_a_wrong_crc_give_the_copy_up);
    RUN(a_table_of_40_entries_comes_in_2_blocks);
    RUN(no_copy_over_a_free_page_or_a_lost_link);
    return test_summary("yge_link_fetch");
}
