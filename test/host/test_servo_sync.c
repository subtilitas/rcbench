/*
 * Synchronising two servos on one surface, run against a modelled pair.
 *
 * The claim under test is not "it returns three numbers". It is that the two
 * installation errors -- a centre that is off and a throw that is long -- can
 * be told apart by measuring current at three positions, and that the routine
 * says so rather than inventing a correction when there is nothing to find.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "servo_sim.h"
#include "servo_sync.h"

#define SAMPLE_MS 5u

static uint32_t run(servo_sync_t *sy, servo_pair_t *pair, uint32_t budget_ms)
{
    uint32_t t = 0;
    uint16_t a = 1500, b = 1500;
    while (sy->state == SERVO_SYNC_RUNNING && t < budget_ms) {
        const float amps = servo_pair_step(pair, a, b, t);
        servo_sync_step(sy, amps, t, &a, &b);
        t += SAMPLE_MS;
    }
    return t;
}

/* What the surface has to absorb, in microseconds, once the corrections the
 * search produced are applied. */
static float residual(const servo_pair_t *pair, const servo_sync_t *sy,
                      int which)
{
    const int32_t c = (int32_t)sy->cfg.centre_us;
    const int32_t d = (int32_t)sy->cfg.deflection_us;
    switch (which) {
    case 1:
        return servo_pair_disagreement(
            pair, (uint16_t)(c + d),
            (uint16_t)(c + sy->trim_us + sy->travel_hi_us));
    case -1:
        return servo_pair_disagreement(
            pair, (uint16_t)(c - d),
            (uint16_t)(c + sy->trim_us - sy->travel_lo_us));
    default:
        return servo_pair_disagreement(pair, (uint16_t)c,
                                       (uint16_t)(c + sy->trim_us));
    }
}

static void setup(servo_sync_t *sy, servo_pair_t *pair)
{
    servo_pair_cfg_t pc;
    servo_pair_defaults(&pc, 1500);
    servo_pair_init(pair, &pc);

    servo_sync_cfg_t sc;
    servo_sync_defaults(&sc, 1500);
    servo_sync_init(sy, &sc);
}

/*
 * The whole point, stated as the thing an operator would care about: after
 * the search, the two servos are no longer fighting.
 */
TEST_CASE(after_the_search_the_pair_barely_disagree_anywhere)
{
    servo_sync_t sy;
    servo_pair_t pair;
    setup(&sy, &pair);
    run(&sy, &pair, 300000);
    CHECK_EQ(sy.state, SERVO_SYNC_DONE);

    /* Two microseconds is well under the scan's own final resolution, and
     * about a twentieth of a degree. */
    for (int w = -1; w <= 1; ++w) {
        const float left = residual(&pair, &sy, w);
        if (left > 2.5f) {
            T_FAIL("%d end still disagrees by %.1f us", w, (double)left);
        }
    }

    /*
     * And the surface is brought back when the search ends.  It finishes at
     * the low end, so a routine that simply stopped would leave a control
     * surface fully deflected on a bench nobody is watching any more.
     */
    uint16_t a = 0, b = 0;
    servo_sync_step(&sy, 0.0f, 0, &a, &b);
    CHECK_EQ(a, 1500);
    CHECK_EQ(b, (uint16_t)(1500 + sy.trim_us));
}

/*
 * Why the search settles before it measures.
 *
 * The scan sweeps servo B across its window in one direction, so a servo that
 * has not arrived is always a step behind where it was told -- and a bias in
 * one direction does not average out, it moves the minimum. On a slow pair
 * that is most of a step, which is most of the correction the search exists
 * to find.
 */
TEST_CASE(a_slow_pair_is_measured_after_it_arrives_not_while_it_travels)
{
    servo_pair_cfg_t pc;
    servo_pair_defaults(&pc, 1500);
    pc.slew_us_per_ms = 0.16f;   /* a 13 us scan step takes most of a window */
    servo_pair_t pair;
    servo_pair_init(&pair, &pc);

    servo_sync_cfg_t sc;
    servo_sync_defaults(&sc, 1500);
    sc.slew_us_per_ms = 0.14f;   /* told about it, pessimistically */
    servo_sync_t sy;
    servo_sync_init(&sy, &sc);
    run(&sy, &pair, 300000);
    CHECK_EQ(sy.state, SERVO_SYNC_DONE);

    /* The same answer as the fast pair: settling is what stops the servos'
     * speed from getting into the result. */
    for (int w = -1; w <= 1; ++w) {
        const float left = residual(&pair, &sy, w);
        if (left > 3.5f) {
            T_FAIL("%d end still disagrees by %.1f us on a slow pair",
                   w, (double)left);
        }
    }
}

/* And it is an improvement, not a coincidence: before the search, the same
 * pair driven with no correction fights hard. */
TEST_CASE(the_uncorrected_pair_really_was_fighting)
{
    servo_pair_cfg_t pc;
    servo_pair_defaults(&pc, 1500);
    servo_pair_t pair;
    servo_pair_init(&pair, &pc);

    /* Both commanded identically, which is what an installation without this
     * measurement does. */
    CHECK(servo_pair_disagreement(&pair, 1500, 1500) > 15.0f);
    CHECK(servo_pair_disagreement(&pair, 1800, 1800) > 25.0f);
}

/*
 * The claim that saves this from being a two-variable search: an offset error
 * shows at centre and a travel error does not.
 */
TEST_CASE(a_pure_offset_error_is_found_at_centre_and_leaves_travel_alone)
{
    servo_pair_cfg_t pc;
    servo_pair_defaults(&pc, 1500);
    pc.offset_us = 25;
    pc.travel    = 1.0f;      /* the throw is right; only the centre is not */
    servo_pair_t pair;
    servo_pair_init(&pair, &pc);

    servo_sync_cfg_t sc;
    servo_sync_defaults(&sc, 1500);
    servo_sync_t sy;
    servo_sync_init(&sy, &sc);
    run(&sy, &pair, 300000);
    CHECK_EQ(sy.state, SERVO_SYNC_DONE);

    /* The trim carries the whole correction ... */
    CHECK(sy.trim_us < -20 && sy.trim_us > -30);
    /* ... and the travel is left where it started. */
    CHECK_EQ(sy.travel_hi_us, 300);
    CHECK_EQ(sy.travel_lo_us, 300);
}

/*
 * And the other way round. A servo whose centre is right but whose throw is
 * long agrees perfectly at centre -- so the centre stage must find nothing to
 * correct, and say so, rather than moving the trim to chase a travel error.
 */
TEST_CASE(a_pure_travel_error_leaves_the_centre_alone)
{
    servo_pair_cfg_t pc;
    servo_pair_defaults(&pc, 1500);
    pc.offset_us = 0;
    pc.travel    = 1.08f;
    servo_pair_t pair;
    servo_pair_init(&pair, &pc);

    servo_sync_cfg_t sc;
    servo_sync_defaults(&sc, 1500);
    servo_sync_t sy;
    servo_sync_init(&sy, &sc);
    run(&sy, &pair, 300000);

    /*
     * The centre scan is not flat even though the centre is right: moving B
     * away from agreement is what the scan does, so a correct centre shows as
     * a clean minimum *at zero correction* rather than as no minimum at all.
     * That distinction matters -- it is the difference between "measured, and
     * nothing to change" and "could not measure", and only the second is a
     * fault.
     */
    CHECK_EQ(sy.state, SERVO_SYNC_DONE);
    CHECK(sy.trim_us > -3 && sy.trim_us < 3);

    /* And the travel error, which centre could not see, is corrected: a
     * throw 8% long must be commanded 300 / 1.08 = 278 us. */
    CHECK(sy.travel_hi_us > 268 && sy.travel_hi_us < 288);
    CHECK(sy.travel_lo_us > 268 && sy.travel_lo_us < 288);
}

/*
 * A pair that is already synchronised must come back with nothing to change,
 * rather than with a small correction assembled out of noise.
 */
TEST_CASE(an_already_synchronised_pair_is_left_alone)
{
    servo_pair_cfg_t pc;
    servo_pair_defaults(&pc, 1500);
    pc.offset_us = 0;
    pc.travel    = 1.0f;
    servo_pair_t pair;
    servo_pair_init(&pair, &pc);

    servo_sync_cfg_t sc;
    servo_sync_defaults(&sc, 1500);
    servo_sync_t sy;
    servo_sync_init(&sy, &sc);
    run(&sy, &pair, 300000);

    CHECK_EQ(sy.state, SERVO_SYNC_DONE);
    CHECK(sy.trim_us > -3 && sy.trim_us < 3);
    CHECK(sy.travel_hi_us > 295 && sy.travel_hi_us < 305);
    CHECK(sy.travel_lo_us > 295 && sy.travel_lo_us < 305);
    /* Nothing to gain, and it says so: the best and worst of the centre scan
     * differ only by what moving B away from agreement costs. */
    CHECK(residual(&pair, &sy, 0) < 3.0f);
}

/* A sensor too coarse to see the disagreement gives the same answer, and that
 * is the right answer: it cannot tell, and it says it cannot tell. */
TEST_CASE(a_sensor_that_cannot_see_the_fight_says_so)
{
    servo_pair_cfg_t pc;
    servo_pair_defaults(&pc, 1500);
    pc.fight_a_per_us = 0.0004f;    /* 40 us of fight is 16 mA */
    servo_pair_t pair;
    servo_pair_init(&pair, &pc);

    servo_sync_cfg_t sc;
    servo_sync_defaults(&sc, 1500);
    servo_sync_t sy;
    servo_sync_init(&sy, &sc);
    run(&sy, &pair, 300000);

    CHECK_EQ(sy.state, SERVO_SYNC_FAULT);
    CHECK_EQ(sy.fault, SERVO_SYNC_FAULT_NO_MINIMUM);
}

/* The two ends are searched separately, so an installation whose throw is
 * long one way and short the other is corrected both ways. */
TEST_CASE(the_two_ends_get_their_own_corrections)
{
    servo_sync_t sy;
    servo_pair_t pair;
    setup(&sy, &pair);
    run(&sy, &pair, 300000);
    CHECK_EQ(sy.state, SERVO_SYNC_DONE);

    /* B's throw is 6% long, so it must be commanded about 6% less: 300 us
     * becomes roughly 283. Both ends, because both were measured. */
    CHECK(sy.travel_hi_us < 295 && sy.travel_hi_us > 270);
    CHECK(sy.travel_lo_us < 295 && sy.travel_lo_us > 270);
}

TEST_CASE(the_hard_ceiling_aborts_the_search)
{
    servo_pair_cfg_t pc;
    servo_pair_defaults(&pc, 1500);
    pc.free_a = 5.0f;          /* over the ceiling before it starts */
    servo_pair_t pair;
    servo_pair_init(&pair, &pc);

    servo_sync_cfg_t sc;
    servo_sync_defaults(&sc, 1500);
    servo_sync_t sy;
    servo_sync_init(&sy, &sc);
    run(&sy, &pair, 60000);

    CHECK_EQ(sy.state, SERVO_SYNC_FAULT);
    CHECK_EQ(sy.fault, SERVO_SYNC_FAULT_OVERCURRENT);
    /* And it does not leave the surface deflected. */
    uint16_t a = 0, b = 0;
    servo_sync_step(&sy, 0.0f, 0, &a, &b);
    CHECK_EQ(a, 1500);
}

TEST_CASE(the_search_takes_a_workable_amount_of_time)
{
    servo_sync_t sy;
    servo_pair_t pair;
    setup(&sy, &pair);
    const uint32_t took = run(&sy, &pair, 300000);
    CHECK_EQ(sy.state, SERVO_SYNC_DONE);
    /* Three stages of three rounds of seven points at 250 ms is about
     * sixteen seconds. Longer than the limit search, and for the same reason
     * worth pinning. */
    CHECK(took < 20000);
}

TEST_CASE(progress_runs_from_nothing_to_all_of_it)
{
    servo_sync_t sy;
    servo_pair_t pair;
    setup(&sy, &pair);
    CHECK_NEAR(servo_sync_progress(&sy), 0.0f, 0.001f);
    run(&sy, &pair, 300000);
    CHECK_NEAR(servo_sync_progress(&sy), 1.0f, 0.001f);
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    servo_sync_defaults(NULL, 1500);
    servo_sync_init(NULL, NULL);
    servo_pair_defaults(NULL, 1500);
    servo_pair_init(NULL, NULL);
    servo_sync_step(NULL, 0.0f, 0, NULL, NULL);
    CHECK_NEAR(servo_sync_progress(NULL), 0.0f, 0.001f);
    CHECK_NEAR(servo_pair_step(NULL, 1500, 1500, 0), 0.0f, 0.001f);
    CHECK_NEAR(servo_pair_disagreement(NULL, 1500, 1500), 0.0f, 0.001f);
}

int main(void)
{
    RUN(the_uncorrected_pair_really_was_fighting);
    RUN(a_slow_pair_is_measured_after_it_arrives_not_while_it_travels);
    RUN(after_the_search_the_pair_barely_disagree_anywhere);
    RUN(a_pure_offset_error_is_found_at_centre_and_leaves_travel_alone);
    RUN(a_pure_travel_error_leaves_the_centre_alone);
    RUN(an_already_synchronised_pair_is_left_alone);
    RUN(a_sensor_that_cannot_see_the_fight_says_so);
    RUN(the_two_ends_get_their_own_corrections);
    RUN(the_hard_ceiling_aborts_the_search);
    RUN(the_search_takes_a_workable_amount_of_time);
    RUN(progress_runs_from_nothing_to_all_of_it);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("servo_sync");
}
