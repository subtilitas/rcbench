/*
 * Where a saved output binding goes in the coprocessor's flash.
 *
 * The rules under test decide two things the bench feels: how often a save
 * costs a sector erase, and what a power cut in the middle of one leaves
 * behind.  A save that erased and programmed inside one window measured
 * 19,178 us on the bring-up module, and for that long the core answers no CAN
 * (Controller Area Network) frame; the erase on its own is not measured.
 * Both are checked here rather than on the board, because the board has one
 * copy of the sector and a test needs many.
 *
 * And what a record carries: version 5 everything, and a version 3 or 4
 * record an earlier build wrote read with what it lacks at its page's
 * defaults -- the supply off, the current monitors off.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "greatest.h"

#include "out_store_map.h"
#include "out_store_rec.h"
#include "sense_page.h"
#include "tone_page.h"

/* A small geometry: the rules are the same at 2 x 16, and a case that has to
 * fill a sector reads better with four slots in it. */
#define SECTORS     2u
#define PER_SECTOR  4u
#define SLOTS       (SECTORS * PER_SECTOR)

static out_store_rec_t recs[SLOTS];

static void store_erased(void)
{
    for (unsigned i = 0; i < SLOTS; ++i) {
        recs[i].erased = true;
        recs[i].valid  = false;
        recs[i].seq    = 0u;
    }
}

/* A record that reads back: magic, version and checksum all agree. */
static void record(unsigned at, uint32_t seq)
{
    recs[at].erased = false;
    recs[at].valid  = true;
    recs[at].seq    = seq;
}

/* A slot written into and not finished: the power went while it was being
 * programmed, or an erase stopped half way through it. */
static void torn(unsigned at)
{
    recs[at].erased = false;
    recs[at].valid  = false;
    recs[at].seq    = 0u;
}

static void erase(unsigned sector)
{
    for (unsigned i = 0; i < PER_SECTOR; ++i) {
        recs[sector * PER_SECTOR + i].erased = true;
        recs[sector * PER_SECTOR + i].valid  = false;
        recs[sector * PER_SECTOR + i].seq    = 0u;
    }
}

static bool plan(out_store_write_t *w)
{
    return out_store_map_write(recs, SECTORS, PER_SECTOR, w);
}

TEST_CASE(an_empty_store_writes_the_first_record)
{
    store_erased();
    CHECK_EQ(out_store_map_newest(recs, SECTORS, PER_SECTOR), -1);
    CHECK_EQ(out_store_map_stale(recs, SECTORS, PER_SECTOR), -1);

    out_store_write_t w;
    CHECK(plan(&w));
    CHECK_EQ(w.at, 0);
    CHECK_EQ(w.seq, 1u);
    CHECK(!w.erase_first);
}

TEST_CASE(a_save_follows_the_live_record_in_its_own_sector)
{
    store_erased();
    record(0, 1u);

    out_store_write_t w;
    CHECK(plan(&w));
    CHECK_EQ(w.at, 1);
    CHECK_EQ(w.seq, 2u);
    CHECK(!w.erase_first);
    /* The one that matters: fifteen saves in sixteen erase nothing. */
    CHECK_EQ(out_store_map_stale(recs, SECTORS, PER_SECTOR), -1);
}

TEST_CASE(a_full_sector_moves_into_the_one_already_erased)
{
    store_erased();
    for (unsigned i = 0; i < PER_SECTOR; ++i) {
        record(i, i + 1u);
    }

    out_store_write_t w;
    CHECK(plan(&w));
    CHECK_EQ(w.at, PER_SECTOR);          /* the first slot of the other one */
    CHECK_EQ(w.seq, PER_SECTOR + 1u);
    CHECK(!w.erase_first);
}

/*
 * The erase that is unavoidable, and the guarantee that survives it: the
 * sector being erased is the one the store is leaving, never the one holding
 * the record that is still wanted.
 */
TEST_CASE(a_full_store_erases_the_sector_it_is_leaving)
{
    store_erased();
    for (unsigned i = 0; i < PER_SECTOR; ++i) {
        record(i, PER_SECTOR + i + 1u);      /* sector 0: the newer half */
        record(PER_SECTOR + i, i + 1u);      /* sector 1: the older half */
    }
    const int live = out_store_map_newest(recs, SECTORS, PER_SECTOR);
    CHECK_EQ(live, (int)PER_SECTOR - 1);     /* seq 2 * PER_SECTOR */

    out_store_write_t w;
    CHECK(plan(&w));
    CHECK(w.erase_first);
    CHECK_EQ(w.erase_sector, 1);
    CHECK_EQ(w.at, PER_SECTOR);
    CHECK_EQ(w.seq, 2u * PER_SECTOR + 1u);
    /* The live record is in sector 0 and the erase is of sector 1. */
    CHECK(w.erase_sector != (unsigned)live / PER_SECTOR);
}

TEST_CASE(a_torn_record_is_skipped_rather_than_written_over)
{
    store_erased();
    record(0, 1u);
    torn(1);

    out_store_write_t w;
    CHECK(plan(&w));
    CHECK_EQ(w.at, 2);
    CHECK_EQ(w.seq, 2u);
    CHECK(!w.erase_first);
}

/*
 * A record is the live one because of its sequence number and not because of
 * where it sits.  A store that has wrapped has its newest record at a lower
 * address than most of the older ones.
 */
TEST_CASE(the_live_record_is_the_highest_sequence_not_the_last_slot)
{
    store_erased();
    for (unsigned i = 0; i < PER_SECTOR; ++i) {
        record(PER_SECTOR + i, i + 1u);      /* sector 1: seq 1..4 */
    }
    record(0, 9u);                           /* sector 0: the live one */

    CHECK_EQ(out_store_map_newest(recs, SECTORS, PER_SECTOR), 0);

    out_store_write_t w;
    CHECK(plan(&w));
    CHECK_EQ(w.at, 1);
    CHECK_EQ(w.seq, 10u);
    CHECK(!w.erase_first);
    /* And sector 1 is what can be erased ahead of the save that needs it. */
    CHECK_EQ(out_store_map_stale(recs, SECTORS, PER_SECTOR), 1);
}

TEST_CASE(the_stale_sector_is_the_one_without_the_live_record)
{
    store_erased();
    record(0, 1u);
    CHECK_EQ(out_store_map_stale(recs, SECTORS, PER_SECTOR), -1);

    record(PER_SECTOR, 2u);   /* now the live record is in sector 1 */
    CHECK_EQ(out_store_map_stale(recs, SECTORS, PER_SECTOR), 0);

    erase(0);
    CHECK_EQ(out_store_map_stale(recs, SECTORS, PER_SECTOR), -1);
}

/*
 * A record this build cannot read -- a store written by a firmware with a
 * different record in it -- reads as neither erased nor valid.  It costs the
 * slot it is in and nothing else: the next save goes to an erased slot rather
 * than erasing a sector to replace it.
 */
TEST_CASE(a_store_with_no_valid_record_takes_an_erased_slot)
{
    store_erased();
    torn(PER_SECTOR);

    out_store_write_t w;
    CHECK(plan(&w));
    CHECK_EQ(w.at, 0);
    CHECK_EQ(w.seq, 1u);
    CHECK(!w.erase_first);
    /* Sector 1 holds nothing wanted and is not erased, so it is offered. */
    CHECK_EQ(out_store_map_stale(recs, SECTORS, PER_SECTOR), 1);
}

TEST_CASE(a_store_with_nowhere_left_erases_the_first_sector)
{
    store_erased();
    for (unsigned i = 0; i < SLOTS; ++i) {
        torn(i);
    }

    out_store_write_t w;
    CHECK(plan(&w));
    CHECK(w.erase_first);
    CHECK_EQ(w.erase_sector, 0);
    CHECK_EQ(w.at, 0);
    CHECK_EQ(w.seq, 1u);
}

/*
 * With one sector there is no other sector to keep the live record in, and
 * the erase takes it.  This is why the coprocessor configures two: the case
 * is here so the cost of one is written down rather than assumed away.
 */
TEST_CASE(one_sector_erases_the_sector_the_live_record_is_in)
{
    store_erased();
    for (unsigned i = 0; i < PER_SECTOR; ++i) {
        record(i, i + 1u);
    }

    out_store_write_t w;
    CHECK(out_store_map_write(recs, 1u, PER_SECTOR, &w));
    CHECK(w.erase_first);
    CHECK_EQ(w.erase_sector, 0);
    CHECK_EQ(w.at, 0);
    /* And nothing can be erased ahead of time, because the only sector holds
     * the record. */
    CHECK_EQ(out_store_map_stale(recs, 1u, PER_SECTOR), -1);
}

TEST_CASE(a_geometry_with_no_slots_is_refused)
{
    store_erased();
    out_store_write_t w;
    CHECK(!out_store_map_write(NULL, SECTORS, PER_SECTOR, &w));
    CHECK(!out_store_map_write(recs, 0u, PER_SECTOR, &w));
    CHECK(!out_store_map_write(recs, SECTORS, 0u, &w));
    CHECK(!out_store_map_write(recs, SECTORS, PER_SECTOR, NULL));
    /* 16 sectors of 16 slots is 256 slots, one past what a uint8_t holds. */
    CHECK(!out_store_map_write(recs, 16u, 16u, &w));
    CHECK_EQ(out_store_map_newest(recs, 0u, PER_SECTOR), -1);
    CHECK_EQ(out_store_map_stale(recs, SECTORS, 0u), -1);
}

/* One save, the way the coprocessor takes it: the erase on one pass and the
 * record on the next.  Returns whether it had to erase. */
static bool save(uint32_t *seq_written)
{
    out_store_write_t w;
    bool erased = false;
    if (!plan(&w)) {
        T_FAIL("%s", "the store refused a plan");
        return false;
    }
    if (w.erase_first) {
        /* The guarantee, checked on every save the sweeps take: the record
         * that is still wanted is not in the sector being erased. */
        const int live = out_store_map_newest(recs, SECTORS, PER_SECTOR);
        if (live >= 0 && (unsigned)live / PER_SECTOR == w.erase_sector) {
            T_FAIL("erased sector %u, which holds the live record %d",
                   w.erase_sector, live);
        }
        erase(w.erase_sector);
        erased = true;
        if (!plan(&w)) {
            T_FAIL("%s", "the store refused a plan after its erase");
            return erased;
        }
    }
    if (!recs[w.at].erased) {
        T_FAIL("slot %u is not erased and would be programmed over", w.at);
    }
    record(w.at, w.seq);
    *seq_written = w.seq;
    return erased;
}

/*
 * Sixty-four saves with nothing erased ahead of time.  The store fills both
 * sectors from erased once and erases one sector per sector-full after that,
 * which at the coprocessor's 2 x 16 slots is one erase per sixteen saves.
 * Every other save is a page program.
 */
TEST_CASE(the_store_erases_once_per_sector_of_saves)
{
    store_erased();
    unsigned erases = 0;
    uint32_t seq = 0;
    for (unsigned i = 1; i <= 8u * SLOTS; ++i) {
        if (save(&seq)) {
            ++erases;
        }
        CHECK_EQ(seq, i);
        const int live = out_store_map_newest(recs, SECTORS, PER_SECTOR);
        CHECK(live >= 0);
        CHECK_EQ(recs[live].seq, i);
    }
    CHECK_EQ(erases, 8u * SECTORS - SECTORS);
}

/*
 * And the same run with the sector erased ahead of the save that needs it,
 * which is what out_store_reclaim() does while the bus is quiet.  No save
 * pays for an erase at all.
 */
TEST_CASE(erasing_ahead_of_time_leaves_no_save_paying_for_one)
{
    store_erased();
    uint32_t seq = 0;
    for (unsigned i = 1; i <= 8u * SLOTS; ++i) {
        if (save(&seq)) {
            T_FAIL("save %u had to erase with a sector standing ready", i);
        }
        const int stale = out_store_map_stale(recs, SECTORS, PER_SECTOR);
        if (stale >= 0) {
            const int live = out_store_map_newest(recs, SECTORS, PER_SECTOR);
            CHECK(live >= 0);
            CHECK((unsigned)live / PER_SECTOR != (unsigned)stale);
            erase((unsigned)stale);
        }
    }
    CHECK_EQ(seq, 8u * SLOTS);
}

/*
 * A power cut during a save leaves the binding from before it.  The slot
 * being programmed is torn, and the record it was to replace is still the
 * live one.
 */
TEST_CASE(a_power_cut_during_a_save_leaves_the_record_before_it)
{
    store_erased();
    record(0, 1u);
    record(1, 2u);

    out_store_write_t w;
    CHECK(plan(&w));
    CHECK_EQ(w.at, 2);
    torn(w.at);                       /* the power went mid-program */

    const int live = out_store_map_newest(recs, SECTORS, PER_SECTOR);
    CHECK_EQ(live, 1);
    CHECK_EQ(recs[live].seq, 2u);
    /* And the save that follows steps over the torn slot. */
    CHECK(plan(&w));
    CHECK_EQ(w.at, 3);
    CHECK_EQ(w.seq, 3u);
}

/*
 * A power cut during an erase leaves that sector unreadable and the live
 * record where it was.  Half an erased record can read as valid only if its
 * checksum still agrees, which is why the coprocessor's checksum covers the
 * sequence number: an erase lifts bits towards 0xFF and could otherwise make
 * an old record outrank the live one.
 */
TEST_CASE(a_power_cut_during_an_erase_leaves_the_live_record)
{
    store_erased();
    record(0, 5u);
    for (unsigned i = 0; i < PER_SECTOR; ++i) {
        record(PER_SECTOR + i, i + 1u);
    }
    CHECK_EQ(out_store_map_stale(recs, SECTORS, PER_SECTOR), 1);

    /* The erase stopped part way: two slots gone, two half written. */
    recs[PER_SECTOR].erased = true;
    recs[PER_SECTOR].valid  = false;
    recs[PER_SECTOR + 1].erased = true;
    recs[PER_SECTOR + 1].valid  = false;
    torn(PER_SECTOR + 2);
    torn(PER_SECTOR + 3);

    const int live = out_store_map_newest(recs, SECTORS, PER_SECTOR);
    CHECK_EQ(live, 0);
    CHECK_EQ(recs[live].seq, 5u);
    /* The sector is still stale, so the next quiet moment finishes the job. */
    CHECK_EQ(out_store_map_stale(recs, SECTORS, PER_SECTOR), 1);
}

/*
 * A record that was not finished, in either direction, is rejected.
 *
 * An erase sets bits and a program clears them, so a record caught in either
 * holds fewer 0 bits than it was written with.  The count that says how many
 * is itself held against its complement, and neither a set bit nor a cleared
 * one can be paid for in that pair.  Exhaustive over the 32 bit positions of
 * both words.
 */
TEST_CASE(an_unfinished_write_is_rejected)
{
    static const uint32_t counts[] = {
        0u, 1u, 2u, 17u, 255u, 256u, 0x0000FFFFuL, 0x12345678uL,
        0x7FFFFFFFuL, 0x80000000uL, 0xFFFFFFFEuL, 0xFFFFFFFFuL,
    };
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
        const uint32_t z = counts[i];
        const uint32_t inv = ~z;
        CHECK(out_store_intact(z, inv, z));
        for (unsigned b = 0; b < 32u; ++b) {
            const uint32_t bit = 1uL << b;
            /* The count word, lifted by an erase or cleared by a program. */
            if ((z & bit) == 0u) {
                CHECK(!out_store_intact(z | bit, inv, z | bit));
                CHECK(!out_store_intact(z | bit, inv, z));
            } else {
                CHECK(!out_store_intact(z & ~bit, inv, z & ~bit));
            }
            /* And the complement word. */
            if ((inv & bit) == 0u) {
                CHECK(!out_store_intact(z, inv | bit, z));
            } else {
                CHECK(!out_store_intact(z, inv & ~bit, z));
            }
        }
        /* The record's own bits, one short of what it claims: a program that
         * did not finish, or an erase that had started. */
        if (z > 0u) {
            CHECK(!out_store_intact(z, inv, z - 1u));
        }
        CHECK(!out_store_intact(z, inv, z + 1u));
    }
    /* A fully erased slot reads 0xFFFFFFFF in both words, which is a pair no
     * record is ever written with. */
    CHECK(!out_store_intact(0xFFFFFFFFuL, 0xFFFFFFFFuL, 0u));
}

/* A configuration whose every register is its own index plus @p base, so a
 * field read from the wrong place shows. */
static void numbered(out_store_t *c, uint16_t base)
{
    uint16_t *w = (uint16_t *)(void *)c;
    for (size_t i = 0; i < sizeof(*c) / sizeof(uint16_t); ++i) {
        w[i] = (uint16_t)(base + i);
    }
}

/* Each version appends to the one before: 64 registers of bindings, then
 * 4 of supply wiring, then 12 of sensor set-up, then 7 of the phase tap's
 * and a spare to keep the set a whole number of 32-bit words. */
TEST_CASE(each_record_version_carries_the_one_before_and_more)
{
    CHECK_EQ(out_store_cfg_size(3u), 128u);
    CHECK_EQ(out_store_cfg_size(4u), 136u);
    CHECK_EQ(out_store_cfg_size(5u), 160u);
    CHECK_EQ(out_store_cfg_size(6u), 176u);
    CHECK_EQ(OUT_STORE_VERSION, 6u);
    CHECK_EQ(out_store_cfg_size(OUT_STORE_VERSION), sizeof(out_store_t));
    CHECK_EQ(sizeof(out_store_t) % 4u, 0u);
    CHECK_EQ(out_store_cfg_size(2u), 0u);
    CHECK_EQ(out_store_cfg_size(7u), 0u);
    /* A header of 20 bytes and the newest configuration fit one 256-byte
     * flash page. */
    CHECK(20u + sizeof(out_store_t) <= 256u);
}

TEST_CASE(a_version_6_record_reads_back_whole)
{
    out_store_t in;
    out_store_t out;
    numbered(&in, 0x1000u);
    memset(&out, 0, sizeof(out));
    CHECK(out_store_cfg_read(6u, &in, &out));
    CHECK_EQ(memcmp(&in, &out, sizeof(in)), 0);
}

TEST_CASE(a_version_5_record_keeps_its_sensors_and_leaves_the_tap_off)
{
    out_store_t in;
    out_store_t out;
    numbered(&in, 0x1800u);
    memset(&out, 0xA5, sizeof(out));
    CHECK(out_store_cfg_read(5u, &in, &out));
    CHECK_EQ(memcmp(out.slots, in.slots, sizeof(in.slots)), 0);
    CHECK_EQ(memcmp(out.chan_cfg, in.chan_cfg, sizeof(in.chan_cfg)), 0);
    CHECK_EQ(memcmp(out.supply, in.supply, sizeof(in.supply)), 0);
    CHECK_EQ(memcmp(out.sense, in.sense, sizeof(in.sense)), 0);
    uint16_t want[LINK_TN_CONFIG_COUNT];
    tone_page_defaults(want);
    CHECK_EQ(memcmp(out.tone, want, sizeof(want)), 0);
    CHECK_EQ(out.tone[LINK_TN_ENABLE], 0u);
    CHECK_EQ(out.tone[LINK_TN_PIN], 22u);
    CHECK_EQ(out.spare, 0u);
}

TEST_CASE(a_version_4_record_keeps_its_wiring_and_leaves_the_sensors_off)
{
    out_store_t in;
    out_store_t out;
    numbered(&in, 0x2000u);
    memset(&out, 0xA5, sizeof(out));
    CHECK(out_store_cfg_read(4u, &in, &out));
    CHECK_EQ(memcmp(out.slots, in.slots, sizeof(in.slots)), 0);
    CHECK_EQ(memcmp(out.chan_cfg, in.chan_cfg, sizeof(in.chan_cfg)), 0);
    CHECK_EQ(memcmp(out.supply, in.supply, sizeof(in.supply)), 0);
    uint16_t want[LINK_SN_CONFIG_COUNT];
    sense_page_defaults(want);
    CHECK_EQ(memcmp(out.sense, want, sizeof(want)), 0);
    CHECK_EQ(out.sense[LINK_SN_ENABLE], 0u);
    CHECK_EQ(out.tone[LINK_TN_ENABLE], 0u);
}

TEST_CASE(a_version_3_record_keeps_its_bindings_and_leaves_the_rest_off)
{
    out_store_t in;
    out_store_t out;
    numbered(&in, 0x3000u);
    memset(&out, 0xA5, sizeof(out));
    CHECK(out_store_cfg_read(3u, &in, &out));
    CHECK_EQ(memcmp(out.slots, in.slots, sizeof(in.slots)), 0);
    CHECK_EQ(memcmp(out.chan_cfg, in.chan_cfg, sizeof(in.chan_cfg)), 0);
    /* The supply as its page starts: disabled, at 19200 baud. */
    CHECK_EQ(out.supply[LINK_SP_ENABLE], 0u);
    CHECK_EQ(out.supply[LINK_SP_TX_PIN], 0u);
    CHECK_EQ(out.supply[LINK_SP_RX_PIN], 0u);
    CHECK_EQ(out.supply[LINK_SP_BAUD], 1u);
    CHECK_EQ(out.sense[LINK_SN_ENABLE], 0u);
    CHECK_EQ(out.sense[LINK_SN_SDA_PIN], 16u);
}

TEST_CASE(a_record_of_another_version_reads_as_nothing)
{
    out_store_t in;
    out_store_t out;
    out_store_t was;
    numbered(&in, 0x4000u);
    memset(&out, 0x5A, sizeof(out));
    was = out;
    CHECK(!out_store_cfg_read(2u, &in, &out));
    CHECK(!out_store_cfg_read(7u, &in, &out));
    CHECK(!out_store_cfg_read(6u, NULL, &out));
    CHECK_EQ(memcmp(&out, &was, sizeof(out)), 0);
    CHECK(!out_store_cfg_read(6u, &in, NULL));
}

int main(void)
{
    RUN(an_unfinished_write_is_rejected);
    RUN(an_empty_store_writes_the_first_record);
    RUN(a_save_follows_the_live_record_in_its_own_sector);
    RUN(a_full_sector_moves_into_the_one_already_erased);
    RUN(a_full_store_erases_the_sector_it_is_leaving);
    RUN(a_torn_record_is_skipped_rather_than_written_over);
    RUN(the_live_record_is_the_highest_sequence_not_the_last_slot);
    RUN(the_stale_sector_is_the_one_without_the_live_record);
    RUN(a_store_with_no_valid_record_takes_an_erased_slot);
    RUN(a_store_with_nowhere_left_erases_the_first_sector);
    RUN(one_sector_erases_the_sector_the_live_record_is_in);
    RUN(a_geometry_with_no_slots_is_refused);
    RUN(the_store_erases_once_per_sector_of_saves);
    RUN(erasing_ahead_of_time_leaves_no_save_paying_for_one);
    RUN(a_power_cut_during_a_save_leaves_the_record_before_it);
    RUN(a_power_cut_during_an_erase_leaves_the_live_record);
    RUN(each_record_version_carries_the_one_before_and_more);
    RUN(a_version_6_record_reads_back_whole);
    RUN(a_version_5_record_keeps_its_sensors_and_leaves_the_tap_off);
    RUN(a_version_4_record_keeps_its_wiring_and_leaves_the_sensors_off);
    RUN(a_version_3_record_keeps_its_bindings_and_leaves_the_rest_off);
    RUN(a_record_of_another_version_reads_as_nothing);
    return test_summary("outstore");
}
