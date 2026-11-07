/**
 * @file test_servo_start_seq.c
 * @brief ARM + START's steps (servo_start_seq.h): the order PORT PWM,
 *        supply, read-back, ARM, ARMED, run; the read-back's 1000ms and
 *        ARMED's 500ms either side of the limit; the part each failure
 *        names; a part already on not asked again; STOP; the step numbers;
 *        a step's time judged before what it reads; the checks judged on
 *        a reading after the parts; the part a refusal names; FAILSAFE
 *        CLEAR first where the IOmcu's failsafe is latched, its 500ms
 *        either side of the limit, and a supply in the failsafe refused
 *        at once.
 *
 * Cases:
 * - a_carrier_sequence_runs_its_steps_in_order
 * - the_read_back_has_1000ms_tested_at_900_and_1100
 * - armed_has_500ms_tested_at_400_and_600
 * - a_pulse_that_does_not_come_names_port_pwm
 * - a_pd_mini_that_does_not_come_names_pd_mini
 * - parts_already_on_are_not_asked_again
 * - a_refusal_fails_the_step_under_way
 * - stop_fails_the_step_under_way_and_asks_all_off
 * - a_run_that_refuses_its_start_fails_at_start
 * - the_steps_are_numbered_by_what_the_run_needs
 * - null_and_idle_take_nothing
 * - a_part_or_armed_after_a_stall_past_its_time_fails
 * - the_checks_wait_for_a_reading_after_the_parts
 * - a_refusal_names_the_part_the_caller_gives
 * - a_latched_failsafe_is_cleared_first_then_the_steps_run
 * - the_clear_has_500ms_tested_at_499_and_500
 * - a_supply_in_the_failsafe_fails_at_once_and_is_not_asked
 * - without_a_supply_the_failsafe_is_left_to_arm
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "servo_start_seq.h"

/** @brief The carrier's port: PORT PWM and PROG 5V. */
static const servo_seq_need_t k_carrier = { true, true, false, false,
                                            false };
/** @brief A PROG PORT fed by the PD mini. */
static const servo_seq_need_t k_pd = { true, false, true, false, false };
/** @brief The carrier's port with the rail's reading waited for. */
static const servo_seq_need_t k_settle = { true, true, false, true, false };

/** @brief What the bench reads: nothing on, SAFE. */
static servo_seq_in_t off(void)
{
    servo_seq_in_t in;
    memset(&in, 0, sizeof(in));
    return in;
}

/** @brief Whether @p d asks nothing at all. */
static bool nothing(const servo_seq_do_t *d)
{
    return !d->clear && !d->set_pwm && !d->p5_on && !d->pd_on && !d->arm
           && !d->start && !d->all_off;
}

/* From SAFE with everything off: the first step asks PORT PWM and PROG 5V
 * at once; the read-back waits for the 5V and the pulse; then ARM once;
 * ARMED, then start; the run takes it and the sequence is idle and done. */
TEST_CASE(a_carrier_sequence_runs_its_steps_in_order)
{
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_init(&q);
    CHECK(!servo_seq_busy(&q));
    CHECK(servo_seq_begin(&q, &k_carrier, 1000u));
    CHECK(servo_seq_busy(&q));
    CHECK(!servo_seq_begin(&q, &k_carrier, 1000u));   /* one at a time */
    servo_seq_in_t in = off();
    servo_seq_step(&q, 1000u, &in, &d);
    CHECK(d.set_pwm && d.p5_on && !d.pd_on && !d.arm && !d.start);
    CHECK_EQ(servo_seq_step_now(&q), SERVO_SEQ_READBACK);
    in.pwm_set = true;
    servo_seq_step(&q, 1020u, &in, &d);
    CHECK(nothing(&d));
    in.p5_on = true;
    servo_seq_step(&q, 1100u, &in, &d);
    CHECK(nothing(&d));                       /* the pulse after the 5V */
    in.pwm_on = true;
    servo_seq_step(&q, 1140u, &in, &d);
    CHECK(d.arm && !d.start);
    CHECK_EQ(q.readback_ms, 140u);
    CHECK_EQ(servo_seq_step_now(&q), SERVO_SEQ_ARM);
    servo_seq_step(&q, 1160u, &in, &d);
    CHECK(nothing(&d));                       /* ARM asked once */
    in.armed = true;
    servo_seq_step(&q, 1180u, &in, &d);
    CHECK(d.start && !d.arm);
    CHECK_EQ(servo_seq_step_now(&q), SERVO_SEQ_RUN);
    /* Asked again until answered. */
    servo_seq_step(&q, 1200u, &in, &d);
    CHECK(d.start);
    servo_seq_started(&q, true, &d);
    CHECK(nothing(&d));
    CHECK(!servo_seq_busy(&q));
    CHECK(q.done);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_NONE);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_NONE);
}

/* The read-back has SERVO_SEQ_READBACK_MS from the supply's ON: at 900ms
 * the sequence still waits and the 5V read then goes on; at 1100ms
 * without it the sequence has failed at PROG 5V, timed out, all off.  The
 * limit itself is the end: 999ms waits, 1000ms fails. */
TEST_CASE(the_read_back_has_1000ms_tested_at_900_and_1100)
{
    CHECK_EQ(SERVO_SEQ_READBACK_MS, 1000u);
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    in.pwm_set = true;

    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    servo_seq_step(&q, 900u, &in, &d);
    CHECK(nothing(&d));
    CHECK_EQ(servo_seq_step_now(&q), SERVO_SEQ_READBACK);
    in.p5_on = in.pwm_on = true;
    servo_seq_step(&q, 900u, &in, &d);
    CHECK(d.arm);

    in = off();
    in.pwm_set = true;
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    servo_seq_step(&q, 1100u, &in, &d);
    CHECK(d.all_off && !d.arm);
    CHECK(!servo_seq_busy(&q));
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PROG_5V);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_TIMEOUT);

    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 50u);
    servo_seq_step(&q, 50u, &in, &d);
    servo_seq_step(&q, 50u + 999u, &in, &d);
    CHECK(servo_seq_busy(&q));
    servo_seq_step(&q, 50u + 1000u, &in, &d);
    CHECK(!servo_seq_busy(&q) && d.all_off);
    /* Across the clock's wrap the same. */
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0xFFFFFF00u);
    servo_seq_step(&q, 0xFFFFFF00u, &in, &d);
    servo_seq_step(&q, 0xFFFFFF00u + 900u, &in, &d);
    CHECK(servo_seq_busy(&q));
    servo_seq_step(&q, 0xFFFFFF00u + 1100u, &in, &d);
    CHECK(!servo_seq_busy(&q));
}

/* ARMED has SERVO_SEQ_ARMED_MS from ARM asked: at 400ms it still waits
 * and ARMED then starts; at 600ms without it the sequence failed at ARM. */
TEST_CASE(armed_has_500ms_tested_at_400_and_600)
{
    CHECK_EQ(SERVO_SEQ_ARMED_MS, 500u);
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    in.pwm_set = in.pwm_on = in.p5_on = true;
    for (int late = 0; late < 2; ++late) {
        servo_seq_init(&q);
        (void)servo_seq_begin(&q, &k_carrier, 100u);
        in.armed = false;
        servo_seq_step(&q, 100u, &in, &d);
        CHECK(d.arm);
        const uint32_t at = 100u + (late ? 600u : 400u);
        servo_seq_step(&q, 100u + 499u, &in, &d);
        CHECK(nothing(&d));
        if (late) {
            servo_seq_step(&q, at, &in, &d);
            CHECK(d.all_off && !d.start);
            CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_ARM);
            CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_TIMEOUT);
        } else {
            in.armed = true;
            servo_seq_step(&q, at, &in, &d);
            CHECK(d.start);
        }
    }
}

/* The 5V reads ON and the port's pulse never does: the read-back names
 * PORT PWM. */
TEST_CASE(a_pulse_that_does_not_come_names_port_pwm)
{
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    in.pwm_set = in.p5_on = true;
    servo_seq_step(&q, 1000u, &in, &d);
    CHECK(d.all_off);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PWM);
}

/* A PROG PORT fed by the PD mini: its output asked ON, not PROG 5V, and a
 * read-back without it names PD MINI. */
TEST_CASE(a_pd_mini_that_does_not_come_names_pd_mini)
{
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    in.pwm_set = in.pwm_on = true;
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_pd, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.pd_on && !d.p5_on && !d.set_pwm);
    servo_seq_step(&q, 1100u, &in, &d);
    CHECK(d.all_off);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PD_MINI);
}

/* A bench set up by hand: PORT PWM on, the 5V and the pulse read on,
 * ARMED.  Nothing is asked; the first step starts the run. */
TEST_CASE(parts_already_on_are_not_asked_again)
{
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    in.pwm_set = in.pwm_on = in.p5_on = in.armed = true;
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.start && !d.set_pwm && !d.p5_on && !d.arm);
    CHECK_EQ(q.readback_ms, 0u);
    /* SAFE with the parts on: ARM only. */
    in.armed = false;
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.arm && !d.set_pwm && !d.p5_on);
}

/* A check of the caller's that refuses on the way: in the read-back it
 * names the part not yet read back, in ARM it names ARM; all off. */
TEST_CASE(a_refusal_fails_the_step_under_way)
{
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    in.refused = true;
    servo_seq_step(&q, 20u, &in, &d);
    CHECK(d.all_off);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PROG_5V);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_REFUSED);

    in = off();
    in.pwm_set = in.pwm_on = in.p5_on = true;
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.arm);
    in.refused = true;
    servo_seq_step(&q, 20u, &in, &d);
    CHECK(d.all_off);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_ARM);
    /* Refused at the very start: it names the first step. */
    in = off();
    in.refused = true;
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.all_off && !d.set_pwm && !d.p5_on);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PWM);
}

/* STOP while ARM is asked: the sequence ends at ARM as STOP, all off; a
 * STOP with none under way asks nothing; a new begin forgets the
 * failure. */
TEST_CASE(stop_fails_the_step_under_way_and_asks_all_off)
{
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    in.pwm_set = in.pwm_on = in.p5_on = true;
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    servo_seq_abort(&q, SERVO_SEQ_WHY_STOP, &d);
    CHECK(d.all_off);
    CHECK(!servo_seq_busy(&q));
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_ARM);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_STOP);
    servo_seq_abort(&q, SERVO_SEQ_WHY_STOP, &d);
    CHECK(nothing(&d));
    /* An abort with no reason is a STOP. */
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_NONE);
    servo_seq_abort(&q, SERVO_SEQ_WHY_NONE, &d);
    CHECK(d.all_off);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_STOP);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PWM);
}

/* The run refuses the start the sequence asks: it fails at START, all
 * off; an answer with none asked takes nothing. */
TEST_CASE(a_run_that_refuses_its_start_fails_at_start)
{
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    in.pwm_set = in.pwm_on = in.p5_on = in.armed = true;
    servo_seq_init(&q);
    servo_seq_started(&q, true, &d);
    CHECK(nothing(&d) && !q.done);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.start);
    /* A refusal of the caller's while the start waits is the run's to
     * answer. */
    in.refused = true;
    servo_seq_step(&q, 20u, &in, &d);
    CHECK(d.start && !d.all_off);
    servo_seq_started(&q, false, &d);
    CHECK(d.all_off);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_RUN);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_REFUSED);
    CHECK(!q.done);
}

/* The steps a sequence shows: five on the carrier, five with the PD mini,
 * three on OUTPUTS with nothing to switch (ARM, RUN and none of the
 * others); IDLE and RUN's place. */
TEST_CASE(the_steps_are_numbered_by_what_the_run_needs)
{
    int n = 0;
    CHECK_EQ(servo_seq_number(&k_carrier, SERVO_SEQ_PWM, &n), 1);
    CHECK_EQ(n, 5);
    CHECK_EQ(servo_seq_number(&k_carrier, SERVO_SEQ_READBACK, NULL), 3);
    CHECK_EQ(servo_seq_number(&k_pd, SERVO_SEQ_RUN, &n), 5);
    const servo_seq_need_t none = { false, false, false, false, false };
    CHECK_EQ(servo_seq_number(&none, SERVO_SEQ_PWM, &n), 0);
    CHECK_EQ(n, 2);
    CHECK_EQ(servo_seq_number(&none, SERVO_SEQ_ARM, NULL), 1);
    CHECK_EQ(servo_seq_number(&none, SERVO_SEQ_RUN, NULL), 2);
    const servo_seq_need_t pd_only = { false, false, true, false, false };
    CHECK_EQ(servo_seq_number(&pd_only, SERVO_SEQ_SUPPLY, &n), 1);
    CHECK_EQ(n, 4);
    CHECK_EQ(servo_seq_number(&k_carrier, SERVO_SEQ_IDLE, NULL), 0);
    CHECK_EQ(servo_seq_number(NULL, SERVO_SEQ_RUN, &n), 5);
    CHECK_EQ(n, 5);
    /* A sequence with nothing to switch goes from the first call to ARM. */
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &none, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.arm && !d.set_pwm && !d.p5_on && !d.pd_on);
}

/* NULL everywhere takes nothing; a step of an idle sequence asks
 * nothing. */
TEST_CASE(null_and_idle_take_nothing)
{
    servo_seq_t q;
    servo_seq_do_t d;
    const servo_seq_in_t in = off();
    servo_seq_init(NULL);
    CHECK(!servo_seq_begin(NULL, &k_carrier, 0u));
    servo_seq_init(&q);
    CHECK(!servo_seq_begin(&q, NULL, 0u));
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(nothing(&d));
    servo_seq_step(NULL, 0u, &in, &d);
    CHECK(nothing(&d));
    servo_seq_step(&q, 0u, NULL, &d);
    CHECK(nothing(&d));
    servo_seq_step(&q, 0u, &in, NULL);
    servo_seq_started(&q, true, NULL);
    servo_seq_abort(&q, SERVO_SEQ_WHY_STOP, NULL);
    servo_seq_abort(NULL, SERVO_SEQ_WHY_STOP, &d);
    CHECK(nothing(&d));
    CHECK(!servo_seq_busy(NULL));
    CHECK_EQ(servo_seq_step_now(NULL), SERVO_SEQ_IDLE);
    CHECK_EQ(servo_seq_part(NULL), SERVO_SEQ_PART_NONE);
    CHECK_EQ(servo_seq_why(NULL), SERVO_SEQ_WHY_NONE);
}

/* A frame that stalls 1500ms: the 5V and the pulse read ON on the next
 * call, past the read-back's 1000ms, and the step fails all the same,
 * named PROG 5V, timed out.  ARMED read only after a 600ms stall fails
 * ARM. */
TEST_CASE(a_part_or_armed_after_a_stall_past_its_time_fails)
{
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    in.pwm_set = in.p5_on = in.pwm_on = true;
    servo_seq_step(&q, 1500u, &in, &d);
    CHECK(d.all_off && !d.arm);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PROG_5V);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_TIMEOUT);

    servo_seq_init(&q);
    in.armed = false;
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.arm);
    in.armed = true;
    servo_seq_step(&q, 600u, &in, &d);
    CHECK(d.all_off && !d.start);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_ARM);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_TIMEOUT);
}

/* With settle, READ-BACK judges the caller's checks only on a reading
 * that began once every part read ON: a refusal on the window measured
 * before the 5V (from 80ms, the parts at 100ms) waits; on one from 120ms
 * it fails, named by the caller's part; a good one from 120ms goes on to
 * ARM; no such reading by 1000ms times out at PROG 5V. */
TEST_CASE(the_checks_wait_for_a_reading_after_the_parts)
{
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_settle, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.p5_on);
    in.pwm_set = in.p5_on = in.pwm_on = true;
    in.refused = true;
    in.refused_part = (uint8_t)SERVO_SEQ_PART_PROG_5V;
    in.reading = true;
    in.reading_from_ms = 80u;
    servo_seq_step(&q, 100u, &in, &d);
    CHECK(nothing(&d));
    CHECK(servo_seq_busy(&q));
    servo_seq_step(&q, 150u, &in, &d);
    CHECK(servo_seq_busy(&q));
    in.reading_from_ms = 120u;
    servo_seq_step(&q, 170u, &in, &d);
    CHECK(d.all_off);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PROG_5V);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_REFUSED);

    servo_seq_init(&q);
    in = off();
    (void)servo_seq_begin(&q, &k_settle, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    in.pwm_set = in.p5_on = in.pwm_on = true;
    in.reading = true;
    in.reading_from_ms = 80u;
    servo_seq_step(&q, 100u, &in, &d);
    CHECK(nothing(&d));
    in.reading_from_ms = 120u;
    servo_seq_step(&q, 170u, &in, &d);
    CHECK(d.arm);
    CHECK_EQ(q.readback_ms, 170u);

    servo_seq_init(&q);
    in = off();
    (void)servo_seq_begin(&q, &k_settle, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    in.pwm_set = in.p5_on = in.pwm_on = true;
    servo_seq_step(&q, 100u, &in, &d);
    servo_seq_step(&q, 1000u, &in, &d);
    CHECK(d.all_off);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PROG_5V);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_TIMEOUT);
}

/* A refusal names the caller's part where it gives one, in any step: the
 * pulse's text in ARM names PORT PWM; an out-of-range part the step's
 * own. */
TEST_CASE(a_refusal_names_the_part_the_caller_gives)
{
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    in.pwm_set = in.pwm_on = in.p5_on = true;
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.arm);
    in.refused = true;
    in.refused_part = (uint8_t)SERVO_SEQ_PART_PWM;
    servo_seq_step(&q, 20u, &in, &d);
    CHECK(d.all_off);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PWM);

    in.refused = false;
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    in.refused = true;
    in.refused_part = (uint8_t)SERVO_SEQ_PARTS;
    servo_seq_step(&q, 20u, &in, &d);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_ARM);
}

/** @brief A PROG PORT fed by the PD mini, the IOmcu's failsafe latched at
 *         the hold: FAILSAFE CLEAR first. */
static const servo_seq_need_t k_pd_clear = { true, false, true, false,
                                             true };

/* The failsafe latched at the hold (FAULT 01 after a panel restart): the
 * first call asks CLEAR and nothing else; while the failsafe still reads,
 * and while the heartbeat reads distrusted, nothing more; once both read
 * clear, PORT PWM and the PD mini's ON in the same call, then the
 * read-back, ARM once, ARMED and the start.  CLEAR shows as step 1 of 6. */
TEST_CASE(a_latched_failsafe_is_cleared_first_then_the_steps_run)
{
    int n = 0;
    CHECK_EQ(servo_seq_number(&k_pd_clear, SERVO_SEQ_CLEAR, &n), 1);
    CHECK_EQ(n, 6);
    CHECK_EQ(servo_seq_number(&k_pd_clear, SERVO_SEQ_READBACK, NULL), 4);
    CHECK_EQ(servo_seq_number(&k_pd, SERVO_SEQ_CLEAR, &n), 0);
    CHECK_EQ(n, 5);

    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_init(&q);
    CHECK(servo_seq_begin(&q, &k_pd_clear, 1000u));
    CHECK_EQ(servo_seq_step_now(&q), SERVO_SEQ_CLEAR);
    servo_seq_in_t in = off();
    in.latched = true;
    servo_seq_step(&q, 1000u, &in, &d);
    CHECK(d.clear);
    d.clear = false;
    CHECK(nothing(&d));
    CHECK_EQ(servo_seq_step_now(&q), SERVO_SEQ_CLEAR);
    servo_seq_step(&q, 1100u, &in, &d);
    CHECK(nothing(&d));                       /* CLEAR asked once */
    in.latched = false;
    in.line_out = true;
    servo_seq_step(&q, 1150u, &in, &d);
    CHECK(nothing(&d));                       /* the heartbeat too */
    in.line_out = false;
    servo_seq_step(&q, 1200u, &in, &d);
    CHECK(!d.clear && d.set_pwm && d.pd_on && !d.arm && !d.all_off);
    CHECK_EQ(servo_seq_step_now(&q), SERVO_SEQ_READBACK);
    in.pwm_set = in.pd_on = in.pwm_on = true;
    servo_seq_step(&q, 1300u, &in, &d);
    CHECK(d.arm);
    CHECK_EQ(q.readback_ms, 100u);
    in.armed = true;
    servo_seq_step(&q, 1320u, &in, &d);
    CHECK(d.start);
    servo_seq_started(&q, true, &d);
    CHECK(!servo_seq_busy(&q) && q.done);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_NONE);
}

/* The failsafe has SERVO_SEQ_CLEAR_MS from the CLEAR to read clear: clear
 * at 499ms the steps go on; still latched at 499ms they wait, and at
 * 500ms they fail at FAILSAFE CLEAR for the failsafe, all off, with no ON
 * and no ARM ever asked.  A clear read only at the limit itself fails: the
 * time first.  Across the clock's wrap the same. */
TEST_CASE(the_clear_has_500ms_tested_at_499_and_500)
{
    CHECK_EQ(SERVO_SEQ_CLEAR_MS, 500u);
    static const uint32_t t0s[2] = { 50u, 0xFFFFFF00u };
    for (int w = 0; w < 2; ++w) {
        const uint32_t t0 = t0s[w];
        servo_seq_t q;
        servo_seq_do_t d;
        servo_seq_in_t in = off();
        in.latched = true;

        servo_seq_init(&q);
        (void)servo_seq_begin(&q, &k_pd_clear, t0);
        servo_seq_step(&q, t0, &in, &d);
        CHECK(d.clear);
        in.latched = false;
        servo_seq_step(&q, t0 + 499u, &in, &d);
        CHECK(d.pd_on && !d.all_off);

        in = off();
        in.latched = true;
        servo_seq_init(&q);
        (void)servo_seq_begin(&q, &k_pd_clear, t0);
        servo_seq_step(&q, t0, &in, &d);
        servo_seq_step(&q, t0 + 499u, &in, &d);
        CHECK(nothing(&d));
        CHECK(servo_seq_busy(&q));
        servo_seq_step(&q, t0 + 500u, &in, &d);
        CHECK(d.all_off && !d.pd_on && !d.set_pwm && !d.arm && !d.clear);
        CHECK(!servo_seq_busy(&q));
        CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_CLEAR);
        CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_FAILSAFE);

        in = off();
        in.latched = true;
        servo_seq_init(&q);
        (void)servo_seq_begin(&q, &k_pd_clear, t0);
        servo_seq_step(&q, t0, &in, &d);
        in.latched = false;
        servo_seq_step(&q, t0 + 500u, &in, &d);
        CHECK(d.all_off && !d.pd_on);
        CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_CLEAR);
    }
}

/* No CLEAR asked (the heartbeat distrusted at the hold, FAULT 10, or the
 * failsafe latched since): a supply still to be switched on is named at
 * once, for the failsafe, before anything is asked -- no PORT PWM, no ON,
 * no CLEAR.  One that comes in the read-back, as the IOmcu's refusal of
 * the ON does about 400ms after it, fails the step on that frame and not
 * at 1000ms.  Other refusals stay as they were: a row of the caller's
 * names its part, REFUSED. */
TEST_CASE(a_supply_in_the_failsafe_fails_at_once_and_is_not_asked)
{
    servo_seq_t q;
    servo_seq_do_t d;
    for (int k = 0; k < 2; ++k) {
        servo_seq_in_t in = off();
        in.line_out = k == 0;
        in.latched  = k == 1;
        servo_seq_init(&q);
        (void)servo_seq_begin(&q, &k_pd, 0u);
        servo_seq_step(&q, 0u, &in, &d);
        CHECK(d.all_off && !d.set_pwm && !d.pd_on && !d.clear && !d.arm);
        CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PD_MINI);
        CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_FAILSAFE);
    }
    /* The carrier's PROG 5V the same. */
    servo_seq_in_t in = off();
    in.latched = true;
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_carrier, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.all_off && !d.p5_on);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PROG_5V);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_FAILSAFE);

    /* In the read-back, at 400ms. */
    in = off();
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_pd, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.pd_on);
    in.pwm_set = in.pwm_on = true;
    servo_seq_step(&q, 380u, &in, &d);
    CHECK(nothing(&d));
    in.latched = true;
    servo_seq_step(&q, 400u, &in, &d);
    CHECK(d.all_off && !d.arm);
    CHECK_EQ(servo_seq_part(&q), SERVO_SEQ_PART_PD_MINI);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_FAILSAFE);

    /* A refusal of the caller's is no failsafe. */
    in = off();
    in.refused = true;
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &k_pd, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.all_off && !d.clear);
    CHECK_EQ(servo_seq_why(&q), SERVO_SEQ_WHY_REFUSED);
}

/* A run that switches no supply goes to ARM in the failsafe: ARM writes
 * its own CLEAR before it arms, so the steps ask none and name nothing. */
TEST_CASE(without_a_supply_the_failsafe_is_left_to_arm)
{
    static const servo_seq_need_t pwm_only = { true, false, false, false,
                                               false };
    servo_seq_t q;
    servo_seq_do_t d;
    servo_seq_in_t in = off();
    in.latched = true;
    servo_seq_init(&q);
    (void)servo_seq_begin(&q, &pwm_only, 0u);
    servo_seq_step(&q, 0u, &in, &d);
    CHECK(d.set_pwm && !d.clear && !d.all_off);
    in.pwm_set = in.pwm_on = true;
    servo_seq_step(&q, 20u, &in, &d);
    CHECK(d.arm && !d.all_off);
}

int main(void)
{
    RUN(a_carrier_sequence_runs_its_steps_in_order);
    RUN(the_read_back_has_1000ms_tested_at_900_and_1100);
    RUN(armed_has_500ms_tested_at_400_and_600);
    RUN(a_pulse_that_does_not_come_names_port_pwm);
    RUN(a_pd_mini_that_does_not_come_names_pd_mini);
    RUN(parts_already_on_are_not_asked_again);
    RUN(a_refusal_fails_the_step_under_way);
    RUN(stop_fails_the_step_under_way_and_asks_all_off);
    RUN(a_run_that_refuses_its_start_fails_at_start);
    RUN(the_steps_are_numbered_by_what_the_run_needs);
    RUN(null_and_idle_take_nothing);
    RUN(a_part_or_armed_after_a_stall_past_its_time_fails);
    RUN(the_checks_wait_for_a_reading_after_the_parts);
    RUN(a_refusal_names_the_part_the_caller_gives);
    RUN(a_latched_failsafe_is_cleared_first_then_the_steps_run);
    RUN(the_clear_has_500ms_tested_at_499_and_500);
    RUN(a_supply_in_the_failsafe_fails_at_once_and_is_not_asked);
    RUN(without_a_supply_the_failsafe_is_left_to_arm);
    return test_summary("servo_start_seq");
}
