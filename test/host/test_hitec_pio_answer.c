/*
 * The end of a LISTEN's answer on the Hitec line's PIO program
 * (protocols/hitec/rp2350/hitec_line.pio), run on the host by the model of
 * hitec_pio_model.h: the answer is 10 bit times, and the receiver waits for
 * the next byte from the end of the answer's stop bit, not from the end of
 * the 16 bit times of the answer word.
 *
 * Under test, for classic open drain on a line the servo holds high and the
 * D series push-pull on a line that idles low:
 *  - the receiver waits again 1 state machine cycle after the answer's stop
 *    bit ends, 161 cycles after the answer's start bit; the 16-bit word would
 *    give 257;
 *  - a start bit 1 cycle (1/16 bit) after the stop bit's end is taken, and so
 *    is one at every bit time up to 6 bits after it, the 6 bit times the
 *    program drops from the word;
 *  - a start bit right at the stop bit's end is not taken at its time: the
 *    receiver's first read sees it low and waits for the idle level.
 *
 * Not tested here: the chip (test_hitec_pio.c names what the model leaves to
 * it).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "hitec_pio.h"
#include "hitec_wire.h"

#include "hitec_pio_model.h"

typedef struct {
    uint8_t family;
    uint8_t drive;
    bool idle;          /* the wire's level while the servo sends nothing */
    uint8_t answer;
} line_case_t;

static const line_case_t cases[] = {
    { HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN, true, HITEC_C_ACK },
    { HITEC_D, HITEC_DRIVE_PUSH_PULL, false, 0x96 },
};

/* The answer on the wire: start bit, 8 data bits, stop bit. */
#define ANSWER_BITS 10u
/* The bit times the program drops from the front of the 16-bit word. */
#define DROP_BITS 6u
#define FIRST_NS 1000000.0   /* the servo's first byte after the release */
#define BYTE_1   0x2B
#define BYTE_2   0xC3

/* A LISTEN for 2 bytes that answers the first, the servo's first byte at
 * FIRST_NS and, for @p second_ns > 0, its second byte that many ns after
 * the release.  Returns the cycle of the answer's start bit; 0 for none. */
static uint64_t listen(const line_case_t *c, double second_ns)
{
    hitec_xfer_t x;

    CHECK(hitec_listen_xfer(c->family, 20000u, 2, true, c->answer, &x));
    memset(&m, 0, sizeof(m));
    ext_clear(c->idle);
    ext_byte(c->family, FIRST_NS, bit_ns(c->family), BYTE_1, true);
    if (second_ns > 0.0) {
        ext_byte(c->family, second_ns, bit_ns(c->family), BYTE_2, true);
    }
    CHECK(transact(150000000u, c->drive, &x, 2, 0));
    CHECK_EQ(m.answers, 1u);
    return next_dir(m.release, true);
}

/* The ns from the release to cycle @p cyc. */
static double since_release(uint64_t cyc)
{
    return cyc_ns(cyc) - cyc_ns(m.release);
}

TEST_CASE(the_receiver_waits_again_one_cycle_after_the_answer_stop_bit)
{
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const uint64_t edge = listen(&cases[i], 0.0);
        const uint64_t stop_end = edge + ANSWER_BITS * 16u;

        CHECK(edge > 0u);
        CHECK_EQ(m.resume, stop_end + 1u);
        CHECK(m.resume < edge + 16u * 16u);
        /* Open drain: the answer's changes on its 16-cycle grid, never
         * driven high.  Both: the pad released at the end of the stop bit,
         * for good. */
        if (cases[i].drive == HITEC_DRIVE_OPEN_DRAIN) {
            check_grid(edge, edge, stop_end);
            CHECK(!pad_ever_high(0));
        }
        CHECK_EQ(pad_at(&m, stop_end), -1);
        CHECK_EQ(next_dir(stop_end, true), 0u);
        CHECK_EQ(HITEC_PIO_ANSWER_BITS, ANSWER_BITS);
    }
}

TEST_CASE(a_start_bit_from_one_cycle_to_6_bits_after_the_answer_is_taken)
{
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const line_case_t *c = &cases[i];
        const uint64_t edge = listen(c, 0.0);
        const uint64_t stop_end = edge + ANSWER_BITS * 16u;
        /* 1 cycle and the input synchroniser's 2 system clock cycles, then
         * every bit time to the end of the 16-bit word. */
        const double sync = 2e9 / 150e6 + 1.0;
        const double first = since_release(stop_end + 1u) + sync;

        for (unsigned b = 0; b <= DROP_BITS; ++b) {
            const double at = first + b * bit_ns(c->family)
                              - (b == DROP_BITS
                                     ? cyc_ns(2) : 0.0);
            hitec_capture_t cap;

            CHECK_EQ(listen(c, at), edge);
            CHECK_EQ(m.n_rx, 2u);
            captured(&cap, true);
            CHECK_EQ(cap.n, 2);
            CHECK_EQ(cap.overflow, 0);
            CHECK_EQ(cap.byte[0].value, BYTE_1);
            CHECK_EQ(cap.byte[1].value, BYTE_2);
            CHECK_EQ(cap.byte[1].framing, 0);
            check_at(cap.byte[1].at_ns, at);
        }
    }
}

TEST_CASE(a_start_bit_right_at_the_answer_stop_bit_end_is_not_taken_at_its_time)
{
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const line_case_t *c = &cases[i];
        const uint64_t edge = listen(c, 0.0);
        const double at = since_release(edge + ANSWER_BITS * 16u);
        hitec_capture_t cap;
        const double slack = cyc_ns(1) + 2e9 / 150e6 + 2.0;

        CHECK_EQ(listen(c, at), edge);
        captured(&cap, true);
        CHECK(cap.n >= 1);
        CHECK_EQ(cap.byte[0].value, BYTE_1);
        /* No second byte stamped at the start bit's time. */
        for (unsigned k = 1; k < cap.n; ++k) {
            CHECK(cap.byte[k].at_ns > at + slack);
        }
    }
}

int main(void)
{
    if (!assemble(HITEC_LINE_PIO)) {
        printf("FAIL  cannot assemble %s\n", HITEC_LINE_PIO);
        return 1;
    }
    RUN(the_receiver_waits_again_one_cycle_after_the_answer_stop_bit);
    RUN(a_start_bit_from_one_cycle_to_6_bits_after_the_answer_is_taken);
    RUN(a_start_bit_right_at_the_answer_stop_bit_end_is_not_taken_at_its_time);
    return test_summary("hitec_pio_answer");
}
