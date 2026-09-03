/*
 * The bidirectional DShot burst: the line code, the group code, the checksum
 * and the sampler that turns a capture into bits.
 *
 * The failure this is really about is a plausible wrong number.  An eRPM
 * frame decoded one bit out of phase is still four quintets and still a
 * number; if it passes the checksum it becomes an rpm reading on a screen
 * with nothing to mark it as wrong.  So the tests hold the checksum against
 * every single-bit error, and hold the sampler against a clock that does not
 * match this end's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "dshot.h"

/* --------------------------------------------------- a captured line, made */

#define CAP_WORDS 16u

typedef struct {
    uint32_t word[CAP_WORDS];
    size_t   n;             /* samples pushed so far */
} capture_t;

static void push(capture_t *c, bool level, unsigned count)
{
    for (unsigned i = 0; i < count && c->n < CAP_WORDS * 32u; ++i, ++c->n) {
        if (level) {
            c->word[c->n / 32u] |= 1u << (31u - (c->n % 32u));
        }
    }
}

/*
 * Lay the 21 line bits down at @p num/@p den samples per bit, so a test can
 * ask what happens when the ESC's clock is a few percent from this end's.
 * The accumulator is what makes the error accumulate the way a real one does
 * rather than being spread evenly.
 */
static void lay(capture_t *c, uint32_t line, unsigned num, unsigned den)
{
    memset(c, 0, sizeof(*c));
    push(c, true, 8u);                       /* the idle before the burst */
    unsigned acc = 0u;
    for (int b = 20; b >= 0; --b) {
        acc += num;
        const unsigned n = acc / den;
        acc -= n * den;
        push(c, ((line >> b) & 1u) != 0u, n);
    }
    push(c, true, 8u);                       /* idle again afterwards */
}

/* ------------------------------------------------------------- the codes */

TEST_CASE(every_value_survives_the_group_and_line_codes)
{
    for (uint32_t v = 0; v <= 0xFFFFu; ++v) {
        const uint32_t line = dshot_gcr_encode((uint16_t)v);
        uint16_t back = 0u;
        if (!dshot_gcr_decode(line, &back)) {
            T_FAIL("value 0x%04X encodes to a line that will not decode",
                   (unsigned)v);
        }
        if (back != (uint16_t)v) {
            T_FAIL("value 0x%04X came back as 0x%04X", (unsigned)v,
                   (unsigned)back);
        }
    }
}

TEST_CASE(the_burst_starts_with_a_falling_edge)
{
    /* The line idles high and the response has to be findable.  If the top
     * bit were ever 1 there would be no edge to find it by. */
    for (uint32_t v = 0; v <= 0xFFFFu; v = v + 1021u) {
        const uint32_t line = dshot_gcr_encode((uint16_t)v);
        if ((line & (1u << 20)) != 0u) {
            T_FAIL("value 0x%04X starts the burst high", (unsigned)v);
        }
    }
}

TEST_CASE(a_quintet_outside_the_table_is_refused)
{
    /* 0x00 is not a code word: five zeros in a row is exactly what the code
     * exists to prevent. */
    uint16_t out = 0u;
    /* Build a line whose lowest quintet decodes to 0x00. */
    const uint32_t line = 0u;      /* decodes to gcr 0, quintet 0 four times */
    CHECK_EQ(dshot_gcr_decode(line, &out), false);
    CHECK_EQ(dshot_gcr_decode(dshot_gcr_encode(0x1234u), NULL), false);
}

/* ------------------------------------------------------------ the reading */

TEST_CASE(an_erpm_frame_decodes_to_the_period_it_carried)
{
    /* 500 us per electrical revolution: mantissa 500, exponent 0. */
    const uint16_t value = dshot_telem_value(DSHOT_TELEM_ERPM, 500u);
    dshot_telem_t t;
    CHECK(dshot_telem_from_value(value, false, &t));
    CHECK_EQ(t.kind, DSHOT_TELEM_ERPM);
    CHECK_EQ(t.period_us, 500u);
    CHECK_EQ(t.erpm, 120000u);
    CHECK_EQ(dshot_rpm(t.erpm, 7u), 17142u);
    CHECK_EQ(dshot_rpm(t.erpm, 0u), 0u);
}

TEST_CASE(the_exponent_shifts_the_mantissa)
{
    /* Mantissa 300, exponent 4: a period of 4800 us. */
    const uint16_t payload = (uint16_t)((4u << 9) | 300u);
    dshot_telem_t t;
    CHECK(dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_ERPM, payload),
                                 false, &t));
    CHECK_EQ(t.period_us, 4800u);
    CHECK_EQ(t.erpm, 12500u);
}

TEST_CASE(a_motor_that_is_not_turning_reports_no_speed_rather_than_a_slow_one)
{
    dshot_telem_t t;
    CHECK(dshot_telem_from_value(dshot_telem_value(DSHOT_TELEM_ERPM, 0x0FFFu),
                                 false, &t));
    CHECK_EQ(t.kind, DSHOT_TELEM_ERPM);
    CHECK_EQ(t.erpm, 0u);
}

TEST_CASE(extended_frames_are_read_only_when_extended_telemetry_is_on)
{
    const uint16_t value = dshot_telem_value(DSHOT_TELEM_VOLTAGE, 66u);
    dshot_telem_t t;

    CHECK(dshot_telem_from_value(value, true, &t));
    CHECK_EQ(t.kind, DSHOT_TELEM_VOLTAGE);
    CHECK_EQ(t.value, 66u);

    /*
     * The same bits from an ESC that was never asked for extended telemetry
     * are an eRPM frame -- mantissa 66, exponent 2, a period of 264 us --
     * and reading them as a voltage puts 16.5 V on the screen for a motor
     * turning at 227272 electrical rpm.
     */
    CHECK(dshot_telem_from_value(value, false, &t));
    CHECK_EQ(t.kind, DSHOT_TELEM_ERPM);
}

TEST_CASE(each_extended_type_comes_back_as_itself)
{
    static const dshot_telem_kind_t kinds[] = {
        DSHOT_TELEM_TEMPERATURE, DSHOT_TELEM_VOLTAGE, DSHOT_TELEM_CURRENT,
        DSHOT_TELEM_DEBUG1, DSHOT_TELEM_DEBUG2, DSHOT_TELEM_STRESS,
        DSHOT_TELEM_STATUS,
    };
    for (unsigned i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        dshot_telem_t t;
        const uint16_t v = dshot_telem_value(kinds[i], (uint16_t)(i + 3u));
        CHECK(dshot_telem_from_value(v, true, &t));
        CHECK_EQ(t.kind, kinds[i]);
        CHECK_EQ(t.value, (uint8_t)(i + 3u));
    }
}

TEST_CASE(the_checksum_catches_every_single_bit_error)
{
    const uint16_t good = dshot_telem_value(DSHOT_TELEM_ERPM, 0x0345u);
    dshot_telem_t t;
    CHECK(dshot_telem_from_value(good, false, &t));

    for (unsigned b = 0; b < 16u; ++b) {
        const uint16_t bad = (uint16_t)(good ^ (uint16_t)(1u << b));
        if (dshot_telem_from_value(bad, false, &t)) {
            T_FAIL("bit %u flipped and the frame was still accepted", b);
        }
    }
}

/* --------------------------------------------------------- the sampler */

TEST_CASE(a_capture_at_the_nominal_rate_gives_back_the_bits)
{
    const uint32_t line = dshot_gcr_encode(
        dshot_telem_value(DSHOT_TELEM_ERPM, 500u));
    capture_t cap;
    lay(&cap, line, 5u, 1u);

    uint32_t back = 0u;
    CHECK(dshot_rx_bits(cap.word, CAP_WORDS, 5u, &back));
    CHECK_EQ(back, line);
}

TEST_CASE(the_sampler_holds_against_an_esc_clock_that_is_not_this_ones)
{
    /*
     * Five percent fast and five percent slow.  Twenty-one bits of five
     * percent is a whole bit of accumulated error, which is what a sampler
     * that divides a fixed count instead of resynchronising gets wrong.
     */
    const uint32_t line = dshot_gcr_encode(
        dshot_telem_value(DSHOT_TELEM_ERPM, 0x0123u));
    static const unsigned num[] = { 100u, 105u, 95u };

    for (unsigned i = 0; i < 3u; ++i) {
        capture_t cap;
        lay(&cap, line, num[i], 20u);          /* 5 samples a bit, scaled */
        uint32_t back = 0u;
        if (!dshot_rx_bits(cap.word, CAP_WORDS, 5u, &back) || back != line) {
            T_FAIL("at %u/20 samples a bit the burst came back as 0x%08lX, "
                   "not 0x%08lX", num[i], (unsigned long)back,
                   (unsigned long)line);
        }
    }
}

TEST_CASE(a_capture_with_no_burst_in_it_is_refused)
{
    capture_t cap;
    memset(&cap, 0, sizeof(cap));
    uint32_t back = 0u;

    push(&cap, true, CAP_WORDS * 32u);              /* idle all the way */
    CHECK_EQ(dshot_rx_bits(cap.word, CAP_WORDS, 5u, &back), false);

    /* A burst that runs off the end of the capture is not 21 bits, and half
     * a burst decodes to a number as readily as a whole one. */
    const uint32_t line = dshot_gcr_encode(0x2222u);
    lay(&cap, line, 5u, 1u);
    CHECK_EQ(dshot_rx_bits(cap.word, 2u, 5u, &back), false);
}

TEST_CASE(null_and_nonsense_arguments_are_refused)
{
    uint32_t back = 0u;
    capture_t cap;
    lay(&cap, dshot_gcr_encode(0x1111u), 5u, 1u);
    CHECK_EQ(dshot_rx_bits(NULL, CAP_WORDS, 5u, &back), false);
    CHECK_EQ(dshot_rx_bits(cap.word, CAP_WORDS, 5u, NULL), false);
    CHECK_EQ(dshot_rx_bits(cap.word, 0u, 5u, &back), false);
    CHECK_EQ(dshot_rx_bits(cap.word, CAP_WORDS, 1u, &back), false);
    CHECK_EQ(dshot_telem_from_value(0u, false, NULL), false);
    CHECK_EQ(dshot_telem_decode(0u, false, NULL), false);
}

TEST_CASE(a_burst_goes_from_the_wire_to_a_speed_in_one_call)
{
    /* Mantissa 500, exponent 1: a period of 1000 us. */
    const uint16_t value =
        dshot_telem_value(DSHOT_TELEM_ERPM, (uint16_t)((1u << 9) | 500u));
    capture_t cap;
    lay(&cap, dshot_gcr_encode(value), 5u, 1u);

    uint32_t line = 0u;
    dshot_telem_t t;
    CHECK(dshot_rx_bits(cap.word, CAP_WORDS, 5u, &line));
    CHECK(dshot_telem_decode(line, false, &t));
    CHECK_EQ(t.period_us, 1000u);
    CHECK_EQ(dshot_rpm(t.erpm, 6u), 10000u);
}

int main(void)
{
    RUN(every_value_survives_the_group_and_line_codes);
    RUN(the_burst_starts_with_a_falling_edge);
    RUN(a_quintet_outside_the_table_is_refused);
    RUN(an_erpm_frame_decodes_to_the_period_it_carried);
    RUN(the_exponent_shifts_the_mantissa);
    RUN(a_motor_that_is_not_turning_reports_no_speed_rather_than_a_slow_one);
    RUN(extended_frames_are_read_only_when_extended_telemetry_is_on);
    RUN(each_extended_type_comes_back_as_itself);
    RUN(the_checksum_catches_every_single_bit_error);
    RUN(a_capture_at_the_nominal_rate_gives_back_the_bits);
    RUN(the_sampler_holds_against_an_esc_clock_that_is_not_this_ones);
    RUN(a_capture_with_no_burst_in_it_is_refused);
    RUN(null_and_nonsense_arguments_are_refused);
    RUN(a_burst_goes_from_the_wire_to_a_speed_in_one_call);
    return test_summary("dshot_telem");
}
