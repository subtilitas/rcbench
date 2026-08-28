/*
 * The extended identifier, spread across four registers with a three-bit gap
 * in the middle of the second.
 *
 * This fails silently when it is wrong: the frame goes out under a different
 * identifier from the one intended, and on a bus that arbitrates by identifier
 * that means the wrong thing wins. So every bit position is checked
 * individually rather than by round-tripping a handful of values -- a
 * round trip agrees with itself even when both halves share a mistake.
 */
#include <string.h>

#include "greatest.h"

#include "link_can.h"
#include "link_pages.h"
#include "mcp2515.h"

TEST_CASE(every_bit_of_the_identifier_lands_where_the_datasheet_says)
{
    for (int bit = 0; bit < 29; ++bit) {
        const uint32_t id = 1u << bit;
        uint8_t r[4];
        if (!mcp2515_pack_id(id, r)) {
            T_FAIL("bit %d would not pack", bit);
            continue;
        }
        /* Work out where that single bit should have landed, from the
         * datasheet's layout rather than from the implementation. */
        uint8_t want[4] = { 0, MCP2515_SIDL_EXIDE, 0, 0 };
        if (bit >= 21)      { want[0] = (uint8_t)(1u << (bit - 21)); }
        else if (bit >= 18) { want[1] |= (uint8_t)(1u << (bit - 18 + 5)); }
        else if (bit >= 16) { want[1] |= (uint8_t)(1u << (bit - 16)); }
        else if (bit >= 8)  { want[2] = (uint8_t)(1u << (bit - 8)); }
        else                { want[3] = (uint8_t)(1u << bit); }

        if (memcmp(r, want, 4) != 0) {
            T_FAIL("bit %d packed to %02X %02X %02X %02X, expected "
                   "%02X %02X %02X %02X", bit, r[0], r[1], r[2], r[3],
                   want[0], want[1], want[2], want[3]);
        }
    }
}

TEST_CASE(the_extended_flag_is_always_set_and_always_required)
{
    uint8_t r[4];
    CHECK(mcp2515_pack_id(0, r));
    CHECK_EQ(r[1] & MCP2515_SIDL_EXIDE, MCP2515_SIDL_EXIDE);

    /* A frame that arrived with an 11-bit identifier is refused rather than
     * read as though the missing bits were zero. */
    uint32_t id = 0xFFFFFFFFu;
    r[1] &= (uint8_t)~MCP2515_SIDL_EXIDE;
    CHECK_EQ(mcp2515_unpack_id(r, &id), false);
    CHECK_EQ(id, 0xFFFFFFFFu);   /* and it wrote nothing */
}

TEST_CASE(an_identifier_too_wide_is_refused_rather_than_truncated)
{
    uint8_t r[4] = { 0xAA, 0xAA, 0xAA, 0xAA };
    CHECK_EQ(mcp2515_pack_id(0x20000000u, r), false);
    CHECK_EQ(mcp2515_pack_id(0xFFFFFFFFu, r), false);
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(r[i], 0xAA);   /* untouched */
    }
    CHECK(mcp2515_pack_id(0x1FFFFFFFu, r));   /* the widest legal one */
}

/* The identifiers this link actually puts on the bus, through the registers
 * that will carry them. */
TEST_CASE(the_links_own_identifiers_survive_the_registers)
{
    const uint32_t ids[] = {
        link_can_id(LINK_CAN_PRIO_CONTROL, LINK_OP_WRITE, LINK_PAGE_CONTROL,
                    0, 1),
        link_can_id(LINK_CAN_PRIO_NORMAL, LINK_OP_READ, LINK_PAGE_BENCH,
                    0, 13),
        link_can_id(LINK_CAN_PRIO_NORMAL, LINK_OP_DATA, LINK_PAGE_BENCH,
                    12, 1),
        link_can_id(LINK_CAN_PRIO_BULK, LINK_OP_DATA, 0x7E, 0, 4),
        0u,
        0x1FFFFFFFu,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        uint8_t r[4];
        uint32_t back = 0;
        CHECK(mcp2515_pack_id(ids[i], r));
        CHECK(mcp2515_unpack_id(r, &back));
        if (back != ids[i]) {
            T_FAIL("id %08X came back as %08X", (unsigned)ids[i],
                   (unsigned)back);
        }
    }
}

/* Exhaustive over the identifier space, in steps small enough to catch a
 * single misplaced bit anywhere in it. */
TEST_CASE(the_round_trip_holds_across_the_whole_identifier_space)
{
    for (uint32_t id = 0; id <= 0x1FFFFFFFu; id += 0x3B9u) {
        uint8_t r[4];
        uint32_t back = 0;
        if (!mcp2515_pack_id(id, r) || !mcp2515_unpack_id(r, &back)
            || back != id) {
            T_FAIL("id %08X failed the round trip (got %08X)",
                   (unsigned)id, (unsigned)back);
            return;
        }
    }
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    uint32_t id = 0;
    uint8_t r[4] = { 0, MCP2515_SIDL_EXIDE, 0, 0 };
    CHECK_EQ(mcp2515_pack_id(0, NULL), false);
    CHECK_EQ(mcp2515_unpack_id(NULL, &id), false);
    CHECK_EQ(mcp2515_unpack_id(r, NULL), false);
}

int main(void)
{
    RUN(every_bit_of_the_identifier_lands_where_the_datasheet_says);
    RUN(the_extended_flag_is_always_set_and_always_required);
    RUN(an_identifier_too_wide_is_refused_rather_than_truncated);
    RUN(the_links_own_identifiers_survive_the_registers);
    RUN(the_round_trip_holds_across_the_whole_identifier_space);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("mcp2515");
}
