/*
 * The limit search, run against a modelled servo on a modelled surface.
 *
 * What is worth testing is not that it finds a number.  It is that it finds
 * one on the free side of the stop, that it says so rather than guessing when
 * there is nothing to find, and that every one of the three protections
 * actually stops it -- because this is the one routine in the project that
 * deliberately drives a servo toward something solid.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "servo_limit.h"
#include "servo_sim.h"

#define SAMPLE_MS 5u   /* a 200 Hz current sensor */

/* Run the search to a conclusion, or until it has plainly hung.  Returns the
 * number of milliseconds it took. */
static uint32_t search(servo_limit_t *lf, servo_sim_t *sim, uint32_t budget_ms)
{
    uint32_t t = 0;
    uint16_t cmd = lf->cmd_us;
    while (lf->state == SERVO_LIMIT_RUNNING && t < budget_ms) {
        const float a = servo_sim_step(sim, cmd, t);
        cmd = servo_limit_step(lf, a, t);
        t += SAMPLE_MS;
    }
    return t;
}

static void setup(servo_limit_t *lf, servo_sim_t *sim, bool positive)
{
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    servo_sim_init(sim, &sc);

    servo_limit_cfg_t lc;
    servo_limit_defaults(&lc, 1500, positive);
    servo_limit_init(lf, &lc);
}

TEST_CASE(it_finds_the_upper_stop_and_stops_short_of_it)
{
    servo_limit_t lf;
    servo_sim_t sim;
    setup(&lf, &sim, true);

    search(&lf, &sim, 60000);
    CHECK_EQ(lf.state, SERVO_LIMIT_FOUND);

    /*
     * The exact number, not a window, because the model is deterministic and
     * a window wide enough to be comfortable is wide enough to accept a wrong
     * answer.  Every earlier version of this assertion passed with the
     * endpoint taken from the position that bound instead of the last one
     * that did not, and with the backoff removed entirely.
     *
     *   baseline          0.12 A free
     *   knee needs        > 0.12 x 1.8 = 0.216 and > 0.12 + 0.15 = 0.27
     *   the model gives   0.12 + 2.28 x over/45 A
     *   so it binds at    over > 3 us, i.e. the first step past 1880
     *   steps of 10 from 1500 put that step at 1890, last free at 1880
     *   backoff 25        1880 - 25 = 1855
     */
    CHECK_EQ(lf.limit_us, 1855);
    CHECK(lf.limit_us < 1880);          /* and on the free side, said plainly */
}

TEST_CASE(it_finds_the_lower_stop_the_same_way)
{
    servo_limit_t lf;
    servo_sim_t sim;
    setup(&lf, &sim, false);

    search(&lf, &sim, 60000);
    CHECK_EQ(lf.state, SERVO_LIMIT_FOUND);
    /* The mirror of the above: last free at 1150, bound at 1140, backed off
     * by 25 in the other direction. */
    CHECK_EQ(lf.limit_us, 1175);
    CHECK(lf.limit_us > 1150);
}

/*
 * The asymmetry is the point of measuring in place: the two stops are not
 * mirror images about the servo's centre once a horn and a pushrod are
 * involved, and a finder that reported them as one number would be inventing
 * the difference away.
 */
TEST_CASE(the_two_ends_are_found_independently_and_are_not_symmetric)
{
    servo_limit_t up, down;
    servo_sim_t s1, s2;
    setup(&up, &s1, true);
    setup(&down, &s2, false);
    search(&up, &s1, 60000);
    search(&down, &s2, 60000);

    CHECK_EQ(up.state, SERVO_LIMIT_FOUND);
    CHECK_EQ(down.state, SERVO_LIMIT_FOUND);

    const int32_t out = (int32_t)up.limit_us - 1500;
    const int32_t in  = 1500 - (int32_t)down.limit_us;
    /* 1880 and 1150 about 1500: 380 out against 350 back. */
    CHECK(out != in);
}

/* A surface that really is free over the whole permitted travel is reported
 * as that, not as a limit that happens to equal the travel ceiling. */
TEST_CASE(a_surface_that_never_binds_is_reported_rather_than_guessed_at)
{
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 500;      /* nothing to hit inside the search range */
    sc.stop_hi_us = 2500;
    servo_sim_t sim;
    servo_sim_init(&sim, &sc);

    servo_limit_cfg_t lc;
    servo_limit_defaults(&lc, 1500, true);
    servo_limit_t lf;
    servo_limit_init(&lf, &lc);

    search(&lf, &sim, 120000);
    CHECK_EQ(lf.state, SERVO_LIMIT_FAULT);
    CHECK_EQ(lf.fault, SERVO_LIMIT_FAULT_NO_LIMIT);
}

/*
 * Protection one.  A servo whose stall current is above the ceiling must trip
 * it, and must trip it wherever it happens rather than at the end of a
 * measurement window.
 */
TEST_CASE(the_hard_ceiling_aborts_wherever_it_is_crossed)
{
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.bind_us = 8;           /* a stiff linkage: straight to stall */
    sc.stall_a = 6.0f;
    servo_sim_t sim;
    servo_sim_init(&sim, &sc);

    servo_limit_cfg_t lc;
    servo_limit_defaults(&lc, 1500, true);
    lc.hard_limit_a = 1.5f;
    servo_limit_t lf;
    servo_limit_init(&lf, &lc);

    search(&lf, &sim, 60000);
    CHECK_EQ(lf.state, SERVO_LIMIT_FAULT);
    CHECK_EQ(lf.fault, SERVO_LIMIT_FAULT_OVERCURRENT);
    /* And it does not leave the surface deflected against the stop. */
    CHECK_EQ(lf.cmd_us, 1500);
}

/*
 * Protection two.  Elevated current that is under the hard ceiling would
 * otherwise be held indefinitely -- it is not dramatic enough to abort and not
 * a knee the search will act on if the knee test has been set too high. That
 * combination is the one that quietly cooks a servo, so it has its own timer.
 */
TEST_CASE(elevated_current_under_the_ceiling_still_times_out)
{
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.stall_a = 1.0f;
    servo_sim_t sim;
    servo_sim_init(&sim, &sc);

    servo_limit_cfg_t lc;
    servo_limit_defaults(&lc, 1500, true);
    lc.hard_limit_a  = 5.0f;   /* never reached */
    lc.knee_ratio    = 50.0f;  /* the knee test is disabled by being absurd */
    lc.knee_margin_a = 50.0f;
    servo_limit_t lf;
    servo_limit_init(&lf, &lc);

    /*
     * With the knee blind, nothing stops the search walking into the stop and
     * staying there -- and that is precisely the configuration the stall timer
     * exists to catch, so it must fire rather than letting the search run its
     * travel out with the servo pushing the whole way.
     */
    lc.stall_above_a = 0.5f;   /* the sim stalls at 1.0 A */
    servo_limit_init(&lf, &lc);
    search(&lf, &sim, 120000);
    CHECK_EQ(lf.state, SERVO_LIMIT_FAULT);
    CHECK_EQ(lf.fault, SERVO_LIMIT_FAULT_STALL);
    CHECK_EQ(lf.cmd_us, 1500);
}

/* Ordinary work does not trip the stall timer: the whole of a normal search
 * sits below the threshold, including the step that finds the knee. */
TEST_CASE(a_normal_search_never_trips_the_stall_timer)
{
    servo_limit_t lf;
    servo_sim_t sim;
    setup(&lf, &sim, true);
    search(&lf, &sim, 60000);
    CHECK_EQ(lf.state, SERVO_LIMIT_FOUND);
    CHECK(lf.fault != SERVO_LIMIT_FAULT_STALL);
}

/* Protection three, and the one the method rests on: a step small enough that
 * the search cannot jump from free to hard against the stop. */
TEST_CASE(a_step_too_coarse_to_meet_the_knee_is_caught_by_the_ceiling)
{
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.bind_us = 5;
    servo_sim_t sim;
    servo_sim_init(&sim, &sc);

    servo_limit_cfg_t lc;
    servo_limit_defaults(&lc, 1500, true);
    lc.step_us      = 200;      /* four times the width of the knee */
    lc.hard_limit_a = 1.5f;
    servo_limit_t lf;
    servo_limit_init(&lf, &lc);

    search(&lf, &sim, 60000);
    /* Either outcome is acceptable; silently reporting a limit is not. */
    CHECK(lf.state != SERVO_LIMIT_RUNNING);
    if (lf.state == SERVO_LIMIT_FOUND) {
        T_FAIL("a 200 us step reported a limit it cannot have resolved");
    }
}

/*
 * The baseline is taken from more than one position on purpose.  A surface
 * with preload draws more at centre than a few degrees out, and a baseline
 * taken from that single reading sits high enough to hide a real knee.
 */
TEST_CASE(a_preloaded_centre_does_not_blind_the_search)
{
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    /*
     * The low stop is *above* the centre the search starts from, so the servo
     * is already pushing at its first position and stops as soon as it steps
     * away.  An earlier version of this test put the stop at 1495 -- just
     * below centre -- which loads nothing at all, and so tested the ordinary
     * case under a name that claimed otherwise.
     */
    sc.stop_lo_us = 1505;
    servo_sim_t sim;
    servo_sim_init(&sim, &sc);

    servo_limit_cfg_t lc;
    servo_limit_defaults(&lc, 1500, true);
    servo_limit_t lf;
    servo_limit_init(&lf, &lc);

    search(&lf, &sim, 60000);
    CHECK_EQ(lf.state, SERVO_LIMIT_FOUND);
    /*
     * The same 1855 as the unloaded case, which is the point: averaging the
     * loaded centre with the two free positions after it puts the baseline at
     * 0.20 A, and the knee still lands on the step past 1880.  Taking the
     * baseline from the centre alone puts it at 0.37 A, which needs 0.67 A to
     * beat -- and the first bound step only reaches 0.63, so the search walks
     * one step further into the stop and answers 1865.
     */
    CHECK_EQ(lf.limit_us, 1855);
}

/* Noise must not be mistaken for a knee: the ratio and the margin have to
 * agree, and neither alone survives a quiet sensor on a large servo. */
TEST_CASE(sensor_noise_alone_does_not_read_as_a_limit)
{
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 500;
    sc.stop_hi_us = 2500;      /* nothing to find ... */
    sc.noise_a    = 0.30f;     /* ... and a very noisy sensor */
    servo_sim_t sim;
    servo_sim_init(&sim, &sc);

    servo_limit_cfg_t lc;
    servo_limit_defaults(&lc, 1500, true);
    servo_limit_t lf;
    servo_limit_init(&lf, &lc);

    search(&lf, &sim, 120000);
    if (lf.state == SERVO_LIMIT_FOUND) {
        T_FAIL("noise at %.2f A read as a knee at %u us",
               (double)sc.noise_a, (unsigned)lf.limit_us);
    }
}

/*
 * The knee test needs the ratio as well as the margin, and this is the case
 * that says why: a large servo's free current is already several times the
 * margin, so a margin alone is met by ordinary sensor scatter. The ratio is
 * what scales the test to the servo instead of to a number chosen for a small
 * one.
 */
TEST_CASE(a_large_servos_own_noise_does_not_clear_the_margin_test)
{
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 500;
    sc.stop_hi_us = 2500;      /* again, nothing to find */
    sc.free_a     = 1.00f;     /* a big servo on a big surface */
    sc.stall_a    = 8.0f;
    sc.noise_a    = 2.00f;     /* scatter that clears 0.15 A after averaging */
    servo_sim_t sim;
    servo_sim_init(&sim, &sc);

    servo_limit_cfg_t lc;
    servo_limit_defaults(&lc, 1500, true);
    lc.hard_limit_a = 12.0f;   /* so the ceiling is not what saves it */
    servo_limit_t lf;
    servo_limit_init(&lf, &lc);

    search(&lf, &sim, 120000);
    if (lf.state == SERVO_LIMIT_FOUND) {
        T_FAIL("scatter on a %.1f A servo read as a knee at %u us",
               (double)sc.free_a, (unsigned)lf.limit_us);
    }
}

/*
 * The mirror of the case above, and why the knee test carries an absolute
 * margin as well as a ratio.
 *
 * A tight spot in a linkage is not the end of its travel. On a lightly loaded
 * surface -- 20 mA to hold it -- an extra 50 mA from a bellcrank that binds
 * slightly is nearly four times the baseline, so a ratio alone stops dead
 * there and reports it as the mechanical limit. It is 50 mA. The margin is
 * what says so.
 */
TEST_CASE(a_tight_spot_in_the_linkage_is_not_the_end_of_the_travel)
{
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.free_a      = 0.02f;    /* a small surface, barely loaded */
    sc.travel_a    = 0.05f;
    sc.stall_a     = 0.40f;
    sc.stop_hi_us  = 1880;
    sc.stiff_lo_us = 1600;     /* tight over a band well short of the stop */
    sc.stiff_hi_us = 1660;
    sc.stiff_a     = 0.05f;
    servo_sim_t sim;
    servo_sim_init(&sim, &sc);

    servo_limit_cfg_t lc;
    servo_limit_defaults(&lc, 1500, true);
    lc.hard_limit_a = 1.0f;
    servo_limit_t lf;
    servo_limit_init(&lf, &lc);

    search(&lf, &sim, 120000);
    CHECK_EQ(lf.state, SERVO_LIMIT_FOUND);
    /* Past the tight spot entirely, and stopped at the real stop instead. */
    if (lf.limit_us < 1700) {
        T_FAIL("a %.0f mA tight spot at 1600-1660 us was read as the limit "
               "(%u us)", (double)sc.stiff_a * 1000.0, (unsigned)lf.limit_us);
    }
}

/*
 * Why the search settles before it measures.
 *
 * A slow servo under load has not arrived when the step is issued, and a
 * servo that is travelling draws travel current -- which is far above its
 * free current and has nothing to do with binding. Read it as a baseline and
 * every threshold derived from it is too high, so the first genuinely bound
 * step slips under the knee test and the search walks further into the stop
 * before it notices.
 *
 * Here that costs the surface: the answer comes back on the wrong side of
 * where the linkage actually binds.
 */
TEST_CASE(a_slow_servo_is_measured_after_it_arrives_not_while_it_travels)
{
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.slew_us_per_ms = 0.12f;   /* a 10 us step takes 83 ms */
    servo_sim_t sim;
    servo_sim_init(&sim, &sc);

    servo_limit_cfg_t lc;
    servo_limit_defaults(&lc, 1500, true);
    servo_limit_t lf;
    servo_limit_init(&lf, &lc);

    search(&lf, &sim, 120000);
    CHECK_EQ(lf.state, SERVO_LIMIT_FOUND);
    /* The same answer as the fast servo: settling is what makes the servo's
     * speed stop mattering to the result. */
    CHECK_EQ(lf.limit_us, 1855);
    CHECK(lf.limit_us < 1880);
}

TEST_CASE(the_search_takes_a_workable_amount_of_time)
{
    servo_limit_t lf;
    servo_sim_t sim;
    setup(&lf, &sim, true);
    const uint32_t took = search(&lf, &sim, 120000);

    CHECK_EQ(lf.state, SERVO_LIMIT_FOUND);
    /*
     * 380 us of travel in 10 us steps at 200 ms a step is about eight
     * seconds. Slow is the point -- but a procedure nobody will sit through
     * is a procedure nobody runs, so the number is worth pinning.
     */
    CHECK(took < 12000);
}

TEST_CASE(progress_runs_from_nothing_to_all_of_it)
{
    servo_limit_t lf;
    servo_sim_t sim;
    setup(&lf, &sim, true);
    CHECK_NEAR(servo_limit_progress(&lf), 0.0f, 0.001f);
    search(&lf, &sim, 60000);
    CHECK_NEAR(servo_limit_progress(&lf), 1.0f, 0.001f);
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    servo_limit_defaults(NULL, 1500, true);
    servo_limit_init(NULL, NULL);
    servo_sim_defaults(NULL);
    servo_sim_init(NULL, NULL);
    CHECK_EQ(servo_limit_step(NULL, 0.0f, 0), 0);
    CHECK_NEAR(servo_sim_step(NULL, 1500, 0), 0.0f, 0.001f);
    CHECK_NEAR(servo_limit_progress(NULL), 0.0f, 0.001f);
}

int main(void)
{
    RUN(it_finds_the_upper_stop_and_stops_short_of_it);
    RUN(it_finds_the_lower_stop_the_same_way);
    RUN(the_two_ends_are_found_independently_and_are_not_symmetric);
    RUN(a_surface_that_never_binds_is_reported_rather_than_guessed_at);
    RUN(the_hard_ceiling_aborts_wherever_it_is_crossed);
    RUN(elevated_current_under_the_ceiling_still_times_out);
    RUN(a_normal_search_never_trips_the_stall_timer);
    RUN(a_step_too_coarse_to_meet_the_knee_is_caught_by_the_ceiling);
    RUN(a_preloaded_centre_does_not_blind_the_search);
    RUN(sensor_noise_alone_does_not_read_as_a_limit);
    RUN(a_large_servos_own_noise_does_not_clear_the_margin_test);
    RUN(a_tight_spot_in_the_linkage_is_not_the_end_of_the_travel);
    RUN(a_slow_servo_is_measured_after_it_arrives_not_while_it_travels);
    RUN(the_search_takes_a_workable_amount_of_time);
    RUN(progress_runs_from_nothing_to_all_of_it);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("servo_limit");
}
