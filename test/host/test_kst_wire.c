/*
 * KST frames and the reply decoder, fed the captures a servo and a bad line
 * produce.
 *
 * The protocol has no parity and no checksum.  What stands between a
 * disturbed reply and a wrong register value is the decoder's set of
 * plausibility checks, so every check is held at its limit and one step
 * beyond it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "kst_sim.h"
#include "kst_wire.h"

/* --- frames ------------------------------------------------------------------ */

/* Manchester code of a 27-bit word, written out without the module: the
 * levels from the first high to the last high. */
static unsigned expand(uint32_t word, unsigned bits, uint8_t *levels)
{
    unsigned n = 0;
    unsigned last_high = 0;

    for (unsigned i = 0; i < bits; ++i) {
        const bool bit = ((word >> (bits - 1u - i)) & 1u) != 0u;

        levels[n++] = bit ? 0u : 1u;
        levels[n++] = bit ? 1u : 0u;
    }
    for (unsigned i = 0; i < n; ++i) {
        if (levels[i] != 0u) {
            last_high = i;
        }
    }
    /* Drop the leading low of the start bit. */
    memmove(levels, levels + 1, n - 1u);
    return last_high;
}

static void check_frame(const kst_frame_t *f, uint32_t word)
{
    uint8_t levels[2u * KST_FRAME_BITS];
    const unsigned n = expand(word, KST_FRAME_BITS, levels);

    CHECK_EQ(f->n_half, n);
    for (unsigned i = 0; i < n; ++i) {
        CHECK_EQ(kst_frame_level(f, i), levels[i]);
    }
    CHECK(kst_frame_level(f, 0));
    CHECK(kst_frame_level(f, n - 1u));
    CHECK(!kst_frame_level(f, n));
}

TEST_CASE(a_read_frame_is_start_10_zeros_address_zeros)
{
    kst_frame_t f;

    CHECK(kst_frame_read(0x01, &f));
    CHECK_EQ(f.kind, KST_FRAME_READ);
    /* 1 10 00000000 00000001 00000000 */
    check_frame(&f, 0x6000100u);
    CHECK(kst_frame_read(0x1F, &f));
    check_frame(&f, 0x6001F00u);
    CHECK(kst_frame_read(0x00, &f));
    check_frame(&f, 0x6000000u);
}

TEST_CASE(a_write_frame_is_start_00_10000000_address_data)
{
    kst_frame_t f;

    CHECK(kst_frame_write(0x05, 0x77, &f));
    CHECK_EQ(f.kind, KST_FRAME_WRITE);
    /* 1 00 10000000 00000101 01110111 */
    check_frame(&f, 0x4800577u);
    CHECK(kst_frame_write(0x1F, 0xFF, &f));
    check_frame(&f, 0x4801FFFu);
    CHECK(kst_frame_write(0x01, 0x00, &f));
    check_frame(&f, 0x4800100u);
}

TEST_CASE(every_register_and_value_gives_a_frame_the_servo_side_reads_back)
{
    for (unsigned reg = 1; reg <= KST_REG_MAX; ++reg) {
        for (unsigned value = 0; value < 256u; ++value) {
            kst_frame_t f;
            uint8_t bits[64];

            CHECK(kst_frame_write((uint8_t)reg, (uint8_t)value, &f));
            CHECK_EQ(sim_read_frame(&f, bits, 64), KST_FRAME_BITS);
            CHECK_EQ(sim_bits(bits, 0, 11), 0x480u);
            CHECK_EQ(sim_bits(bits, 11, 8), reg);
            CHECK_EQ(sim_bits(bits, 19, 8), value);
            CHECK(f.n_half <= KST_FRAME_MAX_HALF_CELLS);
        }
    }
}

TEST_CASE(register_00_and_addresses_above_1f_are_never_written)
{
    kst_frame_t f;

    memset(&f, 0xAA, sizeof(f));
    CHECK(!kst_frame_write(0x00, 0x00, &f));
    CHECK(!kst_frame_write(0x20, 0x12, &f));
    CHECK(!kst_frame_write(0xFF, 0x12, &f));
    /* No frame is left behind for a driver to send. */
    CHECK_EQ(f.n_half, 0xAA);
    CHECK(kst_frame_write(0x01, 0x12, &f));
    CHECK(kst_frame_write(0x1F, 0x12, &f));
    CHECK(!kst_frame_write(0x01, 0x12, NULL));
}

TEST_CASE(a_read_above_1f_is_refused)
{
    kst_frame_t f;

    CHECK(kst_frame_read(0x1F, &f));
    CHECK(!kst_frame_read(0x20, &f));
    CHECK(!kst_frame_read(0xFF, &f));
    CHECK(!kst_frame_read(0x01, NULL));
}

TEST_CASE(the_sync_burst_is_64_pulses_of_one_half_cell)
{
    kst_frame_t f;
    unsigned highs = 0;

    kst_frame_sync(&f);
    CHECK_EQ(f.kind, KST_FRAME_SYNC);
    CHECK_EQ(f.n_half, 127);
    CHECK_EQ(f.n_half, KST_FRAME_MAX_HALF_CELLS);
    for (unsigned i = 0; i < f.n_half; ++i) {
        CHECK_EQ(kst_frame_level(&f, i), (i % 2u) == 0u);
        highs += kst_frame_level(&f, i) ? 1u : 0u;
    }
    CHECK_EQ(highs, KST_SYNC_PULSES);
    CHECK(!kst_frame_level(&f, 127));
    kst_frame_sync(NULL);
}

TEST_CASE(timing_constants_are_the_measured_ones)
{
    kst_frame_t f;

    /* 25.40 us is 3810 cycles of 150 MHz. */
    CHECK_EQ(KST_HALF_CELL_NS, 25400);
    CHECK_EQ((uint64_t)KST_HALF_CELL_CYCLES_150MHZ * 1000000000u / 150000000u,
             KST_HALF_CELL_NS);
    CHECK_EQ(2u * KST_SERVO_HALF_CELL_NS, KST_SERVO_CELL_NS);
    kst_frame_sync(&f);
    CHECK_EQ(kst_frame_duration_ns(&f), 127u * 25400u);
    CHECK(kst_frame_read(0x01, &f));
    /* 54 half-cells less the low in front and the low at the end: the last
     * bit is a 0, high then low, and its falling edge is the last edge. */
    CHECK_EQ(kst_frame_duration_ns(&f), 52u * 25400u);
    CHECK(kst_frame_write(0x01, 0x01, &f));
    CHECK_EQ(kst_frame_duration_ns(&f), 53u * 25400u);
    CHECK_EQ(kst_frame_duration_ns(NULL), 0);
    CHECK(!kst_frame_level(NULL, 0));
    CHECK_EQ(kst_reply_window_ns(KST_EXPECT_READ), 1500000);
    CHECK_EQ(kst_reply_window_ns(KST_EXPECT_SYNC), 1500000);
    CHECK_EQ(kst_reply_window_ns(KST_EXPECT_WRITE), 8000000);
}

/* --- replies ----------------------------------------------------------------- */

static kst_rx_t decode_read(unsigned value, const sim_shape_t *shape,
                            kst_reply_t *out)
{
    kst_capture_t cap;

    sim_reply(&cap, KST_READ_WINDOW_NS, 3, value, shape);
    return kst_reply_decode(&cap, KST_EXPECT_READ, out);
}

TEST_CASE(every_value_decodes_from_a_reply_as_the_servo_sends_it)
{
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);

    for (unsigned value = 0; value < 256u; ++value) {
        kst_reply_t r;

        CHECK_EQ(decode_read(value, &shape, &r), KST_RX_OK);
        CHECK_EQ(r.result, KST_RX_OK);
        CHECK_EQ(r.value, value);
        CHECK_EQ(r.flags, 3);
        CHECK_EQ(r.glitches, 0);
        CHECK_EQ(r.delay_ns, SIM_READ_DELAY_NS);
        CHECK_EQ(r.half_cell_ns, SIM_HALF_NS);
    }
}

TEST_CASE(a_reply_after_a_write_is_taken_in_its_own_window)
{
    const sim_shape_t shape = sim_shape(SIM_WRITE_DELAY_NS);
    kst_capture_t cap;
    kst_reply_t r;

    sim_reply(&cap, KST_WRITE_WINDOW_NS, 3, 0x26, &shape);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_WRITE, &r), KST_RX_OK);
    CHECK_EQ(r.value, 0x26);
    CHECK_EQ(r.delay_ns, SIM_WRITE_DELAY_NS);
    /* The same edges are no reply to a read: far too late. */
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_LATE);
}

TEST_CASE(the_sync_acknowledge_is_flags_10_and_value_00)
{
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_capture_t cap;
    kst_reply_t r;

    sim_reply(&cap, KST_READ_WINDOW_NS, 2, 0x00, &shape);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_SYNC, &r), KST_RX_OK);
    CHECK_EQ(r.flags, 2);
    /* It is not a reply to a read or a write. */
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_FLAGS);
    /* A value other than 00 is no acknowledge. */
    sim_reply(&cap, KST_READ_WINDOW_NS, 2, 0x01, &shape);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_SYNC, &r), KST_RX_FLAGS);
    /* Nor is a read reply. */
    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x00, &shape);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_SYNC, &r), KST_RX_FLAGS);
}

TEST_CASE(flags_other_than_11_fail_a_read_and_a_write)
{
    for (unsigned flags = 0; flags < 4u; ++flags) {
        sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
        kst_capture_t cap;
        kst_reply_t r;

        sim_reply(&cap, KST_READ_WINDOW_NS, flags, 0x55, &shape);
        CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r),
                 flags == 3u ? KST_RX_OK : KST_RX_FLAGS);
        CHECK_EQ(r.flags, flags);
        shape = sim_shape(SIM_WRITE_DELAY_NS);
        sim_reply(&cap, KST_WRITE_WINDOW_NS, flags, 0x55, &shape);
        CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_WRITE, &r),
                 flags == 3u ? KST_RX_OK : KST_RX_FLAGS);
    }
}

TEST_CASE(no_edge_in_the_window_is_no_reply)
{
    kst_capture_t cap;
    kst_reply_t r;

    sim_cap_clear(&cap, KST_READ_WINDOW_NS);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_NO_REPLY);
    CHECK_EQ(r.result, KST_RX_NO_REPLY);
    CHECK_EQ(r.delay_ns, 0);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_SYNC, &r), KST_RX_NO_REPLY);
    sim_cap_clear(&cap, KST_WRITE_WINDOW_NS);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_WRITE, &r), KST_RX_NO_REPLY);
    /* The result is returned without a place to put the reply too. */
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_WRITE, NULL), KST_RX_NO_REPLY);
}

TEST_CASE(a_reply_too_early_is_refused_at_the_window_edge)
{
    sim_shape_t shape = sim_shape(KST_READ_ACCEPT_MIN_NS);
    kst_capture_t cap;
    kst_reply_t r;

    CHECK_EQ(decode_read(0xA5, &shape, &r), KST_RX_OK);
    shape.delay_ns = KST_READ_ACCEPT_MIN_NS - 1u;
    CHECK_EQ(decode_read(0xA5, &shape, &r), KST_RX_QUIET);

    shape = sim_shape(KST_WRITE_ACCEPT_MIN_NS);
    sim_reply(&cap, KST_WRITE_WINDOW_NS, 3, 0xA5, &shape);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_WRITE, &r), KST_RX_OK);
    shape.delay_ns = KST_WRITE_ACCEPT_MIN_NS - 1u;
    sim_reply(&cap, KST_WRITE_WINDOW_NS, 3, 0xA5, &shape);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_WRITE, &r), KST_RX_QUIET);
    /* A read reply's timing after a write is an edge in the quiet time. */
    shape.delay_ns = SIM_READ_DELAY_NS;
    sim_reply(&cap, KST_WRITE_WINDOW_NS, 3, 0xA5, &shape);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_WRITE, &r), KST_RX_QUIET);
}

TEST_CASE(a_reply_too_late_is_refused_at_the_window_edge)
{
    sim_shape_t shape = sim_shape(KST_READ_ACCEPT_MAX_NS);
    kst_capture_t cap;
    kst_reply_t r;

    CHECK_EQ(decode_read(0xA5, &shape, &r), KST_RX_OK);
    CHECK_EQ(r.delay_ns, KST_READ_ACCEPT_MAX_NS);
    shape.delay_ns = KST_READ_ACCEPT_MAX_NS + 1u;
    CHECK_EQ(decode_read(0xA5, &shape, &r), KST_RX_LATE);

    shape = sim_shape(KST_WRITE_ACCEPT_MAX_NS);
    sim_reply(&cap, KST_WRITE_WINDOW_NS, 3, 0xA5, &shape);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_WRITE, &r), KST_RX_OK);
    shape.delay_ns = KST_WRITE_ACCEPT_MAX_NS + 1u;
    sim_reply(&cap, KST_WRITE_WINDOW_NS, 3, 0xA5, &shape);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_WRITE, &r), KST_RX_LATE);
}

TEST_CASE(the_bit_rate_is_held_to_5_percent_at_both_bounds)
{
    sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_reply_t r;

    for (unsigned value = 0; value < 256u; value += 51u) {
        shape.half_ns = KST_RX_HALF_CELL_MIN_NS;
        CHECK_EQ(decode_read(value, &shape, &r), KST_RX_OK);
        CHECK_EQ(r.value, value);
        CHECK_EQ(r.half_cell_ns, KST_RX_HALF_CELL_MIN_NS);
        shape.half_ns = KST_RX_HALF_CELL_MIN_NS - 1u;
        CHECK_EQ(decode_read(value, &shape, &r), KST_RX_RATE);
        CHECK_EQ(r.half_cell_ns, KST_RX_HALF_CELL_MIN_NS - 1u);

        shape.half_ns = KST_RX_HALF_CELL_MAX_NS;
        CHECK_EQ(decode_read(value, &shape, &r), KST_RX_OK);
        CHECK_EQ(r.value, value);
        shape.half_ns = KST_RX_HALF_CELL_MAX_NS + 1u;
        CHECK_EQ(decode_read(value, &shape, &r), KST_RX_RATE);
    }
    /* A reply at half the rate is not followed at all. */
    shape.half_ns = 2u * SIM_HALF_NS;
    CHECK(decode_read(0x55, &shape, &r) != KST_RX_OK);
    shape.half_ns = SIM_HALF_NS / 2u;
    CHECK(decode_read(0x55, &shape, &r) != KST_RX_OK);
}

TEST_CASE(a_glitch_inside_a_cell_is_removed_below_1500_ns_only)
{
    sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_reply_t r;

    /* In the middle of a half-cell, in every cell of the reply. */
    for (unsigned cell = 0; cell < 11u; ++cell) {
        shape.glitch_at_ns = SIM_READ_DELAY_NS + cell * 2u * SIM_HALF_NS
                             + SIM_HALF_NS / 2u;
        shape.glitch_ns = KST_GLITCH_NS - 1u;
        CHECK_EQ(decode_read(0xC3, &shape, &r), KST_RX_OK);
        CHECK_EQ(r.value, 0xC3);
        CHECK_EQ(r.glitches, 1);
    }
    /* A level that lasts 1500 ns is a level change.  Placed around the
     * confirm point of a cell it makes both halves read alike. */
    shape.glitch_at_ns = SIM_READ_DELAY_NS + 2u * SIM_HALF_NS
                         + KST_RX_CONFIRM_NS - 700u;
    shape.glitch_ns = KST_GLITCH_NS - 1u;
    CHECK_EQ(decode_read(0xC3, &shape, &r), KST_RX_OK);
    shape.glitch_ns = KST_GLITCH_NS;
    CHECK_EQ(decode_read(0xC3, &shape, &r), KST_RX_CELL);
    CHECK_EQ(r.glitches, 0);
}

TEST_CASE(a_glitch_before_the_reply_does_not_start_it)
{
    sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_reply_t r;

    shape.glitch_at_ns = 100000;
    shape.glitch_ns = 1000;
    CHECK_EQ(decode_read(0x3C, &shape, &r), KST_RX_OK);
    CHECK_EQ(r.glitches, 1);
    CHECK_EQ(r.delay_ns, SIM_READ_DELAY_NS);
    /* A real pulse there breaks the quiet time. */
    shape.glitch_ns = 5000;
    CHECK_EQ(decode_read(0x3C, &shape, &r), KST_RX_QUIET);
}

TEST_CASE(chatter_on_an_edge_ends_on_its_last_edge)
{
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_capture_t cap;
    kst_reply_t r;

    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x81, &shape);
    /* The first rising edge bounces: up, down, up within 800 ns. */
    sim_cap_edge(&cap, SIM_READ_DELAY_NS + 400u);
    sim_cap_edge(&cap, SIM_READ_DELAY_NS + 800u);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_OK);
    CHECK_EQ(r.value, 0x81);
    CHECK_EQ(r.glitches, 1);
}

TEST_CASE(a_stretched_last_half_cell_is_not_a_bit)
{
    sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_reply_t r;

    /* 32 us at a 1.4 V threshold, 59 us at 0.1 V. */
    shape.tail_ns = 32000;
    CHECK_EQ(decode_read(0x01, &shape, &r), KST_RX_OK);
    CHECK_EQ(r.value, 0x01);
    shape.tail_ns = 59000;
    CHECK_EQ(decode_read(0x01, &shape, &r), KST_RX_OK);
    CHECK_EQ(r.value, 0x01);
    CHECK_EQ(decode_read(0xFF, &shape, &r), KST_RX_OK);
    CHECK_EQ(r.value, 0xFF);
    /* The stretch does not enter the rate. */
    CHECK_EQ(r.half_cell_ns, SIM_HALF_NS);
}

TEST_CASE(the_final_edge_has_100_us_after_the_nominal_end)
{
    sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_capture_t cap;
    kst_reply_t r;

    shape.tail_ns = SIM_HALF_NS + KST_RX_FINAL_EDGE_MAX_NS;
    CHECK_EQ(decode_read(0x01, &shape, &r), KST_RX_OK);
    shape.tail_ns = SIM_HALF_NS + KST_RX_FINAL_EDGE_MAX_NS + 1u;
    CHECK_EQ(decode_read(0x01, &shape, &r), KST_RX_FINAL_EDGE);
    /* The line never falls: the capture ends with it high. */
    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x01, &shape);
    cap.n_edges--;
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_FINAL_EDGE);
}

TEST_CASE(a_reply_of_10_cells_or_12_is_the_wrong_length)
{
    sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_capture_t cap;
    kst_reply_t r;

    for (unsigned cells = 1; cells < 11u; ++cells) {
        shape.cells = cells;
        CHECK_EQ(decode_read(0x00, &shape, &r), KST_RX_LENGTH);
        CHECK_EQ(decode_read(0xFF, &shape, &r), KST_RX_LENGTH);
    }
    /* A twelfth cell after a whole reply. */
    shape.cells = 11;
    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x00, &shape);
    {
        const uint32_t end = cap.edge_ns[cap.n_edges - 1u];

        sim_cap_edge(&cap, end + SIM_HALF_NS);
        sim_cap_edge(&cap, end + 2u * SIM_HALF_NS);
    }
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_LENGTH);
}

TEST_CASE(the_line_is_quiet_for_200_us_after_the_reply)
{
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_capture_t cap;
    kst_reply_t r;
    uint32_t end;

    /* Last bit 0: the reply ends one half-cell after its last edge. */
    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x00, &shape);
    end = cap.edge_ns[cap.n_edges - 1u] + SIM_HALF_NS;
    sim_cap_edge(&cap, end + KST_RX_QUIET_AFTER_NS);
    sim_cap_edge(&cap, end + KST_RX_QUIET_AFTER_NS + 30000u);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_OK);

    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x00, &shape);
    sim_cap_edge(&cap, end + KST_RX_QUIET_AFTER_NS - 1u);
    sim_cap_edge(&cap, end + KST_RX_QUIET_AFTER_NS + 30000u);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_LENGTH);

    /* Last bit 1: the quiet time runs from the final edge. */
    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x01, &shape);
    end = cap.edge_ns[cap.n_edges - 1u];
    sim_cap_edge(&cap, end + KST_RX_QUIET_AFTER_NS);
    sim_cap_edge(&cap, end + KST_RX_QUIET_AFTER_NS + 30000u);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_OK);

    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x01, &shape);
    sim_cap_edge(&cap, end + KST_RX_QUIET_AFTER_NS - 1u);
    sim_cap_edge(&cap, end + KST_RX_QUIET_AFTER_NS + 30000u);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_LENGTH);
}

TEST_CASE(a_capture_that_ends_inside_the_quiet_time_proves_nothing)
{
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_capture_t cap;
    kst_reply_t r;
    uint32_t end;

    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x00, &shape);
    end = cap.edge_ns[cap.n_edges - 1u] + SIM_HALF_NS;
    cap.window_ns = end + KST_RX_QUIET_AFTER_NS;
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_OK);
    cap.window_ns = end + KST_RX_QUIET_AFTER_NS - 1u;
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_WINDOW);
}

TEST_CASE(a_cell_without_opposite_halves_is_refused)
{
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_capture_t cap;
    kst_reply_t r;

    /* Value 0x00 after flags 11: the edge between cell 2 and cell 3 and
     * every mid-cell edge after it.  Take one mid-cell edge away and the
     * boundary edge after it: the cell has one level throughout. */
    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x00, &shape);
    {
        const unsigned drop = 6;  /* the mid-cell edge of cell 5 */

        memmove(&cap.edge_ns[drop], &cap.edge_ns[drop + 2u],
                (cap.n_edges - drop - 2u) * sizeof(cap.edge_ns[0]));
        cap.n_edges = (uint8_t)(cap.n_edges - 2u);
    }
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_CELL);

    /* The start cell: a pulse of 6 us is no half-cell of high. */
    sim_cap_clear(&cap, KST_READ_WINDOW_NS);
    sim_cap_edge(&cap, SIM_READ_DELAY_NS);
    sim_cap_edge(&cap, SIM_READ_DELAY_NS + 6000u);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_CELL);

    /* A start cell and nothing after it is a reply of 1 cell. */
    sim_cap_clear(&cap, KST_READ_WINDOW_NS);
    sim_cap_edge(&cap, SIM_READ_DELAY_NS);
    sim_cap_edge(&cap, SIM_READ_DELAY_NS + SIM_HALF_NS);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_LENGTH);
}

TEST_CASE(an_edge_more_than_8_us_from_its_place_is_not_a_mid_cell_edge)
{
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_capture_t cap;
    kst_reply_t r;

    /* Value 0xFF: every edge after the start is a mid-cell edge or a
     * boundary.  Edge 2 is the mid-cell edge of cell 1. */
    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0xFF, &shape);
    cap.edge_ns[2] += KST_RX_SEARCH_NS - 1u;
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_OK);
    CHECK_EQ(r.value, 0xFF);

    /* The tracker re-centres: the cells after a shifted edge are found from
     * that edge, so a steady drift of the rate is followed. */
    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0xFF, &shape);
    for (unsigned i = 2; i < cap.n_edges; ++i) {
        cap.edge_ns[i] += 5000u;
    }
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_OK);

    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0xFF, &shape);
    cap.edge_ns[2] += 9000u;
    CHECK(kst_reply_decode(&cap, KST_EXPECT_READ, &r) != KST_RX_OK);
}

TEST_CASE(a_line_that_starts_high_is_not_a_reply)
{
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_capture_t cap;
    kst_reply_t r;

    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x12, &shape);
    cap.level0 = 1;
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_START_LEVEL);
    sim_cap_clear(&cap, KST_READ_WINDOW_NS);
    cap.level0 = 1;
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_START_LEVEL);
}

TEST_CASE(a_capture_that_lost_edges_or_is_not_one_is_refused)
{
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_capture_t cap;
    kst_reply_t r;

    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x12, &shape);
    cap.overflow = 1;
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_OVERFLOW);

    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x12, &shape);
    cap.n_edges = KST_CAP_MAX_EDGES + 1u;
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_CAPTURE);

    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x12, &shape);
    cap.edge_ns[3] = cap.edge_ns[2] - 1u;
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_CAPTURE);

    CHECK_EQ(kst_reply_decode(NULL, KST_EXPECT_READ, &r), KST_RX_CAPTURE);
    CHECK_EQ(r.result, KST_RX_CAPTURE);
}

TEST_CASE(a_full_capture_of_glitches_around_a_reply_still_decodes)
{
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    kst_capture_t cap;
    kst_reply_t r;
    unsigned pairs = 0;

    /* 0x55 has 20 edges; glitch pairs fill the buffer to its 48. */
    sim_reply(&cap, KST_READ_WINDOW_NS, 3, 0x55, &shape);
    while (cap.n_edges + 2u <= KST_CAP_MAX_EDGES) {
        const uint32_t at = 60000u + pairs * 7000u;

        sim_cap_edge(&cap, at);
        sim_cap_edge(&cap, at + 900u);
        pairs++;
    }
    CHECK_EQ(cap.n_edges, KST_CAP_MAX_EDGES);
    CHECK_EQ(cap.overflow, 0);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_OK);
    CHECK_EQ(r.value, 0x55);
    CHECK_EQ(r.glitches, pairs);
    /* One edge more does not fit, and the capture says so. */
    sim_cap_edge(&cap, 1400000u);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_OVERFLOW);
}

int main(void)
{
    RUN(a_read_frame_is_start_10_zeros_address_zeros);
    RUN(a_write_frame_is_start_00_10000000_address_data);
    RUN(every_register_and_value_gives_a_frame_the_servo_side_reads_back);
    RUN(register_00_and_addresses_above_1f_are_never_written);
    RUN(a_read_above_1f_is_refused);
    RUN(the_sync_burst_is_64_pulses_of_one_half_cell);
    RUN(timing_constants_are_the_measured_ones);
    RUN(every_value_decodes_from_a_reply_as_the_servo_sends_it);
    RUN(a_reply_after_a_write_is_taken_in_its_own_window);
    RUN(the_sync_acknowledge_is_flags_10_and_value_00);
    RUN(flags_other_than_11_fail_a_read_and_a_write);
    RUN(no_edge_in_the_window_is_no_reply);
    RUN(a_reply_too_early_is_refused_at_the_window_edge);
    RUN(a_reply_too_late_is_refused_at_the_window_edge);
    RUN(the_bit_rate_is_held_to_5_percent_at_both_bounds);
    RUN(a_glitch_inside_a_cell_is_removed_below_1500_ns_only);
    RUN(a_glitch_before_the_reply_does_not_start_it);
    RUN(chatter_on_an_edge_ends_on_its_last_edge);
    RUN(a_stretched_last_half_cell_is_not_a_bit);
    RUN(the_final_edge_has_100_us_after_the_nominal_end);
    RUN(a_reply_of_10_cells_or_12_is_the_wrong_length);
    RUN(the_line_is_quiet_for_200_us_after_the_reply);
    RUN(a_capture_that_ends_inside_the_quiet_time_proves_nothing);
    RUN(a_cell_without_opposite_halves_is_refused);
    RUN(an_edge_more_than_8_us_from_its_place_is_not_a_mid_cell_edge);
    RUN(a_line_that_starts_high_is_not_a_reply);
    RUN(a_capture_that_lost_edges_or_is_not_one_is_refused);
    RUN(a_full_capture_of_glitches_around_a_reply_still_decodes);
    return test_summary("kst_wire");
}
