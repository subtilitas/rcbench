/*
 * Nothing in the panel link uses this any more: CAN carries a CRC, an
 * acknowledge slot and retransmission in silicon.  It stays for OpenYGE,
 * which needs the same polynomial with a different seed -- and the check
 * values below are what tell those two apart.
 *
 * It was written when a CRC was the only thing between a noisy pair and a register
 * write nobody asked for, so it is tested against the published check value
 * rather than against itself.
 */
#include <string.h>

#include "greatest.h"
#include "link_crc.h"

/* The one number an implementation on the other end of the wire can be held
 * to without reading this file. */
TEST_CASE(matches_the_published_check_value)
{
    CHECK_EQ(LINK_CRC_CHECK, link_crc(LINK_CRC_INIT, "123456789", 9));
}

TEST_CASE(empty_input_leaves_the_accumulator_alone)
{
    CHECK_EQ(LINK_CRC_INIT, link_crc(LINK_CRC_INIT, "", 0));
    CHECK_EQ(0x1234u, link_crc(0x1234u, NULL, 0));
}

/* Incremental has to mean incremental: the receiver folds bytes in as they
 * arrive rather than buffering a frame it may be about to reject. */
TEST_CASE(folding_in_pieces_equals_folding_at_once)
{
    static const char msg[] = "the coprocessor never speaks unsolicited";
    const size_t len = sizeof(msg) - 1;

    const uint16_t whole = link_crc(LINK_CRC_INIT, msg, len);
    for (size_t split = 0; split <= len; ++split) {
        uint16_t part = link_crc(LINK_CRC_INIT, msg, split);
        part = link_crc(part, msg + split, len - split);
        CHECK_EQ(whole, part);
    }
}

/* Every single-bit error in a 64-byte frame -- the size the link caps at --
 * must change the residue.  This is the property the wire actually needs. */
TEST_CASE(every_single_bit_flip_is_caught)
{
    uint8_t frame[64];
    for (size_t i = 0; i < sizeof(frame); ++i) {
        frame[i] = (uint8_t)(i * 7u + 3u);
    }
    const uint16_t good = link_crc(LINK_CRC_INIT, frame, sizeof(frame));

    for (size_t byte = 0; byte < sizeof(frame); ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            frame[byte] ^= (uint8_t)(1u << bit);
            CHECK(link_crc(LINK_CRC_INIT, frame, sizeof(frame)) != good);
            frame[byte] ^= (uint8_t)(1u << bit);
        }
    }
}

/* A frame that arrives one byte short must not verify as the frame it was.
 * Truncation is what a byte stream that loses its tail produces -- OpenYGE
 * frames arrive on a UART with no framing beneath them.  (It was also what a
 * mid-frame turnaround on an auto-direction transceiver
 * actually looks like, so it is not a hypothetical. */
TEST_CASE(truncation_changes_the_residue)
{
    static const uint8_t frame[] = { 0xA5, 0x10, 0x00, 0x01, 0x02, 0x03, 0x04 };
    const uint16_t whole = link_crc(LINK_CRC_INIT, frame, sizeof(frame));

    for (size_t len = 0; len < sizeof(frame); ++len) {
        CHECK(link_crc(LINK_CRC_INIT, frame, len) != whole);
    }
}

/* Leading zero bytes are the classic way a CRC with a zero init fools itself.
 * CCITT-FALSE starts at 0xFFFF precisely so they do not. */
TEST_CASE(leading_zeroes_are_not_transparent)
{
    static const uint8_t one[]  = { 0x00, 0x2A };
    static const uint8_t two[]  = { 0x00, 0x00, 0x2A };

    CHECK(link_crc(LINK_CRC_INIT, one, sizeof(one))
           != link_crc(LINK_CRC_INIT, two, sizeof(two)));
}

int main(void)
{
    RUN(matches_the_published_check_value);
    RUN(empty_input_leaves_the_accumulator_alone);
    RUN(folding_in_pieces_equals_folding_at_once);
    RUN(every_single_bit_flip_is_caught);
    RUN(truncation_changes_the_residue);
    RUN(leading_zeroes_are_not_transparent);
    return test_summary("link_crc");
}
