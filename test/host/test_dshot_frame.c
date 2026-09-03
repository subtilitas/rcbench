/*
 * The outbound DShot frame, and the map from travel onto it.
 *
 * The failures under test are the two that are silent on a wire: a checksum
 * that agrees with a shifted copy of the payload, and a throttle map that
 * walks through the command range on its way up from idle.  An ESC given
 * command 12 instead of 4% throttle saves its settings; nothing about that
 * looks like a driver fault.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "dshot.h"

/* The frame taken apart again, so a test says what it means. */
static uint16_t value_of(uint16_t frame) { return (uint16_t)(frame >> 5); }
static bool     telem_of(uint16_t frame) { return (frame & 0x0010u) != 0u; }
static uint16_t csum_of(uint16_t frame)  { return (uint16_t)(frame & 0x0Fu); }

TEST_CASE(a_frame_is_value_telemetry_and_checksum_in_that_order)
{
    const uint16_t f = dshot_frame(1047u, false, false);
    CHECK_EQ(value_of(f), 1047u);
    CHECK_EQ(telem_of(f), false);

    const uint16_t g = dshot_frame(1047u, true, false);
    CHECK_EQ(value_of(g), 1047u);
    CHECK_EQ(telem_of(g), true);
    /* The telemetry bit is inside the checksummed payload, so asking for
     * telemetry changes the checksum as well as the bit. */
    CHECK(csum_of(f) != csum_of(g));
}

TEST_CASE(the_checksum_folds_the_payload_the_way_an_esc_unfolds_it)
{
    /* An ESC checks by folding the whole frame and expecting zero.  If that
     * holds for every value, this end and that end agree. */
    for (uint16_t v = 0; v <= DSHOT_VALUE_MAX; ++v) {
        for (unsigned t = 0; t < 2u; ++t) {
            const uint16_t f = dshot_frame(v, t != 0u, false);
            uint16_t c = f;
            c ^= (uint16_t)(c >> 4);
            c ^= (uint16_t)(c >> 8);
            if ((c & 0x0Fu) != 0u) {
                T_FAIL("value %u telem %u folds to %u, not 0",
                       (unsigned)v, t, (unsigned)(c & 0x0Fu));
            }
        }
    }
}

TEST_CASE(the_bidirectional_checksum_is_the_complement_of_the_ordinary_one)
{
    /* The whole point: a receiver set up for one protocol rejects the other
     * rather than accepting a frame that means something else. */
    for (uint16_t v = 0; v <= DSHOT_VALUE_MAX; v = (uint16_t)(v + 37u)) {
        const uint16_t plain = dshot_frame(v, false, false);
        const uint16_t inv   = dshot_frame(v, false, true);
        CHECK_EQ(value_of(plain), value_of(inv));
        CHECK_EQ(csum_of(inv), (uint16_t)(~csum_of(plain) & 0x0Fu));
    }
}

TEST_CASE(a_value_that_does_not_fit_becomes_a_stop)
{
    CHECK_EQ(value_of(dshot_frame(2048u, false, false)), 0u);
    CHECK_EQ(value_of(dshot_frame(0xFFFFu, false, false)), 0u);
    /* And it is a well-formed stop, not a malformed anything. */
    uint16_t c = dshot_frame(2048u, false, false);
    c ^= (uint16_t)(c >> 4);
    c ^= (uint16_t)(c >> 8);
    CHECK_EQ((uint16_t)(c & 0x0Fu), 0u);
}

TEST_CASE(the_throttle_map_never_lands_in_the_command_range)
{
    /* One step above stop is already past the last command.  This is the
     * property that stops a slow ramp from beeping and saving settings. */
    CHECK_EQ(dshot_throttle(0u, 1000u), 0u);
    for (uint16_t c = 1u; c <= 1000u; ++c) {
        const uint16_t v = dshot_throttle(c, 1000u);
        if (v <= DSHOT_CMD_MAX || v > DSHOT_THROTTLE_MAX) {
            T_FAIL("command %u maps to %u, which is not throttle",
                   (unsigned)c, (unsigned)v);
        }
    }
}

TEST_CASE(the_throttle_map_reaches_both_ends_and_rises_all_the_way)
{
    CHECK_EQ(dshot_throttle(1u, 1000u), DSHOT_THROTTLE_MIN);
    CHECK_EQ(dshot_throttle(1000u, 1000u), DSHOT_THROTTLE_MAX);
    CHECK_EQ(dshot_throttle(2000u, 1000u), DSHOT_THROTTLE_MAX);

    uint16_t last = 0u;
    for (uint16_t c = 1u; c <= 1000u; ++c) {
        const uint16_t v = dshot_throttle(c, 1000u);
        if (v < last) {
            T_FAIL("command %u went backwards: %u after %u",
                   (unsigned)c, (unsigned)v, (unsigned)last);
        }
        last = v;
    }
    /* A span of 1000 against 2000 codes: every step forward moves the ESC. */
    CHECK_EQ(dshot_throttle(500u, 1000u), 1046u);
}

TEST_CASE(a_degenerate_span_does_not_divide_by_zero)
{
    /* Nothing configures a span of 0 or 1, and a driver that did would get a
     * division rather than a refusal, so both are answered explicitly. */
    CHECK_EQ(dshot_throttle(0u, 0u), 0u);
    CHECK_EQ(dshot_throttle(500u, 0u), DSHOT_THROTTLE_MAX);
    CHECK_EQ(dshot_throttle(1u, 1u), DSHOT_THROTTLE_MAX);
}

int main(void)
{
    RUN(a_frame_is_value_telemetry_and_checksum_in_that_order);
    RUN(the_checksum_folds_the_payload_the_way_an_esc_unfolds_it);
    RUN(the_bidirectional_checksum_is_the_complement_of_the_ordinary_one);
    RUN(a_value_that_does_not_fit_becomes_a_stop);
    RUN(the_throttle_map_never_lands_in_the_command_range);
    RUN(the_throttle_map_reaches_both_ends_and_rises_all_the_way);
    RUN(a_degenerate_span_does_not_divide_by_zero);
    return test_summary("dshot_frame");
}
