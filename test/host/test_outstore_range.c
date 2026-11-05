/*
 * Which flash ranges the IOmcu's update may erase and program: never a byte
 * of the boot ROM's two boot slots below 0x2000, where the partition table
 * lives, and never a byte of the output store.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "out_store_range.h"

/* The store's offset in firmware/iomcu/partitions.json: 4080KiB. */
#define CEILING 0x3FC000u

TEST_CASE(an_erase_of_the_partition_table_sectors_is_refused)
{
    CHECK(!out_store_range_erase_ok(0x0000u, CEILING));
    CHECK(!out_store_range_erase_ok(0x1000u, CEILING));
}

TEST_CASE(an_erase_at_0x1fff_is_refused)
{
    CHECK(!out_store_range_erase_ok(0x1FFFu, CEILING));
}

TEST_CASE(an_erase_at_0x2000_runs)
{
    CHECK(out_store_range_erase_ok(OUT_STORE_RANGE_FLOOR, CEILING));
    CHECK_EQ(OUT_STORE_RANGE_FLOOR, 0x2000u);
}

TEST_CASE(an_erase_off_a_sector_boundary_is_refused)
{
    CHECK(!out_store_range_erase_ok(0x2100u, CEILING));
    CHECK(!out_store_range_erase_ok(0x2FFFu, CEILING));
}

TEST_CASE(an_erase_ends_at_the_store_and_not_past_it)
{
    CHECK(out_store_range_erase_ok(CEILING - 0x1000u, CEILING));
    CHECK(!out_store_range_erase_ok(CEILING, CEILING));
    CHECK(!out_store_range_erase_ok(CEILING + 0x1000u, CEILING));
}

TEST_CASE(an_erase_near_4gib_does_not_wrap_into_range)
{
    CHECK(!out_store_range_erase_ok(0xFFFFF000u, CEILING));
    CHECK(!out_store_range_erase_ok(0xFFFFF000u, 0xFFFFFFFFu));
}

TEST_CASE(a_ceiling_at_or_below_the_floor_allows_nothing)
{
    CHECK(!out_store_range_erase_ok(OUT_STORE_RANGE_FLOOR,
                                    OUT_STORE_RANGE_FLOOR));
    CHECK(!out_store_range_erase_ok(OUT_STORE_RANGE_FLOOR, 0x1000u));
    CHECK(!out_store_range_program_ok(OUT_STORE_RANGE_FLOOR, 256u, 0x1000u));
}

TEST_CASE(a_program_that_starts_below_0x2000_is_refused)
{
    /* Ends exactly at 0x2000: every byte in the boot slots. */
    CHECK(!out_store_range_program_ok(0x1000u, 0x1000u, CEILING));
    CHECK(!out_store_range_program_ok(0x1F00u, 0x100u, CEILING));
    CHECK(!out_store_range_program_ok(0x0000u, 0x100u, CEILING));
}

TEST_CASE(a_program_at_0x1fff_is_refused)
{
    CHECK(!out_store_range_program_ok(0x1FFFu, 0x100u, CEILING));
    CHECK(!out_store_range_program_ok(0x1FFFu, 0u, CEILING));
}

TEST_CASE(a_program_from_0x2000_upward_runs)
{
    CHECK(out_store_range_program_ok(0x2000u, 0x1000u, CEILING));
    CHECK(out_store_range_program_ok(0x2000u, 0x100u, CEILING));
    CHECK(out_store_range_program_ok(0x1FF000u, 0x1000u, CEILING));
}

TEST_CASE(a_program_of_no_bytes_follows_the_offset_rule)
{
    CHECK(out_store_range_program_ok(0x2000u, 0u, CEILING));
    CHECK(!out_store_range_program_ok(0x0000u, 0u, CEILING));
}

TEST_CASE(a_program_off_a_page_or_over_a_sector_is_refused)
{
    CHECK(!out_store_range_program_ok(0x2000u, 0xFFu, CEILING));
    CHECK(!out_store_range_program_ok(0x2000u, 0x1100u, CEILING));
    CHECK(!out_store_range_program_ok(0x2100u, 0x100u, CEILING));
}

TEST_CASE(a_program_ends_at_the_store_and_not_past_it)
{
    CHECK(out_store_range_program_ok(CEILING - 0x1000u, 0x1000u, CEILING));
    CHECK(!out_store_range_program_ok(CEILING, 0x100u, CEILING));
    /* No bytes at the store's first byte touch none of it. */
    CHECK(out_store_range_program_ok(CEILING, 0u, CEILING));
    CHECK(!out_store_range_program_ok(CEILING + 0x1000u, 0u, CEILING));
    CHECK(!out_store_range_program_ok(0xFFFFF000u, 0x1000u, 0xFFFFFFFFu));
}

int main(void)
{
    RUN(an_erase_of_the_partition_table_sectors_is_refused);
    RUN(an_erase_at_0x1fff_is_refused);
    RUN(an_erase_at_0x2000_runs);
    RUN(an_erase_off_a_sector_boundary_is_refused);
    RUN(an_erase_ends_at_the_store_and_not_past_it);
    RUN(an_erase_near_4gib_does_not_wrap_into_range);
    RUN(a_ceiling_at_or_below_the_floor_allows_nothing);
    RUN(a_program_that_starts_below_0x2000_is_refused);
    RUN(a_program_at_0x1fff_is_refused);
    RUN(a_program_from_0x2000_upward_runs);
    RUN(a_program_of_no_bytes_follows_the_offset_rule);
    RUN(a_program_off_a_page_or_over_a_sector_is_refused);
    RUN(a_program_ends_at_the_store_and_not_past_it);
    return test_summary("outstore_range");
}
