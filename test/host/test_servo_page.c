/*
 * The SERVO link page at the coprocessor: the frame rate and the sweep.
 *
 * Under test: a sweep starts only on an armed bench and only whole; it keeps
 * its curve while the panel repeats it and starts over when the curve
 * changes; it stops when the panel goes quiet, on a disarm and after its
 * movements, and a finished sweep is not started again by a repeat; it
 * drives the surfaces and nothing else.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "servo_page.h"

static outputs_t    o;
static servo_page_t pg;

#define T0 1000u

/* Channel 0 a surface, channel 1 a throttle, the bench armed at T0. */
static void fresh(bool armed)
{
    outputs_init(&o, T0);
    CHECK(outputs_set_role(&o, 0, OUT_ROLE_SURFACE));
    CHECK(outputs_set_role(&o, 1, OUT_ROLE_THROTTLE));
    outputs_arm(&o, armed, T0);
    servo_page_init(&pg);
}

/* A sweep's four registers, written as the panel writes them: one frame. */
static uint8_t sweep(uint16_t kind, uint16_t mhz, uint16_t span,
                     uint16_t dwell, uint32_t now)
{
    const uint16_t r[4] = { kind, mhz, span, dwell };
    return servo_page_write(&pg, LINK_SV_SWEEP, 4u, r, &o, now);
}

static uint16_t reg(unsigned i)
{
    uint16_t v = 0xFFFFu;
    servo_page_read(&pg, (uint8_t)i, 1u, &v);
    return v;
}

TEST_CASE(a_sweep_needs_the_bench_armed)
{
    fresh(false);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), LINK_NACK_NOT_ARMED);
    CHECK_EQ(reg(LINK_SV_SWEEP), 0u);
    CHECK_EQ(reg(LINK_SV_SWEEP_MHZ), 0u);
    CHECK(!servo_page_step(&pg, &o, T0 + 10u));
    /* Stopping needs nothing armed. */
    CHECK_EQ(sweep(0u, 1000u, 400u, 0u, T0), 0u);
}

TEST_CASE(a_sweep_out_of_range_is_refused_whole)
{
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 49u, 400u, 0u, T0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(sweep(SWEEP_KIND_COUNT, 1000u, 400u, 0u, T0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 501u, 0u, T0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 5001u, T0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_SV_SWEEP_MHZ), 0u);
    CHECK_EQ(reg(LINK_SV_SWEEP_SPAN), 0u);
    const uint16_t one = 1u;
    CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP_DONE, 1u, &one, &o, T0),
             LINK_NACK_READ_ONLY);
    const uint16_t two[2] = { 0u, 0u };
    CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP_DONE, 2u, two, &o, T0),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(servo_page_write(NULL, 0u, 1u, &one, &o, T0),
             LINK_NACK_BAD_RANGE);
    /* A frame rate the PWM driver cannot make stores nothing either. */
    const uint16_t fast[2] = { 600u, SWEEP_SINE };
    CHECK_EQ(servo_page_write(&pg, LINK_SV_FRAME_HZ, 2u, fast, &o, T0),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(servo_page_hz(&pg), 0u);
    const uint16_t hz = 333u;
    CHECK_EQ(servo_page_write(&pg, LINK_SV_FRAME_HZ, 1u, &hz, &o, T0), 0u);
    CHECK_EQ(servo_page_hz(&pg), 333u);
    CHECK(!servo_page_step(&pg, &o, T0));
}

/* The surfaces follow the curve and the throttle does not move. */
TEST_CASE(a_sweep_drives_the_surfaces_and_nothing_else)
{
    fresh(true);
    CHECK(outputs_set(&o, 1, 0u, T0));
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    CHECK_EQ(reg(LINK_SV_SWEEP), SWEEP_SINE);
    CHECK(servo_page_step(&pg, &o, T0 + 250u));
    CHECK_EQ(o.channel[0].command, 900u);
    CHECK_EQ(o.channel[1].command, 0u);
    CHECK_EQ(reg(LINK_SV_SWEEP_DONE), 1u);
}

/* The panel repeats the sweep to keep it going; the curve carries on.  A
 * changed curve starts from its beginning. */
TEST_CASE(a_repeated_sweep_keeps_its_curve_and_a_changed_one_starts_over)
{
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0 + 200u), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 250u));
    CHECK_EQ(o.channel[0].command, 900u);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 300u, 0u, T0 + 300u), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 300u));
    CHECK_EQ(o.channel[0].command, 500u);
    CHECK(servo_page_step(&pg, &o, T0 + 550u));
    CHECK_EQ(o.channel[0].command, 800u);
}

/* A panel that goes quiet for longer than a channel command is trusted
 * leaves no servo moving; so does a disarm, and arming again does not bring
 * the sweep back. */
TEST_CASE(a_sweep_stops_when_nobody_writes_it_and_on_a_disarm)
{
    fresh(true);
    CHECK_EQ(sweep(SWEEP_TRIANGLE, 1000u, 400u, 0u, T0), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + OUT_DEFAULT_TIMEOUT_MS));
    CHECK(!servo_page_step(&pg, &o, T0 + OUT_DEFAULT_TIMEOUT_MS + 1u));
    CHECK_EQ(reg(LINK_SV_SWEEP), 0u);

    CHECK_EQ(sweep(SWEEP_TRIANGLE, 1000u, 400u, 0u, T0 + 2000u), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 2010u));
    outputs_arm(&o, false, T0 + 2020u);
    CHECK(!servo_page_step(&pg, &o, T0 + 2020u));
    outputs_arm(&o, true, T0 + 2030u);
    CHECK(!servo_page_step(&pg, &o, T0 + 2030u));
    CHECK_EQ(reg(LINK_SV_SWEEP), 0u);
}

/* After its movements a sweep ends at the centre and stays ended while the
 * same curve is repeated; a stop, or another curve, starts afresh. */
TEST_CASE(a_finished_sweep_is_not_started_again_by_a_repeat)
{
    fresh(true);
    const uint16_t two = 2u;
    CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP_MOVES, 1u, &two, &o, T0), 0u);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    uint32_t t = T0;
    while (servo_page_step(&pg, &o, t) && t < T0 + 5000u) {
        if ((t - T0) % 100u == 0u) {
            CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, t), 0u);
        }
        ++t;
    }
    CHECK_EQ(t, T0 + 750u);
    CHECK_EQ(o.channel[0].command, SWEEP_CENTRE);
    CHECK_EQ(reg(LINK_SV_SWEEP_DONE), 2u);
    CHECK_EQ(reg(LINK_SV_SWEEP), 0u);

    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, t + 50u), 0u);
    CHECK(!servo_page_step(&pg, &o, t + 60u));
    CHECK_EQ(reg(LINK_SV_SWEEP), 0u);

    CHECK_EQ(sweep(0u, 1000u, 400u, 0u, t + 100u), 0u);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, t + 110u), 0u);
    CHECK(servo_page_step(&pg, &o, t + 120u));
    CHECK_EQ(reg(LINK_SV_SWEEP), SWEEP_SINE);
}

int main(void)
{
    RUN(a_sweep_needs_the_bench_armed);
    RUN(a_sweep_out_of_range_is_refused_whole);
    RUN(a_sweep_drives_the_surfaces_and_nothing_else);
    RUN(a_repeated_sweep_keeps_its_curve_and_a_changed_one_starts_over);
    RUN(a_sweep_stops_when_nobody_writes_it_and_on_a_disarm);
    RUN(a_finished_sweep_is_not_started_again_by_a_repeat);
    return test_summary("servo_page");
}
