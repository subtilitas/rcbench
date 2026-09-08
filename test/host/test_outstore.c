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
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include "out_store_map.h"

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

int main(void)
{
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
    return test_summary("outstore");
}
