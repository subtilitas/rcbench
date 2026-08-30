/*
 * The servo page's rules: what is clamped, what is refused, and why those are
 * different answers to different mistakes.
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "link_servo.h"

static uint16_t page[LINK_SV_COUNT];

static void fresh(void)
{
    link_servo_defaults(page);
}

static uint8_t put(uint8_t reg, uint16_t v, bool may_drive)
{
    return link_servo_write(page, reg, 1, &v, may_drive);
}

/*
 * A pulse outside the range is clamped, not refused.
 *
 * Pulses arrive many times a second from a host that may be mid-drag.
 * Refusing one gives a servo that stops following; clamping gives a servo
 * that stops at its stop, which is what a stop is for.
 */
TEST_CASE(a_pulse_past_the_limit_is_clamped)
{
    fresh();
    CHECK_EQ(put(LINK_SV_PULSE_US, 2400, true), 0);
    CHECK_EQ(page[LINK_SV_PULSE_US], LINK_SV_DEFAULT_MAX);

    CHECK_EQ(put(LINK_SV_PULSE_US, 600, true), 0);
    CHECK_EQ(page[LINK_SV_PULSE_US], LINK_SV_DEFAULT_MIN);

    /* And one inside it is left exactly alone. */
    CHECK_EQ(put(LINK_SV_PULSE_US, 1620, true), 0);
    CHECK_EQ(page[LINK_SV_PULSE_US], 1620);
}

/*
 * A limit outside what a servo can take is refused, not clamped.  It is a
 * configuration mistake, and quietly accepting a clamped version would leave
 * the range looking set when it was not.
 */
TEST_CASE(an_impossible_limit_is_refused)
{
    fresh();
    CHECK_EQ(put(LINK_SV_MIN_US, 100, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_SV_MAX_US, 9000, true), LINK_NACK_BAD_VALUE);
    /* And the page is untouched by a refused write. */
    CHECK_EQ(page[LINK_SV_MIN_US], LINK_SV_DEFAULT_MIN);
    CHECK_EQ(page[LINK_SV_MAX_US], LINK_SV_DEFAULT_MAX);
}

/* A refused write leaves nothing behind, even when part of it was valid --
 * half-applying one would move a servo somewhere nobody asked for and report
 * a refusal at the same time. */
TEST_CASE(a_refused_write_changes_nothing)
{
    fresh();
    const uint16_t before = page[LINK_SV_PULSE_US];
    /* Enable and pulse are fine; the limit that follows is not. */
    const uint16_t in[3] = { 1u, 1700u, 100u };
    CHECK_EQ(link_servo_write(page, LINK_SV_ENABLE, 3, in, true),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(page[LINK_SV_ENABLE], 0);
    CHECK_EQ(page[LINK_SV_PULSE_US], before);
}

/*
 * Driving an output is arming something, and the end holding the wire decides
 * that.  A host asking to drive while the far end is in failsafe is refused,
 * however politely it asks.
 */
TEST_CASE(the_holder_of_the_wire_decides_whether_to_drive)
{
    fresh();
    CHECK_EQ(put(LINK_SV_ENABLE, 1, false), LINK_NACK_NOT_ARMED);
    CHECK_EQ(page[LINK_SV_ENABLE], 0);

    CHECK_EQ(put(LINK_SV_ENABLE, 1, true), 0);
    CHECK_EQ(page[LINK_SV_ENABLE], 1);

    /* Releasing is always allowed: stopping never needs permission. */
    CHECK_EQ(put(LINK_SV_ENABLE, 0, false), 0);
    CHECK_EQ(page[LINK_SV_ENABLE], 0);
}

/* An inverted pair is a mistake rather than a range, and it would make the
 * clamp unsatisfiable -- so it is straightened rather than obeyed. */
TEST_CASE(an_inverted_range_is_straightened)
{
    fresh();
    const uint16_t in[2] = { 1900u, 1100u };   /* min above max */
    CHECK_EQ(link_servo_write(page, LINK_SV_MIN_US, 2, in, true), 0);
    CHECK_EQ(page[LINK_SV_MIN_US], 1100);
    CHECK_EQ(page[LINK_SV_MAX_US], 1900);
    CHECK(page[LINK_SV_PULSE_US] >= 1100
          && page[LINK_SV_PULSE_US] <= 1900);
}

/* A narrowed range pulls a pulse that is now outside it back in, without
 * anybody sending a new pulse. */
TEST_CASE(narrowing_the_range_moves_the_pulse)
{
    fresh();
    CHECK_EQ(put(LINK_SV_PULSE_US, 1950, true), 0);
    CHECK_EQ(page[LINK_SV_PULSE_US], 1950);

    CHECK_EQ(put(LINK_SV_MAX_US, 1700, true), 0);
    CHECK_EQ(page[LINK_SV_PULSE_US], 1700);
}

/* Off the end of the page is a range error, not a silent write past it. */
TEST_CASE(writing_off_the_end_is_refused)
{
    fresh();
    const uint16_t in[4] = { 0, 0, 0, 0 };
    CHECK_EQ(link_servo_write(page, LINK_SV_SLEW_US, 4, in, true),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(link_servo_write(NULL, 0, 1, in, true), LINK_NACK_BAD_RANGE);
}

int main(void)
{
    RUN(a_pulse_past_the_limit_is_clamped);
    RUN(an_impossible_limit_is_refused);
    RUN(a_refused_write_changes_nothing);
    RUN(the_holder_of_the_wire_decides_whether_to_drive);
    RUN(an_inverted_range_is_straightened);
    RUN(narrowing_the_range_moves_the_pulse);
    RUN(writing_off_the_end_is_refused);
    return test_summary("link_servo");
}
