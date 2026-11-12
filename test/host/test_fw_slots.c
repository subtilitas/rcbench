/*
 * The IOmcu's two update slots on the RP2350 boot ROM
 * (shared/fwupdate/fw_slots.h).
 *
 * The failures under test: a partition read out of an answer to another
 * query, or out of a location whose last sector is before its first; slots
 * used under a partition table written for another layout, so an update
 * writes over the store or the running image; the running slot written
 * instead of the other one; an image on trial that is not reported as
 * such, so ARM is not held and the BUY never comes; a write past the end
 * of the slot; a restart into the slot that runs; and a BUY reported as
 * taken when its flash window did not run, from the answer of an earlier
 * BUY.
 *
 * The boot ROM's calls, the flash writes and the restart are firmware
 * (firmware/iomcu/src/update_hw.c) and not in this suite.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "fw_slots.h"
#include "link_pages.h"
#include "link_update_dev.h"

/* firmware/iomcu/partitions.json: A at 8KiB, B at 2044KiB, 2036KiB each. */
#define SECTOR     4096u
#define A_OFF      (8u * 1024u)
#define B_OFF      (2044u * 1024u)
#define SLOT       (2036u * 1024u)
/* The RP2350's XIP (execute in place) window. */
#define XIP        0x10000000u

static const fw_slots_layout_t k_layout = { A_OFF, B_OFF, SLOT };

/* The location word of a partition from @p start, @p bytes long, with the
 * permission and family bits above the sector fields set, as the boot ROM
 * reports them. */
static uint32_t location(uint32_t start, uint32_t bytes)
{
    const uint32_t first = start / SECTOR;
    const uint32_t last = (start + bytes) / SECTOR - 1u;
    return first | (last << FW_SLOTS_LOC_LAST_SECTOR_LSB) | 0xFC000000u;
}

static fw_slots_part_t part(uint32_t start, uint32_t bytes)
{
    const fw_slots_part_t p = { true, start, bytes };
    return p;
}

/* A boot of @p partition with the table of partitions.json. */
static fw_slots_boot_t boot_of(int partition, uint8_t tbyb)
{
    fw_slots_boot_t b;
    memset(&b, 0, sizeof(b));
    b.have_info = true;
    b.partition = partition;
    b.tbyb = tbyb;
    b.p0 = part(A_OFF, SLOT);
    b.p1 = part(B_OFF, SLOT);
    return b;
}

/* Slots of a boot that runs from neither slot. */
static void check_no_slot(const fw_slots_t *s)
{
    CHECK_EQ(s->slot, -1);
    CHECK_EQ(s->link_slot, LINK_UPD_SLOT_NONE);
    CHECK_EQ(s->running_off, A_OFF);
    CHECK_EQ(s->target_off, B_OFF);
}

TEST_CASE(the_boot_roms_answer_reads_as_the_partition)
{
    const uint32_t words[3] = { FW_SLOTS_PT_INFO_HEADER,
                                location(B_OFF, SLOT), 0x12345678u };
    const fw_slots_part_t p = fw_slots_partition(3, words, SECTOR);
    CHECK(p.found);
    CHECK_EQ(p.start, B_OFF);
    CHECK_EQ(p.bytes, SLOT);

    /* One sector: first and last the same. */
    const uint32_t one[3] = { FW_SLOTS_PT_INFO_HEADER,
                              location(A_OFF, SECTOR), 0u };
    const fw_slots_part_t q = fw_slots_partition(3, one, SECTOR);
    CHECK(q.found);
    CHECK_EQ(q.start, A_OFF);
    CHECK_EQ(q.bytes, SECTOR);
}

TEST_CASE(an_answer_to_another_query_is_no_partition)
{
    uint32_t words[3] = { FW_SLOTS_PT_INFO_HEADER, location(A_OFF, SLOT),
                          0u };
    /* Fewer or more words than the query returns, or an error. */
    CHECK(!fw_slots_partition(2, words, SECTOR).found);
    CHECK(!fw_slots_partition(4, words, SECTOR).found);
    CHECK(!fw_slots_partition(-4, words, SECTOR).found);
    CHECK(!fw_slots_partition(3, NULL, SECTOR).found);
    /* The header of the whole table's locations, or of no field. */
    words[0] = 0x0010u;
    CHECK(!fw_slots_partition(3, words, SECTOR).found);
    words[0] = 0x8000u;
    CHECK(!fw_slots_partition(3, words, SECTOR).found);
    words[0] = FW_SLOTS_PT_INFO_HEADER | 0x0001u;
    CHECK(!fw_slots_partition(3, words, SECTOR).found);
}

TEST_CASE(a_last_sector_before_the_first_is_no_partition)
{
    const uint32_t words[3] = {
        FW_SLOTS_PT_INFO_HEADER,
        10u | (9u << FW_SLOTS_LOC_LAST_SECTOR_LSB), 0u,
    };
    CHECK(!fw_slots_partition(3, words, SECTOR).found);
}

TEST_CASE(slot_a_running_writes_slot_b)
{
    fw_slots_t s;
    const fw_slots_boot_t b = boot_of(0, 0u);
    fw_slots_choose(&s, &k_layout, &b);
    CHECK_EQ(s.slot, 0);
    CHECK_EQ(s.link_slot, LINK_UPD_SLOT_A);
    CHECK_EQ(s.running_off, A_OFF);
    CHECK_EQ(s.target_off, B_OFF);
    CHECK_EQ(s.slot_bytes, SLOT);
    CHECK_EQ(fw_slots_update_base(&s, XIP), XIP + B_OFF);
}

TEST_CASE(slot_b_running_writes_slot_a)
{
    fw_slots_t s;
    const fw_slots_boot_t b = boot_of(1, 0u);
    fw_slots_choose(&s, &k_layout, &b);
    CHECK_EQ(s.slot, 1);
    CHECK_EQ(s.link_slot, LINK_UPD_SLOT_B);
    CHECK_EQ(s.running_off, B_OFF);
    CHECK_EQ(s.target_off, A_OFF);
    CHECK_EQ(fw_slots_update_base(&s, XIP), XIP + A_OFF);
}

TEST_CASE(buy_pending_puts_the_running_slot_on_trial)
{
    for (int slot = 0; slot <= 1; ++slot) {
        fw_slots_t s;
        fw_slots_boot_t b = boot_of(slot, FW_SLOTS_BUY_PENDING);
        fw_slots_choose(&s, &k_layout, &b);
        CHECK_EQ(s.slot, slot);
        CHECK_EQ(s.link_slot, (unsigned)slot | LINK_UPD_SLOT_TRIAL);

        /* The update flags beside it put nothing on trial. */
        b.tbyb = 0xFEu;
        fw_slots_choose(&s, &k_layout, &b);
        CHECK_EQ(s.link_slot, (unsigned)slot);
    }
}

TEST_CASE(a_table_for_another_layout_gives_no_slot)
{
    /* Each field of the table off from partitions.json in turn, with an
     * image on trial: neither slot, and no trial either. */
    for (int field = 0; field < 8; ++field) {
        fw_slots_boot_t b = boot_of(1, FW_SLOTS_BUY_PENDING);
        switch (field) {
        case 0: b.p0.found = false; break;
        case 1: b.p1.found = false; break;
        case 2: b.p0.start += SECTOR; break;
        case 3: b.p1.start -= SECTOR; break;
        case 4: b.p0.bytes -= SECTOR; break;
        case 5: b.p1.bytes += SECTOR; break;
        case 6: b.p0 = part(B_OFF, SLOT); b.p1 = part(A_OFF, SLOT); break;
        default: b.p0 = part(A_OFF, SLOT); b.p1 = part(A_OFF, SLOT); break;
        }
        fw_slots_t s;
        fw_slots_choose(&s, &k_layout, &b);
        check_no_slot(&s);
    }
}

TEST_CASE(a_boot_from_neither_partition_gives_no_slot)
{
    const int partitions[] = { -1, 2, 3, 127, -128 };
    for (unsigned i = 0; i < sizeof(partitions) / sizeof(partitions[0]);
         ++i) {
        fw_slots_t s;
        const fw_slots_boot_t b = boot_of(partitions[i], FW_SLOTS_BUY_PENDING);
        fw_slots_choose(&s, &k_layout, &b);
        check_no_slot(&s);
    }
    /* Without the boot info the partition named means nothing. */
    fw_slots_t s;
    fw_slots_boot_t b = boot_of(1, FW_SLOTS_BUY_PENDING);
    b.have_info = false;
    fw_slots_choose(&s, &k_layout, &b);
    check_no_slot(&s);
}

TEST_CASE(a_write_lands_in_the_slot_not_running)
{
    for (int slot = 0; slot <= 1; ++slot) {
        fw_slots_t s;
        const fw_slots_boot_t b = boot_of(slot, 0u);
        fw_slots_choose(&s, &k_layout, &b);
        const uint32_t other = (slot == 0) ? B_OFF : A_OFF;
        uint32_t addr = 0u;
        CHECK(fw_slots_target(&s, 0u, &addr));
        CHECK_EQ(addr, other);
        CHECK(fw_slots_target(&s, SLOT - SECTOR, &addr));
        CHECK_EQ(addr, other + SLOT - SECTOR);
    }
}

TEST_CASE(a_write_at_the_slots_end_or_past_it_is_refused)
{
    fw_slots_t s;
    const fw_slots_boot_t b = boot_of(1, 0u);
    fw_slots_choose(&s, &k_layout, &b);
    uint32_t addr = 0xDEADBEEFu;
    CHECK(!fw_slots_target(&s, SLOT, &addr));
    CHECK(!fw_slots_target(&s, SLOT + SECTOR, &addr));
    CHECK(!fw_slots_target(&s, 0xFFFFF000u, &addr));
    CHECK_EQ(addr, 0xDEADBEEFu);

    /* Slots never chosen hold no byte. */
    fw_slots_t none;
    memset(&none, 0, sizeof(none));
    CHECK(!fw_slots_target(&none, 0u, &addr));
    CHECK_EQ(addr, 0xDEADBEEFu);
}

TEST_CASE(a_buy_counts_when_the_window_ran_and_the_rom_answered_0)
{
    fw_slots_buy_t buy;
    fw_slots_buy_begin(&buy);
    fw_slots_buy_answer(&buy, 0);
    CHECK(fw_slots_buy_taken(&buy, true));

    /* The boot ROM refused: BOOTROM_ERROR_NOT_PERMITTED is -4. */
    fw_slots_buy_begin(&buy);
    fw_slots_buy_answer(&buy, -4);
    CHECK(!fw_slots_buy_taken(&buy, true));
    fw_slots_buy_answer(&buy, 1);
    CHECK(!fw_slots_buy_taken(&buy, true));
}

TEST_CASE(a_window_that_did_not_run_takes_no_earlier_answer)
{
    fw_slots_buy_t buy;
    fw_slots_buy_begin(&buy);
    fw_slots_buy_answer(&buy, 0);
    CHECK(fw_slots_buy_taken(&buy, true));

    /* The next try's window does not open, and the call never runs. */
    fw_slots_buy_begin(&buy);
    CHECK_EQ(buy.rc, FW_SLOTS_BUY_NOT_RUN);
    CHECK(!fw_slots_buy_taken(&buy, false));
    /* Nor does a window that says it ran without the call answering. */
    CHECK(!fw_slots_buy_taken(&buy, true));
    /* An answer of 0 from a window that did not run counts for nothing. */
    fw_slots_buy_answer(&buy, 0);
    CHECK(!fw_slots_buy_taken(&buy, false));
}

/* ------------------------------------------- into link_update_dev_init() */

static bool io_erase(void *ctx, uint32_t off)
{
    (void)ctx;
    (void)off;
    return true;
}

static bool io_program(void *ctx, uint32_t off, const uint8_t *data)
{
    (void)ctx;
    (void)off;
    (void)data;
    return true;
}

static uint8_t s_running[16u * SECTOR];
static uint8_t s_target[16u * SECTOR];

/* UPDATE_CTL's slot register after link_update_dev_init() takes @p s. */
static uint16_t slot_register(const fw_slots_t *s, link_update_dev_t *u)
{
    static const link_update_io_t io = { io_erase, io_program, NULL, NULL,
                                         NULL, NULL };
    memset(s_running, 0xFF, sizeof(s_running));
    memset(s_target, 0xFF, sizeof(s_target));
    link_update_dev_init(u, &io, s_running, s_target,
                         (uint32_t)sizeof(s_running), s->link_slot);
    uint16_t reg = 0u;
    link_update_dev_read_ctl(u, LINK_UPD_SLOT, 1u, &reg);
    return reg;
}

TEST_CASE(the_update_reports_the_chosen_slot_and_holds_arm_on_trial)
{
    static link_update_dev_t u;
    const fw_slots_layout_t small = { 0u, 16u * SECTOR, 16u * SECTOR };
    fw_slots_t s;
    fw_slots_boot_t b = boot_of(1, FW_SLOTS_BUY_PENDING);
    b.p0 = part(0u, 16u * SECTOR);
    b.p1 = part(16u * SECTOR, 16u * SECTOR);

    fw_slots_choose(&s, &small, &b);
    CHECK_EQ(slot_register(&s, &u), LINK_UPD_SLOT_B | LINK_UPD_SLOT_TRIAL);
    CHECK(link_update_dev_holds_arm(&u));

    b.tbyb = 0u;
    fw_slots_choose(&s, &small, &b);
    CHECK_EQ(slot_register(&s, &u), LINK_UPD_SLOT_B);
    CHECK(!link_update_dev_holds_arm(&u));

    /* No slot: reported as none, and an image on trial holds nothing. */
    b.tbyb = FW_SLOTS_BUY_PENDING;
    b.have_info = false;
    fw_slots_choose(&s, &small, &b);
    CHECK_EQ(slot_register(&s, &u), LINK_UPD_SLOT_NONE);
    CHECK(!link_update_dev_holds_arm(&u));
}

int main(void)
{
    RUN(the_boot_roms_answer_reads_as_the_partition);
    RUN(an_answer_to_another_query_is_no_partition);
    RUN(a_last_sector_before_the_first_is_no_partition);
    RUN(slot_a_running_writes_slot_b);
    RUN(slot_b_running_writes_slot_a);
    RUN(buy_pending_puts_the_running_slot_on_trial);
    RUN(a_table_for_another_layout_gives_no_slot);
    RUN(a_boot_from_neither_partition_gives_no_slot);
    RUN(a_write_lands_in_the_slot_not_running);
    RUN(a_write_at_the_slots_end_or_past_it_is_refused);
    RUN(a_buy_counts_when_the_window_ran_and_the_rom_answered_0);
    RUN(a_window_that_did_not_run_takes_no_earlier_answer);
    RUN(the_update_reports_the_chosen_slot_and_holds_arm_on_trial);
    return test_summary("fw_slots");
}
