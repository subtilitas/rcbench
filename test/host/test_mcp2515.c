/*
 * The extended identifier, spread across four registers with a three-bit gap
 * in the middle of the second.
 *
 * A wrong layout fails silently: the frame goes out under a different
 * identifier, and on a bus that arbitrates by identifier the wrong frame
 * wins.  Every bit position is therefore checked individually against the
 * datasheet layout rather than by round-tripping values, because a round trip
 * agrees with itself when both halves share a mistake.
 *
 * SPDX-License-Identifier: MIT
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

/* A fake controller: receive buffers handed over in order, as the driver's
 * fetch consumes them. */
typedef struct {
    uint8_t buf[MCP2515_RX_BUFFERS][13];
    unsigned count, next;
} fake_rx_t;

static bool fake_fetch(void *ctx, uint8_t out[13])
{
    fake_rx_t *rx = (fake_rx_t *)ctx;
    if (rx->next >= rx->count) {
        return false;
    }
    memcpy(out, rx->buf[rx->next++], 13);
    return true;
}

static void put_frame(uint8_t b[13], uint32_t id, uint8_t dlc_reg, uint8_t d0,
                      bool extended)
{
    memset(b, 0, 13);
    (void)mcp2515_pack_id(id, b);
    if (!extended) {
        b[1] = (uint8_t)(b[1] & ~MCP2515_SIDL_EXIDE);
    }
    b[4] = dlc_reg;
    b[5] = d0;
}

TEST_CASE(a_refused_frame_in_rxb0_does_not_hide_a_valid_one_in_rxb1)
{
    link_can_frame_t f;
    fake_rx_t rx;
    memset(&rx, 0, sizeof(rx));
    rx.count = 2;
    put_frame(rx.buf[0], 0x123u, 0x42u, 0xEEu, true);    /* RTR, length 2 */
    put_frame(rx.buf[1], 0x1ABCDu, 0x01u, 0x5Au, true);  /* valid */
    CHECK(mcp2515_recv_next(fake_fetch, &rx, &f, NULL));
    CHECK_EQ(f.id, 0x1ABCDu);
    CHECK_EQ(f.dlc, 1u);
    CHECK_EQ(f.data[0], 0x5Au);
    CHECK_EQ(rx.next, 2u);
    CHECK(!mcp2515_recv_next(fake_fetch, &rx, &f, NULL));
}

TEST_CASE(every_kind_of_refused_frame_is_consumed_and_the_next_is_tried)
{
    link_can_frame_t f;
    for (int kind = 0; kind < 3; ++kind) {
        fake_rx_t rx;
        memset(&rx, 0, sizeof(rx));
        rx.count = 2;
        if (kind == 0) {
            put_frame(rx.buf[0], 0x55u, 0x01u, 0, false);   /* 11-bit id */
        } else if (kind == 1) {
            put_frame(rx.buf[0], 0x55u, 0x40u, 0, true);    /* RTR */
        } else {
            put_frame(rx.buf[0], 0x55u, 0x09u, 0, true);    /* DLC 9 */
        }
        put_frame(rx.buf[1], 0x77u, 0x02u, 0x11u, true);
        CHECK(mcp2515_recv_next(fake_fetch, &rx, &f, NULL));
        CHECK_EQ(f.id, 0x77u);
        CHECK_EQ(f.dlc, 2u);
    }
}

TEST_CASE(only_refused_frames_give_no_frame_and_all_are_consumed)
{
    link_can_frame_t f;
    fake_rx_t rx;
    memset(&rx, 0, sizeof(rx));
    rx.count = 2;
    put_frame(rx.buf[0], 0x55u, 0x40u, 0, true);
    put_frame(rx.buf[1], 0x56u, 0x40u, 0, true);
    CHECK(!mcp2515_recv_next(fake_fetch, &rx, &f, NULL));
    CHECK_EQ(rx.next, 2u);
    CHECK(!mcp2515_recv_next(NULL, &rx, &f, NULL));
    CHECK(!mcp2515_recv_next(fake_fetch, &rx, NULL, NULL));
}

/* Each refused buffer counts once, a frame taken counts nothing, and an
 * empty controller counts nothing. */
TEST_CASE(each_refused_buffer_is_counted_once)
{
    link_can_frame_t f;
    fake_rx_t rx;
    memset(&rx, 0, sizeof(rx));
    rx.count = 2;
    put_frame(rx.buf[0], 0x55u, 0x01u, 0, false);       /* 11-bit id */
    put_frame(rx.buf[1], 0x77u, 0x02u, 0x11u, true);    /* valid */
    uint32_t refused = 0;
    CHECK(mcp2515_recv_next(fake_fetch, &rx, &f, &refused));
    CHECK_EQ(refused, 1u);
    CHECK(!mcp2515_recv_next(fake_fetch, &rx, &f, &refused));
    CHECK_EQ(refused, 1u);

    /* Two refused: an RTR (remote transmission request) and a DLC (data
     * length code) of 15. */
    memset(&rx, 0, sizeof(rx));
    rx.count = 2;
    put_frame(rx.buf[0], 0x55u, 0x40u, 0, true);
    put_frame(rx.buf[1], 0x56u, 0x0Fu, 0, true);
    CHECK(!mcp2515_recv_next(fake_fetch, &rx, &f, &refused));
    CHECK_EQ(refused, 3u);

    /* A valid frame alone counts nothing. */
    memset(&rx, 0, sizeof(rx));
    rx.count = 1;
    put_frame(rx.buf[0], 0x77u, 0x00u, 0, true);
    CHECK(mcp2515_recv_next(fake_fetch, &rx, &f, &refused));
    CHECK_EQ(refused, 3u);
}

/* The count is 32 bits and wraps: 2^32 - 1 refusals and one more reads 0. */
TEST_CASE(the_refusal_count_wraps_at_two_to_the_32)
{
    link_can_frame_t f;
    fake_rx_t rx;
    memset(&rx, 0, sizeof(rx));
    rx.count = 2;
    put_frame(rx.buf[0], 0x55u, 0x40u, 0, true);
    put_frame(rx.buf[1], 0x56u, 0x40u, 0, true);
    uint32_t refused = 0xFFFFFFFFu;
    CHECK(!mcp2515_recv_next(fake_fetch, &rx, &f, &refused));
    CHECK_EQ(refused, 1u);
}

int main(void)
{
    RUN(every_bit_of_the_identifier_lands_where_the_datasheet_says);
    RUN(the_extended_flag_is_always_set_and_always_required);
    RUN(an_identifier_too_wide_is_refused_rather_than_truncated);
    RUN(the_links_own_identifiers_survive_the_registers);
    RUN(the_round_trip_holds_across_the_whole_identifier_space);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    RUN(a_refused_frame_in_rxb0_does_not_hide_a_valid_one_in_rxb1);
    RUN(every_kind_of_refused_frame_is_consumed_and_the_next_is_tried);
    RUN(only_refused_frames_give_no_frame_and_all_are_consumed);
    RUN(each_refused_buffer_is_counted_once);
    RUN(the_refusal_count_wraps_at_two_to_the_32);
    return test_summary("mcp2515");
}
