/*
 * The SERVO link page at the coprocessor: the frame rate and the sweep.
 *
 * Under test: a sweep starts only on an armed bench and only whole; it keeps
 * its curve while the panel repeats it and starts over when the curve
 * changes; it stops when the panel goes quiet, on a disarm and after its
 * movements, and a finished sweep is not started again by a repeat; it
 * drives the surfaces and nothing else.  A hold keeps a running sweep's
 * phase, RESUME carries it on from there, and RESUME is refused with no
 * phase kept or a curve changed since.
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
    CHECK_EQ(sweep(LINK_SV_HOLD + 1u, 1000u, 400u, 0u, T0), LINK_NACK_BAD_VALUE);
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

/* A sweep that stops for silence leaves each surface where its output has
 * got to rather than slewing on towards the curve's last command. */
TEST_CASE(a_sweep_stopped_by_silence_leaves_the_surfaces_where_they_are)
{
    fresh(true);
    CHECK(outputs_set_slew(&o, 0, 200u));      /* 200 units a second */
    CHECK_EQ(sweep(SWEEP_SQUARE, 1000u, 400u, 0u, T0), 0u);
    uint32_t t = T0;
    for (; t <= T0 + OUT_DEFAULT_TIMEOUT_MS + 1u; ++t) {
        (void)servo_page_step(&pg, &o, t);
        outputs_step(&o, t);
    }
    CHECK_EQ(reg(LINK_SV_SWEEP), 0u);
    const uint16_t held = outputs_actual(&o, 0);
    CHECK(held < 900u);                        /* still on its way */
    CHECK_EQ(o.channel[0].command, held);
    for (; t <= T0 + OUT_DEFAULT_TIMEOUT_MS + 200u; ++t) {
        outputs_step(&o, t);
    }
    CHECK_EQ(outputs_actual(&o, 0), held);
}

/* A sweep stopped by the panel holds each surface where its output has got
 * to, as silence does: the command after it is a transaction or two away. */
TEST_CASE(a_sweep_stopped_by_the_panel_leaves_the_surfaces_where_they_are)
{
    fresh(true);
    CHECK(outputs_set_slew(&o, 0, 200u));
    CHECK_EQ(sweep(SWEEP_SQUARE, 1000u, 400u, 0u, T0), 0u);
    uint32_t t = T0;
    for (; t <= T0 + 300u; ++t) {
        (void)servo_page_step(&pg, &o, t);
        outputs_step(&o, t);
    }
    CHECK_EQ(sweep(0u, 1000u, 400u, 0u, t), 0u);
    const uint16_t held = outputs_actual(&o, 0);
    CHECK(held > 500u && held < 900u);
    CHECK_EQ(o.channel[0].command, held);
    for (; t <= T0 + 400u; ++t) {
        outputs_step(&o, t);
    }
    CHECK_EQ(outputs_actual(&o, 0), held);
}

/* A sweep starts and changes only from its four registers in one write;
 * one register stops it. */
TEST_CASE(a_sweep_starts_only_from_all_four_registers)
{
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    const uint16_t span = 300u;
    CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP_SPAN, 1u, &span, &o, T0 + 10u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_SV_SWEEP_SPAN), 400u);
    const uint16_t stop = 0u;
    CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP, 1u, &stop, &o, T0 + 20u), 0u);
    CHECK(!servo_page_step(&pg, &o, T0 + 30u));
    const uint16_t sine = SWEEP_SINE;
    CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP, 1u, &sine, &o, T0 + 40u),
             LINK_NACK_BAD_VALUE);
    CHECK(!servo_page_step(&pg, &o, T0 + 50u));
    CHECK_EQ(reg(LINK_SV_SWEEP), 0u);
    /* The frame rate and the curve together are whole too. */
    const uint16_t all[5] = { 50u, SWEEP_TRIANGLE, 1000u, 400u, 0u };
    CHECK_EQ(servo_page_write(&pg, LINK_SV_FRAME_HZ, 5u, all, &o, T0 + 60u), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 70u));
}

/* A finished sweep the panel still repeats keeps its centre commanded, so
 * a slow SPEED slews the output there instead of a timeout dropping it to
 * rest part way. */
TEST_CASE(a_finished_sweep_keeps_its_centre_while_repeated)
{
    fresh(true);
    CHECK(outputs_set_slew(&o, 0, 100u));      /* 100 units a second */
    const uint16_t one = 1u;
    CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP_MOVES, 1u, &one, &o, T0), 0u);
    CHECK_EQ(sweep(SWEEP_SQUARE, 5000u, 400u, 0u, T0), 0u);
    uint32_t t = T0;
    for (; t <= T0 + 3000u; ++t) {
        if ((t - T0) % 100u == 0u) {
            CHECK_EQ(sweep(SWEEP_SQUARE, 5000u, 400u, 0u, t), 0u);
        }
        (void)servo_page_step(&pg, &o, t);
        outputs_step(&o, t);
        CHECK(!outputs_overdue(&o, 0, t));
    }
    CHECK_EQ(reg(LINK_SV_SWEEP), 0u);
    CHECK_EQ(outputs_actual(&o, 0), SWEEP_CENTRE);
}

/*
 * HOLD keeps each surface exactly where its output had got to when a sweep
 * was stopped -- with a slew, between the curve and where it started -- as
 * long as the panel repeats it, and ends on silence like a sweep.
 */
TEST_CASE(hold_keeps_the_surfaces_where_their_outputs_are)
{
    fresh(false);
    const uint16_t hold = LINK_SV_HOLD;
    CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP, 1u, &hold, &o, T0),
             LINK_NACK_NOT_ARMED);
    outputs_arm(&o, true, T0);
    CHECK(outputs_set_slew(&o, 0, 200u));
    CHECK_EQ(sweep(SWEEP_SQUARE, 1000u, 400u, 0u, T0), 0u);
    uint32_t t = T0;
    for (; t <= T0 + 300u; ++t) {
        (void)servo_page_step(&pg, &o, t);
        outputs_step(&o, t);
    }
    CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP, 1u, &hold, &o, t), 0u);
    const uint16_t held = outputs_actual(&o, 0);
    CHECK(held > 500u && held < 900u);
    CHECK_EQ(reg(LINK_SV_SWEEP), LINK_SV_HOLD);
    for (; t <= T0 + 2000u; ++t) {
        if (t % 100u == 0u) {
            CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP, 1u, &hold, &o, t), 0u);
        }
        (void)servo_page_step(&pg, &o, t);
        outputs_step(&o, t);
        CHECK(!outputs_overdue(&o, 0, t));
    }
    CHECK_EQ(outputs_actual(&o, 0), held);
    /* Unrepeated, the channel rests 500 ms after the last repeat, as it
     * would after any command, and the hold ends with it. */
    const uint32_t last = T0 + 2000u;
    for (; t < last + OUT_DEFAULT_TIMEOUT_MS; ++t) {
        (void)servo_page_step(&pg, &o, t);
        outputs_step(&o, t);
        CHECK(!outputs_overdue(&o, 0, t));
    }
    for (; t <= last + OUT_DEFAULT_TIMEOUT_MS + 10u; ++t) {
        (void)servo_page_step(&pg, &o, t);
        outputs_step(&o, t);
    }
    CHECK_EQ(reg(LINK_SV_SWEEP), 0u);
    CHECK(outputs_overdue(&o, 0, t));
}

/* The sweep register alone: 0, HOLD or RESUME. */
static uint8_t say(uint16_t v, uint32_t now)
{
    return servo_page_write(&pg, LINK_SV_SWEEP, 1u, &v, &o, now);
}

/* Hold from @p from to @p to, repeated every 100 ms as the panel does. */
static void held(uint32_t from, uint32_t to)
{
    for (uint32_t t = from; t <= to; t += 100u) {
        CHECK_EQ(say(LINK_SV_HOLD, t), 0u);
        (void)servo_page_step(&pg, &o, t);
    }
}

/*
 * RESUME carries a held sweep on from the point it was held at, however
 * long the hold: a 1 Hz sine held 100 ms in is at 735 again on the first
 * pass after it, and at its peak 150 ms later.
 */
TEST_CASE(resume_carries_a_held_sweep_on_in_phase)
{
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 100u));
    const uint16_t at_hold = o.channel[0].command;
    CHECK(at_hold > 730u && at_hold < 740u);       /* 500 + 400 sin 36 deg */
    held(T0 + 100u, T0 + 2100u);
    CHECK_EQ(reg(LINK_SV_SWEEP), LINK_SV_HOLD);
    CHECK_EQ(say(LINK_SV_RESUME, T0 + 2150u), 0u);
    CHECK_EQ(reg(LINK_SV_SWEEP), SWEEP_SINE);
    CHECK(servo_page_step(&pg, &o, T0 + 2150u));
    CHECK_EQ(o.channel[0].command, at_hold);
    CHECK(servo_page_step(&pg, &o, T0 + 2300u));
    CHECK_EQ(o.channel[0].command, 900u);
    /* And the panel's repeats of the same curve carry it on. */
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0 + 2350u), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 2550u));
    CHECK_EQ(o.channel[0].command, 500u);           /* 500 ms into it */
}

/* The ends reached go on from the count at the hold, and a movement count
 * ends the resumed sweep where it would have ended unheld. */
TEST_CASE(resume_keeps_the_movements_reached)
{
    fresh(true);
    const uint16_t three = 3u;
    CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP_MOVES, 1u, &three, &o, T0),
             0u);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0 + 400u), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 600u));
    CHECK_EQ(reg(LINK_SV_SWEEP_DONE), 1u);
    held(T0 + 600u, T0 + 1600u);
    CHECK_EQ(say(LINK_SV_RESUME, T0 + 1650u), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 1650u + 200u));   /* 800 ms in */
    CHECK_EQ(reg(LINK_SV_SWEEP_DONE), 2u);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0 + 1650u + 400u), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 1650u + 600u));   /* 1200 ms in */
    CHECK(!servo_page_step(&pg, &o, T0 + 1650u + 700u));  /* the third end */
    CHECK_EQ(reg(LINK_SV_SWEEP_DONE), 3u);
    CHECK_EQ(o.channel[0].command, SWEEP_CENTRE);
}

/*
 * The surfaces are commanded along the curve at once and slew there from
 * where they were held, at their own rate.
 */
TEST_CASE(resume_slews_from_where_the_surfaces_were_held)
{
    fresh(true);
    CHECK(outputs_set_slew(&o, 0, 200u));
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    uint32_t t = T0;
    for (; t <= T0 + 200u; ++t) {
        (void)servo_page_step(&pg, &o, t);
        outputs_step(&o, t);
    }
    CHECK_EQ(say(LINK_SV_HOLD, t), 0u);
    const uint16_t frozen = outputs_actual(&o, 0);
    CHECK(frozen > 500u && frozen < 560u);         /* 200 a second, behind */
    CHECK_EQ(say(LINK_SV_RESUME, t + 100u), 0u);
    (void)servo_page_step(&pg, &o, t + 100u);
    outputs_step(&o, t + 100u);
    CHECK(o.channel[0].command > 870u);            /* the curve, 200 ms in */
    const uint16_t moved = outputs_actual(&o, 0);
    CHECK(moved > frozen && moved <= frozen + 21u);   /* 100 ms at 200/s */
}

/*
 * RESUME needs a held sweep: refused while one runs, after a hold of no
 * sweep, after 0, a disarm or 500 ms unwritten, and once the curve, its
 * dwell or its movement count has changed since the hold.
 */
TEST_CASE(resume_is_refused_with_no_sweep_held)
{
    fresh(true);
    CHECK_EQ(say(LINK_SV_RESUME, T0), LINK_NACK_BAD_VALUE);    /* nothing */
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    CHECK_EQ(say(LINK_SV_RESUME, T0 + 10u), LINK_NACK_BAD_VALUE); /* runs */
    CHECK_EQ(reg(LINK_SV_SWEEP), SWEEP_SINE);
    CHECK_EQ(say(0u, T0 + 20u), 0u);
    held(T0 + 30u, T0 + 130u);                     /* a hold of nothing */
    CHECK_EQ(say(LINK_SV_RESUME, T0 + 140u), LINK_NACK_BAD_VALUE);

    /* Stopped by 0 while held. */
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    held(T0 + 100u, T0 + 200u);
    CHECK_EQ(say(0u, T0 + 250u), 0u);
    CHECK_EQ(say(LINK_SV_RESUME, T0 + 260u), LINK_NACK_BAD_VALUE);

    /* A disarm: not armed, then nothing kept once armed again. */
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    held(T0 + 100u, T0 + 200u);
    outputs_arm(&o, false, T0 + 210u);
    CHECK_EQ(say(LINK_SV_RESUME, T0 + 220u), LINK_NACK_NOT_ARMED);
    (void)servo_page_step(&pg, &o, T0 + 230u);
    outputs_arm(&o, true, T0 + 240u);
    CHECK_EQ(say(LINK_SV_RESUME, T0 + 250u), LINK_NACK_BAD_VALUE);

    /* 500 ms unwritten, before and after a pass has judged it. */
    for (int judged = 0; judged < 2; ++judged) {
        fresh(true);
        CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
        held(T0 + 100u, T0 + 100u);
        const uint32_t late = T0 + 100u + OUT_DEFAULT_TIMEOUT_MS + 1u;
        if (judged) {
            (void)servo_page_step(&pg, &o, late);
            CHECK_EQ(reg(LINK_SV_SWEEP), 0u);
        }
        CHECK_EQ(say(LINK_SV_RESUME, late), LINK_NACK_BAD_VALUE);
    }

    /* The rate, the dwell or the movement count changed while held. */
    for (unsigned r = LINK_SV_SWEEP_MHZ; r <= LINK_SV_SWEEP_MOVES; ++r) {
        fresh(true);
        CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
        held(T0 + 100u, T0 + 100u);
        const uint16_t v = (r == LINK_SV_SWEEP_MHZ) ? 2000u : 7u;
        CHECK_EQ(servo_page_write(&pg, (uint8_t)r, 1u, &v, &o, T0 + 150u), 0u);
        CHECK_EQ(say(LINK_SV_RESUME, T0 + 160u), LINK_NACK_BAD_VALUE);
        CHECK_EQ(reg(LINK_SV_SWEEP), LINK_SV_HOLD);   /* still held */
    }

    /* And with the curve as it was, written whole beside RESUME, it goes. */
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    held(T0 + 100u, T0 + 100u);
    CHECK_EQ(sweep(LINK_SV_RESUME, 1000u, 400u, 0u, T0 + 150u), 0u);
    CHECK_EQ(reg(LINK_SV_SWEEP), SWEEP_SINE);
}

/*
 * The coprocessor serves a pass's writes before its step.  A sweep left
 * unwritten for longer than OUT_DEFAULT_TIMEOUT_MS has stopped even when
 * the pass that stops it has not run yet: a HOLD then keeps no phase, and a
 * repeat of the same curve starts it over rather than carrying it on --
 * the same as when the step comes first.
 */
TEST_CASE(a_write_after_the_timeout_and_before_the_step_finds_it_stopped)
{
    const uint32_t late = T0 + OUT_DEFAULT_TIMEOUT_MS + 1u;
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 100u));
    CHECK_EQ(say(LINK_SV_HOLD, late), 0u);        /* before late's step */
    (void)servo_page_step(&pg, &o, late);
    CHECK_EQ(say(LINK_SV_RESUME, late + 10u), LINK_NACK_BAD_VALUE);

    /* At the limit itself the sweep still runs, and the hold keeps it. */
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 100u));
    CHECK_EQ(say(LINK_SV_HOLD, late - 1u), 0u);
    CHECK_EQ(say(LINK_SV_RESUME, late + 10u), 0u);

    /* A repeat after the timeout starts the curve from the centre. */
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 100u));
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, late), 0u);
    CHECK(servo_page_step(&pg, &o, late));
    CHECK_EQ(o.channel[0].command, 500u);
}

/*
 * A sweep with a movement count that has made its last movement has ended,
 * even when the HOLD is served before the pass that ends it: the surfaces
 * go to the centre the sweep ends on and are held there, no phase is kept,
 * and RESUME is refused -- the same as when the pass comes first.  A 1 Hz
 * sine with one movement ends 250 ms in.
 */
TEST_CASE(a_hold_after_the_last_movement_finds_the_sweep_ended)
{
    for (int step_first = 0; step_first < 2; ++step_first) {
        fresh(true);
        const uint16_t one = 1u;
        CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP_MOVES, 1u, &one, &o, T0),
                 0u);
        CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
        CHECK(servo_page_step(&pg, &o, T0 + 200u));
        outputs_step(&o, T0 + 200u);
        CHECK(o.channel[0].command > 850u);
        if (step_first) {
            CHECK(!servo_page_step(&pg, &o, T0 + 251u));
        }
        CHECK_EQ(say(LINK_SV_HOLD, T0 + 251u), 0u);
        (void)servo_page_step(&pg, &o, T0 + 251u);
        outputs_step(&o, T0 + 251u);
        CHECK_EQ(reg(LINK_SV_SWEEP_DONE), 1u);
        CHECK_EQ(reg(LINK_SV_SWEEP), LINK_SV_HOLD);
        CHECK_EQ(o.channel[0].command, SWEEP_CENTRE);
        CHECK_EQ(outputs_actual(&o, 0), SWEEP_CENTRE);
        CHECK_EQ(say(LINK_SV_RESUME, T0 + 300u), LINK_NACK_BAD_VALUE);
        held(T0 + 300u, T0 + 900u);                /* held at the centre */
        CHECK_EQ(outputs_actual(&o, 0), SWEEP_CENTRE);
    }

    /* Just before the last movement it is still running, and a hold keeps
     * its phase. */
    fresh(true);
    const uint16_t one = 1u;
    CHECK_EQ(servo_page_write(&pg, LINK_SV_SWEEP_MOVES, 1u, &one, &o, T0), 0u);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    CHECK_EQ(say(LINK_SV_HOLD, T0 + 249u), 0u);
    CHECK_EQ(say(LINK_SV_RESUME, T0 + 300u), 0u);
}

/*
 * A RESUME taken whose acknowledgement the host lost: its retry is refused,
 * and the curve written next carries the resumed sweep on, so a host that
 * starts over writes a stop first; the curve after the stop starts from the
 * centre.
 */
TEST_CASE(after_a_lost_resume_acknowledgement_a_stop_makes_a_start)
{
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    held(T0 + 100u, T0 + 200u);
    CHECK_EQ(say(LINK_SV_RESUME, T0 + 250u), 0u);          /* lost reply */
    CHECK_EQ(say(LINK_SV_RESUME, T0 + 350u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0 + 360u), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 360u));
    CHECK(o.channel[0].command != 500u);                  /* carried on */

    CHECK_EQ(say(0u, T0 + 370u), 0u);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0 + 380u), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 380u));
    CHECK_EQ(o.channel[0].command, 500u);                 /* from the start */
}

/* A frame rate written while held leaves the kept phase alone. */
TEST_CASE(a_frame_rate_written_while_held_keeps_the_phase)
{
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    held(T0 + 100u, T0 + 200u);
    const uint16_t hz = 333u;
    CHECK_EQ(servo_page_write(&pg, LINK_SV_FRAME_HZ, 1u, &hz, &o, T0 + 250u),
             0u);
    CHECK_EQ(say(LINK_SV_RESUME, T0 + 260u), 0u);
}

/*
 * The host's timing of the far end's phase, from acknowledgements: started
 * at 1000, held at 1400 keeps 400; resumed at 3000 its phase 0 is 2600, and
 * the page, fed the same writes at the same moments, is where that says.
 * Held again at 3100 keeps 500; resumed at 5000, phase 0 is 4500.  A resume
 * with nothing kept changes nothing, and a start forgets a kept phase.
 */
TEST_CASE(the_host_times_the_far_ends_phase_across_holds)
{
    servo_phase_t ph;
    memset(&ph, 0, sizeof(ph));
    uint32_t start = 0u;
    servo_phase_started(&ph, 1000u);
    CHECK_EQ(servo_phase_held(&ph, 1400u), 400u);
    CHECK(servo_phase_resumed(&ph, 3000u, &start));
    CHECK_EQ(start, 2600u);
    CHECK(!servo_phase_resumed(&ph, 3050u, &start));   /* once */
    CHECK_EQ(start, 2600u);
    CHECK_EQ(servo_phase_held(&ph, 3100u), 500u);
    CHECK(servo_phase_resumed(&ph, 5000u, &start));
    CHECK_EQ(start, 4500u);
    (void)servo_phase_held(&ph, 5100u);
    servo_phase_started(&ph, 5200u);
    CHECK(!servo_phase_resumed(&ph, 5300u, &start));
    CHECK(!servo_phase_resumed(NULL, 0u, &start));
    CHECK_EQ(servo_phase_held(NULL, 0u), 0u);
    servo_phase_started(NULL, 0u);

    /* The page agrees: a 1 Hz sine started at 1000, held at 1400 and
     * resumed at 3000 is where a sweep started at 2600 would be. */
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, 1000u), 0u);
    held(1400u, 2900u);
    CHECK_EQ(say(LINK_SV_RESUME, 3000u), 0u);
    CHECK(servo_page_step(&pg, &o, 3125u));            /* 525 ms in */
    sweep_t ref;
    const sweep_cfg_t cfg = { SWEEP_SINE, 1000u, 400u, 0u, 0u };
    CHECK(sweep_start(&ref, &cfg, 2600u));
    uint16_t c = 0u;
    CHECK(sweep_step(&ref, 3125u, &c));
    CHECK_EQ(o.channel[0].command, c);
}

/*
 * The HOLD's sequence at the panel: a pause kept, then a stop written for
 * CENTRE, then a HOLD of no sweep -- a SWEEP tap whose start a touch loss
 * replaced -- leave nothing to resume, so a PAUSED tap writes the curve
 * whole with no alert, not a RESUME the far end would refuse.
 */
TEST_CASE(a_stop_forgets_the_kept_phase_at_the_panel)
{
    servo_phase_t ph;
    memset(&ph, 0, sizeof(ph));
    servo_phase_started(&ph, 1000u);
    (void)servo_phase_held(&ph, 1400u);
    CHECK(servo_phase_resumable(&ph));
    servo_phase_stopped(&ph);                  /* CENTRE: the stop */
    CHECK(!servo_phase_resumable(&ph));
    /* A HOLD of no sweep holds nothing resumable either. */
    CHECK_EQ(servo_page_resume_plan(true, servo_phase_resumable(&ph),
                                    true, 6u, false),
             SERVO_RESUME_CURVE);
    /* A HOLD that went unanswered may or may not hold there: a resume
     * asked meanwhile starts over, with its alert. */
    CHECK_EQ(servo_page_resume_plan(true, true, false, 6u, false),
             SERVO_RESUME_UNTIMED);
    servo_phase_started(&ph, 2000u);
    CHECK(!servo_phase_resumable(&ph));
    servo_phase_stopped(NULL);
    CHECK(!servo_phase_resumable(NULL));
}

/*
 * A hold the panel lets go of while it is live at the far end -- a late or
 * retried HOLD acknowledged -- must be ended with a stop before a position
 * is written.  Under a live hold the far end stamps the surface with the
 * HOLD's arrival, so a position written then goes to rest 500 ms after it,
 * at once and unslewed; after a stop the position is clocked by its own
 * writes and stays.
 */
TEST_CASE(a_position_after_a_let_go_hold_needs_the_stop_first)
{
    for (int stopped = 0; stopped < 2; ++stopped) {
        fresh(true);
        CHECK(outputs_set_slew(&o, 0, 200u));
        CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
        uint32_t t = T0;
        for (; t < T0 + 100u; ++t) {
            (void)servo_page_step(&pg, &o, t);
            outputs_step(&o, t);
        }
        const uint32_t arrival = t;
        CHECK_EQ(say(LINK_SV_HOLD, arrival), 0u);   /* acked late */
        if (stopped) {
            CHECK_EQ(say(0u, arrival + 1u), 0u);
        }
        /* A dial drag: positions every 100 ms from 50 ms on. */
        bool rested = false;
        for (t = arrival + 1u; t <= arrival + 700u; ++t) {
            if ((t - arrival) % 100u == 50u) {
                CHECK(outputs_set(&o, 0, 475u, t));
            }
            (void)servo_page_step(&pg, &o, t);
            outputs_step(&o, t);
            if (outputs_overdue(&o, 0, t)) {
                rested = true;                 /* dropped to rest */
            }
        }
        CHECK_EQ(rested, !stopped);            /* the stop is what keeps it */
    }
}

/*
 * The host's choice: RESUME for a resume of a hold in force on 4.6; the
 * curve over, and saying so, on an older coprocessor or after a refusal;
 * the curve as usual for anything else, a resumed sweep's repeats included.
 */
TEST_CASE(the_host_resumes_on_4_6_and_starts_over_otherwise)
{
    CHECK_EQ(servo_page_resume_plan(true, true, true, 6u, false),
             SERVO_RESUME_WRITE);
    CHECK_EQ(servo_page_resume_plan(true, true, true, 7u, false),
             SERVO_RESUME_WRITE);
    CHECK_EQ(servo_page_resume_plan(true, true, true, 5u, false),
             SERVO_RESUME_TOO_OLD);
    CHECK_EQ(servo_page_resume_plan(true, true, true, 6u, true),
             SERVO_RESUME_REFUSED);
    CHECK_EQ(servo_page_resume_plan(true, false, true, 6u, false),
             SERVO_RESUME_CURVE);           /* resumed: now a repeat */
    CHECK_EQ(servo_page_resume_plan(false, true, true, 6u, false),
             SERVO_RESUME_CURVE);           /* a new sweep over a hold */
    CHECK_EQ(servo_page_resume_plan(false, false, true, 5u, false),
             SERVO_RESUME_CURVE);
    /* A HOLD acknowledged only at a retry: its phase is not known here. */
    CHECK_EQ(servo_page_resume_plan(true, true, false, 6u, false),
             SERVO_RESUME_UNTIMED);
    CHECK_EQ(servo_page_resume_plan(true, true, false, 5u, false),
             SERVO_RESUME_TOO_OLD);
    CHECK_EQ(servo_page_resume_plan(false, true, false, 6u, false),
             SERVO_RESUME_CURVE);
}

/* A 4.5 coprocessor's answer to RESUME, as the page refused 5 before it
 * meant anything: BAD_VALUE, so the host's fallback is the same either way. */
TEST_CASE(the_curve_over_a_hold_starts_from_its_beginning)
{
    fresh(true);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0), 0u);
    held(T0 + 100u, T0 + 300u);
    CHECK_EQ(sweep(SWEEP_SINE, 1000u, 400u, 0u, T0 + 350u), 0u);
    CHECK(servo_page_step(&pg, &o, T0 + 350u));
    CHECK_EQ(o.channel[0].command, 500u);          /* from the centre */
    CHECK_EQ(say(LINK_SV_RESUME, T0 + 400u), LINK_NACK_BAD_VALUE);
}

int main(void)
{
    RUN(a_sweep_needs_the_bench_armed);
    RUN(a_sweep_out_of_range_is_refused_whole);
    RUN(a_sweep_drives_the_surfaces_and_nothing_else);
    RUN(a_repeated_sweep_keeps_its_curve_and_a_changed_one_starts_over);
    RUN(a_sweep_stops_when_nobody_writes_it_and_on_a_disarm);
    RUN(a_finished_sweep_is_not_started_again_by_a_repeat);
    RUN(a_sweep_stopped_by_silence_leaves_the_surfaces_where_they_are);
    RUN(a_sweep_stopped_by_the_panel_leaves_the_surfaces_where_they_are);
    RUN(a_sweep_starts_only_from_all_four_registers);
    RUN(a_finished_sweep_keeps_its_centre_while_repeated);
    RUN(hold_keeps_the_surfaces_where_their_outputs_are);
    RUN(resume_carries_a_held_sweep_on_in_phase);
    RUN(resume_keeps_the_movements_reached);
    RUN(resume_slews_from_where_the_surfaces_were_held);
    RUN(resume_is_refused_with_no_sweep_held);
    RUN(a_write_after_the_timeout_and_before_the_step_finds_it_stopped);
    RUN(a_hold_after_the_last_movement_finds_the_sweep_ended);
    RUN(after_a_lost_resume_acknowledgement_a_stop_makes_a_start);
    RUN(a_frame_rate_written_while_held_keeps_the_phase);
    RUN(the_host_times_the_far_ends_phase_across_holds);
    RUN(a_stop_forgets_the_kept_phase_at_the_panel);
    RUN(a_position_after_a_let_go_hold_needs_the_stop_first);
    RUN(the_host_resumes_on_4_6_and_starts_over_otherwise);
    RUN(the_curve_over_a_hold_starts_from_its_beginning);
    return test_summary("servo_page");
}
