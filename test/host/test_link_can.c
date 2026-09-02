/*
 * The page protocol carried on CAN (Controller Area Network) identifiers.
 * Three properties: a read fits entirely in its identifier, frames describe
 * themselves so nothing is reassembled, and the arbitration order puts
 * stopping the bench ahead of reading it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_can.h"
#include "link_pages.h"

static link_msg_t msg(uint8_t op, uint8_t page, uint8_t offset, uint8_t count)
{
    link_msg_t m;
    memset(&m, 0, sizeof(m));
    m.op = op; m.page = page; m.offset = offset; m.count = count;
    /* Bounded by the array, not by count: some cases pass an over-large count
     * on purpose to prove the encoder rejects it, and filling regs to that
     * count would run off the end before the encoder ever saw the message. */
    for (uint8_t i = 0; i < count && i < LINK_MAX_REGS; ++i) {
        m.regs[i] = (uint16_t)(0x1000 + i);
    }
    return m;
}

TEST_CASE(an_identifier_carries_every_field_and_survives_the_round_trip)
{
    const uint32_t id = link_can_id(LINK_CAN_PRIO_NORMAL, LINK_OP_DATA,
                                    LINK_PAGE_BENCH, 12, 31);
    /* 29 bits and no more: an identifier wider than the extended field would
     * be silently truncated by every controller on the bus. */
    CHECK_EQ(id >> 29, 0);

    link_can_prio_t prio; uint8_t op, page, offset, count;
    link_can_id_split(id, &prio, &op, &page, &offset, &count);
    CHECK_EQ(prio, LINK_CAN_PRIO_NORMAL);
    CHECK_EQ(op, LINK_OP_DATA);
    CHECK_EQ(page, LINK_PAGE_BENCH);
    CHECK_EQ(offset, 12);
    CHECK_EQ(count, 31);
}

/* Every field at its widest, so a mask that is one bit short shows up here
 * rather than on a bus. */
TEST_CASE(the_fields_do_not_overlap_at_their_extremes)
{
    const uint32_t id = link_can_id((link_can_prio_t)7, 0xF, 0xFF, 0xFF, 0x3F);
    CHECK_EQ(id, 0x1FFFFFFFu);   /* all 29 bits, and exactly 29 */

    link_can_prio_t prio; uint8_t op, page, offset, count;
    link_can_id_split(id, &prio, &op, &page, &offset, &count);
    CHECK_EQ(prio, 7); CHECK_EQ(op, 0xF); CHECK_EQ(page, 0xFF);
    CHECK_EQ(offset, 0xFF); CHECK_EQ(count, 0x3F);

    /* And one field set at a time never disturbs another. */
    CHECK_EQ(link_can_id(0, 0, 0, 0, 0), 0u);
    CHECK_EQ(link_can_id(0, 0, 0xFF, 0, 0) & ~(0xFFu << 14), 0u);
}

/*
 * The property that makes stopping work: a control write outranks every
 * telemetry frame on the bus, decided by the identifier and therefore by the
 * bus itself rather than by anything either processor chooses to do.
 */
TEST_CASE(stopping_the_bench_wins_arbitration_against_reading_it)
{
    const link_msg_t stop = msg(LINK_OP_WRITE, LINK_PAGE_CONTROL, 0, 1);
    const link_msg_t poll = msg(LINK_OP_READ, LINK_PAGE_BENCH, 0, 13);
    link_can_frame_t s[LINK_CAN_MAX_FRAMES], p[LINK_CAN_MAX_FRAMES];
    CHECK_EQ(link_can_encode(&stop, s, LINK_CAN_MAX_FRAMES), 1);
    CHECK_EQ(link_can_encode(&poll, p, LINK_CAN_MAX_FRAMES), 1);

    /* Lower identifier wins, so this comparison IS the guarantee. */
    CHECK(s[0].id < p[0].id);

    /* And so does the news that the bench refused: a NACK (negative
     * acknowledge) of a control write is as urgent as the write. */
    const link_msg_t nack = msg(LINK_OP_NACK, LINK_PAGE_CONTROL, 0, 1);
    link_can_frame_t n[LINK_CAN_MAX_FRAMES];
    CHECK_EQ(link_can_encode(&nack, n, LINK_CAN_MAX_FRAMES), 1);
    CHECK(n[0].id < p[0].id);

    /* The failsafe and limits pages are control too: they decide when the
     * bench stops without being asked. */
    CHECK_EQ(link_can_priority(LINK_PAGE_FAILSAFE, LINK_OP_WRITE),
             LINK_CAN_PRIO_CONTROL);
    CHECK_EQ(link_can_priority(LINK_PAGE_LIMITS, LINK_OP_WRITE),
             LINK_CAN_PRIO_CONTROL);
    CHECK_EQ(link_can_priority(LINK_PAGE_BENCH, LINK_OP_READ),
             LINK_CAN_PRIO_NORMAL);
    CHECK_EQ(link_can_priority(LINK_PAGE_STATUS, LINK_OP_READ),
             LINK_CAN_PRIO_NORMAL);
}

/* A poll is one frame with nothing in it: the whole question is the address. */
TEST_CASE(a_read_request_is_one_frame_carrying_no_payload)
{
    const link_msg_t m = msg(LINK_OP_READ, LINK_PAGE_BENCH, 0, LINK_BN_COUNT);
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    CHECK_EQ(link_can_encode(&m, f, LINK_CAN_MAX_FRAMES), 1);
    CHECK_EQ(f[0].dlc, 0);

    link_msg_t back;
    CHECK(link_can_decode(&f[0], &back));
    CHECK_EQ(back.op, LINK_OP_READ);
    CHECK_EQ(back.page, LINK_PAGE_BENCH);
    CHECK_EQ(back.count, LINK_BN_COUNT);
}

/*
 * Every frame carries its own offset and its own count, so a reply split
 * across several is several complete messages rather than a sequence.  This
 * is what removes reassembly, and with it the timer waiting for a
 * continuation that never comes.
 */
TEST_CASE(a_split_reply_is_frames_that_each_stand_alone)
{
    const link_msg_t m = msg(LINK_OP_DATA, LINK_PAGE_BENCH, 0, LINK_BN_COUNT);
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    const size_t n = link_can_encode(&m, f, LINK_CAN_MAX_FRAMES);
    CHECK_EQ(n, 4);   /* 13 registers, four to a frame: 4+4+4+1 */

    uint16_t seen[LINK_MAX_REGS];
    memset(seen, 0, sizeof(seen));
    /* Decoded in reverse, because order must not matter to a self-describing
     * frame -- and a receiver that depended on arrival order would pass a
     * forward-only test. */
    for (size_t i = n; i-- > 0;) {
        link_msg_t part;
        if (!link_can_decode(&f[i], &part)) {
            T_FAIL("frame %zu did not decode", i);
            continue;
        }
        CHECK_EQ(part.op, LINK_OP_DATA);
        CHECK_EQ(part.page, LINK_PAGE_BENCH);
        for (uint8_t r = 0; r < part.count; ++r) {
            seen[part.offset + r] = part.regs[r];
        }
    }
    for (uint8_t i = 0; i < LINK_BN_COUNT; ++i) {
        CHECK_EQ(seen[i], 0x1000 + i);
    }
    CHECK_EQ(f[3].dlc, 2);   /* the odd one out carries a single register */
}

TEST_CASE(a_whole_page_fits_the_frame_budget_exactly)
{
    const link_msg_t m = msg(LINK_OP_DATA, LINK_PAGE_BENCH, 0, LINK_MAX_REGS);
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    const size_t n = link_can_encode(&m, f, LINK_CAN_MAX_FRAMES);
    CHECK_EQ(n, LINK_CAN_MAX_FRAMES);
    CHECK_EQ(n, 8);
    for (size_t i = 0; i < n; ++i) {
        CHECK_EQ(f[i].dlc, 8);
    }
    /* One frame short of the budget must fail rather than truncate. */
    CHECK_EQ(link_can_encode(&m, f, n - 1), 0);
}

/* A transfer that starts inside a page and runs past its end is a caller bug,
 * and the offsets it would produce would address another page's registers. */
TEST_CASE(a_transfer_past_the_end_of_a_page_is_refused)
{
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    link_msg_t m = msg(LINK_OP_DATA, LINK_PAGE_BENCH, 30, 4);
    CHECK_EQ(link_can_encode(&m, f, LINK_CAN_MAX_FRAMES), 0);
    m = msg(LINK_OP_READ, LINK_PAGE_BENCH, 0, LINK_MAX_REGS + 1);
    CHECK_EQ(link_can_encode(&m, f, LINK_CAN_MAX_FRAMES), 0);
    m = msg(0x09, LINK_PAGE_BENCH, 0, 1);   /* not an op */
    CHECK_EQ(link_can_encode(&m, f, LINK_CAN_MAX_FRAMES), 0);
}

TEST_CASE(a_frame_whose_payload_disagrees_with_its_address_is_refused)
{
    link_msg_t out;
    link_can_frame_t f;
    memset(&f, 0, sizeof(f));

    /* Says three registers, carries two. */
    f.id = link_can_id(LINK_CAN_PRIO_NORMAL, LINK_OP_DATA, LINK_PAGE_BENCH,
                       0, 3);
    f.dlc = 4;
    CHECK_EQ(link_can_decode(&f, &out), false);

    /* Says two, carries eight. */
    f.dlc = 8;
    CHECK_EQ(link_can_decode(&f, &out), false);

    /* More registers than one frame can hold, whatever the payload says. */
    f.id = link_can_id(LINK_CAN_PRIO_NORMAL, LINK_OP_DATA, LINK_PAGE_BENCH,
                       0, 5);
    f.dlc = 8;
    CHECK_EQ(link_can_decode(&f, &out), false);

    /* A question with an answer attached. */
    f.id = link_can_id(LINK_CAN_PRIO_NORMAL, LINK_OP_READ, LINK_PAGE_BENCH,
                       0, 4);
    f.dlc = 8;
    CHECK_EQ(link_can_decode(&f, &out), false);

    /* An op nobody speaks. */
    f.id = link_can_id(LINK_CAN_PRIO_NORMAL, 0x0A, LINK_PAGE_BENCH, 0, 0);
    f.dlc = 0;
    CHECK_EQ(link_can_decode(&f, &out), false);

    /* A DLC (data length code) no CAN controller can produce. */
    f.id = link_can_id(LINK_CAN_PRIO_NORMAL, LINK_OP_DATA, LINK_PAGE_BENCH,
                       0, 4);
    f.dlc = 9;
    CHECK_EQ(link_can_decode(&f, &out), false);
}

/* An acknowledgement carries nothing and is still a message. */
TEST_CASE(an_ack_is_one_empty_frame_and_a_nack_carries_its_reason)
{
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    link_msg_t out;

    const link_msg_t ack = msg(LINK_OP_ACK, LINK_PAGE_CONTROL, 0, 0);
    CHECK_EQ(link_can_encode(&ack, f, LINK_CAN_MAX_FRAMES), 1);
    CHECK_EQ(f[0].dlc, 0);
    CHECK(link_can_decode(&f[0], &out));
    CHECK_EQ(out.op, LINK_OP_ACK);
    CHECK_EQ(out.count, 0);

    link_msg_t nack = msg(LINK_OP_NACK, LINK_PAGE_CONTROL, 0, 1);
    nack.regs[0] = LINK_NACK_BAD_VALUE;
    CHECK_EQ(link_can_encode(&nack, f, LINK_CAN_MAX_FRAMES), 1);
    CHECK_EQ(f[0].dlc, 2);
    CHECK(link_can_decode(&f[0], &out));
    CHECK_EQ(out.regs[0], LINK_NACK_BAD_VALUE);
}

/* Registers cross the wire little-endian. */
TEST_CASE(registers_are_little_endian_on_the_wire)
{
    link_msg_t m = msg(LINK_OP_DATA, LINK_PAGE_BENCH, 0, 1);
    m.regs[0] = 0xBEEF;
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    CHECK_EQ(link_can_encode(&m, f, LINK_CAN_MAX_FRAMES), 1);
    CHECK_EQ(f[0].data[0], 0xEF);
    CHECK_EQ(f[0].data[1], 0xBE);
}

/* Every op and every count, encoded and decoded back, with nothing lost. */
TEST_CASE(every_op_and_count_survives_the_round_trip)
{
    const uint8_t ops[] = { LINK_OP_READ, LINK_OP_WRITE, LINK_OP_DATA,
                            LINK_OP_ACK, LINK_OP_NACK };
    for (size_t o = 0; o < sizeof(ops); ++o) {
        for (uint8_t count = 0; count <= LINK_MAX_REGS; ++count) {
            const link_msg_t m = msg(ops[o], LINK_PAGE_BENCH, 0, count);
            link_can_frame_t f[LINK_CAN_MAX_FRAMES];
            const size_t n = link_can_encode(&m, f, LINK_CAN_MAX_FRAMES);
            if (n == 0) {
                T_FAIL("op %u count %u would not encode", ops[o], count);
                continue;
            }
            uint16_t seen[LINK_MAX_REGS];
            memset(seen, 0, sizeof(seen));
            uint8_t total = 0;
            for (size_t i = 0; i < n; ++i) {
                link_msg_t part;
                if (!link_can_decode(&f[i], &part)) {
                    T_FAIL("op %u count %u frame %zu did not decode",
                           ops[o], count, i);
                    break;
                }
                for (uint8_t r = 0; r < part.count; ++r) {
                    seen[part.offset + r] = part.regs[r];
                }
                total = (uint8_t)(total + part.count);
            }
            if (ops[o] == LINK_OP_READ) {
                continue;   /* carries no registers by construction */
            }
            if (total != count) {
                T_FAIL("op %u: %u registers in, %u out", ops[o], count, total);
            }
            for (uint8_t r = 0; r < count; ++r) {
                CHECK_EQ(seen[r], 0x1000 + r);
            }
        }
    }
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    const link_msg_t m = msg(LINK_OP_DATA, LINK_PAGE_BENCH, 0, 1);
    CHECK_EQ(link_can_encode(NULL, f, LINK_CAN_MAX_FRAMES), 0);
    CHECK_EQ(link_can_encode(&m, NULL, LINK_CAN_MAX_FRAMES), 0);
    CHECK_EQ(link_can_decode(NULL, NULL), false);
    link_can_id_split(0, NULL, NULL, NULL, NULL, NULL);
}

int main(void)
{
    RUN(an_identifier_carries_every_field_and_survives_the_round_trip);
    RUN(the_fields_do_not_overlap_at_their_extremes);
    RUN(stopping_the_bench_wins_arbitration_against_reading_it);
    RUN(a_read_request_is_one_frame_carrying_no_payload);
    RUN(a_split_reply_is_frames_that_each_stand_alone);
    RUN(a_whole_page_fits_the_frame_budget_exactly);
    RUN(a_transfer_past_the_end_of_a_page_is_refused);
    RUN(a_frame_whose_payload_disagrees_with_its_address_is_refused);
    RUN(an_ack_is_one_empty_frame_and_a_nack_carries_its_reason);
    RUN(registers_are_little_endian_on_the_wire);
    RUN(every_op_and_count_survives_the_round_trip);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("link_can");
}
